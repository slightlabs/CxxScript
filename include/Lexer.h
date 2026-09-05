#pragma once

#include "Token.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace Script {

class Lexer {
public:
  Lexer(const std::string &source, const std::string &filename = "");

  std::vector<Token> tokenize();
  Token nextToken();

  const std::string &getFilename() const { return _filename; }

private:
  std::string _source;
  std::string _filename;
  size_t _current;
  int _line;
  int _column;

  static std::unordered_map<std::string, TokenType> _keywords;

  bool isAtEnd() const;
  char advance();
  char peek() const;
  char peekNext() const;
  bool match(char expected);

  void skipWhitespace();
  void skipLineComment();
  void skipBlockComment();

  Token makeToken(TokenType type, const std::string &lexeme);
  Token number();
  Token identifier();
  Token string();
  Token character();

  bool isDigit(char c) const;
  bool isAlpha(char c) const;
  bool isAlphaNumeric(char c) const;

  static void initKeywords();
};

} // namespace Script
