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

#include "sling/myelin/kernel/mkl.h"

#include <dlfcn.h>
#include <mutex>
#include <string>
#include <vector>

#include "sling/base/flags.h"
#include "sling/base/types.h"
#include "sling/myelin/compute.h"
#include "sling/myelin/macro-assembler.h"

DEFINE_string(mkl_threading, "", "Intel MKL threading model: seq,par,gnu,tbb");

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Handle to MKL library.
static void *mkl_lib = nullptr;

// MKL functions.
void *cblas_sgemm = nullptr;

// Flag to check that we only try to initialize the MKL library once.
static std::once_flag mkl_initialized;

// Definitions from mkl_cblas.h in Intel Math Kernel Library.
enum CblasLayout {
  CBLAS_ROW_MAJOR = 101,
  CBLAS_COL_MAJOR = 102,
};

enum CblasTranspose {
  CBLAS_NO_TRANS   = 111,
  CBLAS_TRANS      = 112,
  CBLAS_CONJ_TRANS = 113,
};

// Load Intel MKL library.
static bool LoadMKLLibrary() {
  // Set up list of libraries to load.
  std::vector<const char *> libraries;
  libraries.push_back("libmkl_core.so");
  if (FLAGS_mkl_threading.empty() || FLAGS_mkl_threading == "seq") {
    libraries.push_back("libmkl_sequential.so");
  } else if (FLAGS_mkl_threading == "par") {
    libraries.push_back("libiomp5.so");
    libraries.push_back("libmkl_intel_thread.so");
  } else if (FLAGS_mkl_threading == "tbb") {
    libraries.push_back("libtbb.so");
    libraries.push_back("libmkl_tbb_thread.so");
  } else if (FLAGS_mkl_threading == "gnu") {
    libraries.push_back("libgomp.so");
    libraries.push_back("libmkl_gnu_thread.so");
  } else {
    LOG(FATAL) << "Unknown MKL threading model: " << FLAGS_mkl_threading;
  }
  libraries.push_back("libmkl_intel_ilp64.so");

  // Try to load MKL libraries.
  void *lib = nullptr;
  for (auto *libname : libraries) {
    lib = dlopen(libname, RTLD_LAZY | RTLD_GLOBAL);
    if (lib == nullptr) {
      VLOG(1) << "Error loading " << libname << ": " << dlerror();
      return false;
    }
  }
  mkl_lib  = lib;

  // Resolve library functions.
  cblas_sgemm = dlsym(mkl_lib, "cblas_sgemm");
  CHECK(cblas_sgemm != nullptr);

  return true;
}

// Check if MKL is supported.
bool SupportsMKL() {
  std::call_once(mkl_initialized, []() { LoadMKLLibrary(); });
  return mkl_lib != nullptr;
}

// Matrix multiplication using Intel Math Kernel Library, C = A * B.
class MKLMatMul : public Kernel {
 public:
  string Name() override { return "MKLMatMul"; }
  string Operation() override { return "MatMul"; }

  bool Supports(Step *step) override {
    // Two float 2D tensor inputs and one 2D tensor output.
    if (step->indegree() != 2) return false;
    if (step->outdegree() != 1) return false;
    Tensor *A = step->input(0);
    Tensor *B = step->input(1);
    Tensor *C = step->output(0);
    if (A->rank() != 2 || A->type() != DT_FLOAT) return false;
    if (B->rank() != 2 || B->type() != DT_FLOAT) return false;
    if (C->rank() != 2 || C->type() != DT_FLOAT) return false;

    // Check shape.
    bool tra = step->GetAttr("transpose_a", false);
    bool trb = step->GetAttr("transpose_b", false);
    bool trc = step->GetAttr("transpose_c", false);
    Shape a = A->shape();
    Shape b = B->shape();
    Shape c = C->shape();
    if (trc) return false;
    if (tra) a = a.transpose();
    if (trb) b = b.transpose();

    if (a.dim(0) != c.dim(0)) return false;
    if (a.dim(1) != b.dim(0)) return false;
    if (b.dim(1) != c.dim(1)) return false;

    // Check that MKL is supported.
    if (!A->SupportsOrder(ROW_MAJOR)) return false;
    if (!B->SupportsOrder(ROW_MAJOR)) return false;
    if (!C->SupportsOrder(ROW_MAJOR)) return false;
    if (!SupportsMKL()) return false;

    return true;
  }

  void Adjust(Step *step) override {
    Tensor *A = step->input(0);
    Tensor *B = step->input(1);
    Tensor *C = step->output(0);

    // Only row-major supported for now.
    A->RequireOrder(ROW_MAJOR);
    B->RequireOrder(ROW_MAJOR);
    C->RequireOrder(ROW_MAJOR);

    // Set alignment to largest vector size supported by CPU.
    int alignment = TypeTraits::of(C->type()).size();
    if (CPU::Enabled(SSE)) alignment = 16;
    if (CPU::Enabled(AVX)) alignment = 32;
    if (CPU::Enabled(AVX512F)) alignment = 64;
    A->SetMiniumAlignment(alignment);
    B->SetMiniumAlignment(alignment);
    C->SetMiniumAlignment(alignment);
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get inputs and outputs.
    Tensor *A = step->input(0);
    Tensor *B = step->input(1);
    Tensor *C = step->output(0);

    // Get dimensions for matrices.
    int dsize = sizeof(float);
    bool tra = step->GetAttr("transpose_a", false);
    bool trb = step->GetAttr("transpose_b", false);
    int m = C->dim(0);
    int n = C->dim(1);
    int k = tra ? A->dim(0) : A->dim(1);
    int lda = A->stride(0) / dsize;
    int ldb = B->stride(0) / dsize;
    int ldc = C->stride(0) / dsize;

    // Set up arguments to gemm routine.
    Register tmp = masm->rr().alloc_temp();

    __ pushq(Immediate(ldc));
    __ LoadTensorAddress(tmp, C);
    __ pushq(tmp);

    __ pushq(Immediate(ldb));
    __ LoadTensorAddress(tmp, B);
    __ pushq(tmp);

    __ pushq(Immediate(lda));
    __ LoadTensorAddress(tmp, A);
    __ pushq(tmp);

    auto *one = masm->GetConstant<float>(1.0);
    __ movss(xmm0, one->address());  // alpha=1.0
    __ pxor(xmm1, xmm1);  // beta=0.0

    __ movq(arg_reg_1, Immediate(CBLAS_ROW_MAJOR));
    __ movq(arg_reg_2, Immediate(tra ? CBLAS_TRANS : CBLAS_NO_TRANS));
    __ movq(arg_reg_3, Immediate(trb ? CBLAS_TRANS : CBLAS_NO_TRANS));
    __ movq(arg_reg_4, Immediate(m));
    __ movq(arg_reg_5, Immediate(n));
    __ movq(arg_reg_6, Immediate(k));

    // Call MKL cblas_sgemm.
    auto *cblas_sgemm_ref = masm->GetExtern("cblas_sgemm", cblas_sgemm);
    __ call(cblas_sgemm_ref->address());
    __ addq(rsp, Immediate(6 * 8));
  }

  int64 Complexity(const Step *step) override {
    return step->input(0)->dim(0) * step->input(1)->elements() * 2;
  }
};

void RegisterMKLLibrary(Library *library) {
  library->Register(new MKLMatMul());
}

}  // namespace myelin
}  // namespace sling

