#include "myelin/generator/expression.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Generate vector float expression using SSE and XMM registers.
class VectorFltSSEGenerator : public ExpressionGenerator {
 public:
  VectorFltSSEGenerator() {
    model_.mov_reg_reg = true;
    model_.mov_reg_imm = true;
    model_.mov_reg_mem = true;
    model_.mov_mem_reg = true;
    model_.op_reg_reg = true;
    model_.op_reg_mem = true;
    model_.func_reg_reg = true;
    model_.func_reg_mem = true;
  }

  string Name() override { return "VectorFltSSE"; }

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
            &Assembler::addps, &Assembler::addpd,
            &Assembler::addps, &Assembler::addpd,
            masm);
        break;
      case Expression::SUB:
        GenerateXMMFltOp(instr,
            &Assembler::subps, &Assembler::subpd,
            &Assembler::subps, &Assembler::subpd,
            masm);
        break;
      case Expression::MUL:
        GenerateXMMFltOp(instr,
            &Assembler::mulps, &Assembler::mulpd,
            &Assembler::mulps, &Assembler::mulpd,
            masm);
        break;
      case Expression::DIV:
        GenerateXMMFltOp(instr,
            &Assembler::divps, &Assembler::divpd,
            &Assembler::divps, &Assembler::divpd,
            masm);
        break;
      case Expression::MIN:
        GenerateXMMFltOp(instr,
            &Assembler::minps, &Assembler::minpd,
            &Assembler::minps, &Assembler::minpd,
            masm);
        break;
      case Expression::MAX:
        GenerateXMMFltOp(instr,
            &Assembler::maxps, &Assembler::maxpd,
            &Assembler::maxps, &Assembler::maxpd,
            masm);
        break;
      case Expression::RELU:
        if (CPU::Enabled(SSE2)) {
          if (type_ == DT_FLOAT) {
            __ xorps(xmm(instr->dst), xmm(instr->dst));
          } else if (type_ == DT_DOUBLE) {
            __ xorpd(xmm(instr->dst), xmm(instr->dst));
          } else {
            UNSUPPORTED;
          }
        } else if (type_ == DT_FLOAT) {
          float zero = 0;
          auto *data = masm->CreateDataBlock(sizeof(float));
          data->Add(zero);
          __ movss(xmm(instr->dst), data->address());
        } else {
          UNSUPPORTED;
        }
        GenerateXMMFltOp(instr,
            &Assembler::maxps, &Assembler::maxpd,
            &Assembler::maxps, &Assembler::maxpd,
            masm);
        break;
      default: UNSUPPORTED;
    }
  }
};

ExpressionGenerator *CreateVectorFltSSEGenerator() {
  return new VectorFltSSEGenerator();
}

}  // namespace myelin
}  // namespace sling

