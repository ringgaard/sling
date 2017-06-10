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
    model_.op_reg_reg_mem = true;
    model_.func_reg_reg = true;
    model_.func_reg_mem = true;
    if (CPU::Enabled(FMA3)) {
      model_.fm_reg_reg_reg = true;
      model_.fm_reg_reg_mem = true;
    }
  }

  string Name() override { return "VectorFltAVX256"; }

  int VectorSize() override { return YMMRegSize; }

  void Reserve() override {
    // Reserve YMM registers.
    index_->ReserveYMMRegisters(instructions_.NumRegs());
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
            masm);
        break;
      case Express::MULADD213:
        GenerateYMMFltOp(instr,
            &Assembler::vfmadd213ps, &Assembler::vfmadd213pd,
            &Assembler::vfmadd213ps, &Assembler::vfmadd213pd,
            masm);
        break;
      case Express::MULADD231:
        GenerateYMMFltOp(instr,
            &Assembler::vfmadd231ps, &Assembler::vfmadd231pd,
            &Assembler::vfmadd231ps, &Assembler::vfmadd231pd,
            masm);
        break;
      case Express::MULSUB132:
        GenerateYMMFltOp(instr,
            &Assembler::vfmsub132ps, &Assembler::vfmsub132pd,
            &Assembler::vfmsub132ps, &Assembler::vfmsub132pd,
            masm);
        break;
      case Express::MULSUB213:
        GenerateYMMFltOp(instr,
            &Assembler::vfmsub213ps, &Assembler::vfmsub213pd,
            &Assembler::vfmsub213ps, &Assembler::vfmsub213pd,
            masm);
        break;
      case Express::MULSUB231:
        GenerateYMMFltOp(instr,
            &Assembler::vfmsub231ps, &Assembler::vfmsub231pd,
            &Assembler::vfmsub231ps, &Assembler::vfmsub231pd,
            masm);
        break;
      default: UNSUPPORTED;
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
};

ExpressionGenerator *CreateVectorFltAVX256Generator() {
  return new VectorFltAVX256Generator();
}

}  // namespace myelin
}  // namespace sling

