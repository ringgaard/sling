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

#include <string>
#include <utility>

#include "sling/base/logging.h"
#include "sling/myelin/compute.h"
#include "sling/myelin/macro-assembler.h"
#include "sling/myelin/simd-assembler.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Arguments for matmul op. This takes transposition and element order of the
// arguments into account.
class MatMulArgs {
 public:
  struct Arg {
    // Intialize argument from tensor.
    void Init(Tensor *tensor, bool transposed) {
      this->tensor = tensor;
      this->transposed = transposed;
      int r = rank();
      outer = r - 2;
      inner = r - 1;
      batch = r - 2;
      if (tensor->order() == COLUMN_MAJOR) {
        std::swap(outer, inner);
      }
    }

    // Transpose argument representation.
    void Transpose() {
      transposed = !transposed;
    }

    // Element order with respect to transpose.
    Order order() const {
      switch (tensor->order()) {
        case ROW_MAJOR: return transposed ? COLUMN_MAJOR : ROW_MAJOR;
        case COLUMN_MAJOR: return transposed ? ROW_MAJOR : COLUMN_MAJOR;
        default: break;
      }
      return tensor->order();
    }

    // Height (outer dimension) of matrix w.r.t. physical layout.
    int height() const { return tensor->dim(outer); }

    // Width (inner dimension) of matrix  w.r.t. physical layout.
    int width() const { return tensor->dim(inner); }

    // Number of rows in (transposed) matrix  w.r.t. logical layout.
    int rows() const {
      return tensor->dim(tensor->rank() - (transposed ? 1 : 2));
    }

    // Number of columns in (transposed) matrix  w.r.t. logical layout.
    int columns() const {
      return tensor->dim(tensor->rank() - (transposed ? 2 : 1));
    }

    // Number of elements in matrix.
    int elements() const { return tensor->shape().inner(batch); }

    // Size of matrix in bytes.
    int size() const {
      return batch > 0 ? tensor->stride(batch - 1) : tensor->size();
    }

    // Size of outer dimension including padding.
    int stride() const { return tensor->stride(outer); }

    // Padding bytes for outer dimension.
    int padding() const { return tensor->padding(outer); }

    // Batch size.
    int batch_size() const { return tensor->shape().outer(batch); }

    // Batch stride.
    int batch_stride() const {
      return batch == 0 ? tensor->size() : tensor->stride(batch - 1);
    }

    // Check if matrix has dense layout.
    bool dense() const { return tensor->HasDenseLayout(); }

    // Check if matrix is a row or column vector.
    bool vector() const { return rows() == 1 || columns() == 1; }

    // Check if all elements are aligned.
    bool aligned(int align) const {
      return stride() % align == 0 || elements() == 1;
    }

    // Data type for underlying tensor.
    Type type() const { return tensor->type(); }

    // Rank for underlying tensor.
    int rank() const { return tensor->rank(); }

    Tensor *tensor;   // underlying tensor for argument
    bool transposed;  // argument transposition
    int outer;        // outer dimension for matrix
    int inner;        // inner dimension for matrix
    int batch;        // number of batch dimensions
  };

  // Check if inputs and outputs are valid for a matrix multiplication.
  static bool Valid(const Step *step) {
    if (step->type() == "AssignAddMatMul") {
      return step->indegree() >= 3;
    } else {
      return step->indegree() >= 2 && step->outdegree() >= 1;
    }
  }

  // Initialize arguments for matmul op.
  MatMulArgs(const Step *step) {
    CHECK(Valid(step));

    // An accumulating matmul takes the result as the first input.
    accumulate_ = step->type() == "AssignAddMatMul";

    // Get argument tensors.
    Tensor *c = accumulate_ ? step->input(0) : step->output(0);
    Tensor *a = accumulate_ ? step->input(1) : step->input(0);
    Tensor *b = accumulate_ ? step->input(2) : step->input(1);

    // Initialize arguments.
    c_.Init(c, step->GetAttr("transpose_c", false));
    a_.Init(a, step->GetAttr("transpose_a", false));
    b_.Init(b, step->GetAttr("transpose_b", false));
  }

  // Ensure output order. Returns false if the output tensor does not support
  // this order.
  bool EnsureOutputOrder(Order order) {
    // Determine if matmul needs to be transformed to meet output element order
    // requirement.
    bool transform = false;
    if (order == ROW_MAJOR) {
      if (c_.tensor->order() == COLUMN_MAJOR) transform = true;
    } else if (order == COLUMN_MAJOR) {
      if (c_.tensor->order() == ROW_MAJOR) transform = true;
    }

    // Apply C=A*B => C^T=B^T*A^T to change output order.
    if (transform) {
      std::swap(a_, b_);
      c_.Transpose();
      a_.Transpose();
      b_.Transpose();
    }

    // Check that output supports order.
    return c_.tensor->SupportsOrder(c_.tensor->order());
  }

