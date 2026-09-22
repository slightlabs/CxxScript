// Coverage for the ScriptManager host-facing API: procedure introspection,
// argument conversion, error message formatting, evaluateSnippet semantics,
// checkScript, external variable lifecycle, imports, and CompilationError.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include "ScriptManager.h"
#include <gtest/gtest.h>

using namespace Script;

namespace {
std::string writeTemp(const std::string &name, const std::string &content) {
  std::string path = (std::filesystem::temp_directory_path() / name).string();
  std::ofstream f(path);
  f << content;
  return path;
}
} // namespace

// --- Procedure introspection ------------------------------------------------

TEST(HostApiTest, ProcedureInfoFields) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int64 add3(int32 a, int32 b, string label) { return a + b; }",
      "math.script", errors));

  ScriptManager::ProcedureInfo info;
  ASSERT_TRUE(m.getProcedureInfo("add3", info));
  EXPECT_EQ(info.name, "add3");
  EXPECT_EQ(info.filename, "math.script");
  EXPECT_EQ(info.returnType.baseType, DataType::INT64);
  ASSERT_EQ(info.parameters.size(), 3u);
  EXPECT_EQ(info.parameters[0].name, "a");
  EXPECT_EQ(info.parameters[0].type.baseType, DataType::INT32);
  EXPECT_EQ(info.parameters[2].type.baseType, DataType::STRING);
}

TEST(HostApiTest, ProcedureInfoMissingReturnsFalse) {
  ScriptManager m;
  ScriptManager::ProcedureInfo info;
  EXPECT_FALSE(m.getProcedureInfo("nope", info));
  EXPECT_FALSE(m.hasProcedure("nope"));
}

TEST(HostApiTest, ProcedureNamesListed) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int32 a() { return 1; } int32 b() { return 2; }", "t.script", errors));
  auto names = m.getProcedureNames();
  EXPECT_EQ(names.size(), 2u);
  EXPECT_NE(std::find(names.begin(), names.end(), "a"), names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), "b"), names.end());
}

// --- executeProcedure --------------------------------------------------------

TEST(HostApiTest, ExecuteConvertsArguments) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int64 f(int64 x) { return x * 2; }", "t.script", errors));
  Value result;
  std::string msg;
  // int32 host value converts up to the int64 parameter.
  ASSERT_TRUE(m.executeProcedure("f", {static_cast<int32_t>(21)}, result, msg))
      << msg;
  EXPECT_EQ(std::get<int64_t>(result), 42);
}

TEST(HostApiTest, ExecuteWrongArgCountFails) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int32 f(int32 a, int32 b) { return a + b; }", "t.script", errors));
  Value result;
  std::string msg;
  EXPECT_FALSE(
      m.executeProcedure("f", {static_cast<int32_t>(1)}, result, msg));
  EXPECT_NE(msg.find("argument"), std::string::npos) << msg;
}

TEST(HostApiTest, ExecuteMissingProcedureFails) {
  ScriptManager m;
  Value result;
  std::string msg;
  EXPECT_FALSE(m.executeProcedure("ghost", {}, result, msg));
  EXPECT_NE(msg.find("ghost"), std::string::npos);
}

TEST(HostApiTest, ExecuteVoidProcedureReturnsZero) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource("void f() { }", "t.script", errors));
  Value result;
  std::string msg;
  ASSERT_TRUE(m.executeProcedure("f", {}, result, msg)) << msg;
  EXPECT_EQ(std::get<int32_t>(result), 0);
}

TEST(HostApiTest, ExecuteReturnsContainersToHost) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int32[] f() { return [3, 1, 2]; }", "t.script", errors));
  Value result;
  std::string msg;
  ASSERT_TRUE(m.executeProcedure("f", {}, result, msg)) << msg;
  ASSERT_TRUE(ValueHelper::isArray(result));
  EXPECT_EQ(ValueHelper::toString(result), "[3, 1, 2]");
}

TEST(HostApiTest, HostArrayArgumentSharesStorage) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "void bump(int32[] a) { a[0] += 1; }", "t.script", errors));
  ArrayPtr arr = ValueHelper::createArray(
      TypeInfo(DataType::INT32),
      {static_cast<int32_t>(1), static_cast<int32_t>(2)});
  Value result;
  std::string msg;
  ASSERT_TRUE(m.executeProcedure("bump", {arr}, result, msg)) << msg;
  // Script mutation is visible through the host's ArrayPtr.
  EXPECT_EQ(std::get<int32_t>(arr->elements[0]), 2);
}

