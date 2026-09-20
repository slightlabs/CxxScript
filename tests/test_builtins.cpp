#include "ScriptManager.h"
#include <gtest/gtest.h>
#include <algorithm>

using namespace Script;

namespace {

// Load source and run a procedure, expecting a Value result.
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

} // namespace

// ---------------------------------------------------------------------------
// String builtins
// ---------------------------------------------------------------------------

TEST(BuiltinsTest, LenWorksOnStringsAndArrays) {
  EXPECT_EQ(std::get<int32_t>(run("int32 f() { return len(\"hello\"); }", "f")),
            5);
  EXPECT_EQ(
      std::get<int32_t>(run("int32 f() { int32[] a = [1,2,3]; return len(a); }",
                            "f")),
      3);
}

TEST(BuiltinsTest, SubstrAndCharAt) {
  EXPECT_EQ(std::get<std::string>(
                run("string f() { return substr(\"hello world\", 6); }", "f")),
            "world");
  EXPECT_EQ(std::get<std::string>(
                run("string f() { return substr(\"hello\", 1, 3); }", "f")),
            "ell");
  EXPECT_EQ(std::get<char>(run("char f() { return charAt(\"abc\", 1); }", "f")),
            'b');
}

TEST(BuiltinsTest, StringIndexing) {
  EXPECT_EQ(std::get<char>(run("char f() { string s = \"abc\"; return s[0]; }",
                               "f")),
            'a');
}

TEST(BuiltinsTest, IndexOfContainsStartsEnds) {
  EXPECT_EQ(
      std::get<int32_t>(run("int32 f() { return indexOf(\"hello\", \"ll\"); }",
                            "f")),
      2);
  EXPECT_EQ(std::get<int32_t>(
                run("int32 f() { return indexOf(\"hello\", \"zz\"); }", "f")),
            -1);
  EXPECT_TRUE(std::get<bool>(
      run("bool f() { return contains(\"hello\", \"ell\"); }", "f")));
  EXPECT_TRUE(std::get<bool>(
      run("bool f() { return startsWith(\"hello\", \"he\"); }", "f")));
  EXPECT_TRUE(std::get<bool>(
      run("bool f() { return endsWith(\"hello\", \"lo\"); }", "f")));
}

TEST(BuiltinsTest, CaseTrimReplace) {
  EXPECT_EQ(std::get<std::string>(
                run("string f() { return toUpper(\"abc\"); }", "f")),
            "ABC");
  EXPECT_EQ(std::get<std::string>(
                run("string f() { return toLower(\"AbC\"); }", "f")),
            "abc");
  EXPECT_EQ(std::get<std::string>(
                run("string f() { return trim(\"  hi  \"); }", "f")),
            "hi");
  EXPECT_EQ(std::get<std::string>(run("string f() { return replace(\"a-b-c\", "
                                      "\"-\", \"+\"); }",
                                      "f")),
            "a+b+c");
}

TEST(BuiltinsTest, SplitJoinRepeatReverse) {
  EXPECT_EQ(std::get<std::string>(run("string f() { string[] p = "
                                      "split(\"a,b,c\"); return p[1]; }",
                                      "f")),
            "b");
  EXPECT_EQ(std::get<int32_t>(run("int32 f() { return len(split(\"a,b,c\")); }",
                                  "f")),
            3);
  EXPECT_EQ(std::get<std::string>(
                run("string f() { int32[] n = [1,2,3]; return join(n, \"-\"); }",
                    "f")),
            "1-2-3");
  EXPECT_EQ(std::get<std::string>(
                run("string f() { return repeat(\"ab\", 3); }", "f")),
            "ababab");
  EXPECT_EQ(std::get<std::string>(
                run("string f() { return reverse(\"abc\"); }", "f")),
            "cba");
  EXPECT_EQ(
      std::get<std::string>(run("string f() { return join(reverse([1,2,3])); }",
                                "f")),
      "321");
}

TEST(BuiltinsTest, Format) {
  EXPECT_EQ(std::get<std::string>(run("string f() { return format(\"{}+{}={}\", "
                                      "1, 2, 3); }",
                                      "f")),
            "1+2=3");
}

// ---------------------------------------------------------------------------
// Array builtins
// ---------------------------------------------------------------------------

