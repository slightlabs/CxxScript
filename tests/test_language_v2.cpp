// Comprehensive coverage for the v2 language features:
//   nested containers, array ordering, float modulo, negative indexing,
//   slicing, try/catch/finally/throw, auto, enums, default parameters,
//   procedure overloads, lambdas & function values, struct methods,
//   sandbox controls (builtin disabling, import roots), AST cache.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "ScriptManager.h"
#include <gtest/gtest.h>

using namespace Script;

namespace {
Value run(const std::string &source, const std::string &proc,
          const std::vector<Value> &args = {}) {
  ScriptManager manager;
  std::vector<CompilationError> errors;
  EXPECT_TRUE(manager.loadScriptSource(source, "test.script", errors));
  for (const auto &e : errors) {
    if (!e.isWarning) {
      ADD_FAILURE() << e.toString();
    }
  }
  if (std::any_of(errors.begin(), errors.end(),
                  [](const CompilationError &e) { return !e.isWarning; })) {
    return static_cast<int32_t>(0);
  }
  Value result;
  std::string errorMsg;
  EXPECT_TRUE(manager.executeProcedure(proc, args, result, errorMsg))
      << errorMsg;
  return result;
}

// Runs the proc expecting a runtime failure; returns the message.
std::string runFails(const std::string &source, const std::string &proc,
                     const std::vector<Value> &args = {}) {
  ScriptManager manager;
  std::vector<CompilationError> errors;
  EXPECT_TRUE(manager.loadScriptSource(source, "test.script", errors));
  Value result;
  std::string errorMsg;
  EXPECT_FALSE(manager.executeProcedure(proc, args, result, errorMsg));
  return errorMsg;
}

bool compiles(const std::string &source,
              std::vector<CompilationError> *errorsOut = nullptr) {
  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool ok = manager.loadScriptSource(source, "t.script", errors);
  if (errorsOut) {
    *errorsOut = errors;
  }
  return ok;
}

std::string writeTemp(const std::string &name, const std::string &source) {
  std::string path = (std::filesystem::temp_directory_path() / name).string();
  std::ofstream out(path);
  out << source;
  return path;
}

int32_t i32(const Value &v) { return std::get<int32_t>(v); }
int64_t i64(const Value &v) { return std::get<int64_t>(v); }
bool b(const Value &v) { return std::get<bool>(v); }
std::string s(const Value &v) { return std::get<std::string>(v); }
} // namespace

// ===========================================================================
// Nested containers
// ===========================================================================

TEST(NestedContainersTest, ArrayOfArraysLiteralAndIndex) {
  Value v = run(R"(
int32 f() {
  int32[][] g = [[1, 2], [3, 4]];
  return g[1][0] + g[0][1];
})",
                "f");
  EXPECT_EQ(i32(v), 5);
}

TEST(NestedContainersTest, ArrayOfArraysDeclAndAssign) {
  Value v = run(R"(
int32 f() {
  int32[][] g;
  push(g, [1, 2]);
  push(g, [3]);
  g[0][1] = 9;
  return g[0][1] + len(g[1]);
})",
                "f");
  EXPECT_EQ(i32(v), 10);
}

TEST(NestedContainersTest, ArrayOfStructs) {
  Value v = run(R"(
struct P { int32 x; int32 y; }
int32 f() {
  P[] pts = [P(1, 2), P(3, 4)];
  pts[0].x = 10;
  return pts[0].x + pts[1].y;
})",
                "f");
  EXPECT_EQ(i32(v), 14);
}

TEST(NestedContainersTest, MapOfArrays) {
  Value v = run(R"(
int32 f() {
  map<string, int32[]> m = {"a": [1, 2], "b": [3]};
  m["b"][0] += 10;
  return m["a"][1] + m["b"][0];
})",
                "f");
  EXPECT_EQ(i32(v), 15);
}

TEST(NestedContainersTest, MapOfStructs) {
  Value v = run(R"(
struct P { int32 x; }
int32 f() {
  map<string, P> m = {"p": P(7)};
  m["q"] = P(2);
  return m["p"].x * 10 + m["q"].x;
})",
                "f");
  EXPECT_EQ(i32(v), 72);
}

TEST(NestedContainersTest, ArrayOfMaps) {
  Value v = run(R"(
int32 f() {
  map<string, int32>[] ms = [{"a": 1}, {"a": 2}];
  ms[1]["a"] += 40;
  return ms[0]["a"] + ms[1]["a"];
})",
                "f");
  EXPECT_EQ(i32(v), 43);
}

TEST(NestedContainersTest, TripleNestedArray) {
  Value v = run(R"(
int32 f() {
  int32[][][] c = [[[1]], [[2], [3, 4]]];
  return c[1][1][1] + c[0][0][0];
})",
                "f");
  EXPECT_EQ(i32(v), 5);
}

TEST(NestedContainersTest, NestedForeach) {
  Value v = run(R"(
int32 f() {
  int32[][] g = [[1, 2], [3, 4]];
  int32 sum = 0;
  for (int32[] row : g) {
    for (int32 x : row) { sum += x; }
  }
  return sum;
})",
                "f");
  EXPECT_EQ(i32(v), 10);
}

TEST(NestedContainersTest, NestedMapValueType) {
  Value v = run(R"(
int32 f() {
  map<string, map<string, int32>> m = {"a": {"x": 5}};
  m["a"]["x"] += 1;
  return m["a"]["x"];
})",
                "f");
  EXPECT_EQ(i32(v), 6);
}

