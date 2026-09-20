#include "ScriptManager.h"
#include <gtest/gtest.h>
#include <algorithm>

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
} // namespace

// ---------------------------------------------------------------------------
// for-each
// ---------------------------------------------------------------------------

TEST(ForEachTest, IteratesArray) {
  EXPECT_EQ(std::get<int32_t>(run(R"(
      int32 f() {
        int32[] a = [10, 20, 30];
        int32 sum = 0;
        for (int32 x : a) { sum += x; }
        return sum;
      })",
                                  "f")),
            60);
}

TEST(ForEachTest, BreakAndContinue) {
  EXPECT_EQ(std::get<int32_t>(run(R"(
      int32 f() {
        int32[] a = [1, 2, 3, 4, 5];
        int32 sum = 0;
        for (int32 x : a) {
          if (x == 2) { continue; }
          if (x == 4) { break; }
          sum += x;
        }
        return sum;   // 1 + 3 = 4
      })",
                                  "f")),
            4);
}

TEST(ForEachTest, IteratesStringChars) {
  EXPECT_EQ(std::get<int32_t>(run(R"(
      int32 f() {
        int32 count = 0;
        for (char c : "hello") { count++; }
        return count;
      })",
                                  "f")),
            5);
}

TEST(ForEachTest, LoopVarScopedToLoop) {
  EXPECT_EQ(std::get<int32_t>(run(R"(
      int32 f() {
        int32[] a = [1, 2, 3];
        for (int32 x : a) { }
        int32 x = 99;   // no conflict with loop var
        return x;
      })",
                                  "f")),
            99);
}

TEST(ForEachTest, ElementTypeConversion) {
  EXPECT_EQ(std::get<int64_t>(run(R"(
      int64 f() {
        int32[] a = [1, 2, 3];
        int64 sum = 0;
        for (int64 x : a) { sum += x; }
        return sum;
      })",
                                  "f")),
            6);
}

TEST(ForEachTest, NonIterableRejected) {
  ScriptManager manager;
  std::vector<CompilationError> errors;
  // validator should reject non-iterable at compile time
  EXPECT_FALSE(manager.loadScriptSource(
      "int32 f() { for (int32 x : 5) { } return 0; }", "t.script", errors));
  EXPECT_FALSE(errors.empty());
}

// ---------------------------------------------------------------------------
// ++ / --
// ---------------------------------------------------------------------------

TEST(UpdateTest, PostfixAndPrefix) {
  EXPECT_EQ(std::get<int32_t>(run(R"(
      int32 f() {
        int32 x = 5;
        int32 a = x++;   // a = 5, x = 6
        int32 b = ++x;   // x = 7, b = 7
        return a * 100 + b * 10 + x;  // 500 + 70 + 7 = 577
      })",
                                  "f")),
            577);
}

TEST(UpdateTest, Decrement) {
  EXPECT_EQ(std::get<int32_t>(run(R"(
      int32 f() {
        int32 x = 10;
        x--;
        --x;
        return x;
      })",
                                  "f")),
            8);
}

TEST(UpdateTest, ArrayElementUpdate) {
  EXPECT_EQ(std::get<int32_t>(run(R"(
      int32 f() {
        int32[] a = [1, 2, 3];
        a[1]++;
        ++a[2];
        return a[0] * 100 + a[1] * 10 + a[2];  // 1*100 + 3*10 + 4
      })",
                                  "f")),
            134);
}

TEST(UpdateTest, InForLoopIncrement) {
  EXPECT_EQ(std::get<int32_t>(run(R"(
      int32 f() {
        int32 sum = 0;
        for (int32 i = 0; i < 5; i++) { sum += i; }
        return sum;
      })",
                                  "f")),
            10);
}

TEST(UpdateTest, ExternalVariable) {
  ScriptManager manager;
  int32_t host = 10;
  manager.registerExternalVariable(
      "host", [&]() -> Value { return host; },
      [&](const Value &v) { host = std::get<int32_t>(v); });
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource("int32 f() { host++; return ++host; }",
                                       "t.script", errors));
  Value result;
  std::string msg;
  ASSERT_TRUE(manager.executeProcedure("f", {}, result, msg)) << msg;
  EXPECT_EQ(std::get<int32_t>(result), 12);
  EXPECT_EQ(host, 12);
}

// ---------------------------------------------------------------------------
// Compound assignments
// ---------------------------------------------------------------------------

TEST(CompoundAssignTest, AllOps) {
  EXPECT_EQ(std::get<int64_t>(run(R"(
      int64 f() {
        int64 x = 100;
        x %= 7;    // 2
        x |= 8;    // 10
        x &= 6;    // 2
        x ^= 3;    // 1
        x <<= 4;   // 16
        x >>= 2;   // 4
        return x;
      })",
                                  "f")),
            4);
}

TEST(CompoundAssignTest, IndexCompoundAssign) {
  EXPECT_EQ(std::get<int32_t>(run(R"(
      int32 f() {
        int32[] a = [10, 20, 30];
        a[0] += 5;
        a[1] *= 2;
        a[2] %= 7;
        return a[0] * 100 + a[1] * 10 + a[2];  // 15*100 + 40*10 + 2
      })",
                                  "f")),
            1902);
}

TEST(CompoundAssignTest, ModAssignInForIncrement) {
  EXPECT_EQ(std::get<int32_t>(run(R"(
      int32 f() {
        int32 wraps = 0;
        for (int32 i = 0; i < 20; i++) {
          int32 slot = i;
          slot %= 3;
          if (slot == 0) { wraps++; }
        }
        return wraps;
      })",
                                  "f")),
            7);
}
