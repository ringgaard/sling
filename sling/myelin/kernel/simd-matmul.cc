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

// Maximum number of loop unrolls.
static const int kMaxUnrolls = 4;

// Arguments for matmul op. This takes transposition and element order of the
// arguments into account.
class MatMulArgs {
 public:
  struct Arg {
    // Intialize argument from tensor.
    void Init(Tensor *tensor, bool transposed) {
      this->tensor = tensor;
      this->transposed = transposed;
      if (transposed) {
        this->shape = tensor->shape().transpose();
      } else {
        this->shape = tensor->shape();
      }
    }

    // Transpose argument representation.
    void Transpose() {
      transposed = !transposed;
      shape = shape.transpose();
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

    // Width (inner dimension) with respect to order and transpose.
    int width() const { return shape.dim(0); }

    // Height (outer dimension) with respect to order and transpose.
    int height() const { return shape.dim(1); }

    // Size of tensor in bytes.
    int size() const { return tensor->size(); }

    // Number of bytes per row.
    int stride() const {
      return tensor->stride(0);
    }

    // Padding bytes per row.
    int padding() const {
      return tensor->padding(0);
    }

    // Data type for underlying tensor.
    Type type() const { return tensor->type(); }

    string ToString() const {
      extern const char *ordername[];
      string str = "name=" + tensor->name();
      str.append(" shape=");
      str.append(shape.ToString());
      str.append(" t=");
      str.append(transposed ? "y" : "n");
      str.append(" order=");
      str.append(ordername[order()]);
      str.append(" width=");
      str.append(std::to_string(width()));
      str.append(" height=");
      str.append(std::to_string(height()));
      str.append(" stride=");
      str.append(std::to_string(stride()));
      str.append(" padding=");
      str.append(std::to_string(padding()));
      return str;
    }

    Tensor *tensor;   // underlying tensor for argument
    Shape shape;      // shape after transposition
    bool transposed;  // argument transposition
  };

  // Check if inputs and outputs are valid for a matrix multiplication.
  static bool Valid(const Step *step) {
    if (step->type() == "AssignAddMatMul") {
      return step->indegree() >= 3;
    } else {
      return step->indegree() >= 2 && step->outdegree() >= 1;
    }
  }

  // Initialize arguments for matmul op. The arguments are arranged to meet the
  // required output order.
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
  void SetRequiredOrder(Order order) {
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
    c_.tensor->SetRequiredOrder(required);
  }

  // Check that argument shapes match a matrix multiplication.
  bool CheckShapes() const {
    // Check that tensors are matrices.
    if (a_.shape.rank() != 2) return false;
    if (b_.shape.rank() != 2) return false;
    if (c_.shape.rank() != 2) return false;

    if (a_.shape.dim(0) != c_.shape.dim(0)) return false;
    if (a_.shape.dim(1) != b_.shape.dim(0)) return false;
    if (b_.shape.dim(1) != c_.shape.dim(1)) return false;

    return true;
  }

  // Check if all elements are aligned.
  bool Aligned(int align) const {
    return a_.stride() % align == 0 &&
           b_.stride() % align == 0 &&
           c_.stride() % align == 0;
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

  // An accumulating matmul adds matrix multiplication to the result.
  bool accumulate_;
};

// General matrix multiplication using SIMD code generators.
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
    // Requires CPU with SSE support.
    if (!CPU::Enabled(SSE)) return false;

    // Two float 2D tensor inputs and one 2D tensor output.
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
    args.SetRequiredOrder(ROW_MAJOR);

    // Set alignment.
    int vecbytes = SIMDAssembler::VectorBytes(args.c().type());
    args.a().tensor->SetMiniumAlignment(vecbytes);
    args.b().tensor->SetMiniumAlignment(vecbytes);
    args.c().tensor->SetMiniumAlignment(vecbytes);
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    MatMulArgs args(step);
    CHECK(args.EnsureOutputOrder(ROW_MAJOR));

    LOG(INFO) << "Generate " << step->name();
    LOG(INFO) << "  a: " << args.a().ToString();
    LOG(INFO) << "  b: " << args.b().ToString();
    LOG(INFO) << "  c: " << args.c().ToString();

    // Use the input element order to choose matmul algorithm.
    Order a = args.a().order();
    Order b = args.b().order();
    if (a == ROW_MAJOR && b == ROW_MAJOR) {
      GenerateRowRow(step, masm, args);
    } else if (a == ROW_MAJOR && b == COLUMN_MAJOR) {
      GenerateRowCol(step, masm, args);
    } else if (a == COLUMN_MAJOR && b == ROW_MAJOR) {
      GenerateRowCol(step, masm, args);
    } else if (a == COLUMN_MAJOR && b == COLUMN_MAJOR) {
      GenerateColCol(step, masm, args);
    } else {
      LOG(FATAL) << "Unsupported element order";
    }
  }

