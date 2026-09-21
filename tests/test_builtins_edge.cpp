// Edge-case and error-path coverage for builtins: wrong-type rejection,
// boundary indices, format placeholders, conversions, and math errors.
#include <algorithm>
#include <cmath>
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

// --- Collections ------------------------------------------------------------

TEST(BuiltinsEdgeTest, PushReturnsNewLength) {
  Value v = run(R"(
int32 f() {
  int32[] a = [1, 2];
  int32 n = push(a, 3);
  return n * 10 + len(a);
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 33);
}

TEST(BuiltinsEdgeTest, InsertAtEndAppends) {
  Value v = run(R"(
int32 f() {
  int32[] a = [1, 2];
  insert(a, 2, 9);
  return a[2];
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 9);
}

TEST(BuiltinsEdgeTest, InsertIndexOutOfBounds) {
  EXPECT_NE(runFails("int32 f() { int32[] a=[1]; insert(a, 5, 9); return 0; }",
                     "f")
                .find("insert index out of bounds"),
            std::string::npos);
  EXPECT_NE(runFails("int32 f() { int32[] a=[1]; insert(a, -1, 9); return 0; }",
                     "f")
                .find("insert index out of bounds"),
            std::string::npos);
}

TEST(BuiltinsEdgeTest, RemoveAtReturnsElementAndErrors) {
  Value v = run(R"(
int32 f() {
  int32[] a = [1, 2, 3];
  int32 r = removeAt(a, 0);
  return r * 10 + len(a);
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 12);
  EXPECT_NE(runFails("int32 f() { int32[] a=[1]; removeAt(a, 1); return 0; }",
                     "f")
                .find("removeAt index out of bounds"),
            std::string::npos);
  EXPECT_NE(runFails("int32 f() { removeAt(5, 0); return 0; }", "f")
                .find("removeAt expects an array"),
            std::string::npos);
}

TEST(BuiltinsEdgeTest, PopReturnsLastElement) {
  Value v = run(R"(
string f() {
  string[] a = ["x", "y"];
  string last = pop(a);
  return last + toString(len(a));
})",
                "f");
  EXPECT_EQ(std::get<std::string>(v), "y1");
}

TEST(BuiltinsEdgeTest, ClearArrayInPlace) {
  Value v = run(R"(
int32 f() {
  int32[] a = [1, 2, 3];
  clear(a);
  push(a, 9);
  return len(a) * 10 + a[0];
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 19);
}

TEST(BuiltinsEdgeTest, ClearRejectsScalar) {
  EXPECT_NE(runFails("int32 f() { clear(5); return 0; }", "f")
                .find("clear expects an array or map"),
            std::string::npos);
  EXPECT_NE(runFails("int32 f() { clear(\"s\"); return 0; }", "f")
                .find("clear expects an array or map"),
            std::string::npos);
}

TEST(BuiltinsEdgeTest, MapOpsRejectNonMap) {
  const char *bodies[] = {
      "int32 f() { has([1], 1); return 0; }",
      "int32 f() { remove(5, 1); return 0; }",
      "int32 f() { keys(\"s\"); return 0; }",
      "int32 f() { values([1]); return 0; }",
  };
  for (auto *b : bodies) {
    EXPECT_FALSE(runFails(b, "f").empty());
  }
}

TEST(BuiltinsEdgeTest, HasRejectsUnconvertibleKey) {
  // `has` requires the probe to convert to the key type — a string probe
  // against an int64-keyed map errors rather than silently missing.
  std::string msg = runFails(R"(
bool f() {
  map<int64, string> m = {1: "a"};
  return has(m, "1");
})",
                             "f");
  EXPECT_NE(msg.find("Cannot convert string to int64"), std::string::npos)
      << msg;
}

TEST(BuiltinsEdgeTest, KeysValuesOnEmptyMap) {
  Value v = run(R"(
int32 f() {
  map<string, int32> m;
  return len(keys(m)) + len(values(m)) + size(m);
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 0);
}

TEST(BuiltinsEdgeTest, ValuesFollowKeyOrder) {
  Value v = run(R"(
string f() {
  map<string, int32> m = {"b": 2, "a": 1};
  int32[] vs = values(m);
  return toString(vs[0]) + toString(vs[1]);
})",
                "f");
  EXPECT_EQ(std::get<std::string>(v), "12"); // "a" sorts before "b"
}

TEST(BuiltinsEdgeTest, LenRejectsScalar) {
  EXPECT_NE(runFails("int32 f() { return len(42); }", "f")
                .find("len expects an array, map, or string"),
            std::string::npos);
}

TEST(BuiltinsEdgeTest, ReverseRejectsScalar) {
  EXPECT_NE(runFails("int32 f() { reverse(7); return 0; }", "f")
                .find("reverse expects a string or array"),
            std::string::npos);
}

TEST(BuiltinsEdgeTest, ReverseArrayMutatesInPlace) {
  Value v = run(R"(
int32 f() {
  int32[] a = [1, 2, 3];
  int32[] b = reverse(a);
  return a[0] * 100 + b[1] * 10 + len(b);
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 323);
}

TEST(BuiltinsEdgeTest, ContainsRejectsNonContainer) {
  EXPECT_NE(runFails("int32 f() { return contains(5, 1) ? 1 : 0; }", "f")
                .find("contains expects a string or array"),
            std::string::npos);
}

// --- Strings ----------------------------------------------------------------

TEST(BuiltinsEdgeTest, SubstrBoundary) {
  Value v = run(R"(
string f() {
  string s = "hello";
  return substr(s, 5) + "|" + substr(s, 1, 2);
})",
                "f");
  EXPECT_EQ(std::get<std::string>(v), "|el");
}

TEST(BuiltinsEdgeTest, SubstrOutOfBounds) {
  EXPECT_NE(runFails("int32 f() { substr(\"hi\", 3); return 0; }", "f")
                .find("substr start out of bounds"),
            std::string::npos);
  EXPECT_NE(runFails("int32 f() { substr(\"hi\", -1); return 0; }", "f")
                .find("substr start out of bounds"),
            std::string::npos);
  EXPECT_NE(runFails("int32 f() { substr(\"hi\", 0, -1); return 0; }", "f")
                .find("substr length must be non-negative"),
            std::string::npos);
}

TEST(BuiltinsEdgeTest, CharAtErrors) {
  EXPECT_NE(runFails("int32 f() { charAt(\"hi\", 2); return 0; }", "f")
                .find("charAt index out of bounds"),
            std::string::npos);
  EXPECT_NE(runFails("int32 f() { charAt(\"hi\", -1); return 0; }", "f")
                .find("charAt index out of bounds"),
            std::string::npos);
}

TEST(BuiltinsEdgeTest, IndexOfFromAndMisses) {
  Value v = run(R"(
int32 f() {
  string s = "aXbXc";
  return indexOf(s, "X", 2) * 100 + indexOf(s, "zzz") * 10 + indexOf(s, "c");
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 300 - 10 + 4);
}

TEST(BuiltinsEdgeTest, IndexOfNegativeFromRejected) {
  EXPECT_NE(runFails("int32 f() { indexOf(\"abc\", \"b\", -1); return 0; }", "f")
                .find("indexOf start must be non-negative"),
            std::string::npos);
}

TEST(BuiltinsEdgeTest, ReplaceAllOccurrences) {
  Value v = run(R"(
string f() { return replace("aaa", "a", "bb"); })",
                "f");
  EXPECT_EQ(std::get<std::string>(v), "bbbbbb");
}

TEST(BuiltinsEdgeTest, ReplaceEmptyFromRejected) {
  EXPECT_NE(runFails("int32 f() { replace(\"abc\", \"\", \"x\"); return 0; }",
                     "f")
                .find("'from' string must not be empty"),
            std::string::npos);
}

TEST(BuiltinsEdgeTest, SplitDefaultsAndChars) {
  Value v = run(R"(
string f() {
  string[] def = split("a,b,c");
  string[] chars = split("abc", "");
  string[] nomatch = split("abc", "|");
  string[] trailing = split("a,b,");
  return toString(len(def)) + toString(len(chars)) + chars[1] +
         toString(len(nomatch)) + nomatch[0] + toString(len(trailing)) +
         trailing[2];
})",
                "f");
  // No-match returns the whole string; a trailing separator keeps the
  // empty tail piece.
  EXPECT_EQ(std::get<std::string>(v), "33b1abc3");
}

TEST(BuiltinsEdgeTest, JoinStringifiesElements) {
  Value v = run(R"(
string f() {
  int32[] nums = [1, 2, 3];
  return join(nums, "-") + "|" + join(nums) + "|" + join([]);
})",
                "f");
  EXPECT_EQ(std::get<std::string>(v), "1-2-3|123|");
}

