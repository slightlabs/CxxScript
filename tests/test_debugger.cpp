#include "ScriptManager.h"
#include <gtest/gtest.h>
#include <vector>

using namespace Script;

namespace {

bool load(ScriptManager &mgr, const std::string &source) {
  std::vector<CompilationError> errors;
  return mgr.loadScriptSource(source, "test.script", errors);
}

Value callProc(ScriptManager &mgr, const std::string &proc,
               const std::vector<Value> &args = {}) {
  Value result;
  std::string err;
  EXPECT_TRUE(mgr.executeProcedure(proc, args, result, err)) << err;
  return result;
}

std::vector<Interpreter::DebugContext> runWithHook(
    ScriptManager &mgr, const std::string &proc,
    const std::vector<Value> &args = {}) {
  std::vector<Interpreter::DebugContext> events;
  mgr.setDebugHook(
      [&](const Interpreter::DebugContext &ctx) { events.push_back(ctx); });
  callProc(mgr, proc, args);
  mgr.setDebugHook(nullptr);
  return events;
}

int32_t asInt32(const Value &v) { return std::get<int32_t>(v); }

} // namespace

// Hook fires once per statement, in execution order.
TEST(DebugHook, InvokedPerStatement) {
  ScriptManager mgr;
  ASSERT_TRUE(load(mgr, R"(
int32 main() {
  int32 a = 1;
  a = a + 2;
  return a;
}
)"));
  auto events = runWithHook(mgr, "main");
  // varDecl, assign, return -> 3 statements
  EXPECT_EQ(events.size(), 3u);
}

// Line/column reflect the statement being executed.
TEST(DebugHook, ReportsSourceLocation) {
  ScriptManager mgr;
  ASSERT_TRUE(load(mgr,
                   "int32 main() {\n"
                   "  int32 a = 1;\n"
                   "  return a;\n"
                   "}\n"));
  auto events = runWithHook(mgr, "main");
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].line, 2);
  EXPECT_EQ(events[1].line, 3);
  EXPECT_EQ(events[1].procedure, "main");
  EXPECT_EQ(events[1].filename, "test.script");
}

// Snapshot contains params and locals, updated between statements.
TEST(DebugHook, SnapshotReflectsVariables) {
  ScriptManager mgr;
  ASSERT_TRUE(load(mgr, R"(
int32 f(int32 x) {
  int32 y = x * 2;
  return y;
}
)"));
  auto events = runWithHook(mgr, "f", {Value(int32_t(5))});
  ASSERT_GE(events.size(), 2u);
  // First statement: x bound, y not yet declared.
  ASSERT_TRUE(events[0].variables.count("x"));
  EXPECT_EQ(asInt32(events[0].variables["x"]), 5);
  EXPECT_FALSE(events[0].variables.count("y"));
  // Second statement: y visible with computed value.
  ASSERT_TRUE(events[1].variables.count("y"));
  EXPECT_EQ(asInt32(events[1].variables["y"]), 10);
}

// Inner scopes shadow outer values in the snapshot.
TEST(DebugHook, InnerScopeShadows) {
  ScriptManager mgr;
  ASSERT_TRUE(load(mgr, R"(
int32 main() {
  int32 x = 1;
  {
    int32 x = 9;
    int32 inner = x;
  }
  return x;
}
)"));
  auto events = runWithHook(mgr, "main");
  // Event at line 6: `int32 inner = x;` inside the block sees x == 9.
  bool sawShadow = false;
  for (const auto &e : events) {
    if (e.line == 6) {
      sawShadow = true;
      EXPECT_EQ(asInt32(e.variables.at("x")), 9);
    }
  }
  ASSERT_TRUE(sawShadow);
  // Return sees outer x again.
  EXPECT_EQ(asInt32(events.back().variables.at("x")), 1);
}

// Nested procedure calls report the callee's name and its own locals.
TEST(DebugHook, ReportsCalleeContext) {
  ScriptManager mgr;
  ASSERT_TRUE(load(mgr, R"(
int32 add(int32 a, int32 b) { return a + b; }
int32 main() { return add(1, 2); }
)"));
  auto events = runWithHook(mgr, "main");
  bool sawAdd = false;
  for (const auto &e : events) {
    if (e.procedure == "add") {
      sawAdd = true;
      EXPECT_EQ(asInt32(e.variables.at("a")), 1);
      EXPECT_EQ(asInt32(e.variables.at("b")), 2);
    }
  }
  EXPECT_TRUE(sawAdd);
}

// Loop bodies are visited once per iteration.
TEST(DebugHook, LoopIterations) {
  ScriptManager mgr;
  ASSERT_TRUE(load(mgr, R"(
int32 main() {
  int32 s = 0;
  for (int32 i = 0; i < 3; i = i + 1) {
    s = s + i;
  }
  return s;
}
)"));
  auto events = runWithHook(mgr, "main");
  int bodyStmts = 0;
  for (const auto &e : events) {
    if (e.line == 5)
      ++bodyStmts;
  }
  EXPECT_EQ(bodyStmts, 3);
}

// Clearing the hook stops callbacks.
TEST(DebugHook, ClearStopsCallbacks) {
  ScriptManager mgr;
  ASSERT_TRUE(load(mgr, "int32 main() { return 1; }"));
  int calls = 0;
  mgr.setDebugHook([&](const Interpreter::DebugContext &) { ++calls; });
  callProc(mgr, "main");
  EXPECT_GT(calls, 0);
  calls = 0;
  mgr.setDebugHook(nullptr);
  callProc(mgr, "main");
  EXPECT_EQ(calls, 0);
}

// Hook fires for statements before a runtime error propagates.
TEST(DebugHook, FiresBeforeRuntimeError) {
  ScriptManager mgr;
  ASSERT_TRUE(load(mgr, R"(
int32 main() {
  int32 a = 1;
  int32 b = a / 0;
  return b;
}
)"));
  std::vector<Interpreter::DebugContext> events;
  mgr.setDebugHook(
      [&](const Interpreter::DebugContext &ctx) { events.push_back(ctx); });
  Value result;
  std::string err;
  EXPECT_FALSE(mgr.executeProcedure("main", {}, result, err));
  // varDecl a + the failing division statement both reported
  ASSERT_GE(events.size(), 2u);
  EXPECT_EQ(events[1].line, 4);
}

// No hook installed: execution is unaffected.
TEST(DebugHook, DisabledByDefault) {
  ScriptManager mgr;
  ASSERT_TRUE(load(mgr, "int32 main() { return 42; }"));
  EXPECT_EQ(asInt32(callProc(mgr, "main")), 42);
}
