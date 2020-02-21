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

// Arguments for gather op.
struct GatherArgs {
  GatherArgs(const Step *step, bool pooling = false) {
    // Get arguments.
    if (step->indegree() != 2 && step->indegree() != 3) return;
    if (pooling && step->indegree() == 3) return;
    if (step->outdegree() != 1) return;
    params = step->input(0);
    indices = step->input(1);
    if (step->indegree() == 3) oov = step->input(2);
    result = step->output(0);

    // Check types.
    if (indices->type() != DT_INT32) return;
    if (result->type() != params->type()) return;
    if (oov != nullptr && oov->type() != params->type()) return;

    // Check shapes.
    int b = step->GetAttr("batch", -1);
    int r = indices->rank();
    if (r > 0) n = indices->dim(-1);
    feature = indices->shape().outside(r - 1);
    if (b >= 0) {
      batch = feature.outside(b);
      feature = feature.inside(b);
    }
    index = params->shape().outside(n);
    element = params->shape().inside(n);
    if (index.rank() != n) return;
    if (pooling) {
      if (result->shape() != batch + element) return;
    } else {
      if (result->shape() != batch + feature + element) return;
    }
    if (oov != nullptr) {
      if (oov->shape() != element) return;
    }
    valid = true;
  }

  // Return the number of outer elements (batch + feature).
  int outer_elements() const {
    return batch.elements() * feature.elements();
  }

  // Return the number of elements in parameter slices.
  int slice_elements() const {
    return element.elements();
  }

  // Return the parameter slices size.
  int slice_size() const {
    return params->stride(n - 1);
  }

  bool valid = false;         // arguments are valid
  Tensor *params = nullptr;   // T[N,E] tensor from which to gather values
  Tensor *indices = nullptr;  // int32[B,F,{N}] tensor with indices to gather
  Tensor *oov = nullptr;      // optional T[E] tensor for invalid indices
  Tensor *result = nullptr;   // T[B,F,E] tensor with result

  int n = 1;                  // number of parameter index dimensions
  Shape batch;                // batch shape in indices (B)
  Shape feature;              // feature shape in indices (F)
  Shape index;                // embedding index shape (N)
  Shape element;              // embedding element shape (E)
};

// Look up single embedding.
class SingleGather : public Kernel {
 public:
  string Name() override { return "SingleGather"; }
  string Operation() override { return "Gather"; }

  bool Supports(Step *step) override {
    // Check arguments.
    GatherArgs args(step);
    if (!args.valid) return false;

    // This kernel only supports single lookup.
    if (args.indices->elements() != 1) return false;
    if (args.n != 1) return false;
    if (!args.indices->IsLocal()) return false;
    if (!args.result->IsLocal()) return false;

    // Check that the output is not already a reference.
    if (args.result->ref()) return false;

    return true;
  }

  void Adjust(Step *step) override {
    // Make output a reference into the embedding matrix.
    GatherArgs args(step);
    DCHECK(!args.result->ref());
    args.result->set_ref(true);
    args.result->Link(args.params);
    if (args.oov != nullptr) args.result->Link(args.oov);

    // Embedding matrix must be row-major.
    args.params->RequireOrder(ROW_MAJOR);
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get inputs and outputs.
    GatherArgs args(step);
    CHECK(args.result->ref());

    // Allocate registers.
    Register acc = masm->rr().alloc();
    Register addr = masm->rr().alloc();
    Register params = masm->rr().alloc();

    // Get feature index.
    if (args.indices->ref()) {
      __ movq(addr, Operand(masm->instance(), args.indices->offset()));
      __ movsxlq(acc, Operand(addr));
    } else {
      __ movsxlq(acc, Operand(masm->instance(), args.indices->offset()));
    }

    // Check for OOV feature.
    Label l1;
    if (args.oov != nullptr) {
      __ testq(acc, acc);
      __ j(negative, &l1);
    }

    // Compute offset in embedding.
    __ Multiply(acc, args.params->stride(0));

    // Lookup element in embedding.
    __ LoadTensorAddress(params, args.params);
    __ addq(acc, params);

    // Use oov vector for negative features.
    if (args.oov != nullptr) {
      Label l2;
      __ jmp(&l2);
      __ bind(&l1);
      __ LoadTensorAddress(acc, args.oov);
      __ bind(&l2);
    }

    // Save reference to embedding vector.
    __ movq(Operand(masm->instance(), args.result->offset()), acc);
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
    // Check arguments.
    GatherArgs args(step);
    if (!args.valid) return false;

    return true;
  }

