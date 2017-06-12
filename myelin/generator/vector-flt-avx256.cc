#include "myelin/generator/expression.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Generate vector float expression using AVX and YMM registers.
class VectorFltAVX256Generator : public ExpressionGenerator {
 public:
  VectorFltAVX256Generator() {
    model_.mov_reg_reg = true;
    model_.mov_reg_imm = true;
    model_.mov_reg_mem = true;
    model_.mov_mem_reg = true;
    model_.op_reg_reg_reg = true;
    model_.op_reg_reg_imm = true;
    model_.op_reg_reg_mem = true;
    model_.func_reg_reg = true;
    model_.func_reg_imm = true;
    model_.func_reg_mem = true;
    if (CPU::Enabled(FMA3)) {
      model_.fm_reg_reg_reg = true;
      model_.fm_reg_reg_imm = true;
      model_.fm_reg_reg_mem = true;
    }
  }

  string Name() override { return "VectorFltAVX256"; }

  int VectorSize() override { return YMMRegSize; }

  void Reserve() override {
    // Reserve YMM registers.
    index_->ReserveYMMRegisters(instructions_.NumRegs());

    // Allocate auxiliary registers.
    int num_mm_aux = 0;
    if (!CPU::Enabled(AVX2) &&
        (instructions_.Has(Express::SHR23) ||
         instructions_.Has(Express::SHL23))) {
      num_mm_aux = std::max(num_mm_aux, 1);
    }
    index_->ReserveAuxYMMRegisters(num_mm_aux);
  }

  void Generate(Express::Op *instr, MacroAssembler *masm) override {
    switch (instr->type) {
      case Express::MOV:
        GenerateYMMVectorMove(instr, masm);
        break;
      case Express::ADD:
        GenerateYMMFltOp(instr,
            &Assembler::vaddps, &Assembler::vaddpd,
            &Assembler::vaddps, &Assembler::vaddpd,
            masm);
        break;
      case Express::SUB:
        GenerateYMMFltOp(instr,
            &Assembler::vsubps, &Assembler::vsubpd,
            &Assembler::vsubps, &Assembler::vsubpd,
            masm);
        break;
      case Express::MUL:
        GenerateYMMFltOp(instr,
            &Assembler::vmulps, &Assembler::vmulpd,
            &Assembler::vmulps, &Assembler::vmulpd,
            masm);
        break;
      case Express::DIV:
        GenerateYMMFltOp(instr,
            &Assembler::vdivps, &Assembler::vdivpd,
            &Assembler::vdivps, &Assembler::vdivpd,
            masm);
        break;
      case Express::MIN:
        GenerateYMMFltOp(instr,
            &Assembler::vminps, &Assembler::vminpd,
            &Assembler::vminps, &Assembler::vminpd,
            masm);
        break;
      case Express::MAX:
        GenerateYMMFltOp(instr,
            &Assembler::vmaxps, &Assembler::vmaxpd,
            &Assembler::vmaxps, &Assembler::vmaxpd,
            masm);
        break;
      case Express::RELU:
        GenerateRelu(instr, masm);
        break;
      case Express::MULADD132:
        GenerateYMMFltOp(instr,
            &Assembler::vfmadd132ps, &Assembler::vfmadd132pd,
            &Assembler::vfmadd132ps, &Assembler::vfmadd132pd,
            masm, 2);
        break;
      case Express::MULADD213:
        GenerateYMMFltOp(instr,
            &Assembler::vfmadd213ps, &Assembler::vfmadd213pd,
            &Assembler::vfmadd213ps, &Assembler::vfmadd213pd,
            masm, 2);
        break;
      case Express::MULADD231:
        GenerateYMMFltOp(instr,
            &Assembler::vfmadd231ps, &Assembler::vfmadd231pd,
            &Assembler::vfmadd231ps, &Assembler::vfmadd231pd,
            masm, 2);
        break;
      case Express::MULSUB132:
        GenerateYMMFltOp(instr,
            &Assembler::vfmsub132ps, &Assembler::vfmsub132pd,
            &Assembler::vfmsub132ps, &Assembler::vfmsub132pd,
            masm, 2);
        break;
      case Express::MULSUB213:
        GenerateYMMFltOp(instr,
            &Assembler::vfmsub213ps, &Assembler::vfmsub213pd,
            &Assembler::vfmsub213ps, &Assembler::vfmsub213pd,
            masm, 2);
        break;
      case Express::MULSUB231:
        GenerateYMMFltOp(instr,
            &Assembler::vfmsub231ps, &Assembler::vfmsub231pd,
            &Assembler::vfmsub231ps, &Assembler::vfmsub231pd,
            masm, 2);
        break;
      case Express::CMPEQOQ:
        GenerateCompare(instr, masm, 0);
        break;
      case Express::CMPLTOQ:
        GenerateCompare(instr, masm, 17);
        break;
      case Express::CMPGTOQ:
        GenerateCompare(instr, masm, 30);
        break;
      case Express::CMPNGEUQ:
        GenerateCompare(instr, masm, 4);
        break;
      case Express::AND:
        GenerateYMMFltOp(instr,
            &Assembler::vandps, &Assembler::vandpd,
            &Assembler::vandps, &Assembler::vandpd,
            masm);
        break;
      case Express::OR:
        GenerateYMMFltOp(instr,
            &Assembler::vorps, &Assembler::vorpd,
            &Assembler::vorps, &Assembler::vorpd,
            masm);
        break;
      case Express::ANDNOT:
        GenerateYMMFltOp(instr,
            &Assembler::vandnps, &Assembler::vandnpd,
            &Assembler::vandnps, &Assembler::vandnpd,
            masm);
        break;
      case Express::SHR23:
        GenerateShift(instr, masm, false, 23);
        break;
      case Express::SHL23:
        GenerateShift(instr, masm, true, 23);
        break;
      default:
        UNSUPPORTED;
    }
  }

