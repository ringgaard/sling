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
    step->output(0)->set_ref(step->input(0)->ref());
    CHECK(step->AllowInPlace(0, 0, true));
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    CHECK(step->input(0)->SharedWith(step->output(0)));
  }

  Placement Location() { return NOWHERE; }

  int64 Complexity(const Step *step) override {
    return 0;
  }
};

// Removes dimensions of size 1 from the shape of a tensor while preserving the
// underlying data.
class Squeeze : public Kernel {
 public:
  string Name() override { return "Squeeze"; }
  string Operation() override { return "Squeeze"; }

  bool Supports(Step *step) override {
    // Check inputs and outputs.
    if (step->indegree() != 1 || step->outdegree() != 1) return false;
    Tensor *x = step->input(0);
    Tensor *y = step->output(0);
    if (x->type() != y->type()) return false;
    if (x->elements() != y->elements()) return false;
    return true;
  }

  void Adjust(Step *step) override {
    step->output(0)->set_ref(step->input(0)->ref());
    CHECK(step->AllowInPlace(0, 0, true));
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    CHECK(step->input(0)->SharedWith(step->output(0)));
  }

  Placement Location() { return NOWHERE; }

  int64 Complexity(const Step *step) override {
    return 0;
  }
};

// Inserts a dimension of 1 into a tensor's shape while preserving the
// underlying data.
class ExpandDims : public Kernel {
 public:
  string Name() override { return "ExpandDims"; }
  string Operation() override { return "ExpandDims"; }

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
    step->output(0)->set_ref(step->input(0)->ref());
    CHECK(step->AllowInPlace(0, 0, true));
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    CHECK(step->input(0)->SharedWith(step->output(0)));
  }

  Placement Location() { return NOWHERE; }

  int64 Complexity(const Step *step) override {
    return 0;
  }
};

// Kernel for resizing the input by padding or cropping.
class Resize : public Kernel {
 public:
  bool Supports(Step *step) override {
    // Check inputs and outputs.
    if (step->indegree() != 3 || step->outdegree() != 1) return false;
    Tensor *x = step->input(0);
    Tensor *y = step->output(0);
    if (x->type() != y->type()) return false;
    return true;
  }

