// Tests for the `null` literal and the jsonParse/jsonStringify builtins.
#include <gtest/gtest.h>

#include "ScriptManager.h"

using namespace Script;

namespace {

// Evaluate a snippet, returning (ok, value-as-string, error).
struct R {
  bool ok;
  std::string out;
  std::string err;
};

R eval(const std::string &src, bool useVM = false) {
  ScriptManager m;
  m.setVMEnabled(useVM);
  Value ret;
  std::string error;
  bool ok = m.evaluateSnippet(src, "<test>", ret, error);
  return {ok, ok ? ValueHelper::toString(ret) : "", error};
}

// Both engines must agree on null/JSON semantics.
void bothEngines(const std::string &src, const std::string &expected) {
  R t = eval(src, false);
  ASSERT_TRUE(t.ok) << t.err;
  R v = eval(src, true);
  ASSERT_TRUE(v.ok) << v.err;
  EXPECT_EQ(t.out, v.out);
  EXPECT_EQ(t.out, expected);
}

void bothFail(const std::string &src, const std::string &needle) {
  for (bool vm : {false, true}) {
    R r = eval(src, vm);
    ASSERT_FALSE(r.ok) << "expected failure: " << src;
    EXPECT_NE(r.err.find(needle), std::string::npos)
        << "error: " << r.err;
  }
}

} // namespace

TEST(NullTest, LiteralAndIdentity) {
  bothEngines("auto n = null; return n == null;", "true");
  bothEngines("auto n = null; return n != null;", "false");
  bothEngines("auto n = null; return n == 5;", "false");
  bothEngines("auto n = null; return 5 != n;", "true");
  bothEngines("return null == null;", "true");
}

TEST(NullTest, Conversions) {
  bothEngines("return toString(null);", "null");
  bothEngines("return typeof(null);", "null");
  bothEngines("return isNull(null);", "true");
  bothEngines("return isNull(0);", "false");
  bothEngines("return isNull(\"\");", "false");
  // null is falsy
  bothEngines("return null ? 1 : 2;", "2");
  bothEngines("auto n = null; if (n) { return 1; } return 2;", "2");
}

TEST(NullTest, Errors) {
  bothFail("return null + 1;", "null");
  bothFail("return null - 1;", "null");
  bothFail("return null < 1;", "null");
  bothFail("return toInt(null);", "null");
}

TEST(NullTest, TypedSlotsRejectNull) {
  // Static type check: declared-type vars can't hold null.
  ScriptManager m;
  std::vector<CompilationError> errors;
  EXPECT_FALSE(m.loadScriptSource("int32 f() { int32 x = null; return x; }",
                                  "t.script", errors));
  // The string case passes validation but fails on assignment at runtime.
  bothFail("string s = null; return s;", "null");
}

TEST(NullTest, ContainersCanHoldNull) {
  // Homogeneous null literals and mixed containers via jsonParse work;
  // heterogeneous array literals remain a compile-time type error.
  bothEngines("auto a = [null]; return len(a);", "1");
  bothEngines("auto m = {\"k\": null}; return m[\"k\"] == null;", "true");
  bothEngines("auto a = jsonParse(\"[1, null, 3]\"); return a[1] == null;",
              "true");
  // Counting nulls via index loop (forEach needs a statically iterable type)
  bothEngines("auto a = jsonParse(\"[1, null]\"); auto n = 0; "
              "for (int32 i = 0; i < len(a); i += 1) { if (a[i] == null) { "
              "n += 1; } } return n;",
              "1");
}

TEST(JsonTest, ParseScalarsAndContainers) {
  bothEngines("return jsonParse(\"42\");", "42");
  bothEngines("return jsonParse(\"3.5\");", "3.500000"); // std::to_string
  bothEngines("return jsonParse(\"\\\"hi\\\"\");", "hi");
  bothEngines("return jsonParse(\"true\");", "true");
  bothEngines("return isNull(jsonParse(\"null\"));", "true");
  bothEngines("auto a = jsonParse(\"[1,2,3]\"); return a[0] + a[2];", "4");
  bothEngines(
      "auto m = jsonParse(\"{\\\"a\\\": 1, \\\"b\\\": 2}\"); "
      "return m[\"a\"] + m[\"b\"];",
      "3");
}

TEST(JsonTest, ParseNested) {
  bothEngines(
      "auto v = jsonParse(\"{\\\"user\\\": {\\\"name\\\": \\\"x\\\"}, "
      "\\\"tags\\\": [\\\"a\\\",\\\"b\\\"], \\\"nil\\\": null}\"); "
      "return v[\"user\"][\"name\"] + \"/\" + v[\"tags\"][1];",
      "x/b");
  bothEngines(
      "auto v = jsonParse(\"{\\\"nil\\\": null}\"); "
      "return isNull(v[\"nil\"]);",
      "true");
}

TEST(JsonTest, StringifyRoundTrip) {
  bothEngines(
      "auto v = jsonParse(\"{\\\"a\\\": 1, \\\"b\\\": [true, null]}\"); "
      "return jsonStringify(v);",
      "{\"a\":1,\"b\":[true,null]}");
  bothEngines("return jsonStringify(jsonParse(\"[1,2.5,\\\"x\\\",true]\"));",
              "[1,2.5,\"x\",true]");
  bothEngines("return jsonStringify(null);", "null");
  bothEngines("return jsonStringify(\"a\\nb\");", "\"a\\nb\"");
}

TEST(JsonTest, ParseErrors) {
  bothFail("return jsonParse(\"{\");", "jsonParse");
  bothFail("return jsonParse(\"[1,]\");", "jsonParse");
  bothFail("return jsonParse(\"abc\");", "jsonParse");
  bothFail("return jsonParse(42);", "jsonParse");
}

TEST(JsonTest, StructsSerialize) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "struct P { string name; int32 age; }\n"
      "string go() { auto p = P(\"bob\", 7); return jsonStringify(p); }\n",
      "t.script", errors));
  Value ret;
  std::string err;
  ASSERT_TRUE(m.executeProcedure("go", {}, ret, err));
  EXPECT_EQ(ValueHelper::toString(ret), "{\"age\":7,\"name\":\"bob\"}");
}
