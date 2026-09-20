#include <algorithm>
#include <filesystem>
#include <fstream>

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

std::string writeTemp(const std::string &name, const std::string &source) {
  std::string path = (std::filesystem::temp_directory_path() / name).string();
  std::ofstream out(path);
  out << source;
  return path;
}
} // namespace

// ---------------------------------------------------------------------------
// Declaration & construction
// ---------------------------------------------------------------------------

TEST(StructTest, DeclAndPositionalConstruction) {
  Value v = run(R"(
struct Point { int32 x; int32 y; }
int32 f() {
  Point p = Point(3, 4);
  return p.x * 10 + p.y;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 34);
}

TEST(StructTest, TrailingSemicolonOptional) {
  Value v = run(R"(
struct P { int32 x; };
int32 f() { P p = P(7); return p.x; })",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 7);
}

TEST(StructTest, ForwardReferenceBeforeDecl) {
  // Struct used by a procedure declared before the struct body.
  Value v = run(R"(
int32 f() { P p = P(11); return p.x; }
struct P { int32 x; })",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 11);
}

TEST(StructTest, DefaultInitZeroesFields) {
  Value v = run(R"(
struct P { int32 x; string s; bool b; double d; }
int32 f() {
  P p;
  if (p.x == 0 && p.s == "" && !p.b && p.d == 0.0) { return 1; }
  return 0;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 1);
}

TEST(StructTest, NestedDefaultInit) {
  Value v = run(R"(
struct Inner { int32 v; }
struct Outer { Inner i; int32 n; }
int32 f() {
  Outer o;
  return o.i.v + o.n;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 0);
}

// ---------------------------------------------------------------------------
// Member access & assignment
// ---------------------------------------------------------------------------

TEST(StructTest, MemberAssignAndCompound) {
  Value v = run(R"(
struct P { int32 x; int32 y; }
int32 f() {
  P p = P(1, 2);
  p.x = 10;
  p.y += 5;
  return p.x + p.y;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 17);
}

TEST(StructTest, MemberUpdateOps) {
  Value v = run(R"(
struct P { int32 x; }
int32 f() {
  P p = P(5);
  p.x++;
  ++p.x;
  p.x--;
  return p.x;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 6);
}

TEST(StructTest, NestedMemberChains) {
  Value v = run(R"(
struct V2 { int32 x; int32 y; }
struct R { V2 tl; V2 br; }
int32 f() {
  R r = R(V2(1, 2), V2(3, 4));
  r.tl.x = 9;
  r.br.y += 10;
  return r.tl.x + r.br.y;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 23);
}

TEST(StructTest, MemberThroughArrayIndex) {
  Value v = run(R"(
struct P { int32 x; }
struct S { P[] pts; }
int32 f() {
  S s = S([P(1), P(2)]);
  s.pts[0].x = 50;
  return s.pts[0].x + s.pts[1].x;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 52);
}

// ---------------------------------------------------------------------------
// Structs in containers
// ---------------------------------------------------------------------------

TEST(StructTest, ArrayOfStructs) {
  Value v = run(R"(
struct P { int32 x; int32 y; }
int32 f() {
  P[] pts = [P(1, 2), P(3, 4)];
  pts[1].x = 30;
  return pts[0].x + pts[1].x;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 31);
}

TEST(StructTest, ForeachOverStructArray) {
  Value v = run(R"(
struct P { int32 v; }
int32 f() {
  P[] ps = [P(1), P(2), P(3)];
  int32 t = 0;
  for (P q : ps) { t += q.v; }
  return t;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 6);
}

TEST(StructTest, MapWithStructValues) {
  Value v = run(R"(
struct P { int32 x; }
int32 f() {
  map<string, P> m = {"a": P(7)};
  m["b"] = P(9);
  return m["a"].x + m["b"].x;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 16);
}

TEST(StructTest, ArrayFieldInsideStruct) {
  Value v = run(R"(
struct S { int32[] vals; }
int32 f() {
  S s = S([4, 5, 6]);
  s.vals[1] = 50;
  push(s.vals, 7);
  return s.vals[0] + s.vals[1] + s.vals[2] + s.vals[3];
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 67);
}

// ---------------------------------------------------------------------------
// Procedures: params, returns, equality
// ---------------------------------------------------------------------------

TEST(StructTest, StructParamAndReturn) {
  Value v = run(R"(
struct P { int32 x; }
P make(int32 n) { return P(n * 2); }
int32 read(P p) { return p.x + 1; }
int32 f() { return read(make(10)); })",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 21);
}

TEST(StructTest, StructEquality) {
  Value v = run(R"(
struct P { int32 x; int32 y; }
int32 f() {
  if (P(1, 2) == P(1, 2) && P(1, 2) != P(1, 3)) { return 1; }
  return 0;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 1);
}

TEST(StructTest, StructSharedSemantics) {
  // Structs are reference types: assigning shares the object.
  Value v = run(R"(
struct P { int32 x; }
int32 f() {
  P a = P(1);
  P b = a;
  b.x = 42;
  return a.x;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 42);
}

// ---------------------------------------------------------------------------
// Compile-time validation
// ---------------------------------------------------------------------------

TEST(StructTest, UnknownStructTypeRejected) {
  EXPECT_FALSE(compiles("int32 f() { Foo x = Foo(); return 0; }"));
}

TEST(StructTest, DuplicateStructNameRejected) {
  EXPECT_FALSE(compiles(
      "struct P { int32 x; } struct P { int32 y; } int32 f() { return 0; }"));
}

TEST(StructTest, DuplicateFieldRejected) {
  EXPECT_FALSE(
      compiles("struct P { int32 x; int32 x; } int32 f() { return 0; }"));
}

TEST(StructTest, VoidFieldRejected) {
  EXPECT_FALSE(
      compiles("struct P { void x; } int32 f() { return 0; }"));
}

TEST(StructTest, RecursiveByValueRejected) {
  EXPECT_FALSE(
      compiles("struct N { N next; } int32 f() { return 0; }"));
  EXPECT_FALSE(compiles(
      "struct A { B b; } struct B { A a; } int32 f() { return 0; }"));
}

TEST(StructTest, WrongCtorArgCountRejected) {
  EXPECT_FALSE(compiles(
      "struct P { int32 x; int32 y; } int32 f() { P p = P(1); return 0; }"));
}

TEST(StructTest, CtorArgTypeMismatchRejected) {
  EXPECT_FALSE(compiles(
      "struct P { int32 x; } int32 f() { P p = P(\"s\"); return 0; }"));
}

TEST(StructTest, UnknownFieldAccessRejected) {
  EXPECT_FALSE(compiles(
      "struct P { int32 x; } int32 f() { P p = P(1); return p.zzz; }"));
}

TEST(StructTest, MemberAssignTypeMismatchRejected) {
  EXPECT_FALSE(compiles(
      "struct P { int32 x; } int32 f() { P p = P(1); p.x = \"s\"; return 0; }"));
}

TEST(StructTest, MemberOnNonStructRejected) {
  EXPECT_FALSE(compiles("int32 f() { int32 x = 1; return x.foo; }"));
}

TEST(StructTest, ConstFieldWriteRejected) {
  EXPECT_FALSE(compiles(R"(
struct P { int32 x; }
int32 f() {
  const P p = P(1);
  p.x = 2;
  return 0;
})"));
  EXPECT_FALSE(compiles(R"(
struct P { int32 x; }
int32 f() {
  const P p = P(1);
  p.x++;
  return 0;
})"));
}

TEST(StructTest, ConstFieldReadOk) {
  Value v = run(R"(
struct P { int32 x; }
int32 f() {
  const P p = P(9);
  return p.x;
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 9);
}

TEST(StructTest, NestedConstWriteRejected) {
  EXPECT_FALSE(compiles(R"(
struct I { int32 v; }
struct O { I i; }
int32 f() {
  const O o = O(I(1));
  o.i.v = 2;
  return 0;
})"));
}

// ---------------------------------------------------------------------------
// Runtime errors
// ---------------------------------------------------------------------------

TEST(StructTest, MemberOnNonStructRejectedAtCompile) {
  EXPECT_FALSE(compiles(
      "int32 f() { int32[] arr = [1, 2]; return arr.x; }"));
}

// ---------------------------------------------------------------------------
// Cross-file & hot reload
// ---------------------------------------------------------------------------

TEST(StructTest, StructVisibleAcrossFiles) {
  std::string a = writeTemp("st_a.script",
                            "struct P { int32 x; }\n"
                            "P makeP(int32 n) { return P(n); }\n");
  std::string b = writeTemp("st_b.script",
                            "int32 f() { P p = makeP(8); return p.x; }\n");

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptFile(a, errors));
  ASSERT_TRUE(manager.loadScriptFile(b, errors));

  Value result;
  std::string err;
  ASSERT_TRUE(manager.executeProcedure("f", {}, result, err)) << err;
  EXPECT_EQ(std::get<int32_t>(result), 8);
}

TEST(StructTest, HotReloadSwapsStructDef) {
  std::string file = writeTemp(
      "st_hr.script",
      "struct P { int32 x; }\n"
      "int32 f() { P p = P(1); return p.x; }\n");

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptFile(file, errors));

  Value result;
  std::string err;
  ASSERT_TRUE(manager.executeProcedure("f", {}, result, err)) << err;
  EXPECT_EQ(std::get<int32_t>(result), 1);

  writeTemp("st_hr.script",
            "struct P { int32 x; int32 y; }\n"
            "int32 f() { P p = P(10, 20); return p.x + p.y; }\n");
  ASSERT_TRUE(manager.reloadScriptFile(file, errors));
  ASSERT_TRUE(manager.executeProcedure("f", {}, result, err)) << err;
  EXPECT_EQ(std::get<int32_t>(result), 30);
}

TEST(StructTest, HotReloadRemovesDeletedStruct) {
  std::string file = writeTemp(
      "st_hr2.script",
      "struct P { int32 x; }\n"
      "struct Q { int32 q; }\n"
      "int32 f() { return 1; }\n");

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptFile(file, errors));

  writeTemp("st_hr2.script",
            "struct P { int32 x; }\n"
            "int32 f() { return 1; }\n");
  ASSERT_TRUE(manager.reloadScriptFile(file, errors));

  // Q is gone: a new script referencing it must fail to compile.
  std::vector<CompilationError> errs2;
  EXPECT_FALSE(manager.loadScriptSource(
      "int32 g() { Q q = Q(1); return q.q; }", "st_late.script", errs2));
}

TEST(StructTest, FailedReloadKeepsOldStruct) {
  std::string file = writeTemp(
      "st_hr3.script",
      "struct P { int32 x; }\n"
      "int32 f() { P p = P(5); return p.x; }\n");

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptFile(file, errors));

  writeTemp("st_hr3.script", "this is not valid script @@@\n");
  EXPECT_FALSE(manager.reloadScriptFile(file, errors));

  Value result;
  std::string err;
  ASSERT_TRUE(manager.executeProcedure("f", {}, result, err)) << err;
  EXPECT_EQ(std::get<int32_t>(result), 5);
}

TEST(StructTest, PushAndTypeof) {
  Value v = run(R"(
struct P { int32 x; }
int32 f() {
  P[] ps;
  push(ps, P(1));
  push(ps, P(2));
  if (typeof(ps[0]) != "P") { return -1; }
  return ps[0].x + ps[1].x + len(ps);
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 5);
}

TEST(StructTest, StructNameConflictsWithProcedure) {
  EXPECT_FALSE(compiles(
      "struct P { int32 x; } int32 P() { return 0; } int32 f() { return 0; }"));
}

TEST(StructTest, StructFromImportedFile) {
  writeTemp("st_types.script", "struct V { int32 x; int32 y; }\n");
  std::string main = writeTemp(
      "st_main.script",
      "import \"st_types.script\";\n"
      "int32 f() { V v = V(4, 5); return v.x * 10 + v.y; }\n");

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptFile(main, errors));

  Value result;
  std::string err;
  ASSERT_TRUE(manager.executeProcedure("f", {}, result, err)) << err;
  EXPECT_EQ(std::get<int32_t>(result), 45);
}

TEST(StructTest, StructToString) {
  Value v = run(R"(
struct P { int32 x; int32 y; }
string f() { return toString(P(1, 2)); })",
                "f");
  EXPECT_EQ(std::get<std::string>(v), "P{x: 1, y: 2}");
}

TEST(StructTest, DefaultInitStructArray) {
  Value v = run(R"(
struct P { int32 x; }
int32 f() {
  P[] ps;
  return len(ps);
})",
                "f");
  EXPECT_EQ(std::get<int32_t>(v), 0);
}

TEST(StructTest, MemoryLimitCoversStructs) {
  ScriptManager manager;
  manager.setMemoryLimits(0, 0, 1); // only 1 allocation allowed
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptSource(R"(
struct P { int32 x; }
int32 f() {
  P a = P(1);
  P b = P(2);  // second struct alloc exceeds the limit
  return 0;
})",
                                       "t.script", errors));
  Value result;
  std::string err;
  EXPECT_FALSE(manager.executeProcedure("f", {}, result, err));
  EXPECT_NE(err.find("allocation"), std::string::npos) << err;
}