TEST(HostApiTest, RuntimeErrorMessageContainsLocation) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int32 boom() { return 1 / 0; }", "err.script", errors));
  Value result;
  std::string msg;
  EXPECT_FALSE(m.executeProcedure("boom", {}, result, msg));
  EXPECT_NE(msg.find("err.script"), std::string::npos) << msg;
  EXPECT_NE(msg.find("boom"), std::string::npos) << msg;
}

TEST(HostApiTest, NestedErrorProducesStackTrace) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int32 inner() { return 1 / 0; }\n"
      "int32 outer() { return inner(); }",
      "err.script", errors));
  Value result;
  std::string msg;
  EXPECT_FALSE(m.executeProcedure("outer", {}, result, msg));
  EXPECT_NE(msg.find("Stack trace"), std::string::npos) << msg;
  EXPECT_NE(msg.find("inner"), std::string::npos) << msg;
  EXPECT_NE(msg.find("outer"), std::string::npos) << msg;
}

TEST(HostApiTest, UncaughtScriptExceptionMessage) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int32 f() { throw \"oops\"; return 0; }", "t.script", errors));
  Value result;
  std::string msg;
  EXPECT_FALSE(m.executeProcedure("f", {}, result, msg));
  EXPECT_NE(msg.find("Uncaught script exception"), std::string::npos) << msg;
  EXPECT_NE(msg.find("oops"), std::string::npos) << msg;
}

// --- checkScript -------------------------------------------------------------

TEST(HostApiTest, CheckScriptSourceDoesNotLoad) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  EXPECT_TRUE(m.checkScriptSource("int32 f() { return 1; }", "t.script",
                                  errors));
  // check-only mode validates but does not register procedures.
  EXPECT_FALSE(m.hasProcedure("f"));
}

TEST(HostApiTest, CheckScriptSourceReportsErrors) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  EXPECT_FALSE(
      m.checkScriptSource("int32 f() { return \"x\"; }", "bad.script", errors));
  ASSERT_FALSE(errors.empty());
  EXPECT_EQ(errors[0].filename, "bad.script");
  EXPECT_FALSE(errors[0].isWarning);
}

TEST(HostApiTest, CheckScriptOnFile) {
  std::string path = writeTemp("hostapi_check_ok.script",
                               "int32 f() { return 1; }");
  ScriptManager m;
  std::vector<CompilationError> errors;
  EXPECT_TRUE(m.checkScript(path, errors));

  std::string bad = writeTemp("hostapi_check_bad.script",
                              "int32 f() { return \"s\"; }");
  EXPECT_FALSE(m.checkScript(bad, errors));
  std::filesystem::remove(path);
  std::filesystem::remove(bad);
}

TEST(HostApiTest, WarningsDontFailCheckOrLoad) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  EXPECT_TRUE(m.checkScriptSource(
      "int32 f() { int32 unused = 1; return 0; }", "t.script", errors));
  EXPECT_TRUE(std::any_of(errors.begin(), errors.end(),
                          [](const CompilationError &e) {
                            return e.isWarning;
                          }));
}

// --- CompilationError ---------------------------------------------------------

TEST(HostApiTest, CompilationErrorToStringFormat) {
  CompilationError err("bad thing", "f.script", "proc", 3, 7, false);
  EXPECT_EQ(err.toString(),
            "f.script:3:7: error: bad thing in procedure 'proc'");
  CompilationError warn("careful", "f.script", "", 1, 2, true);
  EXPECT_EQ(warn.toString(), "f.script:1:2: warning: careful");
}

// --- evaluateSnippet ---------------------------------------------------------

TEST(HostApiTest, SnippetReturnExpressionYieldsValue) {
  ScriptManager m;
  Value v;
  std::string msg;
  ASSERT_TRUE(m.evaluateSnippet("return 6 * 7;", "repl", v, msg)) << msg;
  EXPECT_EQ(std::get<int32_t>(v), 42);
}

TEST(HostApiTest, SnippetWithoutReturnValueZero) {
  ScriptManager m;
  Value v;
  std::string msg;
  ASSERT_TRUE(m.evaluateSnippet("int32 x = 5;", "repl", v, msg)) << msg;
  EXPECT_EQ(std::get<int32_t>(v), 0);
}

