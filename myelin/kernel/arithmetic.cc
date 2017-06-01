#include "myelin/kernel/arithmetic.h"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/logging.h"
#include "base/types.h"
#include "myelin/compute.h"
#include "myelin/expression.h"
#include "myelin/macro-assembler.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Mapping from flow variables to expression variables.
typedef std::map<Flow::Variable *, Expression::Var *> VarMap;

// Error handler for unsupported operations.
static void Unsupported(const char *file, int line) {
  LOG(FATAL) << "Unsupported operation (" << file << " line " << line << ")";
}

#define UNSUPPORTED Unsupported(__FILE__, __LINE__);

// Convert operation type to expression op.
static Expression::OpType OpType(Flow::Operation *op) {
  // Operations that can be fused into Calculate operations.
  static std::unordered_map<string, Expression::OpType> ops {
    {"Add", Expression::ADD},
    {"BiasAdd", Expression::ADD},
    {"Sub", Expression::SUB},
    {"Mul", Expression::MUL},
    {"Div", Expression::DIV},
    {"Minimum", Expression::MIN},
    {"Maximum", Expression::MAX},
    {"Relu", Expression::RELU},
  };

  auto f = ops.find(op->type);
  return f == ops.end() ? Expression::INVALID : f->second;
}

// Check if operation is a candidate for Calculate ops.
static bool IsCalculateOp(Flow::Operation *op) {
  return op->type == "Calculate" || OpType(op) != Expression::INVALID;
}

// Initialize expression for flow operation.
static void InitExpression(Flow::Operation *op, Expression *expr) {
  if (op->type == "Calculate") {
    // Build expression from expression recipe attribute on op.
    const string &recipe = op->GetAttr("expr");
    if (!recipe.empty()) expr->Parse(recipe);
  } else {
    // Add op with inputs and outputs.
    CHECK_EQ(op->outputs.size(), 1);
    Expression::Op *func = expr->Operation(OpType(op));
    for (int i = 0; i < op->inputs.size(); ++i) {
      func->AddArgument(expr->Variable(Expression::INPUT, i));
    }
    func->Assign(expr->Variable(Expression::OUTPUT, 0));
  }
}

// Build mapping from flow variables to expression variables.
static void MapVars(Flow::Operation *op, Expression *expr, VarMap *varmap) {
  // Map input variables.
  for (int i = 0; i < op->inputs.size(); ++i) {
    (*varmap)[op->inputs[i]] = expr->Variable(Expression::INPUT, i);
  }

  // Map output variables.
  for (int i = 0; i < op->outputs.size(); ++i) {
    (*varmap)[op->outputs[i]] = expr->Variable(Expression::OUTPUT, i);
  }
}

// Combine arithmetic operators into expressions that can be computed by a
// Calculate kernel.
class ExpressionTransformer : public Transformer {
 public:
  bool Transform(Flow *flow) override {
    // Make list of ops that can potentially be included in Calculate ops.
    std::vector<Flow::Operation *> candidates;
    for (Flow::Operation *op : flow->ops()) {
      if (IsCalculateOp(op)) {
        candidates.push_back(op);
      }
    }

    // Find candidate pairs to merge into combined Calculate ops.
    bool again = true;
    int num_combines = 0;
    while (again) {
      again = false;
      for (int i = 0; i < candidates.size(); ++i) {
        Flow::Operation *op = candidates[i];
        if (op == nullptr) continue;

        // Check if producer of one of the inputs is also a candidate.
        for (auto *input : op->inputs) {
          if (input->producer != nullptr && IsCalculateOp(input->producer)) {
            // Try to combine op with producer.
            if (Combine(flow, input->producer, op)) {
              // Remove op from candidate list and try again.
              candidates[i] = nullptr;
              num_combines++;
              again = true;
              break;
            }
          }
        }
      }
    }
    VLOG(3) << num_combines << " of " << candidates.size() << " ops combined";

    return false;
  }

  bool Combine(Flow *flow, Flow::Operation *first, Flow::Operation *second) {
    // Check for indirect dependencies between ops.
    for (auto *v : second->inputs) {
      if (v->producer != first && v->DependsOn(first)) return false;
    }

    // Compute fused expression.
    string fused_recipe = FuseExpressions(first, second);

    // Fuse the two ops and set expression recipe for the fused Calculate op.
    Flow::Operation *fused = flow->Fuse(first, second, "Calculate", true);
    fused->SetAttr("expr", fused_recipe);

    return true;
  }

  string FuseExpressions(Flow::Operation *first, Flow::Operation *second) {
    // Build first expression.
    Expression expr1;
    InitExpression(first, &expr1);
    VarMap vars1;
    MapVars(first, &expr1, &vars1);

    // Build second expression.
    Expression expr2;
    InitExpression(second, &expr2);
    VarMap vars2;
    MapVars(second, &expr2, &vars2);

    // Build expression variable mapping for mapping variables in the second
    // expression to variables in the first expression.
    Expression::Map mapping;
    int next_input = first->inputs.size();
    int next_output = first->outputs.size();
    for (Flow::Variable *v : second->inputs) {
      if (first->IsInput(v)) {
        // Map input from second op to input from first op.
        mapping[vars2[v]] = vars1[v];
      } else if (first->IsOutput(v)) {
        if (v->consumers.size() == 1) {
          // Second op is the only consumer of the output from the first op,
          // so it can be turned into a temporary variable.
          vars1[v]->type = Expression::TEMP;
          next_output--;

          // Adjust numbering of output variables from the first op.
          for (auto *o : expr1.vars()) {
            if (o->type == Expression::OUTPUT && o->id > vars1[v]->id) {
              o->id--;
            }
          }
        }

        // Map input from second op to output from first op.
        mapping[vars2[v]] = vars1[v];
      } else {
        // Map input from second op to a new input in the merged expression.
        mapping[vars2[v]] = expr1.Variable(Expression::INPUT, next_input++);
      }
    }
    for (Flow::Variable *v : second->outputs) {
      // Map output from second op to a new output in the merged expression.
      mapping[vars2[v]] = expr1.Variable(Expression::OUTPUT, next_output++);
    }
    expr2.CompactTempVars();

    // Merge second expression into the first one.
    expr1.Merge(&expr2, mapping);

    // Eliminate common subexpressions.
    expr1.EliminateCommonSubexpressions();

    // Return merged recipe.
    return expr1.AsRecipe();
  }
};

