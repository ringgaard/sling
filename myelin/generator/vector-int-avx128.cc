#include "myelin/generator/expression.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Generate vector int expression using AVX and XMM registers.
class VectorIntAVX128Generator : public ExpressionGenerator {
 public:
  VectorIntAVX128Generator() {
    model_.mov_reg_reg = true;
    model_.mov_reg_imm = true;
    model_.mov_reg_mem = true;
    model_.mov_mem_reg = true;
    model_.op_reg_reg_reg = true;
    model_.op_reg_reg_mem = true;
    model_.func_reg_reg = true;
    model_.func_reg_mem = true;
  }

  string Name() override { return "VectorIntAVX128"; }

  int VectorSize() override { return XMMRegSize; }

  void Reserve() override {
    // Reserve XMM registers for temps.
    index_->ReserveXMMRegisters(instructions_.NumRegs());

    // Reserve auxiliary registers.
    int num_rr_aux = 0;
    int num_mm_aux = 0;
    if (instructions_.Has(Expression::MUL)) {
      if (type_ == DT_INT8) {
        num_mm_aux = std::max(num_mm_aux, 2);
      }
      if (type_ == DT_INT64) {
        num_rr_aux = std::max(num_rr_aux, 2);
        num_mm_aux = std::max(num_mm_aux, 1);
      }
    }
    if (instructions_.Has(Expression::MIN) ||
        instructions_.Has(Expression::MAX) ||
        instructions_.Has(Expression::RELU)) {
      if (type_ == DT_INT64) {
        num_rr_aux = std::max(num_rr_aux, 2);
      }
    }
    index_->ReserveAuxRegisters(num_rr_aux);
    index_->ReserveAuxXMMRegisters(num_mm_aux);
  }