TEST(BuiltinsTest, InsertRemoveAtClear) {
  EXPECT_EQ(std::get<int32_t>(run("int32 f() { int32[] a = [1,3]; insert(a, 1, "
                                  "2); return a[1] + len(a); }",
                                  "f")),
            5);
  EXPECT_EQ(std::get<int32_t>(run("int32 f() { int32[] a = [1,2,3]; return "
                                  "removeAt(a, 1); }",
                                  "f")),
            2);
  EXPECT_EQ(std::get<int32_t>(run("int32 f() { int32[] a = [1,2,3]; "
                                  "clear(a); return len(a); }",
                                  "f")),
            0);
}

TEST(BuiltinsTest, ContainsArray) {
  EXPECT_TRUE(std::get<bool>(
      run("bool f() { int32[] a = [1,2,3]; return contains(a, 2); }", "f")));
  EXPECT_FALSE(std::get<bool>(
      run("bool f() { int32[] a = [1,2,3]; return contains(a, 9); }", "f")));
}

// ---------------------------------------------------------------------------
// Math builtins
// ---------------------------------------------------------------------------

TEST(BuiltinsTest, AbsMinMaxClamp) {
  EXPECT_EQ(std::get<int32_t>(run("int32 f() { return abs(0 - 7); }", "f")), 7);
  EXPECT_DOUBLE_EQ(
      std::get<double>(run("double f() { return abs(0 - 2.5); }", "f")), 2.5);
  EXPECT_EQ(std::get<int32_t>(run("int32 f() { return min(3, 8); }", "f")), 3);
  EXPECT_EQ(std::get<int32_t>(run("int32 f() { return max(3, 8); }", "f")), 8);
  EXPECT_EQ(
      std::get<int32_t>(run("int32 f() { return clamp(15, 0, 10); }", "f")),
      10);
}

TEST(BuiltinsTest, PowSqrtFloorCeilRound) {
  EXPECT_DOUBLE_EQ(
      std::get<double>(run("double f() { return pow(2, 10); }", "f")), 1024.0);
  EXPECT_DOUBLE_EQ(
      std::get<double>(run("double f() { return sqrt(16); }", "f")), 4.0);
  EXPECT_DOUBLE_EQ(
      std::get<double>(run("double f() { return floor(3.7); }", "f")), 3.0);
  EXPECT_DOUBLE_EQ(
      std::get<double>(run("double f() { return ceil(3.2); }", "f")), 4.0);
  EXPECT_DOUBLE_EQ(
      std::get<double>(run("double f() { return round(3.5); }", "f")), 4.0);
}

TEST(BuiltinsTest, TrigAndLog) {
  EXPECT_DOUBLE_EQ(
      std::get<double>(run("double f() { return sin(0.0); }", "f")), 0.0);
  EXPECT_DOUBLE_EQ(
      std::get<double>(run("double f() { return log(1.0); }", "f")), 0.0);
  EXPECT_DOUBLE_EQ(
      std::get<double>(run("double f() { return pi(); }", "f")),
      3.14159265358979323846);
}

TEST(BuiltinsTest, RandomSeededDeterministic) {
  std::string src = R"(
    double firstAndLast() {
      srand(42);
      double a = random();
      srand(42);
      double b = random();
      return a - b;   // same seed -> same value -> 0
    }
  )";
  EXPECT_DOUBLE_EQ(std::get<double>(run(src, "firstAndLast")), 0.0);

  Value v = run("int64 f() { return randInt(5, 10); }", "f");
  int64_t n = std::get<int64_t>(v);
  EXPECT_GE(n, 5);
  EXPECT_LE(n, 10);
}

// ---------------------------------------------------------------------------
// Conversion / introspection builtins
// ---------------------------------------------------------------------------

TEST(BuiltinsTest, Conversions) {
  EXPECT_EQ(std::get<int64_t>(run("int64 f() { return toInt(3.9); }", "f")), 3);
  EXPECT_EQ(std::get<uint64_t>(
                run("uint64 f() { return toUInt(7); }", "f")),
            7);
  EXPECT_DOUBLE_EQ(
      std::get<double>(run("double f() { return toDouble(5); }", "f")), 5.0);
  EXPECT_EQ(std::get<std::string>(
                run("string f() { return toString(42); }", "f")),
            "42");
  EXPECT_TRUE(
      std::get<bool>(run("bool f() { return toBool(1); }", "f")));
  EXPECT_EQ(std::get<char>(run("char f() { return toChar(65); }", "f")), 'A');
}

