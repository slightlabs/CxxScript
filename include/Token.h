#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace Script {

// A piece of an interpolated string literal: either literal text or the
// source text of an embedded ${...} expression.
struct StringPart {
  bool isExpr = false;
  std::string text;

  StringPart(bool expr = false, const std::string &t = "")
      : isExpr(expr), text(t) {}
};

enum class TokenType {
    // Literals
    INT_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,
    CHAR_LITERAL,
    TRUE,
    FALSE,
    
    // Identifiers and Keywords
    IDENTIFIER,
    
    // Data Types
    INT8,
    UINT8,
    INT16,
    UINT16,
    INT32,
    UINT32,
    INT64,
    UINT64,
    FLOAT,
    DOUBLE,
    STRING,
    BOOL,
    CHAR,
    VOID,
    SWITCH,
    CASE,
    DEFAULT,
    DO,
    BREAK,
    CONTINUE,
    CONST,
    IMPORT,
    STRUCT,
    TRY,
    CATCH,
    THROW,
    FINALLY,
    ENUM,
    
    // Control Flow
    IF,
    ELSE,
    WHILE,
    FOR,
    RETURN,
    
    // Operators
    PLUS,           // +
    MINUS,          // -
    MULTIPLY,       // *
    DIVIDE,         // /
    MODULO,         // %
    INC,            // ++
    DEC,            // --
    ASSIGN,         // =
    PLUS_ASSIGN,    // +=
    MINUS_ASSIGN,   // -=
    MULT_ASSIGN,    // *=
    DIV_ASSIGN,     // /=
    MOD_ASSIGN,     // %=
    AND_ASSIGN,     // &=
    OR_ASSIGN,      // |=
    XOR_ASSIGN,     // ^=
    LSHIFT_ASSIGN,  // <<=
    RSHIFT_ASSIGN,  // >>=
    
    // Comparison
    EQUAL,          // ==
    NOT_EQUAL,      // !=
    LESS_THAN,      // <
    GREATER_THAN,   // >
    LESS_EQUAL,     // <=
    GREATER_EQUAL,  // >=
    
    // Logical
    AND,            // &&
    OR,             // ||
    NOT,            // !
    BIT_AND,        // &
    BIT_OR,         // |
    BIT_XOR,        // ^
    BIT_NOT,        // ~
    LSHIFT,         // <<
    RSHIFT,         // >>
    
    // Delimiters
    LPAREN,         // (
    RPAREN,         // )
    LBRACE,         // {
    RBRACE,         // }
    LBRACKET,       // [
    RBRACKET,       // ]
    SEMICOLON,      // ;
    COMMA,          // ,
    COLON,          // :
    QUESTION,       // ?
    DOT,            // .
    
    // Special
    NUL,            // null
    END_OF_FILE,
    UNKNOWN
};

struct Token {
    TokenType type;
    std::string lexeme;
    int line;
    int column;
    
    // For literals
    int64_t intValue = 0;
    std::string stringValue;
    double doubleValue = 0.0;
    char charValue = '\0';

    // For interpolated string literals ("a${x}b"): literal and expression
    // parts in order of appearance.
    bool interpolated = false;
    std::vector<StringPart> stringParts;
    
    Token(TokenType t = TokenType::UNKNOWN, const std::string& lex = "", int ln = 0, int col = 0)
        : type(t), lexeme(lex), line(ln), column(col) {}
    
    std::string toString() const;
};

} // namespace Script
