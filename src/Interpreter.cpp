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
    addProcedure(proc, script->filename);
  }
  for (auto &s : script->structs) {
    _structs[s->name] = s;
    _structFiles[s.get()] = script->filename;
  }
  for (auto &e : script->enums) {
    _enums[e->name] = e;
    _enumFiles[e.get()] = script->filename;
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

void Interpreter::addEnum(const EnumDeclPtr &decl, const std::string &file) {
  if (!decl) {
    return;
  }
  _enums[decl->name] = decl;
  _enumFiles[decl.get()] = file;
}

EnumDeclPtr Interpreter::removeEnum(const std::string &name) {
  auto it = _enums.find(name);
  if (it == _enums.end()) {
    return nullptr;
  }
  EnumDeclPtr old = it->second;
  _enumFiles.erase(old.get());
  _enums.erase(it);
  return old;
}

bool Interpreter::hasEnum(const std::string &name) const {
  return _enums.find(name) != _enums.end();
}

EnumDeclPtr Interpreter::getEnum(const std::string &name) const {
  auto it = _enums.find(name);
  return it != _enums.end() ? it->second : nullptr;
}

std::vector<std::string> Interpreter::getEnumNames() const {
  std::vector<std::string> names;
  names.reserve(_enums.size());
  for (const auto &kv : _enums) {
    names.push_back(kv.first);
  }
  return names;
}

void Interpreter::disableBuiltin(const std::string &name) {
  _disabledBuiltins.insert(name);
  ++_callCacheVersion;
}

void Interpreter::enableBuiltin(const std::string &name) {
  _disabledBuiltins.erase(name);
  ++_callCacheVersion;
}

bool Interpreter::isBuiltinEnabled(const std::string &name) const {
  return _disabledBuiltins.find(name) == _disabledBuiltins.end();
}

ProcedureDeclPtr Interpreter::removeProcedure(const std::string &name) {
  auto it = _procedures.find(name);
  if (it == _procedures.end()) {
    return nullptr;
  }
  ProcedureDeclPtr old = it->second.front();
  for (auto &p : it->second) {
    _procedureFiles.erase(p.get());
  }
  _procedures.erase(it);
  ++_callCacheVersion;
  return old;
}

// Whether two parameter type lists are identical (overloads must differ).
static bool sameSignature(const std::vector<Parameter> &a,
                          const std::vector<Parameter> &b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].type != b[i].type) {
      return false;
    }
  }
  return true;
}

void Interpreter::addProcedure(const ProcedureDeclPtr &proc,
                               const std::string &file) {
  if (!proc) {
    return;
  }
  auto &set = _procedures[proc->name];
  for (auto &existing : set) {
    if (sameSignature(existing->parameters, proc->parameters)) {
      _procedureFiles.erase(existing.get());
      existing = proc;
      _procedureFiles[proc.get()] = file;
      ++_callCacheVersion;
      return;
    }
  }
  set.push_back(proc);
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
    throw RuntimeError(
        "Maximum array allocation count exceeded (limit " +
            std::to_string(_maxAllocations) + ")",
        _currentFile, 0, 0, _currentProcedure, /*fatal=*/true);
  }
  checkArraySize(elements);
}

void Interpreter::checkArraySize(size_t elements) {
  if (_maxArraySize > 0 && elements > _maxArraySize) {
    throw RuntimeError("Array size limit exceeded (" +
                           std::to_string(elements) + " > " +
                           std::to_string(_maxArraySize) + ")",
                       _currentFile, 0, 0, _currentProcedure, /*fatal=*/true);
  }
}

