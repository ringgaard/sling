#include "myelin/kernel/cuda.h"

#include <string>

#include "myelin/cuda/cuda-kernel.h"

namespace sling {
namespace myelin {

enum BinOp {ADD, SUB, MUL};

// Compute element-wise float binary operator using CUDA.
class CUDAFltBinaryOperator : public CUDAKernel {
 public:
  CUDAFltBinaryOperator(BinOp op) : op_(op) {}

  bool Supports(Step *step) override {
    // Requires CUDA support.
    if (!CUDAKernel::Supports(step)) return false;

    // Check inputs and outputs.
    if (step->inputs().size() != 2) return false;
    if (step->outputs().size() != 1) return false;
    Tensor *a = step->input(0);
    Tensor *b = step->input(1);
    Tensor *c = step->output(0);

    // Check type.
    if (a->type() != DT_FLOAT) return false;
    if (b->type() != DT_FLOAT) return false;
    if (c->type() != DT_FLOAT) return false;

    // Input and output must have same number of elements.
    if (a->shape().elements() != c->shape().elements()) return false;
    if (b->shape().elements() != c->shape().elements()) return false;

    return true;
  }

  void GeneratePTX(Step *step, PTXMacroAssembler *ptx) override {
    ptx_ret();
  }

  int Complexity(const Step *step) override {
    return step->input(0)->elements();
  }

 private:
  BinOp op_;
};

// Element-wise float add using CUDA.
class CUDAFltAdd : public CUDAFltBinaryOperator {
 public:
  CUDAFltAdd() : CUDAFltBinaryOperator(ADD) {}
  string Name() override { return "CUDAFltAdd"; }
  string Operation() override { return "Add"; }
};

// Element-wise float subtract using CUDA.
class CUDAFltSub : public CUDAFltBinaryOperator {
 public:
  CUDAFltSub() : CUDAFltBinaryOperator(SUB) {}
  string Name() override { return "CUDAFltSub"; }
  string Operation() override { return "Sub"; }
};

// Element-wise float multiply using CUDA.
class CUDAFltMul : public CUDAFltBinaryOperator {
 public:
  CUDAFltMul() : CUDAFltBinaryOperator(MUL) {}
  string Name() override { return "CUDAFltMul"; }
  string Operation() override { return "Mul"; }
};

// Compute element-wise integer binary operator using CUDA.
class CUDAIntBinaryOperator : public CUDAKernel {
 public:
  CUDAIntBinaryOperator(BinOp op) : op_(op) {}

  bool Supports(Step *step) override {
    // Requires CUDA support.
    if (!CUDAKernel::Supports(step)) return false;

    // Check inputs and outputs.
    if (step->inputs().size() != 2) return false;
    if (step->outputs().size() != 1) return false;
    Tensor *a = step->input(0);
    Tensor *b = step->input(1);
    Tensor *c = step->output(0);

    // Check type.
    if (a->type() != DT_INT8 &&
        a->type() != DT_INT16 &&
        a->type() != DT_INT32 &&
        a->type() != DT_INT64) {
      return false;
    }
    if (b->type() != a->type()) return false;
    if (c->type() != a->type()) return false;

    // Input and output must have same number of elements.
    if (a->shape().elements() != c->shape().elements()) return false;
    if (b->shape().elements() != c->shape().elements()) return false;

    // Only add and subtract supported.
    if (op_ != ADD && op_ != SUB) return false;

    return true;
  }

  void GeneratePTX(Step *step, PTXMacroAssembler *ptx) override {
    ptx_ret();
  }

  int Complexity(const Step *step) override {
    return step->input(0)->elements();
  }

 private:
  BinOp op_;
};

// Element-wise integer add using CUDA.
class CUDAIntAdd : public CUDAIntBinaryOperator {
 public:
  CUDAIntAdd() : CUDAIntBinaryOperator(ADD) {}
  string Name() override { return "CUDAIntAdd"; }
  string Operation() override { return "Add"; }
};

// Element-wise integer subtract using CUDA.
class CUDAIntSub : public CUDAIntBinaryOperator {
 public:
  CUDAIntSub() : CUDAIntBinaryOperator(SUB) {}
  string Name() override { return "CUDAIntSub"; }
  string Operation() override { return "Sub"; }
};

void RegisterCUDAOperators(Library *library) {
  // Computes  : c = a + b element-wise
  // Input     : a: float32[d1,...,dn]
  //             b: float32[d1,...,dn]
  // Output    : c: float32[d1,...,dn]
  // Requires  : CUDA
  library->Register(new CUDAFltAdd());

  // Computes  : c = a - b element-wise
  // Input     : a: float32[d1,...,dn]
  //             b: float32[d1,...,dn]
  // Output    : c: float32[d1,...,dn]
  // Requires  : CUDA
  library->Register(new CUDAFltSub());

  // Computes  : c = a * b element-wise
  // Input     : a: float32[d1,...,dn]
  //             b: float32[d1,...,dn]
  // Output    : c: float32[d1,...,dn]
  // Requires  : CUDA
  library->Register(new CUDAFltMul());

  // Computes  : c = a + b element-wise
  // Input     : a: int8/16/32/64[d1,...,dn]
  //             b: int8/16/32/64[d1,...,dn]
  // Output    : c: int8/16/32/64[d1,...,dn]
  // Requires  : CUDA
  library->Register(new CUDAIntAdd());

  // Computes  : c = a - b element-wise
  // Input     : a: int8/16/32/64[d1,...,dn]
  //             b: int8/16/32/64[d1,...,dn]
  // Output    : c: int8/16/32/64[d1,...,dn]
  // Requires  : CUDA
  library->Register(new CUDAIntSub());
}

}  // namespace myelin
}  // namespace sling

