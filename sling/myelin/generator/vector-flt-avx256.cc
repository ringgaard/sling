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

// Generate vector float expression using AVX and YMM registers.
class VectorFltAVX256Generator : public ExpressionGenerator {
 public:
  VectorFltAVX256Generator(Type type) {
    model_.name = "VFltAVX256";
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
      model_.instruction_set({
        Express::MULADD132, Express::MULADD213, Express::MULADD231,
        Express::MULSUB132, Express::MULSUB213, Express::MULSUB231,
      });
    }
    model_.cond_reg_reg_reg = true;
    model_.cond_reg_mem_reg = true;
    model_.instruction_set({
      Express::MOV,
      Express::ADD, Express::SUB, Express::MUL, Express::DIV,
      Express::MINIMUM, Express::MAXIMUM, Express::SQRT,
      Express::CMPEQOQ, Express::CMPNEUQ, Express::CMPLTOQ,
      Express::CMPLEOQ, Express::CMPGTOQ, Express::CMPGEOQ,
      Express::COND, Express::SELECT,
      Express::BITAND, Express::BITOR, Express::BITXOR, Express::BITANDNOT,
      Express::AND, Express::OR, Express::XOR, Express::ANDNOT,
      Express::CVTFLTINT, Express::CVTINTFLT,
      Express::CVTEXPINT, Express::CVTINTEXP,
      Express::BITEQ, Express::QUADSIGN,
      Express::FLOOR, Express::CEIL, Express::ROUND, Express::TRUNC,
      Express::CASTFLOAT, Express::CASTDOUBLE, Express::CASTBYTE,
      Express::CASTSHORT, Express::CASTINT, Express::CASTLONG,
      Express::ADDINT, Express::SUBINT,
      Express::SUM, Express::PRODUCT, Express::MIN, Express::MAX,
      Express::ALL, Express::ANY,
    });
    if (type == DT_FLOAT) {
      model_.instruction_set({Express::RECIPROCAL, Express::RSQRT});
    }
  }

  int VectorSize() override { return YMMRegSize; }

  void Reserve() override {
    // Reserve YMM registers.
    index_->ReserveYMMRegisters(instructions_.NumRegs());

    // Allocate auxiliary registers.
    int num_mm_aux = 0;
    if (!CPU::Enabled(AVX2)) {
      if (instructions_.Has({Express::CVTEXPINT, Express::CVTINTEXP})) {
        num_mm_aux = std::max(num_mm_aux, 1);
      }
      if (instructions_.Has({Express::ADDINT, Express::SUBINT,
                             Express::BITEQ})) {
        num_mm_aux = std::max(num_mm_aux, 3);
      }
      if (instructions_.Has(Express::CVTFLTINT) && type_ == DT_DOUBLE) {
        num_mm_aux = std::max(num_mm_aux, 1);
      }
    }
    if (instructions_.Has(Express::CASTDOUBLE) && type_ == DT_FLOAT) {
      num_mm_aux = std::max(num_mm_aux, 1);
    }
    if (instructions_.Has(Express::CASTLONG) && type_ == DT_FLOAT) {
      num_mm_aux = std::max(num_mm_aux, 1);
    }
    if (instructions_.Has({Express::SUM, Express::PRODUCT, Express::MIN,
                           Express::MAX, Express::ALL, Express::ANY})) {
      num_mm_aux = std::max(num_mm_aux, 1);
    }
    if (instructions_.Has(Express::CVTINTFLT) && type_ == DT_DOUBLE) {
      num_mm_aux = std::max(num_mm_aux, 2);
    }
    index_->ReserveAuxYMMRegisters(num_mm_aux);
  }

  void Generate(Express::Op *instr, MacroAssembler *masm) override {
    switch (instr->type) {
      case Express::MOV:
        if (IsLoadZero(instr) && masm->Enabled(ZEROIDIOM)) {
          // Use XOR to zero register instead of loading constant from memory.
          // This uses the floating point version of xor to avoid bypass delays
          // between integer and floating point units.
          switch (type_) {
            case DT_FLOAT:
              __ vxorps(ymm(instr->dst), ymm(instr->dst), ymm(instr->dst));
              break;
            case DT_DOUBLE:
              __ vxorpd(ymm(instr->dst), ymm(instr->dst), ymm(instr->dst));
              break;
            default: UNSUPPORTED;
          }
        } else {
          GenerateYMMVectorMove(instr, masm);
        }
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
      case Express::MINIMUM:
        GenerateYMMFltOp(instr,
            &Assembler::vminps, &Assembler::vminpd,
            &Assembler::vminps, &Assembler::vminpd,
            masm);
        break;
      case Express::MAXIMUM:
        GenerateYMMFltOp(instr,
            &Assembler::vmaxps, &Assembler::vmaxpd,
            &Assembler::vmaxps, &Assembler::vmaxpd,
            masm);
        break;
      case Express::SQRT:
        GenerateYMMFltOp(instr,
            &Assembler::vsqrtps, &Assembler::vsqrtpd,
            &Assembler::vsqrtps, &Assembler::vsqrtpd,
            masm);
        break;
      case Express::RSQRT:
        GenerateYMMFltOp(instr,
            &Assembler::vrsqrtps, &Assembler::vrsqrtps,
            &Assembler::vrsqrtps, &Assembler::vrsqrtps,
            masm);
        break;
      case Express::RECIPROCAL:
        GenerateYMMFltOp(instr,
            &Assembler::vrcpps, &Assembler::vrcpps,
            &Assembler::vrcpps, &Assembler::vrcpps,
            masm);
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
        GenerateCompare(instr, masm, CMP_EQ_OQ);
        break;
      case Express::CMPNEUQ:
        GenerateCompare(instr, masm, CMP_NEQ_UQ);
        break;
      case Express::CMPLTOQ:
        GenerateCompare(instr, masm, CMP_LT_OQ);
        break;
      case Express::CMPLEOQ:
        GenerateCompare(instr, masm, CMP_LE_OQ);
        break;
      case Express::CMPGTOQ:
        GenerateCompare(instr, masm, CMP_GT_OQ);
        break;
      case Express::CMPGEOQ:
        GenerateCompare(instr, masm, CMP_GE_OQ);
        break;
      case Express::COND:
        GenerateConditional(instr, masm);
        break;
      case Express::SELECT:
        GenerateSelect(instr, masm);
        break;
      case Express::BITAND:
      case Express::AND:
        GenerateYMMFltOp(instr,
            &Assembler::vandps, &Assembler::vandpd,
            &Assembler::vandps, &Assembler::vandpd,
            masm);
        break;
      case Express::BITOR:
      case Express::OR:
        GenerateYMMFltOp(instr,
            &Assembler::vorps, &Assembler::vorpd,
            &Assembler::vorps, &Assembler::vorpd,
            masm);
        break;
      case Express::BITEQ:
        GenerateBitEqual(instr, masm);
        break;
      case Express::BITXOR:
      case Express::XOR:
        GenerateYMMFltOp(instr,
            &Assembler::vxorps, &Assembler::vxorpd,
            &Assembler::vxorps, &Assembler::vxorpd,
            masm);
        break;
      case Express::BITANDNOT:
      case Express::ANDNOT:
        GenerateYMMFltOp(instr,
            &Assembler::vandnps, &Assembler::vandnpd,
            &Assembler::vandnps, &Assembler::vandnpd,
            masm);
        break;
      case Express::FLOOR:
        GenerateYMMFltOp(instr,
            &Assembler::vroundps, &Assembler::vroundpd,
            &Assembler::vroundps, &Assembler::vroundpd,
            round_down, masm);
        break;
      case Express::CEIL:
        GenerateYMMFltOp(instr,
            &Assembler::vroundps, &Assembler::vroundpd,
            &Assembler::vroundps, &Assembler::vroundpd,
            round_up, masm);
        break;
      case Express::ROUND:
        GenerateYMMFltOp(instr,
            &Assembler::vroundps, &Assembler::vroundpd,
            &Assembler::vroundps, &Assembler::vroundpd,
            round_nearest, masm);
        break;
      case Express::TRUNC:
        GenerateYMMFltOp(instr,
            &Assembler::vroundps, &Assembler::vroundpd,
            &Assembler::vroundps, &Assembler::vroundpd,
            round_to_zero, masm);
        break;
      case Express::CASTBYTE:
        GenerateCastByte(instr, masm);
        break;
      case Express::CASTSHORT:
        GenerateCastShort(instr, masm);
        break;
      case Express::CASTINT:
        GenerateCastInt(instr, masm);
        break;
      case Express::CASTLONG:
        GenerateCastLong(instr, masm);
        break;
      case Express::CASTFLOAT:
        GenerateCastFloat(instr, masm);
        break;
      case Express::CASTDOUBLE:
        GenerateCastDouble(instr, masm);
        break;
      case Express::CVTFLTINT:
        GenerateFltToInt(instr, masm);
        break;
      case Express::CVTINTFLT:
        GenerateIntToFlt(instr, masm);
        break;
      case Express::CVTEXPINT:
        GenerateShift(instr, masm, false, type_ == DT_FLOAT ? 23 : 52);
        break;
      case Express::CVTINTEXP:
        GenerateShift(instr, masm, true, type_ == DT_FLOAT ? 23 : 52);
        break;
      case Express::QUADSIGN:
        GenerateShift(instr, masm, true, type_ == DT_FLOAT ? 29 : 61);
        break;
      case Express::ADDINT:
        GenerateIntegerAddition(instr, masm);
        break;
      case Express::SUBINT:
        GenerateIntegerSubtract(instr, masm);
        break;
      case Express::SUM:
        GenerateYMMFltAccOp(instr,
            &Assembler::vaddps, &Assembler::vaddpd,
            &Assembler::vaddps, &Assembler::vaddpd,
            masm);
        break;
      case Express::PRODUCT:
        GenerateYMMFltAccOp(instr,
            &Assembler::vmulps, &Assembler::vmulpd,
            &Assembler::vmulps, &Assembler::vmulpd,
            masm);
        break;
      case Express::MIN:
        GenerateYMMFltAccOp(instr,
            &Assembler::vminps, &Assembler::vminpd,
            &Assembler::vminps, &Assembler::vminpd,
            masm);
        break;
      case Express::MAX:
        GenerateYMMFltAccOp(instr,
            &Assembler::vmaxps, &Assembler::vmaxpd,
            &Assembler::vmaxps, &Assembler::vmaxpd,
            masm);
        break;
      case Express::ALL:
        GenerateYMMFltAccOp(instr,
            &Assembler::vandps, &Assembler::vandpd,
            &Assembler::vandps, &Assembler::vandpd,
            masm);
        break;
      case Express::ANY:
        GenerateYMMFltAccOp(instr,
            &Assembler::vorps, &Assembler::vorpd,
            &Assembler::vorps, &Assembler::vorpd,
            masm);
        break;
      default:
        LOG(FATAL) << "Unsupported instruction: " << instr->AsInstruction();
    }
  }

  // Generate cast from 8-bit integer.
  void GenerateCastByte(Express::Op *instr, MacroAssembler *masm) {
    CHECK(instr->src == -1);
    CHECK(instr->dst != -1);
    if (type_ == DT_FLOAT) {
        if (CPU::Enabled(AVX2)) {
          __ vpmovsxbd(ymm(instr->dst), addr(instr->args[0]));
          __ vcvtdq2ps(ymm(instr->dst), ymm(instr->dst));
        } else {
          UNSUPPORTED;
        }
    } else if (type_ == DT_DOUBLE) {
        if (CPU::Enabled(AVX2)) {
          __ vpmovsxbq(ymm(instr->dst), addr(instr->args[0]));
          GenerateLongToDouble(ymm(instr->dst), masm);
        } else {
          UNSUPPORTED;
        }
    } else {
      UNSUPPORTED;
    }
  }

  // Generate cast from 16-bit integer.
  void GenerateCastShort(Express::Op *instr, MacroAssembler *masm) {
    CHECK(instr->src == -1);
    CHECK(instr->dst != -1);
    if (type_ == DT_FLOAT) {
        if (CPU::Enabled(AVX2)) {
          __ vpmovsxwd(ymm(instr->dst), addr(instr->args[0]));
          __ vcvtdq2ps(ymm(instr->dst), ymm(instr->dst));
        } else {
          UNSUPPORTED;
        }
    } else if (type_ == DT_DOUBLE) {
        if (CPU::Enabled(AVX2)) {
          __ vpmovsxwq(ymm(instr->dst), addr(instr->args[0]));
          GenerateLongToDouble(ymm(instr->dst), masm);
        } else {
          UNSUPPORTED;
        }
    } else {
      UNSUPPORTED;
    }
  }

  // Generate cast from 32-bit integer.
  void GenerateCastInt(Express::Op *instr, MacroAssembler *masm) {
    CHECK(instr->src == -1);
    CHECK(instr->dst != -1);
    if (type_ == DT_FLOAT) {
      __ vcvtdq2ps(ymm(instr->dst), addr(instr->args[0]));
    } else if (type_ == DT_DOUBLE) {
      __ vcvtdq2pd(ymm(instr->dst), addr(instr->args[0]));
    } else {
      UNSUPPORTED;
    }
  }

  // Generate cast from 64-bit integer.
  void GenerateCastLong(Express::Op *instr, MacroAssembler *masm) {
    CHECK(instr->src == -1);
    CHECK(instr->dst != -1);
    if (type_ == DT_FLOAT) {
      // Convert first four int64s to float32.
      __ vmovapd(ymm(instr->dst), addr(instr->args[0]));
      GenerateLongToDouble(ymm(instr->dst), masm);
      __ vcvtpd2ps(ymm(instr->dst), ymm(instr->dst));

      // Convert last four int64s to float32.
      __ vmovapd(ymmaux(0), addr(instr->args[0], YMMRegSize));
      GenerateLongToDouble(ymmaux(0), masm);
      __ vcvtpd2ps(ymmaux(0), ymmaux(0));

      // Combine into 8 floats.
      __ vperm2f128(ymm(instr->dst), ymm(instr->dst), ymmaux(0), 0x20);
    } else if (type_ == DT_DOUBLE) {
      __ vmovapd(ymm(instr->dst), addr(instr->args[0]));
      GenerateLongToDouble(ymm(instr->dst), masm);
    } else {
      UNSUPPORTED;
    }
  }

  // Generate cast from 32-bit float.
  void GenerateCastFloat(Express::Op *instr, MacroAssembler *masm) {
    CHECK(instr->src == -1);
    CHECK(instr->dst != -1);
    if (type_ == DT_DOUBLE) {
      __ vcvtps2pd(ymm(instr->dst), addr(instr->args[0]));
    } else {
      UNSUPPORTED;
    }
  }

  // Generate cast from 64-bit float.
  void GenerateCastDouble(Express::Op *instr, MacroAssembler *masm) {
    CHECK(instr->src == -1);
    CHECK(instr->dst != -1);
    if (type_ == DT_FLOAT) {
      __ vcvtpd2ps(ymm(instr->dst), addr(instr->args[0]));
      __ vcvtpd2ps(ymmaux(0), addr(instr->args[0], YMMRegSize));
      __ vperm2f128(ymm(instr->dst), ymm(instr->dst), ymmaux(0), 0x20);
    } else {
      UNSUPPORTED;
    }
  }

  // Convert 64-bit int to 64-bit float.
  void GenerateLongToDouble(YMMRegister reg, MacroAssembler *masm) {
    // Only works for inputs in the range [-2^51, 2^51].
    // see: http://stackoverflow.com/q/41144668
    auto *m = masm->GetConstant<int64>(4843621399236968448LL, 4);
    __ vpaddq(reg, reg, m->address());
    __ vsubpd(reg, reg, m->address());
  }

  // Generate float to integer conversion.
  void GenerateFltToInt(Express::Op *instr, MacroAssembler *masm) {
    // Convert eight floats or four doubles to int32.
    GenerateYMMFltOp(instr,
        &Assembler::vcvttps2dq, &Assembler::vcvttpd2dq,
        &Assembler::vcvttps2dq, &Assembler::vcvttpd2dq,
        masm);

    // Convert int32 to int64 for doubles.
    if (type_ == DT_DOUBLE) {
      if (CPU::Enabled(AVX2)) {
        __ vpmovsxdq(ymm(instr->dst), xmm(instr->dst));
      } else {
        // Sign-extend each lane separately if AVX2 is not supported.
        __ vpermilps(xmmaux(0), xmm(instr->dst), 0x0E);
        __ vpmovsxdq(xmm(instr->dst), xmm(instr->dst));
        __ vpmovsxdq(xmmaux(0), xmmaux(0));
        __ vinsertf128(ymm(instr->dst), ymm(instr->dst), xmmaux(0), 1);
      }
    }
  }

  // Generate integer to float conversion.
  void GenerateIntToFlt(Express::Op *instr, MacroAssembler *masm) {
    if (type_ == DT_FLOAT) {
      // Convert four int32s to floats.
      if (instr->src != -1) {
        __ vcvtdq2ps(ymm(instr->dst), ymm(instr->src));
      } else {
        __ vcvtdq2ps(ymm(instr->dst), addr(instr->args[0]));
      }
    } else if (type_ == DT_DOUBLE) {
      // Make sure source is in a register.
      int src = instr->src;
      if (instr->src == -1) {
        GenerateYMMMoveMemToReg(ymm(instr->dst), addr(instr->args[0]), masm);
        src = instr->dst;
      }

      // Convert four int64s to four int32s in lower lane.
      __ vperm2f128(ymmaux(0), ymm(src), ymm(src), 1);
      __ vpermilps(ymmaux(1), ymm(src), 0xD8);
      __ vpermilps(ymmaux(0), ymmaux(0), 0x8D);
      __ vblendps(ymmaux(1), ymmaux(1), ymmaux(0), 0x3C);

      // Convert four int32s in lower lane to doubles.
      __ vcvtdq2pd(ymm(instr->dst), ymmaux(1));
    } else {
      UNSUPPORTED;
    }
  }

  // Generate left/right shift.
  void GenerateShift(Express::Op *instr, MacroAssembler *masm,
                     bool left, int bits) {
    // Make sure source is in a register.
    CHECK(instr->dst != -1);
    int src = instr->src;
    if (instr->src == -1) {
      GenerateYMMMoveMemToReg(ymm(instr->dst), addr(instr->args[0]), masm);
      src = instr->dst;
    }

    switch (type_) {
      case DT_FLOAT:
        if (CPU::Enabled(AVX2)) {
          // Shift ymm register.
          if (left) {
            __ vpslld(ymm(instr->dst), ymm(src), bits);
          } else {
            __ vpsrld(ymm(instr->dst), ymm(src), bits);
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
        break;
      case DT_DOUBLE:
        if (CPU::Enabled(AVX2)) {
          // Shift ymm register.
          if (left) {
            __ vpsllq(ymm(instr->dst), ymm(src), bits);
          } else {
            __ vpsrlq(ymm(instr->dst), ymm(src), bits);
          }
        } else {
          // Shift ymm register by shifting lo and hi xmm registers.
          __ vextractf128(xmmaux(0), ymm(src), 1);
          if (left) {
            __ vpsllq(xmmaux(0), xmmaux(0), bits);
            __ vpsllq(xmm(instr->dst), xmm(src), bits);
          } else {
            __ vpsrlq(xmmaux(0), xmmaux(0), bits);
            __ vpsrlq(xmm(instr->dst), xmm(src), bits);
          }
          __ vinsertf128(ymm(instr->dst), ymm(instr->dst), xmmaux(0), 1);
        }
        break;
      default: UNSUPPORTED;
    }
  }

  // Generate integer addition.
  void GenerateIntegerAddition(Express::Op *instr, MacroAssembler *masm) {
    if (CPU::Enabled(AVX2)) {
      GenerateYMMFltOp(instr,
          &Assembler::vpaddd, &Assembler::vpaddq,
          &Assembler::vpaddd, &Assembler::vpaddq,
          masm);
    } else {
      // Move second operand to register.
      CHECK(instr->dst != -1);
      YMMRegister src2;
      if (instr->src2 != -1) {
        src2 = ymm(instr->src2);
      } else {
        GenerateYMMMoveMemToReg(ymmaux(0), addr(instr->args[1]), masm);
        src2 = ymmaux(0);
      }

      // Subtract upper and lower parts separately.
      __ vextractf128(xmmaux(1), ymm(instr->src), 1);
      __ vextractf128(xmmaux(2), src2, 1);
      switch (type_) {
        case DT_FLOAT:
          __ vpaddd(xmmaux(1), xmmaux(1), xmmaux(2));
          __ vpaddd(xmm(instr->dst), xmm(instr->src), src2.xmm());
          break;
        case DT_DOUBLE:
          __ vpaddq(xmmaux(1), xmmaux(1), xmmaux(2));
          __ vpaddq(xmm(instr->dst), xmm(instr->src), src2.xmm());
          break;
        default: UNSUPPORTED;
      }
      __ vinsertf128(ymm(instr->dst), ymm(instr->dst), xmmaux(1), 1);
    }
  }

  // Generate integer subtract.
  void GenerateIntegerSubtract(Express::Op *instr, MacroAssembler *masm) {
    if (CPU::Enabled(AVX2)) {
      GenerateYMMFltOp(instr,
          &Assembler::vpsubd, &Assembler::vpsubq,
          &Assembler::vpsubd, &Assembler::vpsubq,
          masm);
    } else {
      // Move second operand to register.
      CHECK(instr->dst != -1);
      YMMRegister src2;
      if (instr->src2 != -1) {
        src2 = ymm(instr->src2);
      } else {
        GenerateYMMMoveMemToReg(ymmaux(0), addr(instr->args[1]), masm);
        src2 = ymmaux(0);
      }

      // Subtract upper and lower parts separately.
      __ vextractf128(xmmaux(1), ymm(instr->src), 1);
      __ vextractf128(xmmaux(2), src2, 1);
      switch (type_) {
        case DT_FLOAT:
          __ vpsubd(xmmaux(1), xmmaux(1), xmmaux(2));
          __ vpsubd(xmm(instr->dst), xmm(instr->src), src2.xmm());
          break;
        case DT_DOUBLE:
          __ vpsubq(xmmaux(1), xmmaux(1), xmmaux(2));
          __ vpsubq(xmm(instr->dst), xmm(instr->src), src2.xmm());
          break;
        default: UNSUPPORTED;
      }
      __ vinsertf128(ymm(instr->dst), ymm(instr->dst), xmmaux(1), 1);
    }
  }

  // Generate compare.
  void GenerateCompare(Express::Op *instr, MacroAssembler *masm, int8 code) {
    GenerateYMMFltOp(instr,
        &Assembler::vcmpps, &Assembler::vcmppd,
        &Assembler::vcmpps, &Assembler::vcmppd,
        code, masm);
  }

  // Generate bitwise equal compare.
  void GenerateBitEqual(Express::Op *instr, MacroAssembler *masm) {
    if (CPU::Enabled(AVX2)) {
      GenerateYMMFltOp(instr,
          &Assembler::vpcmpeqd, &Assembler::vpcmpeqq,
          &Assembler::vpcmpeqd, &Assembler::vpcmpeqq,
          masm);
    } else {
      // Move second operand to register.
      CHECK(instr->dst != -1);
      YMMRegister src2;
      if (instr->src2 != -1) {
        src2 = ymm(instr->src2);
      } else {
        GenerateYMMMoveMemToReg(ymmaux(0), addr(instr->args[1]), masm);
        src2 = ymmaux(0);
      }

      // Compare upper and lower parts separately.
      __ vextractf128(xmmaux(1), ymm(instr->src), 1);
      __ vextractf128(xmmaux(2), src2, 1);
      switch (type_) {
        case DT_FLOAT:
          __ vpcmpeqd(xmmaux(1), xmmaux(1), xmmaux(2));
          __ vpcmpeqd(xmm(instr->dst), xmm(instr->src), src2.xmm());
          break;
        case DT_DOUBLE:
          __ vpcmpeqq(xmmaux(1), xmmaux(1), xmmaux(2));
          __ vpcmpeqq(xmm(instr->dst), xmm(instr->src), src2.xmm());
          break;
        default: UNSUPPORTED;
      }
      __ vinsertf128(ymm(instr->dst), ymm(instr->dst), xmmaux(1), 1);
    }
  }

  // Generate conditional.
  void GenerateConditional(Express::Op *instr, MacroAssembler *masm) {
    CHECK(instr->dst != -1);
    CHECK(instr->src2 != -1);
    CHECK(instr->mask != -1);
    if (instr->src != -1) {
      // COND dst[mask],src,src2
      switch (type_) {
        case DT_FLOAT:
          __ vblendvps(ymm(instr->dst), ymm(instr->src2), ymm(instr->src),
                       ymm(instr->mask));
          break;
        case DT_DOUBLE:
          __ vblendvpd(ymm(instr->dst), ymm(instr->src2), ymm(instr->src),
                       ymm(instr->mask));
          break;
        default: UNSUPPORTED;
      }
    } else {
      // COND dst[mask],[mem],src2
      switch (type_) {
        case DT_FLOAT:
          __ vblendvps(ymm(instr->dst), ymm(instr->src2), addr(instr->args[1]),
                       ymm(instr->mask));
          break;
        case DT_DOUBLE:
          __ vblendvpd(ymm(instr->dst), ymm(instr->src2), addr(instr->args[1]),
                       ymm(instr->mask));
          break;
        default: UNSUPPORTED;
      }
    }
  }

  // Generate masked select.
  void GenerateSelect(Express::Op *instr, MacroAssembler *masm) {
    CHECK(instr->dst != -1);
    CHECK(instr->mask != -1);
    if (instr->src != -1) {
      // SELECT dst[mask],src
      switch (type_) {
        case DT_FLOAT:
          __ vandps(ymm(instr->dst), ymm(instr->mask), ymm(instr->src));
          break;
        case DT_DOUBLE:
          __ vandpd(ymm(instr->dst), ymm(instr->mask), ymm(instr->src));
          break;
        default: UNSUPPORTED;
      }
    } else {
      // SELECT dst[mask],[mem]
      switch (type_) {
        case DT_FLOAT:
          __ vandps(ymm(instr->dst), ymm(instr->mask), addr(instr->args[1]));
          break;
        case DT_DOUBLE:
          __ vandpd(ymm(instr->dst), ymm(instr->mask), addr(instr->args[1]));
          break;
        default: UNSUPPORTED;
      }
    }
  }

  // Generate code for reduction operation.
  void GenerateReduce(Express::Op *instr, MacroAssembler *masm) override {
    auto acc = ymm(instr->acc);
    auto aux = ymmaux(0);
    __ Reduce(ReduceOp(instr), type_, acc, aux);

    switch (type_) {
      case DT_FLOAT:
        if (instr->dst != -1) {
          __ vmovss(xmm(instr->dst), xmm(instr->dst), xmm(instr->acc));
        } else {
          __ vmovss(addr(instr->result), xmm(instr->acc));
        }
        break;
      case DT_DOUBLE:
        if (instr->dst != -1) {
          __ vmovsd(xmm(instr->dst), xmm(instr->dst), xmm(instr->acc));
        } else {
          __ vmovsd(addr(instr->result), xmm(instr->acc));
        }
        break;
      default: UNSUPPORTED;
    }
  }
};

ExpressionGenerator *CreateVectorFltAVX256Generator(Type type) {
  return new VectorFltAVX256Generator(type);
}

}  // namespace myelin
}  // namespace sling

