// Edge-case coverage for core language semantics: recursion, integer
// arithmetic corner cases, truthiness, switch on scalar types, const
// enforcement, struct/lambda corners, cyclic-value guards, and grammar
// rejection paths.
#include <algorithm>
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

bool compiles(const std::string &source) {
  ScriptManager manager;
  std::vector<CompilationError> errors;
  return manager.loadScriptSource(source, "t.script", errors);
}

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
} // namespace

// --- Procedures -------------------------------------------------------------

TEST(LanguageEdgeTest, MutualRecursion) {
  Value v = run(R"(
bool isEven(int32 n) { return n == 0 ? true : isOdd(n - 1); }
bool isOdd(int32 n) { return n == 0 ? false : isEven(n - 1); }
bool f() { return isEven(10) && !isEven(7); })",
                "f");
  EXPECT_TRUE(std::get<bool>(v));
}

TEST(LanguageEdgeTest, DeepRecursion) {
  Value v = run(R"(
int32 depth(int32 n) { return n <= 0 ? 0 : 1 + depth(n - 1); }
int32 f() { return depth(500); })",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 500);
}

// --- Integer arithmetic edges -------------------------------------------------

TEST(LanguageEdgeTest, IntDivisionTruncatesTowardZero) {
  Value v = run(R"(
int32 f() { return 7 / 2 * 100 + (-7 / 2) * 10 + (7 / -2); })",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 300 - 30 - 3);
}

TEST(LanguageEdgeTest, IntModuloSignFollowsDividend) {
  Value v = run(R"(
int32 f() { return (-7 % 3) * 100 + (7 % -3) * 10 + (-7 % -3); })",
                "f");
  // C-style truncating remainder: -1, 1, -1
  EXPECT_EQ(std::get<int32_t>(v), -100 + 10 - 1);
}

TEST(LanguageEdgeTest, CompoundAssignWidensNarrowTypes) {
  // `+=` computes in the promoted type and stores it: an int8 variable
  // widens to int32 rather than wrapping at 127.
  Value v = run(R"(
int32 f() {
  int8 x = 127;
  x += 1;
  return x;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 128);
}

TEST(LanguageEdgeTest, IncDecWidensNarrowTypes) {
  Value v = run(R"(
int32 f() {
  int8 x = 127;
  x++;
  uint8 u = 0;
  u--;
  return x * 1000 + u;   // x == 128 (int32), u == -1 (int32)
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 128000 - 1);
}

TEST(LanguageEdgeTest, MixedWidthArithmeticPromotes) {
  // Promotion is to the wider operand type (not to int like C++).
  Value v = run(R"(
string f() {
  int8 a = 1;
  int64 b = 2;
  int16 c = 3;
  return typeof(a + b) + "|" + typeof(a + c) + "|" + typeof(a + a);
})",
                "f");
  EXPECT_EQ(std::get<std::string>(v), "int64|int16|int8");
}

TEST(LanguageEdgeTest, MixedWidthComparisons) {
  Value v = run(R"(
bool f() {
  int8 a = -1;
  uint8 b = 200;
  int64 big = 5000000000;
  return a < b && b < big && a < 0 && big > 4294967295;
})",
                "f");
  EXPECT_TRUE(std::get<bool>(v));
}

// --- Truthiness ---------------------------------------------------------------

