#include "Parser.h"
#include <sstream>

namespace Script {

Parser::Parser(const std::vector<Token> &tokens, const std::string &filename)
    : _tokens(tokens), _filename(filename), _current(0), _currentProcedure("") {
}

ScriptPtr Parser::parse() {
  auto script = std::make_shared<Script>(_filename);

  while (!isAtEnd()) {
    try {
      auto proc = procedureDeclaration();
      if (proc) {
        script->procedures.push_back(proc);
      }
    } catch (const ParseError &e) {
      // Error already recorded, synchronize and continue
      _errors.push_back(e);
      synchronize();
    }
  }

  return script;
}

bool Parser::isAtEnd() const { return peek().type == TokenType::END_OF_FILE; }

Token Parser::peek() const { return _tokens[_current]; }

Token Parser::previous() const { return _tokens[_current - 1]; }

Token Parser::advance() {
  if (!isAtEnd())
    _current++;
  return previous();
}

bool Parser::check(TokenType type) const {
  if (isAtEnd())
    return false;
  return peek().type == type;
}

bool Parser::match(const std::vector<TokenType> &types) {
  for (TokenType type : types) {
    if (check(type)) {
      advance();
      return true;
    }
  }
  return false;
}

Token Parser::consume(TokenType type, const std::string &message) {
  if (check(type))
    return advance();
  throw error(message);
}

ParseError Parser::error(const std::string &message) {
  Token token = peek();
  std::stringstream ss;
  ss << message << " at line " << token.line << ", column " << token.column;
  if (!_currentProcedure.empty()) {
    ss << " in procedure '" << _currentProcedure << "'";
  }
  return ParseError(ss.str(), token.line, token.column, _currentProcedure);
}

void Parser::synchronize() {
  advance();

  while (!isAtEnd()) {
    if (previous().type == TokenType::SEMICOLON)
      return;
    if (previous().type == TokenType::RBRACE)
      return;

    switch (peek().type) {
    case TokenType::IF:
    case TokenType::WHILE:
    case TokenType::FOR:
    case TokenType::SWITCH:
    case TokenType::DO:
    case TokenType::RETURN:
    case TokenType::INT8:
    case TokenType::UINT8:
    case TokenType::INT16:
    case TokenType::UINT16:
    case TokenType::INT32:
    case TokenType::UINT32:
    case TokenType::INT64:
    case TokenType::UINT64:
    case TokenType::FLOAT:
    case TokenType::DOUBLE:
    case TokenType::STRING:
    case TokenType::BOOL:
    case TokenType::CHAR:
    case TokenType::VOID:
      return;
    default:
      break;
    }

    advance();
  }
}

ProcedureDeclPtr Parser::procedureDeclaration() {
  int line = peek().line;
  int column = peek().column;

  TypeInfo returnType = parseType();

  Token name = consume(TokenType::IDENTIFIER, "Expected procedure name");
  _currentProcedure = name.lexeme;

  consume(TokenType::LPAREN, "Expected '(' after procedure name");
  std::vector<Parameter> params = parameters();
  consume(TokenType::RPAREN, "Expected ')' after parameters");

  consume(TokenType::LBRACE, "Expected '{' before procedure body");
  StmtPtr body = block();

  auto proc = std::make_shared<ProcedureDecl>(returnType, name.lexeme, params,
                                              body, line, column);
  _currentProcedure = "";
  return proc;
}

std::vector<Parameter> Parser::parameters() {
  std::vector<Parameter> params;

  if (!check(TokenType::RPAREN)) {
    do {
      TypeInfo type = parseType();
      Token name = consume(TokenType::IDENTIFIER, "Expected parameter name");
      params.push_back({type, name.lexeme});
    } while (match({TokenType::COMMA}));
  }

  return params;
}

TypeInfo Parser::parseType() {
  DataType base = DataType::VOID;
  if (match({TokenType::INT8}))
    base = DataType::INT8;
  else if (match({TokenType::UINT8}))
    base = DataType::UINT8;
  else if (match({TokenType::INT16}))
    base = DataType::INT16;
  else if (match({TokenType::UINT16}))
    base = DataType::UINT16;
  else if (match({TokenType::INT32}))
    base = DataType::INT32;
  else if (match({TokenType::UINT32}))
    base = DataType::UINT32;
  else if (match({TokenType::INT64}))
    base = DataType::INT64;
  else if (match({TokenType::UINT64}))
    base = DataType::UINT64;
  else if (match({TokenType::FLOAT}))
    base = DataType::FLOAT;
  else if (match({TokenType::DOUBLE}))
    base = DataType::DOUBLE;
  else if (match({TokenType::STRING}))
    base = DataType::STRING;
  else if (match({TokenType::BOOL}))
    base = DataType::BOOL;
  else if (match({TokenType::CHAR}))
    base = DataType::CHAR;
  else if (match({TokenType::VOID}))
    base = DataType::VOID;
  else
    throw error("Expected type");

  bool isArray = false;
  if (match({TokenType::LBRACKET})) {
    consume(TokenType::RBRACKET, "Expected ']' after '[' in type");
    isArray = true;
  }

  return TypeInfo(base, isArray);

  throw error("Expected type name");
}

StmtPtr Parser::statement() {
  if (match({TokenType::IF}))
    return ifStatement();
  if (match({TokenType::WHILE}))
    return whileStatement();
  if (match({TokenType::DO}))
    return doWhileStatement();
  if (match({TokenType::FOR}))
    return forStatement();
  if (match({TokenType::SWITCH}))
    return switchStatement();
  if (match({TokenType::RETURN}))
    return returnStatement();
  if (match({TokenType::BREAK}))
    return breakStatement();
  if (match({TokenType::CONTINUE}))
    return continueStatement();
  if (match({TokenType::LBRACE}))
    return block();

  // Check for variable declaration
  if (check(TokenType::INT8) || check(TokenType::UINT8) ||
      check(TokenType::INT16) || check(TokenType::UINT16) ||
      check(TokenType::INT32) || check(TokenType::UINT32) ||
      check(TokenType::INT64) || check(TokenType::UINT64) ||
      check(TokenType::FLOAT) || check(TokenType::DOUBLE) ||
      check(TokenType::STRING) || check(TokenType::BOOL) ||
      check(TokenType::CHAR)) {
    return varDeclaration();
  }

  return expressionStatement();
}

StmtPtr Parser::varDeclaration() {
  int line = peek().line;
  int column = peek().column;

  TypeInfo type = parseType();
  Token name = consume(TokenType::IDENTIFIER, "Expected variable name");

  ExprPtr initializer = nullptr;
  if (match({TokenType::ASSIGN})) {
    initializer = expression();
  }

  consume(TokenType::SEMICOLON, "Expected ';' after variable declaration");
  return std::make_shared<VarDeclStmt>(type, name.lexeme, initializer, line,
                                       column);
}

StmtPtr Parser::expressionStatement() {
  int line = peek().line;
  int column = peek().column;

  ExprPtr expr = expression();

  // Check for assignment operators
  if (match({TokenType::ASSIGN})) {
    auto varExpr = std::dynamic_pointer_cast<VariableExpr>(expr);
    auto indexExpr = std::dynamic_pointer_cast<IndexExpr>(expr);
    if (!varExpr && !indexExpr) {
      throw error("Invalid assignment target");
    }
    ExprPtr value = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after expression");
    if (varExpr) {
      return std::make_shared<AssignStmt>(
          varExpr->name, value, AssignStmt::Operator::ASSIGN, line, column);
    }
    return std::make_shared<IndexAssignStmt>(indexExpr->arrayExpr,
                                             indexExpr->indexExpr, value,
                                             line, column);
  }

  if (match({TokenType::PLUS_ASSIGN})) {
    auto varExpr = std::dynamic_pointer_cast<VariableExpr>(expr);
    if (!varExpr) {
      throw error("Invalid assignment target");
    }
    ExprPtr value = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after expression");
    return std::make_shared<AssignStmt>(
        varExpr->name, value, AssignStmt::Operator::PLUS_ASSIGN, line, column);
  }

  if (match({TokenType::MINUS_ASSIGN})) {
    auto varExpr = std::dynamic_pointer_cast<VariableExpr>(expr);
    if (!varExpr) {
      throw error("Invalid assignment target");
    }
    ExprPtr value = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after expression");
    return std::make_shared<AssignStmt>(
        varExpr->name, value, AssignStmt::Operator::MINUS_ASSIGN, line, column);
  }

  if (match({TokenType::MULT_ASSIGN})) {
    auto varExpr = std::dynamic_pointer_cast<VariableExpr>(expr);
    if (!varExpr) {
      throw error("Invalid assignment target");
    }
    ExprPtr value = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after expression");
    return std::make_shared<AssignStmt>(
        varExpr->name, value, AssignStmt::Operator::MULT_ASSIGN, line, column);
  }

  if (match({TokenType::DIV_ASSIGN})) {
    auto varExpr = std::dynamic_pointer_cast<VariableExpr>(expr);
    if (!varExpr) {
      throw error("Invalid assignment target");
    }
    ExprPtr value = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after expression");
    return std::make_shared<AssignStmt>(
        varExpr->name, value, AssignStmt::Operator::DIV_ASSIGN, line, column);
  }

  consume(TokenType::SEMICOLON, "Expected ';' after expression");
  return std::make_shared<ExpressionStmt>(expr, line, column);
}

StmtPtr Parser::ifStatement() {
  int line = previous().line;
  int column = previous().column;

  consume(TokenType::LPAREN, "Expected '(' after 'if'");
  ExprPtr condition = expression();
  consume(TokenType::RPAREN, "Expected ')' after if condition");

  StmtPtr thenBranch = statement();
  StmtPtr elseBranch = nullptr;

  if (match({TokenType::ELSE})) {
    elseBranch = statement();
  }

  return std::make_shared<IfStmt>(condition, thenBranch, elseBranch, line,
                                  column);
}

StmtPtr Parser::whileStatement() {
  int line = previous().line;
  int column = previous().column;

  consume(TokenType::LPAREN, "Expected '(' after 'while'");
  ExprPtr condition = expression();
  consume(TokenType::RPAREN, "Expected ')' after while condition");

  StmtPtr body = statement();

  return std::make_shared<WhileStmt>(condition, body, line, column);
}

StmtPtr Parser::doWhileStatement() {
  int line = previous().line;
  int column = previous().column;

  StmtPtr body = statement();

  consume(TokenType::WHILE, "Expected 'while' after do-while body");
  consume(TokenType::LPAREN, "Expected '(' after 'while'");
  ExprPtr condition = expression();
  consume(TokenType::RPAREN, "Expected ')' after while condition");
  consume(TokenType::SEMICOLON, "Expected ';' after do-while statement");

  return std::make_shared<DoWhileStmt>(body, condition, line, column);
}

StmtPtr Parser::forStatement() {
  int line = previous().line;
  int column = previous().column;

  consume(TokenType::LPAREN, "Expected '(' after 'for'");

  // Initializer
  StmtPtr initializer = nullptr;
  if (match({TokenType::SEMICOLON})) {
    initializer = nullptr;
  } else if (check(TokenType::INT8) || check(TokenType::UINT8) ||
             check(TokenType::INT16) || check(TokenType::UINT16) ||
             check(TokenType::INT32) || check(TokenType::UINT32) ||
             check(TokenType::INT64) || check(TokenType::UINT64) ||
             check(TokenType::FLOAT) || check(TokenType::DOUBLE) ||
             check(TokenType::STRING) || check(TokenType::BOOL) ||
             check(TokenType::CHAR)) {
    initializer = varDeclaration();
  } else {
    initializer = expressionStatement();
  }

  // Condition
  ExprPtr condition = nullptr;
  if (!check(TokenType::SEMICOLON)) {
    condition = expression();
  }
  consume(TokenType::SEMICOLON, "Expected ';' after loop condition");

  // Increment
  StmtPtr increment = nullptr;
  if (!check(TokenType::RPAREN)) {
    ExprPtr incrementExpr = expression();

    // Check if it's an assignment
    if (match({TokenType::ASSIGN, TokenType::PLUS_ASSIGN,
               TokenType::MINUS_ASSIGN, TokenType::MULT_ASSIGN,
               TokenType::DIV_ASSIGN})) {
      TokenType opType = previous().type;
      auto varExpr = std::dynamic_pointer_cast<VariableExpr>(incrementExpr);
      if (!varExpr) {
        throw error("Invalid assignment target in for loop");
      }
      ExprPtr value = expression();

      AssignStmt::Operator op;
      switch (opType) {
      case TokenType::ASSIGN:
        op = AssignStmt::Operator::ASSIGN;
        break;
      case TokenType::PLUS_ASSIGN:
        op = AssignStmt::Operator::PLUS_ASSIGN;
        break;
      case TokenType::MINUS_ASSIGN:
        op = AssignStmt::Operator::MINUS_ASSIGN;
        break;
      case TokenType::MULT_ASSIGN:
        op = AssignStmt::Operator::MULT_ASSIGN;
        break;
      case TokenType::DIV_ASSIGN:
        op = AssignStmt::Operator::DIV_ASSIGN;
        break;
      default:
        op = AssignStmt::Operator::ASSIGN;
      }
      increment =
          std::make_shared<AssignStmt>(varExpr->name, value, op, line, column);
    } else {
      increment = std::make_shared<ExpressionStmt>(incrementExpr, line, column);
    }
  }

  consume(TokenType::RPAREN, "Expected ')' after for clauses");

  StmtPtr body = statement();

  return std::make_shared<ForStmt>(initializer, condition, increment, body,
                                   line, column);
}

StmtPtr Parser::switchStatement() {
  int line = previous().line;
  int column = previous().column;

  consume(TokenType::LPAREN, "Expected '(' after 'switch'");
  ExprPtr expr = expression();
  consume(TokenType::RPAREN, "Expected ')' after switch expression");
  consume(TokenType::LBRACE, "Expected '{' after switch expression");

  std::vector<SwitchCase> cases;
  bool seenDefault = false;

  while (!check(TokenType::RBRACE) && !isAtEnd()) {
    if (match({TokenType::CASE})) {
      ExprPtr matchExpr = expression();
      consume(TokenType::COLON, "Expected ':' after case expression");

      std::vector<StmtPtr> stmts;
      while (!check(TokenType::CASE) && !check(TokenType::DEFAULT) &&
             !check(TokenType::RBRACE)) {
        stmts.push_back(statement());
      }

      cases.push_back({matchExpr, stmts, false});
    } else if (match({TokenType::DEFAULT})) {
      consume(TokenType::COLON, "Expected ':' after default");
      if (seenDefault) {
        throw error("Multiple default labels in switch");
      }
      seenDefault = true;

      std::vector<StmtPtr> stmts;
      while (!check(TokenType::CASE) && !check(TokenType::DEFAULT) &&
             !check(TokenType::RBRACE)) {
        stmts.push_back(statement());
      }

      cases.push_back({nullptr, stmts, true});
    } else {
      throw error("Expected 'case' or 'default' in switch statement");
    }
  }

  consume(TokenType::RBRACE, "Expected '}' after switch cases");
  return std::make_shared<SwitchStmt>(expr, cases, line, column);
}

StmtPtr Parser::returnStatement() {
  int line = previous().line;
  int column = previous().column;

  ExprPtr value = nullptr;
  if (!check(TokenType::SEMICOLON)) {
    value = expression();
  }

  consume(TokenType::SEMICOLON, "Expected ';' after return value");
  return std::make_shared<ReturnStmt>(value, line, column);
}

StmtPtr Parser::breakStatement() {
  int line = previous().line;
  int column = previous().column;
  consume(TokenType::SEMICOLON, "Expected ';' after 'break'");
  return std::make_shared<BreakStmt>(line, column);
}

StmtPtr Parser::continueStatement() {
  int line = previous().line;
  int column = previous().column;
  consume(TokenType::SEMICOLON, "Expected ';' after 'continue'");
  return std::make_shared<ContinueStmt>(line, column);
}

StmtPtr Parser::block() {
  int line = previous().line;
  int column = previous().column;

  std::vector<StmtPtr> statements;

  while (!check(TokenType::RBRACE) && !isAtEnd()) {
    statements.push_back(statement());
  }

  consume(TokenType::RBRACE, "Expected '}' after block");
  return std::make_shared<BlockStmt>(statements, line, column);
}

ExprPtr Parser::expression() { return conditional(); }

ExprPtr Parser::conditional() {
  ExprPtr expr = logicalOr();

  if (match({TokenType::QUESTION})) {
    int line = previous().line;
    int column = previous().column;
    ExprPtr thenExpr = expression();
    consume(TokenType::COLON, "Expected ':' in conditional expression");
    ExprPtr elseExpr = conditional();
    return std::make_shared<ConditionalExpr>(expr, thenExpr, elseExpr, line,
                                             column);
  }

  return expr;
}

ExprPtr Parser::logicalOr() {
  ExprPtr expr = logicalAnd();

  while (match({TokenType::OR})) {
    int line = previous().line;
    int column = previous().column;
    ExprPtr right = logicalAnd();
    expr = std::make_shared<BinaryExpr>(
        expr, right, BinaryExpr::Operator::LOGICAL_OR, line, column);
  }

  return expr;
}

ExprPtr Parser::logicalAnd() {
  ExprPtr expr = bitwiseOr();

  while (match({TokenType::AND})) {
    int line = previous().line;
    int column = previous().column;
    ExprPtr right = bitwiseOr();
    expr = std::make_shared<BinaryExpr>(
        expr, right, BinaryExpr::Operator::LOGICAL_AND, line, column);
  }

  return expr;
}

ExprPtr Parser::bitwiseOr() {
  ExprPtr expr = bitwiseXor();

  while (match({TokenType::BIT_OR})) {
    Token op = previous();
    int line = op.line;
    int column = op.column;
    ExprPtr right = bitwiseXor();
    expr = std::make_shared<BinaryExpr>(expr, right, BinaryExpr::Operator::BIT_OR,
                                        line, column);
  }

  return expr;
}

ExprPtr Parser::bitwiseXor() {
  ExprPtr expr = bitwiseAnd();

  while (match({TokenType::BIT_XOR})) {
    Token op = previous();
    int line = op.line;
    int column = op.column;
    ExprPtr right = bitwiseAnd();
    expr = std::make_shared<BinaryExpr>(expr, right, BinaryExpr::Operator::BIT_XOR,
                                        line, column);
  }

  return expr;
}

ExprPtr Parser::bitwiseAnd() {
  ExprPtr expr = shift();

  while (match({TokenType::BIT_AND})) {
    Token op = previous();
    int line = op.line;
    int column = op.column;
    ExprPtr right = shift();
    expr = std::make_shared<BinaryExpr>(expr, right, BinaryExpr::Operator::BIT_AND,
                                        line, column);
  }

  return expr;
}

ExprPtr Parser::shift() {
  ExprPtr expr = equality();

  while (match({TokenType::LSHIFT, TokenType::RSHIFT})) {
    Token op = previous();
    int line = op.line;
    int column = op.column;
    ExprPtr right = equality();

    BinaryExpr::Operator binOp =
        (op.type == TokenType::LSHIFT) ? BinaryExpr::Operator::LSHIFT
                                       : BinaryExpr::Operator::RSHIFT;

    expr = std::make_shared<BinaryExpr>(expr, right, binOp, line, column);
  }

  return expr;
}

ExprPtr Parser::equality() {
  ExprPtr expr = comparison();

  while (match({TokenType::EQUAL, TokenType::NOT_EQUAL})) {
    Token op = previous();
    int line = op.line;
    int column = op.column;
    ExprPtr right = comparison();

    BinaryExpr::Operator binOp = (op.type == TokenType::EQUAL)
                                     ? BinaryExpr::Operator::EQUAL
                                     : BinaryExpr::Operator::NOT_EQUAL;

    expr = std::make_shared<BinaryExpr>(expr, right, binOp, line, column);
  }

  return expr;
}

ExprPtr Parser::comparison() {
  ExprPtr expr = term();

  while (match({TokenType::GREATER_THAN, TokenType::GREATER_EQUAL,
                TokenType::LESS_THAN, TokenType::LESS_EQUAL})) {
    Token op = previous();
    int line = op.line;
    int column = op.column;
    ExprPtr right = term();

    BinaryExpr::Operator binOp;
    switch (op.type) {
    case TokenType::GREATER_THAN:
      binOp = BinaryExpr::Operator::GREATER_THAN;
      break;
    case TokenType::GREATER_EQUAL:
      binOp = BinaryExpr::Operator::GREATER_EQUAL;
      break;
    case TokenType::LESS_THAN:
      binOp = BinaryExpr::Operator::LESS_THAN;
      break;
    case TokenType::LESS_EQUAL:
      binOp = BinaryExpr::Operator::LESS_EQUAL;
      break;
    default:
      binOp = BinaryExpr::Operator::EQUAL;
    }

    expr = std::make_shared<BinaryExpr>(expr, right, binOp, line, column);
  }

  return expr;
}

ExprPtr Parser::term() {
  ExprPtr expr = factor();

  while (match({TokenType::PLUS, TokenType::MINUS})) {
    Token op = previous();
    int line = op.line;
    int column = op.column;
    ExprPtr right = factor();

    BinaryExpr::Operator binOp = (op.type == TokenType::PLUS)
                                     ? BinaryExpr::Operator::ADD
                                     : BinaryExpr::Operator::SUBTRACT;

    expr = std::make_shared<BinaryExpr>(expr, right, binOp, line, column);
  }

  return expr;
}

ExprPtr Parser::factor() {
  ExprPtr expr = unary();

  while (match({TokenType::MULTIPLY, TokenType::DIVIDE, TokenType::MODULO})) {
    Token op = previous();
    int line = op.line;
    int column = op.column;
    ExprPtr right = unary();

    BinaryExpr::Operator binOp;
    switch (op.type) {
    case TokenType::MULTIPLY:
      binOp = BinaryExpr::Operator::MULTIPLY;
      break;
    case TokenType::DIVIDE:
      binOp = BinaryExpr::Operator::DIVIDE;
      break;
    case TokenType::MODULO:
      binOp = BinaryExpr::Operator::MODULO;
      break;
    default:
      binOp = BinaryExpr::Operator::ADD;
    }

    expr = std::make_shared<BinaryExpr>(expr, right, binOp, line, column);
  }

  return expr;
}

ExprPtr Parser::unary() {
  if (match({TokenType::MINUS, TokenType::NOT, TokenType::BIT_NOT})) {
    Token op = previous();
    int line = op.line;
    int column = op.column;
    ExprPtr right = unary();

    UnaryExpr::Operator unOp;
    switch (op.type) {
    case TokenType::MINUS:
      unOp = UnaryExpr::Operator::NEGATE;
      break;
    case TokenType::BIT_NOT:
      unOp = UnaryExpr::Operator::BIT_NOT;
      break;
    default:
      unOp = UnaryExpr::Operator::LOGICAL_NOT;
      break;
    }

    return std::make_shared<UnaryExpr>(right, unOp, line, column);
  }

  return call();
}

ExprPtr Parser::call() {
  ExprPtr expr = primary();

  while (true) {
    if (match({TokenType::LPAREN})) {
      expr = finishCall(expr);
    } else if (match({TokenType::LBRACKET})) {
      expr = finishIndex(expr);
    } else {
      break;
    }
  }

  return expr;
}

ExprPtr Parser::finishCall(ExprPtr callee) {
  auto varExpr = std::dynamic_pointer_cast<VariableExpr>(callee);
  if (!varExpr) {
    throw error("Invalid function call");
  }

  int line = previous().line;
  int column = previous().column;

  std::vector<ExprPtr> arguments;
  if (!check(TokenType::RPAREN)) {
    do {
      arguments.push_back(expression());
    } while (match({TokenType::COMMA}));
  }

  consume(TokenType::RPAREN, "Expected ')' after arguments");

  return std::make_shared<CallExpr>(varExpr->name, arguments, line, column);
}

ExprPtr Parser::finishIndex(ExprPtr callee) {
  int line = previous().line;
  int column = previous().column;
  ExprPtr index = expression();
  consume(TokenType::RBRACKET, "Expected ']' after index expression");
  return std::make_shared<IndexExpr>(callee, index, line, column);
}

ExprPtr Parser::primary() {
  if (match({TokenType::TRUE})) {
    return std::make_shared<LiteralExpr>(true, TypeInfo(DataType::BOOL), previous().line,
                                         previous().column);
  }

  if (match({TokenType::FALSE})) {
    return std::make_shared<LiteralExpr>(false, TypeInfo(DataType::BOOL), previous().line,
                                         previous().column);
  }

  if (match({TokenType::LBRACKET})) {
    int line = previous().line;
    int column = previous().column;
    std::vector<ExprPtr> elements;
    if (!check(TokenType::RBRACKET)) {
      do {
        elements.push_back(expression());
      } while (match({TokenType::COMMA}));
    }
    consume(TokenType::RBRACKET, "Expected ']' after array literal");
    return std::make_shared<ArrayLiteralExpr>(elements, line, column);
  }

  if (match({TokenType::INT_LITERAL})) {
    Token token = previous();
    return std::make_shared<LiteralExpr>(static_cast<int32_t>(token.intValue),
                                         TypeInfo(DataType::INT32), token.line,
                                         token.column);
  }

  if (match({TokenType::FLOAT_LITERAL})) {
    Token token = previous();
    return std::make_shared<LiteralExpr>(token.doubleValue, TypeInfo(DataType::DOUBLE),
                                         token.line, token.column);
  }

  if (match({TokenType::STRING_LITERAL})) {
    Token token = previous();
    return std::make_shared<LiteralExpr>(token.stringValue, TypeInfo(DataType::STRING),
                                         token.line, token.column);
  }

  if (match({TokenType::CHAR_LITERAL})) {
    Token token = previous();
    return std::make_shared<LiteralExpr>(token.charValue, TypeInfo(DataType::CHAR),
                                         token.line, token.column);
  }

  if (match({TokenType::IDENTIFIER})) {
    Token token = previous();
    return std::make_shared<VariableExpr>(token.lexeme, token.line,
                                          token.column);
  }

  if (match({TokenType::LPAREN})) {
    ExprPtr expr = expression();
    consume(TokenType::RPAREN, "Expected ')' after expression");
    return expr;
  }

  throw error("Expected expression");
}

} // namespace Script
