#include "ScriptManager.h"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

using namespace Script;

namespace {
Value run(const std::string &source, const std::string &proc,
          const std::vector<Value> &args = {}) {
  ScriptManager manager;
  std::vector<CompilationError> errors;
  EXPECT_TRUE(manager.loadScriptSource(source, "test.script", errors));
  if (!errors.empty()) {
    for (const auto &e : errors) {
      ADD_FAILURE() << e.toString();
    }
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
} // namespace

// ---------------------------------------------------------------------------
// const
// ---------------------------------------------------------------------------

TEST(ConstTest, ConstVariableReads) {
  EXPECT_EQ(std::get<int32_t>(run("int32 f() { const int32 x = 7; return x * 3; }",
                                  "f")),
            21);
}

TEST(ConstTest, ConstRejectsAssign) {
  EXPECT_FALSE(compiles("int32 f() { const int32 x = 1; x = 2; return x; }"));
  EXPECT_FALSE(compiles("int32 f() { const int32 x = 1; x += 2; return x; }"));
  EXPECT_FALSE(compiles("int32 f() { const int32 x = 1; x++; return x; }"));
}

TEST(ConstTest, ConstRequiresInitializer) {
  EXPECT_FALSE(compiles("int32 f() { const int32 x; return 0; }"));
}

TEST(ConstTest, ConstArrayRejectsIndexAssign) {
  EXPECT_FALSE(
      compiles("int32 f() { const int32[] a = [1]; a[0] = 9; return 0; }"));
}

TEST(ConstTest, ConstForEachVariable) {
  EXPECT_FALSE(compiles(
      "int32 f() { int32[] a=[1]; for (const int32 x : a) { x = 9; } return 0; }"));
  EXPECT_EQ(std::get<int32_t>(run(R"(
      int32 f() {
        int32[] a = [1, 2];
        int32 s = 0;
        for (const int32 x : a) { s += x; }
        return s;
      })",
                                  "f")),
            3);
}

TEST(ConstTest, NonConstUnaffected) {
  EXPECT_EQ(std::get<int32_t>(run("int32 f() { int32 x = 1; x = 9; x++; return x; }",
                                  "f")),
            10);
}

// ---------------------------------------------------------------------------
// Variadic registerExternalFunction
// ---------------------------------------------------------------------------

static int64_t freeSquare(int64_t n) { return n * n; }

TEST(VariadicExternalTest, FreeFunctionPointer) {
  ScriptManager m;
  m.registerExternalFunction("sq", &freeSquare);
  std::vector<CompilationError> errors;
  ASSERT_TRUE(
      m.loadScriptSource("int64 f() { return sq(6); }", "t", errors));
  Value r;
  std::string err;
  ASSERT_TRUE(m.executeProcedure("f", {}, r, err)) << err;
  EXPECT_EQ(std::get<int64_t>(r), 36);
}

TEST(VariadicExternalTest, LambdaAndStdFunction) {
  ScriptManager m;
  m.registerExternalFunction("inc", [](int64_t n) { return n + 1; });
  m.registerExternalFunction(
      "add", std::function<int32_t(int32_t, int32_t)>(
                 [](int32_t a, int32_t b) { return a + b; }));
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int64 f() { return inc(0) + add(40, 2); }", "t", errors));
  Value r;
  std::string err;
  ASSERT_TRUE(m.executeProcedure("f", {}, r, err)) << err;
  EXPECT_EQ(std::get<int64_t>(r), 43);
}

TEST(VariadicExternalTest, VoidReturnAndArrayArg) {
  ScriptManager m;
  m.registerExternalFunction("noop", []() {});
  m.registerExternalFunction("count", [](ArrayPtr a) {
    return static_cast<int64_t>(a->elements.size());
  });
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int64 f() { noop(); int32[] a = [1,2,3]; return count(a); }", "t",
      errors));
  Value r;
  std::string err;
  ASSERT_TRUE(m.executeProcedure("f", {}, r, err)) << err;
  EXPECT_EQ(std::get<int64_t>(r), 3);
}

TEST(VariadicExternalTest, ArityMismatchIsRuntimeError) {
  ScriptManager m;
  m.registerExternalFunction("one", [](int32_t a) { return a; });
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource("int32 f() { return one(1, 2); }", "t", errors));
  Value r;
  std::string err;
  EXPECT_FALSE(m.executeProcedure("f", {}, r, err));
  EXPECT_NE(err.find("argument"), std::string::npos);
}

// ---------------------------------------------------------------------------
// import
// ---------------------------------------------------------------------------

TEST(ImportTest, ImportsResolveRelativeToFile) {
  const std::string dir = "/tmp/cxxscript_test_import";
  std::filesystem::create_directories(dir);
  {
    std::ofstream lib(dir + "/lib.script");
    lib << "int64 triple(int64 n) { return n * 3; }\n";
  }
  {
    std::ofstream main(dir + "/main.script");
    main << "import \"lib.script\";\n"
            "int32 main() { return triple(5); }\n";
  }

  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptFile(dir + "/main.script", errors));
  Value r;
  std::string err;
  ASSERT_TRUE(m.executeProcedure("main", {}, r, err)) << err;
  EXPECT_EQ(std::get<int32_t>(r), 15);
}

TEST(ImportTest, CircularImport) {
  const std::string dir = "/tmp/cxxscript_test_circular";
  std::filesystem::create_directories(dir);
  {
    std::ofstream a(dir + "/a.script");
    a << "import \"b.script\";\nint32 fa() { return fb() + 1; }\n";
  }
  {
    std::ofstream b(dir + "/b.script");
    b << "import \"a.script\";\nint32 fb() { return 10; }\n";
  }

  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptFile(dir + "/a.script", errors));
  Value r;
  std::string err;
  ASSERT_TRUE(m.executeProcedure("fa", {}, r, err)) << err;
  EXPECT_EQ(std::get<int32_t>(r), 11);
}

TEST(ImportTest, MissingImportReportsError) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  EXPECT_FALSE(m.loadScriptSource("import \"nonexistent_xyz.script\";\n"
                                  "int32 f() { return 0; }",
                                  "/tmp/test_missing_import.script", errors));
  EXPECT_FALSE(errors.empty());
}