TEST(NestedContainersTest, StructContainingArrayOfSelf) {
  Value v = run(R"(
struct Node { int32 v; Node[] kids; }
int32 f() {
  Node t = Node(1, [Node(2, []), Node(3, [])]);
  return t.kids[0].v + t.kids[1].v + t.v;
})",
                "f");
  EXPECT_EQ(i32(v), 6);
}

TEST(NestedContainersTest, EmptyNestedLiteralConverts) {
  Value v = run(R"(
int32 f() {
  int32[][] g = [];
  push(g, []);
  push(g, [5]);
  return len(g) + len(g[1]);
})",
                "f");
  EXPECT_EQ(i32(v), 3);
}

TEST(NestedContainersTest, NestedToString) {
  Value v = run(R"(
string f() {
  int32[][] g = [[1, 2], [3]];
  return toString(g);
})",
                "f");
  EXPECT_EQ(s(v), "[[1, 2], [3]]");
}

TEST(NestedContainersTest, NestedTypeof) {
  Value v = run(R"(
string f() {
  int32[][] g = [[1]];
  return typeof(g);
})",
                "f");
  EXPECT_EQ(s(v), "int32[][]");
}

TEST(NestedContainersTest, NestedLiteralTypeMismatchRejected) {
  std::vector<CompilationError> errors;
  EXPECT_FALSE(
      compiles("int32 f() { int32[][] g = [[1], [\"x\"]]; return 0; }",
               &errors));
}

// ===========================================================================
// Array equality and ordering
// ===========================================================================

TEST(ArrayOrderingTest, EqualityAndInequality) {
  Value v = run(R"(
int32 f() {
  int32[] a = [1, 2, 3];
  int32[] b = [1, 2, 3];
  int32[] c = [1, 2, 4];
  return (a == b ? 1 : 0) + (a != c ? 2 : 0) + (a != b ? 4 : 0);
})",
                "f");
  EXPECT_EQ(i32(v), 3);
}

TEST(ArrayOrderingTest, Lexicographic) {
  Value v = run(R"(
int32 f() {
  int32 r = 0;
  if ([1, 2] < [1, 3]) { r += 1; }
  if ([1] < [1, 0]) { r += 2; }      // prefix is smaller
  if ([3] > [2, 9, 9]) { r += 4; }
  if ([1, 2] <= [1, 2]) { r += 8; }
  if ([1, 2] >= [1, 3]) { r += 16; }
  return r;
})",
                "f");
  EXPECT_EQ(i32(v), 15);
}

TEST(ArrayOrderingTest, NestedArrayOrdering) {
  Value v = run(R"(
bool f() { return [[1, 2]] < [[1, 3]]; })",
                "f");
  EXPECT_TRUE(b(v));
}

TEST(ArrayOrderingTest, MapEquality) {
  Value v = run(R"(
bool f() {
  map<string, int32> a = {"x": 1, "y": 2};
  map<string, int32> b = {"y": 2, "x": 1};
  return a == b;
})",
                "f");
  EXPECT_TRUE(b(v));
}

TEST(ArrayOrderingTest, MapOrderingRejected) {
  std::string err = runFails(R"(
bool f() { map<string,int32> a = {}; map<string,int32> b = {}; return a < b; })",
                             "f");
  EXPECT_FALSE(err.empty());
}

TEST(ArrayOrderingTest, StringArrayOrdering) {
  Value v = run(R"(
bool f() { return ["a", "b"] < ["a", "c"]; })",
                "f");
  EXPECT_TRUE(b(v));
}

// ===========================================================================
// Float modulo
// ===========================================================================

TEST(FloatModuloTest, DoubleModulo) {
  Value v = run("double f() { return 7.5 % 2.0; }", "f");
  EXPECT_DOUBLE_EQ(std::get<double>(v), 1.5);
}

TEST(FloatModuloTest, FloatModulo) {
  Value v = run(
      "float f(float a, float b) { return a % b; }", "f",
      {static_cast<float>(5.5f), static_cast<float>(2.0f)});
  EXPECT_FLOAT_EQ(std::get<float>(v), 1.5f);
}

TEST(FloatModuloTest, MixedIntFloat) {
  Value v = run("double f() { return 7 % 2.5; }", "f");
  EXPECT_DOUBLE_EQ(std::get<double>(v), 2.0);
}

TEST(FloatModuloTest, IntModuloStillWorks) {
  Value v = run("int32 f() { return 7 % 3; }", "f");
  EXPECT_EQ(i32(v), 1);
}

// ===========================================================================
// Negative indexing and slicing
// ===========================================================================

TEST(IndexingTest, NegativeArrayIndex) {
  Value v = run("int32 f() { int32[] a = [10, 20, 30]; return a[-1]; }", "f");
  EXPECT_EQ(i32(v), 30);
}

TEST(IndexingTest, NegativeArrayIndexDeep) {
  Value v = run("int32 f() { int32[] a = [10, 20, 30]; return a[-3]; }", "f");
  EXPECT_EQ(i32(v), 10);
}

TEST(IndexingTest, NegativeStringIndex) {
  Value v = run("char f() { return \"abc\"[-1]; }", "f");
  EXPECT_EQ(std::get<char>(v), 'c');
}

TEST(IndexingTest, NegativeIndexOutOfBounds) {
  std::string err = runFails(
      "int32 f() { int32[] a = [1]; return a[-2]; }", "f");
  EXPECT_NE(err.find("out of bounds"), std::string::npos);
}

