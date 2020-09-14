// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sling/myelin/generator/expression.h"

#define __ masm->

namespace sling {
namespace myelin {

using namespace jit;

// Generate scalar int expression using x64 registers.
class ScalarIntGenerator : public ExpressionGenerator {
 public:
  ScalarIntGenerator(Type type) {
    model_.name = "Int";
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
    model_.instruction_set({
      Express::MOV,
      Express::ADD, Express::SUB, Express::MUL, Express::DIV, Express::MOD,
      Express::MINIMUM, Express::MAXIMUM,
      Express::FLOOR, Express::CEIL, Express::ROUND, Express::TRUNC,
    });
  }

  bool ImmediateOperands() override { return true; }

  void Reserve() override {
    // Reserve registers for temps.
    index_->ReserveRegisters(instructions_.NumRegs());

    if (instructions_.Has({Express::DIV, Express::MOD})) {
      // Reserve rax and rdx for integer division.
      index_->ReserveFixedRegister(rax);
      index_->ReserveFixedRegister(rdx);
    } else if (instructions_.Has(Express::MUL) && type_ == DT_INT8) {
      // Reserve al for int8 multiplication.
      index_->ReserveFixedRegister(rax);
    } else if (instructions_.Has({Express::MINIMUM, Express::MAXIMUM})) {
      // Reserve rax for as aux register.
      index_->ReserveFixedRegister(rax);
    }
  }

  void Generate(Express::Op *instr, MacroAssembler *masm) override {
    switch (instr->type) {
      case Express::MOV:
        if (IsLoadZero(instr) && masm->Enabled(ZEROIDIOM)) {
          // Use XOR to zero register instead of loading constant from memory.
          __ xorq(reg(instr->dst), reg(instr->dst));
        } else if (instr->dst != -1 && instr->args[0]->type == Express::CONST) {
          // Load immediate directly to register.
          GenerateMovImm(instr, masm);
        } else {
          GenerateScalarIntMove(instr, masm);
        }
        break;
      case Express::ADD:
        if (instr->dst != -1 && instr->args[1]->type == Express::CONST) {
          GenerateAddImm(instr, masm);
        } else {
          GenerateIntBinaryOp(instr,
              &Assembler::addb, &Assembler::addb,
              &Assembler::addw, &Assembler::addw,
              &Assembler::addl, &Assembler::addl,
              &Assembler::addq, &Assembler::addq,
              masm);
        }
        break;
      case Express::SUB:
        if (instr->dst != -1 && instr->args[1]->type == Express::CONST) {
          GenerateSubImm(instr, masm);
        } else {
          GenerateIntBinaryOp(instr,
              &Assembler::subb, &Assembler::subb,
              &Assembler::subw, &Assembler::subw,
              &Assembler::subl, &Assembler::subl,
              &Assembler::subq, &Assembler::subq,
              masm);
        }
        break;
      case Express::MUL:
        if (instr->dst != -1 && instr->args[1]->type == Express::CONST) {
          GenerateMulImm(instr, masm);
        } else if (type_ == DT_INT8) {
          GenerateMulInt8(instr, masm);
        } else {
          GenerateIntBinaryOp(instr,
              &Assembler::imulw, &Assembler::imulw,  // dummy
              &Assembler::imulw, &Assembler::imulw,
              &Assembler::imull, &Assembler::imull,
              &Assembler::imulq, &Assembler::imulq,
              masm);
        }
        break;
      case Express::DIV:
      case Express::MOD:
        GenerateDivMod(instr, masm);
        break;
      case Express::MINIMUM:
      case Express::MAXIMUM:
        GenerateMinMax(instr, masm);
        break;
      case Express::FLOOR:
      case Express::CEIL:
      case Express::ROUND:
      case Express::TRUNC:
        // Rounding is a no-op for integers.
        if (instr->dst != instr->src) {
          GenerateScalarIntMove(instr, masm);
        }
        break;
      default:
        LOG(FATAL) << "Unsupported instruction: " << instr->AsInstruction();
    }
  }

  // Get constant value for argument.
  int64 GetConstant(Express::Var *var) {
    switch (type_) {
      case DT_INT8: return value<int8>(var);
      case DT_INT16: return value<int16>(var);
      case DT_INT32: return value<int32>(var);
      case DT_INT64: return value<int64>(var);
      default: UNSUPPORTED;
    }
    return -1;
  }

  // Load immediate into register.
  void GenerateMovImm(Express::Op *instr, MacroAssembler *masm) {
    int64 imm = GetConstant(instr->args[0]);
    Register dst = reg(instr->dst);
    switch (type_) {
      case DT_INT8:  __ movb(dst, Immediate(imm)); break;
      case DT_INT16: __ movw(dst, Immediate(imm)); break;
      case DT_INT32: __ movl(dst, Immediate(imm)); break;
      case DT_INT64: __ movq(dst, Immediate(imm)); break;
      default: UNSUPPORTED;
    }
  }

