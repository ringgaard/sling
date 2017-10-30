#include "myelin/kernel/cuda.h"

#include <string>

#include "myelin/cuda/cuda-kernel.h"

namespace sling {
namespace myelin {

// Matrix multiplication using CUDA.
class CUDAMatMulBase : public CUDAKernel {
 public:
  // Maximum number of loop unrolls.
  static const int kMaxUnrolls = 8;

  CUDAMatMulBase(bool bias, bool relu) : bias_(bias), relu_(relu) {}

  bool Supports(Step *step) override {
    // Requires CUDA support.
    if (!CUDAKernel::Supports(step)) return false;

    // Two or three float 2D tensor inputs and one 2D tensor output.
    if (step->inputs().size() != (bias_ ? 3 : 2)) return false;
    if (step->outputs().size() != 1) return false;
    Tensor *A = step->input(0);
    Tensor *B = step->input(1);
    Tensor *C = step->output(0);
    if (A->rank() != 2 || B->rank() != 2 || C->rank() != 2) return false;

    // Check shape.
    if (A->dim(0) != C->dim(0)) return false;
    if (A->dim(1) != B->dim(0)) return false;
    if (B->dim(1) != C->dim(1)) return false;

    // Types must match and be supported by CUDA.
    Type type = A->type();
    if (TypeTraits::of(type).ptx() == nullptr) return false;
    if (B->type() != type || C->type() != type) return false;

    // Check order.
    if (!A->SupportsOrder(ROW_MAJOR)) return false;
    if (!B->SupportsOrder(COLUMN_MAJOR)) return false;
    if (!C->SupportsOrder(ROW_MAJOR)) return false;

    // Check bias vector.
    if (bias_) {
      Tensor *v = step->input(2);
      if (v->type() != type) return false;
      if (v->rank() == 1) {
        if (v->dim(0) != C->dim(1)) return false;
      } else if (v->rank() == 2) {
        if (v->dim(0) != 1 || v->dim(1) != C->dim(1)) return false;
      } else {
        return false;
      }
    }

    return true;
  }

  void Adjust(Step *step) override {
    // Get input and output tensors.
    Tensor *A = step->input(0);
    Tensor *B = step->input(1);
    Tensor *C = step->output(0);

    // Set order requirements.
    A->SetRequiredOrder(ROW_MAJOR);
    B->SetRequiredOrder(COLUMN_MAJOR);
    C->SetRequiredOrder(ROW_MAJOR);
  }