TEST(BuiltinsEdgeTest, JoinRejectsNonArray) {
  EXPECT_NE(runFails("int32 f() { join(\"ab\"); return 0; }", "f")
                .find("join expects an array"),
            std::string::npos);
}

TEST(BuiltinsEdgeTest, RepeatZeroAndNegative) {
  Value v = run("string f() { return repeat(\"ab\", 0) + \"!\"; }", "f");
  EXPECT_EQ(std::get<std::string>(v), "!");
  EXPECT_NE(runFails("int32 f() { repeat(\"ab\", -1); return 0; }", "f")
                .find("repeat count must be non-negative"),
            std::string::npos);
}

TEST(BuiltinsEdgeTest, FormatEscapesAndErrors) {
  Value v = run(R"(
string f() {
  return format("{{x}}={}", 7) + "|" + format("{}-{}", "a", 1);
})",
                "f");
  EXPECT_EQ(std::get<std::string>(v), "{x}=7|a-1");
  EXPECT_NE(runFails("int32 f() { format(\"{} {}\", 1); return 0; }", "f")
                .find("format: not enough arguments"),
            std::string::npos);
  EXPECT_NE(runFails("int32 f() { format(\"{}\", 1, 2); return 0; }", "f")
                .find("format: too many arguments"),
            std::string::npos);
  EXPECT_NE(runFails("int32 f() { format(\"{0}\", 1); return 0; }", "f")
                .find("format: only empty '{}' placeholders are supported"),
            std::string::npos);
}