  void Adjust(Step *step) override {
    // Embedding matrix must be row-major.
    GatherArgs args(step);
    args.params->RequireOrder(ROW_MAJOR);
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get inputs and outputs.
    GatherArgs args(step);

    // Allocate registers.
    Register src = masm->rr().alloc_fixed(rsi);
    Register dst = masm->rr().alloc_fixed(rdi);
    Register cnt = masm->rr().alloc_fixed(rcx);
    Register acc = masm->rr().alloc();
    Register feature = masm->rr().alloc();
    Register indices = masm->rr().alloc();
    Register params = masm->rr().alloc();

    // Load tensor locations.
    __ LoadTensorAddress(params, args.params);
    __ LoadTensorAddress(indices, args.indices);
    __ LoadTensorAddress(dst, args.result);

    // Loop over all batch and feature indices.
    Label lf;
    if (args.outer_elements() > 1) {
      __ xorq(feature, feature);
      __ bind(&lf);
    }

    // Compute address in embedding for index.
    Label l1;
    __ movq(src, params);
    for (int d = 0; d < args.n; ++d) {
      // Get feature index.
      __ movsxlq(acc, Operand(indices, d * sizeof(int32)));

      // Check for OOV feature.
      if (args.oov != nullptr) {
        __ testq(acc, acc);
        __ j(negative, &l1);
      }

      // Compute offset for index dimension.
      __ Multiply(acc, args.params->stride(d));
      __ addq(src, acc);
    }

    // Use oov vector for negative features.
    if (args.oov != nullptr) {
      Label l2;
      __ jmp(&l2);
      __ bind(&l1);
      __ LoadTensorAddress(src, args.oov);
      __ bind(&l2);
    }

    // Copy embedding vector to output.
    __ movq(cnt, Immediate(args.slice_size()));
    __ repmovsb();

    // Next feature index.
    if (args.outer_elements() > 1) {
      __ addq(indices, Immediate(args.n * sizeof(int32)));
      __ incq(feature);
      __ cmpq(feature, Immediate(args.outer_elements()));
      __ j(less, &lf);
    }
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
    // Check arguments.
    GatherArgs args(step, true);
    if (!args.valid) return false;

    // Check types.
    Type type = args.params->type();
    if (!SIMDAssembler::Supports(type)) return false;
    if (pooling_ == AVG) {
      if (type != DT_FLOAT && type != DT_DOUBLE) return false;
      if (!CPU::Enabled(SSE2)) return false;
    }

    return true;
  }

