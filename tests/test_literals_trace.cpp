#include "ScriptManager.h"
#include <gtest/gtest.h>

using namespace Script;

namespace {
Value run(const std::string &source, const std::string &proc,
          const std::vector<Value> &args = {}) {
  ScriptManager manager;
  std::vector<CompilationError> errors;
  EXPECT_TRUE(manager.loadScriptSource(source, "test.script", errors));
  if (!errors.empty()) {
    for (const auto &e : errors) {
      ADD_FAILURE() << e.toString();
    }
    return static_cast<int32_t>(0);
  }
  Value result;
  std::string errorMsg;
  EXPECT_TRUE(manager.executeProcedure(proc, args, result, errorMsg))
      << errorMsg;
  return result;
}
} // namespace

// ---------------------------------------------------------------------------
// Literal forms
// ---------------------------------------------------------------------------

TEST(LiteralTest, HexBinaryOctal) {
  EXPECT_EQ(std::get<int32_t>(run(R"(
      int32 f() { return 0xFF + 0b1010 + 0o17; }  // 255 + 10 + 15 = 280
      )",
                                  "f")),
            280);
}

TEST(LiteralTest, UpperCasePrefix) {
  EXPECT_EQ(std::get<int32_t>(run(R"(
      int32 f() { return 0X10 + 0B11 + 0O7; }  // 16 + 3 + 7 = 26
      )",
                                  "f")),
            26);
}

TEST(LiteralTest, DigitSeparators) {
  EXPECT_EQ(std::get<int32_t>(run(R"(
      int32 f() { return 1_000_000 + 0xFF_FF; }  // 1000000 + 65535
      )",
                                  "f")),
            1065535);
}

TEST(LiteralTest, ExponentFloat) {
  EXPECT_DOUBLE_EQ(std::get<double>(run(R"(
      double f() { return 1.5e2 + 2E-1; }  // 150 + 0.2
      )",
                                       "f")),
                   150.2);
}

TEST(LiteralTest, InvalidLiterals) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  EXPECT_FALSE(m.loadScriptSource("int32 f() { return 0x; }", "t", errors));
  errors.clear();
  EXPECT_FALSE(m.loadScriptSource("int32 f() { return 0b102; }", "t", errors));
  errors.clear();
  EXPECT_FALSE(m.loadScriptSource("int32 f() { return 1__2; }", "t", errors));
  errors.clear();
  EXPECT_FALSE(m.loadScriptSource("int32 f() { return 1_; }", "t", errors));
}

// ---------------------------------------------------------------------------
// Stack traces
// ---------------------------------------------------------------------------

TEST(StackTraceTest, NestedTrace) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(R"(
      int32 inner() {
        return 1 / 0;
      }
      int32 mid() {
        return inner();
      }
      int32 outer() {
        return mid();
      })",
                                 "t.script", errors));

  Value result;
  std::string msg;
  EXPECT_FALSE(m.executeProcedure("outer", {}, result, msg));
  // inner raised the error; trace lists callers innermost-first
  EXPECT_NE(msg.find("inner"), std::string::npos);
  EXPECT_NE(msg.find("mid"), std::string::npos);
  EXPECT_NE(msg.find("outer"), std::string::npos);
  EXPECT_NE(msg.find("Division"), std::string::npos)
      << "trace message: " << msg;
}

TEST(StackTraceTest, TraceContainsCallLines) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int32 boom() { error(\"fail\"); return 0; }\n"
      "int32 caller() {\n"
      "  return boom();\n"   // line 3
      "}\n",
      "t.script", errors));

  Value result;
  std::string msg;
  EXPECT_FALSE(m.executeProcedure("caller", {}, result, msg));
  EXPECT_NE(msg.find("caller"), std::string::npos);
  EXPECT_NE(msg.find(":3"), std::string::npos) << "trace message: " << msg;
}

// ---------------------------------------------------------------------------
// REPL snippets
// ---------------------------------------------------------------------------

TEST(SnippetTest, VariablesPersistAcrossCalls) {
  ScriptManager m;
  Value result;
  std::string err;
  ASSERT_TRUE(m.evaluateSnippet("int32 x = 10;", "<repl>", result, err)) << err;
  ASSERT_TRUE(m.evaluateSnippet("x += 5;", "<repl>", result, err)) << err;
  ASSERT_TRUE(m.evaluateSnippet("return x * 2;", "<repl>", result, err)) << err;
  EXPECT_EQ(std::get<int32_t>(result), 30);
}

TEST(SnippetTest, SnippetsCanCallProcedures) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource("int32 square(int32 n) { return n * n; }",
                                 "s.script", errors));
  Value result;
  std::string err;
  ASSERT_TRUE(m.evaluateSnippet("return square(7);", "<repl>", result, err))
      << err;
  EXPECT_EQ(std::get<int32_t>(result), 49);
}

TEST(SnippetTest, CompileErrorReported) {
  ScriptManager m;
  Value result;
  std::string err;
  EXPECT_FALSE(m.evaluateSnippet("int32 x = ;", "<repl>", result, err));
  EXPECT_FALSE(err.empty());
}
