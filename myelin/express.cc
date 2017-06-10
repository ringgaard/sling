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

#include "myelin/express.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace sling {
namespace myelin {

// Variable mapping.
class VariableMap {
 public:
  VariableMap(Express *expr) : expr_(expr) {}

  Express::Var *operator[](Express::Var *var) {
    Express::Var *&m = mapping_[var];
    if (m == nullptr) {
      auto &vars = expr_->vars();
      if (std::find(vars.begin(), vars.end(), var) != vars.end()) {
        // Existing variable in expression.
        return var;
      }

      // Copy variable and update mapping.
      m = expr_->Variable(var->type, var->id);
    }
    return m;
  }

 private:
  Express *expr_;
  std::map<Express::Var *, Express::Var *> mapping_;
};

// Register allocator.
class RegisterAllocator {
 public:
  // Allocate register for variable.
  int Allocate(Express::Var *var) {
    // Check if a register has already been allocated.
    int regno = -1;
    for (int r = 0; r < reg_.size(); ++r) {
      if (reg_[r] == var) return r;
      if (regno == -1 && reg_[r] == nullptr) regno = r;
    }

    if (regno == -1) {
      // Allocate new register.
      regno = reg_.size();
      reg_.push_back(var);
    } else {
      // Assign unused register to variable.
      reg_[regno] = var;
    }

    return regno;
  }

  // Transfer register from one variable to another. Return the transferred
  // register.
  int Transfer(Express::Var *src, Express::Var *dst) {
    for (int r = 0; r < reg_.size(); ++r) {
      if (reg_[r] == src) {
        reg_[r] = dst;
        return r;
      }
    }
    return -1;
  }

  // Get register allocated for variable. Return -1 if no register is allocated.
  int Get(Express::Var *var) const {
    for (int r = 0; r < reg_.size(); ++r) {
      if (reg_[r] == var) return r;
    }
    return -1;
  }

  // Free register used by variable.
  void Free(Express::Var *var) {
    for (int r = 0; r < reg_.size(); ++r) {
      if (reg_[r] == var) reg_[r] = nullptr;
    }
  }

  // Return the maximum number of register allocated.
  int max() const { return reg_.size(); }

 private:
  std::vector<Express::Var *> reg_;
};

// Mapping from operation name to operation type.
static std::map<string, Express::OpType> optypes = {
  {"Id", Express::MOV},
  {"Add", Express::ADD},
  {"Sub", Express::SUB},
  {"Mul", Express::MUL},
  {"Div", Express::DIV},
  {"Min", Express::MIN},
  {"Max", Express::MAX},
  {"Relu", Express::RELU},
  {"MulAdd132", Express::MULADD132},
  {"MulAdd213", Express::MULADD213},
  {"MulAdd231", Express::MULADD231},
  {"MulSub132", Express::MULSUB132},
  {"MulSub213", Express::MULSUB213},
  {"MulSub231", Express::MULSUB231},
};

static const string opname[] = {
  "Id",
  "Add", "Sub", "Mul", "Div",
  "Min", "Max",
  "Relu",
  "MulAdd132", "MulAdd213", "MulAdd231",
  "MulSub132", "MulSub213", "MulSub231",
  "???",
};

// Recipe parser for converting a string to an expression.
class RecipeParser {
 public:
  // Initialize parser.
  RecipeParser(const string &recipe, Express *expr) {
    recipe_ = ptr_ = recipe.data();
    end_ = ptr_ + recipe.size();
    expr_ = expr;
  }

  // Parse recipe.
  void Parse() {
    // Parse list of assignment expressions.
    ParseAssignment();
    while (is(';')) {
      next();
      ParseAssignment();
    }

    // Check that all the input has been comsumed.
    if (more()) Error("Syntax error in expression");

    // Assign ids to intermediate variables.
    expr_->CompactTempVars();
  }

  // Parse assignment expression.
  void ParseAssignment() {
    // Parse assignment variable.
    Express::Var *var = ParseVariable();
    if (var->type == Express::INPUT) {
      Error("Cannot assign to input variable");
    }

    // Consume '='.
    if (current() != '=') Error("Expected '=' in expression");
    next();

    // Parse expression.
    Express::Op *expr = ParseExpression();

    // Assign expression to variable.
    expr->Assign(var);
  }

