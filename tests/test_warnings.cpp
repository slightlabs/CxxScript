#include <algorithm>

#include "ScriptManager.h"
#include <gtest/gtest.h>

using namespace Script;

namespace {
// Returns (loaded, warnings) — warnings never fail the load.
std::pair<bool, std::vector<std::string>> loadWithWarnings(
    const std::string &source) {
  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool ok = manager.loadScriptSource(source, "t.script", errors);
  std::vector<std::string> warnings;
  for (const auto &e : errors) {
    if (e.isWarning) {
      warnings.push_back(e.toString());
    }
  }
  return {ok, warnings};
}

bool anyWarningContains(const std::vector<std::string> &warnings,
                        const std::string &needle) {
  return std::any_of(warnings.begin(), warnings.end(),
                     [&](const std::string &w) {
                       return w.find(needle) != std::string::npos;
                     });
}
} // namespace

// ---------------------------------------------------------------------------
// Unused variables
// ---------------------------------------------------------------------------

TEST(WarningsTest, UnusedVariableWarns) {
  auto [ok, warnings] = loadWithWarnings(
      "void f() { int32 x = 5; }");
  EXPECT_TRUE(ok);
  EXPECT_TRUE(anyWarningContains(warnings, "Unused variable 'x'"))
      << ::testing::PrintToString(warnings);
}

TEST(WarningsTest, ReadVariableDoesNotWarn) {
  auto [ok, warnings] = loadWithWarnings(
      "int32 f() { int32 x = 5; return x + 1; }");
  EXPECT_TRUE(ok);
  EXPECT_FALSE(anyWarningContains(warnings, "Unused variable"));
}

TEST(WarningsTest, WriteOnlyVariableWarns) {
  auto [ok, warnings] = loadWithWarnings(
      "int32 f() { int32 x = 0; x = 9; return 0; }");
  EXPECT_TRUE(ok);
  EXPECT_TRUE(anyWarningContains(warnings, "Unused variable 'x'"));
}

TEST(WarningsTest, UnusedParameterDoesNotWarn) {
  auto [ok, warnings] = loadWithWarnings(
      "int32 f(int32 unusedParam) { return 1; }");
  EXPECT_TRUE(ok);
  EXPECT_FALSE(anyWarningContains(warnings, "Unused variable"));
}

TEST(WarningsTest, BlockScopedUnusedWarnsAtScopeExit) {
  auto [ok, warnings] = loadWithWarnings(R"(
void f() {
  if (true) {
    int32 inner = 1;
  }
})");
  EXPECT_TRUE(ok);
  EXPECT_TRUE(anyWarningContains(warnings, "Unused variable 'inner'"));
}

TEST(WarningsTest, UnusedForeachVarWarns) {
  auto [ok, warnings] = loadWithWarnings(R"(
int32 f() {
  int32[] a = [1, 2];
  int32 n = 0;
  for (int32 x : a) { n++; }
  return n;
})");
  EXPECT_TRUE(ok);
  EXPECT_TRUE(anyWarningContains(warnings, "Unused variable 'x'"));
}

// ---------------------------------------------------------------------------
// Narrowing conversions
// ---------------------------------------------------------------------------

TEST(WarningsTest, NarrowingVariableWarns) {
  auto [ok, warnings] = loadWithWarnings(
      "void f() { int64 big = 1; int32 x = big; }");
  EXPECT_TRUE(ok);
  EXPECT_TRUE(anyWarningContains(warnings, "Narrowing conversion"));
}

TEST(WarningsTest, OutOfRangeLiteralWarns) {
  auto [ok, warnings] = loadWithWarnings(
      "void f() { int8 b = 300; }");
  EXPECT_TRUE(ok);
  EXPECT_TRUE(anyWarningContains(warnings, "Narrowing conversion"));
}

TEST(WarningsTest, FittingLiteralDoesNotWarn) {
  auto [ok, warnings] = loadWithWarnings(
      "void f() { int8 b = 100; int16 s = 30000; }");
  EXPECT_TRUE(ok);
  EXPECT_FALSE(anyWarningContains(warnings, "Narrowing"));
}