// Replace ops with constant input variables with new computed constant
// variables.
class ConstantFolding : public Transformer {
 public:
  bool Transform(Flow *flow) override {
    for (Flow::Operation *op : flow->ops()) {
      // Check if all inputs are constants.
      bool constant = true;
      for (auto *input : op->inputs) {
        if (input->data == nullptr) {
          constant = false;
          break;
        }
      }
      if (constant) {
        // TODO: compute op and replace with new constant variable.
        VLOG(3) << "Constant op " << op->type << " "
                << op->outputs[0]->TypeString() << " " << op->name;
      }
    }
    return false;
  }
};

class LoopGenerator {
 public:
  LoopGenerator(Step *step, MacroAssembler *masm, int vecsize) {
    // Set up loop to iterate over all the output in vector-sized increments.
    vecsize_ = vecsize;
    size_ = step->output(0)->size();
    single_ = size_ <= vecsize_;

    // Allocate register for offset.
    Registers &rr = masm->rr();
    if (!single_) ofs_ = rr.alloc();
    instance_ = masm->instance();

    // Allocate iterators for all inputs and outputs.
    input_.resize(step->indegree());
    for (int i = 0; i < step->indegree(); ++i) {
      Tensor *var = step->input(i);
      input_[i].var = var;
      if (var->offset() == -1 || var->ref()) {
        input_[i].base = rr.alloc();
        __ LoadTensorAddress(input_[i].base, var);
      }
    }
    output_.resize(step->outdegree());
    for (int i = 0; i < step->outdegree(); ++i) {
      Tensor *var = step->output(i);
      output_[i].var = var;
      if (var->offset() == -1 || var->ref()) {
        output_[i].base = rr.alloc();
        __ LoadTensorAddress(output_[i].base, var);
      }
    }
  }

  void begin(MacroAssembler *masm) {
    if (!single_) {
      __ xorq(ofs_, ofs_);
      __ bind(&begin_);
    }
  }

  void end(MacroAssembler *masm) {
    if (!single_) {
      __ addq(ofs_, Immediate(vecsize_));
      __ cmpq(ofs_, Immediate(size_));
      __ j(less, &begin_);
    }
  }

  Operand addr(Expression::Var *var) {
    CHECK(valid(var));
    Iterator &it =
        var->type == Expression::OUTPUT ? output_[var->id] : input_[var->id];
    if (single_) {
      if (it.base.is_valid()) {
        return Operand(it.base);
      } else {
        return Operand(instance_, it.var->offset());
      }
    } else {
      if (it.base.is_valid()) {
        return Operand(it.base, ofs_);
      } else {
        return Operand(instance_, ofs_, times_1, it.var->offset());
      }
    }
  }

  bool valid(Expression::Var *var) {
    if (var->type == Expression::OUTPUT) {
      return var->id >= 0 && var->id < output_.size();
    } else {
      return var->id >= 0 && var->id < input_.size();
    }
  }

 private:
  // Iterator for looping over (vector) elements in tensor.
  struct Iterator {
    Tensor *var;              // tensor that is being iterated
    Register base = no_reg;   // base register for tensor
  };

  // Vector size.
  int vecsize_;

  // Output size.
  int size_;

  // Loop begin label.
  Label begin_;

  // Instance pointer register.
  Register instance_;

  // Main loop register.
  Register ofs_;

  // Whether only one iteration is needed.
  bool single_;

  // Input and output iterators.
  std::vector<Iterator> input_;
  std::vector<Iterator> output_;
};

// Kernel for computing arithmetic expressions.
class Calculate : public Kernel {
 public:
  // Register sizes in bytes.
  const static int XMMRegSize = 16;
  const static int YMMRegSize = 32;

  // Assembler instruction methods for different instruction formats.
  typedef void (Assembler::*XMMRegReg)(XMMRegister,
                                       XMMRegister);
  typedef void (Assembler::*XMMRegMem)(XMMRegister,
                                       const Operand &);
  typedef void (Assembler::*XMMRegRegReg)(XMMRegister,
                                          XMMRegister,
                                          XMMRegister);
  typedef void (Assembler::*XMMRegRegMem)(XMMRegister,
                                          XMMRegister,
                                          const Operand &);
  typedef void (Assembler::*YMMRegRegReg)(YMMRegister,
                                          YMMRegister,
                                          YMMRegister);
  typedef void (Assembler::*YMMRegRegMem)(YMMRegister,
                                          YMMRegister,
                                          const Operand &);

  string Name() override { return "Calculate"; }
  string Operation() override { return "Calculate"; }

