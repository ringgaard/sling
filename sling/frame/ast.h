// Copyright 2015 Google Inc. All Rights Reserved.
// Author: ringgaard@google.com (Michael Ringgaard)
//
// Abstract syntax tree (AST) for SLING functions.

#ifndef SLING_FRAME_AST_H_
#define SLING_FRAME_AST_H_

#include <vector>
using std::vector;

#include "sling/frame/store.h"
#include "sling/frame/stream.h"

namespace sling {

class AST {
 public:
  // AST node hierarchy:
  //
  // Node
  //   Statement
  //     Empty
  //     Variable
  //     Block
  //     Return
  //     If
  //     Loop
  //     Operation
  //   Expression
  //     Function
  //     Literal
  //     Self
  //     Binary
  //     Unary
  //     Compare
  //     Prefix
  //     Postfix
  //     Assignment
  //     Conditional
  //     Member
  //     Index
  //     Call
  //     Access

  // Variable modifier flags.
  enum Modifier {
   PRIVATE = 0x0001,  // private variable
   STATIC  = 0x0002,  // static variable
   CONST   = 0x0004,  // constant variable
   FUNC    = 0x0008,  // function
   NATIVE  = 0x0010,  // native function
   ARG     = 0x0020,  // argument
  };

  // Forward declarations.
  class Node;
  class Variable;
  class Function;
  class Expression;
  class Statement;
  class Literal;
  class Access;
  class Empty;

  // Deletes all AST nodes owned by the AST.
  ~AST();

  // The Node class is the root of the AST node hierarchy.
  class Node {
   public:
    virtual ~Node() = default;

    virtual Literal *AsLiteral() { return nullptr; }
    virtual Variable *AsVariable() { return nullptr; }
    virtual Function *AsFunction() { return nullptr; }
    virtual Empty *AsEmpty() { return nullptr; }

    virtual void Dump(Store *store, Output *output);
  };

  // Base class for statement AST nodes.
  class Statement : public Node {
   public:
  };

  // Empty statement node.
  class Empty : public Statement {
   public:
    Empty *AsEmpty() override { return this; }
  };

  // Variable declaration statement.
  class Variable : public Statement {
   public:
    virtual Variable *AsVariable() override { return this; }

    void Dump(Store *store, Output *output) override;

    int flags() const { return flags_; }
    void set_flags(int flags) { flags_ = flags; }
    void add_flags(int flags) { flags_ |= flags; }

    Handle name() const { return name_; }
    void set_name(Handle name) { name_ = name; }

    Expression *init() const { return init_; }
    void set_init(Expression *init) { init_ = init; }

   private:
    int flags_ = 0;
    Handle name_ = Handle::nil();
    Expression *init_ = nullptr;
  };

  // Block statement node.
  class Block : public Statement {
   public:
    // Adds statement to block.
    void Add(Statement *statement);

    void Dump(Store *store, Output *output) override;

    bool only_vars() const { return only_vars_; }

   private:
    vector<Statement *> statements_;
    bool only_vars_ = true;
  };

  // Return statement node.
  class Return : public Statement {
   public:
    explicit Return(Expression *expression) : expression_(expression) {}

   private:
    Expression *expression_ = nullptr;
  };

  // If statement node.
  class If : public Statement {
   public:
    If(Expression *condition, Statement *body, Statement *otherwise)
        : condition_(condition), body_(body), otherwise_(otherwise) {}

   private:
    Expression *condition_;
    Statement *body_;
    Statement *otherwise_;
  };

  // Loop statement node.
  class Loop : public Statement {
   public:
    enum Type { DO, FOR, WHILE };

    explicit Loop(Type type) : type_(type) {}

    Statement *body() const { return body_; }
    void set_body(Statement *body) { body_ = body; }

    Statement *init() const { return init_; }
    void set_init(Statement *init) { init_ = init; }

    Expression *cond() const { return cond_; }
    void set_cond(Expression *cond) { cond_ = cond; }

    Expression *next() const { return next_; }
    void set_next(Expression *next) { next_ = next; }

   public:
    Type type_;
    Statement *body_ = nullptr;
    Statement *init_ = nullptr;
    Expression *cond_ = nullptr;
    Expression *next_ = nullptr;
  };

  // Expression operation node.
  class Operation : public Statement {
   public:
    explicit Operation(Expression *expression) : expression_(expression) {}

   private:
    Expression *expression_;
  };

  // Base class for expression AST nodes.
  class Expression : public Node {
   public:
  };

  // Function node.
  class Function : public Expression {
   public:
    Function *AsFunction() override { return this; }

    void AddVar(Variable *variable);
    void AddArg(Variable *variable);

    void Dump(Store *store, Output *output) override;

    Handle name() const { return name_; }
    void set_name(Handle name) { name_ = name; }

    Block *body() const { return body_; }
    void set_body(Block *body) { body_ = body; }

   private:
    Handle name_ = Handle::nil();
    int num_args_ = 0;
    vector<Variable *> variables_;
    Block *body_ = nullptr;
    Function *inner_ = nullptr;
    Function *outer_ = nullptr;
  };

  // Literal expression node.
  class Literal : public Expression {
   public:
    Literal(Handle value) : value_(value) {}

    Literal *AsLiteral() override { return this; }

