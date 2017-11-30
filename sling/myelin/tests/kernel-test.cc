#include <math.h>
#include <string>

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/file/file.h"
#include "sling/myelin/compute.h"
#include "sling/myelin/cuda/cuda.h"
#include "sling/myelin/cuda/cuda-runtime.h"
#include "sling/myelin/kernel/cuda.h"
#include "sling/myelin/kernel/tensorflow.h"
#include "sling/myelin/tests/compare-kernels.h"
#include "third_party/jit/cpu.h"

using namespace sling;
using namespace sling::jit;
using namespace sling::myelin;

DEFINE_string(base, "", "Kernel to be tested against");
DEFINE_string(test, "", "Kernel to be tested");

DEFINE_bool(ignore_errors, false, "Ignore test errors");
DEFINE_double(matmul_accuracy, 1e-6, "Maximum error on matmul operations");
DEFINE_double(func_accuracy, 1e-5, "Maximum error on function operations");

DEFINE_int32(d, -1, "Vector dimension for tests");
DEFINE_int32(dmin, 1, "Minimum vector dimension for tests");
DEFINE_int32(dmax, 128, "Maximum vector dimension for tests");

DEFINE_int32(w, -1, "Matrix width for tests");
DEFINE_int32(wmin, 1, "Minimum matrix width for tests");
DEFINE_int32(wmax, 128, "Maximum matrix width for tests");

DEFINE_int32(m, -1, "Dimension for matrix multiplication tests");
DEFINE_int32(mmin, 1, "Minimum dimension for matrix multiplication tests");
DEFINE_int32(mmax, 32, "Maximum dimension for matrix multiplication tests");

DEFINE_double(minmat, 1.0, "Minimum value for matrix ops");
DEFINE_double(maxmat, 100.0, "Maximum value for matrix ops");

DEFINE_bool(sse, true, "SSE support");
DEFINE_bool(sse2, true, "SSE2 support");
DEFINE_bool(sse3, true, "SSE3 support");
DEFINE_bool(sse41, true, "SSE 4.1 support");
DEFINE_bool(avx, true, "AVX support");
DEFINE_bool(avx2, true, "AVX2 support");
DEFINE_bool(fma3, true, "FMA3 support");

Library library;
CUDARuntime cudart;

// Baseline implementation of float matrix multiplication.
void BaselineMatMatMul(const TensorData &A, const TensorData &B,
                       TensorData *C) {
  for (int i = 0; i < A.dim(0); ++i) {
    for (int j = 0; j < B.dim(1); ++j) {
      float sum = 0.0;
      for (int k = 0; k < A.dim(1); ++k) {
        sum += A.at<float>(i, k) * B.at<float>(k, j);
      }
      C->at<float>(i, j) = sum;
    }
  }
}

// Baseline implementation of float matrix multiplication with double precision
// adder.
void BaselineMatMatMul1(const TensorData &A, const TensorData &B,
                        TensorData *C) {
  for (int i = 0; i < A.dim(0); ++i) {
    for (int j = 0; j < B.dim(1); ++j) {
      double sum = 0.0;
      for (int k = 0; k < A.dim(1); ++k) {
        sum += A.at<float>(i, k) * B.at<float>(k, j);
      }
      C->at<float>(i, j) = sum;
    }
  }
}

// Baseline implementation of float matrix multiplication with double precision
// multiplication and adder.
void BaselineMatMatMul2(const TensorData &A, const TensorData &B,
                        TensorData *C) {
  for (int i = 0; i < A.dim(0); ++i) {
    for (int j = 0; j < B.dim(1); ++j) {
      double sum = 0.0;
      for (int k = 0; k < A.dim(1); ++k) {
        double a = A.at<float>(i, k);
        double b = B.at<float>(k, j);
        sum += a * b;
      }
      C->at<float>(i, j) = sum;
    }
  }
}

// Baseline implementation of argmax.
void BaselineArgMax(const TensorData &x, TensorData *y) {
  float maxval = -INFINITY;
  int best = -1;
  for (int i = 0; i < x.dim(0); ++i) {
    float value = x.at<float>(i);
    if (value > maxval) {
      maxval = value;
      best = i;
    }
  }
  y->at<int>(0) = best;
}