  // Parse expression.
  Express::Op *ParseExpression() {
    // Parse operation name.
    if (!isletter()) Error("Operation name expected in expression");
    const char *start = ptr_;
    while (isletter() || isdigit()) ptr_++;
    string opname(start, ptr_ - start);

    // Parse argument list.
    if (current() != '(') Error("Expected '(' in expression");
    next();
    std::vector<Express::Var *> args;
    args.push_back(ParseArgument());
    while (current() == ',') {
      next();
      args.push_back(ParseArgument());
    }
    if (current() != ')') Error("Expected ')' in expression");
    next();

    // Create new operation.
    Express::OpType optype = Express::Lookup(opname);
    CHECK(optype != Express::INVALID) << opname;
    Express::Op *op = expr_->Operation(optype);
    for (auto *arg : args) op->AddArgument(arg);

    return op;
  }

  // Parse argument.
  Express::Var *ParseArgument() {
    if (isvar()) {
      // Return variable as argument.
      return ParseVariable();
    } else {
      // Parse expression and assign to intermediate variable. The intermediate
      // variable is assigned a unique negative id which will later be fixed up.
      Express::Op *expr = ParseExpression();
      Express::Var *var = expr_->NewTemp();
      expr->Assign(var);
      return var;
    }
  }

  // Parse variable name.
  Express::Var *ParseVariable() {
    // Parse variable type.
    Express::VarType type;
    if (is('%')) {
      type = Express::INPUT;
    } else if (is('#')) {
      type = Express::CONST;
    } else if (is('@')) {
      type = Express::OUTPUT;
    } else if (is('$')) {
      type = Express::TEMP;
    } else {
      Error("Unknown variable type in expression");
    }
    next();

    // Parse variable id.
    int id = 0;
    int digits = 0;
    while (current() >= '0' && current() <= '9') {
      id = id * 10 + (current() - '0');
      next();
      digits++;
    }
    if (digits == 0) Error("Variable id expected in expression");

    // Return variable.
    return expr_->Variable(type, id);
  }

  // Output error.
  void Error(const char *msg) {
    string prefix = string(recipe_, ptr_ - recipe_);
    string suffix = string(ptr_, end_ - ptr_);
    LOG(FATAL) << msg << ": " << prefix << "➤" << suffix;
  }

  // Current input character.
  char current() { return *ptr_; }

  // Consume next input character.
  void next() { ptr_++; }

  // Check if the next input matches.
  bool is(char ch) const { return more() && *ptr_ == ch; }
  bool isdigit() const { return more() && *ptr_ >= '0' && *ptr_ <= '9'; }
  bool isupper() const { return more() && *ptr_ >= 'A' && *ptr_ <= 'Z'; }
  bool islower() const { return more() && *ptr_ >= 'a' && *ptr_ <= 'z'; }
  bool isletter() const { return isupper() || islower(); }
  bool isvar() const { return is('%') || is('@') || is('$'); }

  // Check if the whole expression has been parsed.
  bool more() const { return ptr_ < end_; }
  bool done() const { return ptr_ == end_; }

