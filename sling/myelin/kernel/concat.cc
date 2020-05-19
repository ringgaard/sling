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

#include "sling/myelin/compute.h"
#include "sling/myelin/macro-assembler.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Output concatenation of input tensors along first dimension.
class BasicConcat : public Kernel {
 public:
  string Name() override { return "BasicConcat"; }
  string Operation() override { return "Concat"; }

  bool Supports(Step *step) override {
    // Check inputs and outputs.
    if (step->indegree() < 2 || step->outdegree() != 1) return false;

    // Only concatenation along a singular prefix supported.
    int n = step->GetAttr("N", step->indegree() - 1);
    if (step->indegree() < n + 1) return false;
    Tensor *axis = step->input(n);
    if (!axis->constant()) return false;
    int a = axis->value<int32>();
    if (step->output(0)->shape().outer(a) != 1) return false;
    if (step->output(0)->dynamic()) return false;

    return true;
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get the number of tensors to concatenate.
    int n = step->GetAttr("N", step->indegree() - 1);

    // Allocate registers.
    Register src = masm->rr().alloc_preferred(rsi);
    Register dst = masm->rr().alloc_preferred(rdi);
    Register out = masm->rr().alloc_preferred(rdx);

    // Load output tensor.
    __ LoadTensorAddress(out, step->output(0));

    // Copy input tensors to output.
    int offset = 0;
    for (int i = 0; i < n; ++i) {
      int size = step->input(i)->size();
        __ LoadTensorAddress(src, step->input(i));
        __ leaq(dst, Operand(out, offset));
      __ Copy(dst, 0, src, 0, size);
      offset += size;
    }
    CHECK_EQ(offset, step->output(0)->size()) << step->name();
  }

  int64 Complexity(const Step *step) override {
    return 0;
  }
};

// Output concatenation of input tensors along any axis.
class GeneralConcat : public Kernel {
 public:
  string Name() override { return "GeneralConcat"; }
  string Operation() override { return "Concat"; }

  bool Supports(Step *step) override {
    // Check inputs and outputs.
    if (step->indegree() < 2 || step->outdegree() != 1) return false;

    // Check concatenation axis.
    int n = step->GetAttr("N", step->indegree() - 1);
    if (step->indegree() < n + 1) return false;
    if (!step->input(n)->constant()) return false;
    int axis = step->input(n)->value<int32>();

    // Check outer prefix has same size for all inputs.
    Tensor *output = step->output(0);
    if (output->rank() < axis) return false;
    int prefix = output->shape().outer(axis);
    for (int i = 0; i < n; ++i) {
      Tensor *input = step->input(i);
      if (input->rank() < axis) return false;
      if (input->shape().outer(axis) != prefix) return false;
      if (input->type() != output->type()) return false;
      if (input->dynamic() != output->dynamic()) return false;
    }

    return true;
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get the number of tensors to concatenate.
    int n = step->GetAttr("N", step->indegree() - 1);
    Tensor *output = step->output(0);

    // Allocate registers.
    Register src = masm->rr().alloc_preferred(rsi);
    Register dst = masm->rr().alloc_preferred(rdi);
    Register cnt = masm->rr().alloc_preferred(rcx);
    Register idx = masm->rr().alloc();
    std::vector<Register> in(n);
    for (int i = 0; i < n; ++i) in[i] = masm->rr().alloc();

    // Load input tensors.
    for (int i = 0; i < n; ++i) {
      __ LoadTensorAddress(in[i], step->input(i));
    }

    // Load output tensor.
    __ LoadTensorAddress(dst, output);

    // Loop over outer prefix.
    Label l;
    int axis = step->input(n)->value<int32>();
    int repeat = output->shape().outer(axis);
    if (output->dynamic()) {
      __ LoadDynamicSize(idx, output, repeat);
      step->set_variant("DYN");
    } else {
      __ movq(idx, Immediate(repeat));
    }
    __ bind(&l);

    // Copy input tensors to output.
    int copied = 0;
    for (int i = 0; i < n; ++i) {
      Tensor *input = step->input(i);
      int size = input->AxisSize(axis);
      __ movq(src, in[i]);
      __ movq(cnt, Immediate(size));
      __ repmovsb();
      __ addq(in[i], Immediate(size));
      copied += size;
    }

    // Next chunk.
    int size = output->AxisSize(axis);
    if (copied != size) {
      __ addq(dst, Immediate(size - copied));
    }
    __ decq(idx);
    __ j(not_zero, &l);
  }

