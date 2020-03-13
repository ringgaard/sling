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

#include <set>

#include "sling/myelin/compute.h"
#include "sling/myelin/macro-assembler.h"
#include "sling/myelin/simd-assembler.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Reshape tensor while preserving the underlying data.
class Reshape : public Kernel {
 public:
  string Name() override { return "Reshape"; }
  string Operation() override { return "Reshape"; }

  bool Supports(Step *step) override {
    // Check inputs and outputs.
    if (step->indegree() != 2 || step->outdegree() != 1) return false;
    Tensor *x = step->input(0);
    Tensor *y = step->output(0);
    if (x->type() != y->type()) return false;
    if (x->elements() != y->elements()) return false;
    return true;
  }

  void Adjust(Step *step) override {
    CHECK(step->AllowInPlace(0, 0, true)) << step->name();
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    CHECK(step->input(0)->SharedWith(step->output(0)));
  }

  Placement Location() override { return NOWHERE; }

  int64 Complexity(const Step *step) override {
    return 0;
  }
};

// Kernel for resizing the input by padding or cropping.
class Resize : public Kernel {
 public:
  string Name() override { return "Resize"; }
  string Operation() override { return "Resize"; }

  bool Supports(Step *step) override {
    // Check inputs and outputs.
    if (step->indegree() != 1 || step->outdegree() != 1) return false;
    Tensor *x = step->input(0);
    Tensor *y = step->output(0);
    if (x->type() != y->type()) return false;
    return true;
  }

  void Adjust(Step *step) override {
    Tensor *x = step->input(0);
    Tensor *y = step->output(0);
    if (!x->dynamic() && !y->dynamic()) {
      step->AllowInPlace(0, 0, x->elements() == y->elements());
    }
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Check if resize is a no-op.
    Tensor *x = step->input(0);
    Tensor *y = step->output(0);
    bool shared = x->SharedWith(y);
    bool pad = y->size() > x->size();
    bool crop = y->size() < x->size();
    bool dynamic = x->dynamic() || y->dynamic();
    if (dynamic) {
      step->set_variant("dyn");
    } else if (shared && !pad && !crop) {
      step->set_variant("nop");
      return;
    } else if (!shared) {
      step->set_variant("copy");
    } else if (pad) {
      step->set_variant("pad");
    } else if (crop) {
      step->set_variant("crop");
    }

    // Allocate registers.
    Register src = masm->rr().alloc_fixed(rsi);
    Register dst = masm->rr().alloc_fixed(rdi);
    Register cnt = masm->rr().alloc_fixed(rcx);
    Register acc = masm->rr().alloc_fixed(rax);

    if (dynamic) {
      // Resize dynamically-sized tensor.
      Register xsize = masm->rr().alloc();
      Register ysize = masm->rr().alloc();

      // Load tensors and (dynamic) sizes.
      __ LoadTensorAddressAndSize(src, xsize, x);
      __ LoadTensorAddressAndSize(dst, ysize, y);

      // Copy input to output.
      __ movq(cnt, xsize);
      __ cmpq(cnt, ysize);
      __ cmovq(less, cnt, ysize);
      __ repmovsb();

      // Pad output if needed.
      Label skip;
      __ movq(cnt, ysize);
      __ subq(cnt, xsize);
      __ j(less_equal, &skip);
      __ xorq(acc, acc);
      __ repstosb();
      __ bind(&skip);

    } else if (shared) {
     // Pad output if needed.
     if (pad) {
       __ LoadTensorAddress(dst, y);
       __ addq(dst, Immediate(x->size()));
       __ xorq(acc, acc);
       __ movq(cnt, Immediate(y->size() - x->size()));
       __ repstosb();
     }
    } else {
      // Load tensors.
      __ LoadTensorAddress(src, x);
      __ LoadTensorAddress(dst, y);

      // Copy input to output.
      __ movq(cnt, Immediate(std::min(x->size(), y->size())));
      __ repmovsb();

      // Pad output if needed.
      if (pad) {
        __ xorq(acc, acc);
        __ movq(cnt, Immediate(y->size() - x->size()));
        __ repstosb();
      }
    }
  }

  int64 Complexity(const Step *step) override {
    return 0;
  }
};

// Output a one-hot tensor.
//
// OneHot(index, {depth}, [value])
//
// indices: tensor of indices (int32[B]).
// depth: scalar defining the depth of the one hot dimension (int32).
// value: optional value (T[S]) defining the value to fill in (default: 1)
// output: one hot tensor (T[Bx{depth}xS])
class OneHot : public Kernel {
 public:
  string Name() override { return "OneHot"; }
  string Operation() override { return "OneHot"; }

