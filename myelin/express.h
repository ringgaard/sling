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

#ifndef MYELIN_EXPRESS_H_
#define MYELIN_EXPRESS_H_

#include <map>
#include <string>
#include <vector>

#include "base/logging.h"
#include "base/types.h"

namespace sling {
namespace myelin {

// Express is an intermediate representation (IR) of lists of expressions. An
// expression is a computation of outputs from inputs using a fixed set of
// functions. Express uses single static assignment (SSA) form to represent the
// computations as sequence of operations on variables. The following kinds of
// variables are supported:
//
//   %n: input variable
//   #n: constant variable
//   @n: output variable
//   $n: temporary variable
//
// An Express recipe is a text format for representing computations over
// inputs variables to produce the output variables. A recipe has the following
// grammar:
//
//   <recipe> := <assignment> | <assignment> ';' <recipe>
//   <assignment> := <variable> '=' <expression>
//   <expression> := <variable> | <operation>
//   <operation> := <name> '(' <arg list> ')'
//   <arg list> := <arg> | <arg> ',' <arg list>
//   <arg> := <variable> | <expression>
//   <variable> := <input variable> | <constant> |
//                 <output variable> | <temp variable>
//   <input variable> := '%' <number>
//   <constant> := '#' <number>
//   <output variable> := '@' <number>
//   <temp variable> := '$' <number>
//
class Express {
 public:
  struct Var;
  struct Op;

  // Variable type.
  enum VarType {INPUT, CONST, OUTPUT, TEMP};

  // Operation type.
  enum OpType {
    MOV,        // identity operation, r=a
    ADD,        // addition, r=a+b
    SUB,        // subtraction, r=a-b
    MUL,        // multiplication, r=a*b
    DIV,        // division, r=a/b
    MIN,        // minimum, r=max(a,b)
    MAX,        // maximum, r=min(a,b)

    RELU,       // rectified linear unit, r=max(0,a)

    MULADD132,  // fused multiply/add, r=a*c+b
    MULADD213,  // fused multiply/add, r=b*a+c
    MULADD231,  // fused multiply/add, r=b*c+a
    MULSUB132,  // fused multiply/sub, r=a*c-b
    MULSUB213,  // fused multiply/sub, r=b*a-c
    MULSUB231,  // fused multiply/sub, r=b*c-a

    INVALID,    // invalid operation
  };

  // Variable mapping.
  typedef std::map<Var *, Var *> Map;

  // Variable in expression.
  struct Var {
    Var(const Var &other) = default;
    Var(VarType type, int id) : type(type), id(id), producer(nullptr) {}
    string AsString() const;
    void GetRecipe(string *recipe) const;

    // An inlined variable is a temporary variable that is only needed in a
    // single context.
    bool inlined() const { return type == TEMP && consumers.size() == 1; }

    // Redirect all consumers of variable to another variable.
    void Redirect(Var *other);

    VarType type;                 // variable type
    int id;                       // variable id (-1 for unassigned temps)
    Op *producer;                 // operation producing value for variable
    std::vector<Op *> consumers;  // consumers of variable

    // Live range for variable.
    Op *first = nullptr;          // first usage of variable
    Op *last = nullptr;           // last usage of variable
  };

  // Operation in expression.
  struct Op {
    Op(const Op &other) = default;
    Op(OpType type) : type(type) {}
    string AsString() const;
    void GetRecipe(string *recipe) const;
    bool EqualTo(Op *other) const;
    string AsInstruction() const;

    // Assign result of operation to variable.
    void Assign(Var *var, bool reassign = false);

    // Add argument.
    void AddArgument(Var *arg);

    // Remove all arguments.
    void ClearArguments();

    // Return number of arguments.
    int arity() const { return args.size(); }

    // Check if operation is commutative.
    bool commutative() const {
      return type == ADD || type == MUL || type == MIN || type == MAX;
    }

    // Check if operation is a no-op.
    bool nop() const { return type == MOV && dst != -1 && src == dst; }

    // Operation is computing result = type(args...).
    OpType type;                  // operation type
    Var *result = nullptr;        // variable where result is stored
    std::vector<Var *> args;      // operation arguments

    // Register assignment for operands.
    int dst = -1;                 // register for first operand
    int src = -1;                 // register for second operand
    int src2 = -1;                // register for third operand
    bool first_is_dest = false;   // first argument is also destination
  };

  // Instruction model with instruction forms supported by target architecture
  // for rewriting expression operations.
  struct Model {
    // Move instruction formats.
    bool mov_reg_reg = false;       // dst = src
    bool mov_reg_imm = false;       // dst = imm
    bool mov_reg_mem = false;       // dst = [mem]
    bool mov_mem_reg = false;       // [mem] = src