  // Generate relu.
  void GenerateRelu(Express::Op *instr, MacroAssembler *masm) {
    if (type_ == DT_FLOAT) {
      __ vxorps(ymm(instr->dst), ymm(instr->dst), ymm(instr->dst));
      if (instr->dst != -1 && instr->src != -1) {
        __ vmaxps(ymm(instr->dst), ymm(instr->dst), ymm(instr->src));
      } else if (instr->dst != -1 && instr->src == -1) {
        __ vmaxps(ymm(instr->dst), ymm(instr->dst), addr(instr->args[1]));
      } else {
        UNSUPPORTED;
      }
    } else if (type_ == DT_DOUBLE) {
      __ vxorpd(ymm(instr->dst), ymm(instr->dst), ymm(instr->dst));
      if (instr->dst != -1 && instr->src != -1) {
        __ vmaxpd(ymm(instr->dst), ymm(instr->dst), ymm(instr->src));
      } else if (instr->dst != -1 && instr->src == -1) {
        __ vmaxpd(ymm(instr->dst), ymm(instr->dst), addr(instr->args[1]));
      } else {
        UNSUPPORTED;
      }
    } else {
      UNSUPPORTED;
    }
  }

  // Generate left/right shift.
  void GenerateShift(Express::Op *instr, MacroAssembler *masm,
                     int left, int bits) {
    // Make sure source is in a register.
    CHECK(instr->dst != -1);
    int src = instr->src;
    if (instr->src == -1) {
      switch (type_) {
        case DT_FLOAT:
          __ vmovaps(ymm(instr->dst), addr(instr->args[0]));
          break;
        case DT_DOUBLE:
          __ vmovapd(ymm(instr->dst), addr(instr->args[0]));
          break;
        default: UNSUPPORTED;
      }
      src = instr->dst;
    }

    if (CPU::Enabled(AVX2)) {
      // Shift ymm register.
      switch (type_) {
        case DT_FLOAT:
          if (left) {
            __ vpslld(ymm(instr->dst), ymm(src), bits);
          } else {
            __ vpsrld(ymm(instr->dst), ymm(src), bits);
          }
          break;
        case DT_DOUBLE:
          if (left) {
            __ vpsllq(ymm(instr->dst), ymm(src), bits);
          } else {
            __ vpsrlq(ymm(instr->dst), ymm(src), bits);
          }
          break;
        default: UNSUPPORTED;
      }
    } else {
      // Shift ymm register by shifting lo and hi xmm registers.
      __ vextractf128(xmmaux(0), ymm(src), 1);
      if (left) {
        __ vpslld(xmmaux(0), xmmaux(0), bits);
        __ vpslld(xmm(instr->dst), xmm(src), bits);
      } else {
        __ vpsrld(xmmaux(0), xmmaux(0), bits);
        __ vpsrld(xmm(instr->dst), xmm(src), bits);
      }
      __ vinsertf128(ymm(instr->dst), ymm(instr->dst), xmmaux(0), 1);
    }
  }

  // Generate compare.
  void GenerateCompare(Express::Op *instr, MacroAssembler *masm, int8 code) {
    GenerateYMMFltOp(instr,
        &Assembler::vcmpps, &Assembler::vcmppd,
        &Assembler::vcmpps, &Assembler::vcmppd,
        code, masm);
  }
};

ExpressionGenerator *CreateVectorFltAVX256Generator() {
  return new VectorFltAVX256Generator();
}

}  // namespace myelin
}  // namespace sling

