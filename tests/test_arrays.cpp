#include "ScriptManager.h"
#include <gtest/gtest.h>

using namespace Script;

TEST(ArrayTest, LiteralIndexAndReturn) {
  std::string source = R"(
        int32 getValue() {
            int32[] data = [1, 2, 3];
            return data[1];
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "array.script", errors));
  ASSERT_TRUE(errors.empty());

  Value result;
  std::string errorMsg;
  std::vector<Value> args;
  ASSERT_TRUE(manager.executeProcedure("getValue", args, result, errorMsg)) << errorMsg;
  EXPECT_EQ(std::get<int32_t>(result), 2);
}

TEST(ArrayTest, PushAndLen) {
  std::string source = R"(
        int32 appendAndSize() {
            int32[] data = [10, 20];
            push(data, 30);
            return len(data);
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "array_push.script", errors));
  ASSERT_TRUE(errors.empty());

  Value result;
  std::string errorMsg;
  std::vector<Value> args;
  ASSERT_TRUE(manager.executeProcedure("appendAndSize", args, result, errorMsg)) << errorMsg;
  EXPECT_EQ(std::get<int32_t>(result), 3);
}

TEST(ArrayTest, PopReturnsAndShrinks) {
  std::string source = R"(
        int32 testPop() {
            int32[] data = [1, 2, 3];
            int32 last = pop(data);
            return last + len(data); // 3 + 2 = 5
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "array_pop.script", errors));
  ASSERT_TRUE(errors.empty());

  Value result;
  std::string errorMsg;
  std::vector<Value> args;
  ASSERT_TRUE(manager.executeProcedure("testPop", args, result, errorMsg)) << errorMsg;
  EXPECT_EQ(std::get<int32_t>(result), 5);
}

TEST(ArrayTest, IndexAssignmentRespectsType) {
  std::string source = R"(
        int32 overwrite() {
            int32[] data = [1, 2];
            data[0] = 5;
            return data[0];
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "array_assign.script", errors));
  ASSERT_TRUE(errors.empty());

  Value result;
  std::string errorMsg;
  std::vector<Value> args;
  ASSERT_TRUE(manager.executeProcedure("overwrite", args, result, errorMsg)) << errorMsg;
  EXPECT_EQ(std::get<int32_t>(result), 5);
}

TEST(ArrayTest, HostArrayConversion) {
  std::string source = R"(
        int32 sumFirstTwo(int32[] values) {
            return values[0] + values[1];
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "array_host.script", errors));
  ASSERT_TRUE(errors.empty());

  std::vector<Value> elems = {static_cast<int32_t>(3), static_cast<int32_t>(7)};
  Value hostArray = ValueHelper::createArray(TypeInfo(DataType::INT32), elems);

  Value result;
  std::string errorMsg;
  std::vector<Value> args = {hostArray};
  ASSERT_TRUE(manager.executeProcedure("sumFirstTwo", args, result, errorMsg)) << errorMsg;
  EXPECT_EQ(std::get<int32_t>(result), 10);
}

TEST(ArrayTest, OutOfBoundsThrows) {
  std::string source = R"(
        int32 badIndex() {
            int32[] data = [1, 2];
            return data[5];
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "array_oob.script", errors));
  ASSERT_TRUE(errors.empty());

  Value result;
  std::string errorMsg;
  std::vector<Value> args;
  ASSERT_FALSE(manager.executeProcedure("badIndex", args, result, errorMsg));
  EXPECT_FALSE(errorMsg.empty());
}

TEST(ArrayTest, IndexAssignOutOfBoundsThrows) {
  std::string source = R"(
        int32 badAssign() {
            int32[] data = [1, 2];
            data[5] = 10;
            return data[0];
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "array_oob_assign.script", errors));
  ASSERT_TRUE(errors.empty());

  Value result;
  std::string errorMsg;
  std::vector<Value> args;
  ASSERT_FALSE(manager.executeProcedure("badAssign", args, result, errorMsg));
  EXPECT_FALSE(errorMsg.empty());
}

TEST(ArrayTest, ArithmeticOnArrayRejects) {
  std::string source = R"(
        int32 badAdd() {
            int32[] data = [1];
            return data + 1;
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "array_arith.script", errors));
  ASSERT_TRUE(errors.empty());

  Value result;
  std::string errorMsg;
  std::vector<Value> args;
  ASSERT_FALSE(manager.executeProcedure("badAdd", args, result, errorMsg));
  EXPECT_FALSE(errorMsg.empty());
}