  void Adjust(Step *step) override {
    Tensor *x = step->input(0);
    Tensor *y = step->output(0);
    step->AllowInPlace(0, 0, x->elements() == y->elements());
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Check if resize is a no-op.
    Tensor *x = step->input(0);
    Tensor *y = step->output(0);
    bool shared = x->SharedWith(y);
    bool pad = y->size() > x->size();
    bool crop = y->size() < x->size();
    if (shared && !pad && !crop) {
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

    if (shared) {
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

// Divide "spatial" dimensions [1, ..., M] of the input, and interleaves these
// with the "batch" dimension (0).
class SpaceToBatch : public Resize {
 public:
  string Name() override { return "SpaceToBatch"; }
  string Operation() override { return "SpaceToBatchND"; }
};

// Reshapes the "batch" dimension 0 into M + 1 dimensions, and interleaves these
// back into the spatial dimensions [1, ..., M].
class BatchToSpace : public Resize {
 public:
  string Name() override { return "BatchToSpace"; }
  string Operation() override { return "BatchToSpaceND"; }
};

// Packs an array of rank-R tensors into one rank-(R+1) tensor.
class Pack : public Kernel {
 public:
  string Name() override { return "Pack"; }
  string Operation() override { return "Pack"; }

  bool Supports(Step *step) override {
    // Check inputs and outputs.
    if (step->indegree() != 1 || step->outdegree() != 1) return false;
    Tensor *x = step->input(0);
    Tensor *y = step->output(0);
    if (x->type() != y->type()) return false;
    if (x->elements() != y->elements()) return false;
    return true;
  }

  void Adjust(Step *step) override {
    step->output(0)->set_ref(step->input(0)->ref());
    CHECK(step->AllowInPlace(0, 0, true));
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    CHECK(step->input(0)->SharedWith(step->output(0)));
  }

  Placement Location() { return NOWHERE; }

  int64 Complexity(const Step *step) override {
    return 0;
  }
};

// Unpacks an array of a rank-R tensor into rank-(R-1) tensors.
class Unpack : public Kernel {
 public:
  string Name() override { return "Unpack"; }
  string Operation() override { return "Unpack"; }

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
    step->output(0)->set_ref(step->input(0)->ref());
    CHECK(step->AllowInPlace(0, 0, true));
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    CHECK(step->input(0)->SharedWith(step->output(0)));
  }

  Placement Location() { return NOWHERE; }

  int64 Complexity(const Step *step) override {
    return 0;
  }
};

// Output concatenation of input tensors along first dimension.
class BasicConcat : public Kernel {
 public:
  string Name() override { return "BasicConcat"; }
  string Operation() override { return "ConcatV2"; }

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

    return true;
  }

  void Adjust(Step *step) override {
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get the number of tensors to concatenate.
    int n = step->GetAttr("N", step->indegree() - 1);

    // Allocate registers.
    Register src = masm->rr().alloc_fixed(rsi);
    Register dst = masm->rr().alloc_fixed(rdi);
    Register cnt = masm->rr().alloc_fixed(rcx);
    Register acc = masm->rr().alloc_fixed(rax);
    Register in = masm->rr().alloc();
    Register out = masm->rr().alloc();

    // Load output tensor.
    __ LoadTensorAddress(out, step->output(0));

    // Copy input tensors to output.
    int offset = 0;
    for (int i = 0; i < n; ++i) {
      int size = step->input(i)->size();
      if (size > 0 && size < 16) {
        __ LoadTensorAddress(in, step->input(i));
        int disp = 0;
        int left = size;
        while (left >= 8) {
          __ movq(acc, Operand(in, disp));
          __ movq(Operand(out, offset + disp), acc);
          disp += 8;
          left -= 8;
        }
        while (left >= 4) {
          __ movl(acc, Operand(in, disp));
          __ movl(Operand(out, offset + disp), acc);
          disp += 4;
          left -= 4;
        }
        while (left >= 2) {
          __ movw(acc, Operand(in, disp));
          __ movw(Operand(out, offset + disp), acc);
          disp += 2;
          left -= 2;
        }
        while (left >= 1) {
          __ movb(acc, Operand(in, disp));
          __ movb(Operand(out, offset + disp), acc);
          disp += 1;
          left -= 1;
        }
      } else {
        __ LoadTensorAddress(src, step->input(i));
        __ leaq(dst, Operand(out, offset));
        __ movq(cnt, Immediate(size));
        __ repmovsb();
      }
      offset += size;
    }
    CHECK_EQ(offset, step->output(0)->size());
  }

  int64 Complexity(const Step *step) override {
    return 0;
  }
};

// Output concatenation of input tensors along any axis.
class GeneralConcat : public Kernel {
 public:
  string Name() override { return "GeneralConcat"; }
  string Operation() override { return "ConcatV2"; }

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
    }

    return true;
  }