void Interpreter::checkStringLength(size_t length) {
  if (_maxStringLength > 0 && length > _maxStringLength) {
    throw RuntimeError("String length limit exceeded (" +
                           std::to_string(length) + " > " +
                           std::to_string(_maxStringLength) + ")",
                       _currentFile, 0, 0, _currentProcedure, /*fatal=*/true);
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
  if (it == _procedures.end() || it->second.empty()) {
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
    ProcedureDeclPtr proc =
        it->second.size() == 1
            ? it->second.front()
            : resolveOverload(name, it->second, arguments, 0, 0);
    Value out = executeProcedure(proc, arguments);
    if (topLevel) {
      _executionActive = false;
    }
    return out;
  } catch (const ScriptException &se) {
    if (topLevel) {
      _executionActive = false;
      std::string msg = "Uncaught script exception: ";
      try {
        msg += ValueHelper::toString(se.value);
      } catch (...) {
        msg += "<unprintable>";
      }
      if (!se.procedure.empty()) {
        msg += " (thrown in " + se.procedure + " at line " +
               std::to_string(se.line) + ")";
      }
      throw RuntimeError(msg, se.file, se.line, se.column, se.procedure);
    }
    throw;
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
  } catch (const ScriptException &se) {
    _currentEnv = previousEnv;
    if (topLevel) {
      _executionActive = false;
      std::string msg = "Uncaught script exception: ";
      try {
        msg += ValueHelper::toString(se.value);
      } catch (...) {
        msg += "<unprintable>";
      }
      throw RuntimeError(msg, se.file, se.line, se.column, se.procedure);
    }
    throw;
  } catch (...) {
    _currentEnv = previousEnv;
    if (topLevel)
      _executionActive = false;
    throw;
  }
}

Value Interpreter::executeProcedure(ProcedureDeclPtr proc,
                                    const std::vector<Value> &arguments) {
  std::string file;
  auto fileIt = _procedureFiles.find(proc.get());
  if (fileIt != _procedureFiles.end()) {
    file = fileIt->second;
  }
  return invokeCallable(proc->name, proc->parameters, proc->body,
                        proc->returnType, file, nullptr, arguments, proc->line,
                        proc->column);
}

// Number of leading parameters that have no default expression.
static size_t requiredParamCount(const std::vector<Parameter> &params) {
  size_t n = params.size();
  while (n > 0 && params[n - 1].defaultValue) {
    --n;
  }
  return n;
}

bool Interpreter::runtimeConvertible(const TypeInfo &src,
                                     const TypeInfo &dst) {
  if (dst.isAuto) {
    return true;
  }
  if (dst.isFunction || src.isFunction) {
    return dst.isFunction && src.isFunction;
  }
  if (dst.isStruct && !dst.isArray) {
    return src.isStruct && !src.isArray && src.structName == dst.structName;
  }
  if (src.isStruct && !src.isArray) {
    return false;
  }
  if (dst.isMap) {
    if (!src.isMap) {
      return false;
    }
    TypeInfo sk(src.keyType);
    TypeInfo dk(dst.keyType);
    TypeInfo sv = src.mapValueType ? *src.mapValueType
                                 : TypeInfo(src.baseType);
    TypeInfo dv = dst.mapValueType ? *dst.mapValueType
                                 : TypeInfo(dst.baseType);
    if (sk.baseType == DataType::VOID && sv.baseType == DataType::VOID) {
      return true; // empty literal adopts the target's types
    }
    return runtimeConvertible(sk, dk) && runtimeConvertible(sv, dv);
  }
  if (src.isMap) {
    return false;
  }
  if (dst.isArray) {
    if (!src.isArray) {
      return false;
    }
    TypeInfo se = src.elementType();
    if (se.baseType == DataType::VOID && !se.isStruct && !se.isFunction) {
      return true; // empty literal adopts the target's element type
    }
    return runtimeConvertible(se, dst.elementType());
  }
  if (src.isArray) {
    return false;
  }
  if (src.baseType == DataType::STRING) {
    return dst.baseType == DataType::STRING; // strings don't parse to numbers
  }
  return true; // scalar -> scalar goes through the numeric/bool/string helpers
}

ProcedureDeclPtr
Interpreter::resolveOverload(const std::string &name,
                             const std::vector<ProcedureDeclPtr> &set,
                             const std::vector<Value> &args, int line,
                             int column) {
  std::vector<ProcedureDeclPtr> viable;
  for (const auto &p : set) {
    size_t req = requiredParamCount(p->parameters);
    if (args.size() >= req && args.size() <= p->parameters.size()) {
      viable.push_back(p);
    }
  }
  if (viable.empty()) {
    throw runtimeError("No overload of '" + name + "' accepts " +
                           std::to_string(args.size()) + " argument(s)",
                       line, column);
  }
  if (viable.size() == 1) {
    return viable.front();
  }

  int bestScore = -1;
  ProcedureDeclPtr best;
  bool tied = false;
  for (const auto &p : viable) {
    int score = 0;
    bool ok = true;
    if (p->parameters.size() == args.size()) {
      score += 1; // prefer the overload not relying on defaults
    }
    for (size_t i = 0; i < args.size(); ++i) {
      const TypeInfo &dst = p->parameters[i].type;
      if (dst.isAuto) {
        score += 1;
        continue;
      }
      TypeInfo src = ValueHelper::getType(args[i]);
      if (src == dst) {
        score += 4;
      } else if (src.baseType == dst.baseType && !src.isArray &&
                 !src.isMap && !src.isStruct && !src.isFunction) {
        score += 2; // same family (e.g. int literal -> int64)
      } else if (runtimeConvertible(src, dst)) {
        score += 1;
      } else {
        ok = false;
        break;
      }
    }
    if (!ok) {
      continue;
    }
    if (score > bestScore) {
      bestScore = score;
      best = p;
      tied = false;
    } else if (score == bestScore) {
      tied = true;
    }
  }
  if (!best) {
    throw runtimeError("No viable overload of '" + name + "' for the given "
                           "argument types",
                       line, column);
  }
  if (tied) {
    throw runtimeError("Ambiguous call to overloaded procedure '" + name + "'",
                       line, column);
  }
  return best;
}

Value Interpreter::callFunctionValue(const FuncPtr &fn,
                                     const std::vector<Value> &args, int line,
                                     int column) {
  if (!fn) {
    throw runtimeError("Cannot call a null function value", line, column);
  }
  if (!fn->procs.empty()) {
    ProcedureDeclPtr proc =
        fn->procs.size() == 1
            ? fn->procs.front()
            : resolveOverload(fn->displayName, fn->procs, args, line, column);
    std::string file;
    auto fileIt = _procedureFiles.find(proc.get());
    if (fileIt != _procedureFiles.end()) {
      file = fileIt->second;
    }
    std::string name =
        fn->displayName.empty() ? proc->name : fn->displayName;
    return invokeCallable(name, proc->parameters, proc->body, proc->returnType,
                          file,
                          fn->captured.empty() ? nullptr : &fn->captured, args,
                          line, column);
  }
  TypeInfo ret = fn->hasRetType ? fn->declaredRetType : TypeInfo::autoType();
  std::string name = fn->displayName.empty() ? "<lambda>" : fn->displayName;
  return invokeCallable(name, fn->parameters, fn->body, ret, _currentFile,
                        fn->captured.empty() ? nullptr : &fn->captured, args,
                        line, column);
}

Value Interpreter::invokeCallable(
    const std::string &name, const std::vector<Parameter> &params,
    StmtPtr body, const TypeInfo &retType, const std::string &file,
    const std::unordered_map<std::string, Value> *captures,
    std::vector<Value> args, int line, int column) {
  std::string previousProcedure = _currentProcedure;
  std::string previousFile = _currentFile;
  int previousCallLine = _callSiteLine;
  int previousCallCol = _callSiteColumn;
  _currentProcedure = name;
  _currentFile = file;
  _callStack.push_back(name);

  auto restoreFrame = [&]() {
    _currentProcedure = previousProcedure;
    _currentFile = previousFile;
    _callSiteLine = previousCallLine;
    _callSiteColumn = previousCallCol;
    if (!_callStack.empty()) {
      _callStack.pop_back();
    }
  };

  if (_maxCallDepth > 0 && _currentCallDepth >= _maxCallDepth) {
    restoreFrame();
    throw RuntimeError("Maximum call depth exceeded", _currentFile, line,
                       column, name, /*fatal=*/true);
  }

  size_t required = requiredParamCount(params);
  if (args.size() < required || args.size() > params.size()) {
    std::stringstream ss;
    ss << "'" << name << "' expects ";
    if (required == params.size()) {
      ss << params.size();
    } else {
      ss << required << "-" << params.size();
    }
    ss << " arguments, got " << args.size();
    restoreFrame();
    throw runtimeError(ss.str(), line, column);
  }

  ++_currentCallDepth;

  // Captured variables (lambdas, bound methods) sit between the caller
  // environment and the call's own bindings.
  std::unique_ptr<Environment> capEnv;
  Environment *parentEnv = _currentEnv;
  if (captures && !captures->empty()) {
    capEnv = std::make_unique<Environment>(_currentEnv);
    for (const auto &kv : *captures) {
      capEnv->define(kv.first, kv.second);
    }
    parentEnv = capEnv.get();
  }

  Environment callEnv(parentEnv);
  Environment *previousEnv = _currentEnv;
  _currentEnv = &callEnv;

  auto restoreAll = [&]() {
    _currentEnv = previousEnv;
    restoreFrame();
  };

  bool returnsVoid = retType.baseType == DataType::VOID &&
                     !retType.isArray && !retType.isStruct &&
                     !retType.isMap && !retType.isFunction &&
                     !retType.isAuto;

  try {
    // Bind provided parameters
    for (size_t i = 0; i < args.size(); ++i) {
      Value arg = args[i];
      if (!params[i].type.isAuto) {
        try {
          arg = convertToType(arg, params[i].type);
        } catch (const std::exception &e) {
          throw runtimeError(e.what(), line, column);
        }
      }
      _currentEnv->define(params[i].name, arg);
    }
    // Default arguments evaluate in the callee scope so they may reference
    // earlier parameters and captured variables.
    for (size_t i = args.size(); i < params.size(); ++i) {
      const Parameter &p = params[i];
      Value arg;
      try {
        arg = evaluate(p.defaultValue);
      } catch (const RuntimeError &) {
        throw;
      } catch (const std::exception &e) {
        throw runtimeError(std::string("Default argument for '") + p.name +
                               "': " + e.what(),
                           line, column);
      }
      if (!p.type.isAuto) {
        try {
          arg = convertToType(arg, p.type);
        } catch (const std::exception &e) {
          throw runtimeError(e.what(), line, column);
        }
      }
      _currentEnv->define(p.name, arg);
    }

    execute(body);

    restoreAll();
    --_currentCallDepth;
    if (returnsVoid || retType.isAuto) {
      return static_cast<int32_t>(0);
    }
    throw runtimeError("Non-void procedure must return a value", line, column);

  } catch (const ReturnException &ret) {
    restoreAll();
    --_currentCallDepth;

    if (returnsVoid) {
      return static_cast<int32_t>(0);
    }
    if (retType.isAuto) {
      return ret.value;
    }

    try {
      return convertToType(ret.value, retType);
    } catch (const std::exception &e) {
      throw runtimeError(e.what(), line, column);
    }
  } catch (const ScriptException &) {
    restoreAll();
    --_currentCallDepth;
    throw;
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
  auto it = _procedures.find(name);
  return it != _procedures.end() && !it->second.empty();
}

ProcedureDeclPtr Interpreter::getProcedure(const std::string &name) const {
  auto it = _procedures.find(name);
  if (it != _procedures.end() && !it->second.empty()) {
    return it->second.front();
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
    if (auto *lam = dynamic_cast<LambdaExpr *>(expr.get())) {
      return evaluateLambda(lam);
    }
    if (auto *em = dynamic_cast<EnumMemberExpr *>(expr.get())) {
      return evaluateEnumMember(em);
    }

    throw runtimeError("Unknown expression type", expr->line, expr->column);
  } catch (RuntimeError &) {
    throw;
  } catch (const ScriptException &) {
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
                     _currentProcedure, _currentEnv->snapshot(),
                     _currentCallDepth, _callStack};
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
    } else if (auto *thr = dynamic_cast<ThrowStmt *>(stmt.get())) {
      executeThrow(thr);
    } else if (auto *tc = dynamic_cast<TryCatchStmt *>(stmt.get())) {
      executeTryCatch(tc);
    } else {
      throw runtimeError("Unknown statement type", stmt->line, stmt->column);
    }
  } catch (const ReturnException &) {
    throw;
  } catch (const BreakException &) {
    throw;
  } catch (const ContinueException &) {
    throw;
  } catch (const ScriptException &) {
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

  // Inside a struct method, bare field names resolve through `this`, and
  // bare method names evaluate to bound method values.
  if (name != "this" && _currentEnv->has("this")) {
    const Value &th = _currentEnv->get("this");
    if (ValueHelper::isStruct(th)) {
      auto sv = std::get<StructPtr>(th);
      auto it = sv->fields.find(name);
      if (it != sv->fields.end()) {
        return it->second;
      }
      auto declIt = _structs.find(sv->typeName);
      if (declIt != _structs.end()) {
        std::vector<ProcedureDeclPtr> methods;
        for (const auto &m : declIt->second->methods) {
          if (m->name == name) {
            methods.push_back(m);
          }
        }
        if (!methods.empty()) {
          auto fv = std::make_shared<FunctionValue>();
          fv->displayName = sv->typeName + "." + name;
          fv->procs = std::move(methods);
          fv->captured["this"] = th;
          return fv;
        }
      }
    }
  }

  auto extIt = _externalVariables.find(name);
  if (extIt != _externalVariables.end()) {
    if (!extIt->second.getter) {
      throw runtimeError("External variable '" + name + "' has no getter", line,
                         column);
    }
    return extIt->second.getter();
  }

  // A bare procedure name evaluates to a function value, enabling
  // `auto f = add; f(1, 2)` and passing procedures as arguments.
  auto pit = _procedures.find(name);
  if (pit != _procedures.end() && !pit->second.empty()) {
    auto fv = std::make_shared<FunctionValue>();
    fv->displayName = name;
    fv->procs = pit->second;
    return fv;
  }

  throw runtimeError("Undefined variable: " + name, line, column);
}

void Interpreter::writeVariable(const std::string &name, const Value &value,
                                int line, int column) {
  if (_currentEnv->has(name)) {
    _currentEnv->assign(name, value);
    return;
  }

  // Inside a struct method, writes to bare field names update `this`.
  if (name != "this" && _currentEnv->has("this")) {
    const Value &th = _currentEnv->get("this");
    if (ValueHelper::isStruct(th)) {
      auto sv = std::get<StructPtr>(th);
      auto it = sv->fields.find(name);
      if (it != sv->fields.end()) {
        StructDeclPtr decl = getStruct(sv->typeName);
        if (decl) {
          size_t fi = 0;
          for (size_t i = 0; i < decl->fields.size(); ++i) {
            if (decl->fields[i].name == name) {
              fi = i;
              break;
            }
          }
          try {
            it->second = convertToType(value, decl->fields[fi].type);
          } catch (const std::exception &e) {
            throw runtimeError(e.what(), line, column);
          }
        } else {
          it->second = value;
        }
        return;
      }
    }
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

// Normalize a possibly-negative index (-1 = last element). Throws on
// out-of-bounds; `what` customizes the message ("Array"/"String").
int64_t Interpreter::normalizeIndex(int64_t raw, size_t size, int line,
                                    int column) {
  int64_t idx = raw < 0 ? raw + static_cast<int64_t>(size) : raw;
  if (idx < 0 || idx >= static_cast<int64_t>(size)) {
    throw runtimeError("Index out of bounds", line, column);
  }
  return idx;
}

Value Interpreter::evaluateSlice(const Value &container, IndexExpr *expr) {
  int64_t size;
  bool isStr = std::holds_alternative<std::string>(container);
  if (isStr) {
    size = static_cast<int64_t>(std::get<std::string>(container).size());
  } else if (ValueHelper::isArray(container)) {
    size = static_cast<int64_t>(ValueHelper::arrayElements(container).size());
  } else {
    throw runtimeError("Slicing requires an array or string", expr->line,
                       expr->column);
  }

  int64_t begin = 0;
  int64_t end = size;
  if (expr->indexExpr) {
    begin = ValueHelper::toInt64(evaluate(expr->indexExpr));
  }
  if (expr->endIndex) {
    end = ValueHelper::toInt64(evaluate(expr->endIndex));
  }
  if (begin < 0) {
    begin += size;
  }
  if (end < 0) {
    end += size;
  }
  begin = std::max<int64_t>(0, std::min<int64_t>(begin, size));
  end = std::max<int64_t>(0, std::min<int64_t>(end, size));
  if (end < begin) {
    end = begin;
  }

  if (isStr) {
    std::string out = std::get<std::string>(container).substr(
        static_cast<size_t>(begin), static_cast<size_t>(end - begin));
    checkStringLength(out.size());
    return out;
  }

  const auto &elems = ValueHelper::arrayElements(container);
  std::vector<Value> out(elems.begin() + begin, elems.begin() + end);
  noteArrayAllocation(out.size());
  return ValueHelper::createArray(ValueHelper::arrayElementType(container),
                                  std::move(out));
}

Value Interpreter::evaluateIndex(IndexExpr *expr) {
  Value arrayVal = evaluate(expr->arrayExpr);

  if (expr->isSlice) {
    return evaluateSlice(arrayVal, expr);
  }

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

  // Strings index to their characters; negative indices count from the end.
  if (std::holds_alternative<std::string>(arrayVal)) {
    const std::string &s = std::get<std::string>(arrayVal);
    Value indexVal = evaluate(expr->indexExpr);
    int64_t idx;
    try {
      idx = normalizeIndex(ValueHelper::toInt64(indexVal), s.size(),
                           expr->line, expr->column);
    } catch (RuntimeError &) {
      throw;
    } catch (const std::exception &e) {
      throw runtimeError(e.what(), expr->line, expr->column);
    }
    return static_cast<char>(s[static_cast<size_t>(idx)]);
  }

  if (!ValueHelper::isArray(arrayVal)) {
    throw runtimeError("Indexing non-array value", expr->line, expr->column);
  }

  Value indexVal = evaluate(expr->indexExpr);
  const auto &elems = ValueHelper::arrayElements(arrayVal);
  int64_t idx;
  try {
    idx = normalizeIndex(ValueHelper::toInt64(indexVal), elems.size(),
                         expr->line, expr->column);
  } catch (RuntimeError &) {
    throw;
  } catch (const std::exception &e) {
    throw runtimeError(e.what(), expr->line, expr->column);
  }

  return elems[static_cast<size_t>(idx)];
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
  if (it != sv->fields.end()) {
    return it->second;
  }

  // Struct method: `obj.m` evaluates to a bound function value carrying
  // `this`, so `obj.m(x)` and `auto f = obj.m; f(x)` both work.
  auto declIt = _structs.find(sv->typeName);
  if (declIt != _structs.end()) {
    std::vector<ProcedureDeclPtr> methods;
    for (const auto &m : declIt->second->methods) {
      if (m->name == expr->member) {
        methods.push_back(m);
      }
    }
    if (!methods.empty()) {
      auto fv = std::make_shared<FunctionValue>();
      fv->displayName = sv->typeName + "." + expr->member;
      fv->procs = std::move(methods);
      fv->captured["this"] = object;
      return fv;
    }
  }

  throw runtimeError("Struct '" + sv->typeName + "' has no field or method '" +
                         expr->member + "'",
                     expr->line, expr->column);
}

Value Interpreter::evaluateLambda(LambdaExpr *expr) {
  auto fv = std::make_shared<FunctionValue>();
  fv->displayName = "<lambda>";
  fv->parameters = expr->parameters;
  fv->body = expr->body;
  fv->hasRetType = expr->hasRetType;
  fv->declaredRetType = expr->declaredRetType;
  fv->captured = _currentEnv->snapshot();
  return fv;
}

Value Interpreter::evaluateEnumMember(EnumMemberExpr *expr) {
  // A same-named variable holding a struct shadows the enum type name,
  // keeping `Color.RED` usable even when a local named Color exists.
  if (_currentEnv->has(expr->enumName)) {
    Value v = _currentEnv->get(expr->enumName);
    if (ValueHelper::isStruct(v)) {
      StructPtr sv = std::get<StructPtr>(v);
      auto it = sv->fields.find(expr->memberName);
      if (it != sv->fields.end()) {
        return it->second;
      }
    }
  }

  auto eit = _enums.find(expr->enumName);
  if (eit == _enums.end()) {
    throw runtimeError("Unknown enum '" + expr->enumName + "'", expr->line,
                       expr->column);
  }
  for (const auto &m : eit->second->members) {
    if (m.first == expr->memberName) {
      return static_cast<int64_t>(m.second);
    }
  }
  throw runtimeError("Enum '" + expr->enumName + "' has no member '" +
                         expr->memberName + "'",
                     expr->line, expr->column);
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
  if (type.isFunction) {
    return FuncPtr(nullptr);
  }
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
    auto &elems = ValueHelper::arrayElements(container);
    int64_t i;
    try {
      i = normalizeIndex(ValueHelper::toInt64(indexVal), elems.size(),
                         expr->line, expr->column);
    } catch (RuntimeError &) {
      throw;
    } catch (const std::exception &e) {
      throw runtimeError(e.what(), expr->line, expr->column);
    }
    Value old = elems[static_cast<size_t>(i)];
    Value next = bump(old);
    TypeInfo elemType = ValueHelper::arrayElementType(container);
    try {
      elems[static_cast<size_t>(i)] = convertToType(next, elemType);
    } catch (const std::exception &e) {
      throw runtimeError(e.what(), expr->line, expr->column);
    }
    return expr->prefix ? next : old;
  }

  throw runtimeError("++/-- require a variable or index target", expr->line,
                     expr->column);
}

Value Interpreter::evaluateCall(CallExpr *expr) {
  // Arbitrary-expression callee: fn values, bound methods, chained calls
  // like `makeAdder(2)(3)` or `handlers[i](x)`.
  if (expr->calleeExpr) {
    Value callee = evaluate(expr->calleeExpr);
    std::vector<Value> args;
    args.reserve(expr->arguments.size());
    for (auto &argExpr : expr->arguments) {
      args.push_back(evaluate(argExpr));
    }
    auto *fn = std::get_if<FuncPtr>(&callee);
    if (!fn) {
      throw runtimeError("Call target is not a function value", expr->line,
                         expr->column);
    }
    return callFunctionValue(*fn, args, expr->line, expr->column);
  }

  // A variable holding a function value shadows procedures, externals, and
  // builtins: `auto f = add; f(1, 2)`. Checked before the inline cache so a
  // later-defined variable correctly shadows a cached procedure.
  if (_currentEnv->has(expr->functionName)) {
    Value v = _currentEnv->get(expr->functionName);
    auto *fn = std::get_if<FuncPtr>(&v);
    if (!fn) {
      throw runtimeError("'" + expr->functionName +
                             "' is not a function value",
                         expr->line, expr->column);
    }
    std::vector<Value> args;
    args.reserve(expr->arguments.size());
    for (auto &argExpr : expr->arguments) {
      args.push_back(evaluate(argExpr));
    }
    _callSiteLine = expr->line;
    _callSiteColumn = expr->column;
    return callFunctionValue(*fn, args, expr->line, expr->column);
  }

  // Inside a struct method, a bare `method(...)` calls the sibling method on
  // `this` (member functions hide outer procedures, mirroring C++).
  if (_currentEnv->has("this")) {
    const Value &th = _currentEnv->get("this");
    if (ValueHelper::isStruct(th)) {
      auto sv = std::get<StructPtr>(th);
      auto declIt = _structs.find(sv->typeName);
      if (declIt != _structs.end()) {
        std::vector<ProcedureDeclPtr> methods;
        for (const auto &m : declIt->second->methods) {
          if (m->name == expr->functionName) {
            methods.push_back(m);
          }
        }
        if (!methods.empty()) {
          std::vector<Value> args;
          args.reserve(expr->arguments.size());
          for (auto &argExpr : expr->arguments) {
            args.push_back(evaluate(argExpr));
          }
          auto fv = std::make_shared<FunctionValue>();
          fv->displayName = sv->typeName + "." + expr->functionName;
          fv->procs = std::move(methods);
          fv->captured["this"] = th;
          _callSiteLine = expr->line;
          _callSiteColumn = expr->column;
          return callFunctionValue(fv, args, expr->line, expr->column);
        }
      }
    }
  }

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

  // Check if it's a procedure call (possibly overloaded)
  if (auto it = _procedures.find(expr->functionName);
      it != _procedures.end() && !it->second.empty()) {
    std::vector<Value> args;
    args.reserve(expr->arguments.size());
    for (auto &argExpr : expr->arguments) {
      args.push_back(evaluate(argExpr));
    }
    ProcedureDeclPtr proc = it->second.size() == 1
                                ? it->second.front()
                                : resolveOverload(expr->functionName,
                                                  it->second, args, expr->line,
                                                  expr->column);
    if (it->second.size() == 1) {
      expr->cacheVersion = _callCacheVersion;
      expr->cachedIsProcedure = true;
      expr->cachedIsExternal = false;
      expr->cachedProcedure = proc;
    }
    _callSiteLine = expr->line;
    _callSiteColumn = expr->column;
    return executeProcedure(proc, args);
  }

  // An external variable may also yield a function value.
  if (auto vit = _externalVariables.find(expr->functionName);
      vit != _externalVariables.end() && vit->second.getter) {
    Value v = vit->second.getter();
    if (std::holds_alternative<FuncPtr>(v)) {
      std::vector<Value> args;
      args.reserve(expr->arguments.size());
      for (auto &argExpr : expr->arguments) {
        args.push_back(evaluate(argExpr));
      }
      return callFunctionValue(std::get<FuncPtr>(v), args, expr->line,
                               expr->column);
    }
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
  // external functions may override them. Sandboxed environments can
  // disable individual builtins entirely.
  if (Builtins::isBuiltin(expr->functionName) &&
      isBuiltinEnabled(expr->functionName)) {
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

  if (stmt->type.isAuto) {
    // `auto` — the validator normally resolves the type ahead of time, but
    // a dynamically-typed initializer (unknown at compile time) keeps the
    // value as-is.
    if (!stmt->initializer) {
      throw runtimeError("'auto' variable requires an initializer",
                         stmt->line, stmt->column);
    }
    _currentEnv->define(stmt->name, evaluate(stmt->initializer));
    return;
  }

  if (stmt->initializer) {
    value = evaluate(stmt->initializer);
    try {
      value = convertToType(value, stmt->type);
    } catch (const std::exception &e) {
      throw runtimeError(e.what(), stmt->line, stmt->column);
    }
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

  // Inside a struct method, `field = v` assigns through `this`.
  if (stmt->variableName != "this" && _currentEnv->has("this")) {
    const Value &th = _currentEnv->get("this");
    if (ValueHelper::isStruct(th)) {
      auto sv = std::get<StructPtr>(th);
      auto it = sv->fields.find(stmt->variableName);
      if (it != sv->fields.end()) {
        Value result =
            applyAssignOp(stmt->op, it->second, value, stmt->line, stmt->column);
        StructDeclPtr decl = getStruct(sv->typeName);
        if (decl) {
          for (const auto &f : decl->fields) {
            if (f.name == stmt->variableName) {
              try {
                result = convertToType(result, f.type);
              } catch (const std::exception &e) {
                throw runtimeError(e.what(), stmt->line, stmt->column);
              }
              break;
            }
          }
        }
        it->second = result;
        return;
      }
    }
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
  std::vector<Value> &elems = ValueHelper::arrayElements(arrayVal);
  int64_t idx;
  try {
    idx = normalizeIndex(ValueHelper::toInt64(indexVal), elems.size(),
                         stmt->line, stmt->column);
  } catch (RuntimeError &) {
    throw;
  } catch (const std::exception &e) {
    throw runtimeError(e.what(), stmt->line, stmt->column);
  }

  TypeInfo elementType = ValueHelper::arrayElementType(arrayVal);
  Value rawValue = evaluate(stmt->value);
  if (stmt->op != AssignStmt::Operator::ASSIGN) {
    rawValue = applyAssignOp(stmt->op, elems[static_cast<size_t>(idx)], rawValue,
                             stmt->line, stmt->column);
  }
  Value converted;
  try {
    converted = convertToType(rawValue, elementType);
  } catch (const std::exception &e) {
    throw runtimeError(e.what(), stmt->line, stmt->column);
  }

  elems[static_cast<size_t>(idx)] = converted;
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

void Interpreter::executeThrow(ThrowStmt *stmt) {
  Value v = evaluate(stmt->value);
  throw ScriptException(v, _currentFile, stmt->line, stmt->column,
                        _currentProcedure);
}

void Interpreter::executeTryCatch(TryCatchStmt *stmt) {
  bool caught = false;
  Value thrown;

  try {
    execute(stmt->tryBlock);
  } catch (const ScriptException &se) {
    caught = true;
    thrown = se.value;
  } catch (const RuntimeError &re) {
    if (re.fatal) {
      // Resource-limit violations are not script-catchable.
      if (stmt->finallyBlock) {
        execute(stmt->finallyBlock);
      }
      throw;
    }
    caught = true;
    thrown = std::string(re.what());
  } catch (...) {
    // return/break/continue propagate, but finally still runs.
    if (stmt->finallyBlock) {
      execute(stmt->finallyBlock);
    }
    throw;
  }

  try {
    if (caught) {
      if (stmt->catchBlock) {
        _currentEnv->enterScope();
        try {
          if (!stmt->catchVar.empty()) {
            Value bound = thrown;
            if (!stmt->catchType.isAuto) {
              try {
                bound = convertToType(thrown, stmt->catchType);
              } catch (const std::exception &e) {
                throw runtimeError(
                    std::string("catch variable: ") + e.what(), stmt->line,
                    stmt->column);
              }
            }
            _currentEnv->define(stmt->catchVar, bound);
          }
          execute(stmt->catchBlock);
          _currentEnv->exitScope();
        } catch (...) {
          _currentEnv->exitScope();
          throw;
        }
      } else {
        // No catch clause: rethrow after finally runs.
        throw ScriptException(thrown, _currentFile, stmt->line, stmt->column,
                              _currentProcedure);
      }
    }
  } catch (...) {
    if (stmt->finallyBlock) {
      execute(stmt->finallyBlock);
    }
    throw;
  }

  if (stmt->finallyBlock) {
    execute(stmt->finallyBlock);
  }
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
    throw RuntimeError("Maximum execution steps exceeded", _currentFile, line,
                       column, _currentProcedure, /*fatal=*/true);
  }
}

Value Interpreter::convertToType(const Value &val, const TypeInfo &targetType) {
  TypeInfo sourceType = ValueHelper::getType(val);

  // `auto` accepts the value unchanged (dynamic binding).
  if (targetType.isAuto) {
    return val;
  }

  // Function-typed targets accept only function values, and when the
  // target's signature is fully specified the value's signature must be
  // call-compatible (same arity; each target param convertible to the
  // value's param — loose contravariance check). Overload sets pass when
  // any candidate is compatible.
  if (targetType.isFunction) {
    auto *fv = std::get_if<FuncPtr>(&val);
    if (!fv || !*fv) {
      throw std::runtime_error("Expected function value, got '" +
                               ValueHelper::typeToString(sourceType) + "'");
    }
    if (!targetType.paramTypes.empty()) {
      auto sigCompatible = [&](const TypeInfo &sig) {
        if (sig.paramTypes.size() != targetType.paramTypes.size()) {
          return false;
        }
        for (size_t i = 0; i < targetType.paramTypes.size(); ++i) {
          if (!sig.paramTypes[i].isAuto &&
              !runtimeConvertible(targetType.paramTypes[i],
                                  sig.paramTypes[i])) {
            return false;
          }
        }
        return true;
      };
      const FunctionValue &fvRef = **fv;
      bool ok = false;
      if (!fvRef.procs.empty()) {
        for (const auto &p : fvRef.procs) {
          std::vector<TypeInfo> params;
          for (const auto &pr : p->parameters) {
            params.push_back(pr.type);
          }
          if (sigCompatible(TypeInfo::functionOf(std::move(params),
                                                 nullptr))) {
            ok = true;
            break;
          }
        }
      } else {
        ok = sigCompatible(fvRef.signature());
      }
      if (!ok) {
        throw std::runtime_error(
            "Function signature mismatch: value is not call-compatible "
            "with '" +
            ValueHelper::typeToString(targetType) + "'");
      }
    }
    return val;
  }
  if (sourceType.isFunction) {
    throw std::runtime_error("Cannot convert function to '" +
                             ValueHelper::typeToString(targetType) + "'");
  }

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