  // Set the required order for output.
  void RequireOrder(Order order) {
    EnsureOutputOrder(order);
    Order required;
    switch (order) {
      case ROW_MAJOR:
        required = c_.transposed ? COLUMN_MAJOR : ROW_MAJOR;
        break;
      case COLUMN_MAJOR:
        required = c_.transposed ? ROW_MAJOR : COLUMN_MAJOR;
        break;
      default:
        required = ANY_ORDER;
    }
    c_.tensor->RequireOrder(required);
  }

  // Check that argument shapes match a (batched) matrix multiplication.
  bool CheckShapes() const {
    // Check that tensors are (same-sized batches of) matrices.
    if (a_.rank() < 2) return false;
    if (b_.rank() != a_.rank()) return false;
    if (c_.rank() != a_.rank()) return false;

    if (c_.rows() != a_.rows()) return false;
    if (c_.columns() != b_.columns()) return false;
    if (a_.columns() != b_.rows()) return false;

    if (a_.batch_size() != c_.batch_size()) return false;
    if (b_.batch_size() != c_.batch_size()) return false;

    return true;
  }

  // Check if all elements are aligned.
  bool Aligned(int align) const {
    return a_.aligned(align) && b_.aligned(align) && c_.aligned(align);
  }

  // Whether this is an accumulating matmul.
  bool accumulate() const { return accumulate_; }

  // Matrix multiplication arguments, c = a * b.
  const Arg &a() const { return a_; }
  const Arg &b() const { return b_; }
  const Arg &c() const { return c_; }

 private:
  // Arguments for matrix multiplication, c = a * b.
  Arg c_;
  Arg a_;
  Arg b_;

  // An accumulating matmul adds the matrix multiplication to the result.
  bool accumulate_;
};

// General matrix multiplication using SIMD code generators. It supports
// transposed inputs and output as well as output accumulation.
class SIMDMatMul : public Kernel {
 public:
  SIMDMatMul(bool accumulate) : accumulate_(accumulate) {}

  string Name() override {
    return accumulate_ ? "SIMDAccMatMul" : "SIMDMatMul";
  }
  string Operation() override {
    return accumulate_ ? "AssignAddMatMul" : "MatMul";
  }

  bool Supports(Step *step) override {
    // Check inputs and outputs.
    if (!MatMulArgs::Valid(step)) return false;
    MatMulArgs args(step);
    if (!args.CheckShapes()) return false;
    if (args.accumulate() != accumulate_) return false;

    // Output must be row-major.
    if (!args.EnsureOutputOrder(ROW_MAJOR)) return false;

    // Check that element type is supported.
    Type type = args.c().type();
    if (!SIMDAssembler::Supports(type)) return false;
    if (args.a().type() != type) return false;
    if (args.b().type() != type) return false;

    return true;
  }

  void Adjust(Step *step) override {
    // Set required order for output.
    MatMulArgs args(step);
    args.RequireOrder(ROW_MAJOR);

    // Inputs must be row-major for batched matmul.
    if (args.a().batch_size() != 1) {
      args.a().tensor->RequireOrder(ROW_MAJOR);
      args.b().tensor->RequireOrder(ROW_MAJOR);
    }

    // Set alignment.
    Type type = args.c().type();
    int vecbytes = SIMDAssembler::VectorBytes(type);
    args.a().tensor->SetMiniumAlignment(vecbytes);
    args.b().tensor->SetMiniumAlignment(vecbytes);
    args.c().tensor->SetMiniumAlignment(vecbytes);

    // Reserve registers.
    int regs = SIMDAssembler::RegisterUsage(type) + 8;
    if (args.a().batch_size() > 1) regs++;
    step->SetRegisterUsage(regs);
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    MatMulArgs args(step);
    CHECK(args.EnsureOutputOrder(ROW_MAJOR));
    auto &a = args.a();
    auto &b = args.b();

    // Check for vector product.
    if (a.vector() && a.dense() &&
        b.vector() && b.dense() &&
        a.elements() == b.elements()) {
      // Inner or outer product.
      if (a.rows() == 1) {
        GenerateInner(step, masm, args);
      } else if (b.rows() == 1) {
        GenerateOuter(step, masm, args);
      } else {
        LOG(FATAL) << "Unsupported vector product";
      }
    } else {
      // Use the input element order to choose matrix multiplication algorithm.
      Order oa = a.order();
      Order ob = b.order();
      if (oa == ROW_MAJOR && ob == ROW_MAJOR) {
        GenerateVertical(step, masm, args, false);
      } else if (oa == ROW_MAJOR && ob == COLUMN_MAJOR) {
        GenerateHorizontal(step, masm, args);
      } else if (oa == COLUMN_MAJOR && ob == ROW_MAJOR) {
        GenerateVertical(step, masm, args, true);
      } else if (oa == COLUMN_MAJOR && ob == COLUMN_MAJOR) {
        GenerateColCol(step, masm, args);
      } else {
        LOG(FATAL) << "Unsupported element order";
      }
    }

    // Add batch size to variant.
    int batch_size = a.batch_size();
    if (batch_size > 1) {
      step->set_variant(step->variant() + "*" + std::to_string(batch_size));
    }
  }

