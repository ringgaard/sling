#include "myelin/kernel/cuda.h"

#include <string>

#include "myelin/express.h"
#include "myelin/cuda/cuda-kernel.h"
#include "myelin/kernel/arithmetic.h"

namespace sling {
namespace myelin {

// CUDA PTX instruction model.
Express::Model ptx_model;

// Kernel for computing arithmetic expressions on GPU using CUDA.
class CUDACalculate : public CUDAKernel {
 public:
  // Compilation state.
  struct Compilation {
    Step *step;               // step being compiled
    PTXMacroAssembler *ptx;   // assembler for code generation
    Type dtype;               // element data type
    const char *type;         // PTX type of elements
    int size;                 // element size
    std::vector<PTXReg> reg;  // temporary registers
    PTXReg offset;            // element offset register
    PTXReg addr;              // address register
  };

  CUDACalculate(const string &name, const string &operation, int arity = -1)
      : name_(name), operation_(operation), arity_(arity) {}

  string Name() override { return name_; }
  string Operation() override { return operation_; }

  bool Supports(Step *step) override {
    // Requires CUDA support.
    if (!CUDAKernel::Supports(step)) return false;

    // Check that operation is compatible.
    if (step->type() != operation_) return false;
    if (arity_ != -1 && step->indegree() != arity_) return false;

    // Check that inputs and outputs have the compatible types and shapes.
    if (step->indegree() < 1 || step->outdegree() < 1) return false;
    Type type = step->output(0)->type();
    const Shape &shape = step->output(0)->shape();
    for (auto *input : step->inputs()) {
      if (input->type() != type) return false;
      if (!input->Compatible(step->output(0))) return false;
      // NB: broadcasting not yet supported.
      if (input->elements() != shape.elements()) return false;
    }
    for (auto *output : step->outputs()) {
      if (output->type() != type) return false;
      if (output->shape() != shape) return false;
      // NB: broadcasting not yet supported.
      if (output->elements() != shape.elements()) return false;
    }

    // Check that element type is supported by CUDA.
    if (TypeTraits::of(type).ptx() == nullptr) return false;

    // Dense encoding required.
    for (auto *input : step->inputs()) input->RequireDense();
    for (auto *output : step->outputs()) output->RequireDense();

    return true;
  }

  void Adjust(Step *step) override {
    // Inputs and ouputs must be in standard format.
   for (auto *input : step->inputs()) {
      input->RequireDense();
      input->RequireStandardOrder();
    }
    for (auto *output : step->outputs()) {
      output->RequireDense();
      output->RequireStandardOrder();
    }

    // Enable sharing of inputs and outputs.
    for (int i = 0; i < step->indegree(); ++i) {
      for (int j = 0; j < step->outdegree(); ++j) {
        if (step->input(i)->shape() == step->output(j)->shape()) {
          if (step->AllowInPlace(i, j)) break;
        }
      }
    }
  }

  void GeneratePTX(Step *step, PTXMacroAssembler *ptx)  override {
    // Parse expression for evaluation.
    Express expr(Express::NVIDIA);
    InitExpression(step, &expr, true);

    // Set grid size. Use one thread for each element.
    Tensor *output = step->output(0);
    int size = output->elements();
    ptx->set_grid_dims(size);

    // Set up compilation state.
    Compilation comp;
    comp.step = step;
    comp.ptx = ptx;

    // Get element type.
    comp.dtype = output->type();
    const TypeTraits &traits = TypeTraits::of(comp.dtype);
    comp.type = traits.ptx();
    comp.size = traits.size();

    // Optimize expression.
    expr.EliminateCommonSubexpressions();
    expr.FuseMulAdd();
    expr.CacheResults();

    // Rewrite expression.
    Express instrs;
    CHECK(expr.Rewrite(ptx_model, &instrs));
    instrs.ComputeLiveRanges();
    int regs = instrs.AllocateRegisters();

    // Get grid location.
    ptx_decl(b32, blkdim);
    ptx_decl(b32, blkidx);
    ptx_decl(b32, thridx);
    ptx_emit(mov.u32, blkdim, PTXLiteral("%ntid.x"));
    ptx_emit(mov.u32, thridx, PTXLiteral("%ctaid.x"));
    ptx_emit(mov.u32, blkidx, PTXLiteral("%tid.x"));
    ptx_decl(b32, idx);
    ptx_emit(mad.lo.u32, idx, thridx, blkdim, blkidx);

    // Check bounds.
    ptx_decl(pred, outside);
    ptx_emit(setp.ge.u32, outside, idx, PTXImm(size));
    ptx_if(outside);
    ptx_emit(bra, PTXLabel("done"));
    ptx_endif();

    // Compute element offset.
    ptx_decl(b64, offset);
    comp.offset = offset;
    ptx_emit(mul.wide.u32, offset, idx, PTXImm(comp.size));
    ptx_decl(b64, addr);
    comp.addr = addr;

    // Allocate registers.
    comp.reg.resize(regs);
    std::vector<PTXReg> reg(regs);
    for (int i = 0; i < regs; ++i) {
      comp.reg[i] = ptx->reg(comp.type, "r", i);
    }

    // Generate code for each instruction in expression.
    for (auto *instr : instrs.ops()) {
      if (instr->nop()) continue;
      //LOG(INFO) << "  " << instr->AsInstruction();
      switch (instr->type) {
        case Express::MOV:
          if (instr->dst != -1 && instr->src != -1) {
            ptx->emit(PTXInstr("mov", comp.type),
                      comp.reg[instr->dst],
                      comp.reg[instr->src]);
          } else if (instr->dst != -1) {
            GenerateLoad(instr, &comp);
          } else {
            GenerateStore(instr, &comp);
          }
          break;
        case Express::ADD:
          GenerateBinaryOp("add", instr, &comp);
          break;
        case Express::SUB:
          GenerateBinaryOp("sub", instr, &comp);
          break;
        case Express::MUL:
          if (IsFloat(comp.dtype)) {
            GenerateBinaryOp("mul", instr, &comp);
          } else {
            GenerateBinaryOp("mul.lo", instr, &comp);
          }
          break;
        case Express::DIV:
          if (IsFloat(comp.dtype)) {
            GenerateBinaryOp("div.approx", instr, &comp);
          } else {
            GenerateBinaryOp("div", instr, &comp);
          }
          break;
        case Express::MIN:
          GenerateBinaryOp("min", instr, &comp);
          break;
        case Express::MAX:
          GenerateBinaryOp("max", instr, &comp);
          break;
        case Express::NEG:
          GenerateUnaryOp("neg", instr, &comp);
          break;
        case Express::ABS:
          GenerateUnaryOp("abs", instr, &comp);
          break;
        case Express::RECIPROCAL:
          GenerateUnaryOp("rcp.approx", instr, &comp);
          break;
        case Express::LOG2:
          GenerateUnaryOp("lg2.approx", instr, &comp);
          break;
        case Express::EXP2:
          GenerateUnaryOp("ex2.approx", instr, &comp);
          break;
        default:
          LOG(FATAL) << "Instruction not supported in CUDA: "
                     <<  instr->AsInstruction();
      }
    }

    // Done.
    ptx_label(done);
    ptx_ret();
  }