  void Adjust(Step *step) override {
    GatherArgs args(step, true);

    // Align to one vector register.
    Type type = args.params->type();
    int vecbytes = SIMDAssembler::VectorBytes(type);
    args.params->SetMiniumAlignment(vecbytes);
    args.result->SetMiniumAlignment(vecbytes);

    // Embedding matrix must be row-major.
    args.params->RequireOrder(ROW_MAJOR);

    // Reserve registers.
    int regs = SIMDAssembler::RegisterUsage(type) + 8;
    if (pooling_ == AVG) regs++;
    if (args.batch.elements() > 1) regs++;
    step->SetRegisterUsage(regs);
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get inputs and outputs.
    GatherArgs args(step, true);

    // Create SIMD code generators.
    Type type = args.params->type();
    int dsize = TypeTraits::of(type).size();
    int vecbytes = SIMDAssembler::VectorBytes(type);
    bool aligned = args.slice_size() % vecbytes == 0;
    SIMDAssembler sasm(masm, type, aligned);
    step->set_variant(sasm.name());

    // Compute vector processing strategy.
    SIMDStrategy strategy(&sasm, args.slice_elements());
    strategy.PreloadMasks();

    // Allocate registers.
    bool batched = args.batch.elements() > 1;
    Register acc = masm->rr().alloc_fixed(rax);
    Register src = masm->rr().alloc_fixed(rsi);
    Register dst = masm->rr().alloc_fixed(rdi);
    Register cnt = masm->rr().alloc_fixed(rcx);
    Register ofs = cnt;
    Register fidx = masm->rr().alloc();
    Register params = masm->rr().alloc();
    Register indices = masm->rr().alloc();
    Register result = masm->rr().alloc();
    Register fcnt = pooling_ == AVG ? masm->rr().alloc() : no_reg;
    Register batch = batched ? masm->rr().alloc() : no_reg;
    auto elem = sasm.alloc(strategy.MaxUnrolls());

    // Load tensor locations.
    __ LoadTensorAddress(params, args.params);
    __ LoadTensorAddress(indices, args.indices);
    __ LoadTensorAddress(result, args.result);

    // Loop over batches.
    Label lb;
    if (batched) {
      __ xorq(batch, batch);
      __ bind(&lb);
    }

    // Zero feature index and feature count.
    __ xorq(fidx, fidx);
    if (pooling_ == AVG) {
      __ xorq(fcnt, fcnt);
    }

    // Find first (non-negative) feature. Only first index is tested.
    Label l1, l2, next;
    __ bind(&l1);
    __ movsxlq(acc, Operand(indices));
    __ testq(acc, acc);
    __ j(positive, &l2);

    __ addq(indices, Immediate(args.n * sizeof(int32)));
    __ incq(fidx);
    __ cmpq(fidx, Immediate(args.feature.elements()));
    __ j(less, &l1);

    // No feature found; zero output vector.
    __ xorq(acc, acc);
    __ movq(dst, result);
    __ movq(cnt, Immediate(args.slice_size()));
    __ repstosb();
    __ jmp(&next);

    // First non-negative feature found; copy its embedding vector to output.
    __ bind(&l2);
    __ movq(src, params);
    for (int d = 0; d < args.n; ++d) {
      __ movsxlq(acc, Operand(indices, d * sizeof(int32)));
      __ Multiply(acc, args.params->stride(d));
      __ addq(src, acc);
    }
    __ addq(indices, Immediate(args.n * sizeof(int32)));
    __ movq(dst, result);
    __ movq(cnt, Immediate(args.slice_size()));
    __ repmovsb();
    if (pooling_ == AVG) {
      __ incq(fcnt);
    }

    // Go over the remaining features.
    Label l3, l4, l5;
    __ bind(&l3);
    __ incq(fidx);
    __ cmpq(fidx, Immediate(args.feature.elements()));
    __ j(equal, &l5);

    // Look up element in params.
    __ movq(src, params);
    for (int d = 0; d < args.n; ++d) {
      __ movsxlq(acc, Operand(indices, d * sizeof(int32)));
      if (d == 0) {
        __ testq(acc, acc);
        __ j(negative, &l4);
      }
      __ Multiply(acc, args.params->stride(d));
      __ addq(src, acc);
    }
    __ addq(indices, Immediate(args.n * sizeof(int32)));
    if (pooling_ == AVG) {
      __ incq(fcnt);
    }

    // Combine embedding vector for feature with current result.
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
          gen->Accumulate(op, elem[i], Operand(result, ofs, times_1, disp));
          gen->Store(Operand(result, ofs, times_1, disp), elem[i]);
        }
        __ addq(ofs, Immediate(blksize));
        __ cmpq(ofs, Immediate(blkstart + phase.repeat * blksize));
        __ j(less, &lu);
      } else if (phase.masked == 0) {
        // Residual phase.
        for (int i = 0; i < phase.unrolls; ++i) {
          int disp = blkstart + i * vecsize * dsize;
          gen->Load(elem[i], Operand(src, disp));
          gen->Accumulate(op, elem[i], Operand(result, disp));
          gen->Store(Operand(result, disp), elem[i]);
        }
      } else {
        // Masked phase.
        CHECK_EQ(phase.unrolls, 1);
        gen->MaskedLoad(elem[0], Operand(src, blkstart));
        gen->MaskedAccumulate(op, elem[0], Operand(result, blkstart));
        gen->MaskedStore(Operand(result, blkstart), elem[0]);
      }
    }

    // Next feature.
    __ jmp(&l3);

    // Skip remaining features.
    __ bind(&l4);
    if (batched) {
      __ movq(cnt, Immediate(args.feature.elements()));
      __ subq(cnt, fidx);
      __ Multiply(cnt, args.n * sizeof(int32));
      __ addq(indices, cnt);
    }

    // Compute average.
    __ bind(&l5);
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
            gen->Mul(elem[i], scalar, Operand(result, ofs, times_1, disp));
            gen->Store(Operand(result, ofs, times_1, disp), elem[i]);
          }
          __ addq(ofs, Immediate(blksize));
          __ cmpq(ofs, Immediate(blkstart + phase.repeat * blksize));
          __ j(less, &lu);
        } else if (phase.masked == 0) {
          // Residual phase.
          for (int i = 0; i < phase.unrolls; ++i) {
            int disp = blkstart + i * vecsize * dsize;
            gen->Mul(elem[i], scalar, Operand(result, disp));
            gen->Store(Operand(result, disp), elem[i]);
          }
        } else {
          // Masked phase.
          CHECK_EQ(phase.unrolls, 1);
          gen->MaskedMul(elem[0], scalar, Operand(result, blkstart));
          gen->MaskedStore(Operand(result, blkstart), elem[0]);
        }
      }
    }

    // Next batch.
    __ bind(&next);
    if (batched) {
      __ addq(result, Immediate(args.slice_size()));
      __ incq(batch);
      __ cmpq(batch, Immediate(args.batch.elements()));
      __ j(less, &lb);
    }
  }

  int64 Complexity(const Step *step) override {
    GatherArgs args(step, true);
    int64 ops = args.outer_elements() * args.slice_elements();
    if (pooling_ == AVG) ops += args.slice_elements();
    return  ops;
  }

 private:
  Pooling pooling_;  // pooling operation for combining vectors
};

