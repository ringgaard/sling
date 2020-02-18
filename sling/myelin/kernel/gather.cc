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
#include "sling/myelin/simd-assembler.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Look up single embedding.
class SingleGather : public Kernel {
 public:
  string Name() override { return "SingleGather"; }
  string Operation() override { return "Gather"; }

  bool Supports(Step *step) override {
    // Check inputs and outputs.
    if (step->indegree() != 2 && step->indegree() != 3) return false;
    if (step->outdegree() != 1) return false;

    // Check types.
    Tensor *M = step->input(0);
    Tensor *f = step->input(1);
    Tensor *oov = step->indegree() == 3 ? step->input(2) : nullptr;
    Tensor *v = step->output(0);
    Type type = M->type();
    if (f->type() != DT_INT32) return false;
    if (M->rank() != 2) return false;
    if (v->type() != type) return false;
    if (oov != nullptr && oov->type() != type) return false;
    int n = f->elements();
    int d = M->dim(1);
    int r = v->rank() - 1;
    if (v->shape().outer(r) != n) return false;
    if (v->shape().inner(r) != d) return false;
    if (oov != nullptr && v->shape().inner(r) != oov->elements()) return false;
    if (n != 1) return false;

    // Check that the output is not already a reference.
    if (v->ref()) return false;

    return true;
  }

  void Adjust(Step *step) override {
    // Make output a reference into the embedding matrix.
    Tensor *v = step->output(0);
    DCHECK(!v->ref());
    v->set_ref(true);
    v->Link(step->input(0));
    if (step->indegree() == 3) v->Link(step->input(2));

    // Embedding matrix must be row-major.
    step->input(0)->RequireOrder(ROW_MAJOR);
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get inputs and outputs.
    Tensor *M = step->input(0);
    Tensor *f = step->input(1);
    Tensor *oov = step->indegree() == 3 ? step->input(2) : nullptr;
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

    // Check for OOV feature.
    Label l1;
    if (oov != nullptr) {
      __ testq(acc, acc);
      __ j(negative, &l1);
    }

    // Compute offset in embedding.
    __ Multiply(acc, M->stride(0));

    // Lookup element in embedding.
    __ LoadTensorAddress(embeddings, M);
    __ addq(acc, embeddings);

    // Use oov vector for negative features.
    if (oov != nullptr) {
      Label l2;
      __ jmp(&l2);
      __ bind(&l1);
      __ LoadTensorAddress(acc, oov);
      __ bind(&l2);
    }

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
    if (step->indegree() != 2 && step->indegree() != 3) return false;
    if (step->outdegree() != 1) return false;

    // Check types.
    Tensor *M = step->input(0);
    Tensor *f = step->input(1);
    Tensor *oov = step->indegree() == 3 ? step->input(2) : nullptr;
    Tensor *v = step->output(0);
    Type type = M->type();
    if (f->type() != DT_INT32) return false;
    if (M->rank() != 2) return false;
    if (v->type() != type) return false;
    if (oov != nullptr && oov->type() != type) return false;
    int n = f->elements();
    int d = M->dim(1);
    int r = v->rank() - 1;
    if (v->shape().outer(r) != n) return false;
    if (v->shape().inner(r) != d) return false;
    if (oov != nullptr && v->shape().inner(r) != oov->elements()) return false;

    return true;
  }

  void Adjust(Step *step) override {
    // Embedding matrix must be row-major.
    step->input(0)->RequireOrder(ROW_MAJOR);
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get inputs and outputs.
    Tensor *M = step->input(0);
    Tensor *f = step->input(1);
    Tensor *oov = step->indegree() == 3 ? step->input(2) : nullptr;
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

    // Check for OOV feature.
    Label l1;
    if (oov != nullptr) {
      __ testq(acc, acc);
      __ j(negative, &l1);
    }

    // Compute address in embedding.
    __ movq(src, embeddings);
    __ Multiply(acc, M->stride(0));
    __ addq(src, acc);

    // Use oov vector for negative features.
    if (oov != nullptr) {
      Label l2;
      __ jmp(&l2);
      __ bind(&l1);
      __ LoadTensorAddress(src, oov);
      __ bind(&l2);
    }

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
    // Check inputs and outputs.
    if (step->indegree() != 2 || step->outdegree() != 1) return false;

    // Check types.
    Tensor *M = step->input(0);
    Tensor *f = step->input(1);
    Tensor *v = step->output(0);
    if (!SIMDAssembler::Supports(M->type()) || M->rank() != 2) return false;
    if (f->type() != DT_INT32 || f->rank() != 2) return false;
    if (v->type() != M->type() || v->elements() != M->dim(1)) return false;
    if (pooling_ == AVG) {
      if (M->type() != DT_FLOAT && M->type() != DT_DOUBLE) return false;
      if (!CPU::Enabled(SSE2)) return false;
    }

    return true;
  }