  int64 Complexity(const Step *step) override {
    Express expr(Express::NVIDIA);
    InitExpression(step, &expr, true);
    Tensor *output = step->output(0);
    return output->shape().elements() * expr.Complexity();
  }

  void GenerateLoad(Express::Op *instr, Compilation *comp) {
    PTXMacroAssembler *ptx = comp->ptx;
    CHECK_EQ(instr->arity(), 1);
    CHECK_EQ(instr->result->type, Express::TEMP);
    PTXReg &dst = comp->reg[instr->dst];
    switch (instr->args[0]->type) {
      case Express::INPUT: {
        // mov reg, [ptr].
        Tensor *input = comp->step->input(instr->args[0]->id);
        if (input->IsConstant()) {
          // Load from constant tensor.
          ptx->emit(PTXInstr("ld.global", comp->type), dst,
                    PTXAddr(input->device_data(), comp->offset));
        } else if (input->ref()) {
          // Load from reference tensor.
          ptx->emit("ld.global.u64", comp->addr,
                    PTXAddr(ptx->data(), input->device_offset()));
          ptx->emit("add.u64", comp->addr, comp->addr, comp->offset);
          ptx->emit(PTXInstr("ld.global", comp->type), dst,
                    PTXAddr(comp->addr));
        } else {
          // Load from instance tensor.
          ptx->emit("add.u64", comp->addr, ptx->data(), comp->offset);
          ptx->emit(PTXInstr("ld.global", comp->type), dst,
                    PTXAddr(comp->addr, input->device_offset()));
        }
      }
      break;

      case Express::NUMBER:
        // mov reg, imm.
        if (IsFloat(comp->dtype)) {
          ptx->emit(PTXInstr("mov", comp->type), dst,
                    PTXFloat(Express::NumericFlt32(instr->args[0]->id)));
        } else {
          ptx->emit(PTXInstr("mov", comp->type), dst,
                    PTXImm(Express::NumericFlt32(instr->args[0]->id)));
        }
        break;

      default:
        LOG(FATAL) << "Unsupported: " << instr->AsInstruction();
    }
  }

  void GenerateStore(Express::Op *instr, Compilation *comp) {
    PTXMacroAssembler *ptx = comp->ptx;
    CHECK_EQ(instr->arity(), 1);
    CHECK_EQ(instr->args[0]->type, Express::TEMP);
    CHECK_EQ(instr->result->type, Express::OUTPUT);
    PTXReg &src = comp->reg[instr->src];
    Tensor *output = comp->step->output(instr->result->id);
    CHECK(!output->IsConstant());
    if (output->ref()) {
      // Save to reference tensor.
      ptx->emit("ld.global.u64", comp->addr,
                PTXAddr(ptx->data(), output->device_offset()));
      ptx->emit("add.u64", comp->addr, comp->addr, comp->offset);
      ptx->emit(PTXInstr("st.global", comp->type),
                PTXAddr(comp->addr), src);
    } else {
      // Save to instance tensor.
      ptx->emit("add.u64", comp->addr, ptx->data(), comp->offset);
      ptx->emit(PTXInstr("st.global", comp->type),
                PTXAddr(comp->addr, output->device_offset()), src);
    }
  }

