// Coverage for the bytecode VM: an alternate execution engine that compiles
// procedure bodies to bytecode and runs them on a stack machine. These tests
// assert parity with the tree-walking interpreter plus VM-specific behavior
// (dynamic scoping, captures, finally unwinding, fatal limits).
#include "ScriptManager.h"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

using namespace Script;

namespace {

struct Outcome {
  bool ok = false;
  std::string value; // ValueHelper::toString of the result (when ok)
  std::string error; // error message (when !ok)
};

Outcome run(ScriptManager &m, const std::string &source,
            const std::string &proc,
            const std::vector<Value> &args = {}) {
  std::vector<CompilationError> errors;
  Outcome o;
  if (!m.loadScriptSource(source, "t.script", errors)) {
    for (const auto &e : errors)
      o.error += e.toString() + "\n";
    return o;
  }
  Value v;
  std::string msg;
  o.ok = m.executeProcedure(proc, args, v, msg);
  if (o.ok) {
    o.value = ValueHelper::toString(v);
  } else {
    o.error = msg;
  }
  return o;
}

// Run `source` on both engines and require identical outcomes.
void expectParity(const std::string &source, const std::string &proc,
                  const std::vector<Value> &args = {}) {
  ScriptManager tree, vm;
  vm.setVMEnabled(true);
  // CXXSCRIPT_VM=1 makes every interpreter default to the VM; force the
  // tree-walker explicitly so the comparison is real.
  tree.setVMEnabled(false);
  EXPECT_TRUE(vm.isVMEnabled());
  EXPECT_FALSE(tree.isVMEnabled());
  Outcome a = run(tree, source, proc, args);
  Outcome b = run(vm, source, proc, args);
  EXPECT_EQ(a.ok, b.ok) << "tree: " << a.error << " vm: " << b.error;
  if (a.ok) {
    EXPECT_EQ(a.value, b.value);
  } else {
    EXPECT_EQ(a.error, b.error);
  }
}

Value runVM(ScriptManager &m, const std::string &source,
            const std::string &proc,
            const std::vector<Value> &args = {}) {
  std::vector<CompilationError> errors;
  EXPECT_TRUE(m.loadScriptSource(source, "t.script", errors));
  Value v;
  std::string msg;
  EXPECT_TRUE(m.executeProcedure(proc, args, v, msg)) << msg;
  return v;
}

int32_t i32(const Value &v) { return std::get<int32_t>(v); }
int64_t i64(const Value &v) { return std::get<int64_t>(v); }

} // namespace

// --- Parity battery ----------------------------------------------------------

TEST(VMTest, ArithmeticAndControlFlowParity) {
  expectParity(R"(
int32 f(int32 n) {
  int32 acc = 0;
  for (int32 i = 0; i < n; i += 1) {
    if (i % 3 == 0) { continue; }
    acc += i * 2 - (i >> 1);
  }
  while (acc > 100) { acc -= 37; }
  return acc;
})",
               "f", {static_cast<int32_t>(20)});
}

TEST(VMTest, RecursionParity) {
  expectParity(R"(
int64 fib(int64 n) {
  if (n < 2) { return n; }
  return fib(n - 1) + fib(n - 2);
})",
               "fib", {static_cast<int64_t>(15)});
}

TEST(VMTest, StringAndArrayParity) {
  expectParity(R"(
string f() {
  int32[] a = [5, 3, 8, 1];
  push(a, 13);
  string out = "";
  for (int32 i = 0; i < len(a); i += 1) {
    out += toString(a[i]);
  }
  return out + join(split("x-y-z", "-"), "+");
})",
               "f");
}

TEST(VMTest, MapAndStructParity) {
  expectParity(R"(
struct P { int32 x; int32 y; }
int32 f() {
  P p = P(3, 4);
  map<string, int32> m = {"a": 1, "b": p.x * p.y};
  m["c"] = m["a"] + m["b"];
  return m["c"];
})",
               "f");
}

TEST(VMTest, LambdaCaptureParity) {
  expectParity(R"(
int32 f() {
  int32 base = 100;
  auto g = fn(int32 x) -> int32 { return x + base; };
  return g(1) + g(2);
})",
               "f");
}

TEST(VMTest, SwitchAndTernaryParity) {
  expectParity(R"(
string f(int32 n) {
  string r = n > 0 ? "pos" : "nonpos";
  switch (n) {
    case 0: r += " zero"; break;
    case 1:
    case 2: r += " small"; break;
    default: r += " other";
  }
  return r;
})",
               "f", {static_cast<int32_t>(2)});
}

TEST(VMTest, DefaultArgsParity) {
  expectParity(R"(
int32 g(int32 a, int32 b = 10, int32 c = a * 2) { return a + b + c; }
int32 f() { return g(1) + g(1, 2) + g(1, 2, 3); })",
               "f");
}

