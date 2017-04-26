// Copyright 2015 Google Inc. All Rights Reserved.
// Author: ringgaard@google.com (Michael Ringgaard)
//
// SLING function parser.

#ifndef FRAME_PARSER_H_
#define FRAME_PARSER_H_

#include "frame/ast.h"
#include "frame/reader.h"
#include "frame/store.h"

namespace sling {

class Parser : public Reader {
 public:
  Parser(Store *store, Input *input, AST *ast);

  // Function ::
  //   'func' Symbol? FuncExpression
  AST::Function *ParseFunction();

  // Modifiers ::
  //   ( 'private' | 'static' | 'const' )*
  int ParseModifiers();

  // Block ::
  //   '{' Statement* '}'
  AST::Block *ParseBlock();

  // Statement ::
  //   VarStatement |
  //   ReturnStatement ';' |
  //   IfStatement |
  //   WhileStatement |
  //   ForStatement |
  //   Block |
  //   Expression
  AST::Statement *ParseStatement();

  // VarStatement ::
  //   Modifiers 'var' Symbol ( '=' Expression )? |
  //   Modifiers 'func' Symbol FuncExpression
  AST::Statement *ParseVarStatement(int flags);

  // ReturnStatement ::
  //   'return' Expression? ';'
  AST::Statement *ParseReturnStatement();

  // IfStatement ::
  //   'if' '(' Expression ')' Statement ('else' Statement)?
  AST::Statement *ParseIfStatement();

  // ForStatement ::
  //   'for' '(' Expression? ';' Expression? ';' Expression? ')' Statement
  AST::Statement *ParseForStatement();

  // WhileStatement ::
  //   'while' '(' Expression ')' Statement
  AST::Statement *ParseWhileStatement();

  // Expression ::
  //   AssignmentExpression
  //   Expression ',' AssignmentExpression
  AST::Expression *ParseExpression();

  // AssignmentExpression ::
  //   ConditionalExpression |
  //   LeftHandSideExpression AssignmentOperator AssignmentExpression
  // AssignmentOperator ::
  //   '=' | '+=' | '-=' | '*=' | '/=' | '%=' | '&=' | '|=' | '^=' |
  //   '<<=' | '>>=' | '>>>='
  AST::Expression *ParseAssignmentExpression();

  // ConditionalExpression ::
  //   LogicalOrExpression |
  //   LogicalOrExpression '?' AssignmentExpression ':' AssignmentExpression
  AST::Expression *ParseConditionalExpression();

  // LogicalOrExpression ::
  //   LogicalAndExpression |
  //   LogicalOrExpression '||' LogicalAndExpression
  AST::Expression *ParseLogicalOrExpression();

  // LogicalAndExpression ::
  //   BitwiseOrExpression |
  //   LogicalAndExpression '&&' BitwiseOrExpression
  AST::Expression *ParseLogicalAndExpression();

  // BitwiseOrExpression ::
  //   BitwiseXorExpression |
  //   BitwiseOrExpression '|' BitwiseXorExpression
  AST::Expression *ParseBitwiseOrExpression();

  // BitwiseXorExpression ::
  //   BitwiseAndExpression |
  //   BitwiseXorExpression '^' BitwiseAndExpression
  AST::Expression *ParseBitwiseXorExpression();

  // BitwiseAndExpression ::
  //   EqualityExpression |
  //   BitwiseAndExpression '&' EqualityExpression
  AST::Expression *ParseBitwiseAndExpression();

  // EqualityExpression ::
  //   RelationalExpression |
  //   EqualityExpression '==' RelationalExpression |
  //   EqualityExpression '!=' RelationalExpression |
  //   EqualityExpression '===' RelationalExpression |
  //   EqualityExpression '!==' RelationalExpression
  AST::Expression *ParseEqualityExpression();

  // RelationalExpression ::
  //   ShiftExpression |
  //   RelationalExpression '<' ShiftExpression |
  //   RelationalExpression '>' ShiftExpression |
  //   RelationalExpression '<=' ShiftExpression |
  //   RelationalExpression '>=' ShiftExpression |
  //   RelationalExpression 'isa' ShiftExpression |
  //   RelationalExpression 'in' ShiftExpression
  AST::Expression *ParseRelationalExpression();

  // ShiftExpression ::
  //   AdditiveExpression |
  //   ShiftExpression '<<' AdditiveExpression |
  //   ShiftExpression '>>' AdditiveExpression |
  //   ShiftExpression '>>>' AdditiveExpression
  AST::Expression *ParseShiftExpression();

  // AdditiveExpression ::
  //   MultiplicativeExpression |
  //   AdditiveExpression '+' MultiplicativeExpression |
  //   AdditiveExpression '-' MultiplicativeExpression |
  AST::Expression *ParseAdditiveExpression();

  // MultiplicativeExpression ::
  //   UnaryExpression |
  //   MultiplicativeExpression '*' UnaryExpression |
  //   MultiplicativeExpression '/' UnaryExpression |
  //   MultiplicativeExpression '%' UnaryExpression |
  AST::Expression *ParseMultiplicativeExpression();

  // UnaryExpression ::
  //   PostfixExpression |
  //   '++' UnaryExpression |
  //   '--' UnaryExpression |
  //   '+' UnaryExpression |
  //   '-' UnaryExpression |
  //   '~' UnaryExpression |
  //   '!' UnaryExpression
  AST::Expression *ParseUnaryExpression();

  // PostfixExpression ::
  //   LeftHandSideExpression ('++' | '--')?
  AST::Expression *ParsePostfixExpression();

  // LeftHandSideExpression ::
  //   PrimaryExpression |
  //   PrimaryExpression '[' Expression ']' |
  //   PrimaryExpression '(' Arguments? ')' |
  //   PrimaryExpression '.' Symbol
  // Arguments ::
  //   AssignmentExpression | AssignmentExpression ',' Arguments
  //
  AST::Expression *ParseLeftHandSideExpression();

  // PrimaryExpression ::
  //   'this' | 'self' | 'null' | 'true' | 'false' |
  //   Symbol |
  //   Number |
  //   String |
  //   Character |
  //   FunctionExpression |
  //   ArrayLiteral |
  //   ObjectLiteral |
  //   '(' Expression ')'
  AST::Expression *ParsePrimaryExpression();

  // FuncExpression ::
  //   '(' ArgumentList? ')' '{' Statement* '}'
  // ArgumentList ::
  //   Argument | Argument ',' ArgumentList
  // Argument ::
  //   Modifiers Symbol
  AST::Function *ParseFuncExpression();

  AST::Expression *ParseArrayLiteral();
  AST::Expression *ParseFrameLiteral();

 private:
  // Returns symbol for token in token buffer.
  Handle TokenSymbol() {
    return store()->Symbol(token_text());
  }

  class Scope {
   public:
    Scope(Parser *parser, AST::Function *func) : parser_(parser) {
      prev_ = parser->current_scope_;
      parser->current_scope_ = func;
    }
    ~Scope() {
      parser_->current_scope_ = prev_;
    }

   private:
    Parser *parser_;
    AST::Function *prev_;
  };

  AST *ast_;
  AST::Function *current_scope_ = nullptr;
};

}  // namespace sling

#endif  // FRAME_PARSER_H_

