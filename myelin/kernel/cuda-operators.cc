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
    if (a->elements() != c->elements()) return false;
    if (b->elements() != c->elements()) return false;

    return true;
  }

  void GeneratePTX(Step *step, PTXMacroAssembler *ptx) override {
    // Get input and output tensors.
    Tensor *a = step->input(0);
    Tensor *b = step->input(1);
    Tensor *c = step->output(0);
    int n = a->elements();

    // Set grid size. Use one thread for each element.
    ptx->set_grid_dim(0, n);

    // Get grid location.
    ptx_decl(b32, blkdim);
    ptx_decl(b32, blkidx);
    ptx_decl(b32, thridx);
    ptx_emit(mov.u32, blkdim, PTXLiteral("%ntid.x"));
    ptx_emit(mov.u32, thridx, PTXLiteral("%ctaid.x"));
    ptx_emit(mov.u32, blkidx, PTXLiteral("%tid.x"));
    ptx_decl(b32, idx);
    ptx_emit(mad_lo_s32, idx, thridx, blkdim, blkidx);

    // Check bounds.
    ptx_decl(pred, outside);
    ptx_emit(setp_ge_s32, outside, idx, PTXImm(n));
    ptx_if(outside);
    ptx_emit(bra, PTXLabel("done"));
    ptx_endif();

    // Compute element address.
    ptx_decl(u64, addr);
    ptx_emit(mad_wide_s32, addr, idx, PTXImm(sizeof(float)), ptx->data());

    // Read value from a.
    ptx_decl(f32, aval);
    if (a->IsConstant()) {
      ptx_decl(u64, aptr);
      ptx_emit(mad_wide_s32, aptr, idx, PTXImm(sizeof(float)),
               PTXImm(a->device_data()));
      ptx_emit(ld_global_f32, aval, PTXAddr(aptr));
    } else {
      ptx_emit(ld_global_f32, aval, PTXAddr(addr, a->device_offset()));
    }

    // Read value from b.
    ptx_decl(f32, bval);
    if (b->IsConstant()) {
      ptx_decl(u64, bptr);
      ptx_emit(mad_wide_s32, bptr, idx, PTXImm(sizeof(float)),
               PTXImm(b->device_data()));
      ptx_emit(ld_global_f32, bval, PTXAddr(bptr));
    } else {
      ptx_emit(ld_global_f32, bval, PTXAddr(addr, b->device_offset()));
    }

    // Compute c = f(a, b).
    ptx_decl(f32, cval);
    switch (op_) {
      case ADD:
        ptx_emit(add_f32, cval, aval, bval);
        break;

      case SUB:
        ptx_emit(sub_f32, cval, aval, bval);
        break;

      case MUL:
        ptx_emit(mul_f32, cval, aval, bval);
        break;
    }

    // Store result in c.
    ptx_emit(st_global_f32, PTXAddr(addr, c->device_offset()), cval);

    // Done.
    ptx_label(done);
    ptx_ret();
  }

  int64 Complexity(const Step *step) override {
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
    if (a->type() != DT_INT16 &&
        a->type() != DT_INT32 &&
        a->type() != DT_INT64) {
      return false;
    }
    if (b->type() != a->type()) return false;
    if (c->type() != a->type()) return false;

    // Input and output must have same number of elements.
    if (a->shape().elements() != c->shape().elements()) return false;
    if (b->shape().elements() != c->shape().elements()) return false;

    return true;
  }

  void GeneratePTX(Step *step, PTXMacroAssembler *ptx) override {
    // Get input and output tensors.
    Tensor *a = step->input(0);
    Tensor *b = step->input(1);
    Tensor *c = step->output(0);
    int n = a->elements();
    Type type = a->type();
    const TypeTraits &traits = TypeTraits::of(type);

    // Set grid size. Use one thread for each element.
    ptx->set_grid_dim(0, n);

    // Get grid location.
    ptx_decl(b32, blkdim);
    ptx_decl(b32, blkidx);
    ptx_decl(b32, thridx);
    ptx_emit(mov.u32, blkdim, PTXLiteral("%ntid.x"));
    ptx_emit(mov.u32, thridx, PTXLiteral("%ctaid.x"));
    ptx_emit(mov.u32, blkidx, PTXLiteral("%tid.x"));
    ptx_decl(b32, idx);
    ptx_emit(mad_lo_s32, idx, thridx, blkdim, blkidx);

    // Check bounds.
    ptx_decl(pred, outside);
    ptx_emit(setp_ge_s32, outside, idx, PTXImm(n));
    ptx_if(outside);
    ptx_emit(bra, PTXLabel("done"));
    ptx_endif();

    // Compute element address.
    ptx_decl(u64, addr);
    ptx_emit(mad_wide_s32, addr, idx, PTXImm(traits.size()), ptx->data());

    // Read value from a.
    PTXReg aval = ptx->reg(traits.ptx(), "aval");
    if (a->IsConstant()) {
      ptx_decl(u64, aptr);
      ptx_emit(mad_wide_s32, aptr, idx, PTXImm(sizeof(float)),
               PTXImm(a->device_data()));
      ptx->emit(PTXInstr("ld_global", traits.ptx()), aval, PTXAddr(aptr));
    } else {
      ptx->emit(PTXInstr("ld_global", traits.ptx()), aval,
                PTXAddr(addr, a->device_offset()));
    }

    // Read value from b.
    PTXReg bval = ptx->reg(traits.ptx(), "bval");
    if (b->IsConstant()) {
      ptx_decl(u64, bptr);
      ptx_emit(mad_wide_s32, bptr, idx, PTXImm(sizeof(float)),
               PTXImm(b->device_data()));
      ptx->emit(PTXInstr("ld_global", traits.ptx()), bval, PTXAddr(bptr));
    } else {
      ptx->emit(PTXInstr("ld_global", traits.ptx()), bval,
                PTXAddr(addr, b->device_offset()));
    }

    // Compute c = f(a, b).
    PTXReg cval = ptx->reg(traits.ptx(), "cval");
    switch (op_) {
      case ADD:
        ptx->emit(PTXInstr("add", traits.ptx()), cval, aval, bval);
        break;

      case SUB:
        ptx->emit(PTXInstr("sub", traits.ptx()), cval, aval, bval);
        break;

      case MUL:
        ptx->emit(PTXInstr("mul", traits.ptx()), cval, aval, bval);
        break;
    }

    // Store result in c.
    ptx->emit(PTXInstr("st_global", traits.ptx()),
              PTXAddr(addr, c->device_offset()), cval);

    // Done.
    ptx_label(done);
    ptx_ret();
  }

  int64 Complexity(const Step *step) override {
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

// Element-wise integer multiply using CUDA.
class CUDAIntMul : public CUDAIntBinaryOperator {
 public:
  CUDAIntMul() : CUDAIntBinaryOperator(SUB) {}
  string Name() override { return "CUDAIntMul"; }
  string Operation() override { return "Mul"; }
};

void RegisterCUDAOperatorLibrary(Library *library) {
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
  // Input     : a: int16/32/64[d1,...,dn]
  //             b: int16/32/64[d1,...,dn]
  // Output    : c: int16/32/64[d1,...,dn]
  // Requires  : CUDA
  library->Register(new CUDAIntAdd());

  // Computes  : c = a - b element-wise
  // Input     : a: int16/32/64[d1,...,dn]
  //             b: int16/32/64[d1,...,dn]
  // Output    : c: int16/32/64[d1,...,dn]
  // Requires  : CUDA
  library->Register(new CUDAIntSub());

  // Computes  : c = a * b element-wise
  // Input     : a: int16/32/64[d1,...,dn]
  //             b: int16/32/64[d1,...,dn]
  // Output    : c: int16/32/64[d1,...,dn]
  // Requires  : CUDA
  library->Register(new CUDAIntMul());
}

}  // namespace myelin
}  // namespace sling

