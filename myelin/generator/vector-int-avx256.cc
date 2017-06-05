#include "myelin/generator/expression.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Generate vector int expression using AVX and YMM registers.
class VectorIntAVX256Generator : public ExpressionGenerator {
 public:
  VectorIntAVX256Generator() {
    model_.mov_reg_reg = true;
    model_.mov_reg_imm = true;
    model_.mov_reg_mem = true;
    model_.mov_mem_reg = true;
    model_.op_reg_reg_reg = true;
    model_.op_reg_reg_mem = true;
    model_.func_reg_reg = true;
    model_.func_reg_mem = true;
  }

  string Name() override { return "VectorIntAVX256"; }

  int VectorSize() override { return YMMRegSize; }

  void Reserve() override {
    // Reserve YMM registers for temps.
    index_->ReserveYMMRegisters(instructions_.NumRegs());

    // Allocate auxiliary registers.
    int num_mm_aux = 0;
    if (instructions_.Has(Expression::MUL) && type_ == DT_INT8) {
      num_mm_aux = std::max(num_mm_aux, 2);
    }
    index_->ReserveAuxYMMRegisters(num_mm_aux);
  }

  void Generate(Expression::Op *instr, MacroAssembler *masm) override {
    switch (instr->type) {
      case Expression::MOV:
        GenerateYMMVectorIntMove(instr, masm);
        break;
      case Expression::ADD:
        GenerateYMMIntOp(instr,
            &Assembler::vpaddb, &Assembler::vpaddb,
            &Assembler::vpaddw, &Assembler::vpaddw,
            &Assembler::vpaddd, &Assembler::vpaddd,
            &Assembler::vpaddq, &Assembler::vpaddq,
            masm);
        break;
      case Expression::SUB:
        GenerateYMMIntOp(instr,
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
              __ vmovdqa(ymmaux(1), ymm(instr->src2));
            } else {
              __ vmovdqa(ymmaux(1), addr(instr->args[1]));
            }

            // Multiply even bytes.
            __ vpmullw(ymm(instr->dst), ymm(instr->src), ymmaux(1));

            // Multiply odd bytes.
            __ vpsraw(ymmaux(0), ymm(instr->src), 8);
            __ vpsraw(ymmaux(1), ymmaux(1), 8);
            __ vpmullw(ymmaux(0), ymmaux(0), ymmaux(1));
            __ vpsllw(ymmaux(0), ymmaux(0), 8);

            // Combine even and odd results.
            __ vpcmpeqw(ymmaux(1), ymmaux(1), ymmaux(1));
            __ vpsrlw(ymmaux(1), ymmaux(1), 8);  // constant 8 times 0x00FF
            __ vpand(ymm(instr->dst), ymm(instr->dst), ymmaux(1));
            __ vpor(ymm(instr->dst), ymm(instr->dst), ymmaux(0));
            break;
          case DT_INT16:
          case DT_INT32:
            GenerateYMMIntOp(instr,
                &Assembler::vpmullw, &Assembler::vpmullw,  // dummy
                &Assembler::vpmullw, &Assembler::vpmullw,
                &Assembler::vpmulld, &Assembler::vpmulld,
                &Assembler::vpmulld, &Assembler::vpmulld,  // dummy
                masm);
            break;
          case DT_INT64:
            UNSUPPORTED;
            break;
          default:
            UNSUPPORTED;
        }
        break;
      case Expression::DIV:
        UNSUPPORTED;
        break;
      case Expression::MIN:
        if (type_ == DT_INT64) {
          UNSUPPORTED;
        } else {
          GenerateYMMIntOp(instr,
              &Assembler::vpminsb, &Assembler::vpminsb,
              &Assembler::vpminsw, &Assembler::vpminsw,
              &Assembler::vpminsd, &Assembler::vpminsd,
              &Assembler::vpminsd, &Assembler::vpminsd,
              masm);
        }
        break;
      case Expression::MAX:
        if (type_ == DT_INT64) {
          UNSUPPORTED;
        } else {
          GenerateYMMIntOp(instr,
              &Assembler::vpmaxsb, &Assembler::vpmaxsb,
              &Assembler::vpmaxsw, &Assembler::vpmaxsw,
              &Assembler::vpmaxsd, &Assembler::vpmaxsd,
              &Assembler::vpmaxsd, &Assembler::vpmaxsd,  // dummy
              masm);
        }
        break;
      case Expression::RELU:
        if (type_ == DT_INT64) {
          UNSUPPORTED;
        } else {
          __ vpxor(ymm(instr->src), ymm(instr->src), ymm(instr->src));
          GenerateYMMIntOp(instr,
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

ExpressionGenerator *CreateVectorIntAVX256Generator() {
  return new VectorIntAVX256Generator();
}

}  // namespace myelin
}  // namespace sling