  bool Supports(Step *step) override {
    return true;
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Determine output type and shape from the first output.
    Type type = step->output(0)->type();
    const Shape &shape = step->output(0)->shape();
    int elements = shape.elements();

    // Compile expression to be computed.
    Expression expr;
    expr.Parse(step->GetAttr("expr"));
    expr.EliminateCommonSubexpressions();
    if (CPU::Enabled(AVX) && CPU::Enabled(FMA3)) {
      expr.FuseMulAdd();
      expr.FuseMulSub();
    }
    expr.CacheResults();

    // Determine which generator to use.
    switch (type) {
      case DT_FLOAT:
        if (CPU::Enabled(AVX)) {
          if (elements > 1 && elements % 8 == 0) {
            GenerateVectorFltAVX256(step, expr, masm);
          } else if (elements > 1 && elements % 4 == 0) {
            GenerateVectorFltAVX128(step, expr, masm);
          } else {
            GenerateScalarFltAVX(step, expr, masm);
          }
        } else if (CPU::Enabled(SSE)) {
          if (elements > 1 && elements % 4 == 0) {
            GenerateVectorFltSSE(step, expr, masm);
          } else {
            GenerateScalarFltSSE(step, expr, masm);
          }
        } else {
          LOG(FATAL) << "No generator for float expression";
        }
        break;

      case DT_DOUBLE:
        if (CPU::Enabled(AVX)) {
          if (elements > 1 && elements % 4 == 0) {
            GenerateVectorFltAVX256(step, expr, masm);
          } else if (elements > 1 && elements % 2 == 0) {
            GenerateVectorFltAVX128(step, expr, masm);
          } else {
            GenerateScalarFltAVX(step, expr, masm);
          }
        } else if (CPU::Enabled(SSE)) {
          if (CPU::Enabled(SSE2) && elements > 1 && elements % 2 == 0) {
            GenerateVectorFltSSE(step, expr, masm);
          } else {
            GenerateScalarFltSSE(step, expr, masm);
          }
        } else {
          LOG(FATAL) << "No generator for float expression";
        }
        break;

      default:
       LOG(FATAL) << "No generator for expression";
    }
  }

  // Generate XMM scalar float move.
  void GenerateScalarFltMove(
      Type type, Expression::Op *i,
      std::vector<XMMRegister> &reg, LoopGenerator &loop,
      MacroAssembler *masm) {
    if (i->dst != -1 && i->src != -1) {
      // MOV reg,reg
      switch (type) {
        case DT_FLOAT:
          __ movss(reg[i->dst], reg[i->src]);
          break;
        case DT_DOUBLE:
          __ movsd(reg[i->dst], reg[i->src]);
          break;
        default: UNSUPPORTED;
      }
    } else if (i->dst != -1 && i->src == -1) {
      // MOV reg,[mem]
      switch (type) {
        case DT_FLOAT:
          __ movss(reg[i->dst], loop.addr(i->args[0]));
          break;
        case DT_DOUBLE:
          __ movsd(reg[i->dst], loop.addr(i->args[0]));
          break;
        default: UNSUPPORTED;
      }
    } else if (i->dst == -1 && i->src != -1) {
      // MOV [mem],reg
      switch (type) {
        case DT_FLOAT:
          __ movss(loop.addr(i->result), reg[i->src]);
          break;
        case DT_DOUBLE:
          __ movsd(loop.addr(i->result), reg[i->src]);
          break;
        default: UNSUPPORTED;
      }
    } else {
      UNSUPPORTED;
    }
  }

  // Generate XMM vector float move.
  void GenerateVectorFltMove(
      Type type, Expression::Op *i,
      std::vector<XMMRegister> &reg, LoopGenerator &loop,
      MacroAssembler *masm) {
    if (i->dst != -1 && i->src != -1) {
      // MOV reg,reg
      switch (type) {
        case DT_FLOAT:
          if (CPU::Enabled(AVX)) {
            __ vmovaps(reg[i->dst], reg[i->src]);
          } else {
            __ movaps(reg[i->dst], reg[i->src]);
          }
          break;
        case DT_DOUBLE:
          if (CPU::Enabled(AVX)) {
            __ vmovapd(reg[i->dst], reg[i->src]);
          } else if (CPU::Enabled(SSE2)) {
            __ movapd(reg[i->dst], reg[i->src]);
          } else {
            UNSUPPORTED;
          }
          break;
        default: UNSUPPORTED;
      }
    } else if (i->dst != -1 && i->src == -1) {
      // MOV reg,[mem]
      switch (type) {
        case DT_FLOAT:
          if (CPU::Enabled(AVX)) {
            __ vmovaps(reg[i->dst], loop.addr(i->args[0]));
          } else {
            __ movaps(reg[i->dst], loop.addr(i->args[0]));
          }
          break;
        case DT_DOUBLE:
          if (CPU::Enabled(AVX)) {
            __ vmovapd(reg[i->dst], loop.addr(i->args[0]));
          } else if (CPU::Enabled(SSE2)) {
            __ movapd(reg[i->dst], loop.addr(i->args[0]));
          } else {
            UNSUPPORTED;
          }
          break;
        default: UNSUPPORTED;
      }
    } else if (i->dst == -1 && i->src != -1) {
      // MOV [mem],reg
      switch (type) {
        case DT_FLOAT:
          if (CPU::Enabled(AVX)) {
            __ vmovaps(loop.addr(i->result), reg[i->src]);
          } else {
            __ movaps(loop.addr(i->result), reg[i->src]);
          }
          break;
        case DT_DOUBLE:
          if (CPU::Enabled(AVX)) {
            __ vmovapd(loop.addr(i->result), reg[i->src]);
          } else if (CPU::Enabled(SSE2)) {
            __ movapd(loop.addr(i->result), reg[i->src]);
          } else {
            UNSUPPORTED;
          }
          break;
        default: UNSUPPORTED;
      }
    } else {
      UNSUPPORTED;
    }
  }