TEST(HostApiTest, SnippetGlobalsVisibleToProceduresAndViceVersa) {
  ScriptManager m;
  Value v;
  std::string msg;
  ASSERT_TRUE(m.evaluateSnippet("int32 g = 10;", "repl", v, msg)) << msg;
  // A later snippet can read the earlier global and call a loaded proc.
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int32 dbl(int32 x) { return x * 2; }", "t.script", errors));
  ASSERT_TRUE(m.evaluateSnippet("return dbl(g);", "repl", v, msg)) << msg;
  EXPECT_EQ(std::get<int32_t>(v), 20);
}

TEST(HostApiTest, SnippetControlFlowRuns) {
  ScriptManager m;
  Value v;
  std::string msg;
  ASSERT_TRUE(m.evaluateSnippet(
      "int32 total = 0; for (int32 i = 1; i <= 4; i += 1) { total += i; } "
      "return total;",
      "repl", v, msg))
      << msg;
  EXPECT_EQ(std::get<int32_t>(v), 10);
}

TEST(HostApiTest, SnippetRuntimeErrorReported) {
  ScriptManager m;
  Value v;
  std::string msg;
  EXPECT_FALSE(m.evaluateSnippet("return 1 / 0;", "repl", v, msg));
  EXPECT_NE(msg.find("Runtime error"), std::string::npos) << msg;
}

TEST(HostApiTest, SnippetUncaughtExceptionReported) {
  ScriptManager m;
  Value v;
  std::string msg;
  EXPECT_FALSE(m.evaluateSnippet("throw \"bad\";", "repl", v, msg));
  EXPECT_NE(msg.find("Uncaught script exception"), std::string::npos) << msg;
}

TEST(HostApiTest, SnippetCompileErrorReported) {
  ScriptManager m;
  Value v;
  std::string msg;
  EXPECT_FALSE(m.evaluateSnippet("int32 x = ;", "repl", v, msg));
  EXPECT_FALSE(msg.empty());
}

TEST(HostApiTest, SnippetSeesStructAndEnum) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "struct P { int32 x; }\nenum E { A, B }", "t.script", errors));
  Value v;
  std::string msg;
  ASSERT_TRUE(m.evaluateSnippet("P p = P(9); return p.x + E.B;", "repl", v, msg))
      << msg;
  EXPECT_EQ(std::get<int64_t>(v), 10); // int32 + int64(enum) -> int64
}

TEST(HostApiTest, SnippetConstRespected) {
  ScriptManager m;
  Value v;
  std::string msg;
  ASSERT_TRUE(m.evaluateSnippet("const int32 K = 3;", "repl", v, msg)) << msg;
  EXPECT_FALSE(m.evaluateSnippet("K = 4;", "repl", v, msg));
}

TEST(HostApiTest, SnippetSeesExternalVariable) {
  ScriptManager m;
  int32_t host = 5;
  m.registerExternalVariable(
      "ext", [&]() -> Value { return static_cast<int32_t>(host); },
      [&](const Value &val) { host = std::get<int32_t>(val); });
  Value v;
  std::string msg;
  ASSERT_TRUE(m.evaluateSnippet("ext += 10; return ext;", "repl", v, msg))
      << msg;
  EXPECT_EQ(std::get<int32_t>(v), 15);
  EXPECT_EQ(host, 15);
}

// --- clear() -----------------------------------------------------------------

TEST(HostApiTest, ClearRemovesProceduresStructsEnumsAndReplGlobals) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "struct P { int32 x; }\nenum E { A }\nint32 f() { return 1; }",
      "t.script", errors));
  Value v;
  std::string msg;
  ASSERT_TRUE(m.evaluateSnippet("int32 g = 1;", "repl", v, msg)) << msg;

  m.clear();
  EXPECT_FALSE(m.hasProcedure("f"));
  EXPECT_TRUE(m.getProcedureNames().empty());
  // Snippet global is forgotten: reusing it must fail to compile.
  EXPECT_FALSE(m.evaluateSnippet("return g;", "repl", v, msg));
  // Struct is gone too.
  EXPECT_FALSE(m.evaluateSnippet("P p = P(1); return 0;", "repl", v, msg));
}

TEST(HostApiTest, ManagersAreIndependent) {
  ScriptManager m1, m2;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m1.loadScriptSource("int32 f() { return 1; }", "t.script",
                                  errors));
  EXPECT_FALSE(m2.hasProcedure("f"));
}

// --- External variables --------------------------------------------------------

