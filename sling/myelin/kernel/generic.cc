// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>
#include <vector>

#include "sling/myelin/compute.h"
#include "sling/myelin/builder.h"
#include "sling/myelin/macro-assembler.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Reference op for accessing parameters in other cells of the network. Looks up
// tensor 'var' in instance and outputs a reference to the tensor.
class Reference : public Kernel {
 public:
  string Name() override { return "Reference"; }
  string Operation() override { return "Reference"; }

  bool Supports(Step *step) override {
    // Check inputs and outputs.
    if (step->indegree() != 1 || step->outdegree() != 1) return false;

    // Lookup variable.
    Tensor *var = GetReference(step);
    if (var == nullptr) {
      LOG(WARNING) << "Missing/unknown reference variable for " << step->name();
      return false;
    }

    // Check types.
    Tensor *instance = step->input(0);
    Tensor *ref = step->output(0);
    if (instance->type() != DT_RESOURCE || !instance->ref()) return false;
    if (ref->type() != var->type() || !ref->ref()) return false;

    return true;
  }

  void Adjust(Step *step) override {
    // Propagate alignment constraints from reference to variable.
    Tensor *var = GetReference(step);
    CHECK(var != nullptr);
    step->output(0)->Link(var);

    // Propagate corresponding sparsity tensors.
    if (var->sparse()) {
      Tensor *sparse_ref = step->output(0)->MakeSparse(true);
      sparse_ref->Link(var->sparse());
    }
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get inputs and outputs.
    Tensor *instance = step->input(0);
    Tensor *ref = step->output(0);
    Tensor *var = GetReference(step);
    CHECK(instance->IsLocal());
    CHECK(ref->IsLocal());
    CHECK(var != nullptr);

    // Output reference to variable in other instance.
    Register addr = masm->rr().alloc();
    if (var->IsGlobal()) {
      __ load_extern(addr, var->data(), var->name());
    } else {
      __ movq(addr, Operand(masm->instance(), instance->offset()));
      if (var->ref()) {
        __ movq(addr, Operand(addr, var->offset()));
      } else if (var->offset() != 0) {
        __ addq(addr, Immediate(var->offset()));
      }
    }
    __ movq(Operand(masm->instance(), ref->offset()), addr);

    // Output reference to sparsity vector.
    if (ref->sparse()) {
      CHECK(ref->sparse()->IsLocal());
      CHECK(var->sparse()->IsLocal());
      __ movq(addr, Operand(masm->instance(), instance->offset()));
      if (var->sparse()->ref()) {
        __ movq(addr, Operand(addr, var->sparse()->offset()));
      } else if (var->sparse()->offset() != 0) {
        __ addq(addr, Immediate(var->sparse()->offset()));
      }
      __ movq(Operand(masm->instance(), ref->sparse()->offset()), addr);
    }
  }

  int64 Complexity(const Step *step) override {
    return 0;
  }

  // Get referenced tensor.
  static Tensor *GetReference(Step *step) {
    const string &varname = step->GetAttr("var");
    if (varname.empty()) return nullptr;
    return step->cell()->network()->GetParameter(varname);
  }
};

// Remove identity ops.
class IdentityTransformer : public Transformer {
 public:
  string Name() override { return "IdentityTransformer"; }

  bool Transform(Flow *flow) override {
    // Eliminate no-ops.
    std::vector<Flow::Operation *> noops;
    for (Flow::Operation *op : flow->ops()) {
      if (op->type == "Identity") {
        // Eliminate identity if there is no implicit broadcasting involved.
        if (op->indegree() == 1 && op->outdegree() == 1) {
          Flow::Variable *in = op->inputs[0];
          Flow::Variable *out = op->outputs[0];
          if (!out->shape.missing() && in->shape != out->shape) continue;
          if (in->type != out->type) continue;

          // Assignment of global constant to output needs to be materialized.
          if (out->out() && in->global()) continue;

          // Assignment of local to global needs to be materialized.
          if (in->local() && out->global()) continue;

          noops.push_back(op);
        }
      } else if (op->type == "Reshape") {
        // Eliminate reshaping if input and output shapes are equal.
        if (op->indegree() == 2 && op->outdegree() == 1) {
          Flow::Variable *in = op->inputs[0];
          Flow::Variable *out = op->outputs[0];
          if (in->shape.defined() &&
              out->shape.defined() &&
              in->shape == out->shape &&
              in->type == out->type) {
            Flow::Variable *shape = op->inputs[1];
            op->RemoveInput(shape);
            noops.push_back(op);
          }
        }
      } else if (op->type == "Concat") {
        // Eliminate concatenations with only one input.
        int n = op->GetAttr("N", 0);
        if (n == 1) {
          Flow::Variable *axis = op->inputs[n];
          op->RemoveInput(axis);
          noops.push_back(op);
        }
      }
    }

    // Remove no-ops from the flow and eliminate the intermediate variables.
    for (Flow::Operation *op : noops) {
      flow->Eliminate(op);
    }

    return !noops.empty();
  }
};

// Expand composite functions to basic operations.
class CompositeTransformer : public Transformer {
 public:
  string Name() override { return "CompositeTransformer"; }