TEST(VMTest, SliceAndIndexAssignParity) {
  expectParity(R"(
string f() {
  string t = "hello world";
  int32[] a = [0, 1, 2, 3, 4];
  a[1] += 10;
  int32[] b = a[1:3];
  return t[0:5] + "|" + toString(b[0]) + toString(b[1]);
})",
               "f");
}

TEST(VMTest, InterpolationParity) {
  // "${expr}" interpolation runs identically on both engines.
  expectParity(R"(
string f(string name, int32 n) {
  return "hi ${name}, ${n}*2=${n * 2}, nested ${"${n + 1}"}";
})",
               "f", {std::string("bob"), static_cast<int32_t>(21)});
}

// --- Dynamic scoping ---------------------------------------------------------

TEST(VMTest, DynamicScopingMatchesTreeWalker) {
  // A callee can read its caller's locals through the env chain — the VM
  // reproduces this via real Environment parentage.
  expectParity(R"(
int32 inner() { return depth_marker + 1; }
int32 outer() {
  int32 depth_marker = 41;
  return inner();
})",
               "outer");
}

TEST(VMTest, DynamicScopeSeesNearestCaller) {
  expectParity(R"(
int32 leaf() { return who; }
int32 mid() { int32 who = 2; return leaf(); }
int32 top() { int32 who = 1; return mid() + leaf(); })",
               "top");
}

// --- Exceptions / finally ----------------------------------------------------

TEST(VMTest, FinallyRunsOnReturnBreakContinueThrow) {
  expectParity(R"(
string f() {
  string log = "";
  try { return "x"; } finally { log += "r"; }
  for (int32 i = 0; i < 2; i += 1) {
    try { if (i == 0) { continue; } break; } finally { log += "l"; }
  }
  try { throw 7; } finally { log += "t"; }
  return log;
})",
               "f");
}

TEST(VMTest, NestedFinallyAndRethrowParity) {
  expectParity(R"(
string f() {
  string log = "";
  try {
    try { throw "inner"; }
    catch (string e) { throw e + "+re"; }
    finally { log += "f1"; }
  } catch (string e) {
    log += "|caught:" + e;
  }
  return log;
})",
               "f");
}

TEST(VMTest, UncaughtScriptExceptionMessageParity) {
  expectParity(R"(
int32 deep() { throw "boom"; }
int32 f() { return deep(); })",
               "f");
}

TEST(VMTest, RuntimeErrorCaughtAsStringParity) {
  expectParity(R"(
string f() {
  try { int32[] a = []; return toString(a[5]); }
  catch (string e) { return "caught"; }
  return "miss";
})",
               "f");
}

TEST(VMTest, CatchTypeConversionParity) {
  expectParity(R"(
string f() {
  try { throw 7; }
  catch (int64 e) { return "i64:" + toString(e); }
  return "miss";
})",
               "f");
}

// --- Limits ------------------------------------------------------------------

TEST(VMTest, CallDepthLimitFatalNotCatchable) {
  ScriptManager m;
  m.setVMEnabled(true);
  m.setExecutionLimits(5, 0);
  Outcome o = run(m, R"(
int32 r(int32 x) {
  try { return r(x + 1); } catch (e) { return -1; }
})",
                  "r", {static_cast<int32_t>(0)});
  EXPECT_FALSE(o.ok);
  EXPECT_NE(o.error.find("Maximum call depth exceeded"), std::string::npos)
      << o.error;
}

TEST(VMTest, StepLimitFatalNotCatchable) {
  ScriptManager m;
  m.setVMEnabled(true);
  m.setExecutionLimits(0, 50);
  Outcome o = run(m, R"(
int32 f() {
  int32 i = 0;
  try { while (true) { i += 1; } } catch (e) { return -1; }
  return i;
})",
                  "f");
  EXPECT_FALSE(o.ok);
  EXPECT_NE(o.error.find("Maximum execution steps exceeded"),
            std::string::npos)
      << o.error;
}

TEST(VMTest, StackBudgetStopsRecursion) {
  // VM frames are heap objects, so the guard bills each a synthetic cost;
  // the observable behavior (fatal error) matches the tree-walker.
  ScriptManager m;
  m.setVMEnabled(true);
  m.setExecutionLimits(0, 0, 256 * 1024);
  Outcome o = run(m, "int32 r(int32 x) { return r(x + 1); }", "r",
                  {static_cast<int32_t>(0)});
  EXPECT_FALSE(o.ok);
  EXPECT_NE(o.error.find("Native stack limit exceeded"), std::string::npos)
      << o.error;
}

TEST(VMTest, FinallyRunsOnFatalLimit) {
  ScriptManager m;
  m.setVMEnabled(true);
  m.setMemoryLimits(3, 0, 0);
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int32 f() {\n"
      "  try { int32[] a = [1,2,3,4]; return len(a); }\n"
      "  finally { println(\"cleanup\"); }\n"
      "}",
      "t.script", errors));
  std::string captured;
  m.setOutputCallback([&captured](const std::string &s) { captured += s; });
  Value v;
  std::string msg;
  EXPECT_FALSE(m.executeProcedure("f", {}, v, msg));
  EXPECT_NE(msg.find("Array size limit exceeded"), std::string::npos);
  EXPECT_EQ(captured, "cleanup\n");
}

