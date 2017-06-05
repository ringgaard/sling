#include "myelin/generator/expression.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Generate vector float expression using AVX and XMM registers.
class VectorFltAVX128Generator : public ExpressionGenerator {
 public:
  VectorFltAVX128Generator() {
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

  string Name() override { return "VectorFltAVX128"; }

  int VectorSize() override { return XMMRegSize; }

  void Reserve() override {
    // Reserve XMM registers.
    index_->ReserveXMMRegisters(instructions_.NumRegs());
  }

  void Generate(Expression::Op *instr, MacroAssembler *masm) override {
    switch (instr->type) {
      case Expression::MOV:
        GenerateXMMVectorMove(instr, masm);
        break;
      case Expression::ADD:
        GenerateXMMFltOp(instr,
            &Assembler::vaddps, &Assembler::vaddpd,
            &Assembler::vaddps, &Assembler::vaddpd,
            masm);
        break;
      case Expression::SUB:
        GenerateXMMFltOp(instr,
            &Assembler::vsubps, &Assembler::vsubpd,
            &Assembler::vsubps, &Assembler::vsubpd,
            masm);
        break;
      case Expression::MUL:
        GenerateXMMFltOp(instr,
            &Assembler::vmulps, &Assembler::vmulpd,
            &Assembler::vmulps, &Assembler::vmulpd,
            masm);
        break;
      case Expression::DIV:
        GenerateXMMFltOp(instr,
            &Assembler::vdivps, &Assembler::vdivpd,
            &Assembler::vdivps, &Assembler::vdivpd,
            masm);
        break;
      case Expression::MIN:
        GenerateXMMFltOp(instr,
            &Assembler::vminps, &Assembler::vminpd,
            &Assembler::vminps, &Assembler::vminpd,
            masm);
        break;
      case Expression::MAX:
        GenerateXMMFltOp(instr,
            &Assembler::vmaxps, &Assembler::vmaxpd,
            &Assembler::vmaxps, &Assembler::vmaxpd,
            masm);
        break;
      case Expression::RELU:
        if (type_ == DT_FLOAT) {
          __ vxorps(xmm(instr->dst), xmm(instr->dst), xmm(instr->dst));
          if (instr->dst != -1 && instr->src != -1) {
            __ vmaxps(xmm(instr->dst), xmm(instr->dst), xmm(instr->src));
          } else if (instr->dst != -1 && instr->src == -1) {
            __ vmaxps(xmm(instr->dst), xmm(instr->dst), addr(instr->args[1]));
          } else {
            UNSUPPORTED;
          }
        } else if (type_ == DT_DOUBLE) {
          __ vxorpd(xmm(instr->dst), xmm(instr->dst), xmm(instr->dst));
          if (instr->dst != -1 && instr->src != -1) {
            __ vmaxpd(xmm(instr->dst), xmm(instr->dst), xmm(instr->src));
          } else if (instr->dst != -1 && instr->src == -1) {
            __ vmaxpd(xmm(instr->dst), xmm(instr->dst), addr(instr->args[1]));
          } else {
            UNSUPPORTED;
          }
        } else {
          UNSUPPORTED;
        }
        break;
      case Expression::MULADD132:
        GenerateXMMFltOp(instr,
            &Assembler::vfmadd132ps, &Assembler::vfmadd132pd,
            &Assembler::vfmadd132ps, &Assembler::vfmadd132pd,
            masm);
        break;
      case Expression::MULADD213:
        GenerateXMMFltOp(instr,
            &Assembler::vfmadd213ps, &Assembler::vfmadd213pd,
            &Assembler::vfmadd213ps, &Assembler::vfmadd213pd,
            masm);
        break;
      case Expression::MULADD231:
        GenerateXMMFltOp(instr,
            &Assembler::vfmadd231ps, &Assembler::vfmadd231pd,
            &Assembler::vfmadd231ps, &Assembler::vfmadd231pd,
            masm);
        break;
      case Expression::MULSUB132:
        GenerateXMMFltOp(instr,
            &Assembler::vfmsub132ps, &Assembler::vfmsub132pd,
            &Assembler::vfmsub132ps, &Assembler::vfmsub132pd,
            masm);
        break;
      case Expression::MULSUB213:
        GenerateXMMFltOp(instr,
            &Assembler::vfmsub213ps, &Assembler::vfmsub213pd,
            &Assembler::vfmsub213ps, &Assembler::vfmsub213pd,
            masm);
        break;
      case Expression::MULSUB231:
        GenerateXMMFltOp(instr,
            &Assembler::vfmsub231ps, &Assembler::vfmsub231pd,
            &Assembler::vfmsub231ps, &Assembler::vfmsub231pd,
            masm);
        break;
      default: UNSUPPORTED;
    }
  }
};

ExpressionGenerator *CreateVectorFltAVX128Generator() {
  return new VectorFltAVX128Generator();
}

}  // namespace myelin
}  // namespace sling