  // Compute dot products between rows/columns in A and column blocks in B using
  // vertical summing. The vectors in A can either be traverse from top to
  // bottom (strided) or from left ro right (consecutive).
  void GenerateVertical(Step *step, MacroAssembler *masm,
                        const MatMulArgs &args, bool strided) {
    // Create SIMD code generators.
    Type type = args.c().type();
    int dsize = TypeTraits::of(type).size();
    int vecbytes = SIMDAssembler::VectorBytes(type);
    int batchsize = args.a().batch_size();
    SIMDAssembler sasm(masm, type, args.Aligned(vecbytes));
    step->set_variant(sasm.name() + (strided ? "CR" : "RR"));
    if (strided) {
      CHECK_EQ(args.a().height(), args.b().height());
    } else {
      CHECK_EQ(args.a().width(), args.b().height());
    }

    // Compute vector processing strategy.
    SIMDStrategy strategy(&sasm, args.b().width());
    strategy.PreloadMasks();

    // Allocate registers.
    Register a = masm->rr().alloc();
    Register b = masm->rr().alloc();
    Register c = masm->rr().alloc();
    Register a_ofs = masm->rr().alloc();
    Register b_ptr = masm->rr().alloc();
    Register col_ofs = masm->rr().alloc();
    auto sum = sasm.alloc(strategy.MaxUnrolls());
    int elem = sasm.alloc();

    // Load tensor addresses.
    __ LoadTensorAddress(a, args.a().tensor);
    __ LoadTensorAddress(b, args.b().tensor);
    __ LoadTensorAddress(c, args.c().tensor);

    // Compute inner and outer dimensions.
    int outer_step, outer_limit, inner_step, inner_limit, batch_skip;
    if (strided) {
      outer_step = dsize;
      outer_limit = dsize * args.a().width();
      inner_step = args.a().stride();
      inner_limit = args.a().stride() * args.a().height();
      batch_skip = args.a().size() - outer_limit;
    } else {
      outer_step = args.a().stride();
      outer_limit = args.a().stride() * args.a().height();
      inner_step = dsize;
      inner_limit = dsize * args.a().width();
      batch_skip = 0;
    }
    bool outer_single = outer_step == outer_limit;
    bool inner_single = inner_step == inner_limit;

    // Loop over batches.
    Register batch = no_reg;
    Label lb;
    if (batchsize > 1) {
      batch = masm->rr().alloc();
      __ xorq(batch, batch);
      __ bind(&lb);
    }

    // Loop over rows/columns in A.
    Register a_end = masm->rr().alloc();
    Label l1;
    if (!outer_single) {
      __ leaq(a_end, Operand(a, outer_limit));
      __ bind(&l1);
    }

    // Compute dot product between row/column in A and column blocks in B.
    for (auto &phase : strategy.phases()) {
      auto *gen = phase.generator;
      int vecsize = gen->VectorSize();
      int blkstart = phase.offset * dsize;
      int blksize = phase.unrolls * vecsize * dsize;
      bool last = phase.last && batchsize == 1;

      if (phase.repeat > 1) {
        // Repeated phase.
        Label l2;
        if (phase.offset == 0) {
          __ xorq(col_ofs, col_ofs);
        } else {
          __ movq(col_ofs, Immediate(blkstart));
        }
        __ bind(&l2);

        if (inner_single) {
          // Outer product of A element and B row block.
          gen->Broadcast(elem, Operand(a));
          for (int i = 0; i < phase.unrolls; ++i) {
            int disp = i * vecsize * dsize;
            if (accumulate_) {
              gen->Load(sum[i], Operand(c, i * vecsize * dsize));
              bool retain = i != phase.unrolls - 1;
              gen->MulAdd(sum[i], elem, Operand(b, col_ofs, times_1, disp),
                          retain);
            } else {
              gen->Mul(sum[i], elem, Operand(b, col_ofs, times_1, disp));
            }
            gen->Store(Operand(c, i * vecsize * dsize), sum[i]);
          }
        } else {
          for (int r = 0; r < phase.unrolls; ++r) {
            gen->Zero(sum[r]);
          }
          __ xorq(a_ofs, a_ofs);
          __ leaq(b_ptr, Operand(b, col_ofs));

          // Loop over columns/rows in A and rows in B.
          Label l3;
          __ bind(&l3);
          gen->Broadcast(elem, Operand(a, a_ofs));
          for (int i = 0; i < phase.unrolls; ++i) {
            int disp = i * vecsize * dsize;
            bool retain = i != phase.unrolls - 1;
            gen->MulAdd(sum[i], elem, Operand(b_ptr, disp), retain);
          }
          __ addq(b_ptr, Immediate(args.b().stride()));
          __ addq(a_ofs, Immediate(inner_step));
          __ cmpq(a_ofs, Immediate(inner_limit));
          __ j(less, &l3);

          // Save result in C.
          for (int i = 0; i < phase.unrolls; ++i) {
            if (accumulate_) {
              gen->Add(sum[i], sum[i], Operand(c, i * vecsize * dsize));
            }
            gen->Store(Operand(c, i * vecsize * dsize), sum[i]);
          }
        }
        __ addq(c, Immediate(blksize));

        // Next block.
        __ addq(col_ofs, Immediate(blksize));
        __ cmpq(col_ofs, Immediate(blkstart + phase.repeat * blksize));
        __ j(less, &l2);
      } else if (phase.masked == 0) {
        // Residual phase.
        if (inner_single) {
          // Outer product of A element and B row block.
          gen->Broadcast(elem, Operand(a));
          for (int i = 0; i < phase.unrolls; ++i) {
            int disp = blkstart + i * vecsize * dsize;
            if (accumulate_) {
              gen->Load(sum[i], Operand(c, i * vecsize * dsize));
              bool retain = i != phase.unrolls - 1;
              gen->MulAdd(sum[i], elem, Operand(b, disp), retain);
            } else {
              gen->Mul(sum[i], elem, Operand(b, disp));
            }
            gen->Store(Operand(c, i * vecsize * dsize), sum[i]);
          }
        } else {
          for (int r = 0; r < phase.unrolls; ++r) {
            gen->Zero(sum[r]);
          }
          __ xorq(a_ofs, a_ofs);
          __ leaq(b_ptr, Operand(b, blkstart));

          // Loop over columns/rows in A and rows in B.
          Label l3;
          __ bind(&l3);
          gen->Broadcast(elem, Operand(a, a_ofs));
          for (int i = 0; i < phase.unrolls; ++i) {
            int disp = i * vecsize * dsize;
            bool retain = i != phase.unrolls - 1;
            gen->MulAdd(sum[i], elem, Operand(b_ptr, disp), retain);
          }
          __ addq(b_ptr, Immediate(args.b().stride()));
          __ addq(a_ofs, Immediate(inner_step));
          __ cmpq(a_ofs, Immediate(inner_limit));
          __ j(less, &l3);

          // Save result in C.
          for (int i = 0; i < phase.unrolls; ++i) {
            if (accumulate_) {
              gen->Add(sum[i], sum[i], Operand(c, i * vecsize * dsize));
            }
            gen->Store(Operand(c, i * vecsize * dsize), sum[i]);
          }
        }

        if (!last || !outer_single) {
          __ addq(c, Immediate(blksize));
        }
      } else {
        // Masked phase.
        CHECK_EQ(phase.unrolls, 1);
        if (inner_single) {
          gen->Broadcast(elem, Operand(a));
          if (accumulate_) {
            gen->MaskedLoad(sum[0], Operand(c));
            gen->MaskedMulAdd(sum[0], elem, Operand(b, blkstart));
          } else {
            gen->MaskedMul(sum[0], elem, Operand(b, blkstart));
          }
          gen->MaskedStore(Operand(c), sum[0]);
        } else {
          gen->Zero(sum[0]);
          __ xorq(a_ofs, a_ofs);
          __ leaq(b_ptr, Operand(b, blkstart));

          // Loop over columns/rows in A and rows in B.
          Label l3;
          __ bind(&l3);
          gen->Broadcast(elem, Operand(a, a_ofs));
          gen->MaskedMulAdd(sum[0], elem, Operand(b_ptr));
          __ addq(b_ptr, Immediate(args.b().stride()));
          __ addq(a_ofs, Immediate(inner_step));
          __ cmpq(a_ofs, Immediate(inner_limit));
          __ j(less, &l3);

          // Save result in C.
          if (accumulate_) {
            gen->MaskedAdd(sum[0], sum[0], Operand(c));
          }
          gen->MaskedStore(Operand(c), sum[0]);
        }
        if (!last || !outer_single) {
          __ addq(c, Immediate(phase.masked * dsize));
        }
      }
    }

    // Next row/column in A.
    if (!outer_single) {
      if (args.c().padding() > 0) {
        __ addq(c, Immediate(args.c().padding()));
      }
      __ addq(a, Immediate(outer_step));
      __ cmpq(a, a_end);
      __ j(less, &l1);
    }

    // Next batch.
    if (batchsize > 1) {
      if (outer_single) {
        __ addq(a, Immediate(outer_step));
      } else if (batch_skip != 0) {
        __ addq(a, Immediate(batch_skip));
      }
      __ addq(b, Immediate(args.b().batch_stride()));
      __ incq(batch);
      __ cmpq(batch, Immediate(batchsize));
      __ j(less, &lb);
    }
  }