TEST(IndexingTest, NegativeIndexAssign) {
  Value v = run(R"(
int32 f() { int32[] a = [1, 2, 3]; a[-1] = 9; return a[2]; })",
                "f");
  EXPECT_EQ(i32(v), 9);
}

TEST(IndexingTest, NegativeIndexUpdate) {
  Value v = run(R"(
int32 f() { int32[] a = [1, 2, 3]; a[-1] += 10; return a[0] + a[-1]; })",
                "f");
  EXPECT_EQ(i32(v), 14);
}

TEST(SlicingTest, BasicSlice) {
  Value v = run(R"(
int32 f() { int32[] a = [1, 2, 3, 4, 5]; int32[] b = a[1:3]; return b[0] * 10 + b[1]; })",
                "f");
  EXPECT_EQ(i32(v), 23);
}

TEST(SlicingTest, OpenEndedSlices) {
  Value v = run(R"(
int32 f() {
  int32[] a = [1, 2, 3, 4];
  return len(a[:2]) * 10 + len(a[2:]);
})",
                "f");
  EXPECT_EQ(i32(v), 22);
}

TEST(SlicingTest, FullSlice) {
  Value v = run(R"(
int32 f() { int32[] a = [1, 2, 3]; return len(a[:]); })",
                "f");
  EXPECT_EQ(i32(v), 3);
}

TEST(SlicingTest, NegativeBounds) {
  Value v = run(R"(
int32 f() { int32[] a = [1, 2, 3, 4, 5]; int32[] b = a[-2:]; return b[0] * 10 + b[1]; })",
                "f");
  EXPECT_EQ(i32(v), 45);
}

TEST(SlicingTest, NegativeEndBound) {
  Value v = run(R"(
int32 f() { int32[] a = [1, 2, 3, 4]; int32[] b = a[:-1]; return len(b) * 10 + b[2]; })",
                "f");
  EXPECT_EQ(i32(v), 33);
}

TEST(SlicingTest, ClampedBounds) {
  Value v = run(R"(
int32 f() { int32[] a = [1, 2, 3]; return len(a[-100:100]); })",
                "f");
  EXPECT_EQ(i32(v), 3);
}

TEST(SlicingTest, ReversedRangeIsEmpty) {
  Value v = run(R"(
int32 f() { int32[] a = [1, 2, 3]; return len(a[2:1]); })",
                "f");
  EXPECT_EQ(i32(v), 0);
}

TEST(SlicingTest, StringSlice) {
  Value v = run(R"(
string f() { return "hello world"[6:] + "!" + "hello"[0:1]; })",
                "f");
  EXPECT_EQ(s(v), "world!h");
}

TEST(SlicingTest, SliceReturnsCopy) {
  Value v = run(R"(
int32 f() {
  int32[] a = [1, 2, 3];
  int32[] b = a[0:2];
  b[0] = 99;
  return a[0];
})",
                "f");
  EXPECT_EQ(i32(v), 1);
}

TEST(SlicingTest, SliceOnNonSliceable) {
  // Slicing a scalar is rejected at compile time.
  std::vector<CompilationError> errors;
  EXPECT_FALSE(compiles("int32 f() { int32 x = 5; return len(x[0:1]); }",
                        &errors));
}

TEST(SlicingTest, MapSliceRejected) {
  std::vector<CompilationError> errors;
  EXPECT_FALSE(
      compiles("int32 f() { map<string,int32> m = {}; return len(m[0:1]); }",
               &errors));
}

// ===========================================================================
// try / catch / finally / throw
// ===========================================================================

TEST(ExceptionsTest, CatchString) {
  Value v = run(R"(
string f() {
  try { throw "oops"; } catch (string e) { return "caught " + e; }
})",
                "f");
  EXPECT_EQ(s(v), "caught oops");
}

TEST(ExceptionsTest, CatchInt64) {
  Value v = run(R"(
int64 f() {
  try { throw 42; } catch (int64 e) { return e + 1; }
  return 0;
})",
                "f");
  EXPECT_EQ(i64(v), 43);
}

TEST(ExceptionsTest, UntypedCatch) {
  Value v = run(R"(
string f() {
  try { throw "x"; } catch (e) { return toString(e) + "!"; }
  return "";
})",
                "f");
  EXPECT_EQ(s(v), "x!");
}

TEST(ExceptionsTest, FinallyWithoutCatch) {
  Value v = run(R"(
int32 f() {
  int32 log = 0;
  try { log += 1; } finally { log += 10; }
  return log;
})",
                "f");
  EXPECT_EQ(i32(v), 11);
}

TEST(ExceptionsTest, FinallyRunsOnException) {
  Value v = run(R"(
int32 f() {
  int32 log = 0;
  try {
    try { throw 1; } finally { log += 100; }
  } catch (int64 e) { log += e; }
  return log;
})",
                "f");
  EXPECT_EQ(i32(v), 101);
}

TEST(ExceptionsTest, RethrowFromCatch) {
  Value v = run(R"(
string f() {
  try {
    try { throw "inner"; }
    catch (string e) { throw e + "2"; }
  } catch (string e) { return e; }
  return "";
})",
                "f");
  EXPECT_EQ(s(v), "inner2");
}

TEST(ExceptionsTest, FinallyRunsOnReturn) {
  Value v = run(R"(
int32 f() {
  int32 log = 0;
  try { return log; } finally { log += 5; }
})",
                "f");
  // finally mutates `log` after the return value is captured — returns 0.
  EXPECT_EQ(i32(v), 0);
}

