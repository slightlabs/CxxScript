#include "Interpreter.h"
#include <iostream>
#include <limits>
#include <sstream>
#include <utility>

namespace Script {

// Environment Implementation
void Interpreter::Environment::define(const std::string &name,
                                      const Value &value) {
  if (!_scopes.empty()) {
    auto &scope = _scopes.back();
    auto inserted = scope.emplace(name, value);
    if (!inserted.second) {
      inserted.first->second = value;
    } else {
      _scopeNames.back().push_back(name);
    }
    _lookupCache[name] = _scopes.size() - 1;
  } else {
    _globals[name] = value;
    _lookupCache[name] = GLOBAL;
  }
}

Value Interpreter::Environment::get(const std::string &name) const {
  auto cacheIt = _lookupCache.find(name);
  if (cacheIt != _lookupCache.end()) {
    size_t idx = cacheIt->second;
    if (idx == GLOBAL) {
      auto gIt = _globals.find(name);
      if (gIt != _globals.end()) {
        return gIt->second;
      }
      _lookupCache.erase(cacheIt);
    } else if (idx < _scopes.size()) {
      const auto &scope = _scopes[idx];
      auto sIt = scope.find(name);
      if (sIt != scope.end()) {
        return sIt->second;
      }
      _lookupCache.erase(cacheIt);
    }
  }

  // Search from innermost to outermost scope
  for (size_t i = _scopes.size(); i-- > 0;) {
    auto found = _scopes[i].find(name);
    if (found != _scopes[i].end()) {
      _lookupCache[name] = i;
      return found->second;
    }
  }

  // Check globals
  auto found = _globals.find(name);
  if (found != _globals.end()) {
    _lookupCache[name] = GLOBAL;
    return found->second;
  }

  // Check parent environment
  if (_parent) {
    return _parent->get(name);
  }

  throw std::runtime_error("Undefined variable: " + name);
}

void Interpreter::Environment::assign(const std::string &name,
                                      const Value &value) {
  // Cache-aware fast path
  auto cacheIt = _lookupCache.find(name);
  if (cacheIt != _lookupCache.end()) {
    size_t idx = cacheIt->second;
    if (idx == GLOBAL) {
      auto gIt = _globals.find(name);
      if (gIt != _globals.end()) {
        gIt->second = value;
        return;
      }
      _lookupCache.erase(cacheIt);
    } else if (idx < _scopes.size()) {
      auto &scope = _scopes[idx];
      auto sIt = scope.find(name);
      if (sIt != scope.end()) {
        sIt->second = value;
        return;
      }
      _lookupCache.erase(cacheIt);
    }
  }

  // Search from innermost to outermost scope
  for (size_t i = _scopes.size(); i-- > 0;) {
    auto found = _scopes[i].find(name);
    if (found != _scopes[i].end()) {
      _lookupCache[name] = i;
      found->second = value;
      return;
    }
  }

  // Check globals
  auto found = _globals.find(name);
  if (found != _globals.end()) {
    _lookupCache[name] = GLOBAL;
    found->second = value;
    return;
  }

  // Check parent environment
  if (_parent) {
    _parent->assign(name, value);
    return;
  }

  throw std::runtime_error("Undefined variable: " + name);
}

bool Interpreter::Environment::has(const std::string &name) const {
  auto cacheIt = _lookupCache.find(name);
  if (cacheIt != _lookupCache.end()) {
    size_t idx = cacheIt->second;
    if (idx == GLOBAL) {
      if (_globals.find(name) != _globals.end()) {
        return true;
      }
    } else if (idx < _scopes.size() && _scopes[idx].find(name) != _scopes[idx].end()) {
      return true;
    }
  }

  for (auto it = _scopes.rbegin(); it != _scopes.rend(); ++it) {
    if (it->find(name) != it->end()) {
      size_t idx = static_cast<size_t>(_scopes.size() - 1 - (it - _scopes.rbegin()));
      _lookupCache[name] = idx;
      return true;
    }
  }

  if (_globals.find(name) != _globals.end()) {
    _lookupCache[name] = GLOBAL;
    return true;
  }

  if (_parent) {
    return _parent->has(name);
  }

  return false;
}

void Interpreter::Environment::enterScope() {
  _scopes.emplace_back();
  _scopeNames.emplace_back();
}

void Interpreter::Environment::exitScope() {
  if (!_scopes.empty()) {
    if (!_scopeNames.empty()) {
      for (const auto &name : _scopeNames.back()) {
        _lookupCache.erase(name);
      }
      _scopeNames.pop_back();
    }
    _scopes.pop_back();
  }
}

std::unordered_map<std::string, Value>
Interpreter::Environment::snapshot() const {
  // Parent first so inner environments/scopes override outer bindings.
  std::unordered_map<std::string, Value> out =
      _parent ? _parent->snapshot() : std::unordered_map<std::string, Value>();
  for (const auto &kv : _globals) {
    out[kv.first] = kv.second;
  }
  for (const auto &scope : _scopes) {
    for (const auto &kv : scope) {
      out[kv.first] = kv.second;
    }
  }
  return out;
}

// Interpreter Implementation
Interpreter::Interpreter()
    : _globalEnv(nullptr), _currentEnv(&_globalEnv), _currentProcedure(""),
      _currentFile(""), _callCacheVersion(1),
      _outputCallback([](const std::string &s) { std::cout << s; }),
      _rng(std::random_device{}()) {}

void Interpreter::registerExternalFunction(const std::string &name,
                                           ExternalFunctionCallback callback) {
  _externalFunctions[name] = callback;
  ++_callCacheVersion;
}

void Interpreter::registerExternalFunctions(
    const std::vector<ExternalBinding> &bindings) {
  for (const auto &b : bindings) {
    _externalFunctions[b.name] = b.callback;
  }
  ++_callCacheVersion;
}

void Interpreter::registerExternalFunctions(
    std::initializer_list<ExternalBinding> bindings) {
  registerExternalFunctions(
      std::vector<ExternalBinding>(bindings.begin(), bindings.end()));
}

void Interpreter::unregisterExternalFunction(const std::string &name) {
  _externalFunctions.erase(name);
  ++_callCacheVersion;
}

bool Interpreter::hasExternalFunction(const std::string &name) const {
  return _externalFunctions.find(name) != _externalFunctions.end();
}

void Interpreter::registerExternalVariable(const std::string &name,
                                           ExternalVariableGetter getter,
                                           ExternalVariableSetter setter) {
  _externalVariables[name] = ExternalVariable{std::move(getter),
                                              std::move(setter)};
  ++_callCacheVersion;
}

void Interpreter::registerExternalVariableReadOnly(const std::string &name,
                                                    ExternalVariableGetter getter) {
  registerExternalVariable(name, std::move(getter), nullptr);
}

void Interpreter::unregisterExternalVariable(const std::string &name) {
  _externalVariables.erase(name);
  ++_callCacheVersion;
}

bool Interpreter::hasExternalVariable(const std::string &name) const {
  return _externalVariables.find(name) != _externalVariables.end();
}

void Interpreter::loadScript(ScriptPtr script) {
  for (auto &proc : script->procedures) {
    _procedures[proc->name] = proc;
    _procedureFiles[proc.get()] = script->filename;
  }
  for (auto &s : script->structs) {
    _structs[s->name] = s;
    _structFiles[s.get()] = script->filename;
  }
  ++_callCacheVersion;
}

void Interpreter::addStruct(const StructDeclPtr &decl,
                            const std::string &file) {
  if (!decl) {
    return;
  }
  _structs[decl->name] = decl;
  _structFiles[decl.get()] = file;
}

StructDeclPtr Interpreter::removeStruct(const std::string &name) {
  auto it = _structs.find(name);
  if (it == _structs.end()) {
    return nullptr;
  }
  StructDeclPtr old = it->second;
  _structFiles.erase(old.get());
  _structs.erase(it);
  return old;
}

bool Interpreter::hasStruct(const std::string &name) const {
  return _structs.find(name) != _structs.end();
}

StructDeclPtr Interpreter::getStruct(const std::string &name) const {
  auto it = _structs.find(name);
  return it != _structs.end() ? it->second : nullptr;
}

std::vector<std::string> Interpreter::getStructNames() const {
  std::vector<std::string> names;
  names.reserve(_structs.size());
  for (const auto &kv : _structs) {
    names.push_back(kv.first);
  }
  return names;
}

ProcedureDeclPtr Interpreter::removeProcedure(const std::string &name) {
  auto it = _procedures.find(name);
  if (it == _procedures.end()) {
    return nullptr;
  }
  ProcedureDeclPtr old = it->second;
  _procedureFiles.erase(old.get());
  _procedures.erase(it);
  ++_callCacheVersion;
  return old;
}

void Interpreter::addProcedure(const ProcedureDeclPtr &proc,
                               const std::string &file) {
  if (!proc) {
    return;
  }
  _procedures[proc->name] = proc;
  _procedureFiles[proc.get()] = file;
  ++_callCacheVersion;
}

void Interpreter::setExecutionLimits(size_t maxCallDepth, size_t maxSteps) {
  _maxCallDepth = maxCallDepth;
  _maxSteps = maxSteps;
}

void Interpreter::clearExecutionLimits() { setExecutionLimits(0, 0); }

void Interpreter::setMemoryLimits(size_t maxArraySize, size_t maxStringLength,
                                  size_t maxAllocations) {
  _maxArraySize = maxArraySize;
  _maxStringLength = maxStringLength;
  _maxAllocations = maxAllocations;
}

void Interpreter::clearMemoryLimits() { setMemoryLimits(0, 0, 0); }

void Interpreter::noteArrayAllocation(size_t elements) {
  if (_maxAllocations > 0 && ++_allocationCount > _maxAllocations) {
    throw std::runtime_error(
        "Maximum array allocation count exceeded (limit " +
        std::to_string(_maxAllocations) + ")");
  }
  checkArraySize(elements);
}

void Interpreter::checkArraySize(size_t elements) {
  if (_maxArraySize > 0 && elements > _maxArraySize) {
    throw std::runtime_error("Array size limit exceeded (" +
                             std::to_string(elements) + " > " +
                             std::to_string(_maxArraySize) + ")");
  }
}

void Interpreter::checkStringLength(size_t length) {
  if (_maxStringLength > 0 && length > _maxStringLength) {
    throw std::runtime_error("String length limit exceeded (" +
                             std::to_string(length) + " > " +
                             std::to_string(_maxStringLength) + ")");
  }
}

void Interpreter::checkResultSize(const Value &v) {
  if (auto *arr = std::get_if<ArrayPtr>(&v)) {
    noteArrayAllocation(*arr ? (*arr)->elements.size() : 0);
  } else if (auto *mp = std::get_if<MapPtr>(&v)) {
    noteArrayAllocation(*mp ? (*mp)->entries.size() : 0);
  } else if (auto *sv = std::get_if<StructPtr>(&v)) {
    noteArrayAllocation(*sv ? (*sv)->fields.size() : 0);
  } else if (auto *s = std::get_if<std::string>(&v)) {
    checkStringLength(s->size());
  }
}

void Interpreter::setDebugHook(DebugHook cb) { _debugHook = std::move(cb); }

void Interpreter::clearDebugHook() { _debugHook = nullptr; }

void Interpreter::setOutputCallback(OutputCallback cb) {
  if (cb) {
    _outputCallback = std::move(cb);
  } else {
    _outputCallback = [](const std::string &s) { std::cout << s; };
  }
}

Value Interpreter::executeProcedure(const std::string &name,
                                    const std::vector<Value> &arguments) {
  auto it = _procedures.find(name);
  if (it == _procedures.end()) {
    throw std::runtime_error("Procedure not found: " + name);
  }

  bool topLevel = !_executionActive;
  if (topLevel) {
    _executionActive = true;
    _currentCallDepth = 0;
    _currentSteps = 0;
    _allocationCount = 0;
    _callSiteLine = 0;
    _callSiteColumn = 0;
  }

  try {
    Value out = executeProcedure(it->second, arguments);
    if (topLevel) {
      _executionActive = false;
    }
    return out;
  } catch (...) {
    if (topLevel) {
      _executionActive = false;
    }
    throw;
  }
}

Value Interpreter::executeStatements(const std::vector<StmtPtr> &statements) {
  bool topLevel = !_executionActive;
  if (topLevel) {
    _executionActive = true;
    _currentCallDepth = 0;
    _currentSteps = 0;
    _allocationCount = 0;
    _callSiteLine = 0;
    _callSiteColumn = 0;
  }

  Environment *previousEnv = _currentEnv;
  _currentEnv = &_globalEnv;

  try {
    for (const auto &stmt : statements) {
      execute(stmt);
    }
    _currentEnv = previousEnv;
    if (topLevel)
      _executionActive = false;
    return static_cast<int32_t>(0);
  } catch (const ReturnException &ret) {
    _currentEnv = previousEnv;
    if (topLevel)
      _executionActive = false;
    return ret.value;
  } catch (...) {
    _currentEnv = previousEnv;
    if (topLevel)
      _executionActive = false;
    throw;
  }
}

Value Interpreter::executeProcedure(ProcedureDeclPtr proc,
                                    const std::vector<Value> &arguments) {
  std::string previousProcedure = _currentProcedure;
  std::string previousFile = _currentFile;
  int previousCallLine = _callSiteLine;
  int previousCallCol = _callSiteColumn;
  _currentProcedure = proc->name;
  _currentFile = "";
  auto fileIt = _procedureFiles.find(proc.get());
  if (fileIt != _procedureFiles.end()) {
    _currentFile = fileIt->second;
  }

  auto restoreFrame = [&]() {
    _currentProcedure = previousProcedure;
    _currentFile = previousFile;
    _callSiteLine = previousCallLine;
    _callSiteColumn = previousCallCol;
  };

  if (_maxCallDepth > 0 && _currentCallDepth >= _maxCallDepth) {
    restoreFrame();
    throw runtimeError("Maximum call depth exceeded", proc->line, proc->column);
  }
  // Check argument count
  if (arguments.size() != proc->parameters.size()) {
    std::stringstream ss;
    ss << "Procedure '" << proc->name << "' expects " << proc->parameters.size()
       << " arguments, got " << arguments.size();
    restoreFrame();
    throw runtimeError(ss.str(), proc->line, proc->column);
  }
  ++_currentCallDepth;

  // Create new environment for procedure
  Environment procEnv(_currentEnv);
  Environment *previousEnv = _currentEnv;
  _currentEnv = &procEnv;

  auto restoreAll = [&]() {
    _currentEnv = previousEnv;
    restoreFrame();
  };

  try {
    // Bind parameters
    for (size_t i = 0; i < proc->parameters.size(); ++i) {
      Value convertedArg;
      try {
        convertedArg = convertToType(arguments[i], proc->parameters[i].type);
      } catch (const std::exception &e) {
        throw runtimeError(e.what(), proc->line, proc->column);
      }
      _currentEnv->define(proc->parameters[i].name, convertedArg);
    }

    execute(proc->body);

    // If we reach here, no return statement was executed
    if (proc->returnType.baseType == DataType::VOID &&
        !proc->returnType.isArray && !proc->returnType.isStruct) {
      restoreAll();
      --_currentCallDepth;
      return static_cast<int32_t>(0); // Dummy value
    }

    // Non-void procedure without return
    restoreAll();
    --_currentCallDepth;
    throw runtimeError("Non-void procedure must return a value", proc->line,
                       proc->column);

  } catch (const ReturnException &ret) {
    restoreAll();
    --_currentCallDepth;

    if (proc->returnType.baseType == DataType::VOID &&
        !proc->returnType.isArray && !proc->returnType.isStruct) {
      return static_cast<int32_t>(0); // Dummy value
    }

    try {
      return convertToType(ret.value, proc->returnType);
    } catch (const std::exception &e) {
      throw runtimeError(e.what(), proc->line, proc->column);
    }
  } catch (const BreakException &) {
    restoreAll();
    --_currentCallDepth;
    throw;
  } catch (const ContinueException &) {
    restoreAll();
    --_currentCallDepth;
    throw;
  } catch (RuntimeError &e) {
    // First frame (the proc that raised) reports the error site; caller
    // frames report the line of the call that is unwinding.
    bool raiseFrame = e.trace.empty() && e.procedureName == _currentProcedure;
    e.trace.push_back({_currentProcedure, _currentFile,
                       raiseFrame ? e.line : _callSiteLine,
                       raiseFrame ? e.column : _callSiteColumn});
    restoreAll();
    --_currentCallDepth;
    throw;
  } catch (const std::exception &e) {
    RuntimeError re(e.what(), _currentFile, _callSiteLine, _callSiteColumn,
                    _currentProcedure);
    re.trace.push_back(
        {_currentProcedure, _currentFile, _callSiteLine, _callSiteColumn});
    restoreAll();
    --_currentCallDepth;
    throw re;
  } catch (...) {
    restoreAll();
    --_currentCallDepth;
    throw;
  }
}

bool Interpreter::hasProcedure(const std::string &name) const {
  return _procedures.find(name) != _procedures.end();
}

ProcedureDeclPtr Interpreter::getProcedure(const std::string &name) const {
  auto it = _procedures.find(name);
  if (it != _procedures.end()) {
    return it->second;
  }
  return nullptr;
}

std::vector<std::string> Interpreter::getProcedureNames() const {
  std::vector<std::string> names;
  names.reserve(_procedures.size());
  for (const auto &kv : _procedures) {
    names.push_back(kv.first);
  }
  return names;
}

Value Interpreter::evaluate(ExprPtr expr) {
  try {
    if (auto *lit = dynamic_cast<LiteralExpr *>(expr.get())) {
      return evaluateLiteral(lit);
    }
    if (auto *var = dynamic_cast<VariableExpr *>(expr.get())) {
      return evaluateVariable(var);
    }
    if (auto *arr = dynamic_cast<ArrayLiteralExpr *>(expr.get())) {
      return evaluateArrayLiteral(arr);
    }
    if (auto *m = dynamic_cast<MapLiteralExpr *>(expr.get())) {
      return evaluateMapLiteral(m);
    }
    if (auto *idx = dynamic_cast<IndexExpr *>(expr.get())) {
      return evaluateIndex(idx);
    }
    if (auto *bin = dynamic_cast<BinaryExpr *>(expr.get())) {
      Value v = evaluateBinary(bin);
      checkResultSize(v); // string concatenation can produce large strings
      return v;
    }
    if (auto *un = dynamic_cast<UnaryExpr *>(expr.get())) {
      return evaluateUnary(un);
    }
    if (auto *call = dynamic_cast<CallExpr *>(expr.get())) {
      return evaluateCall(call);
    }
    if (auto *cond = dynamic_cast<ConditionalExpr *>(expr.get())) {
      return evaluateConditional(cond);
    }
    if (auto *interp = dynamic_cast<InterpolatedStringExpr *>(expr.get())) {
      return evaluateInterpolatedString(interp);
    }
    if (auto *upd = dynamic_cast<UpdateExpr *>(expr.get())) {
      return evaluateUpdate(upd);
    }
    if (auto *mem = dynamic_cast<MemberExpr *>(expr.get())) {
      return evaluateMember(mem);
    }

    throw runtimeError("Unknown expression type", expr->line, expr->column);
  } catch (RuntimeError &) {
    throw;
  } catch (const std::exception &e) {
    throw runtimeError(e.what(), expr->line, expr->column);
  }
}

void Interpreter::execute(StmtPtr stmt) {
  consumeExecutionStep(stmt->line, stmt->column);

  if (_debugHook &&
      dynamic_cast<BlockStmt *>(stmt.get()) == nullptr) {
    DebugContext ctx{_currentFile, stmt->line, stmt->column,
                     _currentProcedure, _currentEnv->snapshot()};
    _debugHook(ctx);
  }

  try {
    if (auto *exprStmt = dynamic_cast<ExpressionStmt *>(stmt.get())) {
      executeExpression(exprStmt);
    } else if (auto *varDecl = dynamic_cast<VarDeclStmt *>(stmt.get())) {
      executeVarDecl(varDecl);
    } else if (auto *assign = dynamic_cast<AssignStmt *>(stmt.get())) {
      executeAssign(assign);
    } else if (auto *block = dynamic_cast<BlockStmt *>(stmt.get())) {
      executeBlock(block);
    } else if (auto *ifStmt = dynamic_cast<IfStmt *>(stmt.get())) {
      executeIf(ifStmt);
    } else if (auto *whileStmt = dynamic_cast<WhileStmt *>(stmt.get())) {
      executeWhile(whileStmt);
    } else if (auto *forStmt = dynamic_cast<ForStmt *>(stmt.get())) {
      executeFor(forStmt);
    } else if (auto *doWhile = dynamic_cast<DoWhileStmt *>(stmt.get())) {
      executeDoWhile(doWhile);
    } else if (auto *switchStmt = dynamic_cast<SwitchStmt *>(stmt.get())) {
      executeSwitch(switchStmt);
    } else if (auto *retStmt = dynamic_cast<ReturnStmt *>(stmt.get())) {
      executeReturn(retStmt);
    } else if (auto *brk = dynamic_cast<BreakStmt *>(stmt.get())) {
      executeBreak(brk);
    } else if (auto *cont = dynamic_cast<ContinueStmt *>(stmt.get())) {
      executeContinue(cont);
    } else if (auto *idxAssign = dynamic_cast<IndexAssignStmt *>(stmt.get())) {
      executeIndexAssign(idxAssign);
    } else if (auto *memAssign =
                   dynamic_cast<MemberAssignStmt *>(stmt.get())) {
      executeMemberAssign(memAssign);
    } else if (auto *forEach = dynamic_cast<ForEachStmt *>(stmt.get())) {
      executeForEach(forEach);
    } else {
      throw runtimeError("Unknown statement type", stmt->line, stmt->column);
    }
  } catch (const ReturnException &) {
    throw;
  } catch (const BreakException &) {
    throw;
  } catch (const ContinueException &) {
    throw;
  } catch (RuntimeError &) {
    throw;
  } catch (const std::exception &e) {
    throw runtimeError(e.what(), stmt->line, stmt->column);
  }
}

Value Interpreter::evaluateLiteral(LiteralExpr *expr) { return expr->value; }

Value Interpreter::evaluateVariable(VariableExpr *expr) {
  return readVariable(expr->name, expr->line, expr->column);
}

Value Interpreter::readVariable(const std::string &name, int line, int column) {
  if (_currentEnv->has(name)) {
    return _currentEnv->get(name);
  }

  auto extIt = _externalVariables.find(name);
  if (extIt != _externalVariables.end()) {
    if (!extIt->second.getter) {
      throw runtimeError("External variable '" + name + "' has no getter", line,
                         column);
    }
    return extIt->second.getter();
  }

  throw runtimeError("Undefined variable: " + name, line, column);
}

void Interpreter::writeVariable(const std::string &name, const Value &value,
                                int line, int column) {
  if (_currentEnv->has(name)) {
    _currentEnv->assign(name, value);
    return;
  }

  auto extIt = _externalVariables.find(name);
  if (extIt == _externalVariables.end()) {
    throw runtimeError("Undefined variable: " + name, line, column);
  }
  if (!extIt->second.setter) {
    throw runtimeError("External variable '" + name + "' is read-only", line,
                       column);
  }
  extIt->second.setter(value);
}

Value Interpreter::evaluateArrayLiteral(ArrayLiteralExpr *expr) {
  std::vector<Value> elements;
  elements.reserve(expr->elements.size());
  for (auto &e : expr->elements) {
    elements.push_back(evaluate(e));
  }
  noteArrayAllocation(elements.size());

  TypeInfo elemType = elements.empty() ? TypeInfo(DataType::VOID)
                                       : ValueHelper::getType(elements[0]);
  return ValueHelper::createArray(elemType, elements);
}

Value Interpreter::evaluateMapLiteral(MapLiteralExpr *expr) {
  TypeInfo keyType(DataType::VOID), valueType(DataType::VOID);
  auto m = ValueHelper::createMap(keyType, valueType);
  bool first = true;
  for (auto &kv : expr->entries) {
    Value key = evaluate(kv.first);
    Value val = evaluate(kv.second);
    if (first) {
      keyType = ValueHelper::getType(key);
      valueType = ValueHelper::getType(val);
      if (keyType.isArray || keyType.isMap || keyType.isStruct ||
          keyType.baseType == DataType::VOID) {
        throw runtimeError("Map keys must be scalar values",
                           kv.first->line, kv.first->column);
      }
      m->keyType = keyType;
      m->valueType = valueType;
      first = false;
    }
    try {
      key = convertToType(key, keyType);
      val = convertToType(val, valueType);
    } catch (const std::exception &e) {
      throw runtimeError(e.what(), expr->line, expr->column);
    }
    checkArraySize(m->entries.size() + 1);
    m->entries[std::move(key)] = std::move(val);
  }
  noteArrayAllocation(m->entries.size());
  return m;
}

Value Interpreter::evaluateIndex(IndexExpr *expr) {
  Value arrayVal = evaluate(expr->arrayExpr);

  // Maps index by their declared key type
  if (ValueHelper::isMap(arrayVal)) {
    MapPtr m = std::get<MapPtr>(arrayVal);
    Value key = evaluate(expr->indexExpr);
    try {
      key = convertToType(key, m->keyType);
    } catch (const std::exception &e) {
      throw runtimeError(std::string("Map key: ") + e.what(), expr->line,
                         expr->column);
    }
    auto it = m->entries.find(key);
    if (it == m->entries.end()) {
      throw runtimeError("Map key not found", expr->line, expr->column);
    }
    return it->second;
  }

  // Strings index to their characters
  if (std::holds_alternative<std::string>(arrayVal)) {
    const std::string &s = std::get<std::string>(arrayVal);
    Value indexVal = evaluate(expr->indexExpr);
    uint64_t idx = ValueHelper::toUInt64(indexVal);
    if (idx >= s.size()) {
      throw runtimeError("String index out of bounds", expr->line,
                         expr->column);
    }
    return static_cast<char>(s[static_cast<size_t>(idx)]);
  }

  if (!ValueHelper::isArray(arrayVal)) {
    throw runtimeError("Indexing non-array value", expr->line, expr->column);
  }

  Value indexVal = evaluate(expr->indexExpr);
  uint64_t idx = ValueHelper::toUInt64(indexVal);

  const auto &elems = ValueHelper::arrayElements(arrayVal);
  if (idx >= elems.size()) {
    throw runtimeError("Array index out of bounds", expr->line, expr->column);
  }

  return elems[idx];
}

Value Interpreter::evaluateBinary(BinaryExpr *expr) {
  Value left = evaluate(expr->left);
  // Short-circuit for logical operators at interpreter level to avoid
  // evaluating right operand when not needed
  if (expr->op == BinaryExpr::Operator::LOGICAL_AND) {
    if (!ValueHelper::toBool(left)) {
      return false;
    }
    Value right = evaluate(expr->right);
    return ValueHelper::logicalAnd(left, right);
  }

  if (expr->op == BinaryExpr::Operator::LOGICAL_OR) {
    if (ValueHelper::toBool(left)) {
      return true;
    }
    Value right = evaluate(expr->right);
    return ValueHelper::logicalOr(left, right);
  }

  Value right = evaluate(expr->right);

  switch (expr->op) {
  case BinaryExpr::Operator::ADD:
    return ValueHelper::add(left, right);
  case BinaryExpr::Operator::SUBTRACT:
    return ValueHelper::subtract(left, right);
  case BinaryExpr::Operator::MULTIPLY:
    return ValueHelper::multiply(left, right);
  case BinaryExpr::Operator::DIVIDE:
    return ValueHelper::divide(left, right);
  case BinaryExpr::Operator::MODULO:
    return ValueHelper::modulo(left, right);
  case BinaryExpr::Operator::EQUAL:
    return ValueHelper::equals(left, right);
  case BinaryExpr::Operator::NOT_EQUAL:
    return ValueHelper::notEquals(left, right);
  case BinaryExpr::Operator::LESS_THAN:
    return ValueHelper::lessThan(left, right);
  case BinaryExpr::Operator::GREATER_THAN:
    return ValueHelper::greaterThan(left, right);
  case BinaryExpr::Operator::LESS_EQUAL:
    return ValueHelper::lessOrEqual(left, right);
  case BinaryExpr::Operator::GREATER_EQUAL:
    return ValueHelper::greaterOrEqual(left, right);
  case BinaryExpr::Operator::LOGICAL_AND:
    return ValueHelper::logicalAnd(left, right);
  case BinaryExpr::Operator::LOGICAL_OR:
    return ValueHelper::logicalOr(left, right);
  case BinaryExpr::Operator::BIT_AND:
    return ValueHelper::bitAnd(left, right);
  case BinaryExpr::Operator::BIT_OR:
    return ValueHelper::bitOr(left, right);
  case BinaryExpr::Operator::BIT_XOR:
    return ValueHelper::bitXor(left, right);
  case BinaryExpr::Operator::LSHIFT:
    return ValueHelper::lshift(left, right);
  case BinaryExpr::Operator::RSHIFT:
    return ValueHelper::rshift(left, right);
  }

  throw runtimeError("Unknown binary operator", expr->line, expr->column);
}

Value Interpreter::evaluateUnary(UnaryExpr *expr) {
  Value operand = evaluate(expr->operand);

  switch (expr->op) {
  case UnaryExpr::Operator::NEGATE: {
    DataType operandType = ValueHelper::getType(operand).baseType;
    if (operandType == DataType::DOUBLE || operandType == DataType::FLOAT) {
      return ValueHelper::createValue(operandType, -ValueHelper::toDouble(operand));
    }
    return ValueHelper::createValue(DataType::INT32,
                                    -ValueHelper::toInt64(operand));
  }
  case UnaryExpr::Operator::LOGICAL_NOT:
    return ValueHelper::logicalNot(operand);
  case UnaryExpr::Operator::BIT_NOT:
    return ValueHelper::bitNot(operand);
  }

  throw runtimeError("Unknown unary operator", expr->line, expr->column);
}

Value Interpreter::evaluateConditional(ConditionalExpr *expr) {
  Value cond = evaluate(expr->condition);
  if (ValueHelper::toBool(cond)) {
    return evaluate(expr->thenExpr);
  }
  return evaluate(expr->elseExpr);
}

Value Interpreter::evaluateInterpolatedString(InterpolatedStringExpr *expr) {
  std::string out;
  for (const auto &part : expr->parts) {
    if (part.isExpr) {
      out += ValueHelper::toString(evaluate(part.expr));
    } else {
      out += part.text;
    }
  }
  checkStringLength(out.size());
  return out;
}

Value Interpreter::evaluateMember(MemberExpr *expr) {
  Value object = evaluate(expr->object);
  if (!ValueHelper::isStruct(object)) {
    throw runtimeError("Member access '.' requires a struct value", expr->line,
                       expr->column);
  }
  StructPtr sv = std::get<StructPtr>(object);
  if (!sv) {
    throw runtimeError("Member access on null struct", expr->line,
                       expr->column);
  }
  auto it = sv->fields.find(expr->member);
  if (it == sv->fields.end()) {
    throw runtimeError("Struct '" + sv->typeName + "' has no field '" +
                           expr->member + "'",
                       expr->line, expr->column);
  }
  return it->second;
}

Value Interpreter::constructStruct(const StructDeclPtr &decl,
                                   const std::vector<Value> &args, int line,
                                   int column) {
  if (args.size() != decl->fields.size()) {
    throw runtimeError("Struct '" + decl->name + "' expects " +
                           std::to_string(decl->fields.size()) +
                           " field values, got " + std::to_string(args.size()),
                       line, column);
  }
  auto sv = std::make_shared<StructValue>();
  sv->typeName = decl->name;
  for (const auto &f : decl->fields) {
    sv->fieldOrder.push_back(f.name);
  }
  for (size_t i = 0; i < decl->fields.size(); ++i) {
    Value v;
    try {
      v = convertToType(args[i], decl->fields[i].type);
    } catch (const std::exception &e) {
      throw runtimeError("Field '" + decl->fields[i].name + "': " + e.what(),
                         line, column);
    }
    sv->fields[decl->fields[i].name] = std::move(v);
  }
  noteArrayAllocation(decl->fields.size());
  return sv;
}

// Default-initialized value for a declared type (used by `T x;` and by
// struct fields that have no initializer).
Value Interpreter::defaultValue(const TypeInfo &type) {
  if (type.isMap) {
    TypeInfo valT = type.mapValueType ? *type.mapValueType
                                    : TypeInfo(type.baseType);
    noteArrayAllocation(0);
    return ValueHelper::createMap(TypeInfo(type.keyType), valT);
  }
  if (type.isArray) {
    return ValueHelper::createArray(type.elementType(), {});
  }
  if (type.isStruct) {
    auto it = _structs.find(type.structName);
    if (it == _structs.end()) {
      throw std::runtime_error("Unknown struct type: " + type.structName);
    }
    auto sv = std::make_shared<StructValue>();
    sv->typeName = it->second->name;
    for (const auto &f : it->second->fields) {
      sv->fieldOrder.push_back(f.name);
      sv->fields[f.name] = defaultValue(f.type);
    }
    noteArrayAllocation(sv->fields.size());
    return sv;
  }
  switch (type.baseType) {
  case DataType::INT8:
    return static_cast<int8_t>(0);
  case DataType::UINT8:
    return static_cast<uint8_t>(0);
  case DataType::INT16:
    return static_cast<int16_t>(0);
  case DataType::UINT16:
    return static_cast<uint16_t>(0);
  case DataType::INT32:
    return static_cast<int32_t>(0);
  case DataType::UINT32:
    return static_cast<uint32_t>(0);
  case DataType::INT64:
    return static_cast<int64_t>(0);
  case DataType::FLOAT:
    return static_cast<float>(0.0f);
  case DataType::DOUBLE:
    return static_cast<double>(0.0);
  case DataType::UINT64:
    return static_cast<uint64_t>(0);
  case DataType::STRING:
    return std::string("");
  case DataType::BOOL:
    return false;
  case DataType::CHAR:
    return static_cast<char>(0);
  case DataType::VOID:
    return static_cast<int32_t>(0);
  }
  return static_cast<int32_t>(0);
}

Value Interpreter::evaluateUpdate(UpdateExpr *expr) {
  Value delta = static_cast<int32_t>(1);
  auto bump = [&](const Value &old) -> Value {
    try {
      return expr->increment ? ValueHelper::add(old, delta)
                             : ValueHelper::subtract(old, delta);
    } catch (const std::exception &e) {
      throw runtimeError(e.what(), expr->line, expr->column);
    }
  };

  if (auto *var = dynamic_cast<VariableExpr *>(expr->target.get())) {
    Value old = readVariable(var->name, expr->line, expr->column);
    Value next = bump(old);
    writeVariable(var->name, next, expr->line, expr->column);
    return expr->prefix ? next : old;
  }

  if (auto *mem = dynamic_cast<MemberExpr *>(expr->target.get())) {
    Value object = evaluate(mem->object);
    if (!ValueHelper::isStruct(object)) {
      throw runtimeError("++/-- member target must be a struct", expr->line,
                         expr->column);
    }
    StructPtr sv = std::get<StructPtr>(object);
    auto it = sv->fields.find(mem->member);
    if (it == sv->fields.end()) {
      throw runtimeError("Struct '" + sv->typeName + "' has no field '" +
                             mem->member + "'",
                         expr->line, expr->column);
    }
    Value old = it->second;
    Value next = bump(old);
    auto declIt = _structs.find(sv->typeName);
    if (declIt != _structs.end()) {
      for (const auto &f : declIt->second->fields) {
        if (f.name == mem->member) {
          try {
            next = convertToType(next, f.type);
          } catch (const std::exception &e) {
            throw runtimeError(e.what(), expr->line, expr->column);
          }
          break;
        }
      }
    }
    it->second = next;
    return expr->prefix ? next : old;
  }

  if (auto *idx = dynamic_cast<IndexExpr *>(expr->target.get())) {
    Value container = evaluate(idx->arrayExpr);
    if (ValueHelper::isMap(container)) {
      MapPtr m = std::get<MapPtr>(container);
      Value key = evaluate(idx->indexExpr);
      try {
        key = convertToType(key, m->keyType);
      } catch (const std::exception &e) {
        throw runtimeError(std::string("Map key: ") + e.what(), expr->line,
                           expr->column);
      }
      auto it = m->entries.find(key);
      if (it == m->entries.end()) {
        throw runtimeError("Map key not found", expr->line, expr->column);
      }
      Value old = it->second;
      Value next = bump(old);
      try {
        it->second = convertToType(next, m->valueType);
      } catch (const std::exception &e) {
        throw runtimeError(e.what(), expr->line, expr->column);
      }
      return expr->prefix ? next : old;
    }
    if (!ValueHelper::isArray(container)) {
      throw runtimeError("++/-- index target must be an array or map",
                         expr->line, expr->column);
    }
    Value indexVal = evaluate(idx->indexExpr);
    uint64_t i = ValueHelper::toUInt64(indexVal);
    auto &elems = ValueHelper::arrayElements(container);
    if (i >= elems.size()) {
      throw runtimeError("Array index out of bounds", expr->line, expr->column);
    }
    Value old = elems[i];
    Value next = bump(old);
    TypeInfo elemType = ValueHelper::arrayElementType(container);
    try {
      elems[i] = convertToType(next, elemType);
    } catch (const std::exception &e) {
      throw runtimeError(e.what(), expr->line, expr->column);
    }
    return expr->prefix ? next : old;
  }

  throw runtimeError("++/-- require a variable or index target", expr->line,
                     expr->column);
}

Value Interpreter::evaluateCall(CallExpr *expr) {
  // Inline cache for procedures / externals
  if (expr->cacheVersion == _callCacheVersion) {
    if (expr->cachedIsProcedure) {
      std::vector<Value> args;
      args.reserve(expr->arguments.size());
      for (auto &argExpr : expr->arguments) {
        args.push_back(evaluate(argExpr));
      }
      if (auto proc = expr->cachedProcedure.lock()) {
        _callSiteLine = expr->line;
        _callSiteColumn = expr->column;
        return executeProcedure(proc, args);
      }
    }
    if (expr->cachedIsExternal && expr->cachedExternal) {
      std::vector<Value> args;
      args.reserve(expr->arguments.size());
      for (auto &argExpr : expr->arguments) {
        args.push_back(evaluate(argExpr));
      }
      Value r = expr->cachedExternal(args);
      checkResultSize(r);
      return r;
    }
  }

  // Check if it's a procedure call
  if (auto it = _procedures.find(expr->functionName); it != _procedures.end()) {
    expr->cacheVersion = _callCacheVersion;
    expr->cachedIsProcedure = true;
    expr->cachedIsExternal = false;
    expr->cachedProcedure = it->second;
    std::vector<Value> args;
    args.reserve(expr->arguments.size());
    for (auto &argExpr : expr->arguments) {
      args.push_back(evaluate(argExpr));
    }
    _callSiteLine = expr->line;
    _callSiteColumn = expr->column;
    return executeProcedure(it->second, args);
  }

  // Check if it's a registered external function
  auto extIt = _externalFunctions.find(expr->functionName);
  if (extIt != _externalFunctions.end()) {
    expr->cacheVersion = _callCacheVersion;
    expr->cachedIsProcedure = false;
    expr->cachedIsExternal = true;
    expr->cachedExternal = extIt->second;

    std::vector<Value> args;
    args.reserve(expr->arguments.size());
    for (auto &argExpr : expr->arguments) {
      args.push_back(evaluate(argExpr));
    }
    Value r = extIt->second(args);
    checkResultSize(r);
    return r;
  }

  // Struct constructor: Point(1, 2) with positional field values. User
  // procedures and external functions take precedence over this.
  if (auto sIt = _structs.find(expr->functionName); sIt != _structs.end()) {
    std::vector<Value> args;
    args.reserve(expr->arguments.size());
    for (auto &argExpr : expr->arguments) {
      args.push_back(evaluate(argExpr));
    }
    return constructStruct(sIt->second, args, expr->line, expr->column);
  }

  // Built-ins are the lowest precedence: procedures and host-registered
  // external functions may override them.
  if (Builtins::isBuiltin(expr->functionName)) {
    Value r = Builtins::call(*this, expr->functionName, expr);
    checkResultSize(r);
    return r;
  }

  throw runtimeError("Undefined function: " + expr->functionName, expr->line,
                     expr->column);
}

void Interpreter::executeExpression(ExpressionStmt *stmt) {
  evaluate(stmt->expression);
}

void Interpreter::executeVarDecl(VarDeclStmt *stmt) {
  Value value;

  if (stmt->initializer) {
    value = evaluate(stmt->initializer);
    value = convertToType(value, stmt->type);
  } else {
    try {
      value = defaultValue(stmt->type);
    } catch (const std::exception &e) {
      throw runtimeError(e.what(), stmt->line, stmt->column);
    }
  }

  _currentEnv->define(stmt->name, value);
}

Value Interpreter::applyAssignOp(AssignStmt::Operator op, const Value &current,
                                 const Value &value, int line, int column) {
  try {
    Value out;
    switch (op) {
    case AssignStmt::Operator::ASSIGN:
      out = value;
      break;
    case AssignStmt::Operator::PLUS_ASSIGN:
      out = ValueHelper::add(current, value);
      break;
    case AssignStmt::Operator::MINUS_ASSIGN:
      out = ValueHelper::subtract(current, value);
      break;
    case AssignStmt::Operator::MULT_ASSIGN:
      out = ValueHelper::multiply(current, value);
      break;
    case AssignStmt::Operator::DIV_ASSIGN:
      out = ValueHelper::divide(current, value);
      break;
    case AssignStmt::Operator::MOD_ASSIGN:
      out = ValueHelper::modulo(current, value);
      break;
    case AssignStmt::Operator::BAND_ASSIGN:
      out = ValueHelper::bitAnd(current, value);
      break;
    case AssignStmt::Operator::BOR_ASSIGN:
      out = ValueHelper::bitOr(current, value);
      break;
    case AssignStmt::Operator::BXOR_ASSIGN:
      out = ValueHelper::bitXor(current, value);
      break;
    case AssignStmt::Operator::SHL_ASSIGN:
      out = ValueHelper::lshift(current, value);
      break;
    case AssignStmt::Operator::SHR_ASSIGN:
      out = ValueHelper::rshift(current, value);
      break;
    }
    if (op != AssignStmt::Operator::ASSIGN) {
      checkResultSize(out); // s += ... can grow strings unboundedly
    }
    return out;
  } catch (const std::exception &e) {
    throw runtimeError(e.what(), line, column);
  }
  throw runtimeError("Unknown assignment operator", line, column);
}

void Interpreter::executeAssign(AssignStmt *stmt) {
  Value value = evaluate(stmt->value);

  if (_currentEnv->has(stmt->variableName)) {
    Value result = applyAssignOp(stmt->op, _currentEnv->get(stmt->variableName),
                                 value, stmt->line, stmt->column);
    _currentEnv->assign(stmt->variableName, result);
    return;
  }

  auto extIt = _externalVariables.find(stmt->variableName);
  if (extIt == _externalVariables.end()) {
    throw runtimeError("Undefined variable: " + stmt->variableName, stmt->line,
                       stmt->column);
  }

  auto &extVar = extIt->second;
  if (!extVar.setter) {
    throw runtimeError("External variable '" + stmt->variableName +
                           "' is read-only",
                       stmt->line, stmt->column);
  }

  if (stmt->op == AssignStmt::Operator::ASSIGN) {
    extVar.setter(value);
    return;
  }

  if (!extVar.getter) {
    throw runtimeError("External variable '" + stmt->variableName +
                           "' cannot be read",
                       stmt->line, stmt->column);
  }

  Value result = applyAssignOp(stmt->op, extVar.getter(), value, stmt->line,
                               stmt->column);
  extVar.setter(result);
}

void Interpreter::executeIndexAssign(IndexAssignStmt *stmt) {
  Value arrayVal = evaluate(stmt->arrayExpr);

  if (ValueHelper::isMap(arrayVal)) {
    MapPtr m = std::get<MapPtr>(arrayVal);
    Value key = evaluate(stmt->indexExpr);
    try {
      key = convertToType(key, m->keyType);
    } catch (const std::exception &e) {
      throw runtimeError(std::string("Map key: ") + e.what(), stmt->line,
                         stmt->column);
    }
    Value rawValue = evaluate(stmt->value);
    auto it = m->entries.find(key);
    if (stmt->op != AssignStmt::Operator::ASSIGN) {
      if (it == m->entries.end()) {
        throw runtimeError("Map key not found for compound assignment",
                           stmt->line, stmt->column);
      }
      rawValue = applyAssignOp(stmt->op, it->second, rawValue, stmt->line,
                               stmt->column);
    }
    Value converted;
    try {
      converted = convertToType(rawValue, m->valueType);
    } catch (const std::exception &e) {
      throw runtimeError(e.what(), stmt->line, stmt->column);
    }
    if (it == m->entries.end()) {
      checkArraySize(m->entries.size() + 1);
      m->entries.emplace(std::move(key), std::move(converted));
    } else {
      it->second = converted;
    }
    return;
  }

  if (!ValueHelper::isArray(arrayVal)) {
    throw runtimeError("Index assignment on non-array value", stmt->line, stmt->column);
  }

  Value indexVal = evaluate(stmt->indexExpr);
  uint64_t idx = ValueHelper::toUInt64(indexVal);

  std::vector<Value> &elems = ValueHelper::arrayElements(arrayVal);
  if (idx >= elems.size()) {
    throw runtimeError("Array index out of bounds", stmt->line, stmt->column);
  }

  TypeInfo elementType = ValueHelper::arrayElementType(arrayVal);
  Value rawValue = evaluate(stmt->value);
  if (stmt->op != AssignStmt::Operator::ASSIGN) {
    rawValue = applyAssignOp(stmt->op, elems[idx], rawValue, stmt->line,
                             stmt->column);
  }
  Value converted;
  try {
    converted = convertToType(rawValue, elementType);
  } catch (const std::exception &e) {
    throw runtimeError(e.what(), stmt->line, stmt->column);
  }

  elems[idx] = converted;
}

void Interpreter::executeMemberAssign(MemberAssignStmt *stmt) {
  Value object = evaluate(stmt->object);
  if (!ValueHelper::isStruct(object)) {
    throw runtimeError("Member assignment '.' requires a struct value",
                       stmt->line, stmt->column);
  }
  StructPtr sv = std::get<StructPtr>(object);
  if (!sv) {
    throw runtimeError("Member assignment on null struct", stmt->line,
                       stmt->column);
  }

  auto fieldIt = sv->fields.find(stmt->member);
  if (fieldIt == sv->fields.end()) {
    throw runtimeError("Struct '" + sv->typeName + "' has no field '" +
                           stmt->member + "'",
                       stmt->line, stmt->column);
  }

  Value rawValue = evaluate(stmt->value);
  if (stmt->op != AssignStmt::Operator::ASSIGN) {
    rawValue = applyAssignOp(stmt->op, fieldIt->second, rawValue, stmt->line,
                             stmt->column);
  }

  // Convert to the declared field type when the decl is known.
  auto declIt = _structs.find(sv->typeName);
  if (declIt != _structs.end()) {
    for (const auto &f : declIt->second->fields) {
      if (f.name == stmt->member) {
        try {
          rawValue = convertToType(rawValue, f.type);
        } catch (const std::exception &e) {
          throw runtimeError("Field '" + stmt->member + "': " + e.what(),
                             stmt->line, stmt->column);
        }
        break;
      }
    }
  }

  fieldIt->second = std::move(rawValue);
}

void Interpreter::executeBlock(BlockStmt *stmt) {
  _currentEnv->enterScope();

  try {
    for (auto &statement : stmt->statements) {
      execute(statement);
    }
    _currentEnv->exitScope();
  } catch (...) {
    _currentEnv->exitScope();
    throw;
  }
}

void Interpreter::executeIf(IfStmt *stmt) {
  Value condition = evaluate(stmt->condition);

  if (ValueHelper::toBool(condition)) {
    execute(stmt->thenBranch);
  } else if (stmt->elseBranch) {
    execute(stmt->elseBranch);
  }
}

void Interpreter::executeWhile(WhileStmt *stmt) {
  while (ValueHelper::toBool(evaluate(stmt->condition))) {
    try {
      execute(stmt->body);
    } catch (const ContinueException &) {
      continue;
    } catch (const BreakException &) {
      break;
    }
  }
}

void Interpreter::executeFor(ForStmt *stmt) {
  _currentEnv->enterScope();

  try {
    // Initialize
    if (stmt->initializer) {
      execute(stmt->initializer);
    }

    // Loop
    while (true) {
      // Check condition
      if (stmt->condition && !ValueHelper::toBool(evaluate(stmt->condition))) {
        break;
      }

      // Execute body
      try {
        execute(stmt->body);
      } catch (const ContinueException &) {
        // Skip to increment
      } catch (const BreakException &) {
        break;
      }

      // Increment
      if (stmt->increment) {
        execute(stmt->increment);
      }
    }

    _currentEnv->exitScope();
  } catch (...) {
    _currentEnv->exitScope();
    throw;
  }
}

void Interpreter::executeReturn(ReturnStmt *stmt) {
  Value value;

  if (stmt->value) {
    value = evaluate(stmt->value);
  } else {
    value = static_cast<int32_t>(0); // Dummy value for void returns
  }

  throw ReturnException(value);
}

void Interpreter::executeDoWhile(DoWhileStmt *stmt) {
  while (true) {
    try {
      execute(stmt->body);
    } catch (const ContinueException &) {
      // skip to condition check
    } catch (const BreakException &) {
      break;
    }

    if (!ValueHelper::toBool(evaluate(stmt->condition))) {
      break;
    }
  }
}

void Interpreter::executeSwitch(SwitchStmt *stmt) {
  Value control = evaluate(stmt->expression);
  bool matched = false;

  for (size_t i = 0; i < stmt->cases.size(); ++i) {
    const auto &caseEntry = stmt->cases[i];

    if (!matched) {
      if (caseEntry.isDefault) {
        matched = true;
      } else {
        Value caseVal = evaluate(caseEntry.matchExpr);
        if (ValueHelper::equals(control, caseVal)) {
          matched = true;
        }
      }
    }

    if (matched) {
      try {
        for (auto &s : caseEntry.statements) {
          execute(s);
        }
      } catch (const BreakException &) {
        return;
      }
    }
  }
}

void Interpreter::executeForEach(ForEachStmt *stmt) {
  Value iterable = evaluate(stmt->iterable);
  TypeInfo target = stmt->elemType;

  _currentEnv->enterScope();
  try {
    if (ValueHelper::isArray(iterable)) {
      ArrayPtr arr = std::get<ArrayPtr>(iterable);
      // Live view: elements pushed during iteration are also visited.
      for (size_t i = 0; i < arr->elements.size(); ++i) {
        Value elem;
        try {
          elem = convertToType(arr->elements[i], target);
        } catch (const std::exception &e) {
          throw runtimeError(e.what(), stmt->line, stmt->column);
        }
        _currentEnv->define(stmt->varName, elem);
        try {
          execute(stmt->body);
        } catch (const ContinueException &) {
          continue;
        } catch (const BreakException &) {
          break;
        }
      }
    } else if (std::holds_alternative<std::string>(iterable)) {
      for (char c : std::get<std::string>(iterable)) {
        Value elem;
        try {
          elem = convertToType(static_cast<char>(c), target);
        } catch (const std::exception &e) {
          throw runtimeError(e.what(), stmt->line, stmt->column);
        }
        _currentEnv->define(stmt->varName, elem);
        try {
          execute(stmt->body);
        } catch (const ContinueException &) {
          continue;
        } catch (const BreakException &) {
          break;
        }
      }
    } else if (ValueHelper::isMap(iterable)) {
      MapPtr m = std::get<MapPtr>(iterable);
      for (auto it = m->entries.begin(); it != m->entries.end(); ++it) {
        Value elem;
        try {
          elem = convertToType(it->first, target);
        } catch (const std::exception &e) {
          throw runtimeError(e.what(), stmt->line, stmt->column);
        }
        _currentEnv->define(stmt->varName, elem);
        try {
          execute(stmt->body);
        } catch (const ContinueException &) {
          continue;
        } catch (const BreakException &) {
          break;
        }
      }
    } else {
      throw runtimeError("for-each requires an array, map, or string",
                         stmt->line, stmt->column);
    }
    _currentEnv->exitScope();
  } catch (...) {
    _currentEnv->exitScope();
    throw;
  }
}

void Interpreter::executeBreak(BreakStmt * /*stmt*/) { throw BreakException(); }

void Interpreter::executeContinue(ContinueStmt * /*stmt*/) {
  throw ContinueException();
}

RuntimeError Interpreter::runtimeError(const std::string &message, int line,
                                       int column) {
  return RuntimeError(message, _currentFile, line, column, _currentProcedure);
}

void Interpreter::consumeExecutionStep(int line, int column) {
  if (_maxSteps == 0) {
    return;
  }

  ++_currentSteps;
  if (_currentSteps > _maxSteps) {
    throw runtimeError("Maximum execution steps exceeded", line, column);
  }
}

Value Interpreter::convertToType(const Value &val, const TypeInfo &targetType) {
  TypeInfo sourceType = ValueHelper::getType(val);

  // Struct targets accept only values of the same struct type.
  if (targetType.isStruct && !targetType.isArray) {
    if (sourceType.isStruct && !sourceType.isArray &&
        sourceType.structName == targetType.structName) {
      return val;
    }
    throw std::runtime_error("Expected struct '" + targetType.structName +
                             "', got '" +
                             ValueHelper::typeToString(sourceType) + "'");
  }
  if (sourceType.isStruct && !sourceType.isArray) {
    throw std::runtime_error("Cannot convert struct '" +
                             sourceType.structName +
                             "' to non-struct type");
  }

  if (targetType.isMap) {
    if (!ValueHelper::isMap(val)) {
      throw std::runtime_error("Expected map value");
    }
    MapPtr src = std::get<MapPtr>(val);
    TypeInfo keyT(targetType.keyType);
    TypeInfo valT = targetType.mapValueType
                        ? *targetType.mapValueType
                        : TypeInfo(targetType.baseType);
    if (src->entries.empty()) {
      // Empty map adopts the declared key/value types
      noteArrayAllocation(0);
      return ValueHelper::createMap(keyT, valT);
    }
    if (src->keyType == keyT && src->valueType == valT) {
      return val;
    }
    auto m = ValueHelper::createMap(keyT, valT);
    for (const auto &kv : src->entries) {
      m->entries[convertToType(kv.first, keyT)] =
          convertToType(kv.second, valT);
    }
    checkArraySize(m->entries.size());
    noteArrayAllocation(m->entries.size());
    return m;
  }

  if (sourceType.isMap) {
    throw std::runtime_error("Cannot convert map to non-map type");
  }

  if (targetType.isArray) {
    if (!ValueHelper::isArray(val)) {
      throw std::runtime_error("Expected array value");
    }
    TypeInfo targetElem = targetType.elementType();
    TypeInfo elemType = ValueHelper::arrayElementType(val);
    if (elemType == targetElem) {
      return val; // already correct element type
    }
    const auto &elems = ValueHelper::arrayElements(val);
    std::vector<Value> converted;
    converted.reserve(elems.size());
    for (const auto &e : elems) {
      converted.push_back(convertToType(e, targetElem));
    }
    noteArrayAllocation(converted.size());
    return ValueHelper::createArray(targetElem, converted);
  }

  if (sourceType.isArray) {
    throw std::runtime_error("Cannot convert array to scalar type");
  }

  if (sourceType.baseType == targetType.baseType) {
    return val;
  }

  switch (targetType.baseType) {
  case DataType::CHAR:
  case DataType::INT8:
  case DataType::INT16:
  case DataType::INT32:
  case DataType::INT64:
    return ValueHelper::createValue(targetType.baseType, ValueHelper::toInt64(val));
  case DataType::UINT8:
  case DataType::UINT16:
  case DataType::UINT32:
  case DataType::UINT64:
    return ValueHelper::createValue(targetType.baseType, ValueHelper::toUInt64(val));
  case DataType::FLOAT:
  case DataType::DOUBLE:
    return ValueHelper::createValue(targetType.baseType, ValueHelper::toDouble(val));
  case DataType::STRING:
    return ValueHelper::createValue(targetType.baseType, ValueHelper::toString(val));
  case DataType::BOOL:
    return ValueHelper::createValue(targetType.baseType, ValueHelper::toBool(val));
  case DataType::VOID:
    return val;
  }

  throw std::runtime_error("Unsupported conversion");
}

} // namespace Script