  // Compute dot products between row blocks in A and row blocks in B using
  // horizontal summation.
  void GenerateHorizontal(Step *step, MacroAssembler *masm,
                          const MatMulArgs &args) {
    // Create SIMD code generators.
    Type type = args.c().tensor->type();
    int dsize = TypeTraits::of(type).size();
    int vecbytes = SIMDAssembler::VectorBytes(args.c().type());
    SIMDAssembler sasm(masm, type, args.Aligned(vecbytes));
    step->set_variant(sasm.name() + "RC");
    CHECK_EQ(args.a().width(), args.b().width());
    CHECK_EQ(args.a().batch_size(), 1);

    // Compute vector processing strategy.
    SIMDStrategy strategy(&sasm, args.b().width());
    strategy.PreloadMasks();

    // Allocate registers.
    Register a = masm->rr().alloc();
    Register b = masm->rr().alloc();
    Register c = masm->rr().alloc();
    Register b_ptr = masm->rr().alloc();
    Register b_end = masm->rr().alloc();
    Register ofs = masm->rr().alloc();
    auto sum = sasm.alloc(strategy.MaxUnrolls());
    auto elem = sasm.alloc(strategy.MaxUnrolls());

    // Load tensor addresses.
    __ LoadTensorAddress(a, args.a().tensor);
    __ LoadTensorAddress(b, args.b().tensor);
    __ LoadTensorAddress(c, args.c().tensor);

    // Loop over rows in A.
    if (args.b().height() > 1) {
      __ leaq(b_end, Operand(b, args.b().size()));
    }
    Register a_end = masm->rr().alloc();
    Label l1;
    if (args.a().height() > 1) {
      __ leaq(a_end, Operand(a, args.a().size()));
      __ bind(&l1);
    }

    // Loop over rows in B.
    Label l2;
    if (args.b().height() > 1) {
      if (args.a().height() > 1) {
        __ movq(b_ptr, b);
      } else {
        b_ptr = b;
      }
      __ bind(&l2);
    } else {
      b_ptr = b;
    }
    for (auto r : sum) sasm.main()->Zero(r);

    // Compute dot product between row in A and row in B.
    bool scalar = true;
    for (auto &phase : strategy.phases()) {
      auto *gen = phase.generator;
      int vecsize = gen->VectorSize();
      int blkstart = phase.offset * dsize;
      int blksize = phase.unrolls * vecsize * dsize;
      if (vecsize > 1) scalar = false;

      if (phase.repeat > 1) {
        // Repeated phase.
        Label l3;
        if (blkstart == 0) {
          __ xorq(ofs, ofs);
        } else {
          __ movq(ofs, Immediate(blkstart));
        }
        __ bind(&l3);
        for (int i = 0; i < phase.unrolls; ++i) {
          int disp = i * vecsize * dsize;
          gen->Load(elem[i], Operand(a, ofs, times_1, disp));
          gen->MulAdd(sum[i], elem[i], Operand(b_ptr, ofs, times_1, disp),
                      false);
        }
        __ addq(ofs, Immediate(blksize));
        __ cmpq(ofs, Immediate(blkstart + phase.repeat * blksize));
        __ j(less, &l3);
      } else if (phase.masked == 0) {
        // Residual phase.
        if (phase.offset == 0 || vecsize == sasm.main()->VectorSize()) {
          // Same vector size as bulk; unroll directly into sum registers.
          for (int i = 0; i < phase.unrolls; ++i) {
            int disp = blkstart + i * vecsize * dsize;
            gen->Load(elem[i], Operand(a, disp));
            gen->MulAdd(sum[i], elem[i], Operand(b_ptr, disp), false);
          }
        } else if (phase.unrolls == 1) {
          // Single residual; merge into first sum register.
          gen->Load(elem[0], Operand(a, blkstart));
          gen->Mul(elem[0], elem[0], Operand(b_ptr, blkstart));
          sasm.main()->Add(sum[0], sum[0], elem[0]);
        } else {
          // Accumulate unrolled residual and merge into first sum register.
          auto acc = sasm.alloc();
          gen->Zero(acc);
          for (int i = 0; i < phase.unrolls; ++i) {
            int disp = blkstart + i * vecsize * dsize;
            gen->Load(elem[i], Operand(a, disp));
            gen->MulAdd(acc, elem[i], Operand(b_ptr, disp), false);
          }
          sasm.main()->Add(sum[0], sum[0], acc);
        }
      } else {
        // Masked phase.
        CHECK_EQ(phase.unrolls, 1);
        gen->MaskedLoad(elem[0], Operand(a, blkstart));
        gen->MaskedMulAdd(sum[0], elem[0], Operand(b_ptr, blkstart));
      }
    }

    // Horizontal sum of results.
    sasm.Sum(sum);
    if (!scalar) sasm.main()->Sum(sum[0]);

    // Save result in C.
    if (accumulate_) {
      sasm.scalar()->Add(sum[0], sum[0], Operand(c));
    }
    sasm.scalar()->Store(Operand(c), sum[0]);
    if (args.c().elements() > 1) {
      __ addq(c, Immediate(dsize));
    }

    // Next row in B.
    if (args.b().height() > 1) {
      __ addq(b_ptr, Immediate(args.b().stride()));
      __ cmpq(b_ptr, b_end);
      __ j(less, &l2);
    }

    // Next row in A.
    if (args.a().height() > 1) {
      if (args.c().padding() > 0) {
        __ addq(c, Immediate(args.c().padding()));
      }
      __ addq(a, Immediate(args.a().stride()));
      __ cmpq(a, a_end);
      __ j(less, &l1);
    }
  }

