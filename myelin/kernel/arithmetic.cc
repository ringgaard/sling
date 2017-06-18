#include "myelin/kernel/arithmetic.h"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/logging.h"
#include "base/types.h"
#include "myelin/compute.h"
#include "myelin/express.h"
#include "myelin/macro-assembler.h"
#include "myelin/generator/elementwise.h"
#include "myelin/generator/expression.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Mapping from flow variables to expression variables.
typedef std::map<Flow::Variable *, Express::Var *> VarMap;

// Convert operation type to expression op.
static Express::OpType OpType(const string &op) {
  // Operations that can be fused into Calculate operations.
  static std::unordered_map<string, Express::OpType> ops {
    {"Add", Express::ADD},
    {"Sub", Express::SUB},
    {"Mul", Express::MUL},
    {"Div", Express::DIV},
    {"Minimum", Express::MIN},
    {"Maximum", Express::MAX},
    {"Relu", Express::RELU},
    {"Log", Express::LOG},
    {"Exp", Express::EXP},
    {"Sigmoid", Express::SIGMOID},
    {"Tanh", Express::TANH},
  };

  auto f = ops.find(op);
  return f == ops.end() ? Express::INVALID : f->second;
}

// Check if operation is a candidate for Calculate ops.
static bool IsCalculateOp(Flow::Operation *op) {
  return op->type == "Calculate" || OpType(op->type) != Express::INVALID;
}

// Initialize expression for flow operation.
static void InitExpression(Flow::Operation *op, Express *expr, bool expand) {
  if (op->type == "Calculate") {
    // Build expression from expression recipe attribute on op.
    const string &recipe = op->GetAttr("expr");
    if (!recipe.empty()) expr->Parse(recipe, expand);
  } else {
    // Add op with inputs and output.
    CHECK_EQ(op->outdegree(), 1);
    std::vector<Express::Var *> args(op->indegree());
    for (int i = 0; i < op->indegree(); ++i) {
      args[i] = expr->Variable(Express::INPUT, i);
    }
    Express::Op *func = expr->Function(OpType(op->type), args, expand);
    func->Assign(expr->Variable(Express::OUTPUT, 0));
    expr->CompactTempVars();
  }

  // Mark constant inputs.
  for (int i = 0; i < op->indegree(); ++i) {
    if (op->inputs[i]->data != nullptr) {
      expr->Variable(Express::INPUT, i)->type = Express::CONST;
    }
  }
}

// Initialize expression for step.
static void InitExpression(const Step *step, Express *expr, bool expand) {
  if (step->type() == "Calculate") {
    // Build expression from expression recipe attribute on op.
    const string &recipe = step->GetAttr("expr");
    if (!recipe.empty()) expr->Parse(recipe, expand);
  } else {
    // Add op with inputs and output.
    CHECK_EQ(step->outdegree(), 1);
    std::vector<Express::Var *> args(step->indegree());
    for (int i = 0; i < step->indegree(); ++i) {
      args[i] = expr->Variable(Express::INPUT, i);
    }
    Express::Op *func = expr->Function(OpType(step->type()), args, expand);
    func->Assign(expr->Variable(Express::OUTPUT, 0));
    expr->CompactTempVars();
  }

  // Mark constant inputs.
  for (int i = 0; i < step->indegree(); ++i) {
    if (step->input(i)->IsConstant()) {
      expr->Variable(Express::INPUT, i)->type = Express::CONST;
    }
  }
}