  // Generate YMM vector float move.
  void GenerateVectorFltMove(
      Type type, Expression::Op *i,
      std::vector<YMMRegister> &reg, LoopGenerator &loop,
      MacroAssembler *masm) {
    if (i->dst != -1 && i->src != -1) {
      // MOV reg,reg
      switch (type) {
        case DT_FLOAT:
          __ vmovaps(reg[i->dst], reg[i->src]);
          break;
        case DT_DOUBLE:
          __ vmovapd(reg[i->dst], reg[i->src]);
          break;
        default: UNSUPPORTED;
      }
    } else if (i->dst != -1 && i->src == -1) {
      // MOV reg,[mem]
      switch (type) {
        case DT_FLOAT:
          __ vmovaps(reg[i->dst], loop.addr(i->args[0]));
          break;
        case DT_DOUBLE:
          __ vmovapd(reg[i->dst], loop.addr(i->args[0]));
          break;
        default: UNSUPPORTED;
      }
    } else if (i->dst == -1 && i->src != -1) {
      // MOV [mem],reg
      switch (type) {
        case DT_FLOAT:
          __ vmovaps(loop.addr(i->result), reg[i->src]);
          break;
        case DT_DOUBLE:
          __ vmovapd(loop.addr(i->result), reg[i->src]);
          break;
        default: UNSUPPORTED;
      }
    } else {
      UNSUPPORTED;
    }
  }

  // Generate two-operand XMM float op.
  void GenerateXMMFltOp(
      Type type, Expression::Op *i,
      XMMRegReg fltopreg, XMMRegReg dblopreg,
      XMMRegMem fltopmem, XMMRegMem dblopmem,
      std::vector<XMMRegister> &reg, LoopGenerator &loop,
      MacroAssembler *masm) {
    if (i->dst != -1 && i->src != -1) {
      // OP reg,reg
      switch (type) {
        case DT_FLOAT:
          (masm->*fltopreg)(reg[i->dst], reg[i->src]);
          break;
        case DT_DOUBLE:
          (masm->*dblopreg)(reg[i->dst], reg[i->src]);
          break;
        default: UNSUPPORTED;
      }
    } else if (i->dst != -1 && i->src == -1) {
      // OP reg,[mem]
      switch (type) {
        case DT_FLOAT:
          (masm->*fltopmem)(reg[i->dst], loop.addr(i->args[1]));
          break;
        case DT_DOUBLE:
          (masm->*dblopmem)(reg[i->dst], loop.addr(i->args[1]));
          break;
        default: UNSUPPORTED;
      }
    } else {
      UNSUPPORTED;
    }
  }

  // Generate three-operand XMM float op.
  void GenerateXMMFltOp(
      Type type, Expression::Op *i,
      XMMRegRegReg fltopreg, XMMRegRegReg dblopreg,
      XMMRegRegMem fltopmem, XMMRegRegMem dblopmem,
      std::vector<XMMRegister> &reg, LoopGenerator &loop,
      MacroAssembler *masm) {
    if (i->dst != -1 && i->src != -1 && i->src2 != -1) {
      // OP reg,reg,reg
      switch (type) {
        case DT_FLOAT:
          (masm->*fltopreg)(reg[i->dst], reg[i->src], reg[i->src2]);
          break;
        case DT_DOUBLE:
          (masm->*dblopreg)(reg[i->dst], reg[i->src], reg[i->src2]);
          break;
        default: UNSUPPORTED;
      }
    } else if (i->dst != -1 && i->src != -1 && i->src2 == -1) {
      // OP reg,reg,[mem]
      switch (type) {
        case DT_FLOAT:
          (masm->*fltopmem)(reg[i->dst], reg[i->src], loop.addr(i->args[1]));
          break;
        case DT_DOUBLE:
          (masm->*dblopmem)(reg[i->dst], reg[i->src], loop.addr(i->args[1]));
          break;
        default: UNSUPPORTED;
      }
    } else {
      UNSUPPORTED;
    }
  }

  // Generate three-operand YMM float op.
  void GenerateYMMFltOp(
      Type type, Expression::Op *i,
      YMMRegRegReg fltopreg, YMMRegRegReg dblopreg,
      YMMRegRegMem fltopmem, YMMRegRegMem dblopmem,
      std::vector<YMMRegister> &reg, LoopGenerator &loop,
      MacroAssembler *masm) {
    if (i->dst != -1 && i->src != -1 && i->src2 != -1) {
      // OP reg,reg,reg
      switch (type) {
        case DT_FLOAT:
          (masm->*fltopreg)(reg[i->dst], reg[i->src], reg[i->src2]);
          break;
        case DT_DOUBLE:
          (masm->*dblopreg)(reg[i->dst], reg[i->src], reg[i->src2]);
          break;
        default: UNSUPPORTED;
      }
    } else if (i->dst != -1 && i->src != -1 && i->src2 == -1) {
      // OP reg,reg,[mem]
      switch (type) {
        case DT_FLOAT:
          (masm->*fltopmem)(reg[i->dst], reg[i->src], loop.addr(i->args[1]));
          break;
        case DT_DOUBLE:
          (masm->*dblopmem)(reg[i->dst], reg[i->src], loop.addr(i->args[1]));
          break;
        default: UNSUPPORTED;
      }
    } else {
      UNSUPPORTED;
    }
  }