  // Compute dot products between columns in A and rows in B.
  void GenerateColCol(Step *step, MacroAssembler *masm,
                      const MatMulArgs &args) {
    // Create SIMD code generators.
    Type type = args.c().tensor->type();
    int dsize = TypeTraits::of(type).size();
    SIMDAssembler sasm(masm, type, true);
    step->set_variant(sasm.name() + "CC");
    CHECK_EQ(args.a().height(), args.b().width());
    CHECK_EQ(args.a().batch_size(), 1);

    // Allocate registers. Allocate some preserved registers to avoid register
    // overflow.
    Register a = masm->rr().alloc_extra();
    Register b = masm->rr().alloc_extra();
    Register c = masm->rr().alloc_extra();
    Register b_ptr = masm->rr().alloc();
    Register a_end = masm->rr().alloc();
    Register b_end = masm->rr().alloc();
    Register a_ofs = masm->rr().alloc();
    Register b_ofs = masm->rr().alloc();
    auto elem = sasm.alloc();
    auto sum = sasm.alloc();

    // Save preserved registers.
    __ pushq(a);
    __ pushq(b);
    __ pushq(c);

    // Load tensor addresses.
    __ LoadTensorAddress(a, args.a().tensor);
    __ LoadTensorAddress(b, args.b().tensor);
    __ LoadTensorAddress(c, args.c().tensor);
    if (args.a().width() > 1) {
      __ leaq(a_end, Operand(a, args.a().width() * dsize));
    }
    if (args.b().height() > 1) {
      __ leaq(b_end, Operand(b, args.b().size()));
    }

    // Loop over columns in A.
    Label l1;
    __ bind(&l1);

    // Loop over rows in B.
    __ movq(b_ptr, b);
    Label l2;
    __ bind(&l2);

    // Compute dot product between column in A and row in B.
    auto *gen = sasm.scalar();
    if (args.b().width() == 1) {
      gen->Load(sum, Operand(a));
      gen->Mul(sum, sum, Operand(b_ptr));
    } else {
      __ xorq(a_ofs, a_ofs);
      __ xorq(b_ofs, b_ofs);
      gen->Zero(sum);
      Label l3;
      __ bind(&l3);
      gen->Load(elem, Operand(a, a_ofs));
      gen->MulAdd(sum, elem, Operand(b_ptr, b_ofs), false);
      __ addq(a_ofs, Immediate(args.a().stride()));
      __ addq(b_ofs, Immediate(dsize));
      __ cmpq(b_ofs, Immediate(args.b().width() * dsize));
      __ j(less, &l3);
    }

    // Save result in C.
    if (accumulate_) gen->Add(sum, sum, Operand(c));
    gen->Store(Operand(c), sum);
    __ addq(c, Immediate(dsize));

    // Next row in B.
    if (args.b().height() > 1) {
      __ addq(b_ptr, Immediate(args.b().stride()));
      __ cmpq(b_ptr, b_end);
      __ j(less, &l2);
    }

    // Next column in A.
    if (args.a().width() > 1) {
      if (args.c().padding() > 0) {
        __ addq(c, Immediate(args.c().padding()));
      }
      __ addq(a, Immediate(dsize));
      __ cmpq(a, a_end);
      __ j(less, &l1);
    }

    // Restore preserved registers.
    __ popq(c);
    __ popq(b);
    __ popq(a);
    masm->rr().release(a);
    masm->rr().release(b);
    masm->rr().release(c);
    masm->rr().free(a);
    masm->rr().free(b);
    masm->rr().free(c);
  }