  void Generate(Expression::Op *instr, MacroAssembler *masm) override {
    switch (instr->type) {
      case Expression::MOV:
        GenerateXMMVectorIntMove(instr, masm);
        break;
      case Expression::ADD:
        GenerateXMMIntOp(instr,
            &Assembler::vpaddb, &Assembler::vpaddb,
            &Assembler::vpaddw, &Assembler::vpaddw,
            &Assembler::vpaddd, &Assembler::vpaddd,
            &Assembler::vpaddq, &Assembler::vpaddq,
            masm);
        break;
      case Expression::SUB:
        GenerateXMMIntOp(instr,
            &Assembler::vpsubb, &Assembler::vpsubb,
            &Assembler::vpsubw, &Assembler::vpsubw,
            &Assembler::vpsubd, &Assembler::vpsubd,
            &Assembler::vpsubq, &Assembler::vpsubq,
            masm);
        break;
      case Expression::MUL:
        switch (type_) {
          case DT_INT8:
            // Multiply even and odd bytes and merge results.
            // See https://stackoverflow.com/a/29155682 for the details.
            // First load operands.
            CHECK(instr->dst != -1);
            CHECK(instr->src != -1);
            if (instr->src2 != -1) {
              __ vmovdqa(xmmaux(1), xmm(instr->src2));
            } else {
              __ vmovdqa(xmmaux(1), addr(instr->args[1]));
            }

            // Multiply even bytes.
            __ vpmullw(xmm(instr->dst), xmm(instr->src), xmmaux(1));

            // Multiply odd bytes.
            __ vpsraw(xmmaux(0), xmm(instr->src), 8);
            __ vpsraw(xmmaux(1), xmmaux(1), 8);
            __ vpmullw(xmmaux(0), xmmaux(0), xmmaux(1));
            __ vpsllw(xmmaux(0), xmmaux(0), 8);

            // Combine even and odd results.
            __ vpcmpeqw(xmmaux(1), xmmaux(1), xmmaux(1));
            __ vpsrlw(xmmaux(1), xmmaux(1), 8);  // constant 8 times 0x00FF
            __ vpand(xmm(instr->dst), xmm(instr->dst), xmmaux(1));
            __ vpor(xmm(instr->dst), xmm(instr->dst), xmmaux(0));
            break;
          case DT_INT16:
          case DT_INT32:
            GenerateXMMIntOp(instr,
                &Assembler::vpmullw, &Assembler::vpmullw,  // dummy
                &Assembler::vpmullw, &Assembler::vpmullw,
                &Assembler::vpmulld, &Assembler::vpmulld,
                &Assembler::vpmulld, &Assembler::vpmulld,  // dummy
                masm);
            break;
          case DT_INT64: {
            // Multiply each XMM element using x86 multiply.
            CHECK(instr->dst != -1);
            CHECK(instr->src != -1);
            XMMRegister src2;
            if (instr->src2 != -1) {
              src2 = xmm(instr->src2);
            } else {
              src2 = xmmaux(0);
              __ vmovdqa(src2, addr(instr->args[1]));
            }
            for (int n = 0; n < 2; ++n) {
              __ vpextrq(aux(0), xmm(instr->src), n);
              __ vpextrq(aux(1), src2, n);
              __ imulq(aux(0), aux(1));
              __ vpinsrq(xmm(instr->dst), xmm(instr->dst), aux(0), n);
            }
            break;
          }
          default:
            UNSUPPORTED;
        }
        break;
      case Expression::DIV:
        UNSUPPORTED;
        break;
      case Expression::MIN:
        if (type_ == DT_INT64) {
          CHECK(instr->dst != -1);
          CHECK(instr->src != -1);
          XMMRegister src2;
          if (instr->src2 != -1) {
            src2 = xmm(instr->src2);
          } else {
            src2 = xmm(instr->dst);
            __ vmovdqa(src2, addr(instr->args[1]));
          }
          for (int n = 0; n < 2; ++n) {
            __ vpextrq(aux(0), xmm(instr->src), n);
            __ vpextrq(aux(1), src2, n);
            __ cmpq(aux(0), aux(1));
            __ cmovq(greater, aux(0), aux(1));
            __ vpinsrq(xmm(instr->dst), xmm(instr->dst), aux(0), n);
          }
        } else {
          GenerateXMMIntOp(instr,
              &Assembler::vpminsb, &Assembler::vpminsb,
              &Assembler::vpminsw, &Assembler::vpminsw,
              &Assembler::vpminsd, &Assembler::vpminsd,
              &Assembler::vpminsd, &Assembler::vpminsd,
              masm);
        }
        break;
      case Expression::MAX:
        if (type_ == DT_INT64) {
          CHECK(instr->dst != -1);
          CHECK(instr->src != -1);
          XMMRegister src2;
          if (instr->src2 != -1) {
            src2 = xmm(instr->src2);
          } else {
            src2 = xmm(instr->dst);
            __ vmovdqa(src2, addr(instr->args[1]));
          }
          for (int n = 0; n < 2; ++n) {
            __ vpextrq(aux(0), xmm(instr->src), n);
            __ vpextrq(aux(1), src2, n);
            __ cmpq(aux(0), aux(1));
            __ cmovq(less, aux(0), aux(1));
            __ vpinsrq(xmm(instr->dst), xmm(instr->dst), aux(0), n);
          }
        } else {
          GenerateXMMIntOp(instr,
              &Assembler::vpmaxsb, &Assembler::vpmaxsb,
              &Assembler::vpmaxsw, &Assembler::vpmaxsw,
              &Assembler::vpmaxsd, &Assembler::vpmaxsd,
              &Assembler::vpmaxsd, &Assembler::vpmaxsd,  // dummy
              masm);
        }
        break;
      case Expression::RELU:
        if (type_ == DT_INT64) {
          CHECK(instr->dst != -1);
          XMMRegister src;
          if (instr->src != -1) {
            src = xmm(instr->src);
          } else {
            src = xmm(instr->dst);
            __ vmovdqa(src, addr(instr->args[1]));
          }
          Register zero = aux(1);
          __ xorq(zero, zero);
          for (int n = 0; n < 2; ++n) {
            __ vpextrq(aux(0), src, n);
            __ testq(aux(0), aux(0));
            __ cmovq(positive, aux(0), zero);
            __ vpinsrq(xmm(instr->dst), xmm(instr->dst), aux(0), n);
          }
        } else {
          __ vpxor(xmm(instr->src), xmm(instr->src), xmm(instr->src));
          GenerateXMMIntOp(instr,
              &Assembler::vpmaxsb, &Assembler::vpmaxsb,
              &Assembler::vpmaxsw, &Assembler::vpmaxsw,
              &Assembler::vpmaxsd, &Assembler::vpmaxsd,
              &Assembler::vpmaxsd, &Assembler::vpmaxsd,  // dummy
              masm, 0);
        }
        break;
      default: UNSUPPORTED;
    }
  }
};

ExpressionGenerator *CreateVectorIntAVX128Generator() {
  return new VectorIntAVX128Generator();
}

}  // namespace myelin
}  // namespace sling

