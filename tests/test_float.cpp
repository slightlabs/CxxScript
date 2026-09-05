#include "ScriptManager.h"
#include <gtest/gtest.h>

using namespace Script;

TEST(FloatTest, DeclarationAndArithmetic) {
  std::string source = R"(
        float compute(float a, float b) {
            float sum = a + b;
            float diff = a - b;
            float prod = a * b;
            float quot = a / b;
            return sum + diff + prod + quot;
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "float_arith.script", errors)) << (errors.empty() ? "" : errors[0].toString());

  Value result;
  std::string errorMsg;
  ASSERT_TRUE(manager.executeProcedure(
      "compute", {static_cast<float>(6.0f), static_cast<float>(2.0f)}, result,
      errorMsg))
      << errorMsg;

  ASSERT_TRUE(std::holds_alternative<float>(result));
  // sum=8, diff=4, prod=12, quot=3 => 27
  EXPECT_FLOAT_EQ(std::get<float>(result), 27.0f);
}

TEST(FloatTest, MixedWithIntPromotesToFloat) {
  std::string source = R"(
        float mixed(float a, int32 b) {
            return a + b;
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "float_mixed.script", errors));

  Value result;
  std::string errorMsg;
  ASSERT_TRUE(manager.executeProcedure(
      "mixed", {static_cast<float>(1.5f), static_cast<int32_t>(2)}, result,
      errorMsg))
      << errorMsg;

  ASSERT_TRUE(std::holds_alternative<float>(result));
  EXPECT_FLOAT_EQ(std::get<float>(result), 3.5f);
}

TEST(FloatTest, MixedWithDoublePromotesToDouble) {
  std::string source = R"(
        double mixed(float a, double b) {
            return a + b;
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "float_double.script", errors));

  Value result;
  std::string errorMsg;
  ASSERT_TRUE(manager.executeProcedure(
      "mixed", {static_cast<float>(1.5f), static_cast<double>(2.25)}, result,
      errorMsg))
      << errorMsg;

  ASSERT_TRUE(std::holds_alternative<double>(result));
  EXPECT_DOUBLE_EQ(std::get<double>(result), 3.75);
}

TEST(FloatTest, ComparisonsAndConversions) {
  std::string source = R"(
        bool isGreater(float a, float b) { return a > b; }
        int32 toInt(float a) { return a; }
        float fromInt(int32 a) { return a; }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "float_convert.script", errors));

  Value result;
  std::string errorMsg;

  ASSERT_TRUE(manager.executeProcedure(
      "isGreater", {static_cast<float>(3.5f), static_cast<float>(2.0f)}, result,
      errorMsg))
      << errorMsg;
  EXPECT_TRUE(std::get<bool>(result));

  ASSERT_TRUE(manager.executeProcedure("toInt", {static_cast<float>(9.7f)},
                                       result, errorMsg))
      << errorMsg;
  EXPECT_EQ(std::get<int32_t>(result), 9);

  ASSERT_TRUE(manager.executeProcedure("fromInt", {static_cast<int32_t>(5)},
                                       result, errorMsg))
      << errorMsg;
  ASSERT_TRUE(std::holds_alternative<float>(result));
  EXPECT_FLOAT_EQ(std::get<float>(result), 5.0f);
}

TEST(FloatTest, ModuloIsRejected) {
  std::string source = R"(
        float bad(float a, float b) { return a % b; }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "float_modulo.script", errors));

  Value result;
  std::string errorMsg;
  bool ok = manager.executeProcedure(
      "bad", {static_cast<float>(5.0f), static_cast<float>(2.0f)}, result,
      errorMsg);
  EXPECT_FALSE(ok);
  EXPECT_NE(errorMsg.find("Modulo not supported for floating point"),
            std::string::npos);
}

TEST(FloatTest, ArrayOfFloats) {
  std::string source = R"(
        float average(float[] values) {
            float sum = 0.0;
            for (int32 i = 0; i < len(values); i += 1) {
                sum += values[i];
            }
            return sum / len(values);
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "float_array.script", errors));

  ArrayPtr arr = ValueHelper::createArray(
      TypeInfo(DataType::FLOAT),
      {static_cast<float>(1.0f), static_cast<float>(2.0f), static_cast<float>(3.0f)});

  Value result;
  std::string errorMsg;
  ASSERT_TRUE(manager.executeProcedure("average", {arr}, result, errorMsg))
      << errorMsg;
  ASSERT_TRUE(std::holds_alternative<float>(result));
  EXPECT_FLOAT_EQ(std::get<float>(result), 2.0f);
}