  void GenerateBinaryOp(const char *op, Express::Op *instr, Compilation *comp) {
    CHECK_EQ(instr->arity(), 2);
    CHECK_EQ(instr->result->type, Express::TEMP);
    CHECK_EQ(instr->args[0]->type, Express::TEMP);
    switch (instr->args[1]->type) {
      case Express::TEMP:
        // op reg,reg,reg.
        comp->ptx->emit(PTXInstr(op, comp->type),
                        comp->reg[instr->dst],
                        comp->reg[instr->src],
                        comp->reg[instr->src2]);
        break;

      case Express::NUMBER:
        // op reg,reg,imm.
        if (IsFloat(comp->dtype)) {
          comp->ptx->emit(PTXInstr(op, comp->type),
                          comp->reg[instr->dst],
                          comp->reg[instr->src],
                          PTXFloat(Express::NumericFlt32(instr->args[1]->id)));
        } else {
          comp->ptx->emit(PTXInstr(op, comp->type),
                          comp->reg[instr->dst],
                          comp->reg[instr->src],
                          PTXImm(Express::NumericFlt32(instr->args[1]->id)));
        }
        break;

      default:
        LOG(FATAL) << "Unsupported: " << instr->AsInstruction();
    }
  }

  void GenerateUnaryOp(const char *op, Express::Op *instr, Compilation *comp) {
    CHECK_EQ(instr->arity(), 1);
    CHECK_EQ(instr->result->type, Express::TEMP);
    CHECK_NE(instr->dst, -1);
    switch (instr->args[0]->type) {
      case Express::TEMP:
        // op reg, reg.
        comp->ptx->emit(PTXInstr(op, comp->type),
                        comp->reg[instr->dst],
                        comp->reg[instr->src]);
        break;

      case Express::NUMBER:
        // op reg, imm.
        if (IsFloat(comp->dtype)) {
          comp->ptx->emit(PTXInstr(op, comp->type),
                          comp->reg[instr->dst],
                          PTXFloat(Express::NumericFlt32(instr->args[0]->id)));
        } else {
          comp->ptx->emit(PTXInstr(op, comp->type),
                          comp->reg[instr->dst],
                          PTXImm(Express::NumericFlt32(instr->args[0]->id)));
        }
        break;

      default:
        LOG(FATAL) << "Unsupported: " << instr->AsInstruction();
    }
  }

  static bool IsFloat(Type type) {
    return type == DT_FLOAT || type == DT_DOUBLE || type == DT_HALF;
  }

 private:
  const string name_;       // kernel name
  const string operation_;  // kernel operation
  int arity_;               // number of inputs
};

// Register CUDA arithmetic library.
void RegisterCUDAArithmeticLibrary(Library *library) {
  ptx_model.mov_reg_reg = true;
  ptx_model.mov_reg_imm = true;
  ptx_model.mov_reg_mem = true;
  ptx_model.mov_mem_reg = true;
  ptx_model.op_reg_reg = true;
  ptx_model.op_reg_imm = true;
  ptx_model.op_reg_reg_reg = true;
  ptx_model.op_reg_reg_imm = true;
  ptx_model.func_reg_reg = true;
  ptx_model.func_reg_imm = true;
  ptx_model.fm_reg_reg_reg = true;
  ptx_model.fm_reg_reg_imm = true;

  library->Register(new CUDACalculate("CUDAAdd", "Add", 2));
  library->Register(new CUDACalculate("CUDASub", "Sub", 2));
  library->Register(new CUDACalculate("CUDAMul", "Mul", 2));
  library->Register(new CUDACalculate("CUDADiv", "Div", 2));
  library->Register(new CUDACalculate("CUDAMax", "Maximum", 2));
  library->Register(new CUDACalculate("CUDAMin", "Minimum", 2));

  library->Register(new CUDACalculate("CUDALog", "Log", 1));
  library->Register(new CUDACalculate("CUDAExp", "Exp", 1));
  library->Register(new CUDACalculate("CUDASigmoid", "Sigmoid", 1));
  library->Register(new CUDACalculate("CUDATanh", "Tanh", 1));
  library->Register(new CUDACalculate("CUDACalculate", "Calculate"));

  library->Register(new CUDACalculate("CUDANegate", "Negate", 1));
  library->Register(new CUDACalculate("CUDAAbs", "Abs", 1));
  library->Register(new CUDACalculate("CUDARelu", "Relu", 1));
  library->Register(new CUDACalculate("CUDASoftsign", "Softsign", 1));
  library->Register(new CUDACalculate("CUDASoftplus", "Softplus", 1));
  library->Register(new CUDACalculate("CUDALogSigmoid", "LogSigmoid", 1));
  library->Register(new CUDACalculate("CUDAReciprocal", "Reciprocal", 1));
  library->Register(new CUDACalculate("CUDASquare", "Square", 1));
}

}  // namespace myelin
}  // namespace sling

