// Copyright 2015 Google Inc. All Rights Reserved.
// Author: ringgaard@google.com (Michael Ringgaard)

#include "frame/ast.h"

namespace sling {

static void DumpSymbol(Store *store, Handle sym, Output *output) {
  Handle name = store->GetSymbol(sym)->name;
  output->Write(store->GetString(name)->str());
}

AST::~AST() {
  for (Node *n : nodes_) delete n;
}

void AST::Node::Dump(Store *store, Output *output) {
  output->Write("<<ast>>");
}

void AST::Block::Add(Statement *statement) {
  if (statement->AsEmpty() == nullptr) {
    statements_.push_back(statement);
    if (statement->AsVariable() == nullptr) only_vars_ = false;
  }
}

void AST::Block::Dump(Store *store, Output *output) {
  output->Write("{ ");
  for (Statement *statement : statements_) {
    statement->Dump(store, output);
    output->Write(";");
  }
  output->Write(" }");
}

void AST::Variable::Dump(Store *store, Output *output) {
  if (flags_ & PRIVATE) output->Write("private ");
  if (flags_ & STATIC) output->Write("static ");
  if (flags_ & CONST) output->Write("const ");
  if (flags_ & NATIVE) output->Write("native ");
  if (flags_ & ARG) {
    DumpSymbol(store, name_, output);
  } else {
    output->Write("var");
    output->WriteChar(' ');
    DumpSymbol(store, name_, output);
  }
  if (init_ != nullptr) {
    output->Write(" = ");
    init_->Dump(store, output);
  }
  if ((flags_ & ARG) == 0) output->Write("\n");
}

void AST::Function::AddVar(Variable *variable) {
  variables_.push_back(variable);
}

void AST::Function::AddArg(Variable *variable) {
  AddVar(variable);
  num_args_++;
}

void AST::Function::Dump(Store *store, Output *output) {
  output->Write("func");
  if (!name_.IsNil()) {
    output->WriteChar(' ');
    DumpSymbol(store, name_, output);
  }
  output->WriteChar('(');
  for (int i = 0; i < num_args_; ++i) {
    if (i > 0) output->Write(", ");
    variables_[i]->Dump(store, output);
  }
  output->WriteChar(')');

  output->WriteChar(' ');
  if (body_ != nullptr) body_->Dump(store, output);
  output->WriteChar('\n');
}

AST::Empty *AST::NewEmpty() {
  Empty *node  = new Empty();
  nodes_.push_back(node);
  return node;
}

AST::Variable *AST::NewVariable() {
  Variable *node  = new Variable();
  nodes_.push_back(node);
  return node;
}

AST::Block *AST::NewBlock() {
  Block *node  = new Block();
  nodes_.push_back(node);
  return node;
}

AST::Return *AST::NewReturn(Expression *expression) {
  Return *node  = new Return(expression);
  nodes_.push_back(node);
  return node;
}

AST::If *AST::NewIf(Expression *condition,
                    Statement *body,
                    Statement *otherwise) {
  If *node  = new If(condition, body, otherwise);
  nodes_.push_back(node);
  return node;
}

AST::Loop *AST::NewLoop(Loop::Type type) {
  Loop *node  = new Loop(type);
  nodes_.push_back(node);
  return node;
}

AST::Operation *AST::NewOperation(Expression *expression) {
  Operation *node  = new Operation(expression);
  nodes_.push_back(node);
  return node;
}

AST::Function *AST::NewFunction() {
  Function *node  = new Function();
  nodes_.push_back(node);
  return node;
}

AST::Literal *AST::NewLiteral(Handle value) {
  Literal *node  = new Literal(value);
  nodes_.push_back(node);
  return node;
}

AST::Self *AST::NewSelf() {
  Self *node  = new Self();
  nodes_.push_back(node);
  return node;
}

AST::This *AST::NewThis() {
  This *node  = new This();
  nodes_.push_back(node);
  return node;
}

AST::Binary *AST::NewBinary(Binary::Type type,
                            Expression *left,
                            Expression *right) {
  Binary *node  = new Binary(type, left, right);
  nodes_.push_back(node);
  return node;
}

AST::Unary *AST::NewUnary(Unary::Type type, Expression *expression) {
  Unary *node  = new Unary(type, expression);
  nodes_.push_back(node);
  return node;
}

AST::Compare *AST::NewCompare(Compare::Type type,
                              Expression *left,
                              Expression *right) {
  Compare *node  = new Compare(type, left, right);
  nodes_.push_back(node);
  return node;
}

AST::Prefix *AST::NewPrefix(Prefix::Type type, Expression *expression) {
  Prefix *node  = new Prefix(type, expression);
  nodes_.push_back(node);
  return node;
}

AST::Postfix *AST::NewPostfix(Postfix::Type type, Expression *expression) {
  Postfix *node  = new Postfix(type, expression);
  nodes_.push_back(node);
  return node;
}

AST::Assignment *AST::NewAssignment(Assignment::Type type,
                                    Expression *target,
                                    Expression *value) {
  Assignment *node  = new Assignment(type, target, value);
  nodes_.push_back(node);
  return node;
}

AST::Conditional *AST::NewConditional(Expression *condition,
                                      Expression *left,
                                      Expression *right) {
  Conditional *node  = new Conditional(condition, left, right);
  nodes_.push_back(node);
  return node;
}

AST::Member *AST::NewMember(Expression *object, Handle name) {
  Member *node  = new Member(object, name);
  nodes_.push_back(node);
  return node;
}

AST::Index *AST::NewIndex(Expression *object, Expression *index) {
  Index *node  = new Index(object, index);
  nodes_.push_back(node);
  return node;
}

AST::Call *AST::NewCall(Expression *object) {
  Call *node  = new Call(object);
  nodes_.push_back(node);
  return node;
}

AST::Access *AST::NewAccess(Handle name) {
  Access *node  = new Access(name);
  nodes_.push_back(node);
  return node;
}

}  // namespace sling