// Build mapping from flow variables to expression variables.
static void MapVars(Flow::Operation *op, Express *expr, VarMap *varmap) {
  // Map input variables.
  for (int i = 0; i < op->indegree(); ++i) {
    Express::VarType type =
        op->inputs[i]->constant() ? Express::CONST : Express::INPUT;
      (*varmap)[op->inputs[i]] = expr->Variable(type, i);
  }

  // Map output variables.
  for (int i = 0; i < op->outdegree(); ++i) {
    (*varmap)[op->outputs[i]] = expr->Variable(Express::OUTPUT, i);
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
    // Check that ops have the same types and output shapes.
    if (first->indegree() < 1 || first->outdegree() < 1) return false;
    if (second->indegree() < 1 || second->outdegree() < 1) return false;
    Type type = first->outputs[0]->type;
    const Shape &shape = first->outputs[0]->shape;
    for (auto *input : first->inputs) {
      if (input->type != type) return false;
      // TODO: check that input shape is compatible with the output shape.
    }
    for (auto *input : second->inputs) {
      if (input->type != type) return false;
      // TODO: check that input shape is compatible with the output shape.
    }
    for (auto *output : first->outputs) {
      if (output->type != type) return false;
      if (output->shape != shape) return false;
    }
    for (auto *output : second->outputs) {
      if (output->type != type) return false;
      if (output->shape != shape) return false;
    }

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
    Express expr1;
    InitExpression(first, &expr1, false);
    VarMap vars1;
    MapVars(first, &expr1, &vars1);

    // Build second expression.
    Express expr2;
    InitExpression(second, &expr2, false);
    VarMap vars2;
    MapVars(second, &expr2, &vars2);

    // Build expression variable mapping for mapping variables in the second
    // expression to variables in the first expression.
    Express::Map mapping;
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
          vars1[v]->type = Express::TEMP;
          next_output--;

          // Adjust numbering of output variables from the first op.
          for (auto *o : expr1.vars()) {
            if (o->type == Express::OUTPUT && o->id > vars1[v]->id) {
              o->id--;
            }
          }
        }

        // Map input from second op to output from first op.
        mapping[vars2[v]] = vars1[v];
      } else {
        // Map input from second op to a new input in the merged expression.
        Express::VarType type = v->constant() ? Express::CONST : Express::INPUT;
        mapping[vars2[v]] = expr1.Variable(type, next_input++);
      }
    }
    for (Flow::Variable *v : second->outputs) {
      // Map output from second op to a new output in the merged expression.
      mapping[vars2[v]] = expr1.Variable(Express::OUTPUT, next_output++);
    }
    expr2.CompactTempVars();

    // Merge second expression into the first one.
    expr1.Merge(&expr2, mapping);

    // Return merged recipe.
    return expr1.AsRecipe();
  }
};

// Kernel for computing arithmetic expressions.
class Calculate : public Kernel {
 public:
  Calculate(const string &name, const string &operation)
      : name_(name), operation_(operation) {}

  string Name() override { return name_; }
  string Operation() override { return operation_; }

  bool Supports(Step *step) override {
    // Check that operation is compatible.
    if (step->type() != operation_) return false;

    // Check that inputs and outputs have the compatible types and shapes.
    if (step->indegree() < 1 || step->outdegree() < 1) return false;
    Type type = step->output(0)->type();
    const Shape &shape = step->output(0)->shape();
    for (auto *input : step->inputs()) {
      if (input->type() != type) return false;
      // TODO: check that input shape is compatible with the output shape.
    }
    for (auto *output : step->outputs()) {
      if (output->type() != type) return false;
      if (output->shape() != shape) return false;
    }

    return true;
  }

  void Adjust(Step *step) override {
    // Set the alignment requirements based on the vector size.
    Type type = step->output(0)->type();
    int elements = step->output(0)->elements();
    Express expr;
    InitExpression(step, &expr, false);
    ElementwiseIndexGenerator index(step);
    auto *generator = ExpressionGenerator::Select(expr, type, elements);
    CHECK(generator != nullptr);
    generator->Initalize(expr, type, &index);
    int alignment = generator->VectorSize();
    step->set_variant(generator->Name());
    delete generator;

    for (auto *input : step->inputs()) {
      input->SetMiniumAlignment(alignment);
    }
    for (auto *output : step->outputs()) {
      output->SetMiniumAlignment(alignment);
    }

    // TODO: require compact row-major encoding.

    // Enable sharing of inputs and outputs.
    for (int i = 0; i < step->indegree(); ++i) {
      for (int j = 0; j < step->outdegree(); ++j) {
        if (step->input(i)->shape() == step->output(j)->shape()) {
          if (step->AllowInPlace(i, j)) break;
        }
      }
    }
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Determine output type and shape from the first output.
    Type type = step->output(0)->type();
    const Shape &shape = step->output(0)->shape();
    int elements = shape.elements();

    // Compile expression to be computed.
    Express expr;
    InitExpression(step, &expr, true);

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

    // Compile expression to be computed.
    Express expr;
    InitExpression(step, &expr, true);

    // The number of operations is the number of ops times the output size.
    return shape.elements() * expr.Complexity();
  }

