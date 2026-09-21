// Lexer edge cases: token boundaries, error (UNKNOWN) tokens, numeric
// literal forms, escape handling, and interpolated string splitting.
#include "Lexer.h"
#include <gtest/gtest.h>

using namespace Script;

namespace {
std::vector<Token> lex(const std::string &src) {
  Lexer l(src, "t.script");
  return l.tokenize();
}

// All non-EOF token types in order.
std::vector<TokenType> types(const std::string &src) {
  std::vector<TokenType> out;
  for (const auto &t : lex(src)) {
    if (t.type == TokenType::END_OF_FILE) {
      break;
    }
    out.push_back(t.type);
  }
  return out;
}

bool anyUnknown(const std::string &src) {
  for (const auto &t : lex(src)) {
    if (t.type == TokenType::UNKNOWN) {
      return true;
    }
  }
  return false;
}
} // namespace

TEST(LexerEdgeTest, OperatorDisambiguation) {
  // &, &&, &= must be distinct tokens; same for |, ||, |=.
  auto t = types("& && &= | || |= ^ ^= ~ << <<= >> >>=");
  std::vector<TokenType> want = {
      TokenType::BIT_AND,       TokenType::AND,          TokenType::AND_ASSIGN,
      TokenType::BIT_OR,        TokenType::OR,           TokenType::OR_ASSIGN,
      TokenType::BIT_XOR,       TokenType::XOR_ASSIGN,   TokenType::BIT_NOT,
      TokenType::LSHIFT,        TokenType::LSHIFT_ASSIGN,
      TokenType::RSHIFT,        TokenType::RSHIFT_ASSIGN};
  EXPECT_EQ(t, want);
}

TEST(LexerEdgeTest, ArithmeticAndCompoundOps) {
  auto t = types("+ ++ += - -- -= * *= / /= % %= = == ! != < <= > >= ? : .");
  std::vector<TokenType> want = {
      TokenType::PLUS,         TokenType::INC,          TokenType::PLUS_ASSIGN,
      TokenType::MINUS,        TokenType::DEC,          TokenType::MINUS_ASSIGN,
      TokenType::MULTIPLY,     TokenType::MULT_ASSIGN,
      TokenType::DIVIDE,       TokenType::DIV_ASSIGN,
      TokenType::MODULO,       TokenType::MOD_ASSIGN,
      TokenType::ASSIGN,       TokenType::EQUAL,
      TokenType::NOT,          TokenType::NOT_EQUAL,
      TokenType::LESS_THAN,    TokenType::LESS_EQUAL,
      TokenType::GREATER_THAN, TokenType::GREATER_EQUAL,
      TokenType::QUESTION,     TokenType::COLON,        TokenType::DOT};
  EXPECT_EQ(t, want);
}

TEST(LexerEdgeTest, Delimiters) {
  auto t = types("( ) { } [ ] ; ,");
  std::vector<TokenType> want = {
      TokenType::LPAREN,   TokenType::RPAREN, TokenType::LBRACE,
      TokenType::RBRACE,   TokenType::LBRACKET, TokenType::RBRACKET,
      TokenType::SEMICOLON, TokenType::COMMA};
  EXPECT_EQ(t, want);
}

TEST(LexerEdgeTest, IntLiteralBases) {
  auto toks = lex("255 0xFF 0b11 0o17 1_000");
  ASSERT_GE(toks.size(), 5u);
  EXPECT_EQ(toks[0].intValue, 255);
  EXPECT_EQ(toks[1].intValue, 255);
  EXPECT_EQ(toks[2].intValue, 3);
  EXPECT_EQ(toks[3].intValue, 15);
  EXPECT_EQ(toks[4].intValue, 1000);
}

TEST(LexerEdgeTest, FloatLiteralForms) {
  auto toks = lex("1.5 0.25 1e3 1.5e-3 2E2");
  ASSERT_GE(toks.size(), 5u);
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(toks[i].type, TokenType::FLOAT_LITERAL) << i;
  }
  EXPECT_DOUBLE_EQ(toks[0].doubleValue, 1.5);
  EXPECT_DOUBLE_EQ(toks[2].doubleValue, 1000.0);
  EXPECT_DOUBLE_EQ(toks[3].doubleValue, 0.0015);
}