TEST(ExceptionsTest, FinallyRunsOnBreak) {
  Value v = run(R"(
int32 f() {
  int32 log = 0;
  for (int32 i = 0; i < 10; i += 1) {
    try { break; } finally { log += 1; }
  }
  return log;
})",
                "f");
  EXPECT_EQ(i32(v), 1);
}

TEST(ExceptionsTest, RuntimeErrorCaughtAsString) {
  Value v = run(R"(
string f() {
  try { int32[] a = []; return toString(a[0]); }
  catch (string e) { return "caught"; }
  return "";
})",
                "f");
  EXPECT_EQ(s(v), "caught");
}

TEST(ExceptionsTest, ThrowFromProcedurePropagates) {
  Value v = run(R"(
void inner() { throw "deep"; }
string f() {
  try { inner(); } catch (string e) { return "caught " + e; }
  return "";
})",
                "f");
  EXPECT_EQ(s(v), "caught deep");
}

TEST(ExceptionsTest, UncaughtThrowSurfaces) {
  std::string err = runFails("void f() { throw \"boom\"; }", "f");
  EXPECT_NE(err.find("Uncaught"), std::string::npos);
  EXPECT_NE(err.find("boom"), std::string::npos);
}

TEST(ExceptionsTest, CatchTypeConversion) {
  // Thrown int32 converts to int64 catch binding.
  Value v = run(R"(
int64 f() {
  try { throw 7; } catch (int64 e) { return e * 2; }
  return 0;
})",
                "f");
  EXPECT_EQ(i64(v), 14);
}

TEST(ExceptionsTest, ThrowVoidRejectedAtCompile) {
  std::vector<CompilationError> errors;
  EXPECT_FALSE(compiles("void g() {}\nvoid f() { throw g(); }", &errors));
}

TEST(ExceptionsTest, TryRequiresCatchOrFinally) {
  std::vector<CompilationError> errors;
  EXPECT_FALSE(compiles("void f() { try { } }", &errors));
}

TEST(ExceptionsTest, CatchVarScoped) {
  // The catch binding doesn't leak outside the catch block.
  std::vector<CompilationError> errors;
  bool ok = compiles(
      "string f() { try { throw \"x\"; } catch (string e) { } return e; }",
      &errors);
  // `e` is undefined after the catch — validator may treat it as unknown,
  // which is either a compile error or a runtime failure; we only require
  // that it does not silently succeed.
  if (ok) {
    std::string err = runFails(
        "string f() { try { throw \"x\"; } catch (string e) { } return e; }",
        "f");
    EXPECT_FALSE(err.empty());
  }
}

TEST(ExceptionsTest, CatchWithStructValue) {
  Value v = run(R"(
struct Err { int32 code; }
int32 f() {
  try { throw Err(9); } catch (Err e) { return e.code; }
  return 0;
})",
                "f");
  EXPECT_EQ(i32(v), 9);
}

TEST(ExceptionsTest, CatchVarUnusedOk) {
  Value v = run(R"(
int32 f() {
  try { throw 1; } catch { }
  return 5;
})",
                "f");
  EXPECT_EQ(i32(v), 5);
}

TEST(ExceptionsTest, LimitsNotCatchable) {
  // Resource-limit violations are fatal — a script cannot swallow them.
  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(
      "int32 f() { try { int32[] a = []; while (true) { push(a, 1); } } "
      "catch (string e) { return -1; } return 0; }",
      "t.script", errors));
  manager.setMemoryLimits(4, 0, 0);
  Value result;
  std::string errorMsg;
  EXPECT_FALSE(manager.executeProcedure("f", {}, result, errorMsg));
  EXPECT_NE(errorMsg.find("Array size limit"), std::string::npos);
}

// ===========================================================================
// auto
// ===========================================================================

TEST(AutoTest, ScalarInference) {
  Value v = run("int32 f() { auto x = 42; auto s = \"hi\"; return x + len(s); }",
                "f");
  EXPECT_EQ(i32(v), 44);
}

TEST(AutoTest, DoubleInference) {
  Value v = run("double f() { auto x = 1.5; return x * 2; }", "f");
  EXPECT_DOUBLE_EQ(std::get<double>(v), 3.0);
}

TEST(AutoTest, ArrayInference) {
  Value v = run(R"(
int32 f() { auto a = [1, 2, 3]; return a[1] + len(a); })",
                "f");
  EXPECT_EQ(i32(v), 5);
}

TEST(AutoTest, MapInference) {
  Value v = run(R"(
int32 f() { auto m = {"a": 7}; return m["a"]; })",
                "f");
  EXPECT_EQ(i32(v), 7);
}

TEST(AutoTest, StructInference) {
  Value v = run(R"(
struct P { int32 x; }
int32 f() { auto p = P(8); return p.x; })",
                "f");
  EXPECT_EQ(i32(v), 8);
}

TEST(AutoTest, FunctionInference) {
  Value v = run(R"(
int32 f() { auto sq = fn(int32 x) -> int32 { return x * x; }; return sq(6); })",
                "f");
  EXPECT_EQ(i32(v), 36);
}

TEST(AutoTest, ForeachAuto) {
  Value v = run(R"(
int32 f() {
  int32 sum = 0;
  for (auto x : [4, 5, 6]) { sum += x; }
  return sum;
})",
                "f");
  EXPECT_EQ(i32(v), 15);
}

TEST(AutoTest, RequiresInitializer) {
  std::vector<CompilationError> errors;
  EXPECT_FALSE(compiles("void f() { auto x; }", &errors));
}