void CheckTest(bool success) {
  if (!FLAGS_ignore_errors && !success) {
    LOG(FATAL) << "Test failed, aborting";
  }
}

void CheckFltMatMul(const string &test, const string &base) {
  if (!FLAGS_test.empty() && FLAGS_test != test) return;
  if (!FLAGS_base.empty() && FLAGS_base != base) return;
  LOG(INFO) << "Testing " << test << " against " << base;
  for (int d = FLAGS_dmin; d <= FLAGS_dmax; ++d) {
    for (int w = FLAGS_wmin; w <= FLAGS_wmax; ++w) {
      VLOG(1) << "Testing " << d << "x" << w;
      FltKernelComparator matmul(library, "MatMul", test, base);
      if (cudart.connected()) matmul.set_runtime(&cudart);
      matmul.AddInput("x", {1, d}, FLAGS_minmat, 100.0);
      matmul.AddInput("W", {d, w}, FLAGS_minmat, 100.0);
      matmul.AddOutput("y", {1, w}, FLAGS_matmul_accuracy);
      CheckTest(matmul.Check(3));
    }
  }
}

void CheckFltMatMulAdd(const string &test, const string &base) {
  if (!FLAGS_test.empty() && FLAGS_test != test) return;
  if (!FLAGS_base.empty() && FLAGS_base != base) return;
  LOG(INFO) << "Testing " << test << " against " << base;
  FltKernelComparator matmul(library, "MatMulAdd", test, base);
  if (cudart.connected()) matmul.set_runtime(&cudart);
  matmul.AddInput("x", {1, 10}, FLAGS_minmat, FLAGS_maxmat);
  matmul.AddInput("W", {10, 100}, FLAGS_minmat, FLAGS_maxmat);
  matmul.AddInput("b", {100}, -10.0, 10.0);
  matmul.AddOutput("y", {1, 100}, FLAGS_matmul_accuracy);
  CheckTest(matmul.Check(100));
}

void CheckFltMatMulRelu(const string &test, const string &base) {
  if (!FLAGS_test.empty() && FLAGS_test != test) return;
  if (!FLAGS_base.empty() && FLAGS_base != base) return;
  LOG(INFO) << "Testing " << test << " against " << base;
  FltKernelComparator matmul(library, "MatMulRelu", test, base);
  if (cudart.connected()) matmul.set_runtime(&cudart);
  matmul.AddInput("x", {1, 10}, FLAGS_minmat, FLAGS_maxmat);
  matmul.AddInput("W", {10, 100}, FLAGS_minmat, FLAGS_maxmat);
  matmul.AddOutput("y", {1, 100}, FLAGS_matmul_accuracy);
  CheckTest(matmul.Check(100));
}

void CheckFltMatMulAddRelu(const string &test, const string &base) {
  if (!FLAGS_test.empty() && FLAGS_test != test) return;
  if (!FLAGS_base.empty() && FLAGS_base != base) return;
  LOG(INFO) << "Testing " << test << " against " << base;
  FltKernelComparator matmul(library, "MatMulAddRelu", test, base);
  if (cudart.connected()) matmul.set_runtime(&cudart);
  matmul.AddInput("x", {1, 10}, FLAGS_minmat, FLAGS_maxmat);
  matmul.AddInput("W", {10, 100}, FLAGS_minmat, FLAGS_maxmat);
  matmul.AddInput("b", {100}, FLAGS_minmat, FLAGS_maxmat);
  matmul.AddOutput("y", {1, 100}, FLAGS_matmul_accuracy);
  CheckTest(matmul.Check(100));
}