TEST(LanguageEdgeTest, IfAcceptsNonBoolConditions) {
  Value v = run(R"(
int32 f() {
  int32 n = 0;
  if (5) { n += 1; }
  if ("") { n += 10; }
  if ("x") { n += 100; }
  if (0) { n += 1000; }
  return n;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 101);
}

TEST(LanguageEdgeTest, WhileOnNonBoolCondition) {
  Value v = run(R"(
int32 f() {
  int32 i = 3;
  int32 steps = 0;
  while (i) { i -= 1; steps += 1; }
  return steps;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 3);
}

TEST(LanguageEdgeTest, LogicalNotOnScalars) {
  Value v = run(R"(
bool f() { return !0 == true && !"" == true && !5 == false && !"x" == false; })",
                "f");
  EXPECT_TRUE(std::get<bool>(v));
}

TEST(LanguageEdgeTest, LogicalOpsOnNonBoolOperands) {
  Value v = run(R"(
int32 f() {
  int32 n = 0;
  if (1 && "x") { n += 1; }
  if (0 || "") { n += 10; }
  if (0 || 5) { n += 100; }
  return n;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 101);
}

TEST(LanguageEdgeTest, NegateOnStringRejected) {
  EXPECT_FALSE(compiles("int32 f() { return -\"abc\"; }"));
}

// --- Switch -------------------------------------------------------------------

TEST(LanguageEdgeTest, SwitchOnString) {
  Value v = run(R"(
int32 f(string s) {
  switch (s) {
    case "a": return 1;
    case "b": return 2;
    default: return -1;
  }
}
int32 g() { return f("b") * 10 + f("zz"); })",
                "g");
  EXPECT_EQ(std::get<int32_t>(v), 20 - 1);
}

TEST(LanguageEdgeTest, SwitchOnChar) {
  Value v = run(R"(
int32 f(char c) {
  switch (c) {
    case 'x': return 1;
    case 'y': return 2;
  }
  return 0;
}
int32 g() { return f('y') * 10 + f('z'); })",
                "g");
  EXPECT_EQ(std::get<int32_t>(v), 20);
}