  bool Supports(Step *step) override {
    // Check inputs and outputs.
    if (step->indegree() != 1 && step->indegree() != 2) return false;
    if (step->outdegree() != 1) return false;
    Tensor *index = step->input(0);
    Tensor *value = step->indegree() > 1 ? step->input(1) : nullptr;
    Tensor *onehot = step->output(0);
    int depth = step->GetAttr("depth", 0);
    if (depth == 0 && onehot->rank() > 0) depth = onehot->shape().dim(-1);
    if (depth <= 0) return false;

    // Check output shape.
    if (index->type() != DT_INT32) return false;
    Shape s = index->shape();
    s.add(depth);
    if (value != nullptr) {
      s.append(value->shape());
    }
    if (onehot->shape() != s) return false;
    if (value != nullptr && value->type() != onehot->type()) return false;

    return true;
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    Tensor *index = step->input(0);
    Tensor *value = step->indegree() > 1 ? step->input(1) : nullptr;
    Tensor *onehot = step->output(0);

    // Allocate registers.
    Register src = masm->rr().alloc_fixed(rsi);
    Register dst = masm->rr().alloc_fixed(rdi);
    Register cnt = masm->rr().alloc_fixed(rcx);
    Register acc = masm->rr().alloc_fixed(rax);
    Register input = masm->rr().alloc();
    Register output = masm->rr().alloc();

    // Zero output tensor.
    __ LoadTensorAddress(input, index);
    __ LoadTensorAddress(output, onehot);
    __ movq(dst, output);
    __ movq(cnt, Immediate(onehot->size()));
    __ xorq(acc, acc);
    __ repstosb();

    // Loop over batches.
    bool batched = index->elements() > 1;
    Register batch = masm->rr().alloc();
    Label lb;
    if (batched) {
      __ xorq(batch, batch);
      __ bind(&lb);
    }

    // Compute address of onehot element.
    __ movq(dst, output);
    __ movsxlq(acc, Operand(input));
    __ Multiply(acc, value != nullptr ? value->size() : sizeof(float));
    __ addq(dst, acc);

    // Set one-hot index.
    if (value != nullptr) {
      __ LoadTensorAddress(src, value);
      __ movq(cnt, Immediate(value->size()));
      __ repmovsb();
    } else {
      __ movq(Operand(dst), Immediate(0x3F800000));
    }

    // Next batch.
    if (batched) {
      __ addq(input, Immediate(sizeof(uint32)));
      __ addq(output, Immediate(onehot->AxisSize(index->rank())));
      __ incq(batch);
      __ cmpq(batch, Immediate(index->elements()));
      __ j(less, &lb);
    }
  }

  int64 Complexity(const Step *step) override {
    return 0;
  }
};

// Slice input tensors along first dimension.
class Slice : public Kernel {
 public:
  string Name() override { return "Slice"; }
  string Operation() override { return "Slice"; }

  bool Supports(Step *step) override {
    // Check inputs and outputs.
    if (step->indegree() != 3 || step->outdegree() != 1) return false;

    // Check arguments.
    Tensor *input = step->input(0);
    Tensor *begin = step->input(1);
    Tensor *size = step->input(2);
    Tensor *output = step->output(0);
    if (begin->rank() > 1 || begin->type() != DT_INT32) return false;
    if (size->rank() > 1 || size->type() != DT_INT32) return false;
    std::vector<int> s;
    CHECK(size->GetData(&s));
    if (Shape(s) != output->shape()) return false;
    if (input->type() != output->type()) return false;

    return true;
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get inputs and output.
    Tensor *source = step->input(0);
    Tensor *begin = step->input(1);
    Tensor *size = step->input(2);
    Tensor *destination = step->output(0);

    // Compute size of slice.
    std::vector<int> size_tensor;
    CHECK(size->GetData(&size_tensor));
    int bytes = source->element_size();
    for (int i = 0; i < size_tensor.size(); ++i) {
      bytes *= size_tensor[i];
    }

    // Allocate registers.
    Register src = masm->rr().alloc_fixed(rsi);
    Register dst = masm->rr().alloc_fixed(rdi);

    // Get source and destination addresses.
    __ LoadTensorAddress(src, source, begin);
    __ LoadTensorAddress(dst, destination);

    // Copy input to output.
    __ Copy(dst, 0, src, 0, bytes);
  }

  int64 Complexity(const Step *step) override {
    return 0;
  }
};

// Fold multiplication into update ops.
class UpdateTransformer : public Transformer {
 public:
  string Name() override { return "UpdateTransformer"; }