  // Generate scalar float expression using SSE and XMM registers.
  void GenerateScalarFltSSE(Step *step,
                            const Expression &expr,
                            MacroAssembler *masm) {
    // Set up model for generating scalar float SSE instructions.
    Expression::Model model;
    model.mov_reg_reg = true;
    model.mov_reg_imm = true;
    model.mov_reg_mem = true;
    model.mov_mem_reg = true;
    model.op_reg_reg = true;
    model.op_reg_mem = true;
    model.func_reg_reg = true;
    model.func_reg_mem = true;

    // Convert expression to instructions.
    Expression instructions;
    CHECK(expr.Rewrite(model, &instructions));
    instructions.ComputeLiveRanges();
    int num_regs = instructions.AllocateRegisters();

    // Allocate registers for each input and output and load the tensors.
    Tensor *result = step->output(0);
    Type type = result->type();
    LoopGenerator loop(step, masm, result->element_size());

    // Allocate XMM registers for temp results.
    SIMDRegisters &mm = masm->mm();
    std::vector<XMMRegister> reg(num_regs);
    for (int i = 0; i < num_regs; ++i) reg[i] = mm.allocx();

    // Loop over all outputs.
    loop.begin(masm);

    // Emit instructions for computing expression.
    for (Expression::Op *i : instructions.ops()) {
      // Skip no-ops.
      if (i->nop()) continue;
      LOG(INFO) << "  " << i->AsInstruction()
                << " ; " << i->result->AsString() << "=" << i->AsString();

      switch (i->type) {
        case Expression::MOV:
          GenerateScalarFltMove(type, i, reg, loop, masm);
          break;
        case Expression::ADD:
          GenerateXMMFltOp(
              type, i,
              &Assembler::addss, &Assembler::addsd,
              &Assembler::addss, &Assembler::addsd,
              reg, loop, masm);
          break;
        case Expression::SUB:
          GenerateXMMFltOp(
              type, i,
              &Assembler::subss, &Assembler::subsd,
              &Assembler::subss, &Assembler::subsd,
              reg, loop, masm);
          break;
        case Expression::MUL:
          GenerateXMMFltOp(
              type, i,
              &Assembler::mulss, &Assembler::mulsd,
              &Assembler::mulss, &Assembler::mulsd,
              reg, loop, masm);
          break;
        case Expression::DIV:
          GenerateXMMFltOp(
              type, i,
              &Assembler::divss, &Assembler::divsd,
              &Assembler::divss, &Assembler::divsd,
              reg, loop, masm);
          break;
        case Expression::MIN:
          GenerateXMMFltOp(
              type, i,
              &Assembler::minss, &Assembler::minsd,
              &Assembler::minss, &Assembler::minsd,
              reg, loop, masm);
          break;
        case Expression::MAX:
          GenerateXMMFltOp(
              type, i,
              &Assembler::maxss, &Assembler::maxsd,
              &Assembler::maxss, &Assembler::maxsd,
              reg, loop, masm);
          break;
        case Expression::RELU:
          if (CPU::Enabled(SSE2)) {
            __ pxor(reg[i->dst], reg[i->dst]);
          } else if (type == DT_FLOAT) {
            float zero = 0;
            auto *data = masm->CreateDataBlock(sizeof(float));
            data->Add(zero);
            __ movss(reg[i->dst], data->address());
          } else if (type == DT_DOUBLE) {
            double zero = 0;
            auto *data = masm->CreateDataBlock(sizeof(double));
            data->Add(zero);
            __ movsd(reg[i->dst], data->address());
          } else {
            UNSUPPORTED;
          }
          GenerateXMMFltOp(
              type, i,
              &Assembler::maxss, &Assembler::maxsd,
              &Assembler::maxss, &Assembler::maxsd,
              reg, loop, masm);
          break;
        default: UNSUPPORTED;
      }
    }

    // Next element.
    loop.end(masm);
  }

