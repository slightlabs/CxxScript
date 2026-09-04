#pragma once

#include "Interpreter.h"
#include "Lexer.h"
#include "Parser.h"
#include <initializer_list>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace Script {

struct CompilationError {
  std::string message;
  std::string filename;
  std::string procedureName;
  int line;
  int column;

  CompilationError(const std::string &msg, const std::string &file,
                   const std::string &proc, int ln, int col)
      : message(msg), filename(file), procedureName(proc), line(ln),
        column(col) {}

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

  // Execute a procedure from any loaded script
  bool executeProcedure(const std::string &procedureName,
                        const std::vector<Value> &arguments, Value &returnValue,
                        std::string &errorMessage);

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

  // Unregister an external variable
  void unregisterExternalVariable(const std::string &name);

  // Check if an external variable is registered
  bool hasExternalVariable(const std::string &name) const;

  // Clear all loaded scripts
  void clear();

  // Optional execution guardrails (0 means unlimited)
  void setExecutionLimits(size_t maxCallDepth, size_t maxSteps);
  void clearExecutionLimits();

private:
  std::unique_ptr<Interpreter> _interpreter;
  std::unordered_map<std::string, std::string>
      _procedureFiles; // procedure name -> filename

  bool compileScript(const std::string &source, const std::string &filename,
                     std::vector<CompilationError> &errors, bool load);
};

// --- Inline implementations for typed helpers ---

namespace detail {
inline Value toValue(const int32_t v) { return static_cast<int32_t>(v); }
inline Value toValue(const double v) { return v; }
inline Value toValue(const bool v) { return v; }
inline Value toValue(const std::string &v) { return v; }

template <typename T> inline T fromValue(const Value &v);

template <> inline int32_t fromValue<int32_t>(const Value &v) {
  return static_cast<int32_t>(ValueHelper::toInt64(v));
}
template <> inline double fromValue<double>(const Value &v) {
  return ValueHelper::toDouble(v);
}
template <> inline bool fromValue<bool>(const Value &v) {
  return ValueHelper::toBool(v);
}
template <> inline std::string fromValue<std::string>(const Value &v) {
  return ValueHelper::toString(v);
}

template <typename T> struct IsSupportedType : std::false_type {};
template <> struct IsSupportedType<int32_t> : std::true_type {};
template <> struct IsSupportedType<double> : std::true_type {};
template <> struct IsSupportedType<bool> : std::true_type {};
template <> struct IsSupportedType<std::string> : std::true_type {};

} // namespace detail

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
