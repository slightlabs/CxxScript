// .scriptc compiled-artifact tests: saveCompiled serializes a file's
// bytecode-compiled declarations; loadCompiled re-registers them (the VM
// runs them — artifacts carry no AST).
#include <filesystem>
#include <fstream>

#include "ScriptManager.h"
#include <gtest/gtest.h>

using namespace Script;

namespace {
std::string writeTemp(const std::string &name, const std::string &source) {
  std::string path =
      (std::filesystem::temp_directory_path() / name).string();
  std::ofstream out(path);
  out << source;
  return path;
}

// Compile `source` (as `src` file) into artifact `art` on a fresh manager.
void compileTo(const std::string &src, const std::string &source,
               const std::string &art) {
  std::string srcPath = writeTemp(src, source);
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptFile(srcPath, errors));
  ASSERT_TRUE(m.saveCompiled(srcPath, art, errors))
      << (errors.empty() ? "" : errors.front().toString());
}

Value loadAndCall(const std::string &art, const std::string &proc,
                  const std::vector<Value> &args = {}) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  EXPECT_TRUE(m.loadCompiled(art, errors))
      << (errors.empty() ? "" : errors.front().toString());
  Value result;
  std::string err;
  EXPECT_TRUE(m.executeProcedure(proc, args, result, err)) << err;
  return result;
}

std::string tempPath(const std::string &name) {
  return (std::filesystem::temp_directory_path() / name).string();
}
} // namespace

TEST(CompiledTest, ProceduresRoundTrip) {
  std::string art = tempPath("c_procs.scriptc");
  compileTo("c_procs.script",
            "int64 add(int64 a, int64 b) { return a + b; }\n"
            "int64 fib(int32 n) { if (n < 2) { return n; } "
            "return fib(n - 1) + fib(n - 2); }\n",
            art);

  EXPECT_EQ(std::get<int64_t>(loadAndCall(art, "add", {int64_t(2), int64_t(3)})),
            5);
  EXPECT_EQ(std::get<int64_t>(loadAndCall(art, "fib", {int32_t(15)})), 610);
}

TEST(CompiledTest, StructsAndEnumsRoundTrip) {
  std::string art = tempPath("c_decls.scriptc");
  compileTo("c_decls.script",
            "enum Color { RED, GREEN = 5, BLUE }\n"
            "struct Point { int32 x; int32 y; int64 sum() { return x + y; } }\n"
            "int64 f() { Point p; p.x = 3; p.y = 4; "
            "return p.sum() + Color.BLUE; }\n",
            art);

  EXPECT_EQ(std::get<int64_t>(loadAndCall(art, "f")), 13); // 3+4 + 6
}

TEST(CompiledTest, LambdasAndDefaultsRoundTrip) {
  std::string art = tempPath("c_lam.scriptc");
  compileTo("c_lam.script",
            "int64 apply(int32 v, int32 d = 10) { return v + d; }\n"
            "int64 f() { auto sq = fn(int32 v) -> int64 { return v * v; }; "
            "return sq(6) + apply(5); }\n",
            art);

  EXPECT_EQ(std::get<int64_t>(loadAndCall(art, "f")), 51); // 36 + 15
  EXPECT_EQ(std::get<int64_t>(loadAndCall(art, "apply", {int32_t(1)})),
            11); // default arg preserved
}

TEST(CompiledTest, ExceptionsAndLoopsRoundTrip) {
  std::string art = tempPath("c_exc.scriptc");
  compileTo("c_exc.script",
            "int64 f() {\n"
            "  int64 total = 0;\n"
            "  for (int32 i = 0; i < 5; i += 1) {\n"
            "    try { if (i == 3) { throw \"skip\"; } total += i; }\n"
            "    catch (string e) { total += 100; }\n"
            "    finally { total += 1; }\n"
            "  }\n"
            "  return total;\n"
            "}\n",
            art);

  // total = 0+1+2 +100 (catch) +5 finally-passes → (0+1+2+4) +100 +5 = 112
  EXPECT_EQ(std::get<int64_t>(loadAndCall(art, "f")), 112);
}

TEST(CompiledTest, ArtifactEnablesVM) {
  std::string art = tempPath("c_vm.scriptc");
  compileTo("c_vm.script", "int64 f() { return 7; }", art);

  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadCompiled(art, errors));
  EXPECT_TRUE(m.isVMEnabled());
}

TEST(CompiledTest, RejectsBadArtifacts) {
  ScriptManager m;
  std::vector<CompilationError> errors;

  std::string missing = tempPath("c_missing.scriptc");
  EXPECT_FALSE(m.loadCompiled(missing, errors));

  std::string junk = writeTemp("c_junk.scriptc", "not an artifact");
  errors.clear();
  EXPECT_FALSE(m.loadCompiled(junk, errors));

  std::string trunc = tempPath("c_trunc.scriptc");
  {
    std::ofstream out(trunc, std::ios::binary);
    uint32_t magic = 0x43585343, ver = 1;
    out.write(reinterpret_cast<const char *>(&magic), 4);
    out.write(reinterpret_cast<const char *>(&ver), 4);
  }
  errors.clear();
  EXPECT_FALSE(m.loadCompiled(trunc, errors));
}

TEST(CompiledTest, SaveRequiresLoadedFile) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  EXPECT_FALSE(m.saveCompiled("nonexistent.script", tempPath("c_x.scriptc"),
                              errors));
}
