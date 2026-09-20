#pragma once

#include "Interpreter.h"
#include "Lexer.h"
#include "Parser.h"
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Script {

struct CompilationError {
  std::string message;
  std::string filename;
  std::string procedureName;
  int line;
  int column;
  bool isWarning;

  CompilationError(const std::string &msg, const std::string &file,
                   const std::string &proc, int ln, int col,
                   bool warning = false)
      : message(msg), filename(file), procedureName(proc), line(ln),
        column(col), isWarning(warning) {}

  std::string toString() const;
};

class ScriptManager {
public:
  ScriptManager();
  ~ScriptManager();

  // Load and compile a script file
  bool loadScriptFile(const std::string &filename,
                      std::vector<CompilationError> &errors);

  // Load and compile script from source code
  bool loadScriptSource(const std::string &source, const std::string &filename,
                        std::vector<CompilationError> &errors);

  // Check if script compiles without errors
  bool checkScript(const std::string &filename,
                   std::vector<CompilationError> &errors);
  bool checkScriptSource(const std::string &source, const std::string &filename,
                         std::vector<CompilationError> &errors);

  // Hot reload: recompile a file and swap in its procedures. External
  // functions/variables, REPL globals, and procedures from other files are
  // preserved. If compilation fails the old procedures stay in place.
  // Procedures deleted from the new version are unloaded.
  bool reloadScriptFile(const std::string &filename,
                        std::vector<CompilationError> &errors);

  // Execute a procedure from any loaded script
  bool executeProcedure(const std::string &procedureName,
                        const std::vector<Value> &arguments, Value &returnValue,
                        std::string &errorMessage);

  // Evaluate a snippet of top-level statements (REPL support). Variables
  // declared at top level persist as globals across calls; a top-level
  // 'return <expr>' yields the expression's value in returnValue.
  bool evaluateSnippet(const std::string &source, const std::string &name,
                       Value &returnValue, std::string &errorMessage);

  // Check if a procedure exists
  bool hasProcedure(const std::string &name) const;

  // Get list of all loaded procedures
  std::vector<std::string> getProcedureNames() const;

  // Get procedure signature information
  struct ProcedureInfo {
    std::string name;
    TypeInfo returnType;
    std::vector<Parameter> parameters;
    std::string filename;
  };

  bool getProcedureInfo(const std::string &name, ProcedureInfo &info) const;

  // Register an external function that can be called from scripts
  void registerExternalFunction(const std::string &name,
                                ExternalFunctionCallback callback);

  // Register multiple external functions at once
  void registerExternalFunctions(const std::vector<ExternalBinding> &bindings);

  // Register multiple external functions via initializer list
  void registerExternalFunctions(
      std::initializer_list<ExternalBinding> bindings);

  // Unregister an external function
  void unregisterExternalFunction(const std::string &name);

  // Check if an external function is registered
  bool hasExternalFunction(const std::string &name) const;

  // Register an external variable that scripts can read/write
  void registerExternalVariable(const std::string &name,
                                ExternalVariableGetter getter,
                                ExternalVariableSetter setter = nullptr);

  // Register a read-only external variable (setter omitted)
  void registerExternalVariableReadOnly(const std::string &name,
                                         ExternalVariableGetter getter);

  // Typed helpers for common unary/binary external functions
  template <typename Ret, typename Arg>
  void registerExternalFunctionUnary(const std::string &name,
                                     std::function<Ret(Arg)> fn);

  template <typename Ret, typename Arg1, typename Arg2>
  void registerExternalFunctionBinary(const std::string &name,
                                      std::function<Ret(Arg1, Arg2)> fn);

  // Register a typed external function. Arity and argument types are deduced
  // automatically; conversions use the same rules as procedure parameters.
  //   manager.registerExternalFunction("add",
  //       std::function<int32_t(int32_t, int32_t)>([](int32_t a, int32_t b) {
  //         return a + b;
  //       }));
  //   manager.registerExternalFunction("sq", &squareFn);   // free function
  //   manager.registerExternalFunction("inc", [](int64_t n) { return n + 1; });
  //
  // Supported types: all integer widths, float, double, bool, char,
  // std::string, ArrayPtr. 'void' return is allowed (scripts see a dummy 0).
  // A std::function returning Value keeps the raw-callback overload.
  template <typename Ret, typename... Args>
  typename std::enable_if<!std::is_same<Ret, Value>::value, void>::type
  registerExternalFunction(const std::string &name,
                           std::function<Ret(Args...)> fn);

  template <typename Ret, typename... Args>
  typename std::enable_if<!std::is_same<Ret, Value>::value, void>::type
  registerExternalFunction(const std::string &name, Ret (*fn)(Args...)) {
    registerExternalFunction(name, std::function<Ret(Args...)>(fn));
  }

  // Generic callable (lambdas, functors) — deduced via std::function CTAD.
  template <typename F>
  auto registerExternalFunction(const std::string &name, F fn)
      -> decltype(std::function{fn}, void()) {
    registerExternalFunction(name, std::function{fn});
  }

  // Unregister an external variable
  void unregisterExternalVariable(const std::string &name);

  // Check if an external variable is registered
  bool hasExternalVariable(const std::string &name) const;

  // Clear all loaded scripts
  void clear();

  // Optional execution guardrails (0 means unlimited)
  void setExecutionLimits(size_t maxCallDepth, size_t maxSteps);
  void clearExecutionLimits();

  // Optional memory guardrails (0 means unlimited):
  //   maxArraySize    - max elements a single array may hold
  //   maxStringLength - max length of any produced string
  //   maxAllocations  - total array allocations per top-level execution
  void setMemoryLimits(size_t maxArraySize, size_t maxStringLength,
                       size_t maxAllocations);
  void clearMemoryLimits();

  // Redirect the print()/println() builtin output. Passing nullptr restores
  // the default (writes to stdout).
  void setOutputCallback(Interpreter::OutputCallback cb);

  // Per-statement debug hook (see Interpreter::DebugContext). nullptr clears.
  void setDebugHook(Interpreter::DebugHook cb);

  // --- Sandbox controls for untrusted scripts ---
  // When import roots are set, `import` paths must resolve inside one of
  // them. When empty (default), imports may resolve anywhere.
  void addImportRoot(const std::string &directory);
  void clearImportRoots();
  // Master switch for `import` statements (default enabled).
  void setImportsEnabled(bool enabled);
  // Disable/enable individual builtins by name. Disabled builtins resolve
  // like undefined functions to scripts.
  void disableBuiltin(const std::string &name);
  void enableBuiltin(const std::string &name);
  bool isBuiltinEnabled(const std::string &name) const;

  // --- Parsed-AST cache ---
  // When enabled, successfully parsed files are cached keyed by canonical
  // path + source hash so recompiles skip lexing/parsing. Semantic
  // validation still runs on every compile. Disabled by default.
  void setAstCacheEnabled(bool enabled);
  bool isAstCacheEnabled() const;
  size_t astCacheSize() const;
  void clearAstCache();

private:
  std::unique_ptr<Interpreter> _interpreter;
  std::unordered_map<std::string, std::string>
      _procedureFiles; // procedure name -> filename
  std::unordered_map<std::string, std::string>
      _structFiles; // struct name -> filename
  std::unordered_map<std::string, std::string>
      _enumFiles; // enum name -> filename
  // variables declared by REPL snippets: name -> (type, isConst)
  std::unordered_map<std::string, std::pair<TypeInfo, bool>> _replGlobalTypes;
  std::unordered_set<std::string> _loadedFiles;  // canonical paths already loaded
  std::unordered_set<std::string> _loadingFiles; // in-progress (cycle detection)
  std::vector<std::string> _importRoots; // canonical allowed import dirs
  bool _importsEnabled = true;
  bool _astCacheEnabled = false;
  struct AstCacheEntry {
    size_t sourceHash;
    ScriptPtr script;
  };
  std::unordered_map<std::string, AstCacheEntry> _astCache; // path -> AST

  bool compileScript(const std::string &source, const std::string &filename,
                     std::vector<CompilationError> &errors, bool load,
                     std::vector<std::string> *loadedProcNames = nullptr);

  // Struct/enum type names visible while parsing `filename`:
  // interpreter-loaded decls plus declarations inside (transitively)
  // imported files.
  std::unordered_set<std::string>
  knownStructNames(const std::string &filename,
                   const std::vector<Token> &tokens);
  std::unordered_set<std::string>
  knownEnumNames(const std::string &filename,
                 const std::vector<Token> &tokens);
  void collectTypeNames(const std::string &filename,
                        std::unordered_set<std::string> &structs,
                        std::unordered_set<std::string> &enums,
                        std::unordered_set<std::string> &visited);
  // True if `resolvedPath` is inside one of _importRoots (both canonical).
  bool importPathAllowed(const std::string &resolvedPath) const;
};

// --- Inline implementations for typed helpers ---

namespace detail {

// Types usable as external-function arguments/returns.
template <typename T>
struct IsSupportedType
    : std::bool_constant<
          std::is_same<T, std::string>::value ||
          std::is_same<T, ArrayPtr>::value ||
          std::is_same<T, bool>::value || std::is_same<T, char>::value ||
          std::is_same<T, float>::value || std::is_same<T, double>::value ||
          (std::is_integral<T>::value && !std::is_same<T, bool>::value)> {};

template <typename T> inline Value toValue(const T &v) {
  if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, ArrayPtr> ||
                std::is_same_v<T, bool> || std::is_same_v<T, char> ||
                std::is_same_v<T, float> || std::is_same_v<T, double> ||
                std::is_same_v<T, int8_t> || std::is_same_v<T, uint8_t> ||
                std::is_same_v<T, int16_t> || std::is_same_v<T, uint16_t> ||
                std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t> ||
                std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t>) {
    return v; // T is a Value variant alternative
  } else if constexpr (std::is_same_v<T, const char *> ||
                       std::is_same_v<T, char *>) {
    return std::string(v);
  } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
    return static_cast<int64_t>(v);
  } else if constexpr (std::is_integral_v<T>) {
    return static_cast<uint64_t>(v);
  } else {
    static_assert(IsSupportedType<T>::value, "Unsupported return type");
  }
}

