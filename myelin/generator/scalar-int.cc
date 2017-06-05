#include "myelin/generator/expression.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Generate scalar int expression using x64 registers.
class ScalarIntGenerator : public ExpressionGenerator {
 public:
  ScalarIntGenerator() {
    model_.mov_reg_reg = true;
    model_.mov_reg_imm = true;
    model_.mov_reg_mem = true;
    model_.mov_mem_reg = true;
    model_.op_reg_reg = true;
    model_.op_reg_mem = true;
    model_.func_reg_reg = true;
    model_.func_reg_mem = true;
  }

  string Name() override { return "ScalarInt"; }

  void Reserve() override {
    // Reserve registers for temps.
    index_->ReserveRegisters(instructions_.NumRegs());

    if (instructions_.Has(Expression::DIV)) {
      // Reserve rax and rdx for integer division.
      index_->ReserveFixedRegister(rax);
      index_->ReserveFixedRegister(rdx);
    } else if (instructions_.Has(Expression::MUL) && type_ == DT_INT8) {
      // Reserve al for int8 multiplication.
      index_->ReserveFixedRegister(rax);
    } else if (instructions_.Has(Expression::MIN) ||
               instructions_.Has(Expression::MAX) ||
               instructions_.Has(Expression::RELU)) {
      // Reserve rax for as aux register.
      index_->ReserveFixedRegister(rax);
    }
  }

  void Generate(Expression::Op *instr, MacroAssembler *masm) override {
    switch (instr->type) {
      case Expression::MOV:
        GenerateScalarIntMove(instr, masm);
        break;
      case Expression::ADD:
        GenerateIntBinaryOp(instr,
            &Assembler::addb, &Assembler::addb,
            &Assembler::addw, &Assembler::addw,
            &Assembler::addl, &Assembler::addl,
            &Assembler::addq, &Assembler::addq,
            masm);
        break;
      case Expression::SUB:
        GenerateIntBinaryOp(instr,
            &Assembler::subb, &Assembler::subb,
            &Assembler::subw, &Assembler::subw,
            &Assembler::subl, &Assembler::subl,
            &Assembler::subq, &Assembler::subq,
            masm);
        break;
      case Expression::MUL:
        if (type_ == DT_INT8) {
          CHECK(instr->dst != -1);
          __ movq(rax, reg(instr->dst));
          if (instr->src != -1) {
            __ imulb(reg(instr->src));
          } else {
            __ imulb(addr(instr->args[1]));
          }
          __ movq(reg(instr->dst), rax);
        } else {
          GenerateIntBinaryOp(instr,
              &Assembler::imulw, &Assembler::imulw,  // dummy
              &Assembler::imulw, &Assembler::imulw,
              &Assembler::imull, &Assembler::imull,
              &Assembler::imulq, &Assembler::imulq,
              masm);
        }
        break;
      case Expression::DIV:
        CHECK(instr->dst != -1);
        __ movq(rax, reg(instr->dst));
        if (type_ != DT_INT8) {
          __ xorq(rdx, rdx);
        }
        GenerateIntUnaryOp(instr,
            &Assembler::idivb, &Assembler::idivb,
            &Assembler::idivw, &Assembler::idivw,
            &Assembler::idivl, &Assembler::idivl,
            &Assembler::idivq, &Assembler::idivq,
            masm, 1);
        __ movq(reg(instr->dst), rax);
        break;
      case Expression::MIN:
      case Expression::MAX:
      case Expression::RELU:
        CHECK(instr->dst != -1);
        if (instr->type == Expression::RELU) {
          __ xorq(rax, rax);
        } else if (instr->src != -1) {
          __ movq(rax, reg(instr->src));
        } else {
          GenerateIntMoveMemToReg(rax, addr(instr->args[1]), masm);
        }
        switch (type_) {
          case DT_INT8: __ cmpb(rax, reg(instr->dst)); break;
          case DT_INT16: __ cmpw(rax, reg(instr->dst)); break;
          case DT_INT32: __ cmpl(rax, reg(instr->dst)); break;
          case DT_INT64: __ cmpq(rax, reg(instr->dst)); break;
          default: UNSUPPORTED;
        }
        if (instr->type == Expression::MIN) {
          __ cmovq(less, reg(instr->dst), rax);
        } else {
          __ cmovq(greater, reg(instr->dst), rax);
        }
        break;
      default: UNSUPPORTED;
    }
  }
};

ExpressionGenerator *CreateScalarIntGenerator() {
  return new ScalarIntGenerator();
}

}  // namespace myelin
}  // namespace sling