TEST(BuiltinsEdgeTest, StringIndexReadVsWrite) {
  // Reading a string index yields a char; writing fails at runtime.
  Value v = run("int32 f() { return \"abc\"[1]; }", "f");
  EXPECT_EQ(std::get<int32_t>(v), 'b');
  std::string msg =
      runFails("int32 f() { string s = \"ab\"; s[0] = 'z'; return 0; }", "f");
  EXPECT_NE(msg.find("Index assignment on non-array"), std::string::npos)
      << msg;
}

TEST(BuiltinsEdgeTest, TrimOnlyWhitespace) {
  Value v = run("int32 f() { return len(trim(\"  \t\n \")); }", "f");
  EXPECT_EQ(std::get<int32_t>(v), 0);
}

TEST(BuiltinsEdgeTest, CaseConversionIgnoresNonLetters) {
  Value v =
      run("string f() { return toUpper(\"a1_!\") + toLower(\"B2#?\"); }", "f");
  EXPECT_EQ(std::get<std::string>(v), "A1_!b2#?");
}

// --- Math -------------------------------------------------------------------

TEST(BuiltinsEdgeTest, AbsPreservesTypeAndRejectsNonNumeric) {
  Value v = run(R"(
string f() {
  int32 i = abs(-5);
  double d = abs(-2.5);
  uint8 u = abs(7);
  return typeof(i) + " " + toString(d) + " " + toString(u);
})",
                "f");
  EXPECT_EQ(std::get<std::string>(v), "int32 2.500000 7");
  EXPECT_NE(runFails("int32 f() { abs(\"x\"); return 0; }", "f")
                .find("abs expects a numeric value"),
            std::string::npos);
  EXPECT_NE(runFails("int32 f() { abs(true); return 0; }", "f")
                .find("abs expects a numeric value"),
            std::string::npos);
}

TEST(BuiltinsEdgeTest, MinMaxClampMixedTypes) {
  Value v = run(R"(
double f() {
  return min(3, 2.5) + max(1, 2.5) + clamp(9.5, 0.0, 5.0);
})",
                "f");
  EXPECT_DOUBLE_EQ(std::get<double>(v), 2.5 + 2.5 + 5.0);
}

TEST(BuiltinsEdgeTest, SqrtNegativeThrows) {
  EXPECT_NE(runFails("int32 f() { sqrt(-1.0); return 0; }", "f")
                .find("sqrt of negative number"),
            std::string::npos);
}

TEST(BuiltinsEdgeTest, FmodByZeroThrows) {
  EXPECT_NE(runFails("int32 f() { fmod(1.0, 0.0); return 0; }", "f")
                .find("fmod by zero"),
            std::string::npos);
}

TEST(BuiltinsEdgeTest, LogNonPositiveThrows) {
  EXPECT_NE(runFails("int32 f() { log(0.0); return 0; }", "f")
                .find("log of non-positive number"),
            std::string::npos);
  EXPECT_NE(runFails("int32 f() { log10(-3.0); return 0; }", "f")
                .find("log10 of non-positive number"),
            std::string::npos);
}