TEST(ArrayTest, IndexingNonArrayRejects) {
  std::string source = R"(
        int32 badIndex() {
            int32 value = 3;
            return value[0];
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "index_nonarray.script", errors));
  ASSERT_TRUE(errors.empty());

  Value result;
  std::string errorMsg;
  std::vector<Value> args;
  ASSERT_FALSE(manager.executeProcedure("badIndex", args, result, errorMsg));
  EXPECT_FALSE(errorMsg.empty());
}

TEST(ArrayTest, PushLenOnNonArrayRejects) {
  std::string source = R"(
        int32 badPush() {
            return push(1, 2);
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "push_nonarray.script", errors));
  ASSERT_TRUE(errors.empty());

  Value result;
  std::string errorMsg;
  std::vector<Value> args;
  ASSERT_FALSE(manager.executeProcedure("badPush", args, result, errorMsg));
  EXPECT_FALSE(errorMsg.empty());

  source = R"(
        int32 badLen() {
            return len(5);
        }
    )";
  errors.clear();
  ASSERT_TRUE(manager.loadScriptSource(source, "len_nonarray.script", errors));
  ASSERT_TRUE(errors.empty());
  errorMsg.clear();
  ASSERT_FALSE(manager.executeProcedure("badLen", args, result, errorMsg));
  EXPECT_FALSE(errorMsg.empty());
}

TEST(ArrayTest, PopOnNonArrayAndEmptyRejects) {
  std::string source = R"(
        int32 badPopType() {
            return pop(1);
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "pop_nonarray.script", errors));
  ASSERT_TRUE(errors.empty());

  Value result;
  std::string errorMsg;
  std::vector<Value> args;
  ASSERT_FALSE(manager.executeProcedure("badPopType", args, result, errorMsg));
  EXPECT_FALSE(errorMsg.empty());

  source = R"(
        int32 badPopEmpty() {
            int32[] data = [];
            return pop(data);
        }
    )";
  errors.clear();
  ASSERT_TRUE(manager.loadScriptSource(source, "pop_empty.script", errors));
  ASSERT_TRUE(errors.empty());
  errorMsg.clear();
  ASSERT_FALSE(manager.executeProcedure("badPopEmpty", args, result, errorMsg));
  EXPECT_FALSE(errorMsg.empty());
}

TEST(ArrayTest, PushConvertsElementsAndRejectsBadType) {
  std::string source = R"(
        int32 testPush() {
            int32[] data = [1];
            push(data, 2.5); // converts to int
            push(data, true); // converts to int 1
            return len(data);
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "push_convert.script", errors));
  ASSERT_TRUE(errors.empty());

  Value result;
  std::string errorMsg;
  std::vector<Value> args;
  ASSERT_TRUE(manager.executeProcedure("testPush", args, result, errorMsg)) << errorMsg;
  EXPECT_EQ(std::get<int32_t>(result), 3);

  source = R"(
        int32 badType() {
            int32[] data = [1];
            push(data, "nope");
            return len(data);
        }
    )";
  errors.clear();
  ASSERT_TRUE(manager.loadScriptSource(source, "push_badtype.script", errors));
  ASSERT_TRUE(errors.empty());
  errorMsg.clear();
  ASSERT_FALSE(manager.executeProcedure("badType", args, result, errorMsg));
  EXPECT_FALSE(errorMsg.empty());
}

TEST(ArrayTest, EqualityAndComparisonSemantics) {
  std::string source = R"(
        bool equalArrays() {
            int32[] a = [1, 2];
            int32[] b = [1, 2];
            return a == b;
        }

        bool notEqualArrays() {
            int32[] a = [1, 2];
            int32[] b = [2, 1];
            return a != b;
        }

        bool compareLexicographic() {
            int32[] a = [1];
            int32[] b = [2];
            return a < b;
        }

        bool comparePrefix() {
            int32[] a = [1, 2];
            int32[] b = [1, 2, 3];
            return a < b;
        }
    )";

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(source, "array_eq.script", errors));
  ASSERT_TRUE(errors.empty());

  Value result;
  std::string errorMsg;
  std::vector<Value> args;

  ASSERT_TRUE(manager.executeProcedure("equalArrays", args, result, errorMsg)) << errorMsg;
  EXPECT_TRUE(std::get<bool>(result));

  ASSERT_TRUE(manager.executeProcedure("notEqualArrays", args, result, errorMsg)) << errorMsg;
  EXPECT_TRUE(std::get<bool>(result));

  errorMsg.clear();
  ASSERT_TRUE(manager.executeProcedure("compareLexicographic", args, result, errorMsg)) << errorMsg;
  EXPECT_TRUE(std::get<bool>(result));

  errorMsg.clear();
  ASSERT_TRUE(manager.executeProcedure("comparePrefix", args, result, errorMsg)) << errorMsg;
  EXPECT_TRUE(std::get<bool>(result));
}
