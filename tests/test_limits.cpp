// Coverage for the execution/memory guardrails: array size, string length,
// allocation counts, step budgets, call depth — including reset behavior
// and the fact that limit violations are fatal (not catchable by scripts).
#include <algorithm>
#include "ScriptManager.h"
#include <gtest/gtest.h>

using namespace Script;

namespace {
std::string runFails(ScriptManager &manager, const std::string &source,
                     const std::string &proc,
                     const std::vector<Value> &args = {}) {
  std::vector<CompilationError> errors;
  EXPECT_TRUE(manager.loadScriptSource(source, "t.script", errors));
  Value result;
  std::string errorMsg;
  EXPECT_FALSE(manager.executeProcedure(proc, args, result, errorMsg));
  return errorMsg;
}
} // namespace

// --- Array size limit -------------------------------------------------------

TEST(LimitsTest, ArrayLiteralSizeLimitEnforced) {
  ScriptManager m;
  m.setMemoryLimits(3, 0, 0);
  std::string msg =
      runFails(m, "int32 f() { int32[] a = [1,2,3,4]; return len(a); }", "f");
  EXPECT_NE(msg.find("Array size limit exceeded"), std::string::npos) << msg;
}

TEST(LimitsTest, ArrayLiteralAtLimitOk) {
  ScriptManager m;
  m.setMemoryLimits(4, 0, 0);
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int32 f() { int32[] a = [1,2,3,4]; return len(a); }", "t.script",
      errors));
  Value v;
  std::string msg;
  ASSERT_TRUE(m.executeProcedure("f", {}, v, msg)) << msg;
  EXPECT_EQ(std::get<int32_t>(v), 4);
}

TEST(LimitsTest, PushBeyondArrayLimitEnforced) {
  ScriptManager m;
  m.setMemoryLimits(3, 0, 0);
  std::string msg = runFails(
      m, "int32 f() { int32[] a = [1,2,3]; push(a, 4); return len(a); }", "f");
  EXPECT_NE(msg.find("Array size limit exceeded"), std::string::npos) << msg;
}

TEST(LimitsTest, InsertBeyondArrayLimitEnforced) {
  ScriptManager m;
  m.setMemoryLimits(3, 0, 0);
  std::string msg = runFails(
      m, "int32 f() { int32[] a = [1,2,3]; insert(a, 0, 9); return len(a); }",
      "f");
  EXPECT_NE(msg.find("Array size limit exceeded"), std::string::npos) << msg;
}

// --- String length limit ----------------------------------------------------

TEST(LimitsTest, StringConcatLengthLimitEnforced) {
  ScriptManager m;
  m.setMemoryLimits(0, 10, 0);
  std::string msg = runFails(
      m, "string f() { return \"12345\" + \"67890X\"; }", "f");
  EXPECT_NE(msg.find("String length limit exceeded"), std::string::npos) << msg;
}

TEST(LimitsTest, StringConcatCompoundAssignLimitEnforced) {
  ScriptManager m;
  m.setMemoryLimits(0, 10, 0);
  std::string msg = runFails(
      m, "string f() { string s = \"12345\"; s += \"67890X\"; return s; }",
      "f");
  EXPECT_NE(msg.find("String length limit exceeded"), std::string::npos) << msg;
}

TEST(LimitsTest, InterpolatedStringLengthLimitEnforced) {
  ScriptManager m;
  m.setMemoryLimits(0, 5, 0);
  std::string msg = runFails(
      m, "string f() { int32 n = 1234567; return \"n=${n}\"; }", "f");
  EXPECT_NE(msg.find("String length limit exceeded"), std::string::npos) << msg;
}

TEST(LimitsTest, RepeatLengthLimitEnforced) {
  ScriptManager m;
  m.setMemoryLimits(0, 8, 0);
  std::string msg = runFails(m, "string f() { return repeat(\"ab\", 5); }", "f");
  EXPECT_NE(msg.find("String length limit exceeded"), std::string::npos) << msg;
}

TEST(LimitsTest, StringUnderLimitOk) {
  ScriptManager m;
  m.setMemoryLimits(0, 11, 0);
  std::vector<CompilationError> errors;
  ASSERT_TRUE(
      m.loadScriptSource("string f() { return \"12345\" + \"67890\"; }",
                         "t.script", errors));
  Value v;
  std::string msg;
  ASSERT_TRUE(m.executeProcedure("f", {}, v, msg)) << msg;
  EXPECT_EQ(std::get<std::string>(v), "1234567890");
}

// --- Allocation count -------------------------------------------------------

TEST(LimitsTest, ArrayLiteralAllocationCountEnforced) {
  ScriptManager m;
  m.setMemoryLimits(0, 0, 2); // two container allocations allowed
  std::string msg = runFails(
      m,
      "int32 f() { int32[] a=[1]; int32[] b=[2]; int32[] c=[3]; return 0; }",
      "f");
  EXPECT_NE(msg.find("Maximum array allocation count exceeded"),
            std::string::npos)
      << msg;
}