  // Compute inner product between A and B.
  void GenerateInner(Step *step, MacroAssembler *masm, const MatMulArgs &args) {
    // Create SIMD code generators.
    Type type = args.c().tensor->type();
    int dsize = TypeTraits::of(type).size();
    int vecbytes = SIMDAssembler::VectorBytes(args.c().type());
    SIMDAssembler sasm(masm, type, args.Aligned(vecbytes));
    step->set_variant(sasm.name() + "VI");

    // Compute vector processing strategy.
    SIMDStrategy strategy(&sasm, args.a().columns());
    strategy.PreloadMasks();

    // Allocate registers.
    Register a = masm->rr().alloc();
    Register b = masm->rr().alloc();
    Register c = masm->rr().alloc();
    Register ofs = masm->rr().alloc();
    auto sum = sasm.alloc(strategy.MaxUnrolls());
    auto elem = sasm.alloc(strategy.MaxUnrolls());

    // Load tensor addresses.
    __ LoadTensorAddress(a, args.a().tensor);
    __ LoadTensorAddress(b, args.b().tensor);
    __ LoadTensorAddress(c, args.c().tensor);

    // Loop over batches.
    int batchsize = args.a().batch_size();
    Register batch = no_reg;
    Label lb;
    if (batchsize > 1) {
      batch = masm->rr().alloc();
      __ xorq(batch, batch);
      __ bind(&lb);
    }

    // Clear accumulators.
    for (auto r : sum) sasm.main()->Zero(r);

    // Compute dot product between A and B vectors.
    bool scalar = true;
    for (auto &phase : strategy.phases()) {
      auto *gen = phase.generator;
      int vecsize = gen->VectorSize();
      int blkstart = phase.offset * dsize;
      int blksize = phase.unrolls * vecsize * dsize;
      if (vecsize > 1) scalar = false;

      if (phase.repeat > 1) {
        // Repeated phase.
        Label l1;
        if (blkstart == 0) {
          __ xorq(ofs, ofs);
        } else {
          __ movq(ofs, Immediate(blkstart));
        }
        __ bind(&l1);
        for (int i = 0; i < phase.unrolls; ++i) {
          int disp = i * vecsize * dsize;
          gen->Load(elem[i], Operand(a, ofs, times_1, disp));
          gen->MulAdd(sum[i], elem[i], Operand(b, ofs, times_1, disp),
                      false);
        }
        __ addq(ofs, Immediate(blksize));
        __ cmpq(ofs, Immediate(blkstart + phase.repeat * blksize));
        __ j(less, &l1);
      } else if (phase.masked == 0) {
        // Residual phase.
        if (phase.offset == 0 || vecsize == sasm.main()->VectorSize()) {
          // Same vector size as bulk; unroll directly into sum registers.
          for (int i = 0; i < phase.unrolls; ++i) {
            int disp = blkstart + i * vecsize * dsize;
            gen->Load(elem[i], Operand(a, disp));
            gen->MulAdd(sum[i], elem[i], Operand(b, disp), false);
          }
        } else if (phase.unrolls == 1) {
          // Single residual; merge into first sum register.
          gen->Load(elem[0], Operand(a, blkstart));
          gen->Mul(elem[0], elem[0], Operand(b, blkstart));
          sasm.main()->Add(sum[0], sum[0], elem[0]);
        } else {
          // Accumulate unrolled residual and merge into first sum register.
          auto acc = sasm.alloc();
          gen->Zero(acc);
          for (int i = 0; i < phase.unrolls; ++i) {
            int disp = blkstart + i * vecsize * dsize;
            gen->Load(elem[i], Operand(a, disp));
            gen->MulAdd(acc, elem[i], Operand(b, disp), false);
          }
          sasm.main()->Add(sum[0], sum[0], acc);
        }
      } else {
        // Masked phase.
        CHECK_EQ(phase.unrolls, 1);
        gen->MaskedLoad(elem[0], Operand(a, blkstart));
        gen->MaskedMulAdd(sum[0], elem[0], Operand(b, blkstart));
      }
    }

    // Horizontal sum of results.
    sasm.Sum(sum);
    if (!scalar) sasm.main()->Sum(sum[0]);

    // Save result in C.
    if (accumulate_) {
      sasm.scalar()->Add(sum[0], sum[0], Operand(c));
    }
    sasm.scalar()->Store(Operand(c), sum[0]);

    // Next batch.
    if (batchsize > 1) {
      __ addq(a, Immediate(args.a().size()));
      __ addq(b, Immediate(args.b().size()));
      __ addq(c, Immediate(dsize));
      __ incq(batch);
      __ cmpq(batch, Immediate(batchsize));
      __ j(less, &lb);
    }
  }

