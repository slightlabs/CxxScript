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

std::string runError(const std::string &source, const std::string &proc) {
  ScriptManager manager;
  std::vector<CompilationError> errors;
  EXPECT_TRUE(manager.loadScriptSource(source, "test.script", errors));
  Value result;
  std::string errorMsg;
  EXPECT_FALSE(manager.executeProcedure(proc, {}, result, errorMsg));
  return errorMsg;
}
} // namespace

// ---------------------------------------------------------------------------
// Declaration & literals
// ---------------------------------------------------------------------------

TEST(MapTest, DeclarationAndLiteral) {
  Value v = run(R"(
int32 f() {
  map<string, int32> m = {"a": 1, "b": 2};
  return m["a"] + m["b"];
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 3);
}

TEST(MapTest, EmptyLiteralAndDefaultInit) {
  Value v = run(R"(
int32 f() {
  map<string, int32> m = {};
  m["x"] = 5;
  map<int32, string> n;
  n[7] = "seven";
  return m["x"] + len(n);
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 6);
}

TEST(MapTest, IntKeysAndStringValues) {
  Value v = run(R"(
string f() {
  map<int32, string> m = {1: "one", 2: "two"};
  return m[2] + m[1];
})",
                "f");
  EXPECT_EQ(std::get<std::string>(v), "twoone");
}

TEST(MapTest, NestedMapValue) {
  Value v = run(R"(
int32 f() {
  map<string, map<string, int32>> grid = {};
  grid["r1"] = {"c1": 9, "c2": 4};
  return grid["r1"]["c2"];
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 4);
}

// ---------------------------------------------------------------------------
// Indexing & mutation
// ---------------------------------------------------------------------------

TEST(MapTest, InsertOverwriteAndCompoundAssign) {
  Value v = run(R"(
int32 f() {
  map<string, int32> m = {"a": 1};
  m["b"] = 10;   // insert
  m["a"] = 5;    // overwrite
  m["a"] += 2;   // compound
  m["b"]++;      // update expr on entry
  return m["a"] * 100 + m["b"];
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 711);
}

TEST(MapTest, MissingKeyReadIsError) {
  std::string err = runError(R"(
int32 f() {
  map<string, int32> m = {"a": 1};
  return m["nope"];
})",
                             "f");
  EXPECT_NE(err.find("key not found"), std::string::npos) << err;
}

TEST(MapTest, CompoundAssignMissingKeyIsError) {
  std::string err = runError(R"(
int32 f() {
  map<string, int32> m = {};
  m["x"] += 1;
  return 0;
})",
                             "f");
  EXPECT_NE(err.find("not found"), std::string::npos) << err;
}

TEST(MapTest, KeyTypeCoercion) {
  // int32 key written via int64-compatible literal, read via uint8
  Value v = run(R"(
int32 f() {
  map<int32, int32> m = {10: 99};
  return m[10];
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 99);
}

// ---------------------------------------------------------------------------
// for-each over keys
// ---------------------------------------------------------------------------

TEST(MapTest, ForEachIteratesKeysInOrder) {
  Value v = run(R"(
string f() {
  map<string, int32> m = {"b": 2, "a": 1, "c": 3};
  string out = "";
  for (string k : m) { out += k; }
  return out;
})",
                "f");
  EXPECT_EQ(std::get<std::string>(v), "abc"); // std::map ordering
}

TEST(MapTest, ForEachIntKeys) {
  Value v = run(R"(
int32 f() {
  map<int32, int32> m = {3: 30, 1: 10, 2: 20};
  int32 sum = 0;
  for (int32 k : m) { sum += k * 10 + m[k]; }
  return sum;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 10 + 10 + 20 + 20 + 30 + 30);
}

// ---------------------------------------------------------------------------
// Builtins
// ---------------------------------------------------------------------------

TEST(MapTest, HasRemoveSizeClear) {
  Value v = run(R"(
int32 f() {
  map<string, int32> m = {"a": 1, "b": 2};
  int32 r = 0;
  if (has(m, "a")) { r += 1; }
  if (!has(m, "z")) { r += 2; }
  if (remove(m, "a")) { r += 4; }
  if (!remove(m, "a")) { r += 8; }
  r += size(m) * 100;   // 1 left
  r += len(m) * 1000;
  clear(m);
  if (size(m) == 0) { r += 16; }
  return r;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 1 + 2 + 4 + 8 + 100 + 1000 + 16);
}

TEST(MapTest, KeysAndValuesReturnArrays) {
  Value v = run(R"(
string f() {
  map<string, int32> m = {"b": 2, "a": 1};
  string out = "";
  for (string k : keys(m)) { out += k; }
  int32 vs = 0;
  for (int32 x : values(m)) { vs += x; }
  return out + ":" + toString(vs);
})",
                "f");
  EXPECT_EQ(std::get<std::string>(v), "ab:3");
}

TEST(MapTest, IsMapAndTypeof) {
  Value v = run(R"(
string f() {
  map<string, int32> m = {"a": 1};
  int32[] a = [1];
  return typeof(m) + "|" + toString(isMap(m)) + "|" +
         toString(isMap(a)) + "|" + toString(isArray(m));
})",
                "f");
  EXPECT_EQ(std::get<std::string>(v), "map<string, int32>|true|false|false");
}

TEST(MapTest, ToStringFormat) {
  Value v = run(R"(
string f() {
  map<string, int32> m = {"x": 1, "y": 2};
  return toString(m);
})",
                "f");
  EXPECT_EQ(std::get<std::string>(v), "{x: 1, y: 2}");
}

