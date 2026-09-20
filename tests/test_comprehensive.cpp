#include "ScriptManager.h"
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>

using namespace Script;

// ========== DATA TYPE TESTS ==========

TEST(ComprehensiveTest, AllIntegerTypes) {

  std::string source =
      "int8 testInt8(int8 a, int8 b) { return a + b; }"
      "uint8 testUInt8(uint8 a, uint8 b) { return a + b; }"
      "int16 testInt16(int16 a, int16 b) { return a + b; }"
      "uint16 testUInt16(uint16 a, uint16 b) { return a + b; }"
      "int32 testInt32(int32 a, int32 b) { return a + b; }"
      "uint32 testUInt32(uint32 a, uint32 b) { return a + b; }"
      "int64 testInt64(int64 a, int64 b) { return a + b; }"
      "uint64 testUInt64(uint64 a, uint64 b) { return a + b; }";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "types.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  // Test int8
  std::vector<Value> args8 = {static_cast<int8_t>(50), static_cast<int8_t>(30)};
  success = manager.executeProcedure("testInt8", args8, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<int8_t>(result) == 80);

  // Test uint8
  std::vector<Value> argsu8 = {static_cast<uint8_t>(100),
                               static_cast<uint8_t>(50)};
  success = manager.executeProcedure("testUInt8", argsu8, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<uint8_t>(result) == 150);

  // Test int16
  std::vector<Value> args16 = {static_cast<int16_t>(1000),
                               static_cast<int16_t>(500)};
  success = manager.executeProcedure("testInt16", args16, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<int16_t>(result) == 1500);

  // Test uint16
  std::vector<Value> argsu16 = {static_cast<uint16_t>(30000),
                                static_cast<uint16_t>(20000)};
  success = manager.executeProcedure("testUInt16", argsu16, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<uint16_t>(result) == 50000);

  // Test int32
  std::vector<Value> args32 = {static_cast<int32_t>(100000),
                               static_cast<int32_t>(50000)};
  success = manager.executeProcedure("testInt32", args32, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<int32_t>(result) == 150000);

  // Test uint32
  std::vector<Value> argsu32 = {static_cast<uint32_t>(2000000000),
                                static_cast<uint32_t>(1000000000)};
  success = manager.executeProcedure("testUInt32", argsu32, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<uint32_t>(result) == 3000000000u);

  // Test int64
  std::vector<Value> args64 = {static_cast<int64_t>(5000000000LL),
                               static_cast<int64_t>(3000000000LL)};
  success = manager.executeProcedure("testInt64", args64, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<int64_t>(result) == 8000000000LL);

  // Test uint64
  std::vector<Value> argsu64 = {static_cast<uint64_t>(10000000000ULL),
                                static_cast<uint64_t>(5000000000ULL)};
  success = manager.executeProcedure("testUInt64", argsu64, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<uint64_t>(result) == 15000000000ULL);

}

TEST(ComprehensiveTest, FloatingPointBasics) {

  std::string source =
      "double add(double a, double b) { return a + b; }"
      "double mixed(double a, int32 b) { double t = a * 2.5; return t + b; }"
      "double negate(double a) { return -a; }"
      "double literalInit() { double x = 1.25; double y = 2.75; return x + y; }";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "float.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  // Basic addition
  std::vector<Value> addArgs = {1.5, 2.25};
  success = manager.executeProcedure("add", addArgs, result, errorMsg);
  EXPECT_TRUE(success) << errorMsg;
  EXPECT_NEAR(std::get<double>(result), 3.75, 1e-9);

  // Mixing integers and doubles
  std::vector<Value> mixedArgs = {1.2, static_cast<int32_t>(3)};
  success = manager.executeProcedure("mixed", mixedArgs, result, errorMsg);
  EXPECT_TRUE(success) << errorMsg;
  EXPECT_NEAR(std::get<double>(result), 6.0, 1e-9);

  // Unary negate preserves double
  std::vector<Value> negArgs = {4.0};
  success = manager.executeProcedure("negate", negArgs, result, errorMsg);
  EXPECT_TRUE(success) << errorMsg;
  EXPECT_NEAR(std::get<double>(result), -4.0, 1e-9);

  // Literal initialization
  success = manager.executeProcedure("literalInit", {}, result, errorMsg);
  EXPECT_TRUE(success) << errorMsg;
  EXPECT_NEAR(std::get<double>(result), 4.0, 1e-9);

}