    Handle value() const { return value_; }

   private:
    Handle value_;
  };

  // Self expression node :)
  class Self : public Expression {
   public:
  };

  // This expression node.
  class This : public Expression {
   public:
  };

  // Binary operator node.
  class Binary : public Expression {
   public:
    enum Type {
      MUL, DIV, MOD,
      ADD, SUB,
      SHL, SHR, SAR,
      BIT_AND,
      BIT_XOR,
      BIT_OR,
      AND,
      OR,
      COMMA
    };

    Binary(Type type, Expression *left, Expression *right)
        : type_(type), left_(left), right_(right) {}

   private:
    Type type_;
    Expression *left_ = nullptr;
    Expression *right_ = nullptr;
  };

  // Unary operator node.
  class Unary : public Expression {
   public:
    enum Type {
      PLUS, NEG, NOT, BIT_NOT
    };

    Unary(Type type, Expression *expression)
        : type_(type), expression_(expression) {}

   private:
    Type type_;
    Expression *expression_;
  };

  // Comparison operator node.
  class Compare : public Expression {
   public:
    enum Type {
      LT, LTE, GT, GTE, ISA, IN,
      EQ, NOT_EQ, EQ_STRICT, NOT_EQ_STRICT,
    };

    Compare(Type type, Expression *left, Expression *right)
        : type_(type), left_(left), right_(right) {}

   public:
    Type type_;
    Expression *left_;
    Expression *right_;
  };

  // Prefix operator node.
  class Prefix : public Expression {
   public:
    enum Type {
      INC = Binary::ADD,
      DEC = Binary::SUB
    };

    Prefix(Type type, Expression *expression)
        : type_(type), expression_(expression) {}

   private:
    Type type_;
    Expression *expression_;
  };

  // Postfix operator node.
  class Postfix : public Expression {
   public:
    enum Type {
      INC = Binary::ADD,
      DEC = Binary::SUB
    };

    Postfix(Type type, Expression *expression)
        : type_(type), expression_(expression) {}

   public:
    Type type_;
    Expression *expression_ = nullptr;
  };

  // Assignment operator node.
  class Assignment : public Expression {
   public:
    enum Type {
      MUL = Binary::MUL,
      DIV = Binary::DIV,
      MOD = Binary::MOD,
      ADD = Binary::ADD,
      SUB = Binary::SUB,
      SHL = Binary::SHL,
      SHR = Binary::SHR,
      SAR = Binary::SAR,
      BIT_AND = Binary::BIT_AND,
      BIT_XOR = Binary::BIT_XOR,
      BIT_OR  = Binary::BIT_OR,
      NOP
    };

    Assignment(Type type, Expression *target, Expression *value)
        : target_(target), value_(value) {}

   public:
    Type type_;
    Expression *target_;
    Expression *value_;
  };

  // Conditional expression node.
  class Conditional : public Expression {
   public:
    Conditional(Expression *condition, Expression *left, Expression *right)
        : condition_(condition), left_(left), right_(right) {}

   private:
    Expression *condition_;
    Expression *left_;
    Expression *right_;
  };

  // Member access node.
  class Member : public Expression {
   public:
    Member(Expression *object, Handle name)
        : object_(object), name_(name) {}

   private:
    Expression *object_;
    Handle name_;
  };

  // Index access node.
  class Index : public Expression {
   public:
    Index(Expression *object, Expression *index)
        : object_(object), index_(index) {}

   private:
    Expression *object_;
    Expression *index_;
  };

  // Call operation node.
  class Call : public Expression {
   public:
    Call(Expression *object) : object_(object) {}

    void AddArg(Expression *arg) { arguments_.push_back(arg); }

   private:
    Expression *object_;
    vector<Expression *> arguments_;
  };

  // Variable access node.
  class Access : public Expression {
   public:
    Access(Handle name) : name_(name) {}

   private:
    Handle name_;
  };

  // AST node factory methods.
  Empty *NewEmpty();
  Variable *NewVariable();
  Block *NewBlock();
  Return *NewReturn(Expression *expression);
  If *NewIf(Expression *condition, Statement *body, Statement *otherwise);
  Loop *NewLoop(Loop::Type type);
  Operation *NewOperation(Expression *expression);
  Function *NewFunction();
  Literal *NewLiteral(Handle value);
  Self *NewSelf();
  This *NewThis();
  Binary *NewBinary(Binary::Type type, Expression *left, Expression *right);
  Unary *NewUnary(Unary::Type type, Expression *expression);
  Compare *NewCompare(Compare::Type type, Expression *left, Expression *right);
  Prefix *NewPrefix(Prefix::Type type, Expression *expression);
  Postfix *NewPostfix(Postfix::Type type, Expression *expression);
  Assignment *NewAssignment(Assignment::Type type,
                            Expression *target,
                            Expression *value);
  Conditional *NewConditional(Expression *condition,
                              Expression *left,
                              Expression *right);
  Member *NewMember(Expression *object, Handle name);
  Index *NewIndex(Expression *object, Expression *index);
  Call *NewCall(Expression *object);
  Access *NewAccess(Handle name);

 private:
  // All AST nodes as owned by the AST object.
  vector<Node *> nodes_;
};

}  // namespace sling

#endif  // SLING_FRAME_AST_H_
