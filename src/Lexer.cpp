#include "Lexer.h"
#include <cctype>

namespace Script {

std::unordered_map<std::string, TokenType> Lexer::_keywords;

Lexer::Lexer(const std::string &source, const std::string &filename)
    : _source(source), _filename(filename), _current(0), _line(1), _column(1) {
  if (_keywords.empty()) {
    initKeywords();
  }
}

void Lexer::initKeywords() {
  _keywords["int8"] = TokenType::INT8;
  _keywords["uint8"] = TokenType::UINT8;
  _keywords["int16"] = TokenType::INT16;
  _keywords["uint16"] = TokenType::UINT16;
  _keywords["int32"] = TokenType::INT32;
  _keywords["uint32"] = TokenType::UINT32;
  _keywords["int64"] = TokenType::INT64;
  _keywords["uint64"] = TokenType::UINT64;
  _keywords["float"] = TokenType::FLOAT;
  _keywords["double"] = TokenType::DOUBLE;
  _keywords["string"] = TokenType::STRING;
  _keywords["bool"] = TokenType::BOOL;
  _keywords["char"] = TokenType::CHAR;
  _keywords["void"] = TokenType::VOID;
  _keywords["switch"] = TokenType::SWITCH;
  _keywords["case"] = TokenType::CASE;
  _keywords["default"] = TokenType::DEFAULT;
  _keywords["do"] = TokenType::DO;
  _keywords["break"] = TokenType::BREAK;
  _keywords["continue"] = TokenType::CONTINUE;
  _keywords["const"] = TokenType::CONST;
  _keywords["import"] = TokenType::IMPORT;
  _keywords["struct"] = TokenType::STRUCT;
  _keywords["try"] = TokenType::TRY;
  _keywords["catch"] = TokenType::CATCH;
  _keywords["throw"] = TokenType::THROW;
  _keywords["finally"] = TokenType::FINALLY;
  _keywords["enum"] = TokenType::ENUM;
  _keywords["if"] = TokenType::IF;
  _keywords["else"] = TokenType::ELSE;
  _keywords["while"] = TokenType::WHILE;
  _keywords["for"] = TokenType::FOR;
  _keywords["return"] = TokenType::RETURN;
  _keywords["true"] = TokenType::TRUE;
  _keywords["false"] = TokenType::FALSE;
}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;
  Token token;

  do {
    token = nextToken();
    tokens.push_back(token);
  } while (token.type != TokenType::END_OF_FILE);

  return tokens;
}

