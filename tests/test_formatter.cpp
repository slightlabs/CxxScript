// Coverage for the Formatter pretty-printer: indentation, spacing,
// comments, numeric normalization, and error propagation.
#include "Formatter.h"
#include "Parser.h"
#include <gtest/gtest.h>

using namespace Script;

TEST(FormatterTest, NormalizesSpacingAndIndentation) {
  std::string out = Formatter::format(
      "int32 add(int32 a,int32 b){return a+b;}", "t.script");
  EXPECT_EQ(out, "int32 add(int32 a, int32 b) {\n"
                 "  return a + b;\n"
                 "}\n");
}

TEST(FormatterTest, CollapsedStatementsGetExpanded) {
  std::string out = Formatter::format(
      "int32 f(){int32 x=1;x+=2;return x;}", "t.script");
  EXPECT_EQ(out, "int32 f() {\n"
                 "  int32 x = 1;\n"
                 "  x += 2;\n"
                 "  return x;\n"
                 "}\n");
}

TEST(FormatterTest, IfElseBracesAndPlacement) {
  std::string out = Formatter::format(
      "int32 f(int32 n){if(n>0){return 1;}else{return -1;}}", "t.script");
  EXPECT_EQ(out, "int32 f(int32 n) {\n"
                 "  if (n > 0) {\n"
                 "    return 1;\n"
                 "  } else {\n"
                 "    return -1;\n"
                 "  }\n"
                 "}\n");
}

TEST(FormatterTest, ElseIfChain) {
  std::string out = Formatter::format(
      "int32 f(int32 n){if(n>0){return 1;}else if(n<0){return -1;}else{return "
      "0;}}",
      "t.script");
  EXPECT_NE(out.find("} else if (n < 0) {"), std::string::npos);
  EXPECT_NE(out.find("} else {"), std::string::npos);
}

TEST(FormatterTest, LoopHeadersSpaced) {
  std::string out = Formatter::format(
      "void f(){for(int32 i=0;i<10;i+=1){println(i);}while(false){break;}}",
      "t.script");
  EXPECT_NE(out.find("for (int32 i = 0; i < 10; i += 1) {"), std::string::npos);
  EXPECT_NE(out.find("while (false) {"), std::string::npos);
}

TEST(FormatterTest, ForEachHeader) {
  std::string out = Formatter::format(
      "void f(int32[] a){for(int32 x:a){println(x);}}", "t.script");
  EXPECT_NE(out.find("for (int32 x : a) {"), std::string::npos);
}

TEST(FormatterTest, DoWhileAndSwitch) {
  std::string out = Formatter::format(
      "int32 f(int32 n){do{n-=1;}while(n>0);switch(n){case 0:return 1;default:"
      "return 2;}}",
      "t.script");
  EXPECT_NE(out.find("do {"), std::string::npos);
  EXPECT_NE(out.find("} while (n > 0);"), std::string::npos);
  EXPECT_NE(out.find("switch (n) {"), std::string::npos);
  EXPECT_NE(out.find("    case 0:"), std::string::npos);
  EXPECT_NE(out.find("    default:"), std::string::npos);
}

TEST(FormatterTest, TryCatchFinally) {
  std::string out = Formatter::format(
      "void f(){try{throw \"x\";}catch(string s){println(s);}finally{}}",
      "t.script");
  EXPECT_NE(out.find("  try {"), std::string::npos);
  EXPECT_NE(out.find("  } catch (string s) {"), std::string::npos);
  EXPECT_NE(out.find("  } finally {"), std::string::npos);
}

TEST(FormatterTest, TernaryAndUnaryPreserved) {
  std::string out = Formatter::format(
      "int32 f(int32 a,int32 b){bool ok=!(a==b);return ok?a:-b;}", "t.script");
  EXPECT_NE(out.find("bool ok = !(a == b);"), std::string::npos);
  EXPECT_NE(out.find("return ok ? a : -b;"), std::string::npos);
}

TEST(FormatterTest, TopLevelDeclarationsSeparatedByBlankLines) {
  std::string out = Formatter::format(
      "int32 a(){return 1;}int32 b(){return 2;}", "t.script");
  EXPECT_EQ(out, "int32 a() {\n  return 1;\n}\n\nint32 b() {\n  return 2;\n}\n");
}

