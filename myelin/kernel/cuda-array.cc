#include "myelin/kernel/cuda.h"

#include "myelin/cuda/cuda-kernel.h"

namespace sling {
namespace myelin {

// CUDA-based embedding lookup for single feature.
class CUDALookupSingle : public CUDAKernel {
 public:
  string Name() override { return "CUDALookupSingle"; }
  string Operation() override { return "Lookup"; }

  bool Supports(Step *step) override {
    // Requires CUDA support.
    if (!CUDAKernel::Supports(step)) return false;

    // Check inputs and outputs.
    if (step->indegree() != 2 || step->outdegree() != 1) return false;

    // Check types.
    Tensor *f = step->input(0);
    Tensor *M = step->input(1);
    Tensor *v = step->output(0);
    if (f->type() != DT_INT32 || f->elements() != 1) return false;
    if (M->type() != DT_FLOAT || M->rank() != 2) return false;
    if (v->type() != DT_FLOAT || v->rank() != 2) return false;
    if (v->dim(0) != 1 || v->dim(1) != M->dim(1)) return false;

    // Check that the output is not already a reference or a cell output.
    if (v->ref() || v->out()) return false;

    return true;
  }

  void Adjust(Step *step) override {
    // Make output a reference into the embedding matrix.
    Tensor *v = step->output(0);
    CHECK(!v->ref());
    CHECK(!v->out());
    v->set_ref(true);

    // Embedding matrix must be row-major.
    step->input(1)->SetRequiredOrder(ROW_MAJOR);
  }

  void GeneratePTX(Step *step, PTXMacroAssembler *ptx) override {
    // Get inputs and outputs.
    Tensor *f = step->input(0);
    Tensor *M = step->input(1);
    Tensor *v = step->output(0);

    // Get embedding size. The last element is the OOV element.
    int embedding_size = M->dim(0) - 1;

    // Set grid size.
    ptx->set_grid_dims(1);

    // Get feature index.
    ptx_decl(b64, fptr);
    ptx->LoadTensorAddress(fptr, f);
    ptx_decl(u32, fidx);
    ptx_emit(ld.global.u32, fidx, PTXAddr(fptr));

    // Use OOV for negative index.
    ptx_decl(pred, oov);
    ptx_emit(setp.eq.s32, oov, fidx, PTXImm(-1));
    ptx_if(oov);
    ptx_emit(mov.s32, fidx, PTXImm(embedding_size));
    ptx_endif();

    // Compute offset in embedding.
    ptx_decl(b64, ofs);
    ptx_emit(mul.wide.s32, ofs, fidx, PTXImm(M->stride(0)));

    // Lookup element in embedding.
    ptx_decl(b64, mptr);
    ptx->LoadTensorAddress(mptr, M);
    ptx_emit(add.u64, mptr, mptr, ofs);

    // Save reference to embedding vector.
    ptx_emit(st.global.b64, PTXAddr(ptx->data(), v->device_offset()), mptr);

    // Done.
    ptx_label(done);
    ptx_ret();
  }

  int64 Complexity(const Step *step) override {
    return 0;
  }
};

// Register CUDA array library.
void RegisterCUDAArrayLibrary(Library *library) {
  library->Register(new CUDALookupSingle());
}

}  // namespace myelin
}  // namespace sling