TEST(LanguageEdgeTest, SwitchNoMatchNoDefault) {
  Value v = run(R"(
int32 f() {
  int32 n = 0;
  switch (9) {
    case 1: n = 1;
    case 2: n = 2;
  }
  return n;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 0);
}

TEST(LanguageEdgeTest, SwitchDefaultOnly) {
  Value v = run(R"(
int32 f() {
  switch (9) { default: return 7; }
  return 0;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 7);
}

TEST(LanguageEdgeTest, BreakInSwitchDoesNotExitLoop) {
  Value v = run(R"(
int32 f() {
  int32 count = 0;
  for (int32 i = 0; i < 3; i += 1) {
    switch (i) {
      case 0: break;   // exits switch, not the loop
      default: break;
    }
    count += 1;
  }
  return count;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 3);
}

TEST(LanguageEdgeTest, ContinueInsideSwitchAffectsLoop) {
  Value v = run(R"(
int32 f() {
  int32 total = 0;
  for (int32 i = 0; i < 4; i += 1) {
    switch (i) {
      case 2: continue;   // skips the add below
      default: break;
    }
    total += 1;
  }
  return total;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 3);
}

TEST(LanguageEdgeTest, CaseExpressionEvaluatedAtRuntime) {
  Value v = run(R"(
int32 f(int32 target, int32 probe) {
  switch (probe) {
    case target: return 1;
    default: return 0;
  }
}
int32 g() { return f(4, 4) * 10 + f(4, 5); })",
                "g");
  EXPECT_EQ(std::get<int32_t>(v), 10);
}

// --- Update expressions --------------------------------------------------------

TEST(LanguageEdgeTest, UpdateOnConstRejected) {
  EXPECT_FALSE(compiles("void f() { const int32 x = 1; x++; }"));
  EXPECT_FALSE(compiles("void f() { const int32 x = 1; x--; }"));
  EXPECT_FALSE(compiles("void f() { const int32 x = 1; x += 1; }"));
}

TEST(LanguageEdgeTest, UpdateOnStringRejected) {
  EXPECT_FALSE(compiles("void f() { string s = \"a\"; s++; }"));
}

TEST(LanguageEdgeTest, IncrementMapElement) {
  Value v = run(R"(
int32 f() {
  map<string, int32> m = {"k": 5};
  m["k"]++;
  ++m["k"];
  return m["k"];
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 7);
}

TEST(LanguageEdgeTest, IncrementMissingMapKeyFails) {
  EXPECT_FALSE(runFails("int32 f() { map<string,int32> m; m[\"x\"]++; return 0; }",
                        "f")
                   .empty());
}

TEST(LanguageEdgeTest, StringCompoundAssignAppends) {
  Value v = run(R"(
string f() {
  string s = "a";
  s += 'b';
  s += 5;
  return s;
})",
                "f");
  EXPECT_EQ(std::get<std::string>(v), "ab5");
}

TEST(LanguageEdgeTest, ModuloAssignOnStringFails) {
  EXPECT_FALSE(runFails("int32 f() { string s=\"a\"; s %= \"b\"; return 0; }",
                        "f")
                   .empty());
}

// --- Scoping & declaration errors ----------------------------------------------

TEST(LanguageEdgeTest, RedeclareSameScopeFails) {
  EXPECT_FALSE(compiles("void f() { int32 x = 1; int32 x = 2; }"));
}

TEST(LanguageEdgeTest, NestedBlockShadows) {
  Value v = run(R"(
int32 f() {
  int32 x = 1;
  {
    int32 x = 2;
    x += 10;
  }
  return x;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 1);
}

TEST(LanguageEdgeTest, BreakContinueOutsideLoopRejected) {
  EXPECT_FALSE(compiles("void f() { break; }"));
  EXPECT_FALSE(compiles("void f() { continue; }"));
}

TEST(LanguageEdgeTest, KeywordAsIdentifierRejected) {
  EXPECT_FALSE(compiles("int32 while() { return 1; }"));
  EXPECT_FALSE(compiles("void f() { int32 return = 1; }"));
}

TEST(LanguageEdgeTest, AssignmentToNonLvalueRejected) {
  EXPECT_FALSE(compiles("void f() { 1 = 2; }"));
  EXPECT_FALSE(compiles("void f() { int32 x = 1; (x + 1) = 5; }"));
}

TEST(LanguageEdgeTest, UndeclaredAssignFailsAtRuntime) {
  // Names resolve dynamically, so assignment to an undeclared variable
  // compiles but fails at runtime.
  EXPECT_NE(runFails("int32 f() { y = 1; return y; }", "f")
                .find("Undefined variable"),
            std::string::npos);
  EXPECT_NE(runFails("int32 f() { y += 1; return y; }", "f")
                .find("Undefined variable"),
            std::string::npos);
}

TEST(LanguageEdgeTest, EmptyAndCommentOnlyScriptsLoad) {
  EXPECT_TRUE(compiles(""));
  EXPECT_TRUE(compiles("// only a comment\n/* and a block */\n"));
  EXPECT_TRUE(compiles("void f() { }"));
}

TEST(LanguageEdgeTest, DeepParenthesization) {
  Value v = run("int32 f() { return ((((((1 + 2)))))) * (((3))); }", "f");
  EXPECT_EQ(std::get<int32_t>(v), 9);
}

TEST(LanguageEdgeTest, OperatorAssociativity) {
  Value v = run("int32 f() { return 10 - 2 - 3 + 2 * 3 % 4; }", "f");
  EXPECT_EQ(std::get<int32_t>(v), 5 + 2);
}

// --- Strings & containers -------------------------------------------------------

TEST(LanguageEdgeTest, StringPlusStringifiesScalars) {
  Value v = run(R"(
string f() {
  return 1 + "b" + "|" + "x" + true + "|" + "c" + 'z' + "|" + 2.5 + "!";
})",
                "f");
  EXPECT_EQ(std::get<std::string>(v), "1b|xtrue|cz|2.500000!");
}

TEST(LanguageEdgeTest, StringPlusArrayRejected) {
  EXPECT_NE(runFails("string f() { int32[] a = [1]; return \"a=\" + a; }", "f")
                .find("does not support arrays"),
            std::string::npos);
}

TEST(LanguageEdgeTest, EnumValueAsArrayIndexAndMapKey) {
  Value v = run(R"(
enum E { A, B, C }
int32 f() {
  int32[] arr = [10, 20, 30];
  map<int64, string> m = {1: "one"};
  return arr[E.B] + arr[E.C] + (has(m, E.B) ? 100 : 0);
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 20 + 30 + 100);
}

TEST(LanguageEdgeTest, MapWithCharAndBoolKeys) {
  Value v = run(R"(
int32 f() {
  map<char, int32> cm = {'a': 1, 'b': 2};
  map<bool, int32> bm = {true: 10, false: 20};
  return cm['b'] * 100 + bm[true] * 10 + bm[false];
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 200 + 100 + 20);
}

TEST(LanguageEdgeTest, SliceAssignRejected) {
  EXPECT_FALSE(compiles("void f() { int32[] a = [1,2,3]; a[0:2] = [9]; }"));
}

TEST(LanguageEdgeTest, MemberAccessOnMapRejected) {
  EXPECT_FALSE(compiles("int32 f() { map<string,int32> m; return m.x; }"));
}

// --- Structs & lambdas -----------------------------------------------------------

TEST(LanguageEdgeTest, EmptyStruct) {
  Value v = run(R"(
struct E { }
string f() { E e = E(); return typeof(e) + "|" + toString(e); })",
                "f");
  EXPECT_EQ(std::get<std::string>(v), "E|E{}");
}

TEST(LanguageEdgeTest, MethodRecursion) {
  Value v = run(R"(
struct T {
  int32 n;
  int32 fib(int32 k) { return k < 2 ? k : fib(k - 1) + fib(k - 2); }
}
int32 f() { T t = T(0); return t.fib(10); })",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 55);
}

TEST(LanguageEdgeTest, BoundMethodEqualityByValue) {
  // Bound methods compare by target proc + receiver value: two handles to
  // the same method are equal, and receivers that compare equal also
  // produce equal bound methods.
  Value v = run(R"(
struct P { int32 x; int32 get() { return x; } }
bool f() {
  P p = P(1);
  auto a = p.get;
  auto b = p.get;
  P q = P(1);
  auto c = q.get;
  P r = P(2);
  auto d = r.get;
  return a == b && a == c && a != d;
})",
                "f");
  EXPECT_TRUE(std::get<bool>(v));
}

TEST(LanguageEdgeTest, ThisOutsideMethodFailsAtRuntime) {
  // `this` is just an undeclared name outside methods — dynamic resolution
  // means it compiles but fails at runtime.
  EXPECT_FALSE(runFails("int32 f() { return this; }", "f").empty());
}

TEST(LanguageEdgeTest, LambdaReturningLambdaAnnotated) {
  // An explicit `-> fn(...) -> T` return type makes higher-order lambdas
  // work end to end.
  Value v = run(R"(
int32 f() {
  auto adder = fn(int32 n) -> fn(int32) -> int32 {
    return fn(int32 x) -> int32 { return x + n; };
  };
  auto add5 = adder(5);
  auto add2 = adder(2);
  return add5(10) + add2(10);
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 27);
}

TEST(LanguageEdgeTest, LambdaReturningLambdaUnannotatedRejected) {
  // Without a declared fn return type the validator cannot see that the
  // call result is callable.
  EXPECT_FALSE(compiles(
      "int32 f() {"
      "  auto adder = fn(int32 n) { return fn(int32 x) { return x + n; }; };"
      "  auto add5 = adder(5);"
      "  return add5(10);"
      "}"));
}

TEST(LanguageEdgeTest, LambdaEqualityUsesBodyAndCaptures) {
  // fn values compare by lambda body + full captured environment: a copy
  // of a handle is equal, but separately constructed closures capture
  // different environments and are not equal.
  Value v = run(R"(
fn(int32) -> int32 mk(int32 n) {
  return fn(int32 x) -> int32 { return x + n; };
}
bool f() {
  auto a = mk(1);
  auto b = a;
  auto c = mk(1);
  return a == a && a == b && a != c;
})",
                "f");
  EXPECT_TRUE(std::get<bool>(v));
}

TEST(LanguageEdgeTest, FunctionValueToString) {
  Value v = run(R"(
int32 dbl(int32 x) { return x * 2; }
string f() {
  auto named = dbl;
  auto anon = fn(int32 x) { return x; };
  return toString(named) + "|" + toString(anon);
})",
                "f");
  EXPECT_EQ(std::get<std::string>(v), "<fn dbl>|<fn <lambda>>");
}

TEST(LanguageEdgeTest, FunctionOrderingRejected) {
  EXPECT_FALSE(runFails("int32 f() { auto a = fn(){ return 1; };"
                        " auto b = fn(){ return 2; }; return a < b ? 1 : 0; }",
                        "f")
                   .empty());
}

// --- Cyclic / deep structures ----------------------------------------------------

TEST(LanguageEdgeTest, CyclicArrayToStringTruncates) {
  // Build a self-referencing array on the host and expose it as an
  // external variable: toString must stop at the depth guard.
  ScriptManager m;
  ArrayPtr cyc =
      ValueHelper::createArray(TypeInfo::autoType(), std::vector<Value>{});
  cyc->elements.push_back(cyc);
  m.registerExternalVariable("cyc", [cyc]() -> Value { return cyc; });
  std::vector<CompilationError> errors;
  ASSERT_TRUE(
      m.loadScriptSource("string f() { return toString(cyc); }", "t.script",
                         errors));
  Value v;
  std::string msg;
  ASSERT_TRUE(m.executeProcedure("f", {}, v, msg)) << msg;
  EXPECT_NE(std::get<std::string>(v).find("..."), std::string::npos);
  cyc->elements.clear(); // break the cycle so the shared_ptr frees
}

TEST(LanguageEdgeTest, CyclicEqualityDepthGuard) {
  ScriptManager m;
  ArrayPtr cyc =
      ValueHelper::createArray(TypeInfo::autoType(), std::vector<Value>{});
  cyc->elements.push_back(cyc);
  m.registerExternalVariable("cyc", [cyc]() -> Value { return cyc; });
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "bool f() { return cyc == cyc; }", "t.script", errors));
  Value v;
  std::string msg;
  EXPECT_FALSE(m.executeProcedure("f", {}, v, msg));
  EXPECT_NE(msg.find("depth limit"), std::string::npos) << msg;
  cyc->elements.clear(); // break the cycle so the shared_ptr frees
}