TEST(VMTest, FinallyGetsBoundedStepReserve) {
  ScriptManager m;
  m.setVMEnabled(true);
  m.setExecutionLimits(0, 10);
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int32 f() {\n"
      "  try { int32 i = 0; while (true) { i += 1; } return i; }\n"
      "  finally { println(\"cleanup\"); }\n"
      "}",
      "t.script", errors));
  std::string captured;
  m.setOutputCallback([&captured](const std::string &s) { captured += s; });
  Value v;
  std::string msg;
  EXPECT_FALSE(m.executeProcedure("f", {}, v, msg));
  EXPECT_NE(msg.find("Maximum execution steps exceeded"), std::string::npos);
  EXPECT_EQ(captured, "cleanup\n");
}

// --- Snippets ----------------------------------------------------------------

TEST(VMTest, SnippetGlobalsPersistAcrossCalls) {
  ScriptManager m;
  m.setVMEnabled(true);
  Value v;
  std::string msg;
  ASSERT_TRUE(m.evaluateSnippet("int32 g = 10;", "<repl>", v, msg)) << msg;
  ASSERT_TRUE(m.evaluateSnippet("return g + 5;", "<repl>", v, msg)) << msg;
  EXPECT_EQ(i32(v), 15);
}

TEST(VMTest, SnippetReturnYieldsValue) {
  ScriptManager m;
  m.setVMEnabled(true);
  Value v;
  std::string msg;
  ASSERT_TRUE(m.evaluateSnippet("int32 x = 6; return x * 7;", "<repl>", v,
                                msg))
      << msg;
  EXPECT_EQ(i32(v), 42);
}

TEST(VMTest, SnippetCallsVMProcedures) {
  ScriptManager m;
  m.setVMEnabled(true);
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource("int32 sq(int32 n) { return n * n; }",
                                 "t.script", errors));
  Value v;
  std::string msg;
  ASSERT_TRUE(m.evaluateSnippet("return sq(9);", "<repl>", v, msg)) << msg;
  EXPECT_EQ(i32(v), 81);
}

// --- Interop with host features ----------------------------------------------

TEST(VMTest, ExternalFunctionsAndVariables) {
  ScriptManager m;
  m.setVMEnabled(true);
  int backing = 7;
  m.registerExternalFunctionBinary(
      "add", std::function<int64_t(int64_t, int64_t)>(
                 [](int64_t a, int64_t b) { return a + b; }));
  m.registerExternalVariable(
      "host_val", [&backing]() { return static_cast<int64_t>(backing); },
      [&backing](const Value &v) {
        backing = static_cast<int>(std::get<int64_t>(v));
      });
  Value v = runVM(m, R"(
int64 f() {
  host_val = host_val + 1;
  return add(host_val, 100);
})",
                  "f");
  EXPECT_EQ(i64(v), 108);
  EXPECT_EQ(backing, 8);
}

TEST(VMTest, HotReloadRecompilesVMFunctions) {
  std::string path =
      (std::filesystem::temp_directory_path() / "vm_hr.script").string();
  {
    std::ofstream out(path);
    out << "int32 f() { return 1; }";
  }
  ScriptManager m;
  m.setVMEnabled(true);
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptFile(path, errors));
  Value v;
  std::string msg;
  ASSERT_TRUE(m.executeProcedure("f", {}, v, msg)) << msg;
  EXPECT_EQ(i32(v), 1);
  // Reload with a new body; the new version must compile & run, not reuse
  // the stale bytecode.
  {
    std::ofstream out(path);
    out << "int32 f() { return 42; }";
  }
  ASSERT_TRUE(m.reloadScriptFile(path, errors));
  ASSERT_TRUE(m.executeProcedure("f", {}, v, msg)) << msg;
  EXPECT_EQ(i32(v), 42);
}

TEST(VMTest, DebugHookReceivesStatements) {
  ScriptManager m;
  m.setVMEnabled(true);
  int hits = 0;
  m.setDebugHook([&hits](const Interpreter::DebugContext &) { ++hits; });
  runVM(m, "int32 f() { int32 a = 1; int32 b = 2; return a + b; }", "f");
  EXPECT_GE(hits, 3);
}

TEST(VMTest, StackTraceAcrossVMFrames) {
  ScriptManager m;
  m.setVMEnabled(true);
  Outcome o = run(m, R"(
int32 inner() { int32[] a = []; return a[9]; }
int32 mid() { return inner(); }
int32 f() { return mid(); })",
                  "f");
  EXPECT_FALSE(o.ok);
  EXPECT_NE(o.error.find("inner"), std::string::npos) << o.error;
  EXPECT_NE(o.error.find("mid"), std::string::npos) << o.error;
}