  // Compute outer product between A and B.
  void GenerateOuter(Step *step, MacroAssembler *masm, const MatMulArgs &args) {
    // Create SIMD code generators.
    Type type = args.c().tensor->type();
    int dsize = TypeTraits::of(type).size();
    int vecbytes = SIMDAssembler::VectorBytes(args.c().type());
    SIMDAssembler sasm(masm, type, args.Aligned(vecbytes));
    step->set_variant(sasm.name() + "VO");

    // Compute vector processing strategy.
    SIMDStrategy strategy(&sasm, args.a().rows());
    strategy.PreloadMasks();

    // Get matrix dimensions.
    int rows = args.c().rows();
    int rowsize = args.c().stride();
    int blksize = sasm.main()->SupportsUnroll() ? 4 : 1;

    // Allocate general registers.
    Register aptr = masm->rr().alloc();
    Register bptr = masm->rr().alloc();
    Register cptr = masm->rr().alloc();
    Register colofs = masm->rr().alloc();
    Register rowofs = masm->rr().alloc();

    // Allocate SIMD registers.
    auto areg = sasm.alloc(blksize);
    auto breg = sasm.alloc(strategy.MaxUnrolls());
    auto creg = sasm.alloc(strategy.MaxUnrolls());

    // Load tensor locations.
    __ LoadTensorAddress(aptr, args.a().tensor);
    __ LoadTensorAddress(bptr, args.b().tensor);
    __ LoadTensorAddress(cptr, args.c().tensor);

    // Loop over batches.
    int batchsize = args.a().batch_size();
    Register batch = no_reg;
    Label lb;
    if (batchsize > 1) {
      batch = masm->rr().alloc();
      __ xorq(batch, batch);
      __ bind(&lb);
    }

    // First compute rows in blocks (stage 0) and then the remaining ones one
    // row at a time (stage 1).
    __ xorq(rowofs, rowofs);
    for (int stage = 0; stage < 2; ++stage) {
      // Determine the row block size.
      int rowblk;
      int rowend;
      bool single;
      bool more;
      if (stage == 0) {
        if (rows < blksize) continue;
        rowblk = blksize;
        rowend = (rows / blksize) * blksize;
        single = (rows == blksize);
        more = !single || rows % blksize != 0;
      } else {
        if (rows % blksize == 0) continue;
        rowblk = 1;
        rowend = rows;
        single = (rows % blksize == 1);
        more = !single;
      }

      // Outer loop over row blocks.
      Label l1;
      __ bind(&l1);

      // Load a[row] block.
      for (int r = 0; r < rowblk; ++r) {
        int disp = r * dsize;
        sasm.main()->Broadcast(areg[r], Operand(aptr, rowofs, times_1, disp));
      }

      for (auto &phase : strategy.phases()) {
        auto *gen = phase.generator;
        int vecsize = gen->VectorSize();
        int blkstart = phase.offset * dsize;
        int blksize = phase.unrolls * vecsize * dsize;

        if (phase.repeat > 1) {
          // Repeated phase.
          if (blkstart == 0) {
            __ xorq(colofs, colofs);
          } else {
            __ movq(colofs, Immediate(blkstart));
          }

          Label l2;
          __ bind(&l2);

          // Load b[col] block.
          for (int c = 0; c < phase.unrolls; ++c) {
            int disp = c * vecsize * dsize;
            gen->Load(breg[c], Operand(bptr, colofs, times_1, disp));
          }

          // Multiply a[row] block with b[col] block.
          for (int r = 0; r < rowblk; ++r) {
            for (int c = 0; c < phase.unrolls; ++c) {
              int disp = r * rowsize + c * vecsize * dsize;
              if (accumulate_) {
                gen->Load(creg[c], Operand(cptr, colofs, times_1, disp));
                gen->MulAdd(creg[c], areg[r], breg[c], true);
              } else {
                gen->Mul(creg[c], areg[r], breg[c]);
              }
              gen->Store(Operand(cptr, colofs, times_1, disp), creg[c]);
            }
          }

          // Next column block.
          __ addq(colofs, Immediate(blksize));
          __ cmpq(colofs, Immediate(blkstart + phase.repeat * blksize));
          __ j(less, &l2);
        } else if (phase.masked == 0) {
          // Load b[col] block.
          for (int c = 0; c < phase.unrolls; ++c) {
            int disp = blkstart + c * vecsize * dsize;
            gen->Load(breg[c], Operand(bptr, disp));
          }

          // Multiply a[row] block with b[col] block.
          for (int r = 0; r < rowblk; ++r) {
            for (int c = 0; c < phase.unrolls; ++c) {
              int disp = blkstart + r * rowsize + c * vecsize * dsize;
              if (accumulate_) {
                gen->Load(creg[c], Operand(cptr, disp));
                gen->MulAdd(creg[c], areg[r], breg[c], true);
              } else {
                gen->Mul(creg[c], areg[r], breg[c]);
              }
              gen->Store(Operand(cptr, disp), creg[c]);
            }
          }
        } else {
          // Masked phase.
          CHECK_EQ(phase.unrolls, 1);

          // Load b[col].
          gen->MaskedLoad(breg[0], Operand(bptr, blkstart));

          // Multiply a[row] block with b[col].
          for (int r = 0; r < rowblk; ++r) {
            int disp = blkstart + r * rowsize;
            if (accumulate_) {
              gen->MaskedLoad(creg[0], Operand(cptr, disp));
              gen->MulAdd(creg[0], areg[r], breg[0], true);
            } else {
              gen->Mul(creg[0], areg[r], breg[0]);
            }
            gen->MaskedStore(Operand(cptr, disp), creg[0]);
          }
        }
      }

      // Next row block.
      if (more || batchsize > 1) {
        __ addq(cptr, Immediate(rowblk * rowsize));
      }
      if (!single) {
        __ addq(rowofs, Immediate(rowblk * dsize));
        __ cmpq(rowofs, Immediate(rowend * dsize));
        __ j(less, &l1);
      }
    }

    // Next batch.
    if (batchsize > 1) {
      __ addq(aptr, Immediate(args.a().size()));
      __ addq(bptr, Immediate(args.b().size()));
      __ incq(batch);
      __ cmpq(batch, Immediate(batchsize));
      __ j(less, &lb);
    }
  }

  int64 Complexity(const Step *step) override {
    MatMulArgs args(step);
    int64 ops = args.c().tensor->elements();
    ops *= args.a().columns();
    ops *= 2;
    return  ops;
  }

 private:
  bool accumulate_;  // matmul with assignment
};

void RegisterSIMDMatMulLibrary(Library *library) {
  library->Register(new SIMDMatMul(true));
  library->Register(new SIMDMatMul(false));
}

}  // namespace myelin
}  // namespace sling

