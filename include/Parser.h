#pragma once

#include "AST.h"
#include "Lexer.h"
#include "Token.h"
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace Script {

class ParseError : public std::runtime_error {
public:
  int line;
  int column;
  std::string procedureName;

  ParseError(const std::string &message, int ln, int col,
             const std::string &procName = "")
      : std::runtime_error(message), line(ln), column(col),
        procedureName(procName) {}
};

class Parser {
public:
  // knownStructs: names of struct types declared by previously loaded files
  // (imports, earlier loadScriptSource calls, REPL state).
  Parser(const std::vector<Token> &tokens, const std::string &filename = "",
         const std::unordered_set<std::string> &knownStructs = {});

  ScriptPtr parse();

  // Parse a single expression (used for string interpolation sub-parsing).
  ExprPtr parseExpression();

  // Parse a sequence of top-level statements (REPL / snippet evaluation).
  std::vector<StmtPtr> parseStatements();

private:
  std::vector<Token> _tokens;
  std::string _filename;
  size_t _current;
  std::string _currentProcedure;
  std::vector<ParseError> _errors;
  std::unordered_set<std::string> _structNames; // known + in-file struct types

  // Utility methods
  bool isAtEnd() const;
  Token peek() const;
  Token peekAt(size_t offset) const;
  Token previous() const;
  Token advance();
  bool check(TokenType type) const;
  bool match(const std::vector<TokenType> &types);
  Token consume(TokenType type, const std::string &message);

  ParseError error(const std::string &message);
  void synchronize();

  // Parsing methods
  ProcedureDeclPtr procedureDeclaration();
  StructDeclPtr structDeclaration();
  std::vector<Parameter> parameters();
  TypeInfo parseType();
  Token consumeGreaterThan();
  // Index just past a `map<K, V>` type starting at offset, or -1.
  int mapTypeEnd(size_t offset) const;
  // Whether tokens at offset start a variable declaration (scalar, array,
  // const-qualified, or map<K,V> type followed by a variable name).
  bool isDeclStart(size_t offset) const;

  StmtPtr statement();
  StmtPtr varDeclaration();
  StmtPtr expressionStatement();
  StmtPtr ifStatement();
  StmtPtr whileStatement();
  StmtPtr doWhileStatement();
  StmtPtr forStatement();
  StmtPtr switchStatement();
  StmtPtr returnStatement();
  StmtPtr breakStatement();
  StmtPtr continueStatement();
  StmtPtr block();

  ExprPtr expression();
  ExprPtr conditional();
  ExprPtr assignment();
  ExprPtr logicalOr();
  ExprPtr logicalAnd();
  ExprPtr bitwiseOr();
  ExprPtr bitwiseXor();
  ExprPtr bitwiseAnd();
  ExprPtr shift();
  ExprPtr equality();
  ExprPtr comparison();
  ExprPtr term();
  ExprPtr factor();
  ExprPtr unary();
  ExprPtr primary();
  ExprPtr interpolatedString(const Token &token);
  ExprPtr call();
  ExprPtr finishCall(ExprPtr callee);
  ExprPtr finishIndex(ExprPtr callee);

public:
  const std::vector<ParseError> &getErrors() const { return _errors; }
  bool hasErrors() const { return !_errors.empty(); }
};

} // namespace Script