TEST(LexerEdgeTest, CharLiteralsAndEscapes) {
  auto toks = lex("'a' '\\n' '\\t' '\\'' '\\\\'");
  ASSERT_GE(toks.size(), 5u);
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(toks[i].type, TokenType::CHAR_LITERAL) << i;
  }
  EXPECT_EQ(toks[0].charValue, 'a');
  EXPECT_EQ(toks[1].charValue, '\n');
  EXPECT_EQ(toks[2].charValue, '\t');
  EXPECT_EQ(toks[3].charValue, '\'');
  EXPECT_EQ(toks[4].charValue, '\\');
}

TEST(LexerEdgeTest, InvalidCharsProduceUnknown) {
  EXPECT_TRUE(anyUnknown("@"));
  EXPECT_TRUE(anyUnknown("#"));
  EXPECT_TRUE(anyUnknown("$x")); // bare $ outside a string
}

TEST(LexerEdgeTest, UnterminatedStringIsUnknown) {
  EXPECT_TRUE(anyUnknown("\"abc"));
}

TEST(LexerEdgeTest, UnterminatedCharIsUnknown) {
  EXPECT_TRUE(anyUnknown("'a"));
}

TEST(LexerEdgeTest, EmptyCharLiteralIsUnknown) {
  EXPECT_TRUE(anyUnknown("''"));
}

TEST(LexerEdgeTest, MultiCharLiteralIsUnknown) {
  EXPECT_TRUE(anyUnknown("'ab'"));
}

TEST(LexerEdgeTest, UnterminatedBlockCommentHandled) {
  // Should not hang or crash; remaining tokens still lex.
  auto t = types("int32 x /* never closed");
  ASSERT_GE(t.size(), 2u);
  EXPECT_EQ(t[0], TokenType::INT32);
  EXPECT_EQ(t[1], TokenType::IDENTIFIER);
}

TEST(LexerEdgeTest, CommentsSkippedBetweenTokens) {
  auto t = types("a // line\nb /* block */ c");
  std::vector<TokenType> want = {TokenType::IDENTIFIER, TokenType::IDENTIFIER,
                                 TokenType::IDENTIFIER};
  EXPECT_EQ(t, want);
}

TEST(LexerEdgeTest, LineAndColumnTracked) {
  auto toks = lex("a\n  bb\n    ccc");
  ASSERT_GE(toks.size(), 3u);
  EXPECT_EQ(toks[0].line, 1);
  EXPECT_EQ(toks[1].line, 2);
  EXPECT_EQ(toks[1].column, 3);
  EXPECT_EQ(toks[2].line, 3);
  EXPECT_EQ(toks[2].column, 5);
}

TEST(LexerEdgeTest, InterpolatedStringParts) {
  auto toks = lex("\"a ${x + 1} b ${y}\"");
  ASSERT_GE(toks.size(), 1u);
  EXPECT_EQ(toks[0].type, TokenType::STRING_LITERAL);
  ASSERT_TRUE(toks[0].interpolated);
  ASSERT_EQ(toks[0].stringParts.size(), 5u);
  EXPECT_FALSE(toks[0].stringParts[0].isExpr);
  EXPECT_EQ(toks[0].stringParts[0].text, "a ");
  EXPECT_TRUE(toks[0].stringParts[1].isExpr);
  EXPECT_EQ(toks[0].stringParts[1].text, "x + 1");
  EXPECT_FALSE(toks[0].stringParts[2].isExpr);
  EXPECT_EQ(toks[0].stringParts[2].text, " b ");
  EXPECT_TRUE(toks[0].stringParts[3].isExpr);
  EXPECT_EQ(toks[0].stringParts[3].text, "y");
  // Trailing literal part after the last ${...} is empty.
  EXPECT_FALSE(toks[0].stringParts[4].isExpr);
  EXPECT_TRUE(toks[0].stringParts[4].text.empty());
}

