#ifndef MYELIN_EXPRESSION_H_
#define MYELIN_EXPRESSION_H_

#include <map>
#include <string>
#include <vector>

#include "base/logging.h"
#include "base/types.h"

namespace sling {
namespace myelin {

// An expression is a sequence of operations computing output variables from
// input variables using intermediate temporary variables.
class Expression {
 public:
  struct Var;
  struct Op;

  // Variable type.
  enum VarType {INPUT, OUTPUT, TEMP};

  // Variable mapping.
  typedef std::map<Var *, Var *> Map;

  // Variable in expression.
  struct Var {
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
  };

  // Operation in expression.
  struct Op {
    Op(const string &type) : type(type), result(nullptr) {}
    string AsString() const;
    void GetRecipe(string *recipe) const;
    bool EqualTo(Op *other) const;

    // Assign result of operation to variable.
    void Assign(Var *var);

    // Add argument.
    void AddArgument(Var *arg);

    // Remove all arguments.
    void ClearArguments();

    string type;                  // operation type
    Var *result;                  // variable where result is stored
    std::vector<Var *> args;      // operation arguments
  };

  ~Expression();

  // Parse an expression recipe and add it to the expression. A recipe is a
  // sequence of assignment expressions with the following types of variables:
  //   %n: input variable
  //   @n: output variable
  //   $n: temporary variable
  //
  // A recipe has the following grammar:
  //   <recipe> := <assignment> | <assignment> ';' <recipe>
  //   <assignment> := <variable> '=' <expression>
  //   <expression> := <variable> | <operation>
  //   <operation> := <name> '(' <arg list> ')'
  //   <arg list> := <arg> | <arg> ',' <arg list>
  //   <arg> := <variable> | <expression>
  //   <variable> := <input variable> | <output variable> | <temp variable>
  //   <input variable> := '%' <number>
  //   <output variable> := '@' <number>
  //   <temp variable> := '$' <number>
  void Parse(const string &recipe);

  // Return recipe for expression.
  void GetRecipe(string *recipe) const;
  string AsRecipe() const {
    string str;
    GetRecipe(&str);
    return str;
  }

  // Add new operation to expression.
  Op *Operation(const string &type);

  // Lookup variable in expression or add a new variable if it does not exist.
  Var *Variable(VarType type, int id);

  // Add new temp variable to expression.
  Var *NewTemp();

  // Count the number of variable of a certain type.
  int NumVars(VarType type) const;

  // Compact temporary variable ids and return the number of temporary variable.
  int CompactTempVars();

  // Eliminate common subexpressions.
  void EliminateCommonSubexpressions();

  // Merge variable and operations from another expression into this
  // expression. The variables are mapped through the mapping which maps
  // variables in the other expression to variables in this expression.
  void Merge(Expression *other, const Map &varmap);

  // Variables.
  const std::vector<Var *> vars() const { return vars_; }

  // Operations.
  const std::vector<Op *> ops() const { return ops_; }

 private:
  // Try to eliminate identical operations from expression. Return true if any
  // operations were removed.
  bool TryToEliminateOps();

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

#endif  // MYELIN_EXPRESSION_H_

