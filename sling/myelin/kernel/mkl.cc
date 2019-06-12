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
DEFINE_bool(mklnojit, false, "Disable Intel MKL JIT");

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Definitions from mkl_cblas.h in Intel Math Kernel Library.

typedef int64 mkl_int_t;

enum MKLLayout : mkl_int_t {
  MKL_ROW_MAJOR = 101,
  MKL_COL_MAJOR = 102,
};

enum MKLTranspose : mkl_int_t {
  MKL_NO_TRANS   = 111,
  MKL_TRANS      = 112,
  MKL_CONJ_TRANS = 113,
};

enum MKLJITStatus : mkl_int_t {
  MKL_JIT_SUCCESS  = 0,
  MKL_NO_JIT       = 1,
  MKL_JIT_ERROR    = 2,
};

typedef void *gemm_jit_kernel_t;

// Handle to MKL library.
static void *mkl_lib = nullptr;

// MKL JIT support.
static bool mkl_jit_support = false;

// MKL functions.
void *cblas_sgemm = nullptr;
void *cblas_dgemm = nullptr;
void *cblas_sgemm_batch = nullptr;
void *cblas_dgemm_batch = nullptr;

// MKL JIT functions.

MKLJITStatus (*mkl_cblas_jit_create_sgemm)(
  void **jitter,
  const MKLLayout layout,
  const MKLTranspose transa, const MKLTranspose transb,
  const mkl_int_t m, const mkl_int_t n, const mkl_int_t k,
  const float alpha, const mkl_int_t lda, const mkl_int_t ldb,
  const float beta, const mkl_int_t ldc) = nullptr;

MKLJITStatus (*mkl_cblas_jit_create_dgemm)(
  void **jitter,
  const MKLLayout layout,
  const MKLTranspose transa, const MKLTranspose transb,
  const mkl_int_t m, const mkl_int_t n, const mkl_int_t k,
  const double alpha, const mkl_int_t lda, const mkl_int_t ldb,
  const double beta, const mkl_int_t ldc) = nullptr;

MKLJITStatus (*mkl_jit_destroy)(void *jitter) = nullptr;

gemm_jit_kernel_t (*mkl_jit_get_sgemm_ptr)(const void *jitter) = nullptr;
gemm_jit_kernel_t (*mkl_jit_get_dgemm_ptr)(const void *jitter) = nullptr;

// Flag to check that we only try to initialize the MKL library once.
static std::once_flag mkl_initialized;

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
  {"gnu", {
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

#define LOAD_MKL_FUNCTION(name) \
  name = reinterpret_cast<decltype(name)>(dlsym(lib , #name));

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
  cblas_sgemm_batch = CHECK_NOTNULL(dlsym(lib, "cblas_sgemm_batch"));
  cblas_dgemm_batch = CHECK_NOTNULL(dlsym(lib, "cblas_dgemm_batch"));

  // Resolve MKL JIT functions.
  if (!FLAGS_mklnojit) {
    LOAD_MKL_FUNCTION(mkl_cblas_jit_create_sgemm);
    LOAD_MKL_FUNCTION(mkl_cblas_jit_create_dgemm);
    LOAD_MKL_FUNCTION(mkl_jit_destroy);
    LOAD_MKL_FUNCTION(mkl_jit_get_sgemm_ptr);
    LOAD_MKL_FUNCTION(mkl_jit_get_dgemm_ptr);
  }

  mkl_lib  = lib;
  mkl_jit_support = (mkl_cblas_jit_create_sgemm != nullptr);

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

    // Check type and shape.
    if (!args.Compatible()) return false;

    // Check that MKL is supported.
    if (!args.A.tensor->SupportsOrder(ROW_MAJOR)) return false;
    if (!args.B.tensor->SupportsOrder(ROW_MAJOR)) return false;
    if (!args.C.tensor->SupportsOrder(ROW_MAJOR)) return false;
    if (!SupportsMKL()) return false;
    if (args.C.batchsize() != 1) return false;  // TODO: remove

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

    if (args.C.batchsize() == 1) {
      bool jitted = false;
      if (mkl_jit_support) {
        // Get jitter for MKL function.
        MKLJITStatus status;
        void *jitter;
        if (args.type == DT_FLOAT) {
          status = mkl_cblas_jit_create_sgemm(
              &jitter, MKL_ROW_MAJOR, args.A.op(), args.B.op(),
              m, n, k, 1.0, lda, ldb, 0.0, ldc);
        } else {
          status = mkl_cblas_jit_create_dgemm(
              &jitter, MKL_ROW_MAJOR, args.A.op(), args.B.op(),
              m, n, k, 1.0, lda, ldb, 0.0, ldc);
        }
        if (status == MKL_JIT_SUCCESS || status == MKL_NO_JIT) {
          // Get pointer to JIT function.
          gemm_jit_kernel_t kernel;
          if (args.type == DT_FLOAT) {
            kernel = mkl_jit_get_sgemm_ptr(jitter);
          } else {
            kernel = mkl_jit_get_dgemm_ptr(jitter);
          }

          // Generate call to JIT function.
        __ movp(arg_reg_1, jitter);
        __ LoadTensorAddress(arg_reg_2, args.A.tensor);
        __ LoadTensorAddress(arg_reg_3, args.B.tensor);
        __ LoadTensorAddress(arg_reg_4, args.C.tensor);
        __ call_extern(kernel, "");

          jitted = true;
          step->set_variant(status == MKL_NO_JIT ? "STDJIT" : "JIT");
          step->cell()->network()->AddResource(new MKLJitter(jitter));
        }
      }

      if (!jitted) {
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

        __ movq(arg_reg_1, Immediate(MKL_ROW_MAJOR));
        __ movq(arg_reg_2, Immediate(args.A.op()));
        __ movq(arg_reg_3, Immediate(args.B.op()));
        __ movq(arg_reg_4, Immediate(m));
        __ movq(arg_reg_5, Immediate(n));
        __ movq(arg_reg_6, Immediate(k));

        // Call MKL cblas_gemm.
        if (args.type == DT_FLOAT) {
          __ call_extern(cblas_sgemm, "cblas_sgemm");
        } else {
          __ call_extern(cblas_dgemm, "cblas_dgemm");
        }
        __ addq(rsp, Immediate(6 * 8));

        step->set_variant("STD");
      }
    } else {
      LOG(FATAL) << "MKL batch matmul not yet supported";
    }
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
    MKLTranspose op() const {
      return transpose ? MKL_TRANS : MKL_NO_TRANS;
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

  // Network resource for MKL jitter.
  struct MKLJitter : public Network::Resource {
    MKLJitter(void *jitter) : jitter(jitter) {}
    ~MKLJitter() override { mkl_jit_destroy(jitter); }
    void *jitter;
  };
};

void RegisterMKLLibrary(Library *library) {
  library->Register(new MKLMatMul());
}

}  // namespace myelin
}  // namespace sling

