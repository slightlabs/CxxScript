#include "ScriptManager.h"
#include <gtest/gtest.h>

using namespace Script;

TEST(CharTest, LiteralAndDeclaration) {
  std::string source = R"(
        char letter() {
            char c = 'A';
            return c;
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "char_literal.script", errors))
      << (errors.empty() ? "" : errors[0].toString());

  Value result;
  std::string errorMsg;
  ASSERT_TRUE(manager.executeProcedure("letter", {}, result, errorMsg))
      << errorMsg;
  ASSERT_TRUE(std::holds_alternative<char>(result));
  EXPECT_EQ(std::get<char>(result), 'A');
}

TEST(CharTest, EscapeSequences) {
  std::string source = R"(
        char newline() { return '\n'; }
        char tab() { return '\t'; }
        char quote() { return '\''; }
        char backslash() { return '\\'; }
        char nul() { return '\0'; }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "char_escapes.script", errors))
      << (errors.empty() ? "" : errors[0].toString());

  Value result;
  std::string errorMsg;

  ASSERT_TRUE(manager.executeProcedure("newline", {}, result, errorMsg));
  EXPECT_EQ(std::get<char>(result), '\n');

  ASSERT_TRUE(manager.executeProcedure("tab", {}, result, errorMsg));
  EXPECT_EQ(std::get<char>(result), '\t');

  ASSERT_TRUE(manager.executeProcedure("quote", {}, result, errorMsg));
  EXPECT_EQ(std::get<char>(result), '\'');

  ASSERT_TRUE(manager.executeProcedure("backslash", {}, result, errorMsg));
  EXPECT_EQ(std::get<char>(result), '\\');

  ASSERT_TRUE(manager.executeProcedure("nul", {}, result, errorMsg));
  EXPECT_EQ(std::get<char>(result), '\0');
}

TEST(CharTest, StringConcatenationUsesCharacterNotCode) {
  std::string source = R"(
        string gradeMessage(char grade) {
            return "Grade: " + grade;
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "char_concat.script", errors));

  Value result;
  std::string errorMsg;
  ASSERT_TRUE(manager.executeProcedure("gradeMessage", {static_cast<char>('A')},
                                       result, errorMsg))
      << errorMsg;
  EXPECT_EQ(std::get<std::string>(result), "Grade: A");
}

TEST(CharTest, ComparisonsByCodePoint) {
  std::string source = R"(
        bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }
        bool equalsB(char c) { return c == 'b'; }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "char_compare.script", errors));

  Value result;
  std::string errorMsg;

  ASSERT_TRUE(manager.executeProcedure("isUpper", {static_cast<char>('Q')},
                                       result, errorMsg))
      << errorMsg;
  EXPECT_TRUE(std::get<bool>(result));

  ASSERT_TRUE(manager.executeProcedure("isUpper", {static_cast<char>('q')},
                                       result, errorMsg))
      << errorMsg;
  EXPECT_FALSE(std::get<bool>(result));

  ASSERT_TRUE(manager.executeProcedure("equalsB", {static_cast<char>('b')},
                                       result, errorMsg))
      << errorMsg;
  EXPECT_TRUE(std::get<bool>(result));
}

TEST(CharTest, ArithmeticPromotesToIntWhenMixed) {
  std::string source = R"(
        int32 nextCode(char c) {
            return c + 1;
        }
        char nextChar(char c) {
            return c + 1;
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "char_arith.script", errors));

  Value result;
  std::string errorMsg;

  ASSERT_TRUE(manager.executeProcedure("nextCode", {static_cast<char>('a')},
                                       result, errorMsg))
      << errorMsg;
  ASSERT_TRUE(std::holds_alternative<int32_t>(result));
  EXPECT_EQ(std::get<int32_t>(result), 98);

  ASSERT_TRUE(manager.executeProcedure("nextChar", {static_cast<char>('a')},
                                       result, errorMsg))
      << errorMsg;
  ASSERT_TRUE(std::holds_alternative<char>(result));
  EXPECT_EQ(std::get<char>(result), 'b');
}

TEST(CharTest, ArrayOfChars) {
  std::string source = R"(
        int32 countVowels(char[] letters) {
            int32 count = 0;
            for (int32 i = 0; i < len(letters); i += 1) {
                char c = letters[i];
                if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u') {
                    count += 1;
                }
            }
            return count;
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "char_array.script", errors));

  ArrayPtr arr = ValueHelper::createArray(
      TypeInfo(DataType::CHAR),
      {static_cast<char>('h'), static_cast<char>('e'), static_cast<char>('l'),
       static_cast<char>('l'), static_cast<char>('o')});

  Value result;
  std::string errorMsg;
  ASSERT_TRUE(manager.executeProcedure("countVowels", {arr}, result, errorMsg))
      << errorMsg;
  // "hello" has 2 vowels: 'e' and 'o'
  EXPECT_EQ(std::get<int32_t>(result), 2);
}

TEST(CharTest, DefaultInitialization) {
  std::string source = R"(
        char uninitialized() {
            char c;
            return c;
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "char_default.script", errors));

  Value result;
  std::string errorMsg;
  ASSERT_TRUE(manager.executeProcedure("uninitialized", {}, result, errorMsg))
      << errorMsg;
  ASSERT_TRUE(std::holds_alternative<char>(result));
  EXPECT_EQ(std::get<char>(result), '\0');
}