TEST(BuiltinsEdgeTest, RoundingFunctions) {
  Value v = run(R"(
double f() {
  return floor(-1.5) * 1000 + ceil(-1.5) * 100 + round(2.5) * 10 +
         trunc(-2.7);
})",
                "f");
  // floor(-1.5)=-2, ceil(-1.5)=-1, round(2.5)=3, trunc(-2.7)=-2
  EXPECT_DOUBLE_EQ(std::get<double>(v), -2000 - 100 + 30 - 2);
}

TEST(BuiltinsEdgeTest, ExpLogTrigConstants) {
  Value v = run(R"(
double f() {
  return exp(0.0) + log(1.0) + log10(100.0) + pow(2.0, 10.0) +
         atan2(0.0, 1.0) + fmod(7.5, 2.0);
})",
                "f");
  EXPECT_DOUBLE_EQ(std::get<double>(v), 1.0 + 0.0 + 2.0 + 1024.0 + 0.0 + 1.5);
}

TEST(BuiltinsEdgeTest, PiAndTrigIdentity) {
  Value v = run(R"(
double f() {
  double p = pi();
  return sin(p / 2.0) + cos(0.0) + asin(1.0) / (p / 2.0) +
         acos(1.0) + atan(1.0) / (p / 4.0) + tan(0.0);
})",
                "f");
  EXPECT_NEAR(std::get<double>(v), 4.0, 1e-9);
}

TEST(BuiltinsEdgeTest, RandomInRangeAndRandIntSwapped) {
  Value v = run(R"(
bool f() {
  srand(1);
  double r = random();
  int64 n = randInt(10, 3); // swapped bounds are accepted
  return r >= 0.0 && r < 1.0 && n >= 3 && n <= 10;
})",
                "f");
  EXPECT_TRUE(std::get<bool>(v));
}

TEST(BuiltinsEdgeTest, SrandMakesSequenceDeterministic) {
  Value v = run(R"(
bool f() {
  srand(42);
  double a = random();
  int64 n = randInt(0, 100);
  srand(42);
  return random() == a && randInt(0, 100) == n;
})",
                "f");
  EXPECT_TRUE(std::get<bool>(v));
}

// --- Conversions ------------------------------------------------------------

TEST(BuiltinsEdgeTest, ToIntConversions) {
  Value v = run(R"(
int64 f() {
  uint8 u = 7;
  return toInt(3.99) + toInt(true) + toInt('a') + toInt(u);
})",
                "f");
  EXPECT_EQ(std::get<int64_t>(v), 3 + 1 + 97 + 7);
}

TEST(BuiltinsEdgeTest, ToIntRejectsStringAndContainers) {
  EXPECT_FALSE(runFails("int32 f() { return toInt(\"12\"); }", "f").empty());
  EXPECT_FALSE(runFails("int32 f() { int32[] a=[1]; return toInt(a); }", "f")
                   .empty());
}

TEST(BuiltinsEdgeTest, ToUIntAndToDouble) {
  Value v = run(R"(
double f() {
  uint8 u = 255;
  return toDouble(toUInt(u)) + toDouble(0.5) + toFloat(2.5);
})",
                "f");
  EXPECT_DOUBLE_EQ(std::get<double>(v), 255.0 + 0.5 + 2.5);
}

TEST(BuiltinsEdgeTest, ToBoolSemantics) {
  Value v = run(R"(
bool f() {
  int32[] empty = [];
  return toBool(0) == false && toBool(-3) == true &&
         toBool("") == false && toBool("false") == true &&
         toBool(empty) == true && toBool(0.0) == false;
})",
                "f");
  EXPECT_TRUE(std::get<bool>(v));
}

TEST(BuiltinsEdgeTest, ToCharConversions) {
  Value v = run(R"(
int32 f() {
  // char + char stays char: 65 + 90 + 97 = 252 wraps to -4.
  return toChar(65) + toChar("Z") + toChar('a');
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), -4);
  EXPECT_NE(runFails("int32 f() { toChar(\"ab\"); return 0; }", "f")
                .find("toChar expects a single-character string"),
            std::string::npos);
  EXPECT_NE(runFails("int32 f() { toChar(\"\"); return 0; }", "f")
                .find("toChar expects a single-character string"),
            std::string::npos);
}

