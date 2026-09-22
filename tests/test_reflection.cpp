// Reflection auto-binding prototype: host structs bound via StructCodecOf
// convert to/from script StructValues, flowing through
// registerExternalFunction arguments/returns and callProcedure both ways.
// Script-side, values carry the codec's type name — a matching `struct`
// declaration gives statically checked field access.
#include "ScriptManager.h"
#include <gtest/gtest.h>

using namespace Script;

namespace {

struct Point {
  int32_t x = 0;
  int32_t y = 0;
};

struct Rect {
  Point tl;
  Point br;
};

// Script-side declarations matching the bound host types.
const char *kPointDecl = "struct Point { int32 x; int32 y; }\n";
const char *kRectDecl =
    "struct Point { int32 x; int32 y; }\n"
    "struct Rect { Point tl; Point br; }\n";

Value call(ScriptManager &m, const std::string &proc,
           const std::vector<Value> &args = {}) {
  Value result;
  std::string err;
  EXPECT_TRUE(m.executeProcedure(proc, args, result, err)) << err;
  return result;
}
} // namespace

// Bind the host structs. A real P2996 implementation would generate these
// descriptors via members_of(^^T); the prototype lists them explicitly.
namespace Script {
template <> struct StructCodecOf<Point> {
  static constexpr const char *name = "Point";
  static auto members() {
    return std::tuple{Member{"x", &Point::x}, Member{"y", &Point::y}};
  }
};
template <> struct StructCodecOf<Rect> {
  static constexpr const char *name = "Rect";
  static auto members() {
    return std::tuple{Member{"tl", &Rect::tl}, Member{"br", &Rect::br}};
  }
};
} // namespace Script

TEST(ReflectionTest, HostStructFlowsToScript) {
  ScriptManager m;
  m.registerExternalFunction(
      "origin", std::function<Point()>([] { return Point{3, 4}; }));
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      std::string(kPointDecl) +
          "int64 f() { Point p = origin(); return p.x + p.y; }",
      "t.script", errors));

  EXPECT_EQ(std::get<int64_t>(call(m, "f")), 7);
}

TEST(ReflectionTest, ScriptStructFlowsToHost) {
  ScriptManager m;
  m.registerExternalFunction("sumXY",
                             std::function<int64_t(const Point &)>(
                                 [](const Point &p) { return p.x + p.y; }));
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      std::string(kPointDecl) +
          "int64 f() { Point p; p.x = 10; p.y = 20; return sumXY(p); }",
      "t.script", errors));

  EXPECT_EQ(std::get<int64_t>(call(m, "f")), 30);
}

TEST(ReflectionTest, NestedBoundStructs) {
  ScriptManager m;
  m.registerExternalFunction("unit", std::function<Rect()>([] {
                               return Rect{Point{0, 0}, Point{1, 1}};
                             }));
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      std::string(kRectDecl) +
          "int64 f() { Rect r = unit(); return r.br.x - r.tl.x; }",
      "t.script", errors));

  EXPECT_EQ(std::get<int64_t>(call(m, "f")), 1);
}

TEST(ReflectionTest, CallProcedureReturnsBoundStruct) {
  ScriptManager m;
  m.registerExternalFunction(
      "origin", std::function<Point()>([] { return Point{5, 6}; }));
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      std::string(kPointDecl) + "Point f() { return origin(); }",
      "t.script", errors));

  Point p = m.callProcedure<Point>("f");
  EXPECT_EQ(p.x, 5);
  EXPECT_EQ(p.y, 6);
}

TEST(ReflectionTest, CallProcedureTakesBoundStruct) {
  ScriptManager m;
  m.registerExternalFunction("sumXY",
                             std::function<int64_t(const Point &)>(
                                 [](const Point &p) { return p.x + p.y; }));
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      std::string(kPointDecl) +
          "int64 f(Point p) { return sumXY(p); }",
      "t.script", errors));

  int64_t sum = m.callProcedure<int64_t>("f", Point{7, 8});
  EXPECT_EQ(sum, 15);
}

TEST(ReflectionTest, MissingFieldsDefaultConstruct) {
  ScriptManager m;
  m.registerExternalFunction("getY",
                             std::function<int64_t(const Point &)>(
                                 [](const Point &p) { return p.y; }));
  std::vector<CompilationError> errors;
  // Script struct lacks 'y' — the host field keeps its default.
  ASSERT_TRUE(m.loadScriptSource(
      "struct Point { int32 x; }\n"
      "int64 f() { Point s; s.x = 1; return getY(s); }",
      "t.script", errors));

  EXPECT_EQ(std::get<int64_t>(call(m, "f")), 0);
}

TEST(ReflectionTest, UnboundTypeRejectedAtCompileTime) {
  struct Unbound {};
  static_assert(!::Script::HasStructCodec<Unbound>::value,
                "unspecialized codec must not be detected");
  static_assert(!::Script::detail::IsSupportedType<Unbound>::value,
                "unbound types are not valid external-function arguments");
  static_assert(::Script::detail::IsSupportedType<Point>::value,
                "bound types are valid external-function arguments");
}