  void Adjust(Step *step) override {
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get the number of tensors to concatenate.
    int n = step->GetAttr("N", step->indegree() - 1);

    // Allocate registers.
    Register src = masm->rr().alloc_fixed(rsi);
    Register dst = masm->rr().alloc_fixed(rdi);
    Register cnt = masm->rr().alloc_fixed(rcx);
    Register acc = masm->rr().alloc_fixed(rax);
    Register out = masm->rr().alloc();
    Register idx = masm->rr().alloc();
    std::vector<Register> in(n);
    for (int i = 0; i < n; ++i) in[i] = masm->rr().alloc();

    // Load input tensors.
    for (int i = 0; i < n; ++i) {
      __ LoadTensorAddress(in[i], step->input(i));
    }

    // Load output tensor.
    __ LoadTensorAddress(out, step->output(0));
    __ xorq(idx, idx);

    // Loop over outer prefix.
    Label l;
    int axis = step->input(n)->value<int32>();
    int prefix = step->output(0)->shape().outer(axis);
    __ bind(&l);

    // Copy input tensors to output.
    Tensor *output = step->output(0);
    for (int i = 0; i < n; ++i) {
      Tensor *input = step->input(i);
      int size = axis > 0 ? input->stride(axis - 1) : input->size();
      if (size > 0 && size < 16) {
        int disp = 0;
        int left = size;
        while (left >= 8) {
          __ movq(acc, Operand(in[i], disp));
          __ movq(Operand(out, disp), acc);
          disp += 8;
          left -= 8;
        }
        while (left >= 4) {
          __ movl(acc, Operand(in[i], disp));
          __ movl(Operand(out, disp), acc);
          disp += 4;
          left -= 4;
        }
        while (left >= 2) {
          __ movw(acc, Operand(in[i], disp));
          __ movw(Operand(out, disp), acc);
          disp += 2;
          left -= 2;
        }
        while (left >= 1) {
          __ movb(acc, Operand(in[i], disp));
          __ movb(Operand(out, disp), acc);
          disp += 1;
          left -= 1;
        }
      } else {
        __ movq(src, in[i]);
        __ movq(dst, out);
        __ movq(cnt, Immediate(size));
        __ repmovsb();
      }
      __ addq(in[i], Immediate(size));
    }

    // Next chunk.
    int size = axis > 0 ? output->stride(axis - 1) : output->size();
    __ addq(out, Immediate(size));
    __ incq(idx);
    __ cmpq(idx, Immediate(prefix));
    __ j(less, &l);
  }

  int64 Complexity(const Step *step) override {
    return 0;
  }
};

// Look up single embedding.
class SingleGather : public Kernel {
 public:
  string Name() override { return "SingleGather"; }
  string Operation() override { return "Gather"; }

  bool Supports(Step *step) override {
    // Check inputs and outputs.
    if (step->indegree() != 2 || step->outdegree() != 1) return false;

    // Check types.
    Tensor *M = step->input(0);
    Tensor *f = step->input(1);
    Tensor *v = step->output(0);
    int r = f->rank();
    if (f->type() != DT_INT32 || f->elements() != 1) return false;
    if (M->type() != DT_FLOAT || M->rank() != 2) return false;
    if (v->type() != DT_FLOAT || v->rank() != r + 1) return false;
    if (v->shape().outer(r) != 1 || v->dim(r) != M->dim(1)) return false;

    // Check that the output is not already a reference or a cell output.
    if (v->ref() || v->out()) return false;

    return true;
  }

  void Adjust(Step *step) override {
    // Make output a reference into the embedding matrix.
    Tensor *v = step->output(0);
    DCHECK(!v->ref());
    DCHECK(!v->out());
    v->set_ref(true);
    v->set_link(step->input(0));

    // Embedding matrix must be row-major.
    step->input(0)->SetRequiredOrder(ROW_MAJOR);
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get inputs and outputs.
    Tensor *M = step->input(0);
    Tensor *f = step->input(1);
    Tensor *v = step->output(0);
    CHECK(f->IsLocal());
    CHECK(v->IsLocal());
    CHECK(v->ref());

    // Allocate registers.
    Register acc = masm->rr().alloc();
    Register addr = masm->rr().alloc();
    Register embeddings = masm->rr().alloc();

    // Get feature index.
    if (f->ref()) {
      __ movq(addr, Operand(masm->instance(), f->offset()));
      __ movsxlq(acc, Operand(addr));
    } else {
      __ movsxlq(acc, Operand(masm->instance(), f->offset()));
    }

    // Compute offset in embedding.
    __ Multiply(acc, M->stride(0));

    // Lookup element in embedding.
    __ LoadTensorAddress(embeddings, M);
    __ addq(acc, embeddings);

    // Save reference to embedding vector.
    __ movq(Operand(masm->instance(), v->offset()), acc);
  }