TEST(HostApiTest, UnregisterExternalVariable) {
  ScriptManager m;
  m.registerExternalVariable("ext",
                             []() -> Value { return static_cast<int32_t>(7); });
  EXPECT_TRUE(m.hasExternalVariable("ext"));
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource("int32 f() { return ext; }", "t.script",
                                 errors));
  Value v;
  std::string msg;
  ASSERT_TRUE(m.executeProcedure("f", {}, v, msg)) << msg;

  m.unregisterExternalVariable("ext");
  EXPECT_FALSE(m.hasExternalVariable("ext"));
  // Already-compiled code now fails at runtime.
  EXPECT_FALSE(m.executeProcedure("f", {}, v, msg));
  // A script using an unregistered name still compiles (names resolve
  // dynamically) but fails at runtime.
  ScriptManager m2;
  ASSERT_TRUE(m2.loadScriptSource("int32 g() { return ext; }", "t.script",
                                  errors));
  msg.clear();
  EXPECT_FALSE(m2.executeProcedure("g", {}, v, msg));
  EXPECT_NE(msg.find("ext"), std::string::npos) << msg;
}

TEST(HostApiTest, ReadOnlyExternalRejectsCompoundAssign) {
  ScriptManager m;
  m.registerExternalVariableReadOnly(
      "ro", []() -> Value { return static_cast<int32_t>(1); });
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource("void f() { ro += 1; }", "t.script", errors));
  Value v;
  std::string msg;
  EXPECT_FALSE(m.executeProcedure("f", {}, v, msg));
  EXPECT_NE(msg.find("read-only"), std::string::npos) << msg;
}

TEST(HostApiTest, ExternalGetterThrowSurfacesAsRuntimeError) {
  ScriptManager m;
  m.registerExternalVariable("bad", []() -> Value {
    throw std::runtime_error("getter blew up");
  });
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource("int32 f() { return bad; }", "t.script",
                                 errors));
  Value v;
  std::string msg;
  EXPECT_FALSE(m.executeProcedure("f", {}, v, msg));
  EXPECT_NE(msg.find("getter blew up"), std::string::npos) << msg;
}

TEST(HostApiTest, ExternalSetterThrowSurfacesAsRuntimeError) {
  ScriptManager m;
  m.registerExternalVariable(
      "bad", []() -> Value { return static_cast<int32_t>(0); },
      [](const Value &) { throw std::runtime_error("setter blew up"); });
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource("void f() { bad = 1; }", "t.script", errors));
  Value v;
  std::string msg;
  EXPECT_FALSE(m.executeProcedure("f", {}, v, msg));
  EXPECT_NE(msg.find("setter blew up"), std::string::npos) << msg;
}

// --- External functions: typed helpers -----------------------------------------

static int32_t squareFn(int32_t x) { return x * x; }

TEST(HostApiTest, TypedExternalStringAndBoolArgs) {
  ScriptManager m;
  m.registerExternalFunction("shout",
                             std::function<std::string(std::string, bool)>(
                                 [](std::string s, bool up) {
                                   if (up) {
                                     for (auto &c : s) c = ::toupper(c);
                                   }
                                   return s;
                                 }));
  m.registerExternalFunction("sq", &squareFn);
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "string f() { return shout(\"hi\", true) + toString(sq(4)); }",
      "t.script", errors));
  Value v;
  std::string msg;
  ASSERT_TRUE(m.executeProcedure("f", {}, v, msg)) << msg;
  EXPECT_EQ(std::get<std::string>(v), "HI16");
}

TEST(HostApiTest, TypedExternalArrayArg) {
  ScriptManager m;
  m.registerExternalFunction("sumArr",
                             std::function<int64_t(ArrayPtr)>([](ArrayPtr a) {
                               int64_t s = 0;
                               for (const auto &e : a->elements)
                                 s += ValueHelper::toInt64(e);
                               return s;
                             }));
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int64 f() { return sumArr([1, 2, 3]); }", "t.script", errors));
  Value v;
  std::string msg;
  ASSERT_TRUE(m.executeProcedure("f", {}, v, msg)) << msg;
  EXPECT_EQ(std::get<int64_t>(v), 6);
}

