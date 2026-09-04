#pragma once

#include "AST.h"
#include "DataTypes.h"
#include <functional>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace Script {

class RuntimeError : public std::runtime_error {
public:
  std::string filename;
  int line;
  int column;
  std::string procedureName;

  RuntimeError(const std::string &message, const std::string &file, int ln,
               int col, const std::string &procName = "")
      : std::runtime_error(message), filename(file), line(ln), column(col),
        procedureName(procName) {}
};

class ReturnException : public std::exception {
public:
  Value value;
  ReturnException(const Value &val) : value(val) {}
};

class BreakException : public std::exception {};
class ContinueException : public std::exception {};

// External function callback
using ExternalFunctionCallback =
  std::function<Value(const std::vector<Value> &)>;

struct ExternalBinding {
  std::string name;
  ExternalFunctionCallback callback;
};

// External variable callbacks
using ExternalVariableGetter = std::function<Value()>;
using ExternalVariableSetter = std::function<void(const Value &)>;

class Interpreter {
public:
  Interpreter();

  // Register an external function by name
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

  // Register an external variable by name (getter required, setter optional)
  void registerExternalVariable(const std::string &name,
                                ExternalVariableGetter getter,
                                ExternalVariableSetter setter = nullptr);

  // Convenience overload for read-only variables
  void registerExternalVariableReadOnly(const std::string &name,
                                        ExternalVariableGetter getter);

  // Unregister an external variable
  void unregisterExternalVariable(const std::string &name);

  // Check if an external variable is registered
  bool hasExternalVariable(const std::string &name) const;

  // Load a script (add procedures to the interpreter)
  void loadScript(ScriptPtr script);

  // Execute a procedure by name
  Value executeProcedure(const std::string &name,
                         const std::vector<Value> &arguments);

  // Internal fast path when procedure is already resolved
  Value executeProcedure(ProcedureDeclPtr proc,
                         const std::vector<Value> &arguments);

  // Check if a procedure exists
  bool hasProcedure(const std::string &name) const;

  // Get procedure info (for debugging/inspection)
  ProcedureDeclPtr getProcedure(const std::string &name) const;

private:
  // Environment for variables (stack of scopes)
  class Environment {
  public:
    Environment(Environment *parent = nullptr) : _parent(parent) {}

    void define(const std::string &name, const Value &value);
    Value get(const std::string &name) const;
    void assign(const std::string &name, const Value &value);
    bool has(const std::string &name) const;

    void enterScope();
    void exitScope();

  private:
    Environment *_parent;
    std::vector<std::unordered_map<std::string, Value>> _scopes;
    std::unordered_map<std::string, Value> _globals;
    mutable std::unordered_map<std::string, size_t> _lookupCache; // name -> scope index (GLOBAL = npos)
    std::vector<std::vector<std::string>> _scopeNames;            // names defined per scope for cache invalidation

    static constexpr size_t GLOBAL = std::numeric_limits<size_t>::max();
  };

  std::unordered_map<std::string, ProcedureDeclPtr> _procedures;
  std::unordered_map<ProcedureDecl *, std::string> _procedureFiles;
  std::unordered_map<std::string, ExternalFunctionCallback> _externalFunctions;
  struct ExternalVariable {
    ExternalVariableGetter getter;
    ExternalVariableSetter setter;
  };
  std::unordered_map<std::string, ExternalVariable> _externalVariables;
  Environment _globalEnv;
  Environment *_currentEnv;
  std::string _currentProcedure;
  std::string _currentFile;
  uint64_t _callCacheVersion = 1;

  // Evaluation methods
  Value evaluate(ExprPtr expr);
  void execute(StmtPtr stmt);

  Value evaluateLiteral(LiteralExpr *expr);
  Value evaluateVariable(VariableExpr *expr);
  Value evaluateArrayLiteral(ArrayLiteralExpr *expr);
  Value evaluateIndex(IndexExpr *expr);
  Value evaluateBinary(BinaryExpr *expr);
  Value evaluateUnary(UnaryExpr *expr);
  Value evaluateCall(CallExpr *expr);
  Value evaluateConditional(ConditionalExpr *expr);

  void executeExpression(ExpressionStmt *stmt);
  void executeVarDecl(VarDeclStmt *stmt);
  void executeAssign(AssignStmt *stmt);
  void executeBlock(BlockStmt *stmt);
  void executeIf(IfStmt *stmt);
  void executeWhile(WhileStmt *stmt);
  void executeFor(ForStmt *stmt);
  void executeDoWhile(DoWhileStmt *stmt);
  void executeSwitch(SwitchStmt *stmt);
  void executeReturn(ReturnStmt *stmt);
  void executeBreak(BreakStmt *stmt);
  void executeContinue(ContinueStmt *stmt);
  void executeIndexAssign(IndexAssignStmt *stmt);

  RuntimeError runtimeError(const std::string &message, int line, int column);

  // Type conversion for parameters
  Value convertToType(const Value &val, const TypeInfo &targetType);
};

} // namespace Script