  int64 Complexity(const Step *step) override {
    return 0;
  }
};

// Look up multiple features in embedding.
class MultiGather : public Kernel {
 public:
  string Name() override { return "MultiGather"; }
  string Operation() override { return "Gather"; }

  bool Supports(Step *step) override {
    // Check inputs and outputs.
    if (step->indegree() != 2 || step->outdegree() != 1) return false;

    // Check types.
    Tensor *M = step->input(0);
    Tensor *f = step->input(1);
    Tensor *v = step->output(0);
    int r = f->rank();
    int n = f->elements();
    if (f->type() != DT_INT32) return false;
    if (M->type() != DT_FLOAT || M->rank() != 2) return false;
    if (v->type() != DT_FLOAT || v->rank() != r + 1) return false;
    if (v->shape().outer(r) != n || v->dim(r) != M->dim(1)) return false;

    return true;
  }

  void Adjust(Step *step) override {
    // Embedding matrix must be row-major.
    step->input(0)->SetRequiredOrder(ROW_MAJOR);
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get inputs and outputs.
    Tensor *M = step->input(0);
    Tensor *f = step->input(1);
    Tensor *v = step->output(0);
    CHECK(f->IsLocal());
    CHECK(v->IsLocal());

    // Allocate registers.
    Register src = masm->rr().alloc_fixed(rsi);
    Register dst = masm->rr().alloc_fixed(rdi);
    Register cnt = masm->rr().alloc_fixed(rcx);
    Register acc = masm->rr().alloc();
    Register index = masm->rr().alloc();
    Register input = masm->rr().alloc();
    Register embeddings = masm->rr().alloc();

    // Load tensor locations.
    __ LoadTensorAddress(embeddings, M);
    __ LoadTensorAddress(input, f);
    __ LoadTensorAddress(dst, v);

    // Loop over all feature indices.
    Label l;
    __ xorq(index, index);
    __ bind(&l);

    // Get feature index.
    __ movsxlq(acc, Operand(input, index, times_4));

    // Compute address in embedding.
    __ movq(src, embeddings);
    __ Multiply(acc, M->stride(0));
    __ addq(src, acc);

    // Copy embedding vector to output.
    __ movq(cnt, Immediate(M->stride(0)));
    __ repmovsb();

    // Next feature index.
    __ incq(index);
    __ cmpq(index, Immediate(f->elements()));
    __ j(less, &l);
  }

  int64 Complexity(const Step *step) override {
    return 0;
  }
};

// Look up multiple features in embedding with pooling.
class PoolingGather : public Kernel {
 public:
  // Pooling operations.
  enum Pooling {SUM, AVG, MAX};

  PoolingGather(Pooling pooling) : pooling_(pooling) {}

  string Name() override { return Operation(); }
  string Operation() override {
    switch (pooling_) {
      case SUM: return "GatherSum";
      case AVG: return "GatherAvg";
      case MAX: return "GatherMax";
      default: return "???";
    }
  }

  bool Supports(Step *step) override {
    // Requires SSE or AVX support.
    if (!CPU::Enabled(AVX) && !CPU::Enabled(SSE)) return false;

    // Check inputs and outputs.
    if (step->indegree() != 2 || step->outdegree() != 1) return false;

    // Check types.
    Tensor *M = step->input(0);
    Tensor *f = step->input(1);
    Tensor *v = step->output(0);
    if (M->type() != DT_FLOAT || M->rank() != 2) return false;
    if (f->type() != DT_INT32) return false;
    if (v->type() != DT_FLOAT || v->elements() != M->dim(1)) return false;

    return true;
  }