TEST(AutoTest, ArraySuffixRejected) {
  std::vector<CompilationError> errors;
  EXPECT_FALSE(compiles("void f() { auto[] x = [1]; }", &errors));
}

TEST(AutoTest, ConstAuto) {
  Value v = run("int32 f() { const auto x = 9; return x; }", "f");
  EXPECT_EQ(i32(v), 9);
}

TEST(AutoTest, InferredTypeIsStatic) {
  // `auto` binds the inferred type statically — assigning a string to an
  // int32-inferred variable is a compile error.
  std::vector<CompilationError> errors;
  EXPECT_FALSE(
      compiles("void f() { auto x = 1; x = \"s\"; }", &errors));
}

// ===========================================================================
// Enums
// ===========================================================================

TEST(EnumTest, AutoIncrement) {
  Value v = run(R"(
enum Color { RED, GREEN, BLUE }
int32 f() { return Color.RED + Color.GREEN * 10 + Color.BLUE * 100; })",
                "f");
  EXPECT_EQ(i32(v), 210);
}

TEST(EnumTest, ExplicitValues) {
  Value v = run(R"(
enum Code { OK = 200, NOT_FOUND = 404, ERR = -1 }
int32 f() { return Code.OK + Code.ERR; })",
                "f");
  EXPECT_EQ(i32(v), 199);
}

TEST(EnumTest, ResumeAfterExplicit) {
  Value v = run(R"(
enum E { A = 10, B, C = 1, D }
int32 f() { return E.B * 10 + E.D; })",
                "f");
  EXPECT_EQ(i32(v), 112); // B=11, D=2
}

TEST(EnumTest, TrailingComma) {
  Value v = run(R"(
enum E { A, B, }
int32 f() { return E.B; })",
                "f");
  EXPECT_EQ(i32(v), 1);
}

TEST(EnumTest, InSwitch) {
  Value v = run(R"(
enum Dir { N, E, S, W }
int32 f() {
  int64 d = Dir.E;
  switch (d) {
    case Dir.N: return 1;
    case Dir.E: return 2;
    default: return 3;
  }
})",
                "f");
  EXPECT_EQ(i32(v), 2);
}

TEST(EnumTest, DuplicateMemberRejected) {
  std::vector<CompilationError> errors;
  EXPECT_FALSE(compiles("enum E { A, A }\nvoid f() {}", &errors));
}

TEST(EnumTest, UnknownMemberRejected) {
  std::vector<CompilationError> errors;
  EXPECT_FALSE(compiles("enum E { A }\nint64 f() { return E.Z; }", &errors));
}

TEST(EnumTest, DuplicateEnumNameRejected) {
  std::vector<CompilationError> errors;
  EXPECT_FALSE(compiles("enum E { A }\nenum E { B }\nvoid f() {}", &errors));
}

TEST(EnumTest, EnumStructNameConflict) {
  std::vector<CompilationError> errors;
  EXPECT_FALSE(
      compiles("enum E { A }\nstruct E { int32 x; }\nvoid f() {}", &errors));
}

TEST(EnumTest, ComparisonAndArithmetic) {
  Value v = run(R"(
enum Lvl { LOW, MID, HIGH }
int32 f() { return (Lvl.LOW < Lvl.HIGH ? 1 : 0) + (Lvl.MID == Lvl.MID ? 2 : 0); })",
                "f");
  EXPECT_EQ(i32(v), 3);
}

// ===========================================================================
// Default parameters and overloading
// ===========================================================================

TEST(DefaultsTest, SingleDefault) {
  Value v = run(R"(
int32 f2(int32 a, int32 b = 10) { return a + b; }
int32 f() { return f2(5) + f2(5, 1); })",
                "f");
  EXPECT_EQ(i32(v), 21); // 15 + 6
}

TEST(DefaultsTest, MultipleDefaults) {
  Value v = run(R"(
int32 f2(int32 a = 1, int32 b = 2, int32 c = 3) { return a * 100 + b * 10 + c; }
int32 f() { return f2() + f2(5) + f2(5, 6); })",
                "f");
  EXPECT_EQ(i32(v), 123 + 523 + 563);
}

TEST(DefaultsTest, DefaultExpression) {
  Value v = run(R"(
int32 f2(int32 a, int32 b = a * 2) { return a + b; }
int32 f() { return f2(5); })",
                "f");
  EXPECT_EQ(i32(v), 15);
}

TEST(DefaultsTest, NonTrailingDefaultRejected) {
  std::vector<CompilationError> errors;
  EXPECT_FALSE(
      compiles("void f2(int32 a = 1, int32 b) {}\nvoid f() {}", &errors));
}

TEST(DefaultsTest, DefaultTypeChecked) {
  std::vector<CompilationError> errors;
  EXPECT_FALSE(compiles(
      "void f2(int32 a = \"nope\") {}\nvoid f() {}", &errors));
}

TEST(OverloadTest, ByArity) {
  Value v = run(R"(
int32 g(int32 a) { return a; }
int32 g(int32 a, int32 b) { return a * 10 + b; }
int32 f() { return g(5) + g(1, 2); })",
                "f");
  EXPECT_EQ(i32(v), 17);
}

TEST(OverloadTest, ByType) {
  Value v = run(R"(
int32 g(int32 a) { return 1; }
int32 g(string a) { return 2; }
int32 f() { return g(5) * 10 + g("x"); })",
                "f");
  EXPECT_EQ(i32(v), 12);
}

