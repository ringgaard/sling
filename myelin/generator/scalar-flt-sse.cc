#include "myelin/generator/expression.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Generate scalar float expression using SSE and XMM registers.
class ScalarFltSSEGenerator : public ExpressionGenerator {
 public:
  ScalarFltSSEGenerator() {
    model_.mov_reg_reg = true;
    model_.mov_reg_imm = true;
    model_.mov_reg_mem = true;
    model_.mov_mem_reg = true;
    model_.op_reg_reg = true;
    model_.op_reg_imm = true;
    model_.op_reg_mem = true;
    model_.func_reg_reg = true;
    model_.func_reg_imm = true;
    model_.func_reg_mem = true;
  }

  string Name() override { return "ScalarFltSSE"; }

  void Reserve() override {
    // Reserve XMM registers.
    index_->ReserveXMMRegisters(instructions_.NumRegs());
  }

  void Generate(Express::Op *instr, MacroAssembler *masm) override {
    switch (instr->type) {
      case Express::MOV:
        if (IsClear(instr)) {
          // Use XOR to zero register instead of loading constant from memory.
          switch (type_) {
            case DT_FLOAT:
              __ xorps(xmm(instr->dst), xmm(instr->dst));
              break;
            case DT_DOUBLE:
              __ xorpd(xmm(instr->dst), xmm(instr->dst));
              break;
            default: UNSUPPORTED;
          }
        } else {
          GenerateXMMScalarFltMove(instr, masm);
        }
        break;
      case Express::ADD:
        GenerateXMMFltOp(instr,
            &Assembler::addss, &Assembler::addsd,
            &Assembler::addss, &Assembler::addsd,
            masm);
        break;
      case Express::SUB:
        GenerateXMMFltOp(instr,
            &Assembler::subss, &Assembler::subsd,
            &Assembler::subss, &Assembler::subsd,
            masm);
        break;
      case Express::MUL:
        GenerateXMMFltOp(instr,
            &Assembler::mulss, &Assembler::mulsd,
            &Assembler::mulss, &Assembler::mulsd,
            masm);
        break;
      case Express::DIV:
        GenerateXMMFltOp(instr,
            &Assembler::divss, &Assembler::divsd,
            &Assembler::divss, &Assembler::divsd,
            masm);
        break;
      case Express::MIN:
        GenerateXMMFltOp(instr,
            &Assembler::minss, &Assembler::minsd,
            &Assembler::minss, &Assembler::minsd,
            masm);
        break;
      case Express::MAX:
        GenerateXMMFltOp(instr,
            &Assembler::maxss, &Assembler::maxsd,
            &Assembler::maxss, &Assembler::maxsd,
            masm);
        break;
      case Express::RELU:
        GenerateRelu(instr, masm);
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
        GenerateXMMFltOp(instr,
            &Assembler::andps, &Assembler::andpd,
            &Assembler::andps, &Assembler::andpd,
            masm);
        break;
      case Express::OR:
        GenerateXMMFltOp(instr,
            &Assembler::orps, &Assembler::orpd,
            &Assembler::orps, &Assembler::orpd,
            masm);
        break;
      case Express::ANDNOT:
        if (CPU::Enabled(SSE2)) {
          GenerateXMMFltOp(instr,
              &Assembler::andnps, &Assembler::andnpd,
              &Assembler::andnps, &Assembler::andnpd,
              masm);
        } else {
          UNSUPPORTED;
        }
        break;
      case Express::SHR23:
        GenerateShift(instr, masm, false, 23);
        break;
      case Express::SHL23:
        GenerateShift(instr, masm, true, 23);
        break;
      case Express::FLOOR:
        GenerateFloor(instr, masm);
        break;
      case Express::CVTFLTINT:
        GenerateFltToInt(instr, masm);
        break;
      default: UNSUPPORTED;
    }
  }

  // Generate relu(x) = max(0,x).
  void GenerateRelu(Express::Op *instr, MacroAssembler *masm) {
    if (type_ == DT_FLOAT) {
      __ xorps(xmm(instr->dst), xmm(instr->dst));
    } else if (type_ == DT_DOUBLE) {
      if (CPU::Enabled(SSE2)) {
        __ xorpd(xmm(instr->dst), xmm(instr->dst));
      } else {
        __ xorps(xmm(instr->dst), xmm(instr->dst));
      }
    } else {
      UNSUPPORTED;
    }
    GenerateXMMFltOp(instr,
        &Assembler::maxss, &Assembler::maxsd,
        &Assembler::maxss, &Assembler::maxsd,
        masm);
  }

  // Generate left/right shift.
  void GenerateShift(Express::Op *instr, MacroAssembler *masm,
                     int left, int bits) {
    // Move argument to destination register
    CHECK(instr->dst != -1);
    if (instr->src != -1) {
      __ movapd(xmm(instr->dst), xmm(instr->src));
    } else {
      switch (type_) {
        case DT_FLOAT:
          __ movaps(xmm(instr->dst), addr(instr->args[0]));
          break;
        case DT_DOUBLE:
          __ movapd(xmm(instr->dst), addr(instr->args[0]));
          break;
        default: UNSUPPORTED;
      }
    }

    // Shift xmm register.
    switch (type_) {
      case DT_FLOAT:
        if (CPU::Enabled(SSE2)) {
          if (left) {
            __ pslld(xmm(instr->dst), bits);
          } else {
            __ psrld(xmm(instr->dst), bits);
          }
        } else {
          UNSUPPORTED;
        }
        break;
      case DT_DOUBLE:
        if (CPU::Enabled(SSE2)) {
          if (left) {
            __ psllq(xmm(instr->dst), bits);
          } else {
            __ psrlq(xmm(instr->dst), bits);
          }
        } else {
          UNSUPPORTED;
        }
        break;
      default: UNSUPPORTED;
    }
  }

  // Generate floor rounding.
  void GenerateFloor(Express::Op *instr, MacroAssembler *masm) {
    if (CPU::Enabled(SSE4_1)) {
      GenerateXMMFltOp(instr,
          &Assembler::roundss, &Assembler::roundsd,
          &Assembler::roundss, &Assembler::roundsd,
          kRoundDown, masm);
    } else {
      UNSUPPORTED;
    }
  }

  // Generate float to integer conversion.
  void GenerateFltToInt(Express::Op *instr, MacroAssembler *masm) {
    if (CPU::Enabled(SSE2)) {
      GenerateXMMFltOp(instr,
          &Assembler::cvttps2dq, &Assembler::cvttpd2dq,
          &Assembler::cvttps2dq, &Assembler::cvttpd2dq,
          masm);
    } else {
      UNSUPPORTED;
    }
  }

  // Generate compare.
  void GenerateCompare(Express::Op *instr, MacroAssembler *masm, int8 code) {
    GenerateXMMFltOp(instr,
        &Assembler::cmpss, &Assembler::cmpsd,
        &Assembler::cmpss, &Assembler::cmpsd,
        code, masm);
  }
};

ExpressionGenerator *CreateScalarFltSSEGenerator() {
  return new ScalarFltSSEGenerator();
}

}  // namespace myelin
}  // namespace sling