  // Generate scalar float expression using AVX and XMM registers.
  void GenerateScalarFltAVX(Step *step,
                            const Expression &expr,
                            MacroAssembler *masm) {
    // Set up model for generating scalar float XMM AVX instructions.
    Expression::Model model;
    model.mov_reg_reg = true;
    model.mov_reg_imm = true;
    model.mov_reg_mem = true;
    model.mov_mem_reg = true;
    model.op_reg_reg_reg = true;
    model.op_reg_reg_mem = true;
    model.func_reg_reg = true;
    model.func_reg_mem = true;
    if (CPU::Enabled(FMA3)) {
      model.fm_reg_reg_reg = true;
      model.fm_reg_reg_mem = true;
    }

    // Convert expression to instructions.
    Expression instructions;
    CHECK(expr.Rewrite(model, &instructions));
    instructions.ComputeLiveRanges();
    int num_regs = instructions.AllocateRegisters();

    // Allocate registers for each input and output and load the tensors.
    Tensor *result = step->output(0);
    Type type = result->type();
    LoopGenerator loop(step, masm, result->element_size());

    // Allocate XMM registers for temp results.
    SIMDRegisters &mm = masm->mm();
    std::vector<XMMRegister> reg(num_regs);
    for (int i = 0; i < num_regs; ++i) reg[i] = mm.allocx();

    // Loop over all outputs.
    loop.begin(masm);

    // Emit instructions for computing expression.
    for (Expression::Op *i : instructions.ops()) {
      // Skip no-ops.
      if (i->nop()) continue;
      LOG(INFO) << "  " << i->AsInstruction()
                << " ; " << i->result->AsString() << "=" << i->AsString();

      switch (i->type) {
        case Expression::MOV:
          GenerateScalarFltMove(type, i, reg, loop, masm);
          break;
        case Expression::ADD:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vaddss, &Assembler::vaddsd,
              &Assembler::vaddss, &Assembler::vaddsd,
              reg, loop, masm);
          break;
        case Expression::SUB:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vsubss, &Assembler::vsubsd,
              &Assembler::vsubss, &Assembler::vsubsd,
              reg, loop, masm);
          break;
        case Expression::MUL:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vmulss, &Assembler::vmulsd,
              &Assembler::vmulss, &Assembler::vmulsd,
              reg, loop, masm);
          break;
        case Expression::DIV:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vdivss, &Assembler::vdivsd,
              &Assembler::vdivss, &Assembler::vdivsd,
              reg, loop, masm);
          break;
        case Expression::MIN:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vminss, &Assembler::vminsd,
              &Assembler::vminss, &Assembler::vminsd,
              reg, loop, masm);
          break;
        case Expression::MAX:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vmaxss, &Assembler::vmaxsd,
              &Assembler::vmaxss, &Assembler::vmaxsd,
              reg, loop, masm);
          break;
        case Expression::RELU:
          __ vpxor(reg[i->dst], reg[i->dst], reg[i->dst]);
          switch (type) {
            case DT_FLOAT:
              if (i->dst != -1 && i->src != -1) {
                __ vmaxss(reg[i->dst], reg[i->dst], reg[i->src]);
              } else if (i->dst != -1 && i->src == -1) {
                __ vmaxss(reg[i->dst], reg[i->dst], loop.addr(i->args[1]));
              } else {
                UNSUPPORTED;
              }
              break;
            case DT_DOUBLE:
              if (i->dst != -1 && i->src != -1) {
                __ vmaxsd(reg[i->dst], reg[i->dst], reg[i->src]);
              } else if (i->dst != -1 && i->src == -1) {
                __ vmaxsd(reg[i->dst], reg[i->dst], loop.addr(i->args[1]));
              } else {
                UNSUPPORTED;
              }
              break;
            default: UNSUPPORTED;
          }
          break;
        case Expression::MULADD132:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vfmadd132ss, &Assembler::vfmadd132sd,
              &Assembler::vfmadd132ss, &Assembler::vfmadd132sd,
              reg, loop, masm);
          break;
        case Expression::MULADD213:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vfmadd213ss, &Assembler::vfmadd213sd,
              &Assembler::vfmadd213ss, &Assembler::vfmadd213sd,
              reg, loop, masm);
          break;
        case Expression::MULADD231:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vfmadd231ss, &Assembler::vfmadd231sd,
              &Assembler::vfmadd231ss, &Assembler::vfmadd231sd,
              reg, loop, masm);
          break;
        case Expression::MULSUB132:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vfmsub132ss, &Assembler::vfmsub132sd,
              &Assembler::vfmsub132ss, &Assembler::vfmsub132sd,
              reg, loop, masm);
          break;
        case Expression::MULSUB213:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vfmsub213ss, &Assembler::vfmsub213sd,
              &Assembler::vfmsub213ss, &Assembler::vfmsub213sd,
              reg, loop, masm);
          break;
        case Expression::MULSUB231:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vfmsub231ss, &Assembler::vfmsub231sd,
              &Assembler::vfmsub231ss, &Assembler::vfmsub231sd,
              reg, loop, masm);
          break;
        default: UNSUPPORTED;
      }
    }

    // Next element.
    loop.end(masm);
  }

  // Generate vector float expression using SSE and XMM registers.
  void GenerateVectorFltSSE(Step *step,
                            const Expression &expr,
                            MacroAssembler *masm) {
    // Set up model for generating vector float SSE instructions.
    Expression::Model model;
    model.mov_reg_reg = true;
    model.mov_reg_imm = true;
    model.mov_reg_mem = true;
    model.mov_mem_reg = true;
    model.op_reg_reg = true;
    model.op_reg_mem = true;
    model.func_reg_reg = true;
    model.func_reg_mem = true;

    // Convert expression to instructions.
    Expression instructions;
    CHECK(expr.Rewrite(model, &instructions));
    instructions.ComputeLiveRanges();
    int num_regs = instructions.AllocateRegisters();

    // Allocate registers for each input and output and load the tensors.
    Tensor *result = step->output(0);
    Type type = result->type();
    LoopGenerator loop(step, masm, XMMRegSize);

    // Allocate XMM registers for temp results.
    SIMDRegisters &mm = masm->mm();
    std::vector<XMMRegister> reg(num_regs);
    for (int i = 0; i < num_regs; ++i) reg[i] = mm.allocx();

    // Loop over all outputs.
    loop.begin(masm);

    // Emit instructions for computing expression.
    for (Expression::Op *i : instructions.ops()) {
      // Skip no-ops.
      if (i->nop()) continue;
      LOG(INFO) << "  " << i->AsInstruction()
                << " ; " << i->result->AsString() << "=" << i->AsString();

      switch (i->type) {
        case Expression::MOV:
          GenerateVectorFltMove(type, i, reg, loop, masm);
          break;
        case Expression::ADD:
          GenerateXMMFltOp(
              type, i,
              &Assembler::addps, &Assembler::addpd,
              &Assembler::addps, &Assembler::addpd,
              reg, loop, masm);
          break;
        case Expression::SUB:
          GenerateXMMFltOp(
              type, i,
              &Assembler::subps, &Assembler::subpd,
              &Assembler::subps, &Assembler::subpd,
              reg, loop, masm);
          break;
        case Expression::MUL:
          GenerateXMMFltOp(
              type, i,
              &Assembler::mulps, &Assembler::mulpd,
              &Assembler::mulps, &Assembler::mulpd,
              reg, loop, masm);
          break;
        case Expression::DIV:
          GenerateXMMFltOp(
              type, i,
              &Assembler::divps, &Assembler::divpd,
              &Assembler::divps, &Assembler::divpd,
              reg, loop, masm);
          break;
        case Expression::MIN:
          GenerateXMMFltOp(
              type, i,
              &Assembler::minps, &Assembler::minpd,
              &Assembler::minps, &Assembler::minpd,
              reg, loop, masm);
          break;
        case Expression::MAX:
          GenerateXMMFltOp(
              type, i,
              &Assembler::maxps, &Assembler::maxpd,
              &Assembler::maxps, &Assembler::maxpd,
              reg, loop, masm);
          break;
        case Expression::RELU:
          if (CPU::Enabled(SSE2)) {
            if (type == DT_FLOAT) {
              __ xorps(reg[i->dst], reg[i->dst]);
            } else if (type == DT_DOUBLE) {
              __ xorpd(reg[i->dst], reg[i->dst]);
            } else {
              UNSUPPORTED;
            }
          } else if (type == DT_FLOAT) {
            float zero = 0;
            auto *data = masm->CreateDataBlock(sizeof(float));
            data->Add(zero);
            __ movss(reg[i->dst], data->address());
          } else {
            UNSUPPORTED;
          }
          GenerateXMMFltOp(
              type, i,
              &Assembler::maxps, &Assembler::maxpd,
              &Assembler::maxps, &Assembler::maxpd,
              reg, loop, masm);
          break;
        default: UNSUPPORTED;
      }
    }

    // Next element.
    loop.end(masm);
  }

  // Generate vector float expression using AVX and XMM registers.
  void GenerateVectorFltAVX128(Step *step,
                               const Expression &expr,
                               MacroAssembler *masm) {
    // Set up model for generating vector float AVX instructions.
    Expression::Model model;
    model.mov_reg_reg = true;
    model.mov_reg_imm = true;
    model.mov_reg_mem = true;
    model.mov_mem_reg = true;
    model.op_reg_reg_reg = true;
    model.op_reg_reg_mem = true;
    model.func_reg_reg = true;
    model.func_reg_mem = true;
    if (CPU::Enabled(FMA3)) {
      model.fm_reg_reg_reg = true;
      model.fm_reg_reg_mem = true;
    }

    // Convert expression to instructions.
    Expression instructions;
    CHECK(expr.Rewrite(model, &instructions));
    instructions.ComputeLiveRanges();
    int num_regs = instructions.AllocateRegisters();

    // Allocate registers for each input and output and load the tensors.
    Tensor *result = step->output(0);
    Type type = result->type();
    LoopGenerator loop(step, masm, XMMRegSize);

    // Allocate XMM registers for temp results.
    SIMDRegisters &mm = masm->mm();
    std::vector<XMMRegister> reg(num_regs);
    for (int i = 0; i < num_regs; ++i) reg[i] = mm.allocx();

    // Loop over all outputs.
    loop.begin(masm);

    // Emit instructions for computing expression.
    for (Expression::Op *i : instructions.ops()) {
      // Skip no-ops.
      if (i->nop()) continue;
      LOG(INFO) << "  " << i->AsInstruction()
                << " ; " << i->result->AsString() << "=" << i->AsString();

      switch (i->type) {
        case Expression::MOV:
          GenerateVectorFltMove(type, i, reg, loop, masm);
          break;
        case Expression::ADD:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vaddps, &Assembler::vaddpd,
              &Assembler::vaddps, &Assembler::vaddpd,
              reg, loop, masm);
          break;
        case Expression::SUB:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vsubps, &Assembler::vsubpd,
              &Assembler::vsubps, &Assembler::vsubpd,
              reg, loop, masm);
          break;
        case Expression::MUL:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vmulps, &Assembler::vmulpd,
              &Assembler::vmulps, &Assembler::vmulpd,
              reg, loop, masm);
          break;
        case Expression::DIV:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vdivps, &Assembler::vdivpd,
              &Assembler::vdivps, &Assembler::vdivpd,
              reg, loop, masm);
          break;
        case Expression::MIN:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vminps, &Assembler::vminpd,
              &Assembler::vminps, &Assembler::vminpd,
              reg, loop, masm);
          break;
        case Expression::MAX:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vmaxps, &Assembler::vmaxpd,
              &Assembler::vmaxps, &Assembler::vmaxpd,
              reg, loop, masm);
          break;
        case Expression::RELU:
          if (type == DT_FLOAT) {
            __ vxorps(reg[i->dst], reg[i->dst], reg[i->dst]);
            if (i->dst != -1 && i->src != -1) {
              __ vmaxps(reg[i->dst], reg[i->dst], reg[i->src]);
            } else if (i->dst != -1 && i->src == -1) {
              __ vmaxps(reg[i->dst], reg[i->dst], loop.addr(i->args[1]));
            } else {
              UNSUPPORTED;
            }
          } else if (type == DT_DOUBLE) {
            __ vxorpd(reg[i->dst], reg[i->dst], reg[i->dst]);
            if (i->dst != -1 && i->src != -1) {
              __ vmaxpd(reg[i->dst], reg[i->dst], reg[i->src]);
            } else if (i->dst != -1 && i->src == -1) {
              __ vmaxpd(reg[i->dst], reg[i->dst], loop.addr(i->args[1]));
            } else {
              UNSUPPORTED;
            }
          } else {
            UNSUPPORTED;
          }
          break;
        case Expression::MULADD132:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vfmadd132ps, &Assembler::vfmadd132pd,
              &Assembler::vfmadd132ps, &Assembler::vfmadd132pd,
              reg, loop, masm);
          break;
        case Expression::MULADD213:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vfmadd213ps, &Assembler::vfmadd213pd,
              &Assembler::vfmadd213ps, &Assembler::vfmadd213pd,
              reg, loop, masm);
          break;
        case Expression::MULADD231:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vfmadd231ps, &Assembler::vfmadd231pd,
              &Assembler::vfmadd231ps, &Assembler::vfmadd231pd,
              reg, loop, masm);
          break;
        case Expression::MULSUB132:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vfmsub132ps, &Assembler::vfmsub132pd,
              &Assembler::vfmsub132ps, &Assembler::vfmsub132pd,
              reg, loop, masm);
          break;
        case Expression::MULSUB213:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vfmsub213ps, &Assembler::vfmsub213pd,
              &Assembler::vfmsub213ps, &Assembler::vfmsub213pd,
              reg, loop, masm);
          break;
        case Expression::MULSUB231:
          GenerateXMMFltOp(
              type, i,
              &Assembler::vfmsub231ps, &Assembler::vfmsub231pd,
              &Assembler::vfmsub231ps, &Assembler::vfmsub231pd,
              reg, loop, masm);
          break;
        default: UNSUPPORTED;
      }
    }

    // Next element.
    loop.end(masm);
  }

  // Generate vector float expression using AVX and YMM registers.
  void GenerateVectorFltAVX256(Step *step,
                               const Expression &expr,
                               MacroAssembler *masm) {
    // Set up model for generating vector float AVX instructions.
    Expression::Model model;
    model.mov_reg_reg = true;
    model.mov_reg_imm = true;
    model.mov_reg_mem = true;
    model.mov_mem_reg = true;
    model.op_reg_reg_reg = true;
    model.op_reg_reg_mem = true;
    model.func_reg_reg = true;
    model.func_reg_mem = true;
    if (CPU::Enabled(FMA3)) {
      model.fm_reg_reg_reg = true;
      model.fm_reg_reg_mem = true;
    }

    // Convert expression to instructions.
    Expression instructions;
    CHECK(expr.Rewrite(model, &instructions));
    instructions.ComputeLiveRanges();
    int num_regs = instructions.AllocateRegisters();

    // Allocate registers for each input and output and load the tensors.
    Tensor *result = step->output(0);
    Type type = result->type();
    LoopGenerator loop(step, masm, YMMRegSize);

    // Allocate YMM registers for temp results.
    SIMDRegisters &mm = masm->mm();
    std::vector<YMMRegister> reg(num_regs);
    for (int i = 0; i < num_regs; ++i) reg[i] = mm.allocy();

    // Loop over all outputs.
    loop.begin(masm);

    // Emit instructions for computing expression.
    for (Expression::Op *i : instructions.ops()) {
      // Skip no-ops.
      if (i->nop()) continue;
      LOG(INFO) << "  " << i->AsInstruction()
                << " ; " << i->result->AsString() << "=" << i->AsString();

      switch (i->type) {
        case Expression::MOV:
          GenerateVectorFltMove(type, i, reg, loop, masm);
          break;
        case Expression::ADD:
          GenerateYMMFltOp(
              type, i,
              &Assembler::vaddps, &Assembler::vaddpd,
              &Assembler::vaddps, &Assembler::vaddpd,
              reg, loop, masm);
          break;
        case Expression::SUB:
          GenerateYMMFltOp(
              type, i,
              &Assembler::vsubps, &Assembler::vsubpd,
              &Assembler::vsubps, &Assembler::vsubpd,
              reg, loop, masm);
          break;
        case Expression::MUL:
          GenerateYMMFltOp(
              type, i,
              &Assembler::vmulps, &Assembler::vmulpd,
              &Assembler::vmulps, &Assembler::vmulpd,
              reg, loop, masm);
          break;
        case Expression::DIV:
          GenerateYMMFltOp(
              type, i,
              &Assembler::vdivps, &Assembler::vdivpd,
              &Assembler::vdivps, &Assembler::vdivpd,
              reg, loop, masm);
          break;
        case Expression::MIN:
          GenerateYMMFltOp(
              type, i,
              &Assembler::vminps, &Assembler::vminpd,
              &Assembler::vminps, &Assembler::vminpd,
              reg, loop, masm);
          break;
        case Expression::MAX:
          GenerateYMMFltOp(
              type, i,
              &Assembler::vmaxps, &Assembler::vmaxpd,
              &Assembler::vmaxps, &Assembler::vmaxpd,
              reg, loop, masm);
          break;
        case Expression::RELU:
          if (type == DT_FLOAT) {
            __ vxorps(reg[i->dst], reg[i->dst], reg[i->dst]);
            if (i->dst != -1 && i->src != -1) {
              __ vmaxps(reg[i->dst], reg[i->dst], reg[i->src]);
            } else if (i->dst != -1 && i->src == -1) {
              __ vmaxps(reg[i->dst], reg[i->dst], loop.addr(i->args[1]));
            } else {
              UNSUPPORTED;
            }
          } else if (type == DT_DOUBLE) {
            __ vxorpd(reg[i->dst], reg[i->dst], reg[i->dst]);
            if (i->dst != -1 && i->src != -1) {
              __ vmaxpd(reg[i->dst], reg[i->dst], reg[i->src]);
            } else if (i->dst != -1 && i->src == -1) {
              __ vmaxpd(reg[i->dst], reg[i->dst], loop.addr(i->args[1]));
            } else {
              UNSUPPORTED;
            }
          } else {
            UNSUPPORTED;
          }
          break;
        case Expression::MULADD132:
          GenerateYMMFltOp(
              type, i,
              &Assembler::vfmadd132ps, &Assembler::vfmadd132pd,
              &Assembler::vfmadd132ps, &Assembler::vfmadd132pd,
              reg, loop, masm);
          break;
        case Expression::MULADD213:
          GenerateYMMFltOp(
              type, i,
              &Assembler::vfmadd213ps, &Assembler::vfmadd213pd,
              &Assembler::vfmadd213ps, &Assembler::vfmadd213pd,
              reg, loop, masm);
          break;
        case Expression::MULADD231:
          GenerateYMMFltOp(
              type, i,
              &Assembler::vfmadd231ps, &Assembler::vfmadd231pd,
              &Assembler::vfmadd231ps, &Assembler::vfmadd231pd,
              reg, loop, masm);
          break;
        case Expression::MULSUB132:
          GenerateYMMFltOp(
              type, i,
              &Assembler::vfmsub132ps, &Assembler::vfmsub132pd,
              &Assembler::vfmsub132ps, &Assembler::vfmsub132pd,
              reg, loop, masm);
          break;
        case Expression::MULSUB213:
          GenerateYMMFltOp(
              type, i,
              &Assembler::vfmsub213ps, &Assembler::vfmsub213pd,
              &Assembler::vfmsub213ps, &Assembler::vfmsub213pd,
              reg, loop, masm);
          break;
        case Expression::MULSUB231:
          GenerateYMMFltOp(
              type, i,
              &Assembler::vfmsub231ps, &Assembler::vfmsub231pd,
              &Assembler::vfmsub231ps, &Assembler::vfmsub231pd,
              reg, loop, masm);
          break;
        default: UNSUPPORTED;
      }
    }

    // Next element.
    loop.end(masm);
  }
};

// Register arithmetic kernels.
void RegisterArithmeticKernels(Library *library) {
  library->RegisterTransformer(new ConstantFolding());
  library->RegisterTransformer(new ExpressionTransformer());

  library->Register(new Calculate());
}

}  // namespace myelin
}  // namespace sling

