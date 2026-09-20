#include <filesystem>
#include <fstream>

#include "ScriptManager.h"
#include <gtest/gtest.h>

using namespace Script;

namespace {
std::string writeTemp(const std::string &name, const std::string &source) {
  std::string path = (std::filesystem::temp_directory_path() / name).string();
  std::ofstream out(path);
  out << source;
  return path;
}

Value callProc(ScriptManager &m, const std::string &proc,
               const std::vector<Value> &args = {}) {
  Value result;
  std::string err;
  EXPECT_TRUE(m.executeProcedure(proc, args, result, err)) << err;
  return result;
}
} // namespace

TEST(HotReloadTest, ReloadSwapsProcedureBody) {
  std::string file = writeTemp("hr1.script", "int32 f() { return 1; }");

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptFile(file, errors));
  EXPECT_EQ(std::get<int32_t>(callProc(manager, "f")), 1);

  writeTemp("hr1.script", "int32 f() { return 42; }");
  ASSERT_TRUE(manager.reloadScriptFile(file, errors));
  EXPECT_EQ(std::get<int32_t>(callProc(manager, "f")), 42);
}

TEST(HotReloadTest, ExternalFunctionsSurviveReload) {
  std::string file =
      writeTemp("hr2.script", "int32 f() { return host(3); }");

  ScriptManager manager;
  manager.registerExternalFunction("host", [](int32_t x) { return x * 10; });

  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptFile(file, errors));
  EXPECT_EQ(std::get<int32_t>(callProc(manager, "f")), 30);

  writeTemp("hr2.script", "int32 f() { return host(5) + 1; }");
  ASSERT_TRUE(manager.reloadScriptFile(file, errors));
  EXPECT_EQ(std::get<int32_t>(callProc(manager, "f")), 51);
}

TEST(HotReloadTest, RemovedProcedureIsUnloaded) {
  std::string file = writeTemp("hr3.script",
                               "int32 f() { return 1; }\n"
                               "int32 gone() { return 2; }\n");

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptFile(file, errors));
  EXPECT_TRUE(manager.hasProcedure("gone"));

  writeTemp("hr3.script", "int32 f() { return 1; }\n");
  ASSERT_TRUE(manager.reloadScriptFile(file, errors));
  EXPECT_FALSE(manager.hasProcedure("gone"));
  EXPECT_TRUE(manager.hasProcedure("f"));
}

TEST(HotReloadTest, FailedReloadKeepsOldProcedures) {
  std::string file = writeTemp("hr4.script", "int32 f() { return 7; }");

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptFile(file, errors));

  writeTemp("hr4.script", "int32 f() { return ; }"); // syntax error
  EXPECT_FALSE(manager.reloadScriptFile(file, errors));

  // Old version still callable
  EXPECT_EQ(std::get<int32_t>(callProc(manager, "f")), 7);
}

TEST(HotReloadTest, ReloadSeesOtherFilesProcedures) {
  std::string dep = writeTemp("hr_dep.script", "int32 helper() { return 3; }");
  std::string main = writeTemp("hr5.script",
                               "import \"hr_dep.script\";\n"
                               "int32 f() { return helper() * 2; }\n");

  ScriptManager manager;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(manager.loadScriptFile(main, errors));
  EXPECT_EQ(std::get<int32_t>(callProc(manager, "f")), 6);

  // Reload main: imported helper still resolves
  writeTemp("hr5.script",
            "import \"hr_dep.script\";\nint32 f() { return helper() + 10; }\n");
  ASSERT_TRUE(manager.reloadScriptFile(main, errors));
  EXPECT_EQ(std::get<int32_t>(callProc(manager, "f")), 13);
}