Token Lexer::nextToken() {
  skipWhitespace();

  if (isAtEnd()) {
    return makeToken(TokenType::END_OF_FILE, "");
  }

  int tokenLine = _line;
  int tokenColumn = _column;
  char c = advance();

  // Numbers
  if (isDigit(c)) {
    _current--;
    _column--;
    return number();
  }

  // Identifiers and keywords
  if (isAlpha(c)) {
    _current--;
    _column--;
    return identifier();
  }

  // String literals
  if (c == '"') {
    return string();
  }

  // Character literals
  if (c == '\'') {
    return character();
  }

  // Two-character operators
  switch (c) {
  case '+':
    if (match('+'))
      return makeToken(TokenType::INC, "++");
    if (match('='))
      return makeToken(TokenType::PLUS_ASSIGN, "+=");
    return makeToken(TokenType::PLUS, "+");
  case '-':
    if (match('-'))
      return makeToken(TokenType::DEC, "--");
    if (match('='))
      return makeToken(TokenType::MINUS_ASSIGN, "-=");
    return makeToken(TokenType::MINUS, "-");
  case '*':
    if (match('='))
      return makeToken(TokenType::MULT_ASSIGN, "*=");
    return makeToken(TokenType::MULTIPLY, "*");
  case '/':
    if (match('='))
      return makeToken(TokenType::DIV_ASSIGN, "/=");
    return makeToken(TokenType::DIVIDE, "/");
  case '%':
    if (match('='))
      return makeToken(TokenType::MOD_ASSIGN, "%=");
    return makeToken(TokenType::MODULO, "%");
  case '=':
    if (match('='))
      return makeToken(TokenType::EQUAL, "==");
    return makeToken(TokenType::ASSIGN, "=");
  case '!':
    if (match('='))
      return makeToken(TokenType::NOT_EQUAL, "!=");
    return makeToken(TokenType::NOT, "!");
  case '<':
    if (match('<')) {
      if (match('='))
        return makeToken(TokenType::LSHIFT_ASSIGN, "<<=");
      return makeToken(TokenType::LSHIFT, "<<");
    }
    if (match('='))
      return makeToken(TokenType::LESS_EQUAL, "<=");
    return makeToken(TokenType::LESS_THAN, "<");
  case '>':
    if (match('>')) {
      if (match('='))
        return makeToken(TokenType::RSHIFT_ASSIGN, ">>=");
      return makeToken(TokenType::RSHIFT, ">>");
    }
    if (match('='))
      return makeToken(TokenType::GREATER_EQUAL, ">=");
    return makeToken(TokenType::GREATER_THAN, ">");
  case '&':
    if (match('&'))
      return makeToken(TokenType::AND, "&&");
    if (match('='))
      return makeToken(TokenType::AND_ASSIGN, "&=");
    return makeToken(TokenType::BIT_AND, "&");
  case '|':
    if (match('|'))
      return makeToken(TokenType::OR, "||");
    if (match('='))
      return makeToken(TokenType::OR_ASSIGN, "|=");
    return makeToken(TokenType::BIT_OR, "|");
  case '(':
    return makeToken(TokenType::LPAREN, "(");
  case '[':
    return makeToken(TokenType::LBRACKET, "[");
  case '^':
    if (match('='))
      return makeToken(TokenType::XOR_ASSIGN, "^=");
    return makeToken(TokenType::BIT_XOR, "^");
  case '~':
    return makeToken(TokenType::BIT_NOT, "~");
  case ')':
    return makeToken(TokenType::RPAREN, ")");
  case ']':
    return makeToken(TokenType::RBRACKET, "]");
  case '{':
    return makeToken(TokenType::LBRACE, "{");
  case '}':
    return makeToken(TokenType::RBRACE, "}");
  case ';':
    return makeToken(TokenType::SEMICOLON, ";");
  case ',':
    return makeToken(TokenType::COMMA, ",");
  case ':':
    return makeToken(TokenType::COLON, ":");
  case '?':
    return makeToken(TokenType::QUESTION, "?");
  case '.':
    return makeToken(TokenType::DOT, ".");
  }

  Token errorToken = makeToken(TokenType::UNKNOWN, std::string(1, c));
  errorToken.line = tokenLine;
  errorToken.column = tokenColumn;
  return errorToken;
}

bool Lexer::isAtEnd() const { return _current >= _source.length(); }

char Lexer::advance() {
  _column++;
  return _source[_current++];
}

char Lexer::peek() const {
  if (isAtEnd())
    return '\0';
  return _source[_current];
}

char Lexer::peekNext() const {
  if (_current + 1 >= _source.length())
    return '\0';
  return _source[_current + 1];
}

bool Lexer::match(char expected) {
  if (isAtEnd())
    return false;
  if (_source[_current] != expected)
    return false;
  _current++;
  _column++;
  return true;
}

void Lexer::skipWhitespace() {
  while (!isAtEnd()) {
    char c = peek();
    switch (c) {
    case ' ':
    case '\r':
    case '\t':
      advance();
      break;
    case '\n':
      _line++;
      _column = 0;
      advance();
      break;
    case '/':
      if (peekNext() == '/') {
        skipLineComment();
      } else if (peekNext() == '*') {
        skipBlockComment();
      } else {
        return;
      }
      break;
    default:
      return;
    }
  }
}

void Lexer::skipLineComment() {
  while (peek() != '\n' && !isAtEnd()) {
    advance();
  }
}

void Lexer::skipBlockComment() {
  advance(); // /
  advance(); // *

  while (!isAtEnd()) {
    if (peek() == '*' && peekNext() == '/') {
      advance(); // *
      advance(); // /
      break;
    }
    if (peek() == '\n') {
      _line++;
      _column = 0;
    }
    advance();
  }
}

Token Lexer::makeToken(TokenType type, const std::string &lexeme) {
  Token token(type, lexeme, _line, _column - lexeme.length());
  return token;
}