// ---------------------------------------------------------------------------
// Procedures: params and returns
// ---------------------------------------------------------------------------

TEST(MapTest, MapAsParameterAndReturn) {
  Value v = run(R"(
map<string, int32> doubled(map<string, int32> src) {
  map<string, int32> out = {};
  for (string k : src) { out[k] = src[k] * 2; }
  return out;
}
int32 f() {
  map<string, int32> r = doubled({"a": 3, "b": 4});
  return r["a"] + r["b"];
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 14);
}

// ---------------------------------------------------------------------------
// const
// ---------------------------------------------------------------------------

TEST(MapTest, ConstMapReadOk) {
  Value v = run(R"(
int32 f() {
  const map<string, int32> m = {"a": 7};
  return m["a"];
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 7);
}

TEST(MapTest, ConstMapWriteRejected) {
  EXPECT_FALSE(compiles(R"(
void f() {
  const map<string, int32> m = {"a": 7};
  m["b"] = 1;
})"));
}

TEST(MapTest, ConstMapIncrementRejected) {
  EXPECT_FALSE(compiles(R"(
void f() {
  const map<string, int32> m = {"a": 7};
  m["a"]++;
})"));
}

// ---------------------------------------------------------------------------
// Semantic validation
// ---------------------------------------------------------------------------

TEST(MapTest, MapKeyTypeMismatchIsCompileError) {
  // array literal as key type is invalid at parse; wrong scalar is a
  // conversion at validator level only when incompatible
  EXPECT_FALSE(compiles(R"(
void f() {
  map<string, int32> m = {"a": 1};
  int32[] k = {1};
  int32 x = m[k];
})"));
}

TEST(MapTest, MapValueMismatchIsCompileError) {
  EXPECT_FALSE(compiles(R"(
void f() {
  map<string, int32> m = {"a": 1};
  string[] s = {"x"};
  m["b"] = s;
})"));
}

TEST(MapTest, MapAssignToScalarRejected) {
  EXPECT_FALSE(compiles(R"(
void f() {
  map<string, int32> m = {"a": 1};
  int32 x = m;
})"));
}

TEST(MapTest, MapVariableShadowedNameStillWorks) {
  // 'map' remains usable as an identifier
  Value v = run(R"(
int32 f() {
  int32 map = 5;
  map<string, int32> m = {"k": map};
  return m["k"];
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 5);
}

// ---------------------------------------------------------------------------
// Memory limits
// ---------------------------------------------------------------------------

TEST(MapTest, MapEntryLimitEnforced) {
  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(R"(
void f() {
  map<int32, int32> m = {};
  for (int32 i = 0; i < 100; ++i) { m[i] = i; }
})",
                                       "t.script", errors));
  manager.setMemoryLimits(5, 0, 0); // max container size 5
  Value result;
  std::string errorMsg;
  EXPECT_FALSE(manager.executeProcedure("f", {}, result, errorMsg));
  EXPECT_NE(errorMsg.find("size limit"), std::string::npos) << errorMsg;
}

TEST(MapTest, MapAllocationCountEnforced) {
  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(R"(
void f() {
  for (int32 i = 0; i < 10; ++i) {
    map<int32, int32> m = {i: i};
  }
})",
                                       "t.script", errors));
  manager.setMemoryLimits(0, 0, 3); // max 3 container allocations
  Value result;
  std::string errorMsg;
  EXPECT_FALSE(manager.executeProcedure("f", {}, result, errorMsg));
  EXPECT_NE(errorMsg.find("allocation count"), std::string::npos) << errorMsg;
}
