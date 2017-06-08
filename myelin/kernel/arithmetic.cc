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
#include "myelin/generator/elementwise.h"
#include "myelin/generator/expression.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Mapping from flow variables to expression variables.
typedef std::map<Flow::Variable *, Expression::Var *> VarMap;

// Convert operation type to expression op.
static Expression::OpType OpType(Flow::Operation *op) {
  // Operations that can be fused into Calculate operations.
  static std::unordered_map<string, Expression::OpType> ops {
    {"Add", Expression::ADD},
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

// Kernel for computing arithmetic expressions.
class Calculate : public Kernel {
 public:
  string Name() override { return "Calculate"; }
  string Operation() override { return "Calculate"; }

  bool Supports(Step *step) override {
    return true;
  }

  void Adjust(Step *step) override {
    for (int i = 0; i < step->indegree(); ++i) {
      for (int j = 0; j < step->outdegree(); ++j) {
        if (step->AllowInPlace(i, j)) break;
      }
    }
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Determine output type and shape from the first output.
    Type type = step->output(0)->type();
    const Shape &shape = step->output(0)->shape();
    int elements = shape.elements();

    // Compile expression to be computed.
    Expression expr;
    expr.Parse(step->GetAttr("expr"));

    // Create element-wise index generator.
    ElementwiseIndexGenerator index(step);

    // Select expression generator.
    auto *generator = ExpressionGenerator::Select(expr, type, elements);
    CHECK(generator != nullptr);

    // Initialize expression and index generators.
    generator->Initalize(expr, type, &index);
    index.SetVectorSize(generator->VectorSize());

    // Allocate registers.
    CHECK(index.AllocateRegisters(masm)) << "Register overflow";

    // Generate expression loop.
    index.BeginLoop(masm);
    generator->Generate(masm);
    index.EndLoop(masm);

    delete generator;
  }

  int64 Complexity(const Step *step) {
    // Determine shape from the first output.
    const Shape &shape = step->output(0)->shape();
    int elements = shape.elements();

    // Compile expression to be computed.
    Expression expr;
    expr.Parse(step->GetAttr("expr"));

    // The number of operations is the number of ops times the output size.
    return expr.ops().size() * elements;
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