TEST(OverloadTest, PrefersExactMatch) {
  Value v = run(R"(
int32 g(int64 a) { return 1; }
int32 g(string a) { return 2; }
int32 f() { return g("x"); })",
                "f");
  EXPECT_EQ(i32(v), 2); // string exact beats int64 conversion
}

TEST(OverloadTest, DifferentReturnTypes) {
  Value v = run(R"(
int32 g(int32 a) { return a; }
string g(string a) { return a + "!"; }
string f() { return g("hi") + toString(g(3)); })",
                "f");
  EXPECT_EQ(s(v), "hi!3");
}

TEST(OverloadTest, NoMatchCompileError) {
  std::vector<CompilationError> errors;
  EXPECT_FALSE(compiles(
      "int32 g(int32 a) { return a; }\nvoid f() { int32[] a = []; g(a); }",
      &errors));
}

TEST(OverloadTest, DuplicateSignatureRejected) {
  std::vector<CompilationError> errors;
  EXPECT_FALSE(compiles(
      "int32 g(int32 a) { return 1; }\nint32 g(int32 b) { return 2; }\nvoid f() {}",
      &errors));
}

TEST(OverloadTest, DefaultsAndOverloads) {
  Value v = run(R"(
int32 g(int32 a, int32 b = 100) { return a + b; }
int32 g(string s) { return 7; }
int32 f() { return g(1) + g("x"); })",
                "f");
  EXPECT_EQ(i32(v), 108); // g(1) uses default b=100
}

// ===========================================================================
// Lambdas and function values
// ===========================================================================

TEST(FunctionTest, LambdaBasic) {
  Value v = run(R"(
int32 f() {
  auto sq = fn(int32 x) -> int32 { return x * x; };
  return sq(7);
})",
                "f");
  EXPECT_EQ(i32(v), 49);
}

TEST(FunctionTest, LambdaAutoParams) {
  Value v = run(R"(
int32 f() {
  auto add = fn(a, b) { return a + b; };
  return add(3, 4);
})",
                "f");
  EXPECT_EQ(i32(v), 7);
}

TEST(FunctionTest, LambdaCaptureByCopy) {
  Value v = run(R"(
int32 f() {
  int32 n = 10;
  auto add = fn(int32 x) -> int32 { return x + n; };
  n = 100;                 // snapshot: lambda still sees 10
  return add(5) + n;       // 15 + 100
})",
                "f");
  EXPECT_EQ(i32(v), 115);
}

TEST(FunctionTest, FunctionTypedVariable) {
  Value v = run(R"(
int32 f() {
  fn(int32) -> int32 dbl = fn(int32 x) -> int32 { return x * 2; };
  return dbl(21);
})",
                "f");
  EXPECT_EQ(i32(v), 42);
}

TEST(FunctionTest, ProcedureReference) {
  Value v = run(R"(
int32 add(int32 a, int32 b) { return a + b; }
int32 f() {
  auto f2 = add;
  return f2(20, 22);
})",
                "f");
  EXPECT_EQ(i32(v), 42);
}

TEST(FunctionTest, OverloadedProcedureReference) {
  Value v = run(R"(
int32 g(int32 a) { return a * 10; }
int32 g(int32 a, int32 b) { return a + b; }
int32 f() {
  auto h = g;
  return h(3) + h(3, 4);   // 30 + 7
})",
                "f");
  EXPECT_EQ(i32(v), 37);
}

TEST(FunctionTest, LambdaAsArgument) {
  Value v = run(R"(
int32 applyTwice(fn(int32) -> int32 f2, int32 x) { return f2(f2(x)); }
int32 f() { return applyTwice(fn(x) { return x + 3; }, 1); })",
                "f");
  EXPECT_EQ(i32(v), 7);
}

TEST(FunctionTest, LambdaReturned) {
  Value v = run(R"(
fn(int32) -> int32 makeAdder(int32 n) {
  return fn(int32 x) -> int32 { return x + n; };
}
int32 f() { return makeAdder(5)(4) + makeAdder(1)(0); })",
                "f");
  EXPECT_EQ(i32(v), 10);
}

TEST(FunctionTest, LambdaInArray) {
  // `auto` infers the array-of-functions type; `fn(int32) -> int32[]` would
  // instead declare a function *returning* int32[].
  Value v = run(R"(
int32 f() {
  auto fns = [fn(int32 x) -> int32 { return x + 1; },
              fn(int32 x) -> int32 { return x * 10; }];
  return fns[0](4) + fns[1](4);   // 5 + 40
})",
                "f");
  EXPECT_EQ(i32(v), 45);
}

TEST(FunctionTest, NullFunctionCallFails) {
  std::string err = runFails(R"(
void f() {
  fn(int32) -> int32 g;
  g(1);
})",
                             "f");
  EXPECT_FALSE(err.empty());
}

TEST(FunctionTest, NonFunctionCallRejected) {
  std::vector<CompilationError> errors;
  EXPECT_FALSE(compiles("void f() { int32 x = 5; x(1); }", &errors));
}

TEST(FunctionTest, SignatureMismatchRejected) {
  std::vector<CompilationError> errors;
  EXPECT_FALSE(compiles(
      "void f() { fn(int32) -> int32 g = fn(int32 a, int32 b) { return a; }; }",
      &errors));
}

TEST(FunctionTest, FunctionEquality) {
  Value v = run(R"(
int32 add(int32 a) { return a; }
bool f() {
  auto a = add;
  auto b2 = add;
  auto c = fn(x) { return x; };
  return a == b2 && a != c;
})",
                "f");
  EXPECT_TRUE(b(v));
}

