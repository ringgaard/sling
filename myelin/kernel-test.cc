#include <iostream>
#include <string>

#include "base/flags.h"
#include "base/init.h"
#include "base/logging.h"
#include "file/file.h"
#include "myelin/compare-kernels.h"
#include "myelin/compute.h"
#include "myelin/kernel/sse.h"
#include "myelin/kernel/generic.h"
#include "myelin/kernel/avx.h"
#include "third_party/jit/cpu.h"

using namespace sling;
using namespace sling::jit;
using namespace sling::myelin;

DEFINE_bool(disable_fma3, false, "Disable FMA3 support");
DEFINE_bool(disable_avx2, false, "Disable AVX2 support");
DEFINE_bool(disable_sse41, false, "Disable SSE 4.1 support");

Library library;

void CheckFltMatMul(const string &test, const string &base) {
  LOG(INFO) << "Testing " << test << " against " << base;
  for (int d = 1; d <= 128; ++d) {
    for (int w = 1; w <= 128; ++w) {
      FltKernelComparator matmul(library, "MatMul", test, base);
      matmul.AddInput("x", {1, d}, -100.0, 100.0);
      matmul.AddInput("W", {d, w}, -100.0, 100.0);
      matmul.AddOutput("y", {1, w}, 0.1);
      CHECK(matmul.Check(3)) << "MatMul " << d << " " << w;
    }
  }
}

void CheckFltMatMulAdd(const string &test, const string &base) {
  LOG(INFO) << "Testing " << test << " against " << base;
  FltKernelComparator matmul(library, "MatMulAdd", test, base);
  matmul.AddInput("x", {1, 10}, -10.0, 10.0);
  matmul.AddInput("W", {10, 100}, -10.0, 10.0);
  matmul.AddInput("b", {100}, -10.0, 10.0);
  matmul.AddOutput("y", {1, 100}, 1e-2);
  CHECK(matmul.Check(100));
}

void CheckFltMatMulRelu(const string &test, const string &base) {
  LOG(INFO) << "Testing " << test << " against " << base;
  FltKernelComparator matmul(library, "MatMulRelu", test, base);
  matmul.AddInput("x", {1, 10}, -10.0, 10.0);
  matmul.AddInput("W", {10, 100}, -10.0, 10.0);
  matmul.AddOutput("y", {1, 100}, 1e-2);
  CHECK(matmul.Check(100));
}

void CheckFltMatMulAddRelu(const string &test, const string &base) {
  LOG(INFO) << "Testing " << test << " against " << base;
  FltKernelComparator matmul(library, "MatMulAddRelu", test, base);
  matmul.AddInput("x", {1, 10}, -10.0, 10.0);
  matmul.AddInput("W", {10, 100}, -10.0, 10.0);
  matmul.AddInput("b", {100}, -10.0, 10.0);
  matmul.AddOutput("y", {1, 100}, 1e-2);
  CHECK(matmul.Check(100));
}

void CheckFltMatMatMul(const string &test, const string &base) {
  LOG(INFO) << "Testing " << test << " against " << base;
  for (int i = 1; i <= 64; ++i) {
    for (int j = 1; j <= 64; ++j) {
      for (int k = 1; k <= 64; ++k) {
        FltKernelComparator matmul(library, "MatMul", test, base);
        matmul.AddInput("A", {i, j}, -10.0, 10.0);
        matmul.AddInput("B", {j, k}, -10.0, 10.0);
        matmul.AddOutput("C", {i, k}, 1e-2);
        CHECK(matmul.Check(2));
      }
    }
  }
}

void CheckFltFunc(const string &func,
                  const string &test,
                  const string &base) {
  LOG(INFO) << "Testing " << test << " against " << base;
  FltKernelComparator comp(library, func, test, base);
  comp.AddInput("x", {10}, -10.0, 10.0);
  comp.AddOutput("y", {10}, 1e-6);
  CHECK(comp.Check(100));
}

void CheckFltBinOp(const string &func,
                   const string &test,
                   const string &base) {
  LOG(INFO) << "Testing " << test << " against " << base;
  FltKernelComparator comp(library, func, test, base);
  comp.AddInput("a", {10}, -10.0, 10.0);
  comp.AddInput("b", {10}, -10.0, 10.0);
  comp.AddOutput("c", {10}, 1e-6);
  CHECK(comp.Check(100));
}

void CheckMulTwoAdd(const string &func,
                    const string &test,
                    const string &base) {
  LOG(INFO) << "Testing " << test << " against " << base;
  FltKernelComparator comp(library, func, test, base);
  comp.AddInput("x0", {10}, -10.0, 10.0);
  comp.AddInput("x1", {10}, -10.0, 10.0);
  comp.AddInput("x2", {10}, -10.0, 10.0);
  comp.AddInput("x3", {10}, -10.0, 10.0);
  comp.AddOutput("y", {10}, 1e-6);
  CHECK(comp.Check(100));
}