  // Generate increment.
  void GenerateInc(Register dst, MacroAssembler *masm)  {
    switch (type_) {
      case DT_INT8:  __ incb(dst); break;
      case DT_INT16: __ incw(dst); ; break;
      case DT_INT32: __ incl(dst); ; break;
      case DT_INT64: __ incq(dst); ; break;
      default: UNSUPPORTED;
    }
  }

  // Generate decrement.
  void GenerateDec(Register dst, MacroAssembler *masm)  {
    switch (type_) {
      case DT_INT8:  __ decb(dst); break;
      case DT_INT16: __ decw(dst); ; break;
      case DT_INT32: __ decl(dst); ; break;
      case DT_INT64: __ decq(dst); ; break;
      default: UNSUPPORTED;
    }
  }

  // Add immediate to register. Adding 0 is a no-op.
  void GenerateAddImm(Express::Op *instr, MacroAssembler *masm) {
    int64 imm = GetConstant(instr->args[1]);
    Register dst = reg(instr->dst);
    if (imm == 1) {
      GenerateInc(dst, masm);
    } else if (imm == -1) {
      GenerateDec(dst, masm);
    } else if (imm != 0) {
      switch (type_) {
        case DT_INT8:  __ addb(dst, Immediate(imm)); break;
        case DT_INT16: __ addw(dst, Immediate(imm)); break;
        case DT_INT32: __ addl(dst, Immediate(imm)); break;
        case DT_INT64: __ addq(dst, Immediate(imm)); break;
        default: UNSUPPORTED;
      }
    }
  }

  // Subtract immediate from register. Subtracting 0 is a no-op.
  void GenerateSubImm(Express::Op *instr, MacroAssembler *masm) {
    int64 imm = GetConstant(instr->args[1]);
    Register dst = reg(instr->dst);
    if (imm == 1) {
      GenerateDec(dst, masm);
    } else if (imm == -1) {
      GenerateInc(dst, masm);
    } else if (imm != 0) {
      switch (type_) {
        case DT_INT8:  __ subb(dst, Immediate(imm)); break;
        case DT_INT16: __ subw(dst, Immediate(imm)); break;
        case DT_INT32: __ subl(dst, Immediate(imm)); break;
        case DT_INT64: __ subq(dst, Immediate(imm)); break;
        default: UNSUPPORTED;
      }
    }
  }

  // Multiply register with immediate. Multiplying with 1 is a no-op.
  void GenerateMulImm(Express::Op *instr, MacroAssembler *masm) {
    int64 imm = GetConstant(instr->args[1]);
    Register dst = reg(instr->dst);
    if (imm != 1) {
      // Shift instead of multiply if immediate is a power of two.
      int shift = 0;
      int64 value = 1;
      while (value < imm) {
        value <<= 1;
        shift++;
      }
      if (value == imm) {
        switch (type_) {
          case DT_INT8:  __ salb(dst, Immediate(shift)); break;
          case DT_INT16: __ salw(dst, Immediate(shift)); break;
          case DT_INT32: __ sall(dst, Immediate(shift)); break;
          case DT_INT64: __ salq(dst, Immediate(shift)); break;
          default: UNSUPPORTED;
        }
      } else {
        switch (type_) {
          case DT_INT8:
            __ movsxbq(dst, dst);
            __ imulw(dst, dst, Immediate(imm));
            break;
          case DT_INT16:
            __ imulw(dst, dst, Immediate(imm));
            break;
          case DT_INT32:
            __ imull(dst, dst, Immediate(imm));
            break;
          case DT_INT64:
            __ imulq(dst, dst, Immediate(imm));
            break;
          default: UNSUPPORTED;
        }
      }
    }
  }

  // Generate 8-bit multiply.
  void GenerateMulInt8(Express::Op *instr, MacroAssembler *masm) {
    CHECK(instr->dst != -1);
    __ movq(rax, reg(instr->dst));
    if (instr->src != -1) {
      __ imulb(reg(instr->src));
    } else {
      __ imulb(addr(instr->args[1]));
    }
    __ movq(reg(instr->dst), rax);
  }

