#ifndef MYELIN_GENERATOR_EXPRESSION_H_
#define MYELIN_GENERATOR_EXPRESSION_H_

#include <vector>

#include "myelin/compute.h"
#include "myelin/expression.h"
#include "myelin/generator/index.h"

namespace sling {
namespace myelin {

class ExpressionGenerator {
 public:
  // Assembler type definitions.
  typedef jit::Assembler Assembler;
  typedef jit::Operand Operand;
  typedef jit::Register Register;
  typedef jit::XMMRegister XMMRegister;
  typedef jit::YMMRegister YMMRegister;

  // Register sizes in bytes.
  const static int XMMRegSize = 16;
  const static int YMMRegSize = 32;

  virtual ~ExpressionGenerator() = default;

  // Return generator name.
  virtual string Name() = 0;

  // Return vector size in bytes.
  virtual int VectorSize() { return TypeTraits::of(type_).size(); }

  // Reserve all the registers needed by the generator.
  virtual void Reserve() = 0;

  // Generate code for instruction.
  virtual void Generate(Expression::Op *instr, MacroAssembler *masm) = 0;

  // Initialize expression generator.
  void Initalize(const Expression &expression,
                 Type type,
                 IndexGenerator *index);

  // Generate code for expression.
  void Generate(MacroAssembler *masm);

  // Select expression generator for expression that is supported by the CPU.
  static ExpressionGenerator *Select(const Expression &expr,
                                     Type type, int size);

 protected:
  // Assembler instruction methods for different instruction formats.
  typedef void (Assembler::*OpReg)(Register);
  typedef void (Assembler::*OpMem)(const Operand &);
  typedef void (Assembler::*OpRegReg)(Register,
                                      Register);
  typedef void (Assembler::*OpRegMem)(Register,
                                    const Operand &);
  typedef void (Assembler::*OpXMMRegReg)(XMMRegister,
                                         XMMRegister);
  typedef void (Assembler::*OpXMMRegMem)(XMMRegister,
                                         const Operand &);
  typedef void (Assembler::*OpXMMRegRegReg)(XMMRegister,
                                            XMMRegister,
                                            XMMRegister);
  typedef void (Assembler::*OpXMMRegRegMem)(XMMRegister,
                                            XMMRegister,
                                            const Operand &);
  typedef void (Assembler::*OpYMMRegRegReg)(YMMRegister,
                                            YMMRegister,
                                            YMMRegister);
  typedef void (Assembler::*OpYMMRegRegMem)(YMMRegister,
                                            YMMRegister,
                                            const Operand &);

  // Check if size is a multiple of the vector size.
  static bool IsVector(int size, int vecsize) {
    return size > 1 && size % vecsize == 0;
  }

  // Return operand for accessing memory variable.
  Operand addr(Expression::Var *var) { return index_->addr(var); }

  // Return register for temporary variable.
  Register reg(int idx) { return index_->reg(idx); }
  XMMRegister xmm(int idx) { return index_->xmm(idx); }
  YMMRegister ymm(int idx) { return index_->ymm(idx); }

  // Return register for auxiliary variable.
  Register aux(int idx) { return index_->aux(idx); }
  XMMRegister xmmaux(int idx) { return index_->xmmaux(idx); }
  YMMRegister ymmaux(int idx) { return index_->ymmaux(idx); }

  // Generate XMM scalar float move.
  void GenerateXMMScalarFltMove(Expression::Op *instr, MacroAssembler *masm);

  // Generate XMM vector move.
  void GenerateXMMVectorMove(Expression::Op *instr, MacroAssembler *masm);

  // Generate YMM vector move.
  void GenerateYMMVectorMove(Expression::Op *instr, MacroAssembler *masm);

  // Generate move of x64 operand to register.
  void GenerateIntMoveMemToReg(Register dst, const Operand &src,
                               MacroAssembler *masm);

  // Generate move of x64 register to operand.
  void GenerateIntMoveRegToMem(const Operand &dst, Register src,
                               MacroAssembler *masm);