  void Adjust(Step *step) override {
    Tensor *M = step->input(0);
    Tensor *v = step->output(0);

    // Align to one vector register.
    Type type = M->type();
    int vecbytes = SIMDAssembler::VectorBytes(type);
    M->SetMiniumAlignment(vecbytes);
    v->SetMiniumAlignment(vecbytes);

    // Embedding matrix must be row-major.
    M->RequireOrder(ROW_MAJOR);

    // Reserve registers.
    int regs = SIMDAssembler::RegisterUsage(type) + 9;
    step->SetRegisterUsage(regs);
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get inputs and outputs.
    Tensor *M = step->input(0);
    Tensor *f = step->input(1);
    Tensor *v = step->output(0);
    int n = v->elements();

    // Create SIMD code generators.
    Type type = M->type();
    int dsize = TypeTraits::of(type).size();
    int vecbytes = SIMDAssembler::VectorBytes(type);
    bool aligned = M->stride(0) % vecbytes == 0;
    SIMDAssembler sasm(masm, type, aligned);
    step->set_variant(sasm.name());

    // Compute vector processing strategy.
    SIMDStrategy strategy(&sasm, n);
    strategy.PreloadMasks();

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
    auto elem = sasm.alloc(strategy.MaxUnrolls());

    // Load tensor locations.
    __ LoadTensorAddress(embeddings, M);
    __ LoadTensorAddress(input, f);
    __ LoadTensorAddress(output, v);

    // Zero feature index and feature count.
    __ xorq(fidx, fidx);
    if (pooling_ == AVG) {
      __ xorq(fcnt, fcnt);
    }

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
    if (pooling_ == AVG) {
      __ incq(fcnt);
    }

    // Go over the remaining features.
    Label l3, l4;
    __ bind(&l3);
    __ incq(fidx);
    __ cmpq(fidx, Immediate(f->elements()));
    __ j(equal, &l4);
    __ movsxlq(acc, Operand(input, fidx, times_4));
    __ testq(acc, acc);
    __ j(negative, &l4);

    // Combine embedding vector for feature with current result.
    if (pooling_ == AVG) {
      __ incq(fcnt);
    }
    __ movq(src, embeddings);
    __ Multiply(acc, M->stride(0));
    __ addq(src, acc);

    // Update output vector with embedding vector for feature.
    Reduction op = pooling_ == MAX ? REDUCE_MAX : REDUCE_ADD;
    for (auto &phase : strategy.phases()) {
      auto *gen = phase.generator;
      int vecsize = gen->VectorSize();
      int blkstart = phase.offset * dsize;
      int blksize = phase.unrolls * vecsize * dsize;

      if (phase.repeat > 1) {
        // Repeated phase.
        Label lu;
        if (blkstart == 0) {
          __ xorq(ofs, ofs);
        } else {
          __ movq(ofs, Immediate(blkstart));
        }
        __ bind(&lu);
        for (int i = 0; i < phase.unrolls; ++i) {
          int disp = i * vecsize * dsize;
          gen->Load(elem[i], Operand(src, ofs, times_1, disp));
          gen->Accumulate(op, elem[i], Operand(output, ofs, times_1, disp));
          gen->Store(Operand(output, ofs, times_1, disp), elem[i]);
        }
        __ addq(ofs, Immediate(blksize));
        __ cmpq(ofs, Immediate(blkstart + phase.repeat * blksize));
        __ j(less, &lu);
      } else if (phase.masked == 0) {
        // Residual phase.
        for (int i = 0; i < phase.unrolls; ++i) {
          int disp = blkstart + i * vecsize * dsize;
          gen->Load(elem[i], Operand(src, disp));
          gen->Accumulate(op, elem[i], Operand(output, disp));
          gen->Store(Operand(output, disp), elem[i]);
        }
      } else {
        // Masked phase.
        CHECK_EQ(phase.unrolls, 1);
        gen->MaskedLoad(elem[0], Operand(src, blkstart));
        gen->MaskedAccumulate(op, elem[0], Operand(output, blkstart));
        gen->MaskedStore(Operand(output, blkstart), elem[0]);
      }
    }

    // Next feature.
    __ jmp(&l3);
    __ bind(&l4);

    // Compute average.
    if (pooling_ == AVG) {
      // Compute 1/fcnt.
      int scalar = sasm.alloc();
      XMMRegister sr = jit::XMMRegister::from_code(scalar);
      if (masm->Enabled(AVX)) {
        __ vcvtqsi2ss(sr, sr, fcnt);
        __ vrcpss(sr, sr, sr);
        if (type == DT_DOUBLE) {
          __ vcvtss2sd(sr, sr, sr);
        }
      } else {
        __ cvtqsi2ss(sr, fcnt);
        __ rcpss(sr, sr);
        if (type == DT_DOUBLE) {
          CHECK(masm->Enabled(SSE2));
          __ cvtss2sd(sr, sr);
        }
      }
      sasm.main()->Broadcast(scalar, scalar);

      // Multiply all output elements with scalar to get the average.
      for (auto &phase : strategy.phases()) {
        auto *gen = phase.generator;
        int vecsize = gen->VectorSize();
        int blkstart = phase.offset * dsize;
        int blksize = phase.unrolls * vecsize * dsize;

        if (phase.repeat > 1) {
          // Repeated phase.
          Label lu;
          if (blkstart == 0) {
            __ xorq(ofs, ofs);
          } else {
            __ movq(ofs, Immediate(blkstart));
          }
          __ bind(&lu);
          for (int i = 0; i < phase.unrolls; ++i) {
            int disp = i * vecsize * dsize;
            gen->Mul(elem[i], scalar, Operand(output, ofs, times_1, disp));
            gen->Store(Operand(output, ofs, times_1, disp), elem[i]);
          }
          __ addq(ofs, Immediate(blksize));
          __ cmpq(ofs, Immediate(blkstart + phase.repeat * blksize));
          __ j(less, &lu);
        } else if (phase.masked == 0) {
          // Residual phase.
          for (int i = 0; i < phase.unrolls; ++i) {
            int disp = blkstart + i * vecsize * dsize;
            gen->Mul(elem[i], scalar, Operand(output, disp));
            gen->Store(Operand(output, disp), elem[i]);
          }
        } else {
          // Masked phase.
          CHECK_EQ(phase.unrolls, 1);
          gen->MaskedMul(elem[0], scalar, Operand(output, blkstart));
          gen->MaskedStore(Operand(output, blkstart), elem[0]);
        }
      }
    }

    __ bind(&done);
  }