TEST(HostApiTest, TypedExternalHostExceptionSurfaces) {
  ScriptManager m;
  m.registerExternalFunction("kaboom",
                             std::function<int32_t(int32_t)>([](int32_t) -> int32_t {
                               throw std::runtime_error("host exploded");
                             }));
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int32 f() { return kaboom(1); }", "t.script", errors));
  Value v;
  std::string msg;
  EXPECT_FALSE(m.executeProcedure("f", {}, v, msg));
  EXPECT_NE(msg.find("host exploded"), std::string::npos) << msg;
}

TEST(HostApiTest, ExternalFunctionRegisteredAfterCompileStillCallable) {
  // External functions resolve through the call path, so registration after
  // load works if the validator saw the name... verify actual behavior:
  // a script that only *declares* a call compiles only if the name is known,
  // so this test registers first, unregisters, re-registers.
  ScriptManager m;
  m.registerExternalFunction("f1", [](const std::vector<Value> &) -> Value {
    return static_cast<int32_t>(1);
  });
  std::vector<CompilationError> errors;
  ASSERT_TRUE(
      m.loadScriptSource("int32 g() { return f1(); }", "t.script", errors));
  m.unregisterExternalFunction("f1");
  EXPECT_FALSE(m.hasExternalFunction("f1"));
  Value v;
  std::string msg;
  EXPECT_FALSE(m.executeProcedure("g", {}, v, msg));
  m.registerExternalFunction("f1", [](const std::vector<Value> &) -> Value {
    return static_cast<int32_t>(99);
  });
  ASSERT_TRUE(m.executeProcedure("g", {}, v, msg)) << msg;
  EXPECT_EQ(std::get<int32_t>(v), 99);
}

// --- Output callback -----------------------------------------------------------

TEST(HostApiTest, OutputCallbackCanBeReplaced) {
  ScriptManager m;
  std::string a, b;
  m.setOutputCallback([&a](const std::string &s) { a += s; });
  m.setOutputCallback([&b](const std::string &s) { b += s; });
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource("void f() { print(\"x\"); }", "t.script",
                                 errors));
  Value v;
  std::string msg;
  ASSERT_TRUE(m.executeProcedure("f", {}, v, msg)) << msg;
  EXPECT_TRUE(a.empty());
  EXPECT_EQ(b, "x");
}

// --- Imports -----------------------------------------------------------------

TEST(HostApiTest, TransitiveImportChain) {
  std::string c = writeTemp("hostapi_c.script",
                            "int32 deep() { return 7; }");
  std::string b = writeTemp("hostapi_b.script",
                            "import \"hostapi_c.script\";\n"
                            "int32 mid() { return deep() + 1; }");
  std::string a = writeTemp("hostapi_a.script",
                            "import \"hostapi_b.script\";\n"
                            "int32 top() { return mid() + 1; }");
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptFile(a, errors));
  Value v;
  std::string msg;
  ASSERT_TRUE(m.executeProcedure("top", {}, v, msg)) << msg;
  EXPECT_EQ(std::get<int32_t>(v), 9);
  std::filesystem::remove(a);
  std::filesystem::remove(b);
  std::filesystem::remove(c);
}

TEST(HostApiTest, ImportEnumFromOtherFile) {
  std::string lib = writeTemp("hostapi_enum.script",
                              "enum Dir { N, S, E, W }");
  std::string main = writeTemp("hostapi_enum_main.script",
                               "import \"hostapi_enum.script\";\n"
                               "int64 f() { return Dir.E; }");
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptFile(main, errors));
  Value v;
  std::string msg;
  ASSERT_TRUE(m.executeProcedure("f", {}, v, msg)) << msg;
  EXPECT_EQ(std::get<int64_t>(v), 2);
  std::filesystem::remove(lib);
  std::filesystem::remove(main);
}

TEST(HostApiTest, SameFileLoadedTwiceIsIdempotent) {
  std::string path =
      writeTemp("hostapi_twice.script", "int32 f() { return 1; }");
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptFile(path, errors));
  ASSERT_TRUE(m.loadScriptFile(path, errors)); // second load is a no-op
  Value v;
  std::string msg;
  ASSERT_TRUE(m.executeProcedure("f", {}, v, msg)) << msg;
  std::filesystem::remove(path);
}

TEST(HostApiTest, ReloadOnNeverLoadedFileLoadsIt) {
  std::string path =
      writeTemp("hostapi_reload_fresh.script", "int32 f() { return 5; }");
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.reloadScriptFile(path, errors));
  EXPECT_TRUE(m.hasProcedure("f"));
  std::filesystem::remove(path);
}

