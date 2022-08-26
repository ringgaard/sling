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

#include <vector>

#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/myelin/compute.h"
#include "sling/myelin/kernel/library.h"

namespace sling {
namespace myelin {

// Replace ops with constant input variables with new computed constant
// variables.
class ConstantFolding : public Transformer {
 public:
  ConstantFolding() {
    RegisterStandardLibrary(&library_, LIBRARY_NOPRECOMPUTE);
  }

  string Name() override { return "ConstantFolding"; }

  bool Transform(Flow *flow) override {
    // Find constant ops and replace them with constant variables.
    std::vector<Flow::Operation *> remove;
    bool again = true;
    while (again) {
      again = false;
      for (Flow::Operation *op : flow->ops()) {
        // Operation must have both inputs and outputs.
        if (op->inputs.empty() || op->outputs.empty()) continue;

        // Do not fold ops with the keep flag set.
        if (op->GetAttr("keep", false)) continue;

        // Identity op elimination is handled elsewhere.
        if (op->type == "Identity") continue;

        // Shape, Type, and Size can be pre-computed.
        if ((op->type == "Shape" || op->type == "Rank" || op->type == "Size") &&
             op->inputs[0]->shape.defined() && !op->inputs[0]->dynamic()) {
          // Get input and output.
          CHECK_EQ(op->indegree(), 1);
          CHECK_EQ(op->outdegree(), 1);
          Flow::Variable *input = op->inputs[0];
          Flow::Variable *output = op->outputs[0];
          Shape &shape = input->shape;
          CHECK_EQ(output->type, DT_INT32);

          // Allocate space for constant in flow.
          int size = sizeof(int32);
          if (op->type == "Shape") {
            CHECK_EQ(shape.rank(), output->elements());
            size = shape.rank() * sizeof(int32);
          }
          char *data = flow->AllocateMemory(output->size);
          int32 *result = reinterpret_cast<int32 *>(data);

          // Set constant variable to the pre-computed value.
          if (op->type == "Shape") {
            for (int d = 0; d < shape.rank(); ++d) {
              result[d] = shape.dim(d);
            }
          } else if (op->type == "Rank") {
            result[0] = shape.rank();
          } else if (op->type == "Size") {
            result[0] = shape.elements();
          }

          // Mark op for removal.
          op->RemoveInput(input);
          op->RemoveOutput(output);
          remove.push_back(op);

          // An output variable cannot be converted into a constant, so in that
          // case the output is assigned to the constant with an identity op.
          if (output->out()) {
            Flow::Variable *c = flow->AddVariable(output->name + "/value",
                                                  output->type, output->shape);
            c->data = data;
            c->size = size;
            flow->AddOperation(op->func, op->name, "Identity", {c}, {output});
          } else {
            output->data = data;
            output->size = size;
          }

          // Make sure input variable is not abandoned.
          if (input->in() && input->detached()) {
            op->func->unused.push_back(input);
          }

          again = true;
          continue;
        }

        // Constant shape shifting.
        if (op->type == "Reshape"  ||
            op->type == "Squeeze" ||
            op->type == "ExpandDims") {
          // Get input and output.
          CHECK_EQ(op->indegree(), 2);
          CHECK_EQ(op->outdegree(), 1);
          Flow::Variable *input = op->inputs[0];
          Flow::Variable *shape = op->inputs[1];
          Flow::Variable *output = op->outputs[0];

          if (input->constant() && shape->constant()) {
            // Make the output of the shape shifting op a constant.
            output->data = input->data;
            output->size = input->size;
            output->set_in();

            // Rename variable and add op name as alias.
            output->AddAlias(output->name);
            output->name = input->name;

            // Mark op for removal.
            op->RemoveInput(input);
            op->RemoveInput(shape);
            op->RemoveOutput(output);
            remove.push_back(op);
            again = true;
          }
          continue;
        }

        // Check if all inputs are constants.
        bool constant = true;
        for (auto *input : op->inputs) {
          if (!input->constant()) {
            constant = false;
            break;
          }
        }

        if (constant && !library_.Lookup(op->type).empty()) {
          // Compute op and replace with new constant variable. First extract
          // the constant operation into a separate sub-flow.
          Flow subflow;
          flow->Extract("compute", op->inputs, op->outputs, &subflow);

          // Analyze and compile sub-flow.
          subflow.Analyze(library_);
          Network network;
          if (network.Compile(subflow, library_)) {
            //  Execute sub-flow to compute constant value
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
              output->type = result->type();
              output->shape = result->shape();
            }

            // Mark constant op for removal.
            while (!op->inputs.empty()) op->RemoveInput(op->inputs[0]);
            while (!op->outputs.empty()) op->RemoveOutput(op->outputs[0]);
            remove.push_back(op);
            again = true;
          }
        }
      }
    }

    // Remove constant ops.
    if (remove.empty()) return false;
    for (Flow::Operation *op : remove) {
      flow->DeleteOperation(op);
    }
    return true;
  }

 private:
  // Library with kernels for computing constant ops.
  Library library_;
};

// Remove unused variables.
class RemoveUnusedVariables : public Transformer {
 public:
  string Name() override { return "RemoveUnusedVariables"; }

  bool Transform(Flow *flow) override {
    // Find intermediate variables with no producers or consumers.
    std::vector<Flow::Variable *> remove;
    for (Flow::Variable *var : flow->vars()) {
      if (!var->in() && !var->out() && var->detached()) {
        remove.push_back(var);
      }
    }

    // Remove unused variables.
    if (remove.empty()) return false;
    for (Flow::Variable *var : remove) {
      flow->DeleteVariable(var);
    }
    return true;
  }
};

// Register precompute library.
void RegisterPrecomputeLibrary(Library *library) {
  library->RegisterTransformer(new ConstantFolding());
  library->RegisterTransformer(new RemoveUnusedVariables());
}

}  // namespace myelin
}  // namespace sling