TEST(BuiltinsTest, ParseNumbers) {
  EXPECT_EQ(
      std::get<int64_t>(run("int64 f() { return parseInt(\"123\"); }", "f")),
      123);
  EXPECT_DOUBLE_EQ(std::get<double>(
                       run("double f() { return parseDouble(\"2.5\"); }", "f")),
                   2.5);
}

TEST(BuiltinsTest, TypeofIsArray) {
  EXPECT_EQ(std::get<std::string>(
                run("string f() { return typeof(5); }", "f")),
            "int32");
  EXPECT_EQ(std::get<std::string>(
                run("string f() { int32[] a = [1]; return typeof(a); }", "f")),
            "int32[]");
  EXPECT_TRUE(std::get<bool>(
      run("bool f() { int32[] a = []; return isArray(a); }", "f")));
  EXPECT_FALSE(
      std::get<bool>(run("bool f() { return isArray(1); }", "f")));
}

// ---------------------------------------------------------------------------
// print / assert / error
// ---------------------------------------------------------------------------

TEST(BuiltinsTest, PrintUsesOutputCallback) {
  ScriptManager manager;
  std::string captured;
  manager.setOutputCallback(
      [&captured](const std::string &s) { captured += s; });

  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(
      "void f() { print(\"a\", 1, \"b\"); println(\"c\"); }", "t.script",
      errors));
  Value result;
  std::string errorMsg;
  ASSERT_TRUE(manager.executeProcedure("f", {}, result, errorMsg)) << errorMsg;
  EXPECT_EQ(captured, "a1bc\n");
}

TEST(BuiltinsTest, AssertPassesAndFails) {
  Value ok = run("bool f() { return assert(1 == 1); }", "f");
  EXPECT_TRUE(std::get<bool>(ok));

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(
      "void f() { assert(1 == 2, \"math broke\"); }", "t.script", errors));
  Value result;
  std::string errorMsg;
  ASSERT_FALSE(manager.executeProcedure("f", {}, result, errorMsg));
  EXPECT_NE(errorMsg.find("math broke"), std::string::npos);
}

TEST(BuiltinsTest, ErrorBuiltinRaisesRuntimeError) {
  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(
      "void f() { error(\"custom failure\"); }", "t.script", errors));
  Value result;
  std::string errorMsg;
  ASSERT_FALSE(manager.executeProcedure("f", {}, result, errorMsg));
  EXPECT_NE(errorMsg.find("custom failure"), std::string::npos);
}

// ---------------------------------------------------------------------------
// String interpolation
// ---------------------------------------------------------------------------

TEST(BuiltinsTest, InterpolationBasic) {
  EXPECT_EQ(std::get<std::string>(run("string f() { string name = \"world\"; "
                                      "return \"hi ${name}!\"; }",
                                      "f")),
            "hi world!");
}

TEST(BuiltinsTest, InterpolationExpressions) {
  EXPECT_EQ(std::get<std::string>(
                run("string f() { return \"sum=${2 + 3} arr=${len([1,2])}\"; }",
                    "f")),
            "sum=5 arr=2");
}

TEST(BuiltinsTest, InterpolationEscapedDollar) {
  EXPECT_EQ(std::get<std::string>(
                run("string f() { return \"a\\${notcode}b\"; }", "f")),
            "a${notcode}b");
}

TEST(BuiltinsTest, InterpolationNestedStringLiteral) {
  // '}' inside a nested string literal must not end the interpolation
  EXPECT_EQ(std::get<std::string>(
                run("string f() { return \"x${ \"in}\" }y\"; }", "f")),
            "xin}y");
}

TEST(BuiltinsTest, InterpolationMultipleParts) {
  EXPECT_EQ(std::get<std::string>(
                run("string f() { int32 a = 1; int32 b = 2; return \"${a},${b}\"; }",
                    "f")),
            "1,2");
}
