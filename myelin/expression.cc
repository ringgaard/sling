#include "myelin/expression.h"

#include <algorithm>
#include <string>
#include <vector>

namespace sling {
namespace myelin {

// Recipe parser for converting a string to an expression.
class RecipeParser {
 public:
  // Initialize parser.
  RecipeParser(const string &recipe, Expression *expr) {
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
    Expression::Var *var = ParseVariable();
    if (var->type == Expression::INPUT) {
      Error("Cannot assign to input variable");
    }

    // Consume '='.
    if (current() != '=') Error("Expected '=' in expression");
    next();

    // Parse expression.
    Expression::Op *expr = ParseExpression();

    // Assign expression to variable.
    expr->Assign(var);
  }

  // Parse expression.
  Expression::Op *ParseExpression() {
    // Parse operation name.
    if (!isletter()) Error("Operation name expected in expression");
    const char *start = ptr_;
    while (isletter() || isdigit()) ptr_++;
    string opname(start, ptr_ - start);

    // Parse argument list.
    if (current() != '(') Error("Expected '(' in expression");
    next();
    std::vector<Expression::Var *> args;
    args.push_back(ParseArgument());
    while (current() == ',') {
      next();
      args.push_back(ParseArgument());
    }
    if (current() != ')') Error("Expected ')' in expression");
    next();

    // Create new operation.
    Expression::Op *op = expr_->Operation(opname);
    for (auto *arg : args) op->AddArgument(arg);

    return op;
  }

  // Parse argument.
  Expression::Var *ParseArgument() {
    if (isvar()) {
      // Return variable as argument.
      return ParseVariable();
    } else {
      // Parse expression and assign to intermediate variable. The intermediate
      // variable is assigned a unique negative id which will later be fixed up.
      Expression::Op *expr = ParseExpression();
      Expression::Var *var = expr_->NewTemp();
      expr->Assign(var);
      return var;
    }
  }

  // Parse variable name.
  Expression::Var *ParseVariable() {
    // Parse variable type.
    Expression::VarType type;
    if (is('%')) {
      type = Expression::INPUT;
    } else if (is('@')) {
      type = Expression::OUTPUT;
    } else if (is('$')) {
      type = Expression::TEMP;
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
  Expression *expr_;           // target expression
};

void Expression::Parse(const string &recipe) {
  RecipeParser parser(recipe, this);
  parser.Parse();
}

Expression::~Expression() {
  for (auto *v : vars_) delete v;
  for (auto *o : ops_) delete o;
}

void Expression::GetRecipe(string *recipe) const {
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

Expression::Var *Expression::Variable(VarType type, int id) {
  // Look for existing variable.
  for (Var *v : vars_) {
    if (v->type == type && v->id == id) return v;
  }

  // Add new variable.
  Var *v = new Var(type, id);
  vars_.push_back(v);
  return v;
}

Expression::Op *Expression::Operation(const string &type) {
  Op *op = new Op(type);
  ops_.push_back(op);
  return op;
}

Expression::Var *Expression::NewTemp() {
  // Add new temporary variable.
  Var *v = new Var(TEMP, -1);
  vars_.push_back(v);
  return v;
}

void Expression::RemoveVar(Var *var) {
  // Check that variable is unused.
  DCHECK(var->producer == nullptr);
  DCHECK(var->consumers.empty());

  // Delete variable.
  auto f = std::find(vars_.begin(), vars_.end(), var);
  DCHECK(f != vars_.end());
  vars_.erase(f);
  delete var;
}

void Expression::RemoveOp(Op *op) {
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

int Expression::NumVars(VarType type) const {
  int n = 0;
  for (Var *v : vars_) {
    if (v->type == type) n++;
  }
  return n;
}

int Expression::CompactTempVars() {
  int n = 0;
  for (Var *v : vars_) {
    if (v->type == TEMP) v->id = n++;
  }
  return n;
}

void Expression::EliminateCommonSubexpressions() {
  // Keep trying to eliminate ops until no more can be removed.
  int iterations = 0;
  while (TryToEliminateOps()) iterations++;

  // Compact temporary variables if some operations were eliminated.
  if (iterations > 0) CompactTempVars();
}

bool Expression::TryToEliminateOps() {
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
          // Two output variable. Change second op to identity function.
          v2->Redirect(v1);
          op2->type = "Identity";
          op2->ClearArguments();
          op2->AddArgument(v1);
          return true;
        }
      }
    }
  }
  return false;
}

void Expression::Merge(Expression *other, const Map &varmap) {
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

void Expression::Var::Redirect(Var *other) {
  // Update all consumers to use the other variable.
  for (Op *consumer : consumers) {
    for (int i = 0; i < consumer->args.size(); ++i) {
      if (consumer->args[i] == this) consumer->args[i] = other;
    }
    other->consumers.push_back(consumer);
  }
  consumers.clear();
}

string Expression::Var::AsString() const {
  switch (type) {
    case INPUT: return "%" + std::to_string(id);
    case OUTPUT: return "@" + std::to_string(id);
    case TEMP:  return "$" + std::to_string(id);
  }
  return "???";
}

void Expression::Var::GetRecipe(string *recipe) const {
  char ch;
  switch (type) {
    case INPUT: ch = '%'; break;
    case OUTPUT: ch = '@'; break;
    case TEMP: ch = '$'; break;
    default: ch = '?';
  }
  recipe->push_back(ch);
  recipe->append(std::to_string(id));
}

string Expression::Op::AsString() const {
  string str;
  str.append(type);
  bool first = true;
  for (auto *arg : args) {
    str.push_back(first ? '(' : ',');
    str.append(arg->AsString());
    first = false;
  }
  str.push_back(')');
  return str;
}

void Expression::Op::GetRecipe(string *recipe) const {
  recipe->append(type);
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

void Expression::Op::Assign(Var *var) {
  // Remove any previous assignment.
  if (result != nullptr) result->producer = nullptr;

  // Set new assignment.
  CHECK(var->producer == nullptr);
  result = var;
  var->producer = this;
}

void Expression::Op::AddArgument(Var *arg) {
  arg->consumers.push_back(this);
  args.push_back(arg);
}

void Expression::Op::ClearArguments() {
  // Remove operation as consumer of argument variables.
  for (Var *arg : args) {
    auto f = std::find(arg->consumers.begin(), arg->consumers.end(), this);
    DCHECK(f != arg->consumers.end());
    arg->consumers.erase(f);
  }
  args.clear();
}

bool Expression::Op::EqualTo(Op *other) const {
  if (type != other->type) return false;
  if (args.size() != other->args.size()) return false;
  for (int i = 0; i < args.size(); ++i) {
    if (args[i] != other->args[i]) return false;
  }
  return true;
}

}  // namespace myelin
}  // namespace sling

