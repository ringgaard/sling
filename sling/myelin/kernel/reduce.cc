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

// Reduction over an axis.
class Reduce : public Kernel {
 public:
  Reduce(const string &name, Reduction op) : name_(name), op_(op) {}

  string Name() override { return name_; }
  string Operation() override { return name_; }

  bool Supports(Step *step) override {
    // Check inputs and outputs.
    if (step->indegree() != 1 || step->outdegree() != 1) return false;
    Tensor *x = step->input(0);
    Tensor *y = step->output(0);

    // Check type.
    if (x->type() != y->type()) return false;
    if (!SIMDAssembler::Supports(x->type())) return false;

    // Check shape.
    int axis = step->GetAttr("axis", -1);
    bool keepdims = step->GetAttr("keepdims", false);
    if (axis < 0 || axis >= x->rank()) return false;
    if (x->shape().reduced(axis, keepdims) != y->shape()) return false;

    return true;
  }

  void Adjust(Step *step) override {
    // Get input and output.
    Tensor *x = step->input(0);
    Tensor *y = step->output(0);

    // Require dense standard layout.
    x->RequireStandardOrder();
    y->RequireStandardOrder();
    x->RequireDense();
    y->RequireDense();

    // Set alignment.
    Type type = x->type();
    int vecbytes = SIMDAssembler::VectorBytes(type);
    x->SetMiniumAlignment(vecbytes);
    y->SetMiniumAlignment(vecbytes);
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get input and output.
    Tensor *x = step->input(0);
    Tensor *y = step->output(0);
    int axis = step->GetAttr("axis", -1);

    // Compute dimensions.
    Type type = x->type();
    int dsize = TypeTraits::of(type).size();
    int vecbytes = SIMDAssembler::VectorBytes(type);

    int outer_size = x->shape().outer(axis);
    int reduction_size = x->dim(axis);
    int inner_size = x->shape().inner(axis + 1);

    // Allocate registers.
    Register in = masm->rr().alloc();
    Register out = masm->rr().alloc();
    Register ofs = masm->rr().alloc();

    // Load tensor addresses.
    __ LoadTensorAddress(in, x);
    __ LoadTensorAddress(out, y);

    // Reduction over the last axis is done using horizontal reduction whereas
    // reduction over other axes is done using vertical reduction.
    if (inner_size == 1) {
      // Create SIMD code generators.
      bool aligned = x->stride(axis - 1) % vecbytes == 0;
      SIMDAssembler sasm(masm, type, aligned);

      // Compute vector processing strategy.
      step->set_variant(sasm.name() + "H");
      SIMDStrategy strategy(&sasm, reduction_size);
      strategy.PreloadMasks();

      // Loop over batches.
      Register batch = masm->rr().alloc();
      Label lb;
      if (outer_size > 1) {
        __ xorq(batch, batch);
        __ bind(&lb);
      }

      // Initialize reduction with neutral element.
      auto acc = sasm.alloc(strategy.MaxUnrolls());
      for (auto r : acc) sasm.main()->LoadNeutral(op_, r);

      // Reduce inner vector.
      bool scalar = true;
      for (auto &phase : strategy.phases()) {
        auto *gen = phase.generator;
        int vecsize = gen->VectorSize();
        int blkstart = phase.offset * dsize;
        int blksize = phase.unrolls * vecsize * dsize;
        if (vecsize > 1) scalar = false;

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
            gen->Accumulate(op_, acc[i], Operand(in, ofs, times_1, disp));
          }
          __ addq(ofs, Immediate(blksize));
          __ cmpq(ofs, Immediate(blkstart + phase.repeat * blksize));
          __ j(less, &lu);
        } else if (phase.masked == 0) {
          // Residual phase.
          if (phase.offset == 0 || vecsize == sasm.main()->VectorSize()) {
            // Same vector size as bulk; unroll directly into accumulators.
            for (int i = 0; i < phase.unrolls; ++i) {
              int disp = blkstart + i * vecsize * dsize;
              gen->Accumulate(op_, acc[i], Operand(in, disp));
            }
          } else {
            // Accumulate unrolled residual and merge into first accumulator.
            auto residual = sasm.alloc();
            sasm.main()->LoadNeutral(op_, residual);
            for (int i = 0; i < phase.unrolls; ++i) {
              int disp = blkstart + i * vecsize * dsize;
              gen->Accumulate(op_, residual, Operand(in, disp));
            }
            sasm.main()->Accumulate(op_, acc[0], residual);
          }
        } else {
          // Masked phase.
          CHECK_EQ(phase.unrolls, 1);
          gen->MaskedAccumulate(op_, acc[0], Operand(in, blkstart));
        }
      }