TEST(LimitsTest, AllocationCountResetsPerExecution) {
  ScriptManager m;
  m.setMemoryLimits(0, 0, 1); // one allocation per top-level call
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int32 f() { int32[] a=[1]; return len(a); }", "t.script", errors));
  // Each call allocates once — two sequential calls must both succeed.
  for (int i = 0; i < 2; ++i) {
    Value v;
    std::string msg;
    ASSERT_TRUE(m.executeProcedure("f", {}, v, msg)) << msg;
  }
}

// --- Step budget ------------------------------------------------------------

TEST(LimitsTest, StepBudgetSharedAcrossCallTree) {
  ScriptManager m;
  // Small budget: nested calls must share it, not get their own.
  m.setExecutionLimits(0, 25);
  std::string msg = runFails(
      m,
      "void inner(int32 n) { int32 i = 0; while (i < n) { i += 1; } }\n"
      "int32 outer() { inner(10); inner(10); inner(10); return 1; }",
      "outer");
  EXPECT_NE(msg.find("Maximum execution steps exceeded"), std::string::npos)
      << msg;
}

TEST(LimitsTest, StepBudgetResetsPerExecution) {
  ScriptManager m;
  // Enough for one call but far less than two cumulative calls.
  m.setExecutionLimits(0, 60);
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int32 f() { int32 i = 0; while (i < 3) { i += 1; } return i; }",
      "t.script", errors));
  for (int i = 0; i < 2; ++i) {
    Value v;
    std::string msg;
    ASSERT_TRUE(m.executeProcedure("f", {}, v, msg)) << msg;
    EXPECT_EQ(std::get<int32_t>(v), 3);
  }
}

TEST(LimitsTest, StepLimitNotCatchableByScript) {
  ScriptManager m;
  m.setExecutionLimits(0, 10);
  std::string msg = runFails(
      m,
      "int32 f() {\n"
      "  try { int32 i = 0; while (true) { i += 1; } return i; }\n"
      "  catch (e) { return -1; }\n"
      "}",
      "f");
  EXPECT_NE(msg.find("Maximum execution steps exceeded"), std::string::npos)
      << msg;
}

TEST(LimitsTest, CallDepthLimitNotCatchableByScript) {
  ScriptManager m;
  m.setExecutionLimits(16, 0);
  std::string msg = runFails(
      m,
      "int32 r(int32 x) { return r(x + 1); }\n"
      "int32 f() { try { return r(0); } catch (e) { return -1; } }",
      "f");
  EXPECT_NE(msg.find("Maximum call depth exceeded"), std::string::npos) << msg;
}

TEST(LimitsTest, MemoryLimitNotCatchableByScript) {
  ScriptManager m;
  m.setMemoryLimits(2, 0, 0);
  std::string msg = runFails(
      m,
      "int32 f() {\n"
      "  try { int32[] a = [1,2,3,4]; return len(a); }\n"
      "  catch (e) { return -1; }\n"
      "}",
      "f");
  EXPECT_NE(msg.find("Array size limit exceeded"), std::string::npos) << msg;
}

TEST(LimitsTest, FinallyStillRunsOnFatalLimit) {
  ScriptManager m;
  m.setMemoryLimits(3, 0, 0); // array-size limit is fatal but not per-statement
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int32 f() {\n"
      "  try { int32[] a = [1,2,3,4]; return len(a); }\n"
      "  finally { println(\"cleanup\"); }\n"
      "}",
      "t.script", errors));
  std::string captured;
  m.setOutputCallback(
      [&captured](const std::string &s) { captured += s; });
  Value v;
  std::string msg;
  EXPECT_FALSE(m.executeProcedure("f", {}, v, msg));
  EXPECT_NE(msg.find("Array size limit exceeded"), std::string::npos);
  EXPECT_EQ(captured, "cleanup\n");
}

TEST(LimitsTest, FinallyGetsBoundedStepReserve) {
  // An exhausted step budget still lets finally run cleanup — a bounded
  // reserve is granted so the fatal error can't starve it.
  ScriptManager m;
  m.setExecutionLimits(0, 10);
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int32 f() {\n"
      "  try { int32 i = 0; while (true) { i += 1; } return i; }\n"
      "  finally { println(\"cleanup\"); }\n"
      "}",
      "t.script", errors));
  std::string captured;
  m.setOutputCallback(
      [&captured](const std::string &s) { captured += s; });
  Value v;
  std::string msg;
  EXPECT_FALSE(m.executeProcedure("f", {}, v, msg));
  EXPECT_NE(msg.find("Maximum execution steps exceeded"), std::string::npos);
  EXPECT_EQ(captured, "cleanup\n");
}

TEST(LimitsTest, FinallyReserveIsBounded) {
  // The reserve is finite — a finally that loops forever still dies.
  ScriptManager m;
  m.setExecutionLimits(0, 10);
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int32 f() {\n"
      "  try { int32 i = 0; while (true) { i += 1; } return i; }\n"
      "  finally { int32 j = 0; while (true) { j += 1; } }\n"
      "}",
      "t.script", errors));
  Value v;
  std::string msg;
  EXPECT_FALSE(m.executeProcedure("f", {}, v, msg));
  EXPECT_NE(msg.find("Maximum execution steps exceeded"), std::string::npos);
}

