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

// Merge transpose into matmul attributes.
class TransposeTransformer : public Transformer {
 public:
  string Name() override { return "TransposeTransformer"; }

  bool Transform(Flow *flow) override {
    int updates = 0;

    // Eliminate double transpose.
    for (Flow::Operation *op : flow->Find("Transpose|Transpose")) {
      Flow::Operation *t1 = op;
      Flow::Operation *t2 = t1->inputs[0]->producer;
      if (t1->outputs[0]->out()) continue;
      if (t1->outputs[0]->usages() != 1) continue;
      if (t1->HasAttr("perm")) continue;
      if (t2->HasAttr("perm")) continue;

      t2->outputs[0]->shape = t2->inputs[0]->shape;
      t1->outputs[0]->shape = t1->inputs[0]->shape;
      flow->Eliminate(t1);
      flow->Eliminate(t2);
      updates++;
    }

    // Eliminate double transpose through reference.
    for (Flow::Operation *op : flow->Find("Reference|Transpose")) {
      Flow::Operation *transpose = op;
      Flow::Operation *reference = transpose->inputs[0]->producer;
      if (transpose->outputs[0]->usages() != 1) continue;
      if (transpose->outputs[0]->out()) continue;
      if (reference->outputs[0]->usages() != 1) continue;
      if (reference->outputs[0]->out()) continue;
      if (transpose->HasAttr("perm")) continue;

      Flow::Variable *var = flow->Var(reference->GetAttr("var"));
      if (var == nullptr || var->producer == nullptr) continue;
      if (var->producer->type != "Transpose") continue;
      if (var->producer->HasAttr("perm")) continue;

      // Move reference to the input of the referenced transpose and eliminate
      // transpose.
      Flow::Variable *tin = var->producer->inputs[0];
      reference->SetAttr("var", tin->name);
      tin->set_out();
      transpose->inputs[0]->shape = transpose->outputs[0]->shape;
      flow->Eliminate(transpose);

      // Check if the referenced transpose is still an output.
      if (var->out() && !var->consumers.empty()) {
        int var_refs = 0;
        for (auto *op : flow->ops()) {
          if (op->type == "Reference" && op->GetAttr("var") == var->name) {
            var_refs++;
          }
        }
        if (var_refs == 0) var->set_out(false);
      }

      updates++;
    }

    // Merge double transpose.
    for (Flow::Operation *op : flow->Find("Transpose|Transpose")) {
      Flow::Operation *t1 = op;
      Flow::Operation *t2 = t1->inputs[0]->producer;
      if (t2->outputs[0]->out()) continue;
      if (t2->outputs[0]->usages() != 1) continue;

      int rank1 = t1->outputs[0]->rank();
      int rank2 = t2->outputs[0]->rank();
      if (rank1 != rank2) continue;

      Shape perm1;
      Shape perm2;
      if (!t1->GetAttr("perm", &perm1)) perm1.reverse(rank1);
      if (!t2->GetAttr("perm", &perm2)) perm2.reverse(rank2);
      Shape perm = perm2.permuted(perm1);
      t1->SetAttr("perm", perm);

      t2->outputs[0]->shape = t2->inputs[0]->shape;
      flow->Eliminate(t2);
      updates++;
    }

    // Fold transpose of first argument into matmul.
    for (Flow::Operation *op : flow->Find("Transpose|MatMul")) {
      Flow::Operation *matmul = op;
      Flow::Operation *transpose = matmul->inputs[0]->producer;
      if (transpose->outputs[0]->usages() != 1) continue;
      if (transpose->outputs[0]->out()) continue;
      if (transpose->HasAttr("perm")) continue;

      transpose->outputs[0]->shape = transpose->inputs[0]->shape;
      flow->Eliminate(transpose);
      matmul->SetAttr("transpose_a", !matmul->GetAttr("transpose_a", false));
      updates++;
    }

    // Fold transpose of second argument into matmul.
    for (Flow::Operation *op : flow->Find("Transpose|1:MatMul")) {
      Flow::Operation *matmul = op;
      Flow::Operation *transpose = matmul->inputs[1]->producer;
      if (transpose->outputs[0]->usages() != 1) continue;
      if (transpose->outputs[0]->out()) continue;
      if (transpose->HasAttr("perm")) continue;

      transpose->outputs[0]->shape = transpose->inputs[0]->shape;
      flow->Eliminate(transpose);
      matmul->SetAttr("transpose_b", !matmul->GetAttr("transpose_b", false));
      updates++;
    }

    // Fold transpose of output into matmul.
    for (Flow::Operation *op : flow->Find("MatMul|Transpose")) {
      Flow::Operation *transpose = op;
      Flow::Operation *matmul = transpose->inputs[0]->producer;
      if (matmul->outputs[0]->usages() != 1) continue;
      if (matmul->outputs[0]->out()) continue;
      if (transpose->HasAttr("perm")) continue;

      matmul->outputs[0]->shape = transpose->outputs[0]->shape;
      flow->Eliminate(transpose);
      matmul->SetAttr("transpose_c", !matmul->GetAttr("transpose_c", false));
      updates++;
    }

    // Factor transpose out of MatMul result by applying C^T=A*B => C=B^T*A^T.
    for (Flow::Operation *op : flow->Find("MatMul")) {
      if (!op->GetAttr("transpose_c", false)) continue;
      if (op->indegree() != 2 || op->outdegree() != 1) continue;
      op->SwapInputs();
      bool ta = op->GetAttr("transpose_a", false);
      bool tb = op->GetAttr("transpose_b", false);
      op->SetAttr("transpose_a", !tb);
      op->SetAttr("transpose_b", !ta);
      op->RemoveAttr("transpose_c");
    }

    return updates > 0;
  }
};

// Register transpose kernel.
void RegisterTranspose(Library *library) {
  library->RegisterTransformer(new TransposeTransformer());
  library->Register(new Transpose());
}

}  // namespace myelin
}  // namespace sling