 private:
  const char *recipe_;         // expression recipe
  const char *ptr_;            // current position for parser
  const char *end_;            // end of parsed recipe
  Express *expr_;              // target expression
};

Express::OpType Express::Lookup(const string &opname) {
  auto f = optypes.find(opname);
  return f == optypes.end() ? INVALID : f->second;
}

const string &Express::OpName(OpType type) {
  return opname[type];
}

void Express::Parse(const string &recipe) {
  RecipeParser parser(recipe, this);
  parser.Parse();
}

Express::~Express() {
  for (auto *v : vars_) delete v;
  for (auto *o : ops_) delete o;
}

void Express::GetRecipe(string *recipe) const {
  bool first = true;
  for (Op *op : ops_) {
    if (!op->result->inlined()) {
      if (!first) recipe->push_back(';');
      first = false;
      op->result->GetRecipe(recipe);
      recipe->push_back('=');
      op->GetRecipe(recipe);
    }
  }
}

Express::Var *Express::Variable(VarType type, int id) {
  // Look for existing variable.
  if (id != -1) {
    for (Var *v : vars_) {
      if (v->type == type && v->id == id) return v;
    }
  }

  // Add new variable.
  Var *v = new Var(type, id);
  vars_.push_back(v);
  return v;
}

Express::Op *Express::Operation(OpType type) {
  Op *op = new Op(type);
  ops_.push_back(op);
  return op;
}

Express::Op *Express::OperationBefore(Op *pos, OpType type) {
  Op *op = new Op(type);
  auto f = std::find(ops_.begin(), ops_.end(), pos);
  CHECK(f != ops_.end());
  ops_.insert(f, op);
  return op;
}

Express::Op *Express::OperationAfter(Op *pos, OpType type) {
  Op *op = new Op(type);
  auto f = std::find(ops_.begin(), ops_.end(), pos);
  CHECK(f != ops_.end());
  ops_.insert(f + 1, op);
  return op;
}

Express::Var *Express::NewTemp() {
  // Add new temporary variable.
  Var *v = new Var(TEMP, -1);
  vars_.push_back(v);
  return v;
}

void Express::RemoveVar(Var *var) {
  // Check that variable is unused.
  DCHECK(var->producer == nullptr);
  DCHECK(var->consumers.empty());

  // Delete variable.
  auto f = std::find(vars_.begin(), vars_.end(), var);
  DCHECK(f != vars_.end());
  vars_.erase(f);
  delete var;
}

void Express::RemoveOp(Op *op) {
  // Remove operation as producer of result.
  if (op->result != nullptr) {
    DCHECK(op == op->result->producer);
    op->result->producer = nullptr;
  }

  // Remove operation as consumer of argument variables.
  op->ClearArguments();

  // Delete operation.
  auto f = std::find(ops_.begin(), ops_.end(), op);
  DCHECK(f != ops_.end());
  ops_.erase(f);
  delete op;
}

int Express::NumVars(VarType type) const {
  int n = 0;
  for (Var *v : vars_) {
    if (v->type == type) n++;
  }
  return n;
}

int Express::NumOps(OpType type) const {
  int n = 0;
  for (Op *op : ops_) {
    if (op->type == type) n++;
  }
  return n;
}

int Express::CompactTempVars() {
  int n = 0;
  for (Var *v : vars_) {
    if (v->type == TEMP) v->id = n++;
  }
  return n;
}

void Express::EliminateCommonSubexpressions() {
  // Keep trying to eliminate ops until no more can be removed.
  int iterations = 0;
  while (TryToEliminateOps()) iterations++;

  // Compact temporary variables if some operations were eliminated.
  if (iterations > 0) CompactTempVars();
}

bool Express::TryToEliminateOps() {
  // Find matching ops.
  for (int i = 0; i < ops_.size(); ++i) {
    Op *op1 = ops_[i];
    for (int j = i + 1; j < ops_.size(); ++j) {
      Op *op2 = ops_[j];
      if (op1->EqualTo(op2)) {
        Var *v1 = op1->result;
        Var *v2 = op2->result;
        if (v1->type == TEMP) {
          // Eliminate ith operation.
          std::swap(ops_[i], ops_[j]);
          v1->Redirect(v2);
          RemoveOp(op1);
          RemoveVar(v1);
          return true;
        } else if (v2->type == TEMP) {
          // Eliminate jth operation.
          v2->Redirect(v1);
          RemoveOp(op2);
          RemoveVar(v2);
          return true;
        } else {
          // Two output variable. Change second op to move op.
          v2->Redirect(v1);
          op2->type = MOV;
          op2->ClearArguments();
          op2->AddArgument(v1);
          return true;
        }
      }
    }
  }
  return false;
}

void Express::CacheResults() {
  int cached_vars = 0;
  for (int n = 0; n < vars_.size(); ++n) {
    Var *var = vars_[n];
    if (var->type == OUTPUT && var->consumers.size() > 0) {
      // Make temp variable and update all usages to use this instead.
      Op *op = var->producer;
      CHECK(op != nullptr);
      var->producer = nullptr;
      Var *temp = NewTemp();
      op->result = temp;
      var->consumers.swap(temp->consumers);
      for (Op *o : ops_) {
        for (int i = 0; i < o->args.size(); ++i) {
          if (o->args[i] == var) o->args[i] = temp;
        }
      }

      // Assign temp variable to output.
      Op *assign = OperationAfter(op, MOV);
      assign->Assign(var);
      assign->AddArgument(temp);
      cached_vars++;
    } else if (var->type != TEMP && var->consumers.size() > 1) {
      // Make temp variable and update all usages to use this instead.
      Var *temp = NewTemp();
      var->consumers.swap(temp->consumers);
      Op *first = nullptr;
      for (Op *o : ops_) {
        for (int i = 0; i < o->args.size(); ++i) {
          if (o->args[i] == var) {
            o->args[i] = temp;
            if (first == nullptr) first = o;
          }
        }
      }
      CHECK(first != nullptr);

      // Assign temp variable to input.
      Op *assign = OperationBefore(first, MOV);
      assign->Assign(temp);
      assign->AddArgument(var);
      cached_vars++;
    }
  }
  if (cached_vars > 0) CompactTempVars();
}

void Express::ComputeLiveRanges() {
  for (Op *op : ops_) {
    if (op->result->first == nullptr) op->result->first = op;
    op->result->last = op;
    for (Var *arg : op->args) {
      if (arg->first == nullptr) arg->first = op;
      arg->last = op;
    }
  }
}

int Express::MaxActiveTemps() const {
  int active = 0;
  int max_active = 0;
  for (Op *op : ops_) {
    if (op->result->first == op && op->result->type == TEMP) active++;
    if (active > max_active) max_active = active;
    for (Var *arg : op->args) {
      if (arg->last == op && arg->type == TEMP) active--;
    }
  }
  return max_active;
}

void Express::Copy(const Express &other) {
  // Expression must be empty.
  CHECK(vars_.empty());
  CHECK(ops_.empty());
  vars_.reserve(other.vars().size());
  ops_.reserve(other.ops().size());

  // Copy variables.
  std::map<Var *, Var *> varmap;
  for (Var *var : other.vars()) {
    Var *v = new Var(*var);
    vars_.push_back(v);
    varmap[var] = v;
  }

  // Copy operations.
  std::map<Op *, Op *> opmap;
  for (Op *op : other.ops()) {
    Op *o = new Op(*op);
    ops_.push_back(o);
    opmap[op] = o;
  }

  // Map pointers.
  for (Var *var : vars_) {
    var->producer = opmap[var->producer];
    for (Op *&op : var->consumers) op = opmap[op];
    var->first = opmap[var->first];
    var->last = opmap[var->last];
  }
  for (Op *op : ops_) {
    op->result = varmap[op->result];
    for (Var *&var : op->args) var = varmap[var];
  }
}

void Express::Merge(Express *other, const Map &varmap) {
  // Move variables that are not mapped.
  bool temps_moved = false;
  for (Var *var : other->vars_) {
    auto f = varmap.find(var);
    if (f == varmap.end()) {
      vars_.push_back(var);
      if (var->type == TEMP) temps_moved = true;
    } else {
      delete var;
    }
  }

  // Move operations and map arguments.
  for (Op *op : other->ops_) {
    ops_.push_back(op);
    auto f = varmap.find(op->result);
    if (f != varmap.end()) {
      op->result = f->second;
      f->second->producer = op;
    }
    for (int i = 0; i < op->args.size(); ++i) {
      auto f = varmap.find(op->args[i]);
      if (f != varmap.end()) {
        op->args[i] = f->second;
        f->second->consumers.push_back(op);
      }
    }
  }

  // Clear the vars and ops in the other expression.
  other->vars_.clear();
  other->ops_.clear();

  // Rename temporary variables if needed.
  if (temps_moved) CompactTempVars();
}

void Express::Fuse(OpType outer, OpType inner, OpType left, OpType right) {
  bool again = true;
  while (again) {
    again = false;
    for (Op *op : ops_) {
      if (op->type != outer) continue;
      if (op->arity() != 2) continue;
      if (TryFuseFirst(op, inner, left)) {
        again = true;
      } else if (TryFuseSecond(op, inner, right)) {
        again = true;
      }
      if (again) break;
    }
  }
}

bool Express::TryFuseFirst(Op *op, OpType type, OpType combined) {
  // Check if combined op is supported.
  if (combined == INVALID) return false;

  // Check if intermediate variable is only use as an intermediate.
  Var *intermediate = op->args[0];
  if (!intermediate->inlined()) return false;

  // Check that the producer of the intermediate variable is the correct type.
  Op *sub = intermediate->producer;
  if (sub == nullptr || sub->type != type) return false;
  if (sub->arity() != 2) return false;

  // Combine ops.
  Var *a = sub->args[0];
  Var *b = sub->args[1];
  Var *c = op->args[1];

  op->type = combined;
  op->ClearArguments();
  op->AddArgument(a);
  op->AddArgument(b);
  op->AddArgument(c);

  RemoveOp(sub);
  RemoveVar(intermediate);

  return true;
}

bool Express::TryFuseSecond(Op *op, OpType type, OpType combined) {
  // Check if combined op is supported.
  if (combined == INVALID) return false;

  // Check if intermediate variable is only use as an intermediate.
  Var *intermediate = op->args[1];
  if (!intermediate->inlined()) return false;

  // Check that the producer of the intermediate variable is the correct type.
  Op *sub = intermediate->producer;
  if (sub == nullptr || sub->type != type) return false;
  if (sub->arity() != 2) return false;

  // Combine ops.
  Var *a = op->args[0];
  Var *b = sub->args[0];
  Var *c = sub->args[1];

  op->type = combined;
  op->ClearArguments();
  op->AddArgument(a);
  op->AddArgument(b);
  op->AddArgument(c);

  RemoveOp(sub);
  RemoveVar(intermediate);

  return true;
}

bool Express::Rewrite(const Model &model, Express *rewritten) const {
  // Target expression must be empty.
  CHECK_EQ(rewritten->vars().size(), 0);

  // Mapping from original variables to variables in rewritten expression.
  VariableMap varmap(rewritten);

  // Translate all ops to conform to target model.
  bool success = true;
  for (Op *op : ops_) {
    // Get operation type, result, and arguments.
    OpType type = op->type;
    Var *result = op->result;
    std::vector<Var *> args = op->args;
    Var *source = nullptr;
    Var *source2 = nullptr;
    Var *source3 = nullptr;
    Var *destination = nullptr;
    bool first_is_dest = false;

    // Rewrite operation.
    if (op->arity() == 1) {
      if (type == MOV) {
        // Move operations.
        switch (result->type) {
          case TEMP:
            // Move value into register.
            switch (args[0]->type) {
              case INPUT:
              case OUTPUT:
                if (!model.mov_reg_mem) success = false;
                break;
              case TEMP:
                if (!model.mov_reg_reg) success = false;
                break;
              case CONST:
                if (!model.mov_reg_imm && !model.mov_reg_mem) success = false;
                break;
            }
            break;

          case OUTPUT:
            // Move value into output variable.
            switch (args[0]->type) {
              case INPUT:
                // Add temp variable for input.
                source = rewritten->Variable(TEMP, -1);
                break;
              case OUTPUT:
                // Add temp variable for output.
                destination = rewritten->Variable(TEMP, -1);
                break;
              case TEMP:
                if (!model.mov_reg_reg) success = false;
                break;
              case CONST:
                if (!model.mov_reg_imm && !model.mov_reg_mem) success = false;
                break;
            }
            break;

          case INPUT:
          case CONST:
            // Assignment to inputs and constants not allowed.
            success = false;
        }
      } else {
        // Unary operator.
        switch (result->type) {
          case TEMP:
            switch (args[0]->type) {
              case INPUT:
              case OUTPUT:
                if (!model.func_reg_mem) {
                  // Add temp variable for input.
                  source = rewritten->Variable(TEMP, -1);
                  if (!model.func_reg_reg) success = false;
                }
                break;
              case TEMP:
                if (!model.func_reg_reg) success = false;
                break;
              case CONST:
                if (!model.func_reg_imm) {
                  // Add temp variable for input.
                  source = rewritten->Variable(TEMP, -1);
                  if (!model.func_reg_reg) success = false;
                }
                break;
            }
            break;

          case OUTPUT:
            switch (args[0]->type) {
              case INPUT:
              case OUTPUT:
                if (model.func_reg_mem) {
                  // Add temp variable for output.
                  destination = rewritten->Variable(TEMP, -1);
                } else if (model.func_mem_reg) {
                  // Add temp variable for input.
                  source = rewritten->Variable(TEMP, -1);
                } else {
                  // Add temp variables for input and output.
                  destination = rewritten->Variable(TEMP, -1);
                  source = rewritten->Variable(TEMP, -1);
                  if (!model.func_reg_reg) success = false;
                }
                break;
              case TEMP:
                if (!model.func_mem_reg) {
                  // Add temp variable for output.
                  destination = rewritten->Variable(TEMP, -1);
                  if (!model.func_reg_reg) success = false;
                }
                break;
              case CONST:
                if (!model.func_mem_imm) {
                  // Add temp variable for output.
                  destination = rewritten->Variable(TEMP, -1);
                  if (!model.func_reg_imm) {
                    // Add temp variable for input.
                    source = rewritten->Variable(TEMP, -1);
                    if (!model.func_reg_reg) success = false;
                  }
                }
                break;
            }
            break;

          case INPUT:
          case CONST:
            // Assignment to inputs and constants not allowed.
            success = false;
        }
      }
    } else if (op->arity() == 2 && type != MOV) {
      // Binary operator.
      switch (result->type) {
        case TEMP:
        case OUTPUT:
          if (model.op_reg_reg_reg) {
            // Three-operand instruction. Try to put the memory operand last if
            // operation is commutative.
            if (model.op_reg_reg_mem && op->commutative() &&
                args[0]->type != TEMP && args[1]->type == TEMP) {
              std::swap(args[0], args[1]);
            }

            // Destination must be a register.
            if (result->type == OUTPUT) {
              destination = rewritten->Variable(TEMP, -1);
            }

            // Put first argument into a register.
            if (args[0]->type != TEMP) {
              source = rewritten->Variable(TEMP, -1);
            }

            // Put second argument into a register if memory operands are not
            // supported.
            if (args[1]->type != TEMP && !model.op_reg_reg_mem) {
              source2 = rewritten->Variable(TEMP, -1);
            }

            success = true;
          } else if (model.op_reg_reg) {
            // Two-operand instruction.
            Var *dest = result;
            first_is_dest = true;

            // Try to put the memory operand last if operation is commutative.
            if (model.op_reg_mem && op->commutative() &&
                args[0]->type != TEMP && args[1]->type == TEMP) {
              std::swap(args[0], args[1]);
            }

            // Put result and first argument in the same location.
            if (result != args[0] || !model.op_mem_reg) {
              // Put result in temp register if result is an output.
              if (result->type == OUTPUT) {
                dest = destination = rewritten->Variable(TEMP, -1);
              }

              // Move first argument to destination.
              Op *mov = rewritten->Operation(MOV);
              mov->Assign(varmap[dest], true);
              mov->AddArgument(varmap[args[0]]);
              switch (args[0]->type) {
                case INPUT:
                case OUTPUT:
                  if (!model.mov_reg_mem) success = false;
                  break;
                case TEMP:
                  if (!model.mov_reg_reg) success = false;
                  break;
                case CONST:
                  if (!model.mov_reg_imm) success = false;
                  break;
              }
              args[0] = dest;
            }

            // Make second argument available for instruction.
            switch (args[1]->type) {
              case INPUT:
              case OUTPUT:
                // Put second operand into register if memory operands are not
                // supported.
                if (dest->type != TEMP || !model.op_reg_mem) {
                  source2 = rewritten->Variable(TEMP, -1);
                }
                break;
              case TEMP:
                break;
              case CONST:
                // Put second operand into register if immediate operands are
                // not supported.
                if (dest->type == TEMP) {
                  if (!model.op_reg_imm) {
                    source2 = rewritten->Variable(TEMP, -1);
                  }
                } else {
                  if (!model.op_mem_imm) {
                    source2 = rewritten->Variable(TEMP, -1);
                  }
                }
                break;
            }
          } else {
            success = false;
          }
          break;

        case INPUT:
        case CONST:
          // Assignment to inputs and constants not allowed.
          success = false;
      }
    } else if (op->arity() == 3 && model.fm_reg_reg_reg) {
      // Fused multiply instruction.
      Var *dest = result;
      first_is_dest = true;

      // Try to put memory operand last.
      if (model.fm_reg_reg_mem) {
        if (args[1]->type != TEMP && args[2]->type == TEMP) {
          // Swap second and third argument.
          std::swap(args[1], args[2]);
          switch (type) {
            case MULADD132: type = MULADD213; break;
            case MULADD213: type = MULADD132; break;
            case MULADD231: break;
            case MULSUB132: type = MULSUB213; break;
            case MULSUB213: type = MULSUB132; break;
            case MULSUB231: break;
            default: success = false;
          }
        } else if (args[0]->type != TEMP && args[2]->type == TEMP) {
          // Swap first and third argument.
          std::swap(args[0], args[2]);
          switch (type) {
            case MULADD132: break;
            case MULADD213: type = MULADD231; break;
            case MULADD231: type = MULADD213; break;
            case MULSUB132: break;
            case MULSUB213: type = MULSUB231; break;
            case MULSUB231: type = MULSUB213; break;
            default: success = false;
          }
        }
      }

      // Put result and first argument in the same location.
      if (result != args[0]) {
        // Put result in temp register if result is an output.
        if (result->type == OUTPUT) {
          dest = destination = rewritten->Variable(TEMP, -1);
        }

        // Move first argument to destination.
        Op *mov = rewritten->Operation(MOV);
        mov->Assign(varmap[dest], true);
        mov->AddArgument(varmap[args[0]]);
        switch (args[0]->type) {
          case INPUT:
          case OUTPUT:
            if (!model.mov_reg_mem) success = false;
            break;
          case TEMP:
            if (!model.mov_reg_reg) success = false;
            break;
          case CONST:
            if (!model.mov_reg_imm) success = false;
            break;
        }
        args[0] = dest;
      }

      // Make sure second operand is in register.
      if (args[1]->type != TEMP) {
        source2 = rewritten->Variable(TEMP, -1);
      }

      // Make third argument available for instruction.
      if (args[2]->type != TEMP && !model.fm_reg_reg_mem) {
        source3 = rewritten->Variable(TEMP, -1);
      }
    } else {
      LOG(WARNING) << "Unsupported op: " << op->AsString();
      success = false;
    }

    // Assign first argument to source.
    if (source != nullptr) {
      if (!model.mov_reg_mem) success = false;
      Op *mov = rewritten->Operation(MOV);
      mov->Assign(source);
      mov->AddArgument(varmap[args[0]]);
      args[0] = source;
    }

    // Assign second argument to source2.
    if (source2 != nullptr) {
      if (!model.mov_reg_mem) success = false;
      Op *mov = rewritten->Operation(MOV);
      mov->Assign(source2);
      mov->AddArgument(varmap[args[1]]);
      args[1] = source2;
    }

    // Assign third argument to source3.
    if (source3 != nullptr) {
      if (!model.mov_reg_mem) success = false;
      Op *mov = rewritten->Operation(MOV);
      mov->Assign(source3);
      mov->AddArgument(varmap[args[2]]);
      args[2] = source3;
    }

    // Translate operation.
    Op *instr = rewritten->Operation(type);
    instr->first_is_dest = first_is_dest;
    if (result != nullptr) {
      if (destination != nullptr) {
        // Use destination as temporary for result.
        if (!model.mov_mem_reg) success = false;
        instr->Assign(destination, true);
        Op *mov = rewritten->Operation(MOV);
        mov->Assign(varmap[result], true);
        mov->AddArgument(destination);
      } else {
        // Assign directly to result.
        instr->Assign(varmap[result], true);
      }
    }
    for (Var *arg : args) {
      instr->AddArgument(varmap[arg]);
    }
  }

  rewritten->CompactTempVars();
  return success;
}

int Express::AllocateRegisters() {
  RegisterAllocator regs;
  for (Op *op : ops_) {
    if (op->type == MOV) {
      // Allocate destination register for move op.
      if (op->result->type == TEMP) {
        if (op->result->first == op) {
          if (op->type == MOV &&
              op->args[0]->type == TEMP &&
              op->args[0]->last == op) {
            // Steal register from source.
            op->dst = op->src = regs.Transfer(op->args[0], op->result);
          } else {
            // Allocate register for destination.
            CHECK(op->result->last != nullptr);
            op->dst = regs.Allocate(op->result);
          }
        } else {
          op->dst = regs.Get(op->result);
        }
        CHECK(op->dst != -1);
      }

      // Get source register for move op.
      if (op->args[0]->type == TEMP && op->src == -1) {
        op->src = regs.Get(op->args[0]);
        CHECK(op->src != -1);
      }

      // Free source register if it is no longer needed.
      if (op->args[0]->type == TEMP && op->args[0]->last == op) {
        regs.Free(op->args[0]);
      }
    } else {
      // Allocate register for result.
      if (op->result->type == TEMP) {
        if (op->result->first == op) {
          // Allocate register for result.
          CHECK(op->result->last != nullptr);
          op->dst = regs.Allocate(op->result);
        } else {
          op->dst = regs.Get(op->result);
        }
        CHECK(op->dst != -1);
      }

      // Get registers for source operands.
      int first = op->first_is_dest ? 1 : 0;
      int second = first + 1;
      if (op->arity() > first && op->args[first]->type == TEMP) {
        op->src = regs.Get(op->args[first]);
        CHECK(op->src != -1);
      }
      if (op->arity() > second && op->args[second]->type == TEMP) {
        op->src2 = regs.Get(op->args[second]);
        CHECK(op->src2 != -1);
      }

      // Free unused registers.
      if (op->arity() > first && op->args[first]->type == TEMP) {
        if (op->args[first]->last == op) regs.Free(op->args[first]);
      }
      if (op->arity() > second && op->args[second]->type == TEMP) {
        if (op->args[second]->last == op) regs.Free(op->args[second]);
      }
    }
  }

  return regs.max();
}

int Express::NumRegs() const {
  int num_regs = 0;
  for (auto *op : ops_) {
    if (op->dst != -1 && op->dst + 1 > num_regs) num_regs = op->dst + 1;
    if (op->src != -1 && op->src + 1 > num_regs) num_regs = op->src + 1;
    if (op->src2 != -1 && op->src2 + 1 > num_regs) num_regs = op->src2 + 1;
  }
  return num_regs;
}

void Express::Var::Redirect(Var *other) {
  // Update all consumers to use the other variable.
  for (Op *consumer : consumers) {
    for (int i = 0; i < consumer->args.size(); ++i) {
      if (consumer->args[i] == this) consumer->args[i] = other;
    }
    other->consumers.push_back(consumer);
  }
  consumers.clear();
}

string Express::Var::AsString() const {
  switch (type) {
    case INPUT: return "%" + std::to_string(id);
    case CONST: return "#" + std::to_string(id);
    case OUTPUT: return "@" + std::to_string(id);
    case TEMP:  return "$" + std::to_string(id);
  }
  return "???";
}

void Express::Var::GetRecipe(string *recipe) const {
  char ch;
  switch (type) {
    case INPUT: ch = '%'; break;
    case CONST: ch = '#'; break;
    case OUTPUT: ch = '@'; break;
    case TEMP: ch = '$'; break;
    default: ch = '?';
  }
  recipe->push_back(ch);
  recipe->append(std::to_string(id));
}

string Express::Op::AsString() const {
  string str;
  str.append(OpName(type));
  bool first = true;
  for (auto *arg : args) {
    str.push_back(first ? '(' : ',');
    str.append(arg->AsString());
    first = false;
  }
  str.push_back(')');
  return str;
}

string Express::Op::AsInstruction() const {
  // Opcode.
  string str;
  if (type == MOV) {
    str.append("Mov ");
  } else {
    str.append(OpName(type));
    str.push_back(' ');
  }

  // Destination operand.
  if (dst != -1) {
    str.push_back('r');
    str.append(std::to_string(dst));
  } else {
    result->GetRecipe(&str);
  }

  int first = first_is_dest ? 1 : 0;
  int second = first + 1;

  // Source operand.
  if (src != -1) {
    str.push_back(',');
    str.push_back('r');
    str.append(std::to_string(src));
  } else if (arity() > first) {
    str.push_back(',');
    args[first]->GetRecipe(&str);
  }

  // Second source operand.
  if (src2 != -1) {
    str.push_back(',');
    str.push_back('r');
    str.append(std::to_string(src2));
  } else if (arity() > second) {
    str.push_back(',');
    args[second]->GetRecipe(&str);
  }

  return str;
}

void Express::Op::GetRecipe(string *recipe) const {
  recipe->append(OpName(type));
  recipe->push_back('(');
  bool first = true;
  for (auto *arg : args) {
    if (!first) recipe->push_back(',');
    first = false;
    if (arg->inlined()) {
      arg->producer->GetRecipe(recipe);
    } else {
      arg->GetRecipe(recipe);
    }
  }
  recipe->push_back(')');
}

void Express::Op::Assign(Var *var, bool reassign) {
  // Remove any previous assignment.
  if (result != nullptr) result->producer = nullptr;

  // Set new assignment.
  CHECK(reassign || var->producer == nullptr);
  result = var;
  var->producer = this;
}

void Express::Op::AddArgument(Var *arg) {
  arg->consumers.push_back(this);
  args.push_back(arg);
}

void Express::Op::ClearArguments() {
  // Remove operation as consumer of argument variables.
  for (Var *arg : args) {
    auto f = std::find(arg->consumers.begin(), arg->consumers.end(), this);
    DCHECK(f != arg->consumers.end());
    arg->consumers.erase(f);
  }
  args.clear();
}

bool Express::Op::EqualTo(Op *other) const {
  if (type != other->type) return false;
  if (arity() != other->arity()) return false;
  for (int i = 0; i < args.size(); ++i) {
    if (args[i] != other->args[i]) return false;
  }
  return true;
}

}  // namespace myelin
}  // namespace sling

