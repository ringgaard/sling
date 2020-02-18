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

// Transpose tensor by permuting dimensions.
class Transpose : public Kernel {
 public:
  string Name() override { return "Transpose"; }
  string Operation() override { return "Transpose"; }

  bool Supports(Step *step) override {
    // Check inputs and outputs.
    if (step->indegree() != 1 || step->outdegree() != 1) return false;
    Tensor *x = step->input(0);
    Tensor *y = step->output(0);
    if (x->type() != y->type()) return false;

    // Check permutation.
    Shape perm = GetPerm(step);
    if (x->shape().permuted(perm) != y->shape()) return false;

    return true;
  }

  void Adjust(Step *step) override {
    // Get input and output.
    Tensor *x = step->input(0);
    Tensor *y = step->output(0);
    Shape perm = GetPerm(step);
    int shuffled = Shuffled(perm);

    // Trivial permutation is a no-op.
    if (shuffled <= 0 && step->AllowInPlace(0, 0, true)) return;

    // Require dense standard layout.
    x->RequireStandardOrder();
    y->RequireStandardOrder();
    x->RequireDense();
    y->RequireDense();

    // Reserve registers.
    step->SetRegisterUsage(5 + shuffled);
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get input and output.
    Tensor *x = step->input(0);
    Tensor *y = step->output(0);
    Shape perm = GetPerm(step);

    // Find the number of outer and inner dimensions.
    int outer_dims = Outer(perm);
    int inner_dims = Inner(perm);
    int shuffle_dims = Shuffled(perm);
    if (shuffle_dims <= 0) {
      CHECK(x->SharedWith(y));
      return;
    }

    // Set kernel variant.
    string variant = step->variant();
    if (outer_dims > 0) variant += "O" + std::to_string(outer_dims);
    if (shuffle_dims > 0) variant += "S" + std::to_string(shuffle_dims);
    if (inner_dims > 0) variant += "I" + std::to_string(inner_dims);
    step->set_variant(variant);

    // Allocate registers.
    Register src = masm->rr().alloc_fixed(rsi);
    Register dst = masm->rr().alloc_fixed(rdi);
    Register cnt = masm->rr().alloc_fixed(rcx);
    Register in = masm->rr().alloc();
    Register ofs = masm->rr().alloc();
    Register aux = cnt;

    // Load tensor addresses.
    __ LoadTensorAddress(in, x);
    __ LoadTensorAddress(dst, y);

    // Loop over outer dimensions.
    Register batch = masm->rr().alloc();
    Label lb;
    if (outer_dims > 0) {
      __ xorq(batch, batch);
      __ bind(&lb);
    }

    // Loop over shuffled dimensions.
    std::vector<Label> shuffle_loop(shuffle_dims);
    std::vector<Register> shuffle_index(shuffle_dims);
    for (int i = 0; i < shuffle_dims; ++i) {
      shuffle_index[i] = masm->rr().alloc();
      __ xorq(shuffle_index[i], shuffle_index[i]);
      __ bind(&shuffle_loop[i]);
    }

    // Compute offset of shuffled element/block in input.
    CHECK_GE(shuffle_dims, 2);
    __ leaq(ofs, Operand(shuffle_index[0], shuffle_index[1]));
    for (int i = 2; i < shuffle_dims; ++i) {
      __ addq(ofs, shuffle_index[i]);
    }

    // Copy element/block from input to output.
    int block_size = y->stride(y->rank() - inner_dims - 1);
    if (block_size == 1) {
      __ movb(aux, Operand(in, ofs));
      __ movb(Operand(dst), aux);
      __ addq(dst, Immediate(1));
    } else if (block_size == 2) {
      __ movw(aux, Operand(in, ofs));
      __ movw(Operand(dst), aux);
      __ addq(dst, Immediate(2));
    } else if (block_size == 4) {
      __ movl(aux, Operand(in, ofs));
      __ movl(Operand(dst), aux);
      __ addq(dst, Immediate(4));
    } else if (block_size == 8) {
      __ movq(aux, Operand(in, ofs));
      __ movq(Operand(dst), aux);
      __ addq(dst, Immediate(8));
    } else {
      __ leaq(src, Operand(in, ofs));
      __ movq(cnt, Immediate(block_size));
      __ repmovsb();
    }

    // Next shuffled element/block.
    for (int i = shuffle_dims - 1; i >= 0; --i) {
      int d = perm[i + outer_dims];
      int stride = x->stride(d);
      int size = x->dim(d);
      __ addq(shuffle_index[i], Immediate(stride));
      __ cmpq(shuffle_index[i], Immediate(stride * size));
      __ j(less, &shuffle_loop[i]);
    }

    // Next outer batch.
    if (outer_dims > 0) {
      __ addq(in, Immediate(x->stride(outer_dims - 1)));
      __ incq(batch);
      __ cmpq(batch, Immediate(x->shape().outer(outer_dims)));
      __ j(less, &lb);
    }
  }

  int64 Complexity(const Step *step) override {
    return step->input(0)->elements();
  }

 private:
  // Get permutation attribute.
  static Shape GetPerm(Step *step) {
    Shape perm;
    if (!step->GetAttr("perm", &perm)) {
      perm.reverse(step->input(0)->rank());
    }
    return perm;
  }

  // Number of preserved outer dimensions in permutation.
  static int Outer(const Shape &perm) {
    int r = perm.rank();
    int outer = 0;
    for (int d = 0; d < r; ++d) {
      if (perm[d] != d) break;
      outer++;
    }
    return outer;
  }

  // Number of preserved inner dimensions in permutation.
  static int Inner(const Shape &perm) {
    int r = perm.rank();
    int inner = 0;
    for (int d = r - 1; d >= 0; --d) {
      if (perm[d] != d) break;
      inner++;
    }
    return inner;
  }

  // Number of shuffled dimensions in permutation.
  static int Shuffled(const Shape &perm) {
    return perm.rank() - Outer(perm) - Inner(perm);
  }
};

// Register transpose kernel.
void RegisterTranspose(Library *library) {
  library->Register(new Transpose());
}

}  // namespace myelin
}  // namespace sling
