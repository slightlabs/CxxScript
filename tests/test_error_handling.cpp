#include "ScriptManager.h"
#include <gtest/gtest.h>

using namespace Script;

TEST(ErrorHandlingTest, UnexpectedCharacter) {

  // Script with colon at end of line
  std::string source = "int32 test(int32 x): {"
                       "  return x + 1;"
                       "}";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "error_test.script", errors);

  std::cout << "  Compilation success: " << (success ? "true" : "false")
            << std::endl;
  std::cout << "  Number of errors: " << errors.size() << std::endl;

  if (!errors.empty()) {
    for (const auto &error : errors) {
      std::cout << "  Error at line " << error.line << ", column "
                << error.column;
      if (!error.procedureName.empty()) {
        std::cout << " in procedure '" << error.procedureName << "'";
      }
      std::cout << ": " << error.message << std::endl;
    }
  }

  EXPECT_TRUE(!success);
  EXPECT_TRUE(!errors.empty());

}

TEST(ErrorHandlingTest, UnexpectedColonInStatement) {

  std::string source = "int32 calculate(int32 a, int32 b) {"
                       "  int32 result = a + b:;"
                       "  return result;"
                       "}";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "error_test2.script", errors);

  std::cout << "  Compilation success: " << (success ? "true" : "false")
            << std::endl;
  std::cout << "  Number of errors: " << errors.size() << std::endl;

  if (!errors.empty()) {
    for (const auto &error : errors) {
      std::cout << "  Error at line " << error.line << ", column "
                << error.column;
      if (!error.procedureName.empty()) {
        std::cout << " in procedure '" << error.procedureName << "'";
      }
      std::cout << ": " << error.message << std::endl;
    }
  }

  EXPECT_TRUE(!success);
  EXPECT_TRUE(!errors.empty());

}

TEST(ErrorHandlingTest, MultipleErrors) {

  std::string source = "int32 broken(int32 x): {"
                       "  int32 y = x + 5:;"
                       "  return y:;"
                       "}";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "multi_error.script", errors);

  std::cout << "  Compilation success: " << (success ? "true" : "false")
            << std::endl;
  std::cout << "  Number of errors: " << errors.size() << std::endl;

  if (!errors.empty()) {
    for (size_t i = 0; i < errors.size(); ++i) {
      const auto &error = errors[i];
      std::cout << "  Error " << (i + 1) << " at line " << error.line
                << ", column " << error.column;
      if (!error.procedureName.empty()) {
        std::cout << " in procedure '" << error.procedureName << "'";
      }
      std::cout << ": " << error.message << std::endl;
    }
  }

  EXPECT_TRUE(!success);
  EXPECT_TRUE(!errors.empty());

}

TEST(ErrorHandlingTest, ValidCode) {

  std::string source = "int32 add(int32 a, int32 b) {"
                       "  return a + b;"
                       "}";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.loadScriptSource(source, "valid.script", errors);

  std::cout << "  Compilation success: " << (success ? "true" : "false")
            << std::endl;
  std::cout << "  Number of errors: " << errors.size() << std::endl;

  EXPECT_TRUE(success);
  EXPECT_TRUE(errors.empty());

}

TEST(ErrorHandlingTest, MissingFileReportsError) {

  ScriptManager manager;
  std::vector<CompilationError> errors;

  bool success =
      manager.loadScriptFile("tests/does_not_exist_123456.script", errors);

  EXPECT_FALSE(success);
  ASSERT_EQ(errors.size(), 1u);
  EXPECT_EQ(errors[0].line, 0);
  EXPECT_EQ(errors[0].column, 0);
  EXPECT_NE(errors[0].message.find("Failed to open file"), std::string::npos);
  EXPECT_EQ(errors[0].filename, "tests/does_not_exist_123456.script");

}

TEST(ErrorHandlingTest, SyntaxErrorReportsPosition) {

  std::string source = "int32 broken(int32 x) {\n"
                       "  return x + @;\n"
                       "}\n";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.checkScriptSource(source, "diag.script", errors);

  EXPECT_FALSE(success);
  ASSERT_FALSE(errors.empty());
  EXPECT_GT(errors[0].line, 0);
  EXPECT_GT(errors[0].column, 0);
  EXPECT_EQ(errors[0].filename, "diag.script");
  EXPECT_FALSE(errors[0].toString().empty());

}

TEST(ErrorHandlingTest, TypeMismatchInVariableDeclarationReportsCompileError) {
  std::string source = "int32 bad() {\n"
                       "  int32 value = \"text\";\n"
                       "  return value;\n"
                       "}\n";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.checkScriptSource(source, "type_mismatch.script", errors);

  EXPECT_FALSE(success);
  ASSERT_FALSE(errors.empty());
  EXPECT_NE(errors[0].message.find("Type mismatch"), std::string::npos);
}

TEST(ErrorHandlingTest, TypeMismatchInReturnReportsCompileError) {
  std::string source = "int32 badReturn() {\n"
                       "  return \"abc\";\n"
                       "}\n";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  bool success = manager.checkScriptSource(source, "return_mismatch.script", errors);

  EXPECT_FALSE(success);
  ASSERT_FALSE(errors.empty());
  EXPECT_NE(errors[0].message.find("Type mismatch"), std::string::npos);
}

TEST(ErrorHandlingTest, MaxCallDepthStopsRunawayRecursion) {
  std::string source = "int32 recurse(int32 x) { return recurse(x + 1); }";

  ScriptManager manager;
  manager.setExecutionLimits(32, 0);

  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "recurse.script", errors));
  ASSERT_TRUE(errors.empty());

  Value result;
  std::string errorMsg;
  bool success =
      manager.executeProcedure("recurse", {static_cast<int32_t>(0)}, result, errorMsg);

  EXPECT_FALSE(success);
  EXPECT_NE(errorMsg.find("Maximum call depth exceeded"), std::string::npos);
  EXPECT_NE(errorMsg.find("recurse.script"), std::string::npos);
}

TEST(ErrorHandlingTest, MaxStepBudgetStopsInfiniteLoop) {
  std::string source =
      "int32 spin() {"
      "  int32 i = 0;"
      "  while (true) { i += 1; }"
      "  return i;"
      "}";

  ScriptManager manager;
  manager.setExecutionLimits(0, 200);

  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "spin.script", errors));
  ASSERT_TRUE(errors.empty());

  Value result;
  std::string errorMsg;
  bool success = manager.executeProcedure("spin", {}, result, errorMsg);

  EXPECT_FALSE(success);
  EXPECT_NE(errorMsg.find("Maximum execution steps exceeded"), std::string::npos);
  EXPECT_NE(errorMsg.find("spin.script"), std::string::npos);
}