void CheckFltMatMatMul(const string &test, const string &base) {
  if (!FLAGS_test.empty() && FLAGS_test != test) return;
  if (!FLAGS_base.empty() && FLAGS_base != base) return;
  LOG(INFO) << "Testing " << test << " against " << base;
  for (int i = FLAGS_mmin; i <= FLAGS_mmax; ++i) {
    for (int j = FLAGS_mmin; j <= FLAGS_mmax; ++j) {
      for (int k = FLAGS_mmin; k <= FLAGS_mmax; ++k) {
        FltKernelComparator matmul(library, "MatMul", test, base);
        if (cudart.connected()) matmul.set_runtime(&cudart);
        matmul.AddInput("A", {i, j}, FLAGS_minmat, FLAGS_maxmat);
        matmul.AddInput("B", {j, k}, FLAGS_minmat, FLAGS_maxmat);
        matmul.AddOutput("C", {i, k}, FLAGS_matmul_accuracy);
        CheckTest(matmul.Check(2));
      }
    }
  }
}

void CheckFltFunc(const string &func,
                  const string &test,
                  const string &base,
                  int modulo = 0,
                  bool negative = true) {
  if (!FLAGS_test.empty() && FLAGS_test != test) return;
  if (!FLAGS_base.empty() && FLAGS_base != base) return;
  LOG(INFO) << "Testing " << test << " against " << base;
  for (int d = FLAGS_dmin; d <= FLAGS_dmax; d++) {
    if (modulo != 0 && d % modulo != 0) continue;
    VLOG(1) << "Testing " << d;
    FltKernelComparator comp(library, func, test, base);
    if (cudart.connected()) comp.set_runtime(&cudart);
    comp.AddInput("x", {d}, negative ? -10.0 : 1e-3, 10.0);
    comp.AddOutput("y", {d}, FLAGS_func_accuracy);
    CheckTest(comp.Check(10));
  }
}

void CheckFltBinOp(const string &func,
                   const string &test,
                   const string &base,
                   int modulo = 0) {
  if (!FLAGS_test.empty() && FLAGS_test != test) return;
  if (!FLAGS_base.empty() && FLAGS_base != base) return;
  LOG(INFO) << "Testing " << test << " against " << base;
  for (int d = FLAGS_dmin; d <= FLAGS_dmax; d++) {
    if (modulo != 0 && d % modulo != 0) continue;
    VLOG(1) << "Testing " << d;
    FltKernelComparator comp(library, func, test, base);
    if (cudart.connected()) comp.set_runtime(&cudart);
    comp.AddInput("a", {d}, -100.0, 100.0);
    comp.AddInput("b", {d}, -100.0, 100.0);
    comp.AddOutput("c", {d}, FLAGS_func_accuracy);
    CheckTest(comp.Check(10));
  }
}

void CheckMulTwoAdd(const string &func,
                    const string &test,
                    const string &base) {
  if (!FLAGS_test.empty() && FLAGS_test != test) return;
  if (!FLAGS_base.empty() && FLAGS_base != base) return;
  LOG(INFO) << "Testing " << test << " against " << base;
  FltKernelComparator comp(library, func, test, base);
  comp.AddInput("x0", {10}, -10.0, 10.0);
  comp.AddInput("x1", {10}, -10.0, 10.0);
  comp.AddInput("x2", {10}, -10.0, 10.0);
  comp.AddInput("x3", {10}, -10.0, 10.0);
  comp.AddOutput("y", {10}, FLAGS_func_accuracy);
  CheckTest(comp.Check(100));
}

void CheckIntMatMul(const string &test, const string &base) {
  if (!FLAGS_test.empty() && FLAGS_test != test) return;
  if (!FLAGS_base.empty() && FLAGS_base != base) return;
  LOG(INFO) << "Testing " << test << " against " << base;
  IntKernelComparator matmul(library, "MatMul", test, base);
  if (cudart.connected()) {
    matmul.set_runtime(&cudart);
    matmul.AddInput("x", {1, 10}, DT_INT32);
    matmul.AddInput("W", {10, 100}, DT_INT32);
    matmul.AddOutput("y", {1, 100}, DT_INT32);
  } else {
    matmul.AddInput("x", {1, 10}, DT_INT8);
    matmul.AddInput("W", {10, 100}, DT_INT8);
    matmul.AddOutput("y", {1, 100}, DT_INT16);
  }
  CheckTest(matmul.Check(100));
}