      // Horizontal reduction of results.
      sasm.Reduce(op_, acc);
      if (!scalar) sasm.main()->Reduce(op_, acc[0]);

      // Save result in y.
      sasm.scalar()->Store(Operand(out), acc[0]);

      // Next batch.
      if (outer_size > 1) {
        __ addq(in, Immediate(reduction_size * dsize));
        __ addq(out, Immediate(dsize));
        __ incq(batch);
        __ cmpq(batch, Immediate(outer_size));
        __ j(less, &lb);
      }
    } else {
      // Create SIMD code generators.
      bool aligned = x->stride(axis) % vecbytes == 0;
      SIMDAssembler sasm(masm, type, aligned);

      // Compute vector processing strategy.
      step->set_variant(sasm.name() + "V");
      SIMDStrategy strategy(&sasm, inner_size);
      strategy.PreloadMasks();
      auto acc = sasm.alloc(strategy.MaxUnrolls());

      // Loop over batches.
      Register batch = masm->rr().alloc();
      Label lb;
      if (outer_size > 1) {
        __ xorq(batch, batch);
        __ bind(&lb);
      }

      // Vertically reduction.
      for (auto &phase : strategy.phases()) {
        auto *gen = phase.generator;
        int vecsize = gen->VectorSize();
        int blkstart = phase.offset * dsize;
        int blksize = phase.unrolls * vecsize * dsize;
        int stride = axis > 0 ? x->stride(axis - 1) : x->size();
        bool last = outer_size == 1 && phase.last;

        if (phase.masked == 0) {
          // Repeated/residial phase.
          Label l2;
          if (phase.offset == 0) {
            __ xorq(ofs, ofs);
          } else {
            __ movq(ofs, Immediate(blkstart));
          }
          __ bind(&l2);

          // Initialize accumulators with neutral element.
          for (int r = 0; r < phase.unrolls; ++r) {
            gen->LoadNeutral(op_, acc[r]);
          }

          // Loop over reduction axis and reduce block vertically.
          Label l3;
          __ bind(&l3);
          for (int i = 0; i < phase.unrolls; ++i) {
            int disp = i * vecsize * dsize;
            gen->Accumulate(op_, acc[i], Operand(in, ofs, times_1, disp));
          }
          __ addq(ofs, Immediate(inner_size * dsize));
          __ cmpq(ofs, Immediate(stride));
          __ j(less, &l3);

          // Store result for block.
          for (int i = 0; i < phase.unrolls; ++i) {
            gen->Store(Operand(out, i * vecsize * dsize), acc[i]);
          }
          if (!last || phase.repeat > 1) {
            __ addq(out, Immediate(blksize));
          }

          if (phase.repeat > 1) {
            // Next block.
            __ subq(ofs, Immediate(stride - blksize));
            __ cmpq(ofs, Immediate(blkstart + phase.repeat * blksize));
            __ j(less, &l2);
          }
        } else {
          // Masked phase.
          CHECK_EQ(phase.unrolls, 1);
          CHECK_EQ(phase.repeat, 1);
          if (phase.offset == 0) {
            __ xorq(ofs, ofs);
          } else {
            __ movq(ofs, Immediate(blkstart));
          }

          // Initialize accumulator with neutral element.
          gen->LoadNeutral(op_, acc[0]);

          // Loop over reduction axis and reduce block vertically.
          Label l3;
          __ bind(&l3);
            gen->MaskedAccumulate(op_, acc[0], Operand(in, ofs, times_1));
          __ addq(ofs, Immediate(inner_size * dsize));
          __ cmpq(ofs, Immediate(stride));
          __ j(less, &l3);

          // Store result for block.
          gen->MaskedStore(Operand(out), acc[0]);
          if (!last) {
            __ addq(out, Immediate(phase.masked * dsize));
          }
        }
      }

      // Next batch.
      if (outer_size > 1) {
        __ addq(in, Immediate(reduction_size * inner_size * dsize));
        __ incq(batch);
        __ cmpq(batch, Immediate(outer_size));
        __ j(less, &lb);
      }
    }
  }

  int64 Complexity(const Step *step) override {
    return step->input(0)->elements();
  }

 private:
  string name_;
  Reduction op_;
};

// Register reduce kernels.
void RegisterReduceKernels(Library *library) {
  library->Register(new Reduce("Sum", REDUCE_ADD));
  library->Register(new Reduce("Product", REDUCE_MUL));
  library->Register(new Reduce("Max", REDUCE_MAX));
  library->Register(new Reduce("Min", REDUCE_MIN));
  library->Register(new Reduce("All", REDUCE_AND));
  library->Register(new Reduce("Any", REDUCE_OR));
}

}  // namespace myelin
}  // namespace sling