  bool Transform(Flow *flow) override {
    bool updated = false;
    bool again = true;
    while (again) {
      again = false;
      if (TransformMatMul(flow)) {
        again = true;
        updated = true;
      }
      if (TransformDistributiveUpdate(flow)) {
        again = true;
        updated = true;
      }
      if (TransformSparseUpdate(flow)) {
        again = true;
        updated = true;
      }
    }
    return updated;
  }

  // Transform matrix multiplication updates.
  bool TransformMatMul(Flow *flow) {
    int updates = 0;
    for (Flow::Operation *op : flow->Find("MatMul|1:Add|1:Assign")) {
      Flow::Operation *assign = op;
      Flow::Operation *add = assign->inputs[1]->producer;
      Flow::Operation *matmul = add->inputs[1]->producer;

      if (assign->inputs[0] != add->inputs[0]) continue;
      if (add->outputs[0]->usages() != 1) continue;
      if (matmul->outputs[0]->usages() != 1) continue;

      flow->Fuse(assign, flow->Fuse(add, matmul, ""), "AssignAddMatMul", true);
      updates++;
    }
    return updates > 0;
  }

  // Transform distributive scatter udates.
  bool TransformDistributiveUpdate(Flow *flow) {
    // Find assignments for scatter operations.
    std::set<Flow::Operation *> scatter_assigns;
    for (Flow::Operation *op : flow->Find("Scatter")) {
      while (op->outdegree() == 1 && op->outputs[0]->usages() == 1) {
        op = op->outputs[0]->consumers[0];
      }
      if (op->type == "Assign") scatter_assigns.insert(op);
    }

    // Split additive updates.
    int updates = 0;
    for (Flow::Operation *op : flow->Find("Add|1:Add|1:Assign")) {
      Flow::Operation *assign1 = op;
      Flow::Operation *add1 = assign1->inputs[1]->producer;
      Flow::Operation *add2 = add1->inputs[1]->producer;
      Flow::Variable *target = assign1->inputs[0];

      if (add1->outputs[0]->usages() != 1) continue;
      if (add2->outputs[0]->usages() != 1) continue;
      if (add1->inputs[0] != target) continue;
      if (scatter_assigns.count(assign1) == 0) continue;

      // Split into two accumulative updates.
      Flow::Function *func = assign1->func;
      Flow::Operation *assign2 = flow->AddOperation(func, "", "Assign");
      assign2->AddInput(target);
      assign2->AddInput(add2->outputs[0]);
      add1->ReplaceInput(add1->inputs[1], add2->inputs[0]);
      add2->ReplaceInput(add2->inputs[0], target);
      updates++;
    }
    return updates > 0;
  }

  // Transform sparse updates.
  bool TransformSparseUpdate(Flow *flow) {
    int updates = 0;
    for (Flow::Operation *op : flow->Find("Scatter|1:Add|1:Assign")) {
      Flow::Operation *assign = op;
      Flow::Operation *add = assign->inputs[1]->producer;
      Flow::Operation *scatter = add->inputs[1]->producer;
      if (assign->inputs[0] != add->inputs[0]) continue;
      if (add->outputs[0]->usages() != 1) continue;
      if (scatter->outputs[0]->usages() != 1) continue;

      Flow::Operation *add_scatter = flow->Fuse(add, scatter, "");
      flow->Fuse(assign, add_scatter, "AssignAddScatter", true);
      updates++;
    }
    return updates > 0;
  }
};

// Propagate tensor references across reshapes.
class ReshapeRefTransformer : public Transformer {
 public:
  string Name() override { return "ReshapeRefTransformer"; }

  bool Transform(Flow *flow) override {
    bool updated = false;
    for (Flow::Operation *op : flow->ops()) {
      if (op->type != "Reshape") continue;
      if (op->indegree() != 2 || op->outdegree() != 1) return false;
      if (op->inputs[0]->ref() && !op->outputs[0]->ref()) {
        op->outputs[0]->set_ref();
        updated = true;
      }
      if (op->outputs[0]->ref() && !op->inputs[0]->ref()) {
        op->inputs[0]->set_ref();
        updated = true;
      }
    }

    return updated;
  }
};

// Register array kernels.
void RegisterArrayKernels(Library *library) {
  library->Register(new Reshape());
  library->Register(new Resize());
  library->Register(new OneHot());
  library->Register(new Slice());

  library->RegisterTransformer(new UpdateTransformer());
  library->RegisterTransformer(new ReshapeRefTransformer());
}

}  // namespace myelin
}  // namespace sling