void CheckIntBinOp(const string &func,
                   const string &test,
                   const string &base,
                   int modulo = 0) {
  if (!FLAGS_test.empty() && FLAGS_test != test) return;
  if (!FLAGS_base.empty() && FLAGS_base != base) return;
  LOG(INFO) << "Testing " << test << " against " << base;
  for (int d = FLAGS_dmin; d <= FLAGS_dmax; ++d) {
    if (modulo != 0 && d % modulo != 0) continue;
    VLOG(1) << "Testing " << d;
    if (!cudart.connected()) {
      IntKernelComparator comp8(library, func, test, base);
      comp8.AddInput("a", {d}, DT_INT8);
      comp8.AddInput("b", {d}, DT_INT8);
      comp8.AddOutput("c", {d}, DT_INT8);
      CheckTest(comp8.Check(10));
    }

    IntKernelComparator comp16(library, func, test, base);
    if (cudart.connected()) comp16.set_runtime(&cudart);
    comp16.AddInput("a", {d}, DT_INT16);
    comp16.AddInput("b", {d}, DT_INT16);
    comp16.AddOutput("c", {d}, DT_INT16);
    CheckTest(comp16.Check(10));

    IntKernelComparator comp32(library, func, test, base);
    if (cudart.connected()) comp32.set_runtime(&cudart);
    comp32.AddInput("a", {d}, DT_INT32);
    comp32.AddInput("b", {d}, DT_INT32);
    comp32.AddOutput("c", {d}, DT_INT32);
    CheckTest(comp32.Check(10));

    IntKernelComparator comp64(library, func, test, base);
    if (cudart.connected()) comp64.set_runtime(&cudart);
    comp64.AddInput("a", {d}, DT_INT64);
    comp64.AddInput("b", {d}, DT_INT64);
    comp64.AddOutput("c", {d}, DT_INT64);
    CheckTest(comp64.Check(10));
  }
}

