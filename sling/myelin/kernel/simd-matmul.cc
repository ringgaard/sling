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
  // Maximum number of loop unrolls.
  static const int kMaxUnrolls = 4;

  // Maximum number of adder registers.
  static const int kMaxAdders = 4;

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
    int vecbytes = SIMDAssembler::VectorBytes(args.c().type());
    SIMDAssembler sasm(masm, type, args.Aligned(vecbytes));
    LOG(INFO) << step->name() << " generated by " << sasm.name()
              << (args.Aligned(vecbytes) ? " aligned" : " unaligned");

    // Set kernel variant.
    step->set_variant(sasm.name() + "RR");

    __ nop();
  }

  void GenerateRowCol(Step *step, MacroAssembler *masm,
                      const MatMulArgs &args) {
    // Create SIMD code generators.
    Type type = args.c().tensor->type();
    int vecbytes = SIMDAssembler::VectorBytes(args.c().type());
    SIMDAssembler sasm(masm, type, args.Aligned(vecbytes));
    LOG(INFO) << step->name() << " generated by " << sasm.name()
              << (args.Aligned(vecbytes) ? " aligned" : " unaligned");

    // Set kernel variant.
    step->set_variant(sasm.name() + "RC");

    __ nop();
  }

  void GenerateColRow(Step *step, MacroAssembler *masm,
                      const MatMulArgs &args) {
    __ nop();
  }

  void GenerateColCol(Step *step, MacroAssembler *masm,
                      const MatMulArgs &args) {
    LOG(INFO) << "ColCol";
    __ nop();
  }

  int64 Complexity(const Step *step) override {
    MatMulArgs args(step);
    return args.a().tensor->elements() * args.b().tensor->elements() * 2;
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