TEST(FunctionTest, FunctionOrderingRejected) {
  std::string err = runFails(R"(
int32 add(int32 a) { return a; }
bool f() { return add < add; })",
                             "f");
  EXPECT_FALSE(err.empty());
}

TEST(FunctionTest, LambdaShadowsProcedureName) {
  Value v = run(R"(
int32 len2(int32 a) { return a; }
int32 f() {
  auto add = fn(x) { return x + 1; };
  return add(4);
})",
                "f");
  EXPECT_EQ(i32(v), 5);
}

// ===========================================================================
// Struct methods
// ===========================================================================

TEST(MethodTest, BasicMethod) {
  Value v = run(R"(
struct P { int32 x; int32 y; int32 mag() { return x * x + y * y; } }
int32 f() { return P(3, 4).mag(); })",
                "f");
  EXPECT_EQ(i32(v), 25);
}

TEST(MethodTest, ImplicitThisRead) {
  Value v = run(R"(
struct C { int32 n; int32 get() { return n; } }
int32 f() { return C(9).get(); })",
                "f");
  EXPECT_EQ(i32(v), 9);
}

TEST(MethodTest, ImplicitThisWrite) {
  Value v = run(R"(
struct C { int32 n; void set(int32 v) { n = v; } int32 get() { return n; } }
int32 f() { C c = C(0); c.set(42); return c.get(); })",
                "f");
  EXPECT_EQ(i32(v), 42);
}

TEST(MethodTest, ExplicitThisAccess) {
  Value v = run(R"(
struct C { int32 n; int32 get() { return this.n + 1; } }
int32 f() { return C(4).get(); })",
                "f");
  EXPECT_EQ(i32(v), 5);
}

TEST(MethodTest, SiblingMethodCall) {
  Value v = run(R"(
struct C { int32 n; int32 dbl() { return n * 2; } int32 quad() { return dbl() * 2; } }
int32 f() { return C(5).quad(); })",
                "f");
  EXPECT_EQ(i32(v), 20);
}

TEST(MethodTest, BoundMethodValue) {
  Value v = run(R"(
struct C { int32 n; int32 get() { return n; } }
int32 f() {
  C c = C(33);
  auto m = c.get;
  return m();
})",
                "f");
  EXPECT_EQ(i32(v), 33);
}

TEST(MethodTest, MethodOnNestedStruct) {
  Value v = run(R"(
struct Inner { int32 v; int32 get() { return v; } }
struct Outer { Inner in; }
int32 f() { return Outer(Inner(11)).in.get(); })",
                "f");
  EXPECT_EQ(i32(v), 11);
}

TEST(MethodTest, MethodMutationVisibleToCaller) {
  Value v = run(R"(
struct C { int32 n; void bump() { n += 1; } }
int32 f() { C c = C(0); c.bump(); c.bump(); return c.n; })",
                "f");
  EXPECT_EQ(i32(v), 2);
}

TEST(MethodTest, FieldMethodNameConflict) {
  std::vector<CompilationError> errors;
  EXPECT_FALSE(compiles(
      "struct C { int32 x; int32 x() { return 1; } }\nvoid f() {}", &errors));
}

TEST(MethodTest, UnknownMemberError) {
  std::vector<CompilationError> errors;
  EXPECT_FALSE(compiles(
      "struct C { int32 x; }\nint32 f() { return C(1).nope(); }", &errors));
}

// ===========================================================================
// Sandbox controls
// ===========================================================================

TEST(SandboxTest, DisableBuiltin) {
  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(
      "int32 f() { return len(\"abcd\"); }", "t.script", errors));
  manager.disableBuiltin("len");
  Value result;
  std::string errorMsg;
  EXPECT_FALSE(manager.executeProcedure("f", {}, result, errorMsg));
  EXPECT_NE(errorMsg.find("len"), std::string::npos);
}

TEST(SandboxTest, EnableBuiltinRestores) {
  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(
      "int32 f() { return len(\"abcd\"); }", "t.script", errors));
  manager.disableBuiltin("len");
  manager.enableBuiltin("len");
  Value result;
  std::string errorMsg;
  ASSERT_TRUE(manager.executeProcedure("f", {}, result, errorMsg)) << errorMsg;
  EXPECT_EQ(i32(result), 4);
}

TEST(SandboxTest, ProcedureShadowsDisabledBuiltin) {
  // A user procedure still wins over a builtin slot even when the builtin
  // is disabled — the builtin is never reached.
  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(
      "int32 len(string s) { return 99; }\nint32 f() { return len(\"x\"); }",
      "t.script", errors));
  manager.disableBuiltin("len");
  Value result;
  std::string errorMsg;
  ASSERT_TRUE(manager.executeProcedure("f", {}, result, errorMsg)) << errorMsg;
  EXPECT_EQ(i32(result), 99);
}