Token Lexer::number() {
  int startColumn = _column;
  size_t start = _current;

  // Base-prefixed integer literals: 0x hex, 0b binary, 0o octal
  if (peek() == '0' && (peekNext() == 'x' || peekNext() == 'X' ||
                        peekNext() == 'b' || peekNext() == 'B' ||
                        peekNext() == 'o' || peekNext() == 'O')) {
    char kind = static_cast<char>(std::tolower(peekNext()));
    int base = kind == 'x' ? 16 : (kind == 'b' ? 2 : 8);
    advance(); // '0'
    advance(); // prefix letter

    auto isBaseDigit = [&](char c) -> bool {
      if (base == 16)
        return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
      if (base == 8)
        return c >= '0' && c <= '7';
      return c == '0' || c == '1';
    };

    std::string digits;
    bool lastSep = false;
    while (!isAtEnd()) {
      char c = peek();
      if (isBaseDigit(c)) {
        digits += c;
        lastSep = false;
        advance();
      } else if (c == '_' && !digits.empty() && !lastSep) {
        lastSep = true;
        advance();
      } else {
        break;
      }
    }

    // Reject empty digits, trailing separator, or a stray alphanumeric
    // (e.g. 0x, 0b2, 0xFFz) as a bad literal.
    if (digits.empty() || lastSep || isAlphaNumeric(peek())) {
      std::string bad = _source.substr(start, _current - start);
      if (isAlphaNumeric(peek()))
        bad += advance();
      Token token = makeToken(TokenType::UNKNOWN, bad);
      token.column = startColumn;
      return token;
    }

    Token token = makeToken(TokenType::INT_LITERAL, _source.substr(start, _current - start));
    token.column = startColumn;
    try {
      token.intValue = static_cast<int64_t>(std::stoull(digits, nullptr, base));
    } catch (const std::exception &) {
      token.type = TokenType::UNKNOWN;
    }
    return token;
  }

  // Decimal (with optional fraction, exponent, and _ digit separators)
  std::string numStr;
  bool isFloat = false;

  auto digits = [&](bool &sepErr) {
    while (isDigit(peek()) || peek() == '_') {
      char c = peek();
      if (c == '_') {
        // A separator is only valid between two digits.
        if (numStr.empty() || !isDigit(numStr.back()) || !isDigit(peekNext()))
          sepErr = true;
        advance();
      } else {
        numStr += advance();
      }
    }
  };

  bool sepErr = false;
  digits(sepErr);
  if (numStr.empty())
    sepErr = true;

  if (peek() == '.' && isDigit(peekNext())) {
    isFloat = true;
    numStr += advance(); // '.'
    digits(sepErr);
  }

  // Exponent: [eE][+-]?digits
  if (peek() == 'e' || peek() == 'E') {
    size_t saveCur = _current;
    int saveCol = _column;
    advance(); // 'e'
    if (peek() == '+' || peek() == '-')
      advance();
    if (isDigit(peek())) {
      isFloat = true;
      numStr += 'e';
      if (_source[saveCur + 1] == '+' || _source[saveCur + 1] == '-')
        numStr += _source[saveCur + 1];
      digits(sepErr);
    } else {
      _current = saveCur;
      _column = saveCol;
    }
  }

  if (sepErr || isAlpha(peek())) {
    std::string bad = _source.substr(start, _current - start);
    if (isAlphaNumeric(peek()))
      bad += advance();
    Token token = makeToken(TokenType::UNKNOWN, bad);
    token.column = startColumn;
    return token;
  }

  Token token = makeToken(isFloat ? TokenType::FLOAT_LITERAL
                                  : TokenType::INT_LITERAL,
                          numStr);
  token.column = startColumn;
  try {
    if (isFloat) {
      token.doubleValue = std::stod(numStr);
    } else {
      // Parse as unsigned so values up to 2^64-1 lex like the base-prefixed
      // forms: they wrap into two's-complement int64, which also makes
      // -9223372036854775808 (INT64_MIN) writable as a literal.
      token.intValue = static_cast<int64_t>(std::stoull(numStr));
    }
  } catch (const std::exception &) {
    token.type = TokenType::UNKNOWN;
  }
  return token;
}

Token Lexer::identifier() {
  int startColumn = _column;
  size_t start = _current;

  while (isAlphaNumeric(peek())) {
    advance();
  }

  std::string text = _source.substr(start, _current - start);

  auto it = _keywords.find(text);
  TokenType type = (it != _keywords.end()) ? it->second : TokenType::IDENTIFIER;

  Token token = makeToken(type, text);
  token.column = startColumn;
  return token;
}