    // Two-operand instruction formats.
    bool op_reg_reg = false;        // dst = op(dst, src)
    bool op_reg_imm = false;        // dst = op(dst, imm)
    bool op_reg_mem = false;        // dst = op(dst, [mem])
    bool op_mem_reg = false;        // [mem] = op([mem], src)
    bool op_mem_imm = false;        // [mem] = op([mem], imm)

    // Three-operand instruction formats.
    bool op_reg_reg_reg = false;    // dst = op(src1, src2)
    bool op_reg_reg_mem = false;    // dst = op(src, [mem])

    // Unary function instruction formats.
    bool func_reg_reg = false;      // dst = op(src)
    bool func_reg_imm = false;      // dst = op(imm)
    bool func_reg_mem = false;      // dst = op([mem])
    bool func_mem_reg = false;      // [mem] = op(src)
    bool func_mem_imm = false;      // [mem] = op(imm)

    // Fused multiply instruction formats.
    bool fm_reg_reg_reg = false;    // dst = op(dst, src1, src2)
    bool fm_reg_reg_mem = false;    // dst = op(dst, src, [mem])
  };

  ~Express();

  // Parse an expression recipe and add it to the expression.
  void Parse(const string &recipe);

  // Return recipe for expression.
  void GetRecipe(string *recipe) const;
  string AsRecipe() const {
    string str;
    GetRecipe(&str);
    return str;
  }

  // Add new operation to expression.
  Op *Operation(OpType type);
  Op *OperationBefore(Op *pos, OpType type);
  Op *OperationAfter(Op *pos, OpType type);

  // Lookup variable in expression or add a new variable if it does not exist.
  Var *Variable(VarType type, int id);

  // Add new temp variable to expression.
  Var *NewTemp();

  // Count the number of variable of a certain type.
  int NumVars(VarType type) const;

  // Count the number of ops of a certain type.
  int NumOps(OpType type) const;

  // Check if expression has node of a certain type.
  bool Has(OpType type) const { return NumOps(type) > 0; }

  // Compact temporary variable ids and return the number of temporary variable.
  int CompactTempVars();

  // Eliminate common subexpressions.
  void EliminateCommonSubexpressions();

  // Cache inputs and results used in multiple ops in temporary variables.
  void CacheResults();

  // Compute live range for each variable.
  void ComputeLiveRanges();

  // Return maximum number of active temp variables.
  int MaxActiveTemps() const;

  // Copy operations and variables from another expression.
  void Copy(const Express &other);

  // Merge variable and operations from another expression into this
  // expression. The variables are mapped through the mapping which maps
  // variables in the other expression to variables in this expression.
  void Merge(Express *other, const Map &varmap);

  // Fuse operations. All occurrences of outer(inner(a,b),c) are changed to
  // left(a,b,c) and all occurrences of outer(a,inner(b,c)) to right(a,b,c).
  void Fuse(OpType outer, OpType inner, OpType left, OpType right);
  void FuseMulAdd() { Fuse(ADD, MUL, MULADD213, MULADD231); }
  void FuseMulSub() { Fuse(SUB, MUL, MULSUB213, MULSUB231); }

  // Rewrite expression to match instruction forms supported by target
  // architecture. The expression is assumed to be on static single assignment
  // form. The expression is rewritten by adding additional temporary variables
  // to the rewritten expression so only the supported instruction form are
  // needed for evaluating the expression.
  bool Rewrite(const Model &model, Express *rewritten) const;

  // Allocate registers for operands. Return the number of registers used.
  int AllocateRegisters();

  // Returns the number of register used by expression.
  int NumRegs() const;

  // Variables.
  const std::vector<Var *> vars() const { return vars_; }

  // Operations.
  const std::vector<Op *> ops() const { return ops_; }

  // Look up op type for op name. Return INVALID for unknown op name.
  static OpType Lookup(const string &opname);

  // Return op name for op type.
  static const string &OpName(OpType type);

 private:
  // Try to eliminate identical operations from expression. Return true if any
  // operations were removed.
  bool TryToEliminateOps();

  // Try to fuse operation with the producer of the first argument.
  bool TryFuseFirst(Op *op, OpType type, OpType combined);

  // Try to fuse operation with the producer of the second argument.
  bool TryFuseSecond(Op *op, OpType type, OpType combined);

  // Remove variable.
  void RemoveVar(Var *var);

  // Remove operation.
  void RemoveOp(Op *op);

  // Variables in expression.
  std::vector<Var *> vars_;

  // Operations in expression.
  std::vector<Op *> ops_;
};

}  // namespace myelin
}  // namespace sling

#endif  // MYELIN_EXPRESS_H_