  void GenerateRowRow(Step *step, MacroAssembler *masm,
                      const MatMulArgs &args) {
    // Create SIMD code generators.
    Type type = args.c().tensor->type();
    int dsize = TypeTraits::of(type).size();
    int vecbytes = SIMDAssembler::VectorBytes(args.c().type());
    SIMDAssembler sasm(masm, type, args.Aligned(vecbytes));
    step->set_variant(sasm.name() + "RR");

    // Compute vector processing strategy.
    SIMDStrategy strategy(&sasm, args.b().width(), kMaxUnrolls);
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

    // Load tensor addresses and masks.
    __ LoadTensorAddress(a, args.a().tensor);
    __ LoadTensorAddress(b, args.b().tensor);
    __ LoadTensorAddress(c, args.c().tensor);

    // Loop over rows in A.
    Register a_end = masm->rr().alloc();
    Label l1;
    if (args.a().height() > 1) {
      __ leaq(a_end, Operand(a, args.a().size()));
      __ bind(&l1);
    }

    // Compute dot product between row in A and column blocks in B.
    for (auto &phase : strategy.phases()) {
      auto *gen = phase.generator;
      int vecsize = gen->VectorSize();
      int blkstart = phase.offset * dsize;
      int blksize = phase.unrolls * vecsize * dsize;

      if (phase.repeat > 1) {
        // Repeated phase.
        Label l2;
        if (phase.offset == 0) {
          __ xorq(col_ofs, col_ofs);
        } else {
          __ movq(col_ofs, Immediate(blkstart));
        }
        __ bind(&l2);

        for (int r = 0; r < phase.unrolls; ++r) {
          gen->Zero(sum[r]);
        }
        __ xorq(a_ofs, a_ofs);
        __ leaq(b_ptr, Operand(b, col_ofs));

        // Loop over columns in A and rows in B.
        Label l3;
        __ bind(&l3);
        gen->Broadcast(elem, Operand(a, a_ofs));
        for (int i = 0; i < phase.unrolls; ++i) {
          int disp = i * args.b().stride();
          bool retain = i != phase.unrolls - 1;
          gen->MulAdd(sum[i], elem, Operand(b_ptr, disp), retain);
        }
        __ addq(b_ptr, Immediate(phase.unrolls * args.b().stride()));
        __ addq(a_ofs, Immediate(phase.unrolls * dsize));
        __ cmpq(a_ofs, Immediate(args.a().width() * dsize));
        __ j(less, &l3);

        // Save result in C.
        for (int i = 0; i < phase.unrolls; ++i) {
          if (accumulate_) {
            gen->Add(sum[i], sum[i], Operand(c, i * vecsize * dsize));
          }
          gen->Store(Operand(c, i * vecsize * dsize), sum[i]);
        }
        __ addq(c, Immediate(phase.unrolls * vecsize * dsize));

        // Next block.
        __ addq(col_ofs, Immediate(blksize));
        __ cmpq(col_ofs, Immediate(blkstart + phase.repeat * blksize));
        __ j(less, &l2);
      } else if (phase.masked == 0) {
        // Residual phase.
        for (int r = 0; r < phase.unrolls; ++r) {
          gen->Zero(sum[r]);
        }
        __ xorq(a_ofs, a_ofs);
        __ leaq(b_ptr, Operand(b, phase.offset * dsize));

        // Loop over columns in A and rows in B.
        Label l3;
        __ bind(&l3);
        gen->Broadcast(elem, Operand(a, a_ofs));
        for (int i = 0; i < phase.unrolls; ++i) {
          int disp = i * args.b().stride();
          bool retain = i != phase.unrolls - 1;
          gen->MulAdd(sum[i], elem, Operand(b_ptr, disp), retain);
        }
        __ addq(b_ptr, Immediate(phase.unrolls * args.b().stride()));
        __ addq(a_ofs, Immediate(phase.unrolls * dsize));
        __ cmpq(a_ofs, Immediate(args.a().width() * dsize));
        __ j(less, &l3);

        // Save result in C.
        for (int i = 0; i < phase.unrolls; ++i) {
          if (accumulate_) {
            gen->Add(sum[i], sum[i], Operand(c, i * vecsize * dsize));
          }
          gen->Store(Operand(c, i * vecsize * dsize), sum[i]);
        }
        __ addq(c, Immediate(phase.unrolls * vecsize * dsize));
      } else {
        // Masked phase.
        gen->Zero(sum[0]);
        __ xorq(a_ofs, a_ofs);
        __ leaq(b_ptr, Operand(b, phase.offset * dsize));

        // Loop over columns in A and rows in B.
        Label l3;
        __ bind(&l3);
        gen->Broadcast(elem, Operand(a, a_ofs));
        gen->MaskedMulAdd(sum[0], elem, Operand(b_ptr));
        __ addq(b_ptr, Immediate(args.b().stride()));
        __ addq(a_ofs, Immediate(dsize));
        __ cmpq(a_ofs, Immediate(args.a().width() * dsize));
        __ j(less, &l3);

        // Save result in C.
        if (accumulate_) {
          gen->MaskedAdd(sum[0], sum[0], Operand(c));
        }
        gen->MaskedStore(Operand(c), sum[0]);
        __ addq(c, Immediate(phase.masked * dsize));
      }
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

  void GenerateRowCol(Step *step, MacroAssembler *masm,
                      const MatMulArgs &args) {
    // Create SIMD code generators.
    Type type = args.c().tensor->type();
    int dsize = TypeTraits::of(type).size();
    int vecbytes = SIMDAssembler::VectorBytes(args.c().type());
    SIMDAssembler sasm(masm, type, args.Aligned(vecbytes));
    step->set_variant(sasm.name() + "RR");

    // Compute vector processing strategy.
    SIMDStrategy strategy(&sasm, args.b().width(), kMaxUnrolls);
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

    // Load tensor addresses and masks.
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
      __ movq(b_ptr, b);
      __ bind(&l2);
    } else {
      b_ptr = b;
    }
    for (auto r : sum) sasm.main()->Zero(r);

    // Compute dot product between row in A and row in B.
    for (auto &phase : strategy.phases()) {
      auto *gen = phase.generator;
      int vecsize = gen->VectorSize();
      int blkstart = phase.offset * dsize;
      int blksize = phase.unrolls * vecsize * dsize;

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
          int disp = i * vecsize;
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
            int disp = i * vecsize;
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
            int disp = i * vecsize + blkstart;
            gen->Load(elem[i], Operand(a, disp));
            gen->MulAdd(acc, elem[i], Operand(b, disp), false);
          }
          sasm.main()->Add(sum[0], sum[0], acc);
        }
      } else {
        // Masked phase.
        gen->MaskedLoad(elem[0], Operand(a, blkstart));
        gen->MaskedMulAdd(sum[0], sum[0], Operand(b_ptr, blkstart));
      }
    }

    // Horizontal sum of results.
    sasm.Sum(sum);
    sasm.main()->Sum(sum[0]);

    // Save result in C.
    if (accumulate_) {
      sasm.scalar()->Add(sum[0], sum[0], Operand(c));
    }
    sasm.scalar()->Store(Operand(c), sum[0]);
    __ addq(c, Immediate(dsize));

    // Next row in B.
    if (args.b().height() > 1) {
      __ addq(b_ptr, Immediate(args.b().stride()));
      __ cmpq(b, b_end);
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

  void GenerateColRow(Step *step, MacroAssembler *masm,
                      const MatMulArgs &args) {
    // Create SIMD code generators.
    Type type = args.c().tensor->type();
    int vecbytes = SIMDAssembler::VectorBytes(args.c().type());
    SIMDAssembler sasm(masm, type, args.Aligned(vecbytes));

    // Set kernel variant.
    step->set_variant(sasm.name() + "CR");
    LOG(INFO) << step->name() << " generated by " << step->variant()
              << (args.Aligned(vecbytes) ? " aligned" : " unaligned");

    __ nop();
  }

  void GenerateColCol(Step *step, MacroAssembler *masm,
                      const MatMulArgs &args) {
    // Create SIMD code generators.
    Type type = args.c().tensor->type();
    int vecbytes = SIMDAssembler::VectorBytes(args.c().type());
    SIMDAssembler sasm(masm, type, args.Aligned(vecbytes));

    // Set kernel variant.
    step->set_variant(sasm.name() + "CC");
    LOG(INFO) << step->name() << " generated by " << step->variant()
              << (args.Aligned(vecbytes) ? " aligned" : " unaligned");

    __ nop();
  }

  int64 Complexity(const Step *step) override {
    MatMulArgs args(step);
    return args.c().tensor->elements() * args.a().shape.dim(1) * 2;
  }

 private:
  bool accumulate_;  // matmul with assignment.
};

void RegisterSIMDMatMulLibrary(Library *library) {
  library->Register(new SIMDMatMul(true));
  library->Register(new SIMDMatMul(false));
}

}  // namespace myelin
}  // namespace sling

