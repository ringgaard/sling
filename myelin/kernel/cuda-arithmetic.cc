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
    }
    for (auto *output : step->outputs()) {
      if (output->type() != type) return false;
      if (output->shape() != shape) return false;
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
    ptx->set_grid_dim(0, size);

    // Get element type.
    const char *type = TypeTraits::of(output->type()).ptx();
    LOG(INFO) << "type " << type;

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
    ptx_emit(mad_lo_s32, idx, thridx, blkdim, blkidx);

    // Check bounds.
    ptx_decl(pred, outside);
    ptx_emit(setp_ge_s32, outside, idx, PTXImm(size));
    ptx_if(outside);
    ptx_emit(bra, PTXLabel("done"));
    ptx_endif();

    // Allocate registers.
    std::vector<PTXReg> reg(regs);
    for (int i = 0; i < regs; ++i) {
      reg[i] = ptx->reg(type, "r", i);
    }

    // Generate code for each instruction in expression.
    // TODO: generate code
    LOG(INFO) << regs << " registers";
    for (auto *instr : instrs.ops()) {
      if (!instr->nop()) LOG(INFO) << "  " << instr->AsInstruction();
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

  library->Register(new CUDACalculate("CUDAAddExpr", "Add", 2));
  library->Register(new CUDACalculate("CUDASubExpr", "Sub", 2));
  library->Register(new CUDACalculate("CUDAMulExpr", "Mul", 2));
  library->Register(new CUDACalculate("CUDADivExpr", "Div", 2));
  library->Register(new CUDACalculate("CUDAMaxExpr", "Maximum", 2));
  library->Register(new CUDACalculate("CUDAMinExpr", "Minimum", 2));

  library->Register(new CUDACalculate("CUDALogExpr", "Log", 1));
  library->Register(new CUDACalculate("CUDAExpExpr", "Exp", 1));
  library->Register(new CUDACalculate("CUDASigmoidExpr", "Sigmoid", 1));
  library->Register(new CUDACalculate("CUDATanhExpr", "Tanh", 1));
  library->Register(new CUDACalculate("CUDACalculate", "Calculate"));

  library->Register(new CUDACalculate("CUDANegateExpr", "Negate", 1));
  library->Register(new CUDACalculate("CUDAAbsExpr", "Abs", 1));
  library->Register(new CUDACalculate("CUDAReluExpr", "Relu", 1));
  library->Register(new CUDACalculate("CUDASoftsignExpr", "Softsign", 1));
  library->Register(new CUDACalculate("CUDASoftplusExpr", "Softplus", 1));
  library->Register(new CUDACalculate("CUDALogSigmoidExpr", "LogSigmoid", 1));
  library->Register(new CUDACalculate("CUDAReciprocalExpr", "Reciprocal", 1));
  library->Register(new CUDACalculate("CUDASquareExpr", "Square", 1));
}

}  // namespace myelin
}  // namespace sling