TEST(ComprehensiveTest, FloatingPointComparisonsAndAssignments) {

  std::string source =
      "double compare(double a, double b) {"
      "  double sum = a + b;"
      "  double diff = a - b;"
      "  double prod = a * b;"
      "  double ratio = a / b;"
      "  if (sum > prod) { return sum; }"
      "  if (diff < 0.0) { return ratio; }"
      "  return prod;"
      "}"
      "int32 truncate(double v) { int32 x = v; return x; }"
      "double widen(int32 v) { double x = v; return x + 0.5; }";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "float_compare.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  // Comparison paths (sum > prod)
  success = manager.executeProcedure("compare", {1.0, 1.5}, result, errorMsg);
  EXPECT_TRUE(success) << errorMsg;
  EXPECT_NEAR(std::get<double>(result), 2.5, 1e-9);

  // diff < 0 -> returns ratio
  success = manager.executeProcedure("compare", {2.0, 4.0}, result, errorMsg);
  EXPECT_TRUE(success) << errorMsg;
  EXPECT_NEAR(std::get<double>(result), 0.5, 1e-9);

  // Truncation when binding to int
  success = manager.executeProcedure("truncate", {3.9}, result, errorMsg);
  EXPECT_TRUE(success) << errorMsg;
  EXPECT_EQ(std::get<int32_t>(result), 3);

  // Widening int to double via var init
  success = manager.executeProcedure("widen", {static_cast<int32_t>(2)}, result, errorMsg);
  EXPECT_TRUE(success) << errorMsg;
  EXPECT_NEAR(std::get<double>(result), 2.5, 1e-9);
}

TEST(ComprehensiveTest, FloatingPointModuloUsesFmod) {

  std::string source = "double fmod_(double a, double b) { return a % b; }";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "float_mod.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  success = manager.executeProcedure("fmod_", {2.5, 1.0}, result, errorMsg);
  ASSERT_TRUE(success) << errorMsg;
  EXPECT_DOUBLE_EQ(std::get<double>(result), 0.5);
}

TEST(ComprehensiveTest, FloatingPointPrecisionAndDivisionByZero) {

  std::string source =
      "double accumulate() {"
      "  double x = 0.0;"
      "  int32 i = 0;"
      "  for (i = 0; i < 10; i += 1) { x = x + 0.1; }"
      "  return x;"
      "}"
      "double cancel() {"
      "  double x = 1.0;"
      "  x = x + 0.000001;"
      "  x = x - 0.000001;"
      "  return x;"
      "}"
      "double reciprocal(double v) { return 1.0 / v; }";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "float_prec.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  // Accumulated rounding should stay close to 1.0
  success = manager.executeProcedure("accumulate", {}, result, errorMsg);
  EXPECT_TRUE(success) << errorMsg;
  EXPECT_NEAR(std::get<double>(result), 1.0, 1e-9);

  // Cancellation should keep value near the original
  success = manager.executeProcedure("cancel", {}, result, errorMsg);
  EXPECT_TRUE(success) << errorMsg;
  EXPECT_NEAR(std::get<double>(result), 1.0, 1e-12);

  // Division by zero should be rejected (no inf/NaN produced)
  success = manager.executeProcedure("reciprocal", {0.0}, result, errorMsg);
  EXPECT_FALSE(success);
  EXPECT_NE(errorMsg.find("Division by zero"), std::string::npos);
}