TEST(BuiltinsEdgeTest, ParseIntToleranceAndErrors) {
  Value v = run(R"(
int64 f() {
  return parseInt("  -42 ") + parseInt("+7");
})",
                "f");
  EXPECT_EQ(std::get<int64_t>(v), -35);
  EXPECT_FALSE(runFails("int32 f() { return parseInt(\"\"); }", "f").empty());
  EXPECT_NE(runFails("int32 f() { return parseInt(\"12abc\"); }", "f")
                .find("parseInt: cannot parse"),
            std::string::npos);
  EXPECT_NE(runFails("int32 f() { return parseInt(\"abc\"); }", "f")
                .find("parseInt: cannot parse"),
            std::string::npos);
}

TEST(BuiltinsEdgeTest, ParseDoubleToleranceAndErrors) {
  Value v = run(R"(
double f() {
  return parseDouble(" 1.5 ") + parseDouble("1e2");
})",
                "f");
  EXPECT_DOUBLE_EQ(std::get<double>(v), 101.5);
  EXPECT_NE(runFails("int32 f() { return parseDouble(\"x\"); }", "f")
                .find("parseDouble: cannot parse"),
            std::string::npos);
}

TEST(BuiltinsEdgeTest, TypeofNames) {
  Value v = run(R"(
string f() {
  int32[][] grid = [[1]];
  map<string, int32> m;
  float fv = 1.5;
  auto fnv = fn(int32 x) -> int32 { return x; };
  uint64 uv = 1;
  return typeof(1) + "," + typeof(uv) + "," + typeof(1.5) + "," +
         typeof(fv) + "," + typeof("s") + "," + typeof(true) + "," +
         typeof('c') + "," + typeof(grid) + "," + typeof(m) + "," +
         typeof(fnv);
})",
                "f");
  EXPECT_EQ(std::get<std::string>(v),
            "int32,uint64,double,float,string,bool,char,int32[][],"
            "map<string, int32>,fn(int32)->int32");
}

TEST(BuiltinsEdgeTest, IsArrayIsMapNegatives) {
  Value v = run(R"(
bool f() {
  int32[] a = [1];
  map<string, int32> m;
  return isArray(a) && !isArray(m) && !isArray("s") && !isArray(1) &&
         isMap(m) && !isMap(a) && !isMap("s");
})",
                "f");
  EXPECT_TRUE(std::get<bool>(v));
}

// --- Output & control -------------------------------------------------------

TEST(BuiltinsEdgeTest, PrintHandlesMultipleArgs) {
  ScriptManager m;
  std::string out;
  m.setOutputCallback([&out](const std::string &s) { out += s; });
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "void f() { print(\"a\", 1, \"b\"); println(\"x\"); println(); }",
      "t.script", errors));
  Value v;
  std::string msg;
  ASSERT_TRUE(m.executeProcedure("f", {}, v, msg)) << msg;
  EXPECT_EQ(out, "a1bx\n\n");
}

TEST(BuiltinsEdgeTest, PrintStringifiesContainers) {
  ScriptManager m;
  std::string out;
  m.setOutputCallback([&out](const std::string &s) { out += s; });
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "void f() { int32[] a = [1, 2]; println(a); println(true); }",
      "t.script", errors));
  Value v;
  std::string msg;
  ASSERT_TRUE(m.executeProcedure("f", {}, v, msg)) << msg;
  EXPECT_EQ(out, "[1, 2]\ntrue\n");
}

TEST(BuiltinsEdgeTest, ErrorStringifiesArgument) {
  std::string msg =
      runFails("int32 f() { error(42); return 0; }", "f");
  EXPECT_NE(msg.find("42"), std::string::npos) << msg;
}

TEST(BuiltinsEdgeTest, AssertVariants) {
  Value v = run("bool f() { return assert(1 == 1); }", "f");
  EXPECT_TRUE(std::get<bool>(v));
  EXPECT_NE(runFails("int32 f() { assert(false); return 0; }", "f")
                .find("Assertion failed"),
            std::string::npos);
  EXPECT_NE(runFails("int32 f() { assert(1 > 2, \"custom msg\"); return 0; }",
                     "f")
                .find("custom msg"),
            std::string::npos);
}

TEST(BuiltinsEdgeTest, BuiltinArityErrors) {
  EXPECT_FALSE(runFails("int32 f() { return len(); }", "f").empty());
  EXPECT_FALSE(runFails("int32 f() { return len(\"a\", \"b\"); }", "f").empty());
  EXPECT_FALSE(runFails("int32 f() { push([1]); return 0; }", "f").empty());
}