template <typename T> inline T fromValue(const Value &v) {
  if constexpr (std::is_same_v<T, std::string>) {
    return ValueHelper::toString(v);
  } else if constexpr (std::is_same_v<T, ArrayPtr>) {
    if (!ValueHelper::isArray(v)) {
      throw std::runtime_error("Expected array argument");
    }
    return std::get<ArrayPtr>(v);
  } else if constexpr (std::is_same_v<T, bool>) {
    return ValueHelper::toBool(v);
  } else if constexpr (std::is_same_v<T, char>) {
    return static_cast<char>(ValueHelper::toInt64(v));
  } else if constexpr (std::is_same_v<T, float>) {
    return static_cast<float>(ValueHelper::toDouble(v));
  } else if constexpr (std::is_same_v<T, double>) {
    return ValueHelper::toDouble(v);
  } else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T>) {
    return static_cast<T>(ValueHelper::toUInt64(v));
  } else if constexpr (std::is_integral_v<T>) {
    return static_cast<T>(ValueHelper::toInt64(v));
  } else {
    static_assert(IsSupportedType<T>::value, "Unsupported argument type");
  }
}

template <typename Ret, typename... Args, size_t... I>
Value invokeTyped(const std::function<Ret(Args...)> &fn,
                  const std::vector<Value> &args, std::index_sequence<I...>) {
  if constexpr (std::is_same_v<Ret, void>) {
    fn(fromValue<Args>(args[I])...);
    return static_cast<int32_t>(0);
  } else {
    return toValue(fn(fromValue<Args>(args[I])...));
  }
}

} // namespace detail