// --- Misc --------------------------------------------------------------------

TEST(LanguageEdgeTest, ForEachSeesElementsPushedDuringIteration) {
  // Live-view semantics: pushes inside the loop extend the iteration.
  Value v = run(R"(
int32 f() {
  int32[] a = [1];
  int32 visited = 0;
  for (int32 x : a) {
    visited += 1;
    if (visited < 3) { push(a, x + 1); }
  }
  return visited * 10 + a[2];
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 33);
}

TEST(LanguageEdgeTest, TernaryNested) {
  Value v = run(R"(
int32 f(int32 n) { return n > 0 ? 1 : n < 0 ? -1 : 0; }
int32 g() { return f(5) * 100 + f(-3) * 10 + f(0); })",
                "g");
  EXPECT_EQ(std::get<int32_t>(v), 100 - 10);
}

TEST(LanguageEdgeTest, ReturnValueInVoidProcedureRejected) {
  EXPECT_FALSE(compiles("void f() { return 5; }"));
  EXPECT_TRUE(compiles("void f() { return; }"));
}

TEST(LanguageEdgeTest, Int64BoundaryLiterals) {
  // INT64_MIN can't be written as a literal (the digits don't fit int64);
  // express it as INT64_MAX + 1 negated instead.
  Value v = run(R"(
int64 f() {
  int64 maxed = 9223372036854775807;
  int64 mined = -9223372036854775807 - 1;
  return maxed + mined;
})",
                "f");
  EXPECT_EQ(std::get<int64_t>(v), -1);
}

TEST(LanguageEdgeTest, ImportInsideProcedureRejected) {
  EXPECT_FALSE(compiles("int32 f() { import \"x.script\"; return 0; }"));
}

TEST(LanguageEdgeTest, BitNotOnStringRejected) {
  EXPECT_FALSE(compiles("int32 f() { return ~\"a\"; }"));
}
