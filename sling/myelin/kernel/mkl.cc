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
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "sling/base/flags.h"
#include "sling/base/types.h"
#include "sling/myelin/compute.h"
#include "sling/myelin/macro-assembler.h"

DEFINE_string(mklrt, "", "Intel MKL runtime model");

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Handle to MKL library.
static void *mkl_lib = nullptr;

// MKL functions.
void *cblas_sgemm = nullptr;
void *cblas_dgemm = nullptr;

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

// Intel MKL runtime models.
std::map<string, std::vector<const char *>> mkl_runtimes = {
  // Default model.
  {"", {
    "libmkl_core.so",
    "libmkl_sequential.so",
    "libmkl_intel_ilp64.so"}
  },

  // Sequential model.
  {"seq", {
    "libmkl_core.so",
    "libmkl_sequential.so",
    "libmkl_intel_ilp64.so"}
  },

  // Intel OMP threading model.
  {"intel", {
    "libmkl_core.so",
    "libiomp5.so",
    "libmkl_intel_thread.so",
    "libmkl_intel_ilp64.so"}
  },

  // Intel Threading Building Blocks (TBB) model.
  {"tbb", {
    "libmkl_core.so",
    "libtbb.so",
    "libmkl_tbb_thread.so",
    "libmkl_intel_ilp64.so"}
  },

  // GNU OpenMP threading model.
  {"tbb", {
    "libmkl_core.so",
    "libgomp.so",
    "libmkl_gnu_thread.so",
    "libmkl_intel_ilp64.so"}
  },

  // Google MKL model.
  {"g3", {
    "libmklml_gnu.so",
    "libmklml_intel.so"}
  },

  // MKL local model.
  {"local", {
    "local/mkl/libmklml_gnu.so",
    "local/mkl/libmklml_intel.so"}
  },
};