  int64 Complexity(const Step *step) override {
    Tensor *M = step->input(0);
    Tensor *f = step->input(1);
    return M->dim(1) * f->elements() + (pooling_ == AVG ? M->dim(1) : 0);
  }

 private:
  Pooling pooling_;  // pooling operation for combining vectors
};

// Accumulate sparse (scaled) input.
class AssignAddScatter : public Kernel {
 public:
  AssignAddScatter(bool scale) : scale_(scale) {}

  string Name() override { return Operation(); }
  string Operation() override {
    return scale_ ? "AssignAddMulScatter" : "AssignAddScatter";
  }

  bool Supports(Step *step) override {
    // Check inputs and outputs.
    Args args(step, scale_);
    if (!args.valid) return false;

    // Check arguments.
    Type type = args.var->type();
    if (!SIMDAssembler::Supports(type)) return false;
    if (args.var->rank() != 2) return false;
    if (args.var->constant()) return false;
    if (args.indices->type() != DT_INT32) return false;
    if (args.indices->rank() != 2) return false;
    if (args.value->type() != type || args.value->rank() != 2) return false;
    if (args.value->dim(1) != args.var->dim(1)) return false;
    if (args.value->dim(0) != 1 &&
        args.value->dim(0) != args.indices->dim(1)) {
      return false;
    }
    if (scale_) {
      if (args.scaler->type() != type) return false;
      if (args.scaler->elements() != 1) return false;
    }
    if (args.ref) {
      if (args.ref->type() != type) return false;
      if (args.ref->shape() != args.var->shape()) return false;
      if (!args.ref->ref()) return false;
    }

    return true;
  }

