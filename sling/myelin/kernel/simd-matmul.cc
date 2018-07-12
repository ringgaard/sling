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
        default: tensor->order();
      }
    }

    // Width (inner dimension) with respect to order and transpose.
    int width() const {
      switch (tensor->order()) {
        case ROW_MAJOR: return shape.dim(1);
        case COLUMN_MAJOR: return shape.dim(0);
        default: LOG(FATAL) << "Undertermined layout: " << tensor->name();
      }
    }

    // Height (outer dimension) with respect to order and transpose.
    int height() const {
      switch (tensor->order()) {
        case ROW_MAJOR: return shape.dim(0);
        case COLUMN_MAJOR: return shape.dim(1);
        default: LOG(FATAL) << "Undertermined layout: " << tensor->name();
      }
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

  // Ensure output order.
  void EnsureOutputOrder(Order order) {
    // Determine if matmul need to be transformed to meet output element order
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

    // TODO: check types are the same and supported by SIMD generator.
    return true;
  }

  void Adjust(Step *step) override {
  }

  void Generate(Step *step, MacroAssembler *masm) override {
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