Token Lexer::string() {
  int startColumn = _column - 1;
  std::string value;
  std::vector<StringPart> parts;
  bool interpolated = false;

  auto flushLiteral = [&]() {
    parts.push_back(StringPart(false, value));
    value.clear();
  };

  while (peek() != '"' && !isAtEnd()) {
    if (peek() == '\n') {
      _line++;
      _column = 0;
    }

    // String interpolation: ${expr}
    if (peek() == '$' && peekNext() == '{') {
      advance(); // $
      advance(); // {
      flushLiteral();

      std::string expr;
      int depth = 1;
      bool inStr = false, inChar = false, esc = false, closed = false;

      while (!isAtEnd() && !closed) {
        char ch = peek();
        if (ch == '\n') {
          _line++;
          _column = 0;
        }
        if (esc) {
          expr += ch;
          esc = false;
          advance();
          continue;
        }
        if ((inStr || inChar) && ch == '\\') {
          expr += ch;
          esc = true;
          advance();
          continue;
        }
        if (inStr) {
          expr += ch;
          if (ch == '"')
            inStr = false;
          advance();
          continue;
        }
        if (inChar) {
          expr += ch;
          if (ch == '\'')
            inChar = false;
          advance();
          continue;
        }
        switch (ch) {
        case '"':
          inStr = true;
          break;
        case '\'':
          inChar = true;
          break;
        case '{':
          depth++;
          break;
        case '}':
          depth--;
          if (depth == 0) {
            advance();
            closed = true;
          }
          break;
        default:
          break;
        }
        if (!closed) {
          expr += ch;
          advance();
        }
      }

      if (!closed) {
        Token token = makeToken(TokenType::UNKNOWN, "\"${" + expr);
        token.column = startColumn;
        return token;
      }

      parts.push_back(StringPart(true, expr));
      interpolated = true;
      continue;
    }

    // Handle escape sequences
    if (peek() == '\\' && !isAtEnd()) {
      advance(); // skip backslash
      char escaped = peek();

      switch (escaped) {
      case '"':
        value += '"';
        break; // Escaped quote
      case '\\':
        value += '\\';
        break; // Escaped backslash
      case 'n':
        value += '\n';
        break; // Newline
      case 't':
        value += '\t';
        break; // Tab
      case 'r':
        value += '\r';
        break; // Carriage return
      case '0':
        value += '\0';
        break; // Null character
      case '$':
        value += '$';
        break; // Escaped $ (literal, no interpolation)
      default:
        // Unknown escape sequence - keep the backslash
        value += '\\';
        value += escaped;
        break;
      }
      advance(); // consume the escaped character
    } else {
      value += advance();
    }
  }

  if (isAtEnd()) {
    Token token = makeToken(TokenType::UNKNOWN, "\"" + value);
    token.column = startColumn;
    return token;
  }

  advance(); // closing "

  Token token = makeToken(TokenType::STRING_LITERAL, "\"" + value + "\"");
  token.column = startColumn;
  if (interpolated) {
    flushLiteral();
    token.interpolated = true;
    token.stringParts = parts;
  } else {
    token.stringValue = value;
  }
  return token;
}

Token Lexer::character() {
  int startColumn = _column - 1;

  if (isAtEnd()) {
    Token token = makeToken(TokenType::UNKNOWN, "'");
    token.column = startColumn;
    return token;
  }

  char value;
  if (peek() == '\\') {
    advance(); // skip backslash
    char escaped = peek();

    switch (escaped) {
    case '\'':
      value = '\'';
      break; // Escaped single quote
    case '"':
      value = '"';
      break; // Escaped double quote
    case '\\':
      value = '\\';
      break; // Escaped backslash
    case 'n':
      value = '\n';
      break; // Newline
    case 't':
      value = '\t';
      break; // Tab
    case 'r':
      value = '\r';
      break; // Carriage return
    case '0':
      value = '\0';
      break; // Null character
    default:
      // Unknown escape sequence in a char literal is an error.
      Token badToken = makeToken(TokenType::UNKNOWN, std::string("'\\") + escaped);
      badToken.column = startColumn;
      return badToken;
    }
    advance(); // consume the escaped character
  } else if (peek() == '\'') {
    // Empty char literal ''
    Token token = makeToken(TokenType::UNKNOWN, "''");
    token.column = startColumn;
    return token;
  } else {
    value = advance();
  }

  if (peek() != '\'') {
    // More than one character between quotes, or unterminated
    Token token = makeToken(TokenType::UNKNOWN, std::string("'") + value);
    token.column = startColumn;
    return token;
  }
  advance(); // closing '

  Token token = makeToken(TokenType::CHAR_LITERAL, std::string("'") + value + "'");
  token.column = startColumn;
  token.charValue = value;
  return token;
}

bool Lexer::isDigit(char c) const { return c >= '0' && c <= '9'; }

bool Lexer::isAlpha(char c) const {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool Lexer::isAlphaNumeric(char c) const { return isAlpha(c) || isDigit(c); }

} // namespace Script