TEST(HostApiTest, ReloadMissingFileFails) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  EXPECT_FALSE(m.reloadScriptFile("/nonexistent/nowhere.script", errors));
  ASSERT_FALSE(errors.empty());
  EXPECT_NE(errors[0].message.find("Failed to open file"), std::string::npos);
}

// --- Sandbox controls ----------------------------------------------------------

TEST(HostApiTest, IsBuiltinEnabledReflectsDisableEnable) {
  ScriptManager m;
  EXPECT_TRUE(m.isBuiltinEnabled("print"));
  m.disableBuiltin("print");
  EXPECT_FALSE(m.isBuiltinEnabled("print"));
  m.enableBuiltin("print");
  EXPECT_TRUE(m.isBuiltinEnabled("print"));
}

TEST(HostApiTest, ImportRootsClearedRestoresAnywhere) {
  std::string lib =
      writeTemp("hostapi_root_lib.script", "int32 f() { return 1; }");
  std::string main = writeTemp("hostapi_root_main.script",
                               "import \"hostapi_root_lib.script\";\n"
                               "int32 g() { return f(); }");
  ScriptManager m;
  m.addImportRoot("/nonexistent_root");
  std::vector<CompilationError> errors;
  EXPECT_FALSE(m.loadScriptFile(main, errors));
  errors.clear();
  m.clearImportRoots();
  EXPECT_TRUE(m.loadScriptFile(main, errors));
  std::filesystem::remove(lib);
  std::filesystem::remove(main);
}

// --- External variable holding a function value ------------------------------

TEST(HostApiTest, ExternalVariableHoldingFunctionIsCallable) {
  // A FuncPtr stored in an external variable is invoked when the script
  // calls the variable like a procedure.
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "fn(int32)->int32 mk() { return fn(int32 x) -> int32 { return x * 3; }; }",
      "t.script", errors));
  Value fnVal;
  std::string err;
  ASSERT_TRUE(m.executeProcedure("mk", {}, fnVal, err)) << err;
  ASSERT_TRUE(std::holds_alternative<FuncPtr>(fnVal));

  FuncPtr fn = std::get<FuncPtr>(fnVal);
  m.registerExternalVariableReadOnly("cb", [fn]() { return fn; });

  ASSERT_TRUE(m.loadScriptSource(
      "int32 call() { return cb(5); }", "u.script", errors));
  Value v;
  ASSERT_TRUE(m.executeProcedure("call", {}, v, err)) << err;
  EXPECT_EQ(std::get<int32_t>(v), 15);
}

TEST(HostApiTest, ExternalSettersConvertEachNumericType) {
  // The setter conversion switch must handle every scalar alternative.
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource("void w(int32 v) { host = v; }", "t.script",
                                 errors));

  int64_t seen = 0;
  int conversions = 0;
  auto setter = [&](const Value &v) {
    ++conversions;
    std::visit(
        [&](auto &&x) {
          using T = std::decay_t<decltype(x)>;
          if constexpr (std::is_arithmetic_v<T>) {
            seen = static_cast<int64_t>(x);
          } else if constexpr (std::is_same_v<T, char>) {
            seen = static_cast<int64_t>(x);
          }
        },
        v);
  };
  m.registerExternalVariable("host", []() { return static_cast<int64_t>(0); },
                             setter);

  // No cast syntax exists — each type comes from a declared literal.
  for (const char *decl :
       {"int8", "uint8", "int16", "uint16", "int32", "uint32", "int64",
        "uint64", "char"}) {
    std::vector<CompilationError> errs;
    std::string src =
        std::string("void w() { ") + decl + " v = 7; host = v; }";
    ASSERT_TRUE(m.loadScriptSource(src, "s.script", errs));
    Value v;
    std::string err;
    ASSERT_TRUE(m.executeProcedure("w", {}, v, err)) << decl << ": " << err;
    EXPECT_EQ(seen, 7) << decl;
  }
  EXPECT_GE(conversions, 9);
}

TEST(HostApiTest, UncaughtExceptionCarriesLocationAndTrace) {
  // Uncaught script exceptions format file, line, and a stack trace.
  std::string path = writeTemp("hostapi_throw.script", R"(
int32 inner() { throw "boom"; }
int32 outer() { return inner(); }
)");
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptFile(path, errors));
  Value v;
  std::string err;
  ASSERT_FALSE(m.executeProcedure("outer", {}, v, err));
  EXPECT_NE(err.find("Uncaught script exception"), std::string::npos) << err;
  EXPECT_NE(err.find("boom"), std::string::npos) << err;
  EXPECT_NE(err.find("hostapi_throw.script"), std::string::npos) << err;
  EXPECT_NE(err.find("inner"), std::string::npos) << err;
  std::filesystem::remove(path);
}