 private:
  const string name_;       // kernel name
  const string operation_;  // kernel operation
};

// Register calculation kernels in library.
static void RegisterCalculate(Library *library) {
  library->RegisterTransformer(new ExpressionTransformer());
  library->Register(new Calculate("Calculate", "Calculate"));
  library->Register(new Calculate("AddExpr", "Add"));
  library->Register(new Calculate("SubExpr", "Sub"));
  library->Register(new Calculate("MulExpr", "Mul"));
  library->Register(new Calculate("DivExpr", "Div"));
  library->Register(new Calculate("MaxExpr", "Maximum"));
  library->Register(new Calculate("MinExpr", "Minimum"));
  library->Register(new Calculate("ReluExpr", "Relu"));
  library->Register(new Calculate("LogExpr", "Log"));
  library->Register(new Calculate("ExpExpr", "Exp"));
  library->Register(new Calculate("SigmoidExpr", "Sigmoid"));
  library->Register(new Calculate("TanhExpr", "Tanh"));
}

// Replace ops with constant input variables with new computed constant
// variables.
class ConstantFolding : public Transformer {
 public:
  bool Transform(Flow *flow) override {
    std::vector<Flow::Operation *> remove;
    bool again = true;
    while (again) {
      again = false;
      for (Flow::Operation *op : flow->ops()) {
        // Operation must have both inputs and outputs.
        if (op->inputs.empty() || op->outputs.empty()) continue;

        // Check if all inputs are constants.
        bool constant = true;
        for (auto *input : op->inputs) {
          if (!input->constant()) {
            constant = false;
            break;
          }
        }
        if (!constant || !IsCalculateOp(op)) continue;

        // Compute op and replace with new constant variable. First extract
        // the constant operation into a separate sub-flow.
        Flow subflow;
        flow->Extract("compute", op->inputs, op->outputs, &subflow);

        // Analyze, compile and execute sub-flow to compute constant value.
        Library library;
        RegisterCalculate(&library);
        subflow.Analyze(library);
        Network network;
        CHECK(network.Compile(subflow, library));
        auto *cell = network.GetCell("compute");
        Instance data(cell);
        data.Compute();

        // Extract results and change output variables to constants.
        for (auto *output : op->outputs) {
          // Allocate space for constant in flow.
          auto *result = cell->GetParameter(output->name);
          size_t size = result->space();
          char *buffer = flow->AllocateMemory(size);
          memcpy(buffer, data.GetAddress(result), size);

          // Change variable to a constant.
          output->data = buffer;
          output->size = size;
          output->in = true;
        }

        // Mark constant op for removal.
        while (!op->inputs.empty()) op->RemoveInput(op->inputs[0]);
        while (!op->outputs.empty()) op->RemoveOutput(op->outputs[0]);
        remove.push_back(op);
        again = true;
      }
    }

    // Remove constant ops.
    if (remove.empty()) return false;
    for (Flow::Operation *op : remove) {
      flow->DeleteOperation(op);
    }
    return true;
  }
};

// Register arithmetic kernels.
void RegisterArithmeticKernels(Library *library) {
  library->RegisterTransformer(new ConstantFolding());
  RegisterCalculate(library);
}

}  // namespace myelin
}  // namespace sling

