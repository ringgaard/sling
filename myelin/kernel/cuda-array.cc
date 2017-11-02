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

// CUDA-based embedding lookup for multiple features.
class CUDALookupMultiple : public CUDAKernel {
 public:
  string Name() override { return "CUDALookupMultiple"; }
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
    if (f->type() != DT_INT32) return false;
    if (M->type() != DT_FLOAT || M->rank() != 2) return false;
    if (v->type() != DT_FLOAT || v->rank() != 2) return false;
    if (v->dim(0) != 1 || v->dim(1) != M->dim(1)) return false;

    return true;
  }

  void Adjust(Step *step) override {
    // Embedding matrix must be row-major.
    step->input(1)->SetRequiredOrder(ROW_MAJOR);
  }

  void GeneratePTX(Step *step, PTXMacroAssembler *ptx) override {
    // Get inputs and outputs.
    Tensor *f = step->input(0);
    Tensor *M = step->input(1);
    Tensor *v = step->output(0);

    // Get embedding size and dimension. The last element is the OOV element.
    int embedding_size = M->dim(0) - 1;
    int embedding_dims = v->dim(1);

    // Get number of input features.
    int num_features = f->dim(1);

    // Use one thread for each element in the embedding.
    ptx->set_grid_dims(embedding_dims);

    // Get thread index.
    ptx_decl(b32, idx);
    ptx->GetThreadIndex(idx, 0);

    // Check bounds.
    ptx_decl(pred, outside);
    ptx_emit(setp.ge.u32, outside, idx, PTXImm(embedding_dims));
    ptx_if(outside);
    ptx_jump(done);
    ptx_endif();

    // Get embedding.
    ptx_decl(u64, embedding);
    ptx->LoadTensorAddress(embedding, M);
    ptx_emit(mad.wide.u32, embedding, idx, PTXImm(M->stride(1)), embedding);

    // Loop over input features.
    ptx_decl(f32, sum);
    ptx_emit(mov.f32, sum, PTXFloat(0));
    ptx_decl(b64, fptr);
    ptx->LoadTensorAddress(fptr, f);
    ptx_decl(u32, fidx);
    ptx_emit(mov.f32, fidx, PTXImm(0));
    ptx_label(loop1);

    // Get feature from feature vector.
    ptx_decl(u32, feature);
    ptx_emit(ld.global.u32, feature, PTXAddr(fptr));

    // Use OOV if feature value is -1.
    ptx_decl(pred, oov);
    ptx_emit(setp.eq.u32, oov, fidx, PTXImm(-1));
    ptx_if(oov);
    ptx_emit(mov.u32, fidx, PTXImm(embedding_size));
    ptx_endif();

    // Skip if feature value is -2.
    ptx_decl(pred, empty);
    ptx_emit(setp.eq.u32, empty, fidx, PTXImm(-2));
    ptx_if(oov);
    ptx_jump(skip);
    ptx_endif();

    // Add embedding for feature to sum.
    ptx_decl(b64, mptr);
    ptx_emit(mad.wide.u32, mptr, fidx, PTXImm(M->stride(0)), embedding);
    ptx_decl(f32, value);
    ptx_emit(ld.global.f32, value, PTXAddr(mptr));
    ptx_emit(add.f32, sum, sum, value);

    // Next feature.
    ptx_label(skip);
    ptx_emit(add.u32, fidx, fidx, PTXImm(1));
    ptx_emit(add.u64, fptr, fptr, PTXImm(sizeof(int)));
    ptx_decl(pred, more);
    ptx_emit(setp.lt.u32, more, fidx, PTXImm(num_features));
    ptx_if(more);
    ptx_jump(loop1);
    ptx_endif();

    // Save sum to output.
    ptx_decl(b64, vptr);
    ptx->LoadTensorAddress(vptr, v);
    ptx_emit(mad.wide.u32, vptr, idx, PTXImm(v->stride(1)), vptr);
    ptx_emit(st.global.f32, PTXAddr(vptr), sum);

    // Done.
    ptx_label(done);
    ptx_ret();
  }

  int64 Complexity(const Step *step) override {
    return step->input(0)->elements() * step->output(0)->elements();
  }
};

// Register CUDA array library.
void RegisterCUDAArrayLibrary(Library *library) {
  library->Register(new CUDALookupSingle());
  library->Register(new CUDALookupMultiple());
}

}  // namespace myelin
}  // namespace sling