TEST(FormatterTest, LineCommentReEmittedBeforeNode) {
  std::string out = Formatter::format(
      "// greeting\nint32 f(){return 1;}", "t.script");
  EXPECT_EQ(out, "// greeting\nint32 f() {\n  return 1;\n}\n");
}

TEST(FormatterTest, BlockCommentPreserved) {
  std::string out = Formatter::format(
      "/* multi\n   line */\nint32 f(){return 1;}", "t.script");
  EXPECT_NE(out.find("/* multi\n   line */"), std::string::npos);
  EXPECT_NE(out.find("int32 f() {"), std::string::npos);
}

TEST(FormatterTest, CommentInsideStringNotLifted) {
  std::string out = Formatter::format(
      "string f(){return \"// not a comment\";}", "t.script");
  EXPECT_NE(out.find("return \"// not a comment\";"), std::string::npos);
  // The string contents must not be promoted to a real comment line.
  EXPECT_EQ(out.find("\n// not a comment"), std::string::npos);
}

TEST(FormatterTest, NumericLiteralsNormalizeToDecimal) {
  std::string out = Formatter::format(
      "int64 f(){return 0x10+0b11+0o17;}", "t.script");
  EXPECT_NE(out.find("return 16 + 3 + 15;"), std::string::npos);
}

TEST(FormatterTest, EnumGetsExplicitValues) {
  std::string out = Formatter::format("enum C{A,B=5,D}", "t.script");
  EXPECT_NE(out.find("enum C { A = 0, B = 5, D = 6 }"), std::string::npos);
}

TEST(FormatterTest, StructFieldsAndMethods) {
  std::string out = Formatter::format(
      "struct P{int32 x;int32 y;int32 mag(){return x*x+y*y;}}", "t.script");
  EXPECT_NE(out.find("struct P {"), std::string::npos);
  EXPECT_NE(out.find("  int32 x;"), std::string::npos);
  EXPECT_NE(out.find("  int32 mag() {"), std::string::npos);
}

TEST(FormatterTest, ImportStatement) {
  std::string out = Formatter::format(
      "import \"lib.script\";\nint32 f(){return 1;}", "t.script");
  EXPECT_NE(out.find("import \"lib.script\";"), std::string::npos);
}

TEST(FormatterTest, StringEscapesRoundTrip) {
  std::string out = Formatter::format(
      "string f(){return \"a\\n\\t\\\"b\\\\\";}", "t.script");
  EXPECT_NE(out.find("return \"a\\n\\t\\\"b\\\\\";"), std::string::npos);
}

TEST(FormatterTest, InterpolatedStringPreserved) {
  std::string out = Formatter::format(
      "string f(int32 n){return \"n=${n + 1}\";}", "t.script");
  EXPECT_NE(out.find("${n + 1}"), std::string::npos);
}

TEST(FormatterTest, LambdaFormatting) {
  std::string out = Formatter::format(
      "auto f(){auto g=fn(int32 x)->int32{return x*2;};return g(1);}",
      "t.script");
  EXPECT_NE(out.find("auto g = fn(int32 x) -> int32 {"), std::string::npos);
}

TEST(FormatterTest, OutputIsIdempotent) {
  std::string src =
      "// c\nstruct P{int32 x;}\nint32 f(int32 n){if(n>0){return n*2;}return -n;}";
  std::string once = Formatter::format(src, "t.script");
  EXPECT_EQ(Formatter::format(once, "t.script"), once);
}

TEST(FormatterTest, EmptySourceFormatsToEmpty) {
  EXPECT_EQ(Formatter::format("", "t.script"), "\n");
}

TEST(FormatterTest, InvalidSourceThrowsParseError) {
  EXPECT_THROW(Formatter::format("int32 f( { return ;", "t.script"), ParseError);
}

TEST(FormatterTest, UnparseableStatementThrows) {
  EXPECT_THROW(Formatter::format("int32 f(){ 1 + ; }", "t.script"), ParseError);
}