// --- Clearing limits --------------------------------------------------------

TEST(LimitsTest, ClearExecutionLimitsRestoresUnbounded) {
  ScriptManager m;
  m.setExecutionLimits(4, 0);
  std::string msg = runFails(
      m, "int32 r(int32 x) { return r(x + 1); }", "r",
      {static_cast<int32_t>(0)});
  EXPECT_NE(msg.find("Maximum call depth exceeded"), std::string::npos);

  ScriptManager m2;
  m2.setExecutionLimits(4, 0);
  m2.clearExecutionLimits();
  // Deeper than 4 now runs fine.
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m2.loadScriptSource(
      "int32 r(int32 x) { return x >= 20 ? x : r(x + 1); }", "t.script",
      errors));
  Value v;
  ASSERT_TRUE(m2.executeProcedure("r", {static_cast<int32_t>(0)}, v, msg))
      << msg;
  EXPECT_EQ(std::get<int32_t>(v), 20);
}

TEST(LimitsTest, ClearMemoryLimitsRestoresUnbounded) {
  ScriptManager m;
  m.setMemoryLimits(2, 5, 0);
  m.clearMemoryLimits();
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "string f() { int32[] a = [1,2,3,4,5]; return \"a much longer string\"; }",
      "t.script", errors));
  Value v;
  std::string msg;
  ASSERT_TRUE(m.executeProcedure("f", {}, v, msg)) << msg;
}

// --- Limits combined --------------------------------------------------------

TEST(LimitsTest, BothLimitsCanBeActive) {
  ScriptManager m;
  m.setExecutionLimits(5, 0);
  m.setMemoryLimits(3, 0, 0);
  std::string msg = runFails(
      m, "int32 f() { int32[] a = [1,2,3,4]; return len(a); }", "f");
  EXPECT_NE(msg.find("Array size limit exceeded"), std::string::npos) << msg;
  std::string msg2 = runFails(
      m, "int32 r(int32 x) { return r(x + 1); }", "r",
      {static_cast<int32_t>(0)});
  EXPECT_NE(msg2.find("Maximum call depth exceeded"), std::string::npos)
      << msg2;
}

// --- Native stack budget ----------------------------------------------------

TEST(LimitsTest, NativeStackBudgetStopsDeepRecursion) {
  // A small native-stack budget converts unbounded recursion into a clean
  // fatal error rather than a native stack overflow.
  ScriptManager m;
  m.setExecutionLimits(0, 0, 256 * 1024);
  std::string msg = runFails(
      m, "int32 r(int32 x) { return r(x + 1); }", "r",
      {static_cast<int32_t>(0)});
  EXPECT_NE(msg.find("Native stack limit exceeded"), std::string::npos) << msg;
}

TEST(LimitsTest, NativeStackBudgetIsFatalNotCatchable) {
  // Like other guardrail violations, the stack guard bypasses try/catch.
  ScriptManager m;
  m.setExecutionLimits(0, 0, 256 * 1024);
  std::string msg = runFails(m, R"(
int32 r(int32 x) {
  try { return r(x + 1); } catch (e) { return -1; }
})",
                             "r", {static_cast<int32_t>(0)});
  EXPECT_NE(msg.find("Native stack limit exceeded"), std::string::npos) << msg;
}

TEST(LimitsTest, ShallowRecursionUnderBudgetStillWorks) {
  // Ordinary call depth is unaffected by the stack budget. Sanitizer builds
  // inflate frames several-fold, so keep the budget generous.
  ScriptManager m;
  m.setExecutionLimits(0, 0, 16 * 1024 * 1024);
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int32 f(int32 n) { return n == 0 ? 0 : f(n - 1) + 1; }", "t.script",
      errors));
  Value v;
  std::string msg;
  ASSERT_TRUE(m.executeProcedure("f", {static_cast<int32_t>(50)}, v, msg))
      << msg;
  EXPECT_EQ(std::get<int32_t>(v), 50);
}

TEST(LimitsTest, StackBudgetDisabledByDefault) {
  // With no budget set, call-depth limits still govern recursion.
  ScriptManager m;
  m.setExecutionLimits(32, 0);
  std::string msg = runFails(
      m, "int32 r(int32 x) { return r(x + 1); }", "r",
      {static_cast<int32_t>(0)});
  EXPECT_NE(msg.find("Maximum call depth exceeded"), std::string::npos) << msg;
}

TEST(LimitsTest, ClearExecutionLimitsClearsStackBudget) {
  ScriptManager m;
  m.setExecutionLimits(0, 0, 1024); // absurdly small — anything would trip it
  m.clearExecutionLimits();
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int32 f(int32 n) { return n == 0 ? 0 : f(n - 1) + 1; }", "t.script",
      errors));
  Value v;
  std::string msg;
  ASSERT_TRUE(m.executeProcedure("f", {static_cast<int32_t>(20)}, v, msg))
      << msg;
}
