#pragma once

#include "AST.h"
#include "Builtins.h"
#include "DataTypes.h"
#include <functional>
#include <initializer_list>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Script {

// A single frame in a runtime stack trace: the procedure and the source
// location of the call that led to the next frame (0/0 = invoked by host).
struct StackFrame {
  std::string procedure;
  std::string file;
  int line;
  int column;
};

class RuntimeError : public std::runtime_error {
public:
  std::string filename;
  int line;
  int column;
  std::string procedureName;
  // Ordered innermost-first: frames appended as the error unwinds.
  std::vector<StackFrame> trace;
  // Fatal errors (resource-limit violations) cannot be caught by script
  // try/catch — otherwise a hostile script could swallow guardrail hits.
  bool fatal = false;

  RuntimeError(const std::string &message, const std::string &file, int ln,
               int col, const std::string &procName = "", bool fatalErr = false)
      : std::runtime_error(message), filename(file), line(ln), column(col),
        procedureName(procName), fatal(fatalErr) {}
};

class ReturnException : public std::exception {
public:
  Value value;
  ReturnException(const Value &val) : value(val) {}
};

class BreakException : public std::exception {};
class ContinueException : public std::exception {};

// Thrown by a script `throw` statement; deliberately NOT a std::exception so
// it propagates untouched through the engine's generic error handlers until
// a script `catch` (or the top-level entry point) receives it.
class ScriptException {
public:
  Value value;
  std::string file;
  int line;
  int column;
  std::string procedure;

  ScriptException(const Value &v, const std::string &f, int ln, int col,
                  const std::string &proc)
      : value(v), file(f), line(ln), column(col), procedure(proc) {}
};

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

  // Hot-reload support: remove all overloads of a procedure (returns the
  // first old declaration, or nullptr) and add a standalone declaration
  // (same-signature declarations replace, others overload).
  ProcedureDeclPtr removeProcedure(const std::string &name);
  void addProcedure(const ProcedureDeclPtr &proc, const std::string &file);

  // Sandboxing: individual builtins can be turned off for untrusted
  // scripts. A disabled builtin resolves like an undefined function.
  void disableBuiltin(const std::string &name);
  void enableBuiltin(const std::string &name);
  bool isBuiltinEnabled(const std::string &name) const;

  // Optional execution guardrails (0 means unlimited):
  //   maxCallDepth   - max nested script calls
  //   maxSteps       - max statements/expressions per top-level execution
  //   maxStackBytes  - approx. native stack the script may consume; guards
  //                    against process crashes from deep recursion on
  //                    platforms/threads with small stacks
  void setExecutionLimits(size_t maxCallDepth, size_t maxSteps,
                          size_t maxStackBytes = 0);
  void clearExecutionLimits();

  // Optional memory guardrails (0 means unlimited):
  //   maxArraySize     - max elements a single array may hold
  //   maxStringLength  - max length of any produced string
  //   maxAllocations   - total array allocations per top-level execution
  void setMemoryLimits(size_t maxArraySize, size_t maxStringLength,
                       size_t maxAllocations);
  void clearMemoryLimits();

  // Sink for print()/println() builtins. Defaults to writing to stdout.
  using OutputCallback = std::function<void(const std::string &)>;
  void setOutputCallback(OutputCallback cb);

  // Debug hook: invoked before each statement executes. Receives the current
  // source location, procedure name, a snapshot of visible variables, and
  // call-stack state (callStack.back() == procedure).
  struct DebugContext {
    std::string filename;
    int line;
    int column;
    std::string procedure;
    std::unordered_map<std::string, Value> variables;
    size_t callDepth = 0;
    std::vector<std::string> callStack;
  };
  using DebugHook = std::function<void(const DebugContext &)>;
  void setDebugHook(DebugHook cb);
  void clearDebugHook();

  // Execute a procedure by name
  Value executeProcedure(const std::string &name,
                         const std::vector<Value> &arguments);

  // Internal fast path when procedure is already resolved
  Value executeProcedure(ProcedureDeclPtr proc,
                         const std::vector<Value> &arguments);

  // Execute raw statements in the global environment (REPL support).
  // Variables declared this way persist as interpreter globals.
  // A top-level 'return' exits early and yields its value.
  Value executeStatements(const std::vector<StmtPtr> &statements);

  // Check if a procedure exists
  bool hasProcedure(const std::string &name) const;

  // Struct declarations (loaded with scripts; shared across files)
  void addStruct(const StructDeclPtr &decl, const std::string &file);
  StructDeclPtr removeStruct(const std::string &name);
  bool hasStruct(const std::string &name) const;
  StructDeclPtr getStruct(const std::string &name) const;
  std::vector<std::string> getStructNames() const;

  // Enum declarations (loaded with scripts; shared across files)
  void addEnum(const EnumDeclPtr &decl, const std::string &file);
  EnumDeclPtr removeEnum(const std::string &name);
  bool hasEnum(const std::string &name) const;
  EnumDeclPtr getEnum(const std::string &name) const;
  std::vector<std::string> getEnumNames() const;

  // Invoke a function value (lambda, procedure reference, bound method).
  Value callFunctionValue(const FuncPtr &fn, const std::vector<Value> &args,
                          int line, int column);

  // Names of all loaded procedures
  std::vector<std::string> getProcedureNames() const;

  // Get procedure info (for debugging/inspection)
  ProcedureDeclPtr getProcedure(const std::string &name) const;