TEST(HostApiTest, ReentrantExecutionSurfacesUncaughtException) {
  // A host callback that re-enters executeProcedure mid-execution hits the
  // ScriptException path: the inner call isn't top-level, so the script
  // exception propagates and is reported as "Uncaught exception".
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int32 bad() { throw \"kaput\"; }\n"
      "int32 caller() { reenter(); return 1; }",
      "t.script", errors));

  std::string innerErr;
  m.registerExternalFunction(
      "reenter", [&](const std::vector<Value> &) -> Value {
        Value v;
        std::string e;
        EXPECT_FALSE(m.executeProcedure("bad", {}, v, e));
        innerErr = e;
        return static_cast<int32_t>(0);
      });

  Value v;
  std::string err;
  ASSERT_TRUE(m.executeProcedure("caller", {}, v, err)) << err;
  EXPECT_NE(innerErr.find("Uncaught exception"), std::string::npos) << innerErr;
  EXPECT_NE(innerErr.find("kaput"), std::string::npos) << innerErr;
}

// --- Debug hook --------------------------------------------------------------

TEST(HostApiTest, DebugHookReceivesContext) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(R"(
int32 helper(int32 x) { int32 doubled = x * 2; return doubled; }
int32 f() { int32 a = 3; return helper(a); })",
                                 "dbg.script", errors));

  std::vector<Interpreter::DebugContext> seen;
  m.setDebugHook(
      [&seen](const Interpreter::DebugContext &ctx) { seen.push_back(ctx); });

  Value v;
  std::string err;
  ASSERT_TRUE(m.executeProcedure("f", {}, v, err)) << err;
  EXPECT_EQ(std::get<int32_t>(v), 6);

  ASSERT_FALSE(seen.empty());
  bool sawHelper = false;
  for (const auto &c : seen) {
    EXPECT_EQ(c.filename, "dbg.script");
    EXPECT_FALSE(c.procedure.empty());
    EXPECT_GT(c.line, 0);
    if (c.procedure == "helper") {
      sawHelper = true;
      EXPECT_GE(c.callDepth, 1u);
      ASSERT_FALSE(c.callStack.empty());
      EXPECT_EQ(c.callStack.back(), "helper");
      // The helper's local/parameter should be visible.
      EXPECT_TRUE(c.variables.count("x") || c.variables.count("doubled"));
    }
  }
  EXPECT_TRUE(sawHelper) << "hook never saw the helper procedure";

  m.setDebugHook(nullptr); // nullptr detaches without crashing
}

// --- callProcedure<Ret>(name, args...) --------------------------------------

TEST(HostApiTest, CallProcedureTyped) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int64 add(int32 a, int32 b) { return a + b; }\n"
      "string greet(string who) { return \"hi \" + who; }\n"
      "double half(int32 n) { return n / 2.0; }\n"
      "void reset() { }\n",
      "typed_call.script", errors));

  EXPECT_EQ(m.callProcedure<int64_t>("add", 2, 3), 5);
  EXPECT_EQ(m.callProcedure<std::string>("greet", std::string("bo")), "hi bo");
  EXPECT_DOUBLE_EQ(m.callProcedure<double>("half", 7), 3.5);
  m.callProcedure<void>("reset"); // no throw
}

TEST(HostApiTest, CallProcedureConvertsArgs) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int64 total(int64 a, int64 b, int64 c) { return a + b + c; }",
      "conv_call.script", errors));
  // int literals → int64 params; mixed widths convert.
  EXPECT_EQ(m.callProcedure<int64_t>("total", int32_t(1), int64_t(2), 3), 6);
}

TEST(HostApiTest, CallProcedureThrowsOnError) {
  ScriptManager m;
  std::vector<CompilationError> errors;
  ASSERT_TRUE(m.loadScriptSource(
      "int32 boom() { int32 z = 1 / 0; return z; }\n"
      "int32 ok() { return 1; }",
      "throwing.script", errors));
  EXPECT_THROW(m.callProcedure<int32_t>("boom"), std::runtime_error);
  EXPECT_THROW(m.callProcedure<int32_t>("missing"), std::runtime_error);
}
