#include "myelin/kernel/cuda.h"

#include <string>

#include "myelin/cuda/cuda-kernel.h"

namespace sling {
namespace myelin {

// Vector-matrix multiplication  using CUDA.
class CUDAFltVecMatMulBase : public CUDAKernel {
 public:
  CUDAFltVecMatMulBase(bool bias, bool relu)
      : bias_(bias), relu_(relu) {}

  bool Supports(Step *step) override {
    // Requires CUDA support.
    if (!CUDAKernel::Supports(step)) return false;

    // Two or three float 2D tensor inputs and one 2D tensor output.
    if (step->inputs().size() != (bias_ ? 3 : 2)) return false;
    if (step->outputs().size() != 1) return false;
    Tensor *x = step->input(0);
    Tensor *W = step->input(1);
    Tensor *y = step->output(0);
    if (x->rank() != 2 || x->type() != DT_FLOAT) return false;
    if (W->rank() != 2 || W->type() != DT_FLOAT) return false;
    if (y->rank() != 2 || y->type() != DT_FLOAT) return false;

    // Check shape. First input must be a row vector.
    if (x->dim(0) != 1 || x->dim(1) != W->dim(0)) return false;
    if (y->dim(0) != x->dim(0) || y->dim(1) != W->dim(1)) return false;

    // The matrix must support column-major order.
    if (!W->SupportsOrder(COLUMN_MAJOR)) return false;

    // Check bias vector.
    if (bias_) {
      Tensor *b = step->input(2);
      if (b->type() != DT_FLOAT) return false;
      if (b->rank() == 1) {
        if (b->dim(0) != y->dim(1)) return false;
      } else if (b->rank() == 2) {
        if (b->dim(0) != 1 || b->dim(1) != y->dim(1)) return false;
      } else {
        return false;
      }
    }

    return true;
  }

  void GeneratePTX(Step *step, PTXMacroAssembler *ptx) override {
    ptx_ret();
  }

  int64 Complexity(const Step *step) override {
    int ops = step->input(1)->elements() * 2;
    if (bias_) ops += step->input(2)->elements();
    if (relu_) ops += step->output(0)->elements();
    return ops;
  }

 protected:
  bool bias_;    // add bias vector to result, y=Wx+b
  bool relu_;    // apply rectified linear unit, y=max(0,Wx+b)
};

class CUDAFltVecMatMul : public CUDAFltVecMatMulBase {
 public:
  CUDAFltVecMatMul() : CUDAFltVecMatMulBase(false, false) {}

  string Name() override { return "CUDAFltVecMatMul"; }
  string Operation() override { return "MatMul"; }
};

class CUDAFltVecMatMulAdd : public CUDAFltVecMatMulBase {
 public:
  CUDAFltVecMatMulAdd() : CUDAFltVecMatMulBase(true, false) {}

  string Name() override { return "CUDAFltVecMatMulAdd"; }
  string Operation() override { return "MatMulAdd"; }
};

class CUDAFltVecMatMulRelu : public CUDAFltVecMatMulBase {
 public:
  CUDAFltVecMatMulRelu() : CUDAFltVecMatMulBase(false, true) {}

  string Name() override { return "CUDAFltVecMatMulRelu"; }
  string Operation() override { return "MatMulRelu"; }
};

class CUDAFltVecMatMulAddRelu : public CUDAFltVecMatMulBase {
 public:
  CUDAFltVecMatMulAddRelu() : CUDAFltVecMatMulBase(true, true) {}

  string Name() override { return "CUDAFltVecMatMulAddRelu"; }
  string Operation() override { return "MatMulAddRelu"; }
};

void RegisterCUDAMatMul(Library *library) {
  // Computes  : y = x * W
  // Input     : x: float32[1,n]
  //             W: float32[n,m] column-major
  // Output    : y: float32[1,m]
  // Requires  : CUDA
  library->Register(new CUDAFltVecMatMul());

  // Computes  : y = x * W + b
  // Input     : x: float32[1,n]
  //             W: float32[n,m] column-major
  //             b: float32[1,n]
  // Output    : y: float32[1,m]
  // Requires  : CUDA
  library->Register(new CUDAFltVecMatMulAdd());

  // Computes  : y = max(0, x * W)
  // Input     : x: float32[1,n]
  //             W: float32[n,m] column-major
  // Output    : y: float32[1,m]
  // Requires  : AVX
  library->Register(new CUDAFltVecMatMulRelu());

  // Computes  : y = max(0, x * W + b)
  // Input     : x: float32[1,n]
  //             W: float32[n,m] column-major
  //             b: float32[1,n]
  // Output    : y: float32[1,m]
  // Requires  : CUDA
  library->Register(new CUDAFltVecMatMulAddRelu());
}

}  // namespace myelin
}  // namespace sling