TEST(LexerEdgeTest, EscapedInterpolationIsLiteralText) {
  auto toks = lex("\"a \\${x}\"");
  ASSERT_GE(toks.size(), 1u);
  EXPECT_EQ(toks[0].type, TokenType::STRING_LITERAL);
  EXPECT_FALSE(toks[0].interpolated);
  EXPECT_EQ(toks[0].stringValue, "a ${x}");
}

TEST(LexerEdgeTest, StringEscapesDecoded) {
  auto toks = lex("\"a\\n\\t\\\"b\\\\\"");
  ASSERT_GE(toks.size(), 1u);
  EXPECT_EQ(toks[0].stringValue, "a\n\t\"b\\");
}

TEST(LexerEdgeTest, RemainingEscapesDecoded) {
  // \r and \0 decode; \' inside a string is an unknown escape, preserved as-is.
  auto toks = lex("\"\\r\\0\\'\" '\\\''");
  ASSERT_GE(toks.size(), 2u);
  EXPECT_EQ(toks[0].type, TokenType::STRING_LITERAL);
  EXPECT_EQ(toks[0].stringValue, std::string("\r\0\\'", 4));
  EXPECT_EQ(toks[1].type, TokenType::CHAR_LITERAL);
  EXPECT_EQ(toks[1].charValue, '\'');
}

TEST(LexerEdgeTest, EscapeInsideInterpolationExpression) {
  // An escaped quote inside ${...} must not end the string or the expr part.
  auto toks = lex("\"${\"a\\nb\" + \"c\\t\"} tail\"");
  ASSERT_GE(toks.size(), 1u);
  EXPECT_EQ(toks[0].type, TokenType::STRING_LITERAL);
  ASSERT_TRUE(toks[0].interpolated);
  ASSERT_GE(toks[0].stringParts.size(), 3u);
  // Leading ${ means part 0 is an empty literal, part 1 is the expression
  // (its raw source text, escapes preserved for re-lexing).
  EXPECT_FALSE(toks[0].stringParts[0].isExpr);
  EXPECT_TRUE(toks[0].stringParts[1].isExpr);
  EXPECT_NE(toks[0].stringParts[1].text.find("\"a\\nb\""), std::string::npos);
  EXPECT_EQ(toks[0].stringParts[2].text, " tail");
}

TEST(LexerEdgeTest, CharLiteralInsideInterpolationExpression) {
  // '}' inside ${...} must not terminate the interpolation early.
  auto toks = lex("\"${x == '}' ? 1 : 2}\"");
  ASSERT_GE(toks.size(), 1u);
  ASSERT_TRUE(toks[0].interpolated);
  ASSERT_GE(toks[0].stringParts.size(), 2u);
  EXPECT_TRUE(toks[0].stringParts[1].isExpr);
  EXPECT_NE(toks[0].stringParts[1].text.find("'}'"), std::string::npos);
}

TEST(LexerEdgeTest, KeywordsVsIdentifiers) {
  auto t = types("if else while for return switch case default do break "
                 "continue const import struct try catch throw finally enum "
                 "fn auto this true false int32 string void map identify");
  // `fn`, `auto`, `this`, `map` are contextual — they lex as identifiers.
  std::vector<TokenType> want = {
      TokenType::IF,        TokenType::ELSE,   TokenType::WHILE,
      TokenType::FOR,       TokenType::RETURN, TokenType::SWITCH,
      TokenType::CASE,      TokenType::DEFAULT, TokenType::DO,
      TokenType::BREAK,     TokenType::CONTINUE, TokenType::CONST,
      TokenType::IMPORT,    TokenType::STRUCT, TokenType::TRY,
      TokenType::CATCH,     TokenType::THROW,  TokenType::FINALLY,
      TokenType::ENUM,      TokenType::IDENTIFIER, TokenType::IDENTIFIER,
      TokenType::IDENTIFIER, TokenType::TRUE,  TokenType::FALSE,
      TokenType::INT32,     TokenType::STRING, TokenType::VOID,
      TokenType::IDENTIFIER, TokenType::IDENTIFIER};
  EXPECT_EQ(t, want);
}

TEST(LexerEdgeTest, EmptySourceYieldsOnlyEof) {
  auto toks = lex("");
  ASSERT_GE(toks.size(), 1u);
  EXPECT_EQ(toks[0].type, TokenType::END_OF_FILE);
}