  void Adjust(Step *step) override {
    // Embedding matrix must be row-major.
    step->input(0)->SetRequiredOrder(ROW_MAJOR);
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get inputs and outputs.
    Tensor *M = step->input(0);
    Tensor *f = step->input(1);
    Tensor *v = step->output(0);
    CHECK(f->IsLocal());
    CHECK(v->IsLocal());

    // Allocate registers.
    Register acc = masm->rr().alloc_fixed(rax);
    Register src = masm->rr().alloc_fixed(rsi);
    Register dst = masm->rr().alloc_fixed(rdi);
    Register cnt = masm->rr().alloc_fixed(rcx);
    Register ofs = cnt;
    Register fidx = masm->rr().alloc();
    Register fcnt = masm->rr().alloc();
    Register embeddings = masm->rr().alloc();
    Register input = masm->rr().alloc();
    Register output = masm->rr().alloc();
    YMMRegister elem = masm->mm().allocy();
    XMMRegister xelem = elem.xmm();

    // Load tensor locations.
    __ LoadTensorAddress(embeddings, M);
    __ LoadTensorAddress(input, f);
    __ LoadTensorAddress(output, v);

    // Zero feature index and feature count.
    __ xorq(fidx, fidx);
    __ xorq(fcnt, fcnt);

    // Find first (non-negative) feature.
    Label l1, l2, done;
    __ bind(&l1);
    __ movsxlq(acc, Operand(input, fidx, times_4));
    __ testq(acc, acc);
    __ j(positive, &l2);
    __ incq(fidx);
    __ cmpq(fidx, Immediate(f->elements()));
    __ j(less, &l1);

    // No feature found; zero output vector.
    __ xorq(acc, acc);
    __ movq(dst, output);
    __ movq(cnt, Immediate(v->size()));
    __ repstosb();
    __ jmp(&done);

    // First non-negative feature found; copy its embedding vector to output.
    __ bind(&l2);
    __ movq(src, embeddings);
    __ Multiply(acc, M->stride(0));
    __ addq(src, acc);
    __ movq(dst, output);
    __ movq(cnt, Immediate(M->stride(0)));
    __ repmovsb();
    __ incq(fcnt);

    // Go over the remaining features.
    Label l3, l4;
    __ bind(&l3);
    __ incq(fidx);
    __ cmpq(fidx, Immediate(f->elements()));
    __ j(equal, &l4);
    __ movsxlq(acc, Operand(input, fidx, times_4));
    __ testq(acc, acc);
    __ j(negative, &l3);

    // Combine embedding vector for feature with current result.
    __ incq(fcnt);
    __ movq(src, embeddings);
    __ Multiply(acc, M->stride(0));
    __ addq(src, acc);

    // Update output vector with embedding vector for feature.
    if (masm->Enabled(AVX)) {
      // Combine elements using AVX vectors.
      if (v->elements() >= 8) {
        Label next;
        __ xorq(ofs, ofs);
        __ bind(&next);
        __ vmovaps(elem, Operand(src, ofs));
        if (pooling_ == MAX) {
          __ vmaxps(elem, elem, Operand(dst, ofs));
        } else {
          __ vaddps(elem, elem, Operand(dst, ofs));
        }
        __ vmovaps(Operand(output, ofs), elem);
        __ addq(ofs, Immediate(8 * sizeof(float)));
        __ cmpq(ofs, Immediate(v->size()));
        __ j(less, &next);
      }

      // Combine residual elements.
      int disp = (v->elements() / 8) * 8 * sizeof(float);
      for (int i = 0; i < v->size() % 8; ++i) {
        __ vmovss(elem, Operand(src, disp));
        if (pooling_ == MAX) {
          __ vmaxss(elem, elem, Operand(dst, disp));
        } else {
          __ vaddss(elem, elem, Operand(dst, disp));
        }
        __ vmovss(Operand(output, disp), elem);
        disp += sizeof(float);
      }
    } else {
      // Combine elements using SSE vectors.
      if (v->elements() >= 4) {
        Label next;
        __ xorq(ofs, ofs);
        __ bind(&next);
        __ movaps(xelem, Operand(src, ofs));
        if (pooling_ == MAX) {
          __ maxps(xelem, Operand(output, ofs));
        } else {
          __ addps(xelem, Operand(output, ofs));
        }
        __ movaps(Operand(output, ofs), xelem);
        __ addq(ofs, Immediate(4 * sizeof(float)));
        __ cmpq(ofs, Immediate(v->size()));
        __ j(less, &next);
      }

      // Combine residual elements.
      int disp = (v->elements() / 4) * 4 * sizeof(float);
      for (int i = 0; i < v->size() % 4; ++i) {
        __ movss(xelem, Operand(src, disp));
        if (pooling_ == MAX) {
          __ maxss(xelem, Operand(output, disp));
        } else {
          __ addss(xelem, Operand(output, disp));
        }
        __ movss(Operand(output, disp), xelem);
        disp += sizeof(float);
      }
    }

    // Next feature.
    __ jmp(&l3);
    __ bind(&l4);

    // Compute average.
    if (pooling_ == AVG) {
      __ movq(dst, output);
      if (masm->Enabled(AVX)) {
        // Compute 1/fcnt.
        YMMRegister scalar = masm->mm().allocy();
        __ vcvtqsi2ss(scalar.xmm(), scalar.xmm(), fcnt);
        __ vrcpss(scalar.xmm(), scalar.xmm(), scalar.xmm());
        __ vbroadcastss(scalar, scalar);

        // Multiply all output elements with scalar to get the average.
        if (v->elements() >= 8) {
          Label next;
          __ xorq(ofs, ofs);
          __ bind(&next);
          __ vmulps(elem, scalar, Operand(output, ofs));
          __ vmovaps(Operand(output, ofs), elem);
          __ addq(ofs, Immediate(8 * sizeof(float)));
          __ cmpq(ofs, Immediate(v->size()));
          __ j(less, &next);
        }
        int disp = (v->elements() / 8) * 8 * sizeof(float);
        for (int i = 0; i < v->size() % 8; ++i) {
          __ vmulss(xelem, scalar.xmm(), Operand(output, disp));
          __ vmovss(Operand(output, disp), elem);
          disp += sizeof(float);
        }
      } else {
        // Compute 1/fcnt.
        XMMRegister scalar = masm->mm().allocx();
        __ cvtqsi2ss(scalar, fcnt);
        __ rcpss(scalar, scalar);

        // Multiply all output elements with scalar to get the average.
        Label next;
        __ xorq(ofs, ofs);
        __ bind(&next);
        __ movss(xelem, Operand(output, ofs));
        __ mulss(xelem, scalar);
        __ movss(Operand(output, ofs), xelem);
        __ addq(ofs, Immediate(sizeof(float)));
        __ cmpq(ofs, Immediate(v->size()));
        __ j(less, &next);
      }
    }

    __ bind(&done);
  }

  int64 Complexity(const Step *step) override {
    Tensor *M = step->input(0);
    Tensor *f = step->input(1);
    return M->dim(1) * f->elements();
  }

 private:
  Pooling pooling_;  // pooling operation for combining vectors
};

// Register array kernels.
void RegisterArrayKernels(Library *library) {
  library->Register(new Reshape());
  library->Register(new Squeeze());
  library->Register(new ExpandDims());
  library->Register(new SpaceToBatch());
  library->Register(new BatchToSpace());
  library->Register(new Pack());
  library->Register(new Unpack());
  library->Register(new GeneralConcat());
  library->Register(new BasicConcat());
  library->Register(new MultiGather());
  library->Register(new SingleGather());
  library->Register(new PoolingGather(PoolingGather::SUM));
  library->Register(new PoolingGather(PoolingGather::AVG));
  library->Register(new PoolingGather(PoolingGather::MAX));
}

}  // namespace myelin
}  // namespace sling