template <typename Ret, typename... Args>
typename std::enable_if<!std::is_same<Ret, Value>::value, void>::type
ScriptManager::registerExternalFunction(const std::string &name,
                                        std::function<Ret(Args...)> fn) {
  static_assert(
      (detail::IsSupportedType<Args>::value && ...),
      "Unsupported argument type (allowed: integers, float, double, bool, "
      "char, std::string, ArrayPtr)");
  static_assert(std::is_same<Ret, void>::value ||
                    detail::IsSupportedType<Ret>::value,
                "Unsupported return type");

  registerExternalFunction(
      name, ExternalFunctionCallback(
                [fn](const std::vector<Value> &args) -> Value {
                  if (args.size() != sizeof...(Args)) {
                    std::stringstream ss;
                    ss << "Expected " << sizeof...(Args) << " argument(s), got "
                       << args.size();
                    throw std::runtime_error(ss.str());
                  }
                  return detail::invokeTyped(
                      fn, args, std::index_sequence_for<Args...>{});
                }));
}

template <typename Ret, typename Arg>
void ScriptManager::registerExternalFunctionUnary(const std::string &name,
                                                  std::function<Ret(Arg)> fn) {
  static_assert(detail::IsSupportedType<Ret>::value, "Unsupported return type");
  static_assert(detail::IsSupportedType<Arg>::value, "Unsupported argument type");

  registerExternalFunction(
      name, [fn](const std::vector<Value> &args) -> Value {
        if (args.size() != 1) {
          throw std::runtime_error("Expected 1 argument");
        }
        Arg a = detail::fromValue<Arg>(args[0]);
        Ret r = fn(a);
        return detail::toValue(r);
      });
}

template <typename Ret, typename Arg1, typename Arg2>
void ScriptManager::registerExternalFunctionBinary(
    const std::string &name, std::function<Ret(Arg1, Arg2)> fn) {
  static_assert(detail::IsSupportedType<Ret>::value, "Unsupported return type");
  static_assert(detail::IsSupportedType<Arg1>::value,
                "Unsupported first argument type");
  static_assert(detail::IsSupportedType<Arg2>::value,
                "Unsupported second argument type");

  registerExternalFunction(
      name, [fn](const std::vector<Value> &args) -> Value {
        if (args.size() != 2) {
          throw std::runtime_error("Expected 2 arguments");
        }
        Arg1 a1 = detail::fromValue<Arg1>(args[0]);
        Arg2 a2 = detail::fromValue<Arg2>(args[1]);
        Ret r = fn(a1, a2);
        return detail::toValue(r);
      });
}

} // namespace Script