  int64 Complexity(const Step *step) override {
    return 0;
  }
};

// Split input tensors into chunks along a dimension.
class Split : public Kernel {
 public:
  string Name() override { return "Split"; }
  string Operation() override { return "Split"; }

  bool Supports(Step *step) override {
    // Check inputs and outputs.
    if (step->indegree() != 3) return false;

    // Only constant number of splits along a singular prefix supported.
    Tensor *input = step->input(0);
    Tensor *splits = step->input(1);
    Tensor *axis = step->input(2);

    // Check splits.
    if (splits->type() != DT_INT32 || !splits->constant()) return false;
    int n = splits->value<int32>();
    if (n != step->outdegree()) return false;

    // Check axis.
    if (axis->type() != DT_INT32 || !axis->constant()) return false;
    int a = axis->value<int32>();
    if (a > input->rank() - 1) return false;

    // Check that outputs match the input.
    Type dt = input->type();
    int size = input->shape().inner(a);
    if (size % n != 0) return false;
    for (int i = 0; i < n; ++i) {
      Tensor *output = step->output(i);
      if (output->type() != dt) return false;
      if (output->rank() != input->rank()) return false;
      if (output->shape().inner(a) != size / n) return false;
      if (output->dynamic() != input->dynamic()) return false;
    }
    return true;
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get input.
    Tensor *input = step->input(0);
    int n = step->input(1)->value<int32>();
    int axis = step->input(2)->value<int32>();
    int repeat = input->shape().outer(axis);

    // Allocate registers.
    Register src = masm->rr().alloc_preferred(rsi);
    Register dst = masm->rr().alloc_preferred(rdi);
    Register cnt = masm->rr().alloc_preferred(rcx);
    Register idx = masm->rr().alloc_preferred(rcx);

    // Load input tensor.
    __ LoadTensorAddress(src, input);

    if (input->dynamic() || repeat > 1) {
      // Load output tensors.
      step->set_variant("REP");
      std::vector<Register> out(n);
      for (int i = 0; i < n; ++i) {
        out[i] = masm->rr().alloc();
        __ LoadTensorAddress(out[i], step->output(i));
      }

      // Loop over outer prefix.
      Label l;
      if (input->dynamic()) {
        __ LoadDynamicSize(idx, input, repeat);
        step->set_variant("DYN");
      } else {
        __ movq(idx, Immediate(repeat));
      }
      __ bind(&l);

      // Split input to output.
      int copied = 0;
      for (int i = 0; i < n; ++i) {
        Tensor *output = step->output(i);
        int size = output->AxisSize(axis);
        __ movq(dst, out[i]);
        __ movq(cnt, Immediate(size));
        __ repmovsb();
        __ addq(out[i], Immediate(size));
        copied += size;
      }

      // Next chunk.
      int size = input->AxisSize(axis);
      if (copied != size) {
        __ addq(src, Immediate(size - copied));
      }
      __ decq(idx);
      __ j(not_zero, &l);
    } else {
      // Simple non-repeated split.
      for (int i = 0; i < n; ++i) {
        int size = step->output(i)->AxisSize(axis);
        __ LoadTensorAddress(dst, step->output(i));
        __ movq(cnt, Immediate(size));
        __ repmovsb();
      }
    }
  }

  int64 Complexity(const Step *step) override {
    return 0;
  }
};

// Register array kernels.
void RegisterConcatKernels(Library *library) {
  library->Register(new GeneralConcat());
  library->Register(new BasicConcat());
  library->Register(new Split());
}

}  // namespace myelin
}  // namespace sling