private:
  friend class Builtins;

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

    // Snapshot of all visible variables (globals + scopes, innermost wins).
    std::unordered_map<std::string, Value> snapshot() const;

  private:
    Environment *_parent;
    std::vector<std::unordered_map<std::string, Value>> _scopes;
    std::unordered_map<std::string, Value> _globals;
    mutable std::unordered_map<std::string, size_t> _lookupCache; // name -> scope index (GLOBAL = npos)
    std::vector<std::vector<std::string>> _scopeNames;            // names defined per scope for cache invalidation

    static constexpr size_t GLOBAL = std::numeric_limits<size_t>::max();
  };

  // Procedure overload sets: multiple declarations may share a name when
  // their parameter type lists differ.
  std::unordered_map<std::string, std::vector<ProcedureDeclPtr>> _procedures;
  std::unordered_map<ProcedureDecl *, std::string> _procedureFiles;
  std::unordered_map<std::string, StructDeclPtr> _structs;
  std::unordered_map<StructDecl *, std::string> _structFiles;
  std::unordered_map<std::string, EnumDeclPtr> _enums;
  std::unordered_map<EnumDecl *, std::string> _enumFiles;
  std::unordered_map<std::string, ExternalFunctionCallback> _externalFunctions;
  std::unordered_set<std::string> _disabledBuiltins;
  struct ExternalVariable {
    ExternalVariableGetter getter;
    ExternalVariableSetter setter;
  };
  std::unordered_map<std::string, ExternalVariable> _externalVariables;
  Environment _globalEnv;
  Environment *_currentEnv;
  std::string _currentProcedure;
  std::string _currentFile;
  std::vector<std::string> _callStack; // procedure names, innermost last
  uint64_t _callCacheVersion = 1;
  size_t _maxCallDepth = 0;
  size_t _maxSteps = 0;
  size_t _maxStackBytes = 0;
  const char *_stackBase = nullptr; // set at top-level execution entry
  size_t _currentCallDepth = 0;
  size_t _currentSteps = 0;
  size_t _maxArraySize = 0;
  size_t _maxStringLength = 0;
  size_t _maxAllocations = 0;
  size_t _allocationCount = 0;
  bool _executionActive = false;
  OutputCallback _outputCallback;
  DebugHook _debugHook;
  std::mt19937_64 _rng;
  int _callSiteLine = 0;   // source line of the call that entered the
  int _callSiteColumn = 0; // current procedure (0 = invoked by host)

  // Evaluation methods
  Value evaluate(ExprPtr expr);
  void execute(StmtPtr stmt);

  Value evaluateLiteral(LiteralExpr *expr);
  Value evaluateVariable(VariableExpr *expr);
  Value evaluateArrayLiteral(ArrayLiteralExpr *expr);
  Value evaluateMapLiteral(MapLiteralExpr *expr);
  Value evaluateIndex(IndexExpr *expr);
  Value evaluateBinary(BinaryExpr *expr);
  Value evaluateUnary(UnaryExpr *expr);
  Value evaluateCall(CallExpr *expr);
  Value evaluateConditional(ConditionalExpr *expr);
  Value evaluateInterpolatedString(InterpolatedStringExpr *expr);
  Value evaluateUpdate(UpdateExpr *expr);
  Value evaluateMember(MemberExpr *expr);
  Value evaluateLambda(LambdaExpr *expr);
  Value evaluateEnumMember(EnumMemberExpr *expr);
  Value evaluateSlice(const Value &container, IndexExpr *expr);
  int64_t normalizeIndex(int64_t raw, size_t size, int line, int column);
  Value constructStruct(const StructDeclPtr &decl,
                        const std::vector<Value> &args, int line, int column);
  Value defaultValue(const TypeInfo &type);

  // Overload resolution: pick the procedure in `overloads` best matching
  // `args` (arity incl. defaults, then conversion cost). Throws on no/ambig.
  ProcedureDeclPtr resolveOverload(const std::string &name,
                                   const std::vector<ProcedureDeclPtr> &set,
                                   const std::vector<Value> &args, int line,
                                   int column);
  // Whether a runtime value of type `src` can convert to `dst` without
  // throwing (used for overload ranking; mirrors convertToType's rules).
  bool runtimeConvertible(const TypeInfo &src, const TypeInfo &dst);
  // Shared call machinery for procedures, lambdas, and bound methods.
  Value invokeCallable(const std::string &name,
                       const std::vector<Parameter> &params, StmtPtr body,
                       const TypeInfo &retType, const std::string &file,
                       const std::unordered_map<std::string, Value> *captures,
                       std::vector<Value> args, int line, int column);

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
  void executeMemberAssign(MemberAssignStmt *stmt);
  void executeForEach(ForEachStmt *stmt);
  void executeThrow(ThrowStmt *stmt);
  void executeTryCatch(TryCatchStmt *stmt);

  RuntimeError runtimeError(const std::string &message, int line, int column);
  void consumeExecutionStep(int line, int column);

  // Memory-limit checks (plain std::runtime_error so callers can attach
  // location info; invoked from evaluate/execute paths that add it).
  void noteArrayAllocation(size_t elements);
  void checkArraySize(size_t elements);
  void checkStringLength(size_t length);
  void checkResultSize(const Value &v); // post-check builtin/call results

  // Variable access honoring both scopes and external variables
  Value readVariable(const std::string &name, int line, int column);
  void writeVariable(const std::string &name, const Value &value, int line,
                     int column);
  Value applyAssignOp(AssignStmt::Operator op, const Value &current,
                      const Value &value, int line, int column);

  // Type conversion for parameters
  Value convertToType(const Value &val, const TypeInfo &targetType);
};

} // namespace Script