TEST(ComprehensiveTest, TypeConversions) {

  std::string source = "int32 convert(int8 small) {"
                       "  int32 big = small;"
                       "  return big * 1000;"
                       "}"
                       "uint64 convertToLarge(int32 medium) {"
                       "  uint64 large = medium;"
                       "  return large * 1000;"
                       "}";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "convert.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  std::vector<Value> args1 = {static_cast<int8_t>(42)};
  success = manager.executeProcedure("convert", args1, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<int32_t>(result) == 42000);

  std::vector<Value> args2 = {static_cast<int32_t>(1000000)};
  success = manager.executeProcedure("convertToLarge", args2, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<uint64_t>(result) == 1000000000ULL);

}

TEST(ComprehensiveTest, IntegerEdgeBoundaries) {

  std::string source = "int32 echo32(int32 v) { return v + 0; }"
                       "int64 echo64(int64 v) { return v - 0; }"
                       "uint32 echou32(uint32 v) { return v; }"
                       "uint64 echou64(uint64 v) { return v + 0; }";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "edge_bounds.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  // int32 min/max
  std::vector<Value> args32Min = {std::numeric_limits<int32_t>::min()};
  success = manager.executeProcedure("echo32", args32Min, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_EQ(std::get<int32_t>(result), std::numeric_limits<int32_t>::min());

  std::vector<Value> args32Max = {std::numeric_limits<int32_t>::max()};
  success = manager.executeProcedure("echo32", args32Max, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_EQ(std::get<int32_t>(result), std::numeric_limits<int32_t>::max());

  // int64 min/max
  std::vector<Value> args64Min = {std::numeric_limits<int64_t>::min()};
  success = manager.executeProcedure("echo64", args64Min, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_EQ(std::get<int64_t>(result), std::numeric_limits<int64_t>::min());

  std::vector<Value> args64Max = {std::numeric_limits<int64_t>::max()};
  success = manager.executeProcedure("echo64", args64Max, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_EQ(std::get<int64_t>(result), std::numeric_limits<int64_t>::max());

  // uint32 max
  std::vector<Value> argsu32Max = {std::numeric_limits<uint32_t>::max()};
  success = manager.executeProcedure("echou32", argsu32Max, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_EQ(std::get<uint32_t>(result), std::numeric_limits<uint32_t>::max());

  // uint64 max
  std::vector<Value> argsu64Max = {std::numeric_limits<uint64_t>::max()};
  success = manager.executeProcedure("echou64", argsu64Max, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_EQ(std::get<uint64_t>(result), std::numeric_limits<uint64_t>::max());

}

// ========== OPERATOR TESTS ==========

TEST(ComprehensiveTest, ModuloOperator) {

  std::string source = "int32 testMod(int32 a, int32 b) {"
                       "  return a % b;"
                       "}"
                       "int32 evenOdd(int32 n) {"
                       "  return n % 2;"
                       "}";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "modulo.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  // Basic modulo
  std::vector<Value> args1 = {static_cast<int32_t>(17),
                              static_cast<int32_t>(5)};
  success = manager.executeProcedure("testMod", args1, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<int32_t>(result) == 2);

  // Even/odd test
  std::vector<Value> args2 = {static_cast<int32_t>(7)};
  success = manager.executeProcedure("evenOdd", args2, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<int32_t>(result) == 1);

  std::vector<Value> args3 = {static_cast<int32_t>(8)};
  success = manager.executeProcedure("evenOdd", args3, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<int32_t>(result) == 0);

}

TEST(ComprehensiveTest, AllCompoundAssignments) {

  std::string source = "int32 testPlusAssign(int32 x) {"
                       "  int32 val = 10;"
                       "  val += x;"
                       "  return val;"
                       "}"
                       "int32 testMinusAssign(int32 x) {"
                       "  int32 val = 100;"
                       "  val -= x;"
                       "  return val;"
                       "}"
                       "int32 testMultAssign(int32 x) {"
                       "  int32 val = 5;"
                       "  val *= x;"
                       "  return val;"
                       "}"
                       "int32 testDivAssign(int32 x) {"
                       "  int32 val = 100;"
                       "  val /= x;"
                       "  return val;"
                       "}";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "compound.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  // Test +=
  std::vector<Value> args1 = {static_cast<int32_t>(15)};
  success = manager.executeProcedure("testPlusAssign", args1, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<int32_t>(result) == 25);

  // Test -=
  std::vector<Value> args2 = {static_cast<int32_t>(30)};
  success =
      manager.executeProcedure("testMinusAssign", args2, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<int32_t>(result) == 70);

  // Test *=
  std::vector<Value> args3 = {static_cast<int32_t>(7)};
  success = manager.executeProcedure("testMultAssign", args3, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<int32_t>(result) == 35);

  // Test /=
  std::vector<Value> args4 = {static_cast<int32_t>(4)};
  success = manager.executeProcedure("testDivAssign", args4, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<int32_t>(result) == 25);

}

TEST(ComprehensiveTest, AllComparisonOperators) {

  std::string source =
      "bool testEqual(int32 a, int32 b) { return a == b; }"
      "bool testNotEqual(int32 a, int32 b) { return a != b; }"
      "bool testLessThan(int32 a, int32 b) { return a < b; }"
      "bool testGreaterThan(int32 a, int32 b) { return a > b; }"
      "bool testLessEqual(int32 a, int32 b) { return a <= b; }"
      "bool testGreaterEqual(int32 a, int32 b) { return a >= b; }";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "compare.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  // Test ==
  std::vector<Value> eq1 = {static_cast<int32_t>(5), static_cast<int32_t>(5)};
  success = manager.executeProcedure("testEqual", eq1, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == true);

  std::vector<Value> eq2 = {static_cast<int32_t>(5), static_cast<int32_t>(6)};
  success = manager.executeProcedure("testEqual", eq2, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == false);

  // Test !=
  success = manager.executeProcedure("testNotEqual", eq1, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == false);

  success = manager.executeProcedure("testNotEqual", eq2, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == true);

  // Test <
  std::vector<Value> lt1 = {static_cast<int32_t>(3), static_cast<int32_t>(5)};
  success = manager.executeProcedure("testLessThan", lt1, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == true);

  std::vector<Value> lt2 = {static_cast<int32_t>(5), static_cast<int32_t>(3)};
  success = manager.executeProcedure("testLessThan", lt2, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == false);

  // Test >
  success = manager.executeProcedure("testGreaterThan", lt1, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == false);

  success = manager.executeProcedure("testGreaterThan", lt2, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == true);

  // Test <=
  success = manager.executeProcedure("testLessEqual", eq1, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == true);

  success = manager.executeProcedure("testLessEqual", lt1, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == true);

  success = manager.executeProcedure("testLessEqual", lt2, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == false);

  // Test >=
  success = manager.executeProcedure("testGreaterEqual", eq1, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == true);

  success = manager.executeProcedure("testGreaterEqual", lt2, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == true);

  success = manager.executeProcedure("testGreaterEqual", lt1, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == false);

}

TEST(ComprehensiveTest, LogicalOperators) {

  std::string source = "bool testAnd(bool a, bool b) { return a && b; }"
                       "bool testOr(bool a, bool b) { return a || b; }"
                       "bool testNot(bool a) { return !a; }"
                       "bool testComplex(bool a, bool b, bool c) {"
                       "  return (a || b) && !c;"
                       "}";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "logical.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  // Test AND
  std::vector<Value> tt = {true, true};
  success = manager.executeProcedure("testAnd", tt, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == true);

  std::vector<Value> tf = {true, false};
  success = manager.executeProcedure("testAnd", tf, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == false);

  // Test OR
  success = manager.executeProcedure("testOr", tf, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == true);

  std::vector<Value> ff = {false, false};
  success = manager.executeProcedure("testOr", ff, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == false);

  // Test NOT
  std::vector<Value> t = {true};
  success = manager.executeProcedure("testNot", t, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == false);

  std::vector<Value> f = {false};
  success = manager.executeProcedure("testNot", f, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == true);

  // Test complex
  std::vector<Value> complex = {true, false, false};
  success = manager.executeProcedure("testComplex", complex, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == true);

}

TEST(ComprehensiveTest, LogicalEvaluationWithSideEffects) {

  std::string source = "bool falseAndExplode() { return false && explode(); }"
                       "bool trueOrExplode() { return true || explode(); }";

  ScriptManager manager;
  int callCount = 0;
  manager.registerExternalFunction(
      "explode", [&callCount](const std::vector<Value> & /*args*/) -> Value {
        ++callCount;
        return false;
      });

  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "short_circuit.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  // false && explode()
  std::vector<Value> noArgs;
  success = manager.executeProcedure("falseAndExplode", noArgs, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_FALSE(std::get<bool>(result));

  // true || explode()
  success = manager.executeProcedure("trueOrExplode", noArgs, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<bool>(result));

  // Short-circuiting should skip explode() in both cases
  EXPECT_EQ(callCount, 0);

}

TEST(ComprehensiveTest, LogicalShortCircuiting) {

  std::string source =
      "bool testAnd() { return leftFalse() && right(); }"
      "bool testOr()  { return leftTrue()  || right(); }";

  int sideEffect = 0;
  ScriptManager manager;

  manager.registerExternalFunction(
      "leftFalse", [&sideEffect](const std::vector<Value> & /*args*/) -> Value {
        sideEffect += 1;
        return false;
      });

  manager.registerExternalFunction(
      "leftTrue", [&sideEffect](const std::vector<Value> & /*args*/) -> Value {
        sideEffect += 1;
        return true;
      });

  manager.registerExternalFunction(
      "right", [&sideEffect](const std::vector<Value> & /*args*/) -> Value {
        sideEffect += 10;
        return true;
      });

  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "logic_short.script", errors));

  Value result;
  std::string errorMsg;

  sideEffect = 0;
  ASSERT_TRUE(manager.executeProcedure("testAnd", {}, result, errorMsg));
  EXPECT_FALSE(std::get<bool>(result));
  EXPECT_EQ(sideEffect, 1); // right() not executed

  sideEffect = 0;
  ASSERT_TRUE(manager.executeProcedure("testOr", {}, result, errorMsg));
  EXPECT_TRUE(std::get<bool>(result));
  EXPECT_EQ(sideEffect, 1); // right() not executed
}

TEST(ComprehensiveTest, UnaryMinus) {

  std::string source = "int32 negate(int32 x) {"
                       "  return -x;"
                       "}"
                       "int32 doubleNegate(int32 x) {"
                       "  return -(-x);"
                       "}";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "unary.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  std::vector<Value> args1 = {static_cast<int32_t>(42)};
  success = manager.executeProcedure("negate", args1, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<int32_t>(result) == -42);

  success = manager.executeProcedure("doubleNegate", args1, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<int32_t>(result) == 42);

}

// ========== ERROR HANDLING TESTS ==========

TEST(ComprehensiveTest, DivisionByZero) {

  std::string source = "int32 divide(int32 a, int32 b) {"
                       "  return a / b;"
                       "}";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "divzero.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  std::vector<Value> args = {static_cast<int32_t>(10), static_cast<int32_t>(0)};
  success = manager.executeProcedure("divide", args, result, errorMsg);
  EXPECT_TRUE(!success);
  EXPECT_TRUE(errorMsg.find("ivision by zero") != std::string::npos ||
         errorMsg.find("ivide by zero") != std::string::npos);

  std::cout << "  ✓ Division by zero error detected" << std::endl;
}

TEST(ComprehensiveTest, ModuloByZero) {

  std::string source = "int32 modulo(int32 a, int32 b) {"
                       "  return a % b;"
                       "}";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "modzero.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  std::vector<Value> args = {static_cast<int32_t>(10), static_cast<int32_t>(0)};
  success = manager.executeProcedure("modulo", args, result, errorMsg);
  EXPECT_TRUE(!success);
  EXPECT_TRUE(errorMsg.find("odulo by zero") != std::string::npos);

  std::cout << "  ✓ Modulo by zero error detected" << std::endl;
}

TEST(ComprehensiveTest, UndefinedVariable) {

  std::string source = "int32 test() {"
                       "  return undefinedVar;"
                       "}";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "undef.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  std::vector<Value> args;
  success = manager.executeProcedure("test", args, result, errorMsg);
  EXPECT_TRUE(!success);
  EXPECT_TRUE(errorMsg.find("ndefined") != std::string::npos);

  std::cout << "  ✓ Undefined variable error detected" << std::endl;
}

TEST(ComprehensiveTest, WrongArgumentCount) {

  std::string source = "int32 add(int32 a, int32 b) {"
                       "  return a + b;"
                       "}";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "args.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  // Too few arguments
  std::vector<Value> args1 = {static_cast<int32_t>(10)};
  success = manager.executeProcedure("add", args1, result, errorMsg);
  EXPECT_TRUE(!success);
  EXPECT_TRUE(errorMsg.find("expects") != std::string::npos);

  // Too many arguments
  std::vector<Value> args2 = {static_cast<int32_t>(10),
                              static_cast<int32_t>(20),
                              static_cast<int32_t>(30)};
  success = manager.executeProcedure("add", args2, result, errorMsg);
  EXPECT_TRUE(!success);
  EXPECT_TRUE(errorMsg.find("expects") != std::string::npos);

  std::cout << "  ✓ Wrong argument count error detected" << std::endl;
}

TEST(ComprehensiveTest, NonExistentProcedure) {

  ScriptManager manager;
  std::vector<CompilationError> errors;
  std::string source = "int32 test() { return 0; }";
  bool success = manager.loadScriptSource(source, "test.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  std::vector<Value> args;
  success = manager.executeProcedure("nonExistent", args, result, errorMsg);
  EXPECT_TRUE(!success);
  EXPECT_TRUE(errorMsg.find("not found") != std::string::npos ||
         errorMsg.find("Procedure not found") != std::string::npos);

  std::cout << "  ✓ Non-existent procedure error detected" << std::endl;
}

// ========== EDGE CASE TESTS ==========

TEST(ComprehensiveTest, EmptyStringOperations) {

  std::string source = "string concat(string a, string b) {"
                       "  return a + b;"
                       "}"
                       "bool isEmpty(string s) {"
                       "  return s == \"\";"
                       "}";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "emptystr.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  // Concatenate with empty
  std::vector<Value> args1 = {std::string(""), std::string("test")};
  success = manager.executeProcedure("concat", args1, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<std::string>(result) == "test");

  // Check empty
  std::vector<Value> args2 = {std::string("")};
  success = manager.executeProcedure("isEmpty", args2, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<bool>(result) == true);

  std::vector<Value> args3 = {std::string("x")};
  success = manager.executeProcedure("isEmpty", args3, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<bool>(result) == false);

}

TEST(ComprehensiveTest, WhitespaceAndWindowsNewlines) {

  std::string source = "int32 padded()\r\n"
                       "{\r\n"
                       "\tint32 value = 2;\r\n"
                       "\treturn value + 3;\r\n"
                       "}\r\n";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "whitespace.script", errors);
  EXPECT_TRUE(success);

  std::vector<Value> args;
  Value result;
  std::string errorMsg;

  success = manager.executeProcedure("padded", args, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_EQ(std::get<int32_t>(result), 5);

}

TEST(ComprehensiveTest, NestedScopes) {

  std::string source = "int32 testScope() {"
                       "  int32 x = 10;"
                       "  if (true) {"
                       "    int32 x = 20;"
                       "    if (true) {"
                       "      int32 x = 30;"
                       "      return x;"
                       "    }"
                       "  }"
                       "  return x;"
                       "}";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "scope.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  std::vector<Value> args;
  success = manager.executeProcedure("testScope", args, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<int32_t>(result) == 30);

}

TEST(ComprehensiveTest, WhileLoop) {

  std::string source = "int32 countdown(int32 n) {"
                       "  int32 count = 0;"
                       "  while (n > 0) {"
                       "    count += 1;"
                       "    n -= 1;"
                       "  }"
                       "  return count;"
                       "}";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "while.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  std::vector<Value> args = {static_cast<int32_t>(5)};
  success = manager.executeProcedure("countdown", args, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<int32_t>(result) == 5);

}

TEST(ComprehensiveTest, ForLoopVariations) {

  std::string source = "int32 sumRange(int32 start, int32 end) {"
                       "  int32 sum = 0;"
                       "  for (int32 i = start; i <= end; i += 1) {"
                       "    sum += i;"
                       "  }"
                       "  return sum;"
                       "}"
                       "int32 countDown(int32 n) {"
                       "  int32 count = 0;"
                       "  for (int32 i = n; i > 0; i -= 1) {"
                       "    count += 1;"
                       "  }"
                       "  return count;"
                       "}";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "forloop.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  // Sum 1 to 10
  std::vector<Value> args1 = {static_cast<int32_t>(1),
                              static_cast<int32_t>(10)};
  success = manager.executeProcedure("sumRange", args1, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<int32_t>(result) == 55);

  // Countdown from 10
  std::vector<Value> args2 = {static_cast<int32_t>(10)};
  success = manager.executeProcedure("countDown", args2, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<int32_t>(result) == 10);

}

TEST(ComprehensiveTest, BooleanExpressions) {

  std::string source = "bool inRange(int32 x, int32 min, int32 max) {"
                       "  return x >= min && x <= max;"
                       "}"
                       "bool isValid(int32 x) {"
                       "  return x > 0 || x < -10;"
                       "}";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "bool.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  // In range
  std::vector<Value> args1 = {static_cast<int32_t>(5), static_cast<int32_t>(1),
                              static_cast<int32_t>(10)};
  success = manager.executeProcedure("inRange", args1, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<bool>(result) == true);

  // Out of range
  std::vector<Value> args2 = {static_cast<int32_t>(15), static_cast<int32_t>(1),
                              static_cast<int32_t>(10)};
  success = manager.executeProcedure("inRange", args2, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<bool>(result) == false);

  // Valid (positive)
  std::vector<Value> args3 = {static_cast<int32_t>(5)};
  success = manager.executeProcedure("isValid", args3, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<bool>(result) == true);

  // Invalid (in middle range)
  std::vector<Value> args4 = {static_cast<int32_t>(-5)};
  success = manager.executeProcedure("isValid", args4, result, errorMsg);
  EXPECT_TRUE(success);
  EXPECT_TRUE(std::get<bool>(result) == false);

}

TEST(ComprehensiveTest, VoidProcedures) {

  std::string source = "void doNothing() {}"
                       "void earlyReturn(int32 x) {"
                       "  if (x > 0) {"
                       "    return;"
                       "  }"
                       "}";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "void.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  std::vector<Value> args1;
  success = manager.executeProcedure("doNothing", args1, result, errorMsg);
  EXPECT_TRUE(success);

  std::vector<Value> args2 = {static_cast<int32_t>(10)};
  success = manager.executeProcedure("earlyReturn", args2, result, errorMsg);
  EXPECT_TRUE(success);

}

TEST(ComprehensiveTest, StringComparisons) {

  std::string source = "bool strEqual(string a, string b) { return a == b; }"
                       "bool strNotEqual(string a, string b) { return a != b; }"
                       "bool strLess(string a, string b) { return a < b; }"
                       "bool strGreater(string a, string b) { return a > b; }";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "strcmp.script", errors);
  EXPECT_TRUE(success);

  std::string errorMsg;
  Value result;

  // Equal
  std::vector<Value> args1 = {std::string("hello"), std::string("hello")};
  success = manager.executeProcedure("strEqual", args1, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == true);

  // Not equal
  std::vector<Value> args2 = {std::string("hello"), std::string("world")};
  success = manager.executeProcedure("strNotEqual", args2, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == true);

  // Lexicographic less
  std::vector<Value> args3 = {std::string("apple"), std::string("banana")};
  success = manager.executeProcedure("strLess", args3, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == true);

  // Lexicographic greater
  std::vector<Value> args4 = {std::string("zebra"), std::string("apple")};
  success = manager.executeProcedure("strGreater", args4, result, errorMsg);
  EXPECT_TRUE(success && std::get<bool>(result) == true);

}