  // Generate division or modulus.
  void GenerateDivMod(Express::Op *instr, MacroAssembler *masm) {
    CHECK(instr->dst != -1);
    Register dst = reg(instr->dst);
    bool modulus = instr->type == Express::MOD;

    if (instr->args[1]->type == Express::CONST) {
      int64 imm = GetConstant(instr->args[1]);
      if (imm == 1) {
        // Modulus by one is zero and division by one is a nop-op.
        if (modulus) {
          __ xorq(dst, dst);
        }
        return;
      }
      if (imm == 0) LOG(WARNING) << "Division by zero";

      // Shift/mask instead of division/modulus if immediate is a power of two.
      int shift = 0;
      int64 value = 1;
      while (value < imm) {
        value <<= 1;
        shift++;
      }
      if (value == imm) {
        if (modulus) {
          switch (type_) {
            case DT_INT16:
              __ testw(dst, dst);
              __ leal(rax, Operand(dst, imm - 1));
              __ cmovl(not_sign, rax, dst);
              __ andw(rax, Immediate(-imm));
              __ subw(dst, rax);
              break;
            case DT_INT8:
            case DT_INT32:
              __ testl(dst, dst);
              __ leal(rax, Operand(dst, imm - 1));
              __ cmovl(not_sign, rax, dst);
              __ andl(rax, Immediate(-imm));
              __ subl(dst, rax);
              break;
            case DT_INT64:
              __ testq(dst, dst);
              __ leaq(rax, Operand(dst, imm - 1));
              __ cmovq(not_sign, rax, dst);
              __ andq(rax, Immediate(-imm));
              __ subq(dst, rax);
              break;
            case DT_UINT8:
              __ andb(dst, Immediate(imm - 1));
              break;
            case DT_UINT16:
              __ andw(dst, Immediate(imm - 1));
              break;
            case DT_UINT32:
              __ andl(dst, Immediate(imm - 1));
              break;
            case DT_UINT64:
              __ andq(dst, Immediate(imm - 1));
              break;
            default:
              UNSUPPORTED;
          }
        } else {
          switch (type_) {
            case DT_INT8:
              __ testb(dst, dst);
              __ leal(rax, Operand(dst, imm - 1));
              __ cmovl(sign, dst, rax);
              __ sarb(dst, Immediate(shift));
              break;
            case DT_INT16:
              __ testw(dst, dst);
              __ leal(rax, Operand(dst, imm - 1));
              __ cmovl(sign, dst, rax);
              __ sarw(dst, Immediate(shift));
              break;
            case DT_INT32:
              __ testl(dst, dst);
              __ leal(rax, Operand(dst, imm - 1));
              __ cmovl(sign, dst, rax);
              __ sarl(dst, Immediate(shift));
              break;
            case DT_INT64:
              __ testq(dst, dst);
              if (shift < 32) {
                __ leaq(rax, Operand(dst, imm - 1));
              } else {
                __ movq(rax, imm - 1);
                __ addq(rax, dst);
              }
              __ cmovq(sign, dst, rax);
              __ sarq(dst, Immediate(shift));
              break;
            case DT_UINT8:
              __ sarb(dst, Immediate(shift));
              break;
            case DT_UINT16:
              __ sarw(dst, Immediate(shift));
              break;
            case DT_UINT32:
              __ sarl(dst, Immediate(shift));
              break;
            case DT_UINT64:
              __ sarq(dst, Immediate(shift));
              break;
            default:
              UNSUPPORTED;
          }
        }
        return;
      }
    }

    if (dst.code() != rax.code()) {
      __ movq(rax, dst);
    }

    // Sign-extend rax into rdx:rax.
    switch (type_) {
      case DT_INT8:  __ movsxbl(rax, rax); break;
      case DT_INT16: __ cbw(); break;
      case DT_INT32: __ cdq(); break;
      case DT_INT64: __ cqo(); break;
      default: UNSUPPORTED;
    }

    GenerateIntUnaryOp(instr,
        &Assembler::idivb, &Assembler::idivb,
        &Assembler::idivw, &Assembler::idivw,
        &Assembler::idivl, &Assembler::idivl,
        &Assembler::idivq, &Assembler::idivq,
        masm, 1);

    if (modulus) {
      if (dst.code() != rdx.code()) {
        __ movq(dst, rdx);
      }
    } else {
      if (dst.code() != rax.code()) {
        __ movq(dst, rax);
      }
    }
  }

  // Generate min/max.
  void GenerateMinMax(Express::Op *instr, MacroAssembler *masm) {
    CHECK(instr->dst != -1);
    if (instr->args[1]->type == Express::NUMBER) {
      int64 imm = Express::NumericFlt32(instr->args[1]->id);
      __ movq(rax, Immediate(imm));
    } else if (instr->args[1]->type == Express::CONST) {
      int64 imm = GetConstant(instr->args[1]);
      __ movq(rax, Immediate(imm));
    } else if (instr->src != -1) {
      __ movq(rax, reg(instr->src));
    } else {
      GenerateIntMoveMemToReg(rax, addr(instr->args[1]), masm);
    }
    switch (type_) {
      case DT_INT8:  __ cmpb(rax, reg(instr->dst)); break;
      case DT_INT16: __ cmpw(rax, reg(instr->dst)); break;
      case DT_INT32: __ cmpl(rax, reg(instr->dst)); break;
      case DT_INT64: __ cmpq(rax, reg(instr->dst)); break;
      default: UNSUPPORTED;
    }
    if (instr->type == Express::MINIMUM) {
      __ cmovq(less, reg(instr->dst), rax);
    } else {
      __ cmovq(greater, reg(instr->dst), rax);
    }
  }
};

ExpressionGenerator *CreateScalarIntGenerator(Type type) {
  return new ScalarIntGenerator(type);
}

}  // namespace myelin
}  // namespace sling