  bool Transform(Flow *flow) override {
    int updates = 0;

    // SoftMax is defined as:
    //   SoftMax(x) = Normalize(Exp(x)))
    // but is computed as:
    //  SoftMax(x) = Normalize(Exp(Sub(x, Max(x))))
    // for better numeric stablity.
    for (Flow::Operation *op : flow->Find("SoftMax")) {
      if (op->indegree() != 1 || op->outdegree() != 1) continue;

      Flow::Variable *x = op->inputs[0];
      Flow::Variable *y = op->outputs[0];
      int axis = op->GetAttr("axis", -1);

      FlowBuilder f(flow, op->func);
      Scope s(&f, op->name, false);
      auto *max = f.Max(x, axis, true);
      auto *softmax = f.Normalize(f.Exp(f.Sub(x, max)), axis, true);

      flow->RemoveOperation(op);
      f.Bind(y, softmax);

      updates++;
    }

    // LogSumExp is defined as:
    //   LogSumExp(x) = Log(Sum(Exp(x)))
    // but is computed as:
    //  LogSumExp(x) = Add(Log(Sum(Exp(Sub(x, Max(x))))), Max(x))
    // for better numeric stablity.
    for (Flow::Operation *op : flow->Find("LogSumExp")) {
      if (op->indegree() != 1 || op->outdegree() != 1) continue;

      Flow::Variable *x = op->inputs[0];
      Flow::Variable *y = op->outputs[0];
      int axis = op->GetAttr("axis", -1);
      bool keepdims = op->GetAttr("keepdims", false);

      FlowBuilder f(flow, op->func);
      Scope s(&f, op->name, false);
      auto *max = f.Max(x, axis, axis != -1);
      auto *sub = f.Sub(x, max);
      if (axis != -1 && !keepdims) max = f.Squeeze(max, axis);
      auto *lse = f.Add(f.Log(f.Sum(f.Exp(sub), axis, keepdims)), max);

      flow->RemoveOperation(op);
      f.Bind(y, lse);

      updates++;
    }

    return updates > 0;
  }
};

// Flattens nested concatenations, if possible.  E.g.,
// tf.concat([a, tf.concat([b, c], 1), d], 1) = tf.concat([a, b, c, d], 1)
class FlattenConcatTransformer : public Transformer {
 public:
  string Name() override { return "FlattenConcatTransformer"; }

  bool Transform(Flow *flow) override {
    bool transformed = false;
    while (TryFlattenOnce(flow)) transformed = true;
    return transformed;
  }

 private:
  // Returns true if the operation is a concatenation.
  static bool IsConcat(const Flow::Operation &operation) {
    if (operation.type != "Concat") return false;
    if (!operation.HasAttr("N")) return false;
    const int num_to_concat = operation.GetAttr("N", -1);
    if (num_to_concat <= 0) return false;
    if (operation.indegree() != num_to_concat + 1) return false;
    if (operation.outdegree() != 1) return false;
    return true;
  }

  // Flattens one nested concatenation and returns true, if possible.
  static bool TryFlattenOnce(Flow *flow) {
    // Search for a parent and child concat, where both have the same axis and
    // the result of the child concat is only used by the parent concat.
    for (Flow::Operation *child : flow->ops()) {
      if (!IsConcat(*child)) continue;

      // The child should have only one consumer, the parent.
      Flow::Variable *child_result = child->outputs[0];
      if (child_result->usages() != 1) continue;
      Flow::Operation *parent = child_result->consumers[0];
      if (!IsConcat(*parent)) continue;

      // The axes (i.e., final inputs) should match.
      int parent_axis = 0, child_axis = 0;
      if (!parent->inputs.back()->GetData(&parent_axis)) continue;
      if (!child->inputs.back()->GetData(&child_axis)) continue;
      if (parent_axis != child_axis) continue;

      // The child axis will be pruned, so it should have no other dependencies.
      if (child->inputs.back()->usages() != 1) continue;
      if (child->inputs.back()->producer != nullptr) continue;

      Flatten(flow, parent, child);
      return true;
    }

    return false;
  }

  // Flattens the child concatenation into the parent concatenation by replacing
  // the child with the inputs it concatenates.
  static void Flatten(Flow *flow, Flow::Operation *parent,
                      Flow::Operation *child) {
    VLOG(9) << "Flattening " << child->type << " (" << child->name << ") into "
            << parent->type << " (" << parent->name << ")";

    // Find the index of the child among the parent's inputs.  This is where the
    // child's inputs should be inserted.
    Flow::Variable *child_result = child->outputs[0];
    const int child_index =
        std::find(parent->inputs.begin(), parent->inputs.end(), child_result) -
        parent->inputs.begin();
    CHECK_LT(child_index, parent->inputs.size())
        << "parent=" << parent->name << " child=" << child->name;

    // Discard the child's axis; it is redundant with the parent's axis.
    Flow::Variable *child_axis = child->inputs.back();
    child->RemoveInput(child_axis);
    flow->DeleteVariable(child_axis);

    // Discard the child's result; it will be replaced with the child's inputs.
    child->RemoveOutput(child_result);
    parent->RemoveInput(child_result);
    flow->DeleteVariable(child_result);

    // Move the child's inputs to the parent.
    while (!child->inputs.empty()) {
      Flow::Variable *input = child->inputs.back();  // iterate back to front
      child->MoveInput(input, parent);

      // MoveInput() appends to the parent's input list, so pop and reinsert it
      // at the proper location.  Since we iterate the child's inputs backwards,
      // it suffices to repeatedly insert at the same index.
      CHECK_EQ(input, parent->inputs.back());
      parent->inputs.pop_back();
      parent->inputs.insert(parent->inputs.begin() + child_index, input);
    }

    flow->DeleteOperation(child);
    parent->SetAttr("N", static_cast<int>(parent->inputs.size() - 1));
  }
};

// Register generic transforms.
void RegisterGenericTransforms(Library *library) {
  library->RegisterTransformer(new IdentityTransformer());
  library->RegisterTransformer(new FlattenConcatTransformer());
  library->RegisterTransformer(new CompositeTransformer());
}

// Register generic library.
void RegisterGenericLibrary(Library *library) {
  library->Register(new Reference());
}

}  // namespace myelin
}  // namespace sling