  void Adjust(Step *step, const Options &options) override {
    Args args(step, scale_);

    // Add sparsity bitmap index.
    if (options.sparse_threshold > 0 &&
        args.var->dim(0) >= options.sparse_threshold &&
        args.var->IsLocal() &&
        step->GetAttr("sparse", true)) {
      Tensor *sparse = args.var->MakeSparse();
      if (args.ref) args.ref->set_sparse(sparse);
    }

    // Link output reference to input variable.
    if (args.ref) args.var->Link(args.ref);

    // Align to one vector register.
    Type type = args.var->type();
    int vecbytes = SIMDAssembler::VectorBytes(type);
    args.var->SetMiniumAlignment(vecbytes);
    args.value->SetMiniumAlignment(vecbytes);

    // Embedding matrix must be row-major.
    args.var->RequireOrder(ROW_MAJOR);

    // Reserve registers.
    int regs = SIMDAssembler::RegisterUsage(type) + 8;
    if (args.scaler) regs++;
    step->SetRegisterUsage(regs);
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get inputs.
    Args args(step, scale_);
    Tensor *sparse = args.var->sparse();
    bool single = args.indices->elements() == 1;
    int n = args.value->dim(1);

    // Create SIMD code generators.
    Type type = args.var->type();
    int dsize = TypeTraits::of(type).size();
    int vecbytes = SIMDAssembler::VectorBytes(type);
    bool aligned = args.var->stride(0) % vecbytes == 0;
    SIMDAssembler sasm(masm, type, aligned);
    step->set_variant(sasm.name());

    // Compute vector processing strategy.
    SIMDStrategy strategy(&sasm, n);
    strategy.PreloadMasks();

    // Allocate registers.
    Register bit = masm->rr().alloc_fixed(rcx);
    Register acc = masm->rr().alloc();
    Register varaddr = masm->rr().alloc();
    Register idxaddr = masm->rr().alloc();
    Register valaddr = masm->rr().alloc();
    Register bmaddr = masm->rr().alloc();
    Register fidx = masm->rr().alloc();
    Register ofs = masm->rr().alloc();
    Register src = bit;
    Register aux = ofs;
    auto elem = sasm.alloc(strategy.MaxUnrolls());
    int factor = args.scaler ? sasm.alloc() : -1;

    // Load tensor locations.
    __ LoadTensorAddress(varaddr, args.var);
    __ LoadTensorAddress(idxaddr, args.indices);
    __ LoadTensorAddress(valaddr, args.value);
    if (sparse) {
      __ LoadTensorAddress(bmaddr, sparse);
    }

    // Optionally output reference to assigned variable.
    if (args.ref != nullptr) {
      CHECK(args.ref->IsLocal());
      CHECK(args.ref->ref());
      __ movq(Operand(masm->instance(), args.ref->offset()), varaddr);
    }

    // Load scaling value.
    if (args.scaler) {
      __ LoadTensorAddress(src, args.scaler);
      sasm.main()->Broadcast(factor, Operand(src));
    }

    // Loop over features.
    if (!single) {
      __ xorq(fidx, fidx);
    }
    Label l1, l2;
    __ bind(&l1);
    if (single) {
      __ movsxlq(acc, Operand(idxaddr));
    } else {
      __ movsxlq(acc, Operand(idxaddr, fidx, times_4));
    }
    __ testq(acc, acc);
    __ j(negative, &l2);

    // Update sparsity bitmap.
    if (sparse) {
      __ movq(bit, acc);
      __ movq(aux, Immediate(1));
      __ shlq_cl(aux);
      __ shrq(bit, Immediate(6));
      __ orq(Operand(bmaddr, bit, times_8), aux);
    }

    //  Look up address of index in embedding.
    __ Multiply(acc, args.var->stride(0));
    __ addq(acc, varaddr);

    // Update OOV vector for missing features.
    if (args.oov) {
      Label l3;
      __ jmp(&l3);
      __ bind(&l2);
      __ LoadTensorAddress(acc, args.oov);
      __  bind(&l3);
    }

    // Add (scaled) input vector for feature to embedding vector.
    for (auto &phase : strategy.phases()) {
      auto *gen = phase.generator;
      int vecsize = gen->VectorSize();
      int blkstart = phase.offset * dsize;
      int blksize = phase.unrolls * vecsize * dsize;

      if (phase.repeat > 1) {
        // Repeated phase.
        Label lu;
        if (blkstart == 0) {
          __ xorq(ofs, ofs);
        } else {
          __ movq(ofs, Immediate(blkstart));
        }
        __ bind(&lu);
        for (int i = 0; i < phase.unrolls; ++i) {
          int disp = i * vecsize * dsize;
          gen->Load(elem[i], Operand(acc, ofs, times_1, disp));
          if (scale_) {
            gen->MulAdd(elem[i], factor, Operand(valaddr, ofs, times_1, disp),
                        true);
          } else {
            gen->Add(elem[i], elem[i], Operand(valaddr, ofs, times_1, disp));
          }
          gen->Store(Operand(acc, ofs, times_1, disp), elem[i]);
        }
        __ addq(ofs, Immediate(blksize));
        __ cmpq(ofs, Immediate(blkstart + phase.repeat * blksize));
        __ j(less, &lu);
      } else if (phase.masked == 0) {
        // Residual phase.
        for (int i = 0; i < phase.unrolls; ++i) {
          int disp = blkstart + i * vecsize * dsize;
          gen->Load(elem[i], Operand(acc, disp));
          if (scale_) {
            gen->MulAdd(elem[i], factor, Operand(valaddr, disp), true);
          } else {
            gen->Add(elem[i], elem[i], Operand(valaddr, disp));
          }
          gen->Store(Operand(acc, disp), elem[i]);
        }
      } else {
        // Masked phase.
        CHECK_EQ(phase.unrolls, 1);
        gen->MaskedLoad(elem[0], Operand(acc, blkstart));
        if (scale_) {
          gen->MaskedMulAdd(elem[0], factor, Operand(valaddr, blkstart));
        } else {
          gen->MaskedAdd(elem[0], elem[0], Operand(valaddr, blkstart));
        }
        gen->MaskedStore(Operand(acc, blkstart), elem[0]);
      }
    }

    if (args.value->dim(0) != 1) {
      __ addq(valaddr, Immediate(args.value->stride(0)));
    }

    if (!single) {
      __ incq(fidx);
      __ cmpq(fidx, Immediate(args.indices->elements()));
      __ j(less, &l1);
    }
    if (args.oov == nullptr) {
      __ bind(&l2);
    }
  }