// Scatter input to sparse output.
class Scatter : public Kernel {
 public:
  Scatter(bool accumulate) : accumulate_(accumulate) {}

  string Name() override { return Operation(); }

  string Operation() override {
    return accumulate_ ? "AssignAddScatter" : "Scatter";
  }

  bool Supports(Step *step) override {
    // Check inputs and outputs.
    Args args(step, accumulate_);
    if (!args.valid) return false;

    // Check types.
    Type type = args.var->type();
    if (args.value->type() != type) return false;
    if (args.indices->type() != DT_INT32) return false;

    // Check shapes.
    if (args.index.rank() != args.n) return false;
    if (args.value->shape() != args.value_shape()) return false;

    // Check optional arguments.
    if (args.ref != nullptr) {
      if (args.ref->type() != type) return false;
      if (args.ref->shape() != args.var->shape()) return false;
    }
    if (args.oov != nullptr) {
      if (args.oov->shape() != args.element) return false;
    }

    // Check for SIMD support.
    if (!SIMDAssembler::Supports(args.var->type())) return false;
    if (args.var->constant()) return false;

    return true;
  }

  void Adjust(Step *step, const Options &options) override {
    Args args(step, accumulate_);

    // Add sparsity bitmap index.
    if (options.sparse_threshold > 0 &&
        args.index.elements() >= options.sparse_threshold &&
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
    int regs = SIMDAssembler::RegisterUsage(type) + 7;
    if (args.batch.elements() > 1) regs++;
    if (args.var->sparse()) regs++;
    step->SetRegisterUsage(regs);
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get inputs.
    Args args(step, accumulate_);
    Tensor *sparse = args.var->sparse();
    bool batched = args.batch.elements() > 1;
    bool single = args.feature.elements() == 1;
    int n = args.element.elements();

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
    Register acc = masm->rr().alloc_fixed(rax);
    Register dst = masm->rr().alloc_fixed(rdi);
    Register cnt = masm->rr().alloc_fixed(rcx);
    Register batch = batched ? masm->rr().alloc() : no_reg;
    Register fidx = masm->rr().alloc();
    Register varaddr = masm->rr().alloc();
    Register idxaddr = masm->rr().alloc();
    Register valaddr = masm->rr().alloc();
    Register bmaddr = sparse ? masm->rr().alloc() : no_reg;
    auto elem = sasm.alloc(strategy.MaxUnrolls());

    // Load tensor locations.
    __ LoadTensorAddress(varaddr, args.var);
    __ LoadTensorAddress(idxaddr, args.indices);
    __ LoadTensorAddress(valaddr, args.value);
    if (sparse) {
      __ LoadTensorAddress(bmaddr, sparse);
    }

    // Clear output for non-accumulating scatter.
    if (!accumulate_) {
      __ xorq(acc, acc);
      __ movq(dst, varaddr);
      __ movq(cnt, Immediate(args.var->size()));
      __ repstosb();
    }

    // Optionally output reference to assigned variable.
    if (args.ref != nullptr) {
      CHECK(args.ref->IsLocal());
      CHECK(args.ref->ref());
      __ movq(Operand(masm->instance(), args.ref->offset()), varaddr);
    }

    // Loop over batches.
    Label lbatch;
    if (batched) {
      __ xorq(batch, batch);
      __ bind(&lbatch);
    }

    // Loop over features.
    Label lfeature;
    if (!single) {
      __ xorq(fidx, fidx);
      __ bind(&lfeature);
    }

    // Compute index into scatter variable.
    Label loov;
    __ movsxlq(dst, Operand(idxaddr));
    __ testq(dst, dst);
    __ j(negative, &loov);
    for (int d = 1; d < args.n; ++d) {
      __ Multiply(dst, args.var->dim(d - 1));
      __ movsxlq(acc, Operand(idxaddr, d * sizeof(int32)));
      __ addq(dst, acc);
    }

    // Update sparsity bitmap.
    if (sparse) {
      __ movq(cnt, dst);
      __ movq(acc, Immediate(1));
      __ shlq_cl(acc);
      __ shrq(cnt, Immediate(6));
      __ orq(Operand(bmaddr, cnt, times_8), acc);
    }

    // Compute address of slice in scatter variable.
    __ Multiply(dst, args.var->stride(args.n - 1));
    __ addq(dst, varaddr);

    // Handle missing features.
    Label lskip, lnext;
    if (args.oov) {
      // Update OOV vector for missing features.
      __ jmp(&lskip);
      __ bind(&loov);
      __ LoadTensorAddress(dst, args.oov);
    } else if (batched) {
      // Skip unused features.
      __ jmp(&lskip);
      __ bind(&loov);
      __ movq(cnt, Immediate(args.feature.elements()));
      __ subq(cnt, fidx);
      __ Multiply(cnt, args.n * sizeof(int32));
      __ addq(idxaddr, cnt);
      __ jmp(&lnext);
    }
    __  bind(&lskip);

    // Add input vector for feature to embedding vector.
    for (auto &phase : strategy.phases()) {
      auto *gen = phase.generator;
      int vecsize = gen->VectorSize();
      int blkstart = phase.offset * dsize;
      int blksize = phase.unrolls * vecsize * dsize;

      if (phase.repeat > 1) {
        // Repeated phase.
        Label lu;
        if (blkstart == 0) {
          __ xorq(cnt, cnt);
        } else {
          __ movq(cnt, Immediate(blkstart));
        }
        __ bind(&lu);
        for (int i = 0; i < phase.unrolls; ++i) {
          int disp = i * vecsize * dsize;
          gen->Load(elem[i], Operand(dst, cnt, times_1, disp));
          gen->Add(elem[i], elem[i], Operand(valaddr, cnt, times_1, disp));
          gen->Store(Operand(dst, cnt, times_1, disp), elem[i]);
        }
        __ addq(cnt, Immediate(blksize));
        __ cmpq(cnt, Immediate(blkstart + phase.repeat * blksize));
        __ j(less, &lu);
      } else if (phase.masked == 0) {
        // Residual phase.
        for (int i = 0; i < phase.unrolls; ++i) {
          int disp = blkstart + i * vecsize * dsize;
          gen->Load(elem[i], Operand(dst, disp));
          gen->Add(elem[i], elem[i], Operand(valaddr, disp));
          gen->Store(Operand(dst, disp), elem[i]);
        }
      } else {
        // Masked phase.
        CHECK_EQ(phase.unrolls, 1);
        gen->MaskedLoad(elem[0], Operand(dst, blkstart));
        gen->MaskedAdd(elem[0], elem[0], Operand(valaddr, blkstart));
        gen->MaskedStore(Operand(dst, blkstart), elem[0]);
      }
    }

    // Next feature.
    if (!single) {
      if (!args.pooled) {
        __ addq(valaddr, Immediate(args.value_element_size()));
      }
      __ addq(idxaddr, Immediate(args.n * sizeof(int32)));
      __ incq(fidx);
      __ cmpq(fidx, Immediate(args.feature.elements()));
      __ j(less, &lfeature);
    }

    // Next batch.
    if (batched) {
      if (args.pooled) {
        __ addq(valaddr, Immediate(args.value_element_size()));
      }
      __ bind(&lnext);
      __ incq(batch);
      __ cmpq(batch, Immediate(args.batch.elements()));
      __ j(less, &lbatch);
    } else if (args.oov == nullptr) {
      __ bind(&loov);
    }
  }

  int64 Complexity(const Step *step) override {
    Args args(step, accumulate_);
    return args.feature.elements() * args.element.elements();
  }

 private:
  // Arguments to scatter ops.
  // var = Scatter(indices, value, [oov])
  // [ref] = AssignAddScatter(var, indices, value, [oov])
  struct Args {
    Args(const Step *step, bool accumulate) {
      // Get fixed arguments.
      if (accumulate) {
        if (step->indegree() < 3) return;
        if (step->outdegree() > 1) return;
        var = step->input(0);
        indices = step->input(1);
        value = step->input(2);
        if (step->indegree() > 3) oov = step->input(3);
        if (step->outdegree() > 0) ref = step->output(0);
      } else {
        if (step->indegree() < 2) return;
        if (step->outdegree() != 1) return;
        indices = step->input(0);
        value = step->input(1);
        if (step->indegree() > 2) oov = step->input(2);
        var = step->output(0);
      }

      // Compute index shapes.
      pooled = step->GetAttr("pooled", false);
      int b = step->GetAttr("batch", -1);
      int r = indices->rank();
      if (r > 0) n = indices->dim(-1);
      feature = indices->shape().outside(r - 1);
      if (b >= 0) {
        batch = feature.outside(b);
        feature = feature.inside(b);
      }
      index = var->shape().outside(n);
      element = var->shape().inside(n);

      valid = true;
    }

    // Return expected shape for value.
    Shape value_shape() const {
      if (pooled) {
        return batch + element;
      } else {
        return batch + feature + element;
      }
    }

    // Return value element size.
    int value_element_size() const {
      if (pooled) {
        return value->stride(batch.rank() - 1);
      } else {
        return value->stride(batch.rank() + feature.rank() - 1);
      }
    }

    bool valid = false;         // arguments are valid
    Tensor *var = nullptr;      // T[N,E] tensor to scatter values into
    Tensor *indices = nullptr;  // int32[B,F,{N}] tensor with indices to scatter
    Tensor *value = nullptr;    // T[B,(F),E] tensor with values to scatter
    Tensor *ref = nullptr;      // &T[N,E] tensor with reference to var
    Tensor *oov = nullptr;      // optional T[E] tensor for invalid indices

    int n = 1;                  // number of parameter index dimensions
    bool pooled = false;        // value is pooled, i.e. no feature shape
    Shape batch;                // batch shape in indices (B)
    Shape feature;              // feature shape in indices (F)
    Shape index;                // embedding index shape (N)
    Shape element;              // embedding element shape (E)
  };

  bool accumulate_;  // accumulate output
};

// Register gather/scatter kernels.
void RegisterGatherKernels(Library *library) {
  library->Register(new MultiGather());
  library->Register(new SingleGather());
  library->Register(new PoolingGather(PoolingGather::SUM));
  library->Register(new PoolingGather(PoolingGather::AVG));
  library->Register(new PoolingGather(PoolingGather::MAX));
  library->Register(new Scatter(false));
  library->Register(new Scatter(true));
}

}  // namespace myelin
}  // namespace sling
