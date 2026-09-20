#include "Parser.h"
#include <sstream>

namespace Script {

namespace {

bool isTypeToken(TokenType t) {
  switch (t) {
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
    return true;
  default:
    return false;
  }
}

AssignStmt::Operator assignOpFor(TokenType t) {
  switch (t) {
  case TokenType::PLUS_ASSIGN:
    return AssignStmt::Operator::PLUS_ASSIGN;
  case TokenType::MINUS_ASSIGN:
    return AssignStmt::Operator::MINUS_ASSIGN;
  case TokenType::MULT_ASSIGN:
    return AssignStmt::Operator::MULT_ASSIGN;
  case TokenType::DIV_ASSIGN:
    return AssignStmt::Operator::DIV_ASSIGN;
  case TokenType::MOD_ASSIGN:
    return AssignStmt::Operator::MOD_ASSIGN;
  case TokenType::AND_ASSIGN:
    return AssignStmt::Operator::BAND_ASSIGN;
  case TokenType::OR_ASSIGN:
    return AssignStmt::Operator::BOR_ASSIGN;
  case TokenType::XOR_ASSIGN:
    return AssignStmt::Operator::BXOR_ASSIGN;
  case TokenType::LSHIFT_ASSIGN:
    return AssignStmt::Operator::SHL_ASSIGN;
  case TokenType::RSHIFT_ASSIGN:
    return AssignStmt::Operator::SHR_ASSIGN;
  default:
    return AssignStmt::Operator::ASSIGN;
  }
}

bool isAssignTarget(const ExprPtr &expr) {
  return std::dynamic_pointer_cast<VariableExpr>(expr) != nullptr ||
         std::dynamic_pointer_cast<IndexExpr>(expr) != nullptr ||
         std::dynamic_pointer_cast<MemberExpr>(expr) != nullptr;
}

} // namespace

Parser::Parser(const std::vector<Token> &tokens, const std::string &filename,
               const std::unordered_set<std::string> &knownStructs,
               const std::unordered_set<std::string> &knownEnums)
    : _tokens(tokens), _filename(filename), _current(0), _currentProcedure(""),
      _structNames(knownStructs), _enumNames(knownEnums) {
}

ScriptPtr Parser::parse() {
  auto script = std::make_shared<Script>(_filename);

  // Pre-scan for `struct Name` / `enum Name` so type positions can reference
  // a declaration that appears later in the same file.
  for (size_t i = 0; i + 1 < _tokens.size(); ++i) {
    if (_tokens[i].type == TokenType::STRUCT &&
        _tokens[i + 1].type == TokenType::IDENTIFIER) {
      _structNames.insert(_tokens[i + 1].lexeme);
    }
    if (_tokens[i].type == TokenType::ENUM &&
        _tokens[i + 1].type == TokenType::IDENTIFIER) {
      _enumNames.insert(_tokens[i + 1].lexeme);
    }
  }

  while (!isAtEnd()) {
    try {
      if (match({TokenType::IMPORT})) {
        Token path = consume(TokenType::STRING_LITERAL,
                             "Expected string path after 'import'");
        consume(TokenType::SEMICOLON, "Expected ';' after import");
        script->imports.emplace_back(path.stringValue, path.line);
        continue;
      }
      if (check(TokenType::STRUCT)) {
        auto decl = structDeclaration();
        if (decl) {
          script->structs.push_back(decl);
        }
        continue;
      }
      if (check(TokenType::ENUM)) {
        auto decl = enumDeclaration();
        if (decl) {
          script->enums.push_back(decl);
        }
        continue;
      }
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

ExprPtr Parser::parseExpression() {
  ExprPtr expr = expression();
  if (peek().type != TokenType::END_OF_FILE) {
    throw error("Expected end of expression");
  }
  return expr;
}

std::vector<StmtPtr> Parser::parseStatements() {
  std::vector<StmtPtr> stmts;
  while (!isAtEnd()) {
    stmts.push_back(statement());
  }
  return stmts;
}

bool Parser::isAtEnd() const { return peek().type == TokenType::END_OF_FILE; }

Token Parser::peek() const { return _tokens[_current]; }

Token Parser::peekAt(size_t offset) const {
  size_t idx = _current + offset;
  if (idx >= _tokens.size()) {
    return _tokens.back(); // END_OF_FILE token
  }
  return _tokens[idx];
}

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
    case TokenType::STRUCT:
    case TokenType::ENUM:
    case TokenType::TRY:
    case TokenType::THROW:
    case TokenType::CONST:
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
    bool sawDefault = false;
    do {
      TypeInfo type = parseType();
      Token name = consume(TokenType::IDENTIFIER, "Expected parameter name");
      Parameter p{type, name.lexeme, nullptr};
      if (match({TokenType::ASSIGN})) {
        p.defaultValue = expression();
        sawDefault = true;
      } else if (sawDefault) {
        throw error("Parameter '" + name.lexeme +
                    "' requires a default value: defaults must be trailing");
      }
      params.push_back(std::move(p));
    } while (match({TokenType::COMMA}));
  }

  return params;
}

Parameter Parser::lambdaParameter() {
  // Bare `name` => auto-typed lambda parameter.
  if (check(TokenType::IDENTIFIER) &&
      peek().lexeme != "map" && peek().lexeme != "fn" &&
      _structNames.count(peek().lexeme) == 0 &&
      (peekAt(1).type == TokenType::COMMA ||
       peekAt(1).type == TokenType::RPAREN ||
       peekAt(1).type == TokenType::ASSIGN)) {
    Token name = advance();
    Parameter p{TypeInfo::autoType(), name.lexeme, nullptr};
    if (match({TokenType::ASSIGN})) {
      p.defaultValue = expression();
    }
    return p;
  }

  TypeInfo type = parseType();
  Token name = consume(TokenType::IDENTIFIER, "Expected parameter name");
  Parameter p{type, name.lexeme, nullptr};
  if (match({TokenType::ASSIGN})) {
    p.defaultValue = expression();
  }
  return p;
}

StructDeclPtr Parser::structDeclaration() {
  Token kw = consume(TokenType::STRUCT, "Expected 'struct'");
  Token name =
      consume(TokenType::IDENTIFIER, "Expected struct name after 'struct'");
  consume(TokenType::LBRACE, "Expected '{' after struct name");

  std::vector<Parameter> fields;
  auto decl = std::make_shared<StructDecl>(name.lexeme, fields, kw.line,
                                           kw.column);
  while (!check(TokenType::RBRACE) && !isAtEnd()) {
    int memberLine = peek().line;
    int memberCol = peek().column;
    TypeInfo memberType = parseType();
    Token memberName =
        consume(TokenType::IDENTIFIER, "Expected member name in struct");
    if (check(TokenType::LPAREN)) {
      // Method: `retType name(params) { body }`
      advance();
      std::vector<Parameter> params = parameters();
      consume(TokenType::RPAREN, "Expected ')' after method parameters");
      consume(TokenType::LBRACE, "Expected '{' before method body");
      std::string savedProc = _currentProcedure;
      _currentProcedure = name.lexeme + "." + memberName.lexeme;
      StmtPtr body = block();
      _currentProcedure = savedProc;
      decl->methods.push_back(std::make_shared<ProcedureDecl>(
          memberType, memberName.lexeme, params, body, memberLine,
          memberCol));
    } else {
      consume(TokenType::SEMICOLON, "Expected ';' after struct field");
      decl->fields.push_back({memberType, memberName.lexeme, nullptr});
    }
  }
  consume(TokenType::RBRACE, "Expected '}' after struct body");
  match({TokenType::SEMICOLON}); // optional trailing semicolon

  return decl;
}

EnumDeclPtr Parser::enumDeclaration() {
  Token kw = consume(TokenType::ENUM, "Expected 'enum'");
  Token name =
      consume(TokenType::IDENTIFIER, "Expected enum name after 'enum'");
  consume(TokenType::LBRACE, "Expected '{' after enum name");

  std::vector<std::pair<std::string, int64_t>> members;
  int64_t next = 0;
  while (!check(TokenType::RBRACE) && !isAtEnd()) {
    Token member = consume(TokenType::IDENTIFIER, "Expected enum member name");
    if (match({TokenType::ASSIGN})) {
      bool neg = match({TokenType::MINUS});
      Token value =
          consume(TokenType::INT_LITERAL, "Expected integer value in enum");
      next = neg ? -value.intValue : value.intValue;
    }
    members.emplace_back(member.lexeme, next);
    ++next;
    if (!check(TokenType::RBRACE)) {
      consume(TokenType::COMMA, "Expected ',' or '}' in enum declaration");
    } else {
      match({TokenType::COMMA}); // optional trailing comma
    }
  }
  consume(TokenType::RBRACE, "Expected '}' after enum body");
  match({TokenType::SEMICOLON}); // optional trailing semicolon

  return std::make_shared<EnumDecl>(name.lexeme, std::move(members), kw.line,
                                    kw.column);
}

TypeInfo Parser::parseType() {
  TypeInfo t = parseBaseType();

  // Any number of trailing `[]` pairs makes this an array type (possibly
  // nested): `int32[][]`, `map<string,int32>[]`, `Point[][]`.
  while (check(TokenType::LBRACKET) &&
         peekAt(1).type == TokenType::RBRACKET) {
    advance();
    advance();
    if (t.isAuto) {
      throw error("'auto' cannot have an array suffix");
    }
    t = TypeInfo::arrayOf(t);
  }
  return t;
}

TypeInfo Parser::parseBaseType() {
  // map<K, V> — 'map' is a contextual identifier, not a keyword
  if (check(TokenType::IDENTIFIER) && peek().lexeme == "map" &&
      peekAt(1).type == TokenType::LESS_THAN) {
    advance(); // 'map'
    advance(); // '<'
    TypeInfo keyType = parseType();
    if (keyType.isArray || keyType.isMap || keyType.isFunction ||
        keyType.isAuto || keyType.baseType == DataType::VOID) {
      throw error("Map key type must be a scalar or struct type");
    }
    consume(TokenType::COMMA, "Expected ',' in map<K, V>");
    TypeInfo valueType = parseType();
    if (valueType.baseType == DataType::VOID && !valueType.isStruct) {
      throw error("Map value type cannot be void");
    }
    consumeGreaterThan();
    return TypeInfo::mapOf(keyType, valueType);
  }

  // auto — inferred from the initializer at declaration time
  if (check(TokenType::IDENTIFIER) && peek().lexeme == "auto") {
    advance();
    return TypeInfo::autoType();
  }

  // fn(T1, T2) -> R — function type annotation for parameters/variables
  if (check(TokenType::IDENTIFIER) && peek().lexeme == "fn" &&
      peekAt(1).type == TokenType::LPAREN) {
    advance(); // 'fn'
    advance(); // '('
    std::vector<TypeInfo> paramTypes;
    if (!check(TokenType::RPAREN)) {
      do {
        paramTypes.push_back(parseType());
      } while (match({TokenType::COMMA}));
    }
    consume(TokenType::RPAREN, "Expected ')' in function type");
    std::shared_ptr<TypeInfo> ret;
    if (check(TokenType::MINUS) &&
        peekAt(1).type == TokenType::GREATER_THAN) {
      advance(); // '-'
      advance(); // '>'
      ret = std::make_shared<TypeInfo>(parseType());
    }
    return TypeInfo::functionOf(std::move(paramTypes), ret);
  }

  // User-declared struct type, e.g. `Point` or `Point[]`
  if (check(TokenType::IDENTIFIER) &&
      _structNames.count(peek().lexeme) > 0) {
    std::string name = peek().lexeme;
    advance();
    return TypeInfo::structOf(name);
  }

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

  return TypeInfo(base, false);
}

Token Parser::consumeGreaterThan() {
  if (check(TokenType::GREATER_THAN))
    return advance();
  // '>>' splits into two '>' for nested generics like map<K, map<K,V>>
  if (check(TokenType::RSHIFT)) {
    _tokens[_current].type = TokenType::GREATER_THAN;
    _tokens[_current].lexeme = ">";
    return _tokens[_current]; // don't advance — the second '>' is next
  }
  // '>>=' splits into '>' + '>=' for triple-nested generics
  if (check(TokenType::RSHIFT_ASSIGN)) {
    _tokens[_current].type = TokenType::GREATER_EQUAL;
    _tokens[_current].lexeme = ">=";
    return _tokens[_current];
  }
  throw error("Expected '>' after type arguments");
}

int Parser::mapTypeEnd(size_t offset) const {
  size_t base = _current + offset;
  if (base + 1 >= _tokens.size() ||
      _tokens[base].type != TokenType::IDENTIFIER ||
      _tokens[base].lexeme != "map" ||
      _tokens[base + 1].type != TokenType::LESS_THAN)
    return -1;
  int depth = 0;
  for (size_t i = base + 1; i < _tokens.size(); ++i) {
    switch (_tokens[i].type) {
    case TokenType::LESS_THAN:
      depth++;
      break;
    case TokenType::GREATER_THAN:
      depth--;
      break;
    case TokenType::RSHIFT:
      depth -= 2;
      break;
    case TokenType::RSHIFT_ASSIGN:
      depth -= 3;
      break;
    default:
      break;
    }
    if (depth <= 0)
      return static_cast<int>(i - _current + 1);
  }
  return -1;
}

int Parser::typeEnd(size_t offset) const {
  size_t i = _current + offset;
  if (i >= _tokens.size())
    return -1;
  const Token &t = _tokens[i];

  if (t.type == TokenType::IDENTIFIER && t.lexeme == "auto") {
    ++i;
  } else if (t.type == TokenType::IDENTIFIER && t.lexeme == "map" &&
             i + 1 < _tokens.size() &&
             _tokens[i + 1].type == TokenType::LESS_THAN) {
    int end = mapTypeEnd(offset);
    if (end < 0)
      return -1;
    i = _current + static_cast<size_t>(end);
  } else if (t.type == TokenType::IDENTIFIER && t.lexeme == "fn" &&
             i + 1 < _tokens.size() &&
             _tokens[i + 1].type == TokenType::LPAREN) {
    // fn(T1, T2) -> R
    int depth = 0;
    size_t j = i + 1;
    for (; j < _tokens.size(); ++j) {
      if (_tokens[j].type == TokenType::LPAREN)
        ++depth;
      else if (_tokens[j].type == TokenType::RPAREN && --depth == 0)
        break;
    }
    if (j >= _tokens.size())
      return -1;
    i = j + 1;
    if (i + 1 < _tokens.size() && _tokens[i].type == TokenType::MINUS &&
        _tokens[i + 1].type == TokenType::GREATER_THAN) {
      int e = typeEnd(i + 2 - _current);
      if (e < 0)
        return -1;
      i = _current + static_cast<size_t>(e);
    }
  } else if (isTypeToken(t.type) || t.type == TokenType::VOID ||
             (t.type == TokenType::IDENTIFIER &&
              _structNames.count(t.lexeme) > 0)) {
    ++i;
  } else {
    return -1;
  }

  // trailing [] pairs
  while (i + 1 < _tokens.size() &&
         _tokens[i].type == TokenType::LBRACKET &&
         _tokens[i + 1].type == TokenType::RBRACKET) {
    i += 2;
  }
  return static_cast<int>(i - _current);
}

bool Parser::isDeclStart(size_t offset) const {
  if (_current + offset >= _tokens.size())
    return false;
  TokenType t = _tokens[_current + offset].type;
  if (t == TokenType::CONST)
    return isDeclStart(offset + 1);
  if (isTypeToken(t))
    return true;
  int end = typeEnd(offset);
  return end > 0 && _current + static_cast<size_t>(end) < _tokens.size() &&
         _tokens[_current + static_cast<size_t>(end)].type ==
             TokenType::IDENTIFIER;
}

bool Parser::lambdaAhead() const {
  // Called after the `fn` identifier has been consumed, with _current at
  // the '('. Decides whether this is `fn ( params ) [-> type] {` rather
  // than a call to a procedure named fn.
  if (!check(TokenType::LPAREN))
    return false;

  int depth = 0;
  size_t i = _current;
  for (; i < _tokens.size(); ++i) {
    if (_tokens[i].type == TokenType::LPAREN)
      ++depth;
    else if (_tokens[i].type == TokenType::RPAREN && --depth == 0)
      break;
  }
  if (i >= _tokens.size())
    return false;
  ++i;

  // optional `-> returnType`
  if (i + 1 < _tokens.size() && _tokens[i].type == TokenType::MINUS &&
      _tokens[i + 1].type == TokenType::GREATER_THAN) {
    int end = typeEnd(i + 2 - _current);
    if (end < 0)
      return false;
    i = _current + static_cast<size_t>(end);
  }
  return i < _tokens.size() && _tokens[i].type == TokenType::LBRACE;
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
  if (match({TokenType::TRY}))
    return tryCatchStatement();
  if (match({TokenType::THROW}))
    return throwStatement();
  if (match({TokenType::LBRACE}))
    return block();

  // Check for variable declaration (optionally const-qualified)
  if (isDeclStart(0)) {
    return varDeclaration();
  }

  return expressionStatement();
}

StmtPtr Parser::varDeclaration() {
  int line = peek().line;
  int column = peek().column;

  bool isConst = match({TokenType::CONST});
  TypeInfo type = parseType();
  Token name = consume(TokenType::IDENTIFIER, "Expected variable name");

  ExprPtr initializer = nullptr;
  if (match({TokenType::ASSIGN})) {
    initializer = expression();
  }
  if (isConst && !initializer) {
    throw error("Const variable '" + name.lexeme + "' requires an initializer");
  }
  if (type.isAuto && !initializer) {
    throw error("'auto' variable '" + name.lexeme +
                "' requires an initializer");
  }

  consume(TokenType::SEMICOLON, "Expected ';' after variable declaration");
  return std::make_shared<VarDeclStmt>(type, name.lexeme, initializer, line,
                                       column, isConst);
}

StmtPtr Parser::expressionStatement() {
  int line = peek().line;
  int column = peek().column;

  ExprPtr expr = expression();

  // Check for assignment operators
  if (match({TokenType::ASSIGN, TokenType::PLUS_ASSIGN, TokenType::MINUS_ASSIGN,
             TokenType::MULT_ASSIGN, TokenType::DIV_ASSIGN,
             TokenType::MOD_ASSIGN, TokenType::AND_ASSIGN, TokenType::OR_ASSIGN,
             TokenType::XOR_ASSIGN, TokenType::LSHIFT_ASSIGN,
             TokenType::RSHIFT_ASSIGN})) {
    TokenType opType = previous().type;
    auto varExpr = std::dynamic_pointer_cast<VariableExpr>(expr);
    auto indexExpr = std::dynamic_pointer_cast<IndexExpr>(expr);
    auto memberExpr = std::dynamic_pointer_cast<MemberExpr>(expr);
    if (!varExpr && !indexExpr && !memberExpr) {
      throw error("Invalid assignment target");
    }
    ExprPtr value = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after expression");
    AssignStmt::Operator op = assignOpFor(opType);
    if (varExpr) {
      return std::make_shared<AssignStmt>(varExpr->name, value, op, line,
                                          column);
    }
    if (memberExpr) {
      return std::make_shared<MemberAssignStmt>(memberExpr->object,
                                                memberExpr->member, value, op,
                                                line, column);
    }
    return std::make_shared<IndexAssignStmt>(indexExpr->arrayExpr,
                                             indexExpr->indexExpr, value, op,
                                             line, column);
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

  // For-each: for ([const] type name : iterable) body
  bool elemConst = false;
  size_t typeOff = 0;
  if (check(TokenType::CONST) && isDeclStart(1)) {
    elemConst = true;
    typeOff = 1;
  }
  bool scalarOrStructType = isTypeToken(peekAt(typeOff).type) ||
                            (peekAt(typeOff).type == TokenType::IDENTIFIER &&
                             (_structNames.count(peekAt(typeOff).lexeme) > 0 ||
                              peekAt(typeOff).lexeme == "auto"));
  bool foreachType = scalarOrStructType &&
                     peekAt(typeOff + 1).type == TokenType::IDENTIFIER &&
                     peekAt(typeOff + 2).type == TokenType::COLON;
  if (!foreachType) {
    // Complex element types (arrays, maps, nested containers): locate the
    // end of the type, then check for `name :`.
    int end = typeEnd(typeOff);
    if (end > 0) {
      size_t e = _current + static_cast<size_t>(end);
      foreachType = e + 1 < _tokens.size() &&
                    _tokens[e].type == TokenType::IDENTIFIER &&
                    _tokens[e + 1].type == TokenType::COLON;
    }
  }
  if (foreachType) {
    if (elemConst)
      advance(); // 'const'
    TypeInfo elemType = parseType();
    Token name =
        consume(TokenType::IDENTIFIER, "Expected for-each variable name");
    consume(TokenType::COLON, "Expected ':' in for-each loop");
    ExprPtr iterable = expression();
    consume(TokenType::RPAREN, "Expected ')' after for-each clauses");
    StmtPtr body = statement();
    return std::make_shared<ForEachStmt>(elemType, name.lexeme, iterable, body,
                                         line, column, elemConst);
  }

  // Initializer
  StmtPtr initializer = nullptr;
  if (match({TokenType::SEMICOLON})) {
    initializer = nullptr;
  } else if (isDeclStart(0)) {
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
               TokenType::DIV_ASSIGN, TokenType::MOD_ASSIGN,
               TokenType::AND_ASSIGN, TokenType::OR_ASSIGN,
               TokenType::XOR_ASSIGN, TokenType::LSHIFT_ASSIGN,
               TokenType::RSHIFT_ASSIGN})) {
      TokenType opType = previous().type;
      auto varExpr = std::dynamic_pointer_cast<VariableExpr>(incrementExpr);
      if (!varExpr) {
        throw error("Invalid assignment target in for loop");
      }
      ExprPtr value = expression();

      increment = std::make_shared<AssignStmt>(varExpr->name, value,
                                               assignOpFor(opType), line,
                                               column);
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

StmtPtr Parser::tryCatchStatement() {
  int line = previous().line;
  int column = previous().column;

  consume(TokenType::LBRACE, "Expected '{' after 'try'");
  StmtPtr tryBlock = block();

  TypeInfo catchType = TypeInfo::autoType();
  std::string catchVar;
  StmtPtr catchBlock = nullptr;
  if (match({TokenType::CATCH})) {
    if (match({TokenType::LPAREN})) {
      // `catch (e)` binds the thrown value as-is; `catch (int64 e)`
      // converts it to the declared type.
      if (!isDeclStart(0)) {
        Token var = consume(TokenType::IDENTIFIER,
                            "Expected variable name in catch clause");
        catchVar = var.lexeme;
      } else {
        catchType = parseType();
        Token var = consume(TokenType::IDENTIFIER,
                            "Expected variable name in catch clause");
        catchVar = var.lexeme;
      }
      consume(TokenType::RPAREN, "Expected ')' after catch variable");
    }
    consume(TokenType::LBRACE, "Expected '{' after catch clause");
    catchBlock = block();
  }

  StmtPtr finallyBlock = nullptr;
  if (match({TokenType::FINALLY})) {
    consume(TokenType::LBRACE, "Expected '{' after 'finally'");
    finallyBlock = block();
  }

  if (!catchBlock && !finallyBlock) {
    throw error("try requires a catch or finally clause");
  }
  return std::make_shared<TryCatchStmt>(tryBlock, catchType, catchVar,
                                        catchBlock, finallyBlock, line,
                                        column);
}

StmtPtr Parser::throwStatement() {
  int line = previous().line;
  int column = previous().column;
  ExprPtr value = expression();
  consume(TokenType::SEMICOLON, "Expected ';' after throw expression");
  return std::make_shared<ThrowStmt>(value, line, column);
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
  if (match({TokenType::INC, TokenType::DEC})) {
    Token op = previous();
    ExprPtr target = unary();
    if (!isAssignTarget(target)) {
      throw error("++/-- require a variable or index target");
    }
    return std::make_shared<UpdateExpr>(target, op.type == TokenType::INC,
                                        /*prefix=*/true, op.line, op.column);
  }

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
    } else if (match({TokenType::DOT})) {
      Token dot = previous();
      Token member =
          consume(TokenType::IDENTIFIER, "Expected member name after '.'");
      expr = std::make_shared<MemberExpr>(expr, member.lexeme, dot.line,
                                          dot.column);
    } else if (match({TokenType::INC, TokenType::DEC})) {
      Token op = previous();
      if (!isAssignTarget(expr)) {
        throw error("++/-- require a variable or index target");
      }
      expr = std::make_shared<UpdateExpr>(expr, op.type == TokenType::INC,
                                          /*prefix=*/false, op.line,
                                          op.column);
    } else {
      break;
    }
  }

  return expr;
}

ExprPtr Parser::finishCall(ExprPtr callee) {
  int line = previous().line;
  int column = previous().column;

  std::vector<ExprPtr> arguments;
  if (!check(TokenType::RPAREN)) {
    do {
      arguments.push_back(expression());
    } while (match({TokenType::COMMA}));
  }

  consume(TokenType::RPAREN, "Expected ')' after arguments");

  if (auto varExpr = std::dynamic_pointer_cast<VariableExpr>(callee)) {
    return std::make_shared<CallExpr>(varExpr->name, arguments, line, column);
  }
  // Calling through an arbitrary expression: fn values, struct methods,
  // `f()()`, `arr[0](x)`, `obj.m(x)`.
  return std::make_shared<CallExpr>(callee, arguments, line, column);
}

ExprPtr Parser::finishIndex(ExprPtr callee) {
  int line = previous().line;
  int column = previous().column;

  // Slice: arr[a:b], arr[:b], arr[a:], arr[:]
  if (check(TokenType::COLON)) {
    advance();
    ExprPtr end = nullptr;
    if (!check(TokenType::RBRACKET)) {
      end = expression();
    }
    consume(TokenType::RBRACKET, "Expected ']' after slice");
    return std::make_shared<IndexExpr>(callee, nullptr, end, true, line,
                                       column);
  }

  ExprPtr index = expression();
  if (match({TokenType::COLON})) {
    ExprPtr end = nullptr;
    if (!check(TokenType::RBRACKET)) {
      end = expression();
    }
    consume(TokenType::RBRACKET, "Expected ']' after slice");
    return std::make_shared<IndexExpr>(callee, index, end, true, line,
                                       column);
  }

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

  if (match({TokenType::LBRACE})) {
    int line = previous().line;
    int column = previous().column;
    std::vector<std::pair<ExprPtr, ExprPtr>> entries;
    if (!check(TokenType::RBRACE)) {
      do {
        ExprPtr key = expression();
        consume(TokenType::COLON, "Expected ':' in map literal");
        ExprPtr value = expression();
        entries.emplace_back(key, value);
      } while (match({TokenType::COMMA}));
    }
    consume(TokenType::RBRACE, "Expected '}' after map literal");
    return std::make_shared<MapLiteralExpr>(std::move(entries), line, column);
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
    if (token.interpolated) {
      return interpolatedString(token);
    }
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
    // Lambda literal: fn ( params ) [-> type] { ... }
    if (token.lexeme == "fn" && lambdaAhead()) {
      return lambdaExpression(token);
    }
    // Enum member access: EnumName.Member
    if (_enumNames.count(token.lexeme) > 0 &&
        check(TokenType::DOT) &&
        peekAt(1).type == TokenType::IDENTIFIER) {
      advance(); // '.'
      Token member = advance();
      return std::make_shared<EnumMemberExpr>(token.lexeme, member.lexeme,
                                              token.line, token.column);
    }
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

ExprPtr Parser::lambdaExpression(const Token &fnToken) {
  consume(TokenType::LPAREN, "Expected '(' after 'fn'");

  std::vector<Parameter> params;
  if (!check(TokenType::RPAREN)) {
    bool sawDefault = false;
    do {
      Parameter p = lambdaParameter();
      if (p.defaultValue) {
        sawDefault = true;
      } else if (sawDefault) {
        throw error("Parameter '" + p.name +
                    "' requires a default value: defaults must be trailing");
      }
      params.push_back(std::move(p));
    } while (match({TokenType::COMMA}));
  }
  consume(TokenType::RPAREN, "Expected ')' after lambda parameters");

  TypeInfo retType(DataType::VOID);
  bool hasRetType = false;
  if (check(TokenType::MINUS) &&
      peekAt(1).type == TokenType::GREATER_THAN) {
    advance(); // '-'
    advance(); // '>'
    retType = parseType();
    hasRetType = true;
  }

  consume(TokenType::LBRACE, "Expected '{' before lambda body");
  StmtPtr body = block();

  return std::make_shared<LambdaExpr>(std::move(params), body, retType,
                                      hasRetType, fnToken.line,
                                      fnToken.column);
}

ExprPtr Parser::interpolatedString(const Token &token) {
  std::vector<InterpolatedStringExpr::Part> parts;

  for (const auto &part : token.stringParts) {
    if (!part.isExpr) {
      parts.push_back({false, part.text, nullptr});
      continue;
    }

    Lexer subLexer(part.text, _filename);
    std::vector<Token> tokens;
    try {
      tokens = subLexer.tokenize();
    } catch (const std::exception &e) {
      throw error(std::string("Invalid string interpolation: ") + e.what());
    }
    for (const auto &t : tokens) {
      if (t.type == TokenType::UNKNOWN) {
        throw error("Invalid character in string interpolation");
      }
    }

    Parser subParser(tokens, _filename, _structNames, _enumNames);
    ExprPtr subExpr;
    try {
      subExpr = subParser.parseExpression();
    } catch (const ParseError &e) {
      throw error(std::string("Invalid expression in string interpolation: ") +
                  e.what());
    }
    parts.push_back({true, "", subExpr});
  }

  return std::make_shared<InterpolatedStringExpr>(std::move(parts), token.line,
                                                  token.column);
}

} // namespace Script