  // Generate x64 scalar int move.
  void GenerateScalarIntMove(Expression::Op *instr, MacroAssembler *masm);

  // Generate XMM vector int move.
  void GenerateXMMVectorIntMove(Expression::Op *instr, MacroAssembler *masm);

  // Generate YMM vector int move.
  void GenerateYMMVectorIntMove(Expression::Op *instr, MacroAssembler *masm);

  // Generate two-operand XMM float op.
  void GenerateXMMFltOp(
    Expression::Op *instr,
    OpXMMRegReg fltopreg, OpXMMRegReg dblopreg,
    OpXMMRegMem fltopmem, OpXMMRegMem dblopmem,
    MacroAssembler *masm);

  // Generate three-operand XMM float op.
  void GenerateXMMFltOp(
      Expression::Op *instr,
      OpXMMRegRegReg fltopreg, OpXMMRegRegReg dblopreg,
      OpXMMRegRegMem fltopmem, OpXMMRegRegMem dblopmem,
      MacroAssembler *masm);

  // Generate three-operand YMM float op.
  void GenerateYMMFltOp(
      Expression::Op *instr,
      OpYMMRegRegReg fltopreg, OpYMMRegRegReg dblopreg,
      OpYMMRegRegMem fltopmem, OpYMMRegRegMem dblopmem,
      MacroAssembler *masm);

  // Generate one-operand x64 int op.
  void GenerateIntUnaryOp(
      Expression::Op *instr,
      OpReg opregb, OpMem opmemb,
      OpReg opregw, OpMem opmemw,
      OpReg opregd, OpMem opmemd,
      OpReg opregq, OpMem opmemq,
      MacroAssembler *masm, int argnum = 0);

  // Generate two-operand x64 int op.
  void GenerateIntBinaryOp(
      Expression::Op *instr,
      OpRegReg opregb, OpRegMem opmemb,
      OpRegReg opregw, OpRegMem opmemw,
      OpRegReg opregd, OpRegMem opmemd,
      OpRegReg opregq, OpRegMem opmemq,
      MacroAssembler *masm, int argnum = 1);

  // Generate two-operand XMM int op.
  void GenerateXMMIntOp(
      Expression::Op *instr,
      OpXMMRegReg opregb, OpXMMRegMem opmemb,
      OpXMMRegReg opregw, OpXMMRegMem opmemw,
      OpXMMRegReg opregd, OpXMMRegMem opmemd,
      OpXMMRegReg opregq, OpXMMRegMem opmemq,
      MacroAssembler *masm, int argnum = 1);

  // Generate three-operand XMM int op.
  void GenerateXMMIntOp(
      Expression::Op *instr,
      OpXMMRegRegReg opregb, OpXMMRegRegMem opmemb,
      OpXMMRegRegReg opregw, OpXMMRegRegMem opmemw,
      OpXMMRegRegReg opregd, OpXMMRegRegMem opmemd,
      OpXMMRegRegReg opregq, OpXMMRegRegMem opmemq,
      MacroAssembler *masm, int argnum = 1);

  // Generate three-operand YMM int op.
  void GenerateYMMIntOp(
      Expression::Op *instr,
      OpYMMRegRegReg opregb, OpYMMRegRegMem opmemb,
      OpYMMRegRegReg opregw, OpYMMRegRegMem opmemw,
      OpYMMRegRegReg opregd, OpYMMRegRegMem opmemd,
      OpYMMRegRegReg opregq, OpYMMRegRegMem opmemq,
      MacroAssembler *masm, int argnum = 1);

  // Index generator for expression.
  IndexGenerator *index_ = nullptr;

  // Type for expression.
  Type type_;

  // Instruction model for instruction set used by generator.
  Expression::Model model_;

  // Expression that should be generated.
  Expression expression_;

  // Instructions for generating expression.
  Expression instructions_;
};

// Error handler for unsupported operations.
void UnsupportedOperation(const char *file, int line);

#define UNSUPPORTED UnsupportedOperation(__FILE__, __LINE__);

}  // namespace myelin
}  // namespace sling

#endif  // MYELIN_GENERATOR_EXPRESSION_H_