void CheckArgMax(const string &test, const string &base) {
  if (!FLAGS_test.empty() && FLAGS_test != test) return;
  if (!FLAGS_base.empty() && FLAGS_base != base) return;
  LOG(INFO) << "Testing " << test << " against " << base;
  for (int d = FLAGS_dmin; d <= FLAGS_dmax; d++) {
    FltIntKernelComparator comp(library, "ArgMax", test, base);
    VLOG(1) << "Testing " << d;
    if (cudart.connected()) comp.set_runtime(&cudart);
    comp.AddInput("x", {d}, -10.0, 10.0);
    comp.AddOutput("y", {1}, DT_INT32);
    CheckTest(comp.Check(10));
  }
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);
  if (FLAGS_w != -1) FLAGS_wmin = FLAGS_wmax = FLAGS_w;
  if (FLAGS_d != -1) FLAGS_dmin = FLAGS_dmax = FLAGS_d;
  if (FLAGS_m != -1) FLAGS_mmin = FLAGS_mmax = FLAGS_m;

  // Disable selected CPU features.
  if (!FLAGS_sse) CPU::Disable(SSE);
  if (!FLAGS_sse2) CPU::Disable(SSE2);
  if (!FLAGS_sse3) CPU::Disable(SSE3);
  if (!FLAGS_sse41) CPU::Disable(SSE4_1);
  if (!FLAGS_avx) CPU::Disable(AVX);
  if (!FLAGS_avx2) CPU::Disable(AVX2);
  if (!FLAGS_fma3) CPU::Disable(FMA3);

  // Register kernels.
  RegisterTensorflowLibrary(&library);
  RegisterCUDALibrary(&library);
  library.Register("MatMul", "BaselineMatMatMul", BaselineMatMatMul)
     .Input(0, DT_FLOAT, 2)
     .Input(1, DT_FLOAT, 2)
     .Output(0, DT_FLOAT, 2);
  library.Register("MatMul", "BaselineMatMatMul1", BaselineMatMatMul1)
     .Input(0, DT_FLOAT, 2)
     .Input(1, DT_FLOAT, 2)
     .Output(0, DT_FLOAT, 2);
  library.Register("MatMul", "BaselineMatMatMul2", BaselineMatMatMul2)
     .Input(0, DT_FLOAT, 2)
     .Input(1, DT_FLOAT, 2)
     .Output(0, DT_FLOAT, 2);
  library.Register("ArgMax", "BaselineArgMax", BaselineArgMax)
     .Input(0, DT_FLOAT, 1)
     .Output(0, DT_INT32);

  // Test GenFltVecMatMul against itself to test the kernel comparator.
  CheckFltMatMul("GenFltVecMatMul", "GenFltVecMatMul");

  // Test baselines against each other.
  CheckFltMatMul("BaselineMatMatMul", "BaselineMatMatMul1");
  CheckFltMatMul("BaselineMatMatMul", "BaselineMatMatMul2");

  // Test GenFltVecMatMul against baseline.
  CheckFltMatMul("GenFltVecMatMul", "BaselineMatMatMul");
  CheckFltMatMul("GenFltVecMatMul", "BaselineMatMatMul1");
  CheckFltMatMul("GenFltVecMatMul", "BaselineMatMatMul2");

  // Test expression kernels.
  CheckFltBinOp("Add", "AddExpr", "GenFltAdd");
  CheckFltBinOp("Sub", "SubExpr", "GenFltSub");
  CheckFltBinOp("Mul", "MulExpr", "GenFltMul");

  CheckIntBinOp("Add", "AddExpr", "GenIntAdd");
  CheckIntBinOp("Sub", "SubExpr", "GenIntSub");
  CheckIntBinOp("Mul", "MulExpr", "GenIntMul");

  // Test argmax.
  CheckArgMax("GenFltArgMax", "BaselineArgMax");

  if (CPU::Enabled(SSE4_1)) {
    // Test expression intrinsics.
    CheckFltFunc("Log", "LogExpr", "GenFltLog", 0, false);
    CheckFltFunc("Exp", "ExpExpr", "GenFltExp");
    CheckFltFunc("Sigmoid", "SigmoidExpr", "GenFltSigmoid");
    CheckFltFunc("Tanh", "TanhExpr", "GenFltTanh");

    // Test SSE float matrix multiplication.
    CheckFltMatMul("SSEFltVecMatMul", "GenFltVecMatMul");
    CheckFltMatMulAdd("SSEFltVecMatMulAdd", "GenFltVecMatMulAdd");
    CheckFltMatMulRelu("SSEFltVecMatMulRelu", "GenFltVecMatMulRelu");
    CheckFltMatMulAddRelu("SSEFltVecMatMulAddRelu", "GenFltVecMatMulAddRelu");
  } else {
    LOG(WARNING) << "CPU does not support SSE 4.1, skipping SSE tests";
  }

  if (CPU::Enabled(AVX)) {
    // Test AVX float matrix multiplication.
    CheckFltMatMul("AVXFltVecMatMulV", "GenFltVecMatMul");
    CheckFltMatMulAdd("AVXFltVecMatMulAddV", "GenFltVecMatMulAdd");
    CheckFltMatMulRelu("AVXFltVecMatMulReluV", "GenFltVecMatMulRelu");
    CheckFltMatMulAddRelu("AVXFltVecMatMulAddReluV", "GenFltVecMatMulAddRelu");

    CheckFltMatMul("AVXFltVecMatMulH", "GenFltVecMatMul");
    CheckFltMatMulAdd("AVXFltVecMatMulAddH", "GenFltVecMatMulAdd");
    CheckFltMatMulRelu("AVXFltVecMatMulReluH", "GenFltVecMatMulRelu");
    CheckFltMatMulAddRelu("AVXFltVecMatMulAddReluH", "GenFltVecMatMulAddRelu");

    // Compare AVX float matrix multiplication to baseline.
    CheckFltMatMul("AVXFltVecMatMulV", "BaselineMatMatMul");
    CheckFltMatMul("AVXFltVecMatMulV", "BaselineMatMatMul1");
    CheckFltMatMul("AVXFltVecMatMulV", "BaselineMatMatMul2");

    CheckFltMatMul("AVXFltVecMatMulH", "BaselineMatMatMul");
    CheckFltMatMul("AVXFltVecMatMulH", "BaselineMatMatMul1");
    CheckFltMatMul("AVXFltVecMatMulH", "BaselineMatMatMul2");

    // Compare AVX matrix-matrix multiplication.
    CheckFltMatMatMul("AVXFltMatMatMul", "GenFltMatMatMul");

    // Test AVX math functions.
    CheckFltFunc("Exp", "AVXFltExp", "GenFltExp", 8);
    CheckFltFunc("Sigmoid", "AVXFltSigmoid", "GenFltSigmoid", 8);
    CheckFltFunc("Tanh", "AVXFltTanh", "GenFltTanh", 8);

    // Test AVX arithmetic operators.
    CheckFltBinOp("Add", "AVXFltAdd", "GenFltAdd", 8);
    CheckFltBinOp("Sub", "AVXFltSub", "GenFltSub", 8);
    CheckFltBinOp("Mul", "AVXFltMul", "GenFltMul", 8);

    CheckMulTwoAdd("MulTwoAdd", "AVXFltMulTwoAdd", "GenFltMulTwoAdd");
  } else {
    LOG(WARNING) << "CPU does not support AVX, skipping AVX tests";
  }

  if (CPU::Enabled(AVX2)) {
    // Test AVX integer operators.
    CheckIntBinOp("Add", "AVXIntAdd", "GenIntAdd", 8);
    CheckIntBinOp("Sub", "AVXIntSub", "GenIntSub", 8);

    // Test AVX integer matrix multiplication.
    CheckIntMatMul("AVXIntVecMatMulH", "GenIntVecMatMul");

    // Test AVX argmax.
    CheckArgMax("AVXFltArgMax", "GenFltArgMax");
  } else {
    LOG(WARNING) << "CPU does not support AVX2, skipping AVX2 tests";
  }

  if (CUDA::Supported()) {
    cudart.Connect();
    LOG(INFO) << cudart.Description();

    // Test CUDA floating point operators.
    CheckFltBinOp("Add", "CUDAAdd", "AddExpr");
    CheckFltBinOp("Sub", "CUDASub", "SubExpr");
    CheckFltBinOp("Mul", "CUDAMul", "MulExpr");
    CheckFltBinOp("Div", "CUDADiv", "DivExpr");
    CheckFltBinOp("Maximum", "CUDAMax", "MaxExpr");
    CheckFltBinOp("Minimum", "CUDAMin", "MinExpr");

    // Test CUDA integer operators.
    CheckIntBinOp("Add", "CUDAAdd", "GenIntAdd");
    CheckIntBinOp("Sub", "CUDASub", "GenIntSub");
    CheckIntBinOp("Mul", "CUDAMul", "GenIntMul");

    // Test CUDA functions.
    CheckFltFunc("Log", "CUDALog", "GenFltLog", 0, false);
    CheckFltFunc("Exp", "CUDAExp", "GenFltExp");
    CheckFltFunc("Sigmoid", "CUDASigmoid", "GenFltSigmoid");
    CheckFltFunc("Tanh", "CUDATanh", "GenFltTanh");

    CheckFltFunc("Negate", "CUDANegate", "NegateExpr");
    CheckFltFunc("Abs", "CUDAAbs", "AbsExpr");
    CheckFltFunc("Relu", "CUDARelu", "ReluExpr");
    CheckFltFunc("Reciprocal", "CUDAReciprocal", "ReciprocalExpr");
    CheckFltFunc("Square", "CUDASquare", "SquareExpr");

    // Test CUDA matrix mulplication.
    CheckFltMatMul("CUDAMatMul", "GenFltVecMatMul");
    CheckFltMatMulAdd("CUDAMatMulAdd", "GenFltVecMatMulAdd");
    CheckFltMatMulRelu("CUDAMatMulRelu", "GenFltVecMatMulRelu");
    CheckFltMatMulAddRelu("CUDAMatMulAddRelu", "GenFltVecMatMulAddRelu");
    CheckFltMatMatMul("CUDAMatMul", "GenFltMatMatMul");
    CheckIntMatMul("CUDAMatMul", "GenIntVecMatMul");

    // Test CUDA reductions.
    CheckArgMax("CUDAArgMax", "GenFltArgMax");

    cudart.Disconnect();
  } else {
    LOG(WARNING) << "No GPU, skipping CUDA tests";
  }

  return 0;
}