TEST(WarningsTest, SameWidthArithmeticDoesNotWarn) {
  auto [ok, warnings] = loadWithWarnings(
      "int32 f(int32 a, int32 b) { int32 r = a * b + 10; return r; }");
  EXPECT_TRUE(ok);
  EXPECT_FALSE(anyWarningContains(warnings, "Narrowing"));
}

TEST(WarningsTest, NarrowingArgumentWarns) {
  auto [ok, warnings] = loadWithWarnings(R"(
void takesSmall(int8 v) { }
void f() { int64 big = 1; takesSmall(big); })");
  EXPECT_TRUE(ok);
  EXPECT_TRUE(anyWarningContains(warnings, "Narrowing conversion"));
}

TEST(WarningsTest, FloatToIntWarns) {
  auto [ok, warnings] = loadWithWarnings(
      "void f() { double d = 1.5; int32 x = d; }");
  EXPECT_TRUE(ok);
  EXPECT_TRUE(anyWarningContains(warnings, "Narrowing"));
}

// ---------------------------------------------------------------------------
// Missing return
// ---------------------------------------------------------------------------

TEST(WarningsTest, MissingReturnWarns) {
  auto [ok, warnings] = loadWithWarnings(
      "int32 f(int32 x) { if (x > 0) { return 1; } }");
  EXPECT_TRUE(ok);
  EXPECT_TRUE(anyWarningContains(warnings, "may reach the end"));
}

TEST(WarningsTest, FullReturnCoverageDoesNotWarn) {
  auto [ok, warnings] = loadWithWarnings(R"(
int32 f(int32 x) {
  if (x > 0) { return 1; } else { return 2; }
})");
  EXPECT_TRUE(ok);
  EXPECT_FALSE(anyWarningContains(warnings, "may reach the end"));
}

TEST(WarningsTest, WhileTrueReturnDoesNotWarn) {
  auto [ok, warnings] = loadWithWarnings(
      "int32 f() { while (true) { return 1; } }");
  EXPECT_TRUE(ok);
  EXPECT_FALSE(anyWarningContains(warnings, "may reach the end"));
}

TEST(WarningsTest, WhileTrueWithBreakWarns) {
  auto [ok, warnings] = loadWithWarnings(
      "int32 f(int32 x) { while (true) { if (x > 0) { break; } } }");
  EXPECT_TRUE(ok);
  EXPECT_TRUE(anyWarningContains(warnings, "may reach the end"));
}

TEST(WarningsTest, ReturnAfterBreakableLoopDoesNotWarn) {
  auto [ok, warnings] = loadWithWarnings(
      "int32 f() { while (true) { break; } return 0; }");
  EXPECT_TRUE(ok);
  EXPECT_FALSE(anyWarningContains(warnings, "may reach the end"));
}

TEST(WarningsTest, SwitchAllCasesReturnDoesNotWarn) {
  auto [ok, warnings] = loadWithWarnings(R"(
int32 f(int32 x) {
  switch (x) {
    case 1: { return 1; }
    default: { return 0; }
  }
})");
  EXPECT_TRUE(ok);
  EXPECT_FALSE(anyWarningContains(warnings, "may reach the end"));
}

TEST(WarningsTest, VoidProcedureNeverWarns) {
  auto [ok, warnings] = loadWithWarnings(
      "void f(int32 x) { if (x > 0) { int32 y = x; } }");
  EXPECT_TRUE(ok);
  EXPECT_FALSE(anyWarningContains(warnings, "may reach the end"));
}

// ---------------------------------------------------------------------------
// Warnings don't fail compilation
// ---------------------------------------------------------------------------

TEST(WarningsTest, WarningsDoNotFailLoadOrRun) {
  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(
      "int32 f() { int64 big = 5; int32 x = big; return x; }", "t.script",
      errors));
  Value result;
  std::string err;
  ASSERT_TRUE(manager.executeProcedure("f", {}, result, err)) << err;
  EXPECT_EQ(std::get<int32_t>(result), 5);
}

TEST(WarningsTest, ErrorsStillFailAlongsideWarnings) {
  auto [ok, warnings] = loadWithWarnings(
      "void f() { int64 big = 1; int32 x = big; int32 y = \"s\"; }");
  EXPECT_FALSE(ok);
  EXPECT_TRUE(anyWarningContains(warnings, "Narrowing"));
}