// Load Intel MKL library.
static bool LoadMKLLibrary() {
  // Set up list of libraries to load.
  auto f = mkl_runtimes.find(FLAGS_mklrt);
  if (f == mkl_runtimes.end()) {
    LOG(ERROR) << "Unknown MKL runtime model: " << FLAGS_mklrt;
    return false;
  }

  // Try to load MKL libraries.
  void *lib = nullptr;
  for (auto *libname : f->second) {
    VLOG(2) << "Loading MKL runtime: " << libname;
    lib = dlopen(libname, RTLD_LAZY | RTLD_GLOBAL);
    if (lib == nullptr) {
      VLOG(1) << "Error loading " << libname << ": " << dlerror();
      return false;
    }
  }

  // Resolve library functions.
  cblas_sgemm = CHECK_NOTNULL(dlsym(lib, "cblas_sgemm"));
  cblas_dgemm = CHECK_NOTNULL(dlsym(lib, "cblas_dgemm"));

  mkl_lib  = lib;
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
    // Two inputs and one output.
    if (step->indegree() != 2) return false;
    if (step->outdegree() != 1) return false;
    Args args(step);

    // Check shape.
    if (!args.Compatible()) return false;

    // Check that MKL is supported.
    if (!args.A.tensor->SupportsOrder(ROW_MAJOR)) return false;
    if (!args.B.tensor->SupportsOrder(ROW_MAJOR)) return false;
    if (!args.C.tensor->SupportsOrder(ROW_MAJOR)) return false;
    if (!SupportsMKL()) return false;

    return true;
  }

  void Adjust(Step *step) override {
    Args args(step);

    // Only row-major supported for now.
    args.A.tensor->RequireOrder(ROW_MAJOR);
    args.B.tensor->RequireOrder(ROW_MAJOR);
    args.C.tensor->RequireOrder(ROW_MAJOR);

    // Set alignment to largest vector size supported by CPU.
    int alignment = args.traits.size();
    if (CPU::Enabled(SSE)) alignment = 16;
    if (CPU::Enabled(AVX)) alignment = 32;
    if (CPU::Enabled(AVX512F)) alignment = 64;
    args.A.tensor->SetMiniumAlignment(alignment);
    args.B.tensor->SetMiniumAlignment(alignment);
    args.C.tensor->SetMiniumAlignment(alignment);
  }

  void Generate(Step *step, MacroAssembler *masm) override {
    // Get arguments.
    Args args(step);

    // Get dimensions for matrices.
    int dsize = args.traits.size();
    int m = args.C.rows();
    int n = args.C.cols();
    int k = args.A.cols();
    int lda = args.A.stride() / dsize;
    int ldb = args.B.stride() / dsize;
    int ldc = args.C.stride() / dsize;

    // Set up arguments to gemm routine.
    Register tmp = masm->rr().alloc_temp();

    __ pushq(Immediate(ldc));
    __ LoadTensorAddress(tmp, args.C.tensor);
    __ pushq(tmp);

    __ pushq(Immediate(ldb));
    __ LoadTensorAddress(tmp, args.B.tensor);
    __ pushq(tmp);

    __ pushq(Immediate(lda));
    __ LoadTensorAddress(tmp, args.A.tensor);
    __ pushq(tmp);

    if (args.type == DT_FLOAT) {
      auto *one = masm->GetConstant<float>(1.0);
      __ movss(xmm0, one->address());  // alpha=1.0
    } else {
      auto *one = masm->GetConstant<double>(1.0);
      __ movsd(xmm0, one->address());  // alpha=1.0
    }
    __ pxor(xmm1, xmm1); // beta=0.0

    __ movq(arg_reg_1, Immediate(CBLAS_ROW_MAJOR));
    __ movq(arg_reg_2, Immediate(args.A.op()));
    __ movq(arg_reg_3, Immediate(args.B.op()));
    __ movq(arg_reg_4, Immediate(m));
    __ movq(arg_reg_5, Immediate(n));
    __ movq(arg_reg_6, Immediate(k));

    // Call MKL cblas_sgemm.
    if (args.type == DT_FLOAT) {
      __ call_extern(cblas_sgemm, "cblas_sgemm");
    } else {
      __ call_extern(cblas_dgemm, "cblas_dgemm");
    }
    __ addq(rsp, Immediate(6 * 8));
  }

  int64 Complexity(const Step *step) override {
    return step->input(0)->dim(0) * step->input(1)->elements() * 2;
  }

 private:
  // Matrix argument with optional transpose.
  struct Matrix {
    Matrix(Tensor *tensor, bool t) : tensor(tensor), transpose(t) {}

    // Element data type.
    Type type() const { return tensor->type(); }

    // Tensor rank.
    int rank() const { return tensor->rank(); }

    // Number of batch dimensions.
    int batchdims() const { return rank() - 2; }

    // Batch size.
    int batchsize() const { return tensor->shape().outer(batchdims()); }

    // Rows and comlumns after optional transpose.
    int rows() const { return tensor->dim(rank() - (transpose ? 1 : 2)); }
    int cols() const { return tensor->dim(rank() - (transpose ? 2 : 1)); }

    // Matrix stride for outer dimension.
    int stride() const { return tensor->stride(batchdims()); }

    // Matrix operation.
    CblasTranspose op() const {
      return transpose ? CBLAS_TRANS : CBLAS_NO_TRANS;
    }

    Tensor *tensor;   // underlying tensor for matrix
    bool transpose;   // transposed matrix
  };

  // Arguments for MatMul kernel.
  struct Args {
    Args(const Step *step)
      : A(step->input(0), step->GetAttr("transpose_a", false)),
        B(step->input(1), step->GetAttr("transpose_b", false)),
        C(step->output(0), step->GetAttr("transpose_c", false)),
        type(C.type()),
        traits(TypeTraits::of(type)) {
    }

    // Check that shapes and types are compatible.
    bool Compatible() const {
      // Check types.
      if (type != DT_FLOAT && type != DT_DOUBLE) return false;
      if (A.type() != type || B.type() != type) return false;

      // Output cannot be transposed.
      if (C.transpose) return false;

      // Check ranks.
      if (C.rank() < 2) return false;
      if (A.rank() != C.rank()) return false;
      if (B.rank() != C.rank()) return false;

      // Check shapes.
      if (A.rows() != C.rows()) return false;
      if (A.cols() != B.rows()) return false;
      if (B.cols() != C.cols()) return false;

      // Check batch size.
      if (C.batchsize() != A.batchsize()) return false;
      if (C.batchsize() != B.batchsize()) return false;

      return true;
    }

    // Arguments.
    Matrix A;
    Matrix B;
    Matrix C;

    // Datatype.
    Type type;
    const TypeTraits &traits;
  };
};

void RegisterMKLLibrary(Library *library) {
  library->Register(new MKLMatMul());
}

}  // namespace myelin
}  // namespace sling