TEST(SandboxTest, ImportsDisabled) {
  ScriptManager manager;
  manager.setImportsEnabled(false);
  std::vector<CompilationError> errors;
  EXPECT_FALSE(manager.loadScriptSource(
      "import \"x.script\";\nvoid f() {}", "t.script", errors));
  bool found = false;
  for (const auto &e : errors) {
    if (e.message.find("disabled") != std::string::npos) {
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

TEST(SandboxTest, ImportRootsWhitelist) {
  std::filesystem::path tmp = std::filesystem::temp_directory_path();
  std::filesystem::path allowed = tmp / "cx_allowed";
  std::filesystem::create_directories(allowed);
  writeTemp("cx_allowed/lib.cx", "int32 helper() { return 7; }");
  std::string importer = writeTemp(
      "importer.cx", "import \"cx_allowed/lib.cx\";\nint32 f() { return helper(); }");

  // Root = temp dir: import inside the root is allowed.
  ScriptManager okMgr;
  okMgr.addImportRoot(tmp.string());
  std::vector<CompilationError> errors;
  ASSERT_TRUE(okMgr.loadScriptFile(importer, errors));
  Value result;
  std::string errorMsg;
  ASSERT_TRUE(okMgr.executeProcedure("f", {}, result, errorMsg)) << errorMsg;
  EXPECT_EQ(i32(result), 7);

  // Root = /nonexistent: same import is rejected.
  ScriptManager noMgr;
  noMgr.addImportRoot("/nonexistent_root_xyz");
  errors.clear();
  EXPECT_FALSE(noMgr.loadScriptFile(importer, errors));
  bool found = false;
  for (const auto &e : errors) {
    if (e.message.find("outside the allowed roots") != std::string::npos) {
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

// ===========================================================================
// AST cache
// ===========================================================================

TEST(AstCacheTest, CacheHitProducesSameBehavior) {
  std::string path = writeTemp(
      "cached.cx", "int32 answer() { return 42; }");
  ScriptManager mgr;
  mgr.setAstCacheEnabled(true);
  std::vector<CompilationError> errors;
  ASSERT_TRUE(mgr.loadScriptFile(path, errors));
  // Second load hits the cache.
  ASSERT_TRUE(mgr.loadScriptFile(path, errors));
  EXPECT_GE(mgr.astCacheSize(), 1u);
  Value result;
  std::string errorMsg;
  ASSERT_TRUE(mgr.executeProcedure("answer", {}, result, errorMsg)) << errorMsg;
  EXPECT_EQ(i32(result), 42);
}

TEST(AstCacheTest, CacheInvalidatesOnChange) {
  std::string path = writeTemp("cached2.cx", "int32 answer() { return 1; }");
  ScriptManager mgr;
  mgr.setAstCacheEnabled(true);
  std::vector<CompilationError> errors;
  ASSERT_TRUE(mgr.loadScriptFile(path, errors));
  // Rewrite the file — hash mismatch forces a reparse.
  { std::ofstream out(path); out << "int32 answer() { return 77; }"; }
  mgr.reloadScriptFile(path, errors);
  Value result;
  std::string errorMsg;
  ASSERT_TRUE(mgr.executeProcedure("answer", {}, result, errorMsg)) << errorMsg;
  EXPECT_EQ(i32(result), 77);
}

TEST(AstCacheTest, ClearCache) {
  std::string path = writeTemp("cached3.cx", "int32 a() { return 1; }");
  ScriptManager mgr;
  mgr.setAstCacheEnabled(true);
  std::vector<CompilationError> errors;
  ASSERT_TRUE(mgr.loadScriptFile(path, errors));
  mgr.clearAstCache();
  EXPECT_EQ(mgr.astCacheSize(), 0u);
}

// ===========================================================================
// Runtime error paths for new features
// ===========================================================================

TEST(ErrorPathsTest, IndexOnFunctionValue) {
  std::string err = runFails(R"(
int32 add(int32 a) { return a; }
int32 f() { auto g = add; return g[0]; })",
                             "f");
  EXPECT_FALSE(err.empty());
}

TEST(ErrorPathsTest, StructMemberOnLambda) {
  // Member access on a function value is a compile-time error.
  std::vector<CompilationError> errors;
  EXPECT_FALSE(compiles(
      "int32 f() { auto g = fn(x) { return x; }; return len(g.foo); }",
      &errors));
}

TEST(ErrorPathsTest, SliceOnMapRejected) {
  std::vector<CompilationError> errors;
  EXPECT_FALSE(compiles(
      "int32 f() { map<string,int32> m = {}; m[\"a\"] = 1; "
      "return len(m[0:1]); }",
      &errors));
}

TEST(ErrorPathsTest, ThrowInLambdaCaughtByCaller) {
  Value v = run(R"(
string f() {
  auto t = fn() { throw "lambda-throw"; };
  try { t(); } catch (string e) { return "got " + e; }
  return "";
})",
                "f");
  EXPECT_EQ(s(v), "got lambda-throw");
}

TEST(ErrorPathsTest, NestedTryAcrossCalls) {
  Value v = run(R"(
void inner() { throw 5; }
void mid() { inner(); }
int64 f() {
  try { mid(); } catch (int64 e) { return e * 2; }
  return 0;
})",
                "f");
  EXPECT_EQ(i64(v), 10);
}

TEST(ErrorPathsTest, RethrowFromAutoCatch) {
  // `throw e` on an auto-bound catch variable rethrows the caught value.
  Value v = run(R"(
string f() {
  try {
    try { throw "inner"; }
    catch (e) { throw e; }   // auto-bound rethrow
    return "unreachable";
  } catch (string msg) { return msg; }
})",
                "f");
  EXPECT_EQ(s(v), "inner");

  // Rethrow preserves non-string types end to end.
  Value w = run(R"(
int64 f() {
  try {
    try { throw 7; }
    catch (e) { throw e; }
    return 0;
  } catch (int64 code) { return code; }
})",
                "f");
  EXPECT_EQ(i64(w), 7);
}