  void GeneratePTX(Step *step, PTXMacroAssembler *ptx) override {
    // Get input and output tensors.
    Tensor *A = step->input(0);
    Tensor *B = step->input(1);
    Tensor *v = bias_ ? step->input(2) : nullptr;
    Tensor *C = step->output(0);

    int width = C->dim(1);
    int height = C->dim(0);
    int depth = A->dim(1);

    Type dtype = A->type();
    const TypeTraits &traits = TypeTraits::of(dtype);
    const char *type = traits.ptx();
    bool fp = dtype == DT_FLOAT || dtype == DT_DOUBLE || dtype == DT_HALF;
    bool vec = height == 1;
    int dsize = traits.size();

    // Compute number of unrolls.
    int unrolls = 0;
    for (int i = 1; i <= kMaxUnrolls; ++i) {
      if (depth % i == 0) unrolls = i;
    }
    if (step->variant().empty()) {
      string variant = "U" + std::to_string(unrolls);
      step->set_variant(variant);
    }

    // Set grid size. Use one thread for each output element in C.
    ptx->set_grid_dims(width, height);

    // Get output row and column in C.
    ptx_decl(u32, col);
    ptx->GetThreadIndex(col, 0);
    ptx_decl(u32, row);
    if (!vec) {
      ptx->GetThreadIndex(row, 1);
    }

    // Check bounds.
    if (vec) {
      ptx_decl(pred, outside);
      ptx_emit(setp.ge.u32, outside, col, PTXImm(width));
      ptx_if(outside);
      ptx_emit(bra, PTXLabel("done"));
      ptx_endif();
    } else {
      ptx_decl(pred, outside_col);
      ptx_emit(setp.ge.u32, outside_col, col, PTXImm(width));
      ptx_decl(pred, outside_row);
      ptx_emit(setp.ge.u32, outside_row, row, PTXImm(height));
      ptx_decl(pred, outside);
      ptx_emit(or.pred, outside, outside_col, outside_row);
      ptx_if(outside);
      ptx_emit(bra, PTXLabel("done"));
      ptx_endif();
    }

    // Compute address of row in A.
    ptx_decl(b64, aptr);
    ptx->LoadTensorAddress(aptr, A);
    if (!vec) {
      ptx_emit(mad.wide.u32, aptr, row, PTXImm(A->stride(0)), aptr);
    }

    // Compute address of column in B.
    ptx_decl(b64, bptr);
    ptx->LoadTensorAddress(bptr, B);
    ptx_emit(mad.wide.u32, bptr, col, PTXImm(B->stride(1)), bptr);

    // Compute dot product.
    ptx_decl(u32, idx);
    ptx_emit(mov.u32, idx, PTXImm(0));
    PTXReg sum = ptx->reg(type, "sum");
    if (fp) {
      ptx->emit(PTXInstr("mov", type), sum, PTXFloat(0));
    } else {
      ptx->emit(PTXInstr("mov", type), sum, PTXImm(0));
    }
    ptx_label(loop);

    // Compute sum += A[row,idx] * B[idx,col].
    PTXReg a = ptx->reg(type, "a");
    PTXReg b = ptx->reg(type, "b");
    for (int i = 0; i < unrolls; ++i) {
      ptx->emit(PTXInstr("ld.global", type), a, PTXAddr(aptr, i * dsize));
      ptx->emit(PTXInstr("ld.global", type), b, PTXAddr(bptr, i * dsize));
      ptx->emit(PTXInstr(fp ? "fma.rn" : "mad.lo", type), sum, a, b, sum);
    }

    // Next element.
    if (unrolls != dsize) {
      ptx_emit(add.u32, idx, idx, PTXImm(unrolls));
      ptx_emit(add.u64, aptr, aptr, PTXImm(dsize * unrolls));
      ptx_emit(add.u64, bptr, bptr, PTXImm(dsize * unrolls));

      ptx_decl(pred, more);
      ptx_emit(setp.lt.u32, more, idx, PTXImm(depth));
      ptx_if(more);
      ptx_emit(bra, PTXLabel("loop"));
      ptx_endif();
    }

    // Compute output offset.
    ptx_decl(b64, ofs);
    ptx_emit(mul.wide.u32, ofs, col, PTXImm(dsize));

    // Optionally add bias.
    if (bias_) {
      ptx_decl(b64, vptr);
      ptx->LoadTensorAddress(vptr, v);
      ptx_emit(add.u64, vptr, ofs, vptr);

      PTXReg bias = ptx->reg(type, "bias");
      ptx->emit(PTXInstr("ld.global", type), bias, PTXAddr(vptr));
      ptx->emit(PTXInstr("add", type), sum, sum, bias);
    }

    // Optionally compute relu.
    if (relu_) {
      if (fp) {
        ptx->emit(PTXInstr("max", type), sum, sum, PTXFloat(0));
      } else {
        ptx->emit(PTXInstr("max", type), sum, sum, PTXImm(0));
      }
    }

    // Save result in C[row,col].
    ptx_decl(b64, cptr);
    ptx->LoadTensorAddress(cptr, C);
    if (!vec) {
      ptx_emit(mad.wide.u32, cptr, row, PTXImm(C->stride(0)), cptr);
    }
    ptx_emit(add.u64, cptr, ofs, cptr);
    ptx->emit(PTXInstr("st.global", type), PTXAddr(cptr), sum);

    // Done.
    ptx_label(done);
    ptx_ret();
  }

  int64 Complexity(const Step *step) override {
    int ops = step->input(0)->dim(0) * step->input(1)->elements() * 2;
    if (bias_) ops += step->input(2)->elements();
    if (relu_) ops += step->output(0)->elements();
    return ops;
  }

 protected:
  bool bias_;    // add bias vector to result, y=Wx+b
  bool relu_;    // apply rectified linear unit, y=max(0,Wx+b)
};

class CUDAMatMul : public CUDAMatMulBase {
 public:
  CUDAMatMul() : CUDAMatMulBase(false, false) {}

  string Name() override { return "CUDAMatMul"; }
  string Operation() override { return "MatMul"; }
};

class CUDAMatMulAdd : public CUDAMatMulBase {
 public:
  CUDAMatMulAdd() : CUDAMatMulBase(true, false) {}

  string Name() override { return "CUDAMatMulAdd"; }
  string Operation() override { return "MatMulAdd"; }
};

class CUDAMatMulRelu : public CUDAMatMulBase {
 public:
  CUDAMatMulRelu() : CUDAMatMulBase(false, true) {}

  string Name() override { return "CUDAMatMulRelu"; }
  string Operation() override { return "MatMulRelu"; }
};

class CUDAMatMulAddRelu : public CUDAMatMulBase {
 public:
  CUDAMatMulAddRelu() : CUDAMatMulBase(true, true) {}

  string Name() override { return "CUDAMatMulAddRelu"; }
  string Operation() override { return "MatMulAddRelu"; }
};

void RegisterCUDAMatMulLibrary(Library *library) {
  library->Register(new CUDAMatMul());
  library->Register(new CUDAMatMulAdd());
  library->Register(new CUDAMatMulRelu());
  library->Register(new CUDAMatMulAddRelu());
}

}  // namespace myelin
}  // namespace sling