  int64 Complexity(const Step *step) override {
    Tensor *indices = step->input(1);
    Tensor *value = step->input(2);
    return value->elements() * indices->elements() * (scale_ ? 2 : 1);
  }

 private:
  // Arguments to scatter op.
  struct Args {
    Args(Step *step, bool scale) {
      if (step->indegree() < 3) return;
      if (step->outdegree() > 1) return;
      var = step->input(0);
      indices = step->input(1);
      value = step->input(2);
      if (step->outdegree() > 0) ref = step->output(0);

      if (scale) {
        if (step->indegree() != 4 && step->indegree() != 5) return;
        if (step->indegree() > 3) scaler = step->input(3);
        if (step->indegree() > 4) oov = step->input(4);
      } else {
        if (step->indegree() != 3 && step->indegree() != 4) return;
        if (step->indegree() > 3) oov = step->input(3);
      }
      valid = true;
    }

    bool valid = false;
    Tensor *var = nullptr;
    Tensor *indices = nullptr;
    Tensor *value = nullptr;
    Tensor *scaler = nullptr;
    Tensor *ref = nullptr;
    Tensor *oov = nullptr;
  };

  bool scale_;  // scale input
};

// Register gather/scatter kernels.
void RegisterGatherKernels(Library *library) {
  library->Register(new MultiGather());
  library->Register(new SingleGather());
  library->Register(new PoolingGather(PoolingGather::SUM));
  library->Register(new PoolingGather(PoolingGather::AVG));
  library->Register(new PoolingGather(PoolingGather::MAX));
  library->Register(new AssignAddScatter(false));
  library->Register(new AssignAddScatter(true));
}

}  // namespace myelin
}  // namespace sling