void CheckIntMatMul(const string &test, const string &base) {
  LOG(INFO) << "Testing " << test << " against " << base;
  IntKernelComparator matmul(library, "MatMul", test, base);
  matmul.AddInput("x", {1, 10}, DT_INT8);
  matmul.AddInput("W", {10, 100}, DT_INT8);
  matmul.AddOutput("y", {1, 100}, DT_INT16);
  CHECK(matmul.Check(100));
}

void CheckIntBinOp(const string &func,
                   const string &test,
                   const string &base) {
  LOG(INFO) << "Testing " << test << " against " << base;
  for (int w = 1; w <= 128; ++w) {
    IntKernelComparator comp8(library, func, test, base);
    comp8.AddInput("a", {w}, DT_INT8);
    comp8.AddInput("b", {w}, DT_INT8);
    comp8.AddOutput("c", {w}, DT_INT8);
    CHECK(comp8.Check(10));

    IntKernelComparator comp16(library, func, test, base);
    comp16.AddInput("a", {w}, DT_INT16);
    comp16.AddInput("b", {w}, DT_INT16);
    comp16.AddOutput("c", {w}, DT_INT16);
    CHECK(comp16.Check(10));

    IntKernelComparator comp32(library, func, test, base);
    comp32.AddInput("a", {w}, DT_INT32);
    comp32.AddInput("b", {w}, DT_INT32);
    comp32.AddOutput("c", {w}, DT_INT32);
    CHECK(comp32.Check(10));

    IntKernelComparator comp64(library, func, test, base);
    comp64.AddInput("a", {w}, DT_INT64);
    comp64.AddInput("b", {w}, DT_INT64);
    comp64.AddOutput("c", {w}, DT_INT64);
    CHECK(comp64.Check(10));
  }
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  if (FLAGS_disable_sse41) CPU::Disable(SSE4_1);
  if (FLAGS_disable_fma3) CPU::Disable(FMA3);
  if (FLAGS_disable_avx2) CPU::Disable(AVX2);

  RegisterAVXKernels(&library);
  RegisterSSEKernels(&library);
  RegisterGenericKernels(&library);
  RegisterGenericTransformations(&library);

  if (CPU::Enabled(SSE4_1)) {
    CheckFltMatMul("SSEFltVecMatMul", "GenFltVecMatMul");
    CheckFltMatMulAdd("SSEFltVecMatMulAdd", "GenFltVecMatMulAdd");
    CheckFltMatMulRelu("SSEFltVecMatMulRelu", "GenFltVecMatMulRelu");
    CheckFltMatMulAddRelu("SSEFltVecMatMulAddRelu", "GenFltVecMatMulAddRelu");
  } else {
    LOG(WARNING) << "CPU does not support SSE 4.1, skipping SSE tests";
  }

  if (CPU::Enabled(AVX)) {
    CheckFltMatMul("AVXFltVecMatMulV", "GenFltVecMatMul");
    CheckFltMatMul("AVXFltVecMatMulH", "GenFltVecMatMul");
    CheckFltMatMulAdd("AVXFltVecMatMulAddV", "GenFltVecMatMulAdd");
    CheckFltMatMulAdd("AVXFltVecMatMulAddH", "GenFltVecMatMulAdd");
    CheckFltMatMulRelu("AVXFltVecMatMulReluV", "GenFltVecMatMulRelu");
    CheckFltMatMulRelu("AVXFltVecMatMulReluH", "GenFltVecMatMulRelu");
    CheckFltMatMulAddRelu("AVXFltVecMatMulAddReluV", "GenFltVecMatMulAddRelu");
    CheckFltMatMulAddRelu("AVXFltVecMatMulAddReluH", "GenFltVecMatMulAddRelu");

    CheckFltMatMatMul("AVXFltMatMatMul", "GenFltMatMatMul");

    CheckFltFunc("Tanh", "AVXFltTanh", "GenFltTanh");
    CheckFltFunc("Exp", "AVXFltExp", "GenFltExp");
    CheckFltFunc("Sigmoid", "AVXFltSigmoid", "GenFltSigmoid");

    CheckFltBinOp("Add", "AVXFltAdd", "GenFltAdd");
    CheckFltBinOp("Mul", "AVXFltMul", "GenFltMul");

    CheckMulTwoAdd("MulTwoAdd", "AVXFltMulTwoAdd", "GenFltMulTwoAdd");
  } else {
    LOG(WARNING) << "CPU does not support AVX, skipping AVX tests";
  }

  if (CPU::Enabled(AVX2)) {
    CheckIntBinOp("Add", "AVXIntAdd", "GenIntAdd");
    CheckIntBinOp("Sub", "AVXIntSub", "GenIntSub");

    CheckIntMatMul("AVXIntVecMatMulH", "GenIntVecMatMul");
  } else {
    LOG(WARNING) << "CPU does not support AVX2, skipping AVX2 tests";
  }

  return 0;
}

