// Copyright 2015 Google Inc. All Rights Reserved.
// Author: ringgaard@google.com (Michael Ringgaard)

#include "sling/frame/parser.h"

#include "sling/frame/ast.h"
#include "sling/frame/store.h"
#include "sling/string/numbers.h"

namespace sling {

Parser::Parser(Store *store, Input *input, AST *ast)
    : Reader(store, input), ast_(ast) {}

AST::Function *Parser::ParseFunction() {
  // Parse 'func' keyword or symbol.
  if (token() == FUNC_TOKEN ||
      (token() == SYMBOL_TOKEN && token_text() == "func")) {
    NextToken();
  } else {
    SetError(StrCat("'func' expected", ", got ", token(), ": ", token_text()));
    return nullptr;
  }

  // Parse function name.
  Handle name = Handle::nil();
  if (token() == SYMBOL_TOKEN) {
    name = TokenSymbol();
    NextToken();
  }

  // Parse function arguments and function body.
  AST::Function *func = ParseFuncExpression();
  if (func == nullptr) return nullptr;

  // Return function AST node.
  func->set_name(name);
  return func;
}

int Parser::ParseModifiers() {
  int flags = 0;
  for (;;) {
    if (token() == PRIVATE_TOKEN) {
      flags |= AST::PRIVATE;
    } else if (token() == STATIC_TOKEN) {
      flags |= AST::STATIC;
    } else if (token() == CONST_TOKEN) {
      flags |= AST::CONST;
    } else {
      break;
    }

    NextToken();
  }
  return flags;
}

AST::Block *Parser::ParseBlock() {
  NextToken();
  AST::Block *block = ast_->NewBlock();
  while (token() != '}') {
    AST::Statement *statement = ParseStatement();
    if (statement == nullptr) return nullptr;
    block->Add(statement);
  }
  NextToken();
  return block;
}

AST::Statement *Parser::ParseStatement() {
  AST::Statement *statement = nullptr;
  switch (token())
  {
    case PRIVATE_TOKEN: case STATIC_TOKEN: case CONST_TOKEN:
    case FUNC_TOKEN: case VAR_TOKEN:
      return ParseVarStatement(0);

    case RETURN_TOKEN:
      statement = ParseReturnStatement();
      break;

    case IF_TOKEN:
      return ParseIfStatement();

    case WHILE_TOKEN:
      return ParseWhileStatement();

    case FOR_TOKEN:
      return ParseForStatement();

    case '{':
      return ParseBlock();

    case ';':
      NextToken();
      return ast_->NewEmpty();

    default:
      AST::Expression *expression = ParseExpression();
      if (expression == nullptr) return nullptr;
      statement = ast_->NewOperation(expression);
  }
  if (statement == nullptr) return nullptr;

  // Check that statement is terminated by semicolon.
  if (token() != ';') {
    SetError("';' missing");
    return nullptr;
  }
  NextToken();

  return statement;
}

AST::Statement *Parser::ParseVarStatement(int flags) {
  // Create new variable.
  AST::Variable *var = ast_->NewVariable();

  // Get variable modifiers.
  var->set_flags(flags | ParseModifiers());

  if (token() == VAR_TOKEN) {
    // Get variable name.
    NextToken();
    if (token() != SYMBOL_TOKEN) {
      SetError("Variable identifier expected");
      return nullptr;
    }

    // Set variable name.
    var->set_name(TokenSymbol());
    NextToken();

    // Parse variable initializer.
    if (token() == '=') {
      NextToken();
      AST::Expression *init = ParseExpression();
      if (init == nullptr) return nullptr;
      var->set_init(init);
      if (init->AsFunction() != nullptr) var->add_flags(AST::FUNC);
    }
  } else if (token() == FUNC_TOKEN) {
    // Get function name.
    NextToken();
    if (token() != SYMBOL_TOKEN) {
      SetError("Function identifier expected");
      return nullptr;
    }

    // Set function name and flags.
    var->add_flags(AST::CONST | AST::FUNC);
    var->set_name(TokenSymbol());
    NextToken();

    // Parse function expression.
    AST::Function *func = ParseFuncExpression();
    if (func == nullptr) return nullptr;
    func->set_name(var->name());
    var->set_init(func);
  }
  else {
    SetError("Syntax error (var or func expected)");
    return nullptr;
  }

  // Add variable to function.
  current_scope_->AddVar(var);
  if (var->init() == nullptr) return ast_->NewEmpty();
  return var;
}

AST::Statement *Parser::ParseReturnStatement() {
  // Parse return statement with optional return expression.
  NextToken();
  if (token() == ';' || token() == -1) return ast_->NewReturn(nullptr);
  AST::Expression *expression = ParseExpression();
  if (expression == nullptr) return nullptr;

  // Create return AST node.
  return ast_->NewReturn(expression);
}

AST::Statement *Parser::ParseIfStatement() {
  // Parse condition.
  NextToken();
  if (token() != '(') {
    SetError("'(' missing in if statement");
    return nullptr;
  }
  NextToken();

  AST::Expression *condition = ParseExpression();
  if (condition == nullptr) return nullptr;

  if (token() != ')') {
    SetError("')' missing in if condition");
    return nullptr;
  }
  NextToken();

  // Parse then statement.
  AST::Statement *body = ParseStatement();
  if (body == nullptr) return nullptr;

  // Parse else statement.
  AST::Statement *otherwise;
  if (token() == ELSE_TOKEN) {
    NextToken();
    otherwise = ParseStatement();
    if (otherwise == nullptr) return nullptr;
  } else {
    otherwise = ast_->NewEmpty();
  }

  // Create if AST node.
  return ast_->NewIf(condition, body, otherwise);
}

AST::Statement *Parser::ParseForStatement() {
  // Parse for loop initialization.
  NextToken();
  if (token() != '(') {
    SetError("'(' missing in for statement");
    return nullptr;
  }
  NextToken();
  AST::Statement *init = nullptr;
  if (token() != ';') {
    if (token() == VAR_TOKEN) {
      init = ParseVarStatement(0);
      if (init == nullptr) return nullptr;
    } else {
      AST::Expression *expression = ParseExpression();
      if (expression == nullptr) return nullptr;
      init = ast_->NewOperation(expression);
    }
  }
  if (token() != ';') {
    SetError("';' missing in for statement");
    return nullptr;
  }
  NextToken();

  // Parse for loop condition.
  AST::Expression *cond = nullptr;
  if (token() != ';') {
    cond = ParseExpression();
    if (cond == nullptr) return nullptr;
  }
  if (token() != ';') {
    SetError("';' missing in for statement");
    return nullptr;
  }
  NextToken();

  // Parse for loop next expression.
  AST::Expression *next = nullptr;
  if (token() != ';') {
    next = ParseExpression();
    if (next == nullptr) return nullptr;
  }
  if (token() != ')') {
    SetError("';' missing in for statement");
    return nullptr;
  }
  NextToken();

  // Parse for statement body.
  AST::Statement *body = ParseStatement();
  if (body == nullptr) return nullptr;

  // Create for loop AST node.
  AST::Loop *loop = ast_->NewLoop(AST::Loop::FOR);
  loop->set_init(init);
  loop->set_cond(cond);
  loop->set_next(next);
  loop->set_body(body);
  return loop;
}

AST::Statement *Parser::ParseWhileStatement() {
  // Parse while loop condition.
  NextToken();
  if (token() != '(') {
    SetError("'(' missing in while statement");
    return nullptr;
  }
  NextToken();
  AST::Expression *cond = ParseExpression();
  if (cond == nullptr) return nullptr;
  if (token() != ')') {
    SetError("')' missing in if statement");
    return nullptr;
  }
  NextToken();

  // Parse while loop body.
  AST::Statement *body = ParseStatement();
  if (body == nullptr) return nullptr;

  // Create while loop AST node.
  AST::Loop *loop = ast_->NewLoop(AST::Loop::WHILE);
  loop->set_cond(cond);
  loop->set_body(body);
  return loop;
}

AST::Expression *Parser::ParseExpression() {
  // Parse expression.
  AST::Expression *result = ParseAssignmentExpression();
  if (result == nullptr) return nullptr;

  // Parse comma-separated list of expressions.
  while (token() == ',') {
    NextToken();
    AST::Expression *right = ParseAssignmentExpression();
    result = ast_->NewBinary(AST::Binary::COMMA, result, right);
  }
  return result;
}

AST::Expression *Parser::ParseAssignmentExpression() {
  // Parse left-hand side expression.
  AST::Expression *expression = ParseConditionalExpression();
  if (expression == nullptr) return nullptr;

  // Parse assignment operator.
  AST::Assignment::Type type;
  switch (token()) {
    case '=': type = AST::Assignment::NOP; break;
    case ASSIGN_MUL_TOKEN: type = AST::Assignment::MUL; break;
    case ASSIGN_DIV_TOKEN: type = AST::Assignment::DIV; break;
    case ASSIGN_MOD_TOKEN: type = AST::Assignment::MOD; break;
    case ASSIGN_ADD_TOKEN: type = AST::Assignment::ADD; break;
    case ASSIGN_SUB_TOKEN: type = AST::Assignment::SUB; break;
    case ASSIGN_SHL_TOKEN: type = AST::Assignment::SHL; break;
    case ASSIGN_SHR_TOKEN: type = AST::Assignment::SHR; break;
    case ASSIGN_SAR_TOKEN: type = AST::Assignment::SAR; break;
    case ASSIGN_BIT_AND_TOKEN: type = AST::Assignment::BIT_AND; break;
    case ASSIGN_BIT_XOR_TOKEN: type = AST::Assignment::BIT_XOR; break;
    case ASSIGN_BIT_OR_TOKEN: type = AST::Assignment::BIT_OR; break;

    default:
      // Parsed conditional expression only (no assignment).
      return expression;
  }
  NextToken();

  // Parse right-hand side expression.
  AST::Expression *right = ParseAssignmentExpression();
  if (right == nullptr) return nullptr;

  // Create assignment AST node.
  return ast_->NewAssignment(type, expression, right);
}

AST::Expression *Parser::ParseConditionalExpression() {
  // Parse (condition) expression.
  AST::Expression *expression = ParseLogicalOrExpression();
  if (expression == nullptr) return nullptr;
  if (token() != '?') return expression;
  NextToken();

  // Parse left expression (condition is true).
  AST::Expression *left = ParseAssignmentExpression();
  if (left == nullptr) return nullptr;
  if (token() != ':') {
    SetError("':' missing in conditional expression");
    return nullptr;
  }
  NextToken();

  // Parse right expression (condition is false).
  AST::Expression *right = ParseAssignmentExpression();
  if (right == nullptr) return nullptr;

  // Create conditional AST node.
  return ast_->NewConditional(expression, left, right);
}

AST::Expression *Parser::ParseLogicalOrExpression() {
  AST::Expression *result = ParseLogicalAndExpression();
  if (result == nullptr) return nullptr;
  while (token() == OR_TOKEN) {
    NextToken();
    AST::Expression *right = ParseLogicalAndExpression();
    if (right == nullptr) return nullptr;
    result = ast_->NewBinary(AST::Binary::OR, result, right);
  }
  return result;
}

AST::Expression *Parser::ParseLogicalAndExpression() {
  AST::Expression *result = ParseBitwiseOrExpression();
  if (result == nullptr) return nullptr;
  while (token() == AND_TOKEN) {
    NextToken();
    AST::Expression *right = ParseBitwiseOrExpression();
    if (right == nullptr) return nullptr;
    result = ast_->NewBinary(AST::Binary::AND, result, right);
  }
  return result;
}

AST::Expression *Parser::ParseBitwiseOrExpression() {
  AST::Expression *result = ParseBitwiseXorExpression();
  if (result == nullptr) return nullptr;
  while (token() == '|') {
    NextToken();
    AST::Expression *right = ParseBitwiseXorExpression();
    if (right == nullptr) return nullptr;
    result = ast_->NewBinary(AST::Binary::BIT_OR, result, right);
  }
  return result;
}

AST::Expression *Parser::ParseBitwiseXorExpression() {
  AST::Expression *result = ParseBitwiseAndExpression();
  if (result == nullptr) return nullptr;
  while (token() == '^') {
    NextToken();
    AST::Expression *right = ParseBitwiseAndExpression();
    if (right == nullptr) return nullptr;
    result = ast_->NewBinary(AST::Binary::BIT_XOR, result, right);
  }
  return result;
}

AST::Expression *Parser::ParseBitwiseAndExpression() {
  AST::Expression *result = ParseEqualityExpression();
  if (result == nullptr) return nullptr;
  while (token() == '&') {
    NextToken();
    AST::Expression *right = ParseEqualityExpression();
    if (right == nullptr) return nullptr;
    result = ast_->NewBinary(AST::Binary::BIT_AND, result, right);
  }
  return result;
}

AST::Expression *Parser::ParseEqualityExpression() {
  AST::Expression *result = ParseRelationalExpression();
  if (result == nullptr) return nullptr;
  for (;;) {
    AST::Compare::Type type;
    switch (token()) {
      case EQ_TOKEN: type = AST::Compare::EQ; break;
      case NE_TOKEN: type = AST::Compare::NOT_EQ; break;
      case EQ_STRICT_TOKEN: type = AST::Compare::EQ_STRICT; break;
      case NE_STRICT_TOKEN: type = AST::Compare::NOT_EQ_STRICT; break;
      default: return result;
    }
    NextToken();

    AST::Expression *right = ParseRelationalExpression();
    if (right == nullptr) return nullptr;
    result = ast_->NewCompare(type, result, right);
  }
}

AST::Expression *Parser::ParseRelationalExpression() {
  AST::Expression *result = ParseShiftExpression();
  if (result == nullptr) return nullptr;
  for (;;) {
    AST::Compare::Type type;
    switch (token()) {
      case '<': type = AST::Compare::LT; break;
      case LTE_TOKEN: type = AST::Compare::LT; break;
      case '>': type = AST::Compare::GT; break;
      case GTE_TOKEN: type = AST::Compare::GTE; break;
      case ISA_TOKEN: type = AST::Compare::ISA; break;
      case IN_TOKEN: type = AST::Compare::IN; break;
      default: return result;
    }
    NextToken();

    AST::Expression *right = ParseShiftExpression();
    if (right == nullptr) return nullptr;
    result = ast_->NewCompare(type, result, right);
  }
}

AST::Expression *Parser::ParseShiftExpression() {
  AST::Expression *result = ParseAdditiveExpression();
  if (result == nullptr) return nullptr;
  for (;;) {
    AST::Binary::Type type;
    switch (token()) {
      case SHL_TOKEN: type = AST::Binary::SHL; break;
      case SHR_TOKEN: type = AST::Binary::SHR; break;
      case SAR_TOKEN: type = AST::Binary::SAR; break;
      default: return result;
    }
    NextToken();
    AST::Expression *right = ParseAdditiveExpression();
    if (right == nullptr) return nullptr;
    result = ast_->NewBinary(type, result, right);
  }
}

AST::Expression *Parser::ParseAdditiveExpression() {
  AST::Expression *result = ParseMultiplicativeExpression();
  if (result == nullptr) return nullptr;
  for (;;) {
    AST::Binary::Type type;
    switch (token()) {
      case '+': type = AST::Binary::ADD; break;
      case '-': type = AST::Binary::SUB; break;
      default: return result;
    }
    NextToken();
    AST::Expression *right = ParseMultiplicativeExpression();
    if (right == nullptr) return nullptr;
    result = ast_->NewBinary(type, result, right);
  }
}

AST::Expression *Parser::ParseMultiplicativeExpression() {
  AST::Expression *result = ParseUnaryExpression();
  if (result == nullptr) return nullptr;
  for (;;) {
    AST::Binary::Type type;
    switch (token()) {
      case '*': type = AST::Binary::MUL; break;
      case '/': type = AST::Binary::DIV; break;
      case '%': type = AST::Binary::MOD; break;
      default: return result;
    }
    NextToken();
    AST::Expression *right = ParseUnaryExpression();
    if (right == nullptr) return nullptr;
    result = ast_->NewBinary(type, result, right);
  }
}

AST::Expression *Parser::ParseUnaryExpression() {
  AST::Unary::Type type;
  switch (token()) {
    case '-': {
      NextToken();
      AST::Expression *expression = ParseUnaryExpression();
      if (expression == nullptr) return nullptr;
      AST::Literal *literal = expression->AsLiteral();
      if (literal != nullptr && literal->value().IsInt()) {
        Handle negative = Handle::Integer(-literal->value().AsInt());
        return ast_->NewLiteral(negative);
      } else {
        return ast_->NewUnary(AST::Unary::NEG, expression);
      }
    }

    case INC_TOKEN: {
      NextToken();
      AST::Expression *expression = ParseUnaryExpression();
      if (expression == nullptr) return nullptr;
      return ast_->NewPrefix(AST::Prefix::INC, expression);
    }

    case DEC_TOKEN: {
      NextToken();
      AST::Expression *expression = ParseUnaryExpression();
      if (expression == nullptr) return nullptr;
      return ast_->NewPrefix(AST::Prefix::DEC, expression);
    }

    case '+': type = AST::Unary::PLUS; break;
    case '~': type = AST::Unary::BIT_NOT; break;
    case '!': type = AST::Unary::NOT; break;

    default:
      return ParsePostfixExpression();
  }

  NextToken();
  AST::Expression *expression = ParseUnaryExpression();
  if (expression == nullptr) return nullptr;
  return ast_->NewUnary(type, expression);
}

AST::Expression *Parser::ParsePostfixExpression() {
  AST::Expression *result = ParseLeftHandSideExpression();
  if (result == nullptr) return nullptr;
  if (token() == INC_TOKEN) {
    NextToken();
    result = ast_->NewPostfix(AST::Postfix::INC, result);
  } else if (token() == DEC_TOKEN) {
    NextToken();
    result = ast_->NewPostfix(AST::Postfix::DEC, result);
  }

  return result;
}

AST::Expression *Parser::ParseLeftHandSideExpression() {
  AST::Expression *result = ParsePrimaryExpression();
  if (result == nullptr) return nullptr;
  for (;;) {
    switch (token()) {
      case '[': {
        NextToken();
        AST::Expression *index = ParseExpression();
        if (index == nullptr) return nullptr;
        result = ast_->NewIndex(result, index);
        if (token() != ']') {
          SetError("']' missing in index expression");
          return nullptr;
        }
        NextToken();
        break;
      }

      case '(': {
        NextToken();
        AST::Call *call = ast_->NewCall(result);
        while (token() != ')') {
          AST::Expression *argument = ParseAssignmentExpression();
          if (argument == nullptr) return nullptr;
          call->AddArg(argument);
          if (token() == ',') {
            NextToken();
          } else if (token() != ')') {
            SetError("Syntax error (')' missing in argument list)");
            return nullptr;
          }
        }
        NextToken();
        result = call;
        break;
      }

      case '.': {
        NextToken();
        if (token() != SYMBOL_TOKEN) {
          SetError("Identifier expected");
          return nullptr;
        }
        result = ast_->NewMember(result, store()->Symbol(token_text()));
        NextToken();
        break;
      }

      default:
        return result;
    }
  }
}

AST::Expression *Parser::ParsePrimaryExpression() {
  AST::Expression *result = nullptr;
  switch (token()) {
    case THIS_TOKEN:
      result = ast_->NewThis();
      NextToken();
      break;

    case SELF_TOKEN:
      result = ast_->NewSelf();
      NextToken();
      break;

    case NULL_TOKEN:
      result = ast_->NewLiteral(Handle::nil());
      NextToken();
      break;

    case TRUE_TOKEN:
      result = ast_->NewLiteral(Handle::Bool(true));
      NextToken();
      break;

    case FALSE_TOKEN:
      result = ast_->NewLiteral(Handle::Bool(false));
      NextToken();
      break;

    case SYMBOL_TOKEN:
      result = ast_->NewAccess(TokenSymbol());
      NextToken();
      break;

    case INTEGER_TOKEN: {
      int32 value;
      CHECK(safe_strto32(token_text(), &value)) << token_text();
      result = ast_->NewLiteral(Handle::Integer(value));
      NextToken();
      break;
    }

    case FLOAT_TOKEN: {
      float value;
      CHECK(safe_strtof(token_text(), &value));
      result = ast_->NewLiteral(Handle::Float(value));
      NextToken();
      break;
    }

    case STRING_TOKEN: {
      result = ast_->NewLiteral(store()->AllocateString(token_text()));
      NextToken();
      break;
    }

    case CHARACTER_TOKEN: {
      int32 value = static_cast<uint8>(token_text()[0]);
      result = ast_->NewLiteral(Handle::Integer(value));
      NextToken();
      break;
    }

    case FUNC_TOKEN:
      NextToken();
      result = ParseFuncExpression();
      if (result == nullptr) return nullptr;
      break;

    case '[':
      result = ParseArrayLiteral();
      if (result == nullptr) return nullptr;
      break;

    case '{':
      result = ParseFrameLiteral();
      if (result == nullptr) return nullptr;
      break;

    case '(':
      NextToken();
      result = ParseExpression();
      if (result == nullptr) return nullptr;
      if (token() != ')') {
        SetError("')' missing in expression");
        return nullptr;
      }
      NextToken();
      break;

    default: {
      SetError(StrCat("Unexpected token: ", token_text()));
      return nullptr;
    }
  }

  return result;
}

AST::Function *Parser::ParseFuncExpression() {
  // Create new function and function scope.
  AST::Function *func = ast_->NewFunction();
  Scope scope(this, func);

  // Parse arguments.
  if (token() != '(') {
    SetError("'(' missing in function arguments");
    return nullptr;
  }
  NextToken();
  while (token() != ')') {
    // Parse formal argument.
    AST::Variable *arg = ast_->NewVariable();
    arg->set_flags(ParseModifiers());
    if (arg->flags() & AST::STATIC) {
      SetError("Argument cannot be static");
      return nullptr;
    }

    if (token() != SYMBOL_TOKEN) {
      SetError("Argument name missing");
      return nullptr;
    }

    arg->set_name(TokenSymbol());
    arg->add_flags(AST::ARG);
    current_scope_->AddArg(arg);
    NextToken();

    if (token() == ',') {
      NextToken();
    } else if (token() != ')') {
      SetError("Syntax error (')' missing in argument list)");
      return nullptr;
    }
  }
  NextToken();

  // Parse body.
  if (token() != '{') {
    SetError("'{' missing in function body");
    return nullptr;
  }
  NextToken();
  AST::Block *body = ast_->NewBlock();
  func->set_body(body);
  while (token() != '}' && token() != -1) {
    AST::Statement *statement = ParseStatement();
    if (statement == nullptr) return nullptr;
    body->Add(statement);
    if (token() == ';') NextToken();
  }
  if (token() != '}') {
    SetError("'}' missing");
    return nullptr;
  }
  NextToken();

  // If the function body is empty, an implicit "return self;" is added.
  if (body->only_vars()) {
    body->Add(ast_->NewReturn(ast_->NewSelf()));
  }

  return func;
}

AST::Expression *Parser::ParseArrayLiteral() {
  SetError("Array literals not yet supported");
  return nullptr;
}

AST::Expression *Parser::ParseFrameLiteral() {
  SetError("Frame literals not yet supported");
  return nullptr;
}

}  // namespace sling

