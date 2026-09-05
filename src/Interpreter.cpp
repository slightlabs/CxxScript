#include "Interpreter.h"
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

// Interpreter Implementation
Interpreter::Interpreter()
    : _globalEnv(nullptr), _currentEnv(&_globalEnv), _currentProcedure(""),
      _currentFile(""), _callCacheVersion(1) {}

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
  ++_callCacheVersion;
}

void Interpreter::setExecutionLimits(size_t maxCallDepth, size_t maxSteps) {
  _maxCallDepth = maxCallDepth;
  _maxSteps = maxSteps;
}

void Interpreter::clearExecutionLimits() { setExecutionLimits(0, 0); }

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

Value Interpreter::executeProcedure(ProcedureDeclPtr proc,
                                    const std::vector<Value> &arguments) {
  std::string previousProcedure = _currentProcedure;
  std::string previousFile = _currentFile;
  _currentProcedure = proc->name;
  _currentFile = "";
  auto fileIt = _procedureFiles.find(proc.get());
  if (fileIt != _procedureFiles.end()) {
    _currentFile = fileIt->second;
  }

  if (_maxCallDepth > 0 && _currentCallDepth >= _maxCallDepth) {
    _currentProcedure = previousProcedure;
    _currentFile = previousFile;
    throw runtimeError("Maximum call depth exceeded", proc->line, proc->column);
  }
  // Check argument count
  if (arguments.size() != proc->parameters.size()) {
    std::stringstream ss;
    ss << "Procedure '" << proc->name << "' expects " << proc->parameters.size()
       << " arguments, got " << arguments.size();
    _currentProcedure = previousProcedure;
    _currentFile = previousFile;
    throw runtimeError(ss.str(), proc->line, proc->column);
  }
  ++_currentCallDepth;

  // Create new environment for procedure
  Environment procEnv(_currentEnv);
  Environment *previousEnv = _currentEnv;
  _currentEnv = &procEnv;

  // Bind parameters
  for (size_t i = 0; i < proc->parameters.size(); ++i) {
    Value convertedArg = convertToType(arguments[i], proc->parameters[i].type);
    _currentEnv->define(proc->parameters[i].name, convertedArg);
  }

  try {
    execute(proc->body);

    // If we reach here, no return statement was executed
    if (proc->returnType.baseType == DataType::VOID && !proc->returnType.isArray) {
      _currentEnv = previousEnv;
      _currentProcedure = previousProcedure;
      _currentFile = previousFile;
      --_currentCallDepth;
      return static_cast<int32_t>(0); // Dummy value
    }

    // Non-void procedure without return
    _currentEnv = previousEnv;
    _currentProcedure = previousProcedure;
    _currentFile = previousFile;
    throw runtimeError("Non-void procedure must return a value", proc->line,
                       proc->column);

  } catch (const ReturnException &ret) {
    _currentEnv = previousEnv;
    _currentProcedure = previousProcedure;
    _currentFile = previousFile;

    if (proc->returnType.baseType == DataType::VOID && !proc->returnType.isArray) {
      --_currentCallDepth;
      return static_cast<int32_t>(0); // Dummy value
    }

    --_currentCallDepth;
    return convertToType(ret.value, proc->returnType);
  } catch (...) {
    _currentEnv = previousEnv;
    _currentProcedure = previousProcedure;
    _currentFile = previousFile;
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

Value Interpreter::evaluate(ExprPtr expr) {
  if (auto *lit = dynamic_cast<LiteralExpr *>(expr.get())) {
    return evaluateLiteral(lit);
  }
  if (auto *var = dynamic_cast<VariableExpr *>(expr.get())) {
    return evaluateVariable(var);
  }
  if (auto *arr = dynamic_cast<ArrayLiteralExpr *>(expr.get())) {
    return evaluateArrayLiteral(arr);
  }
  if (auto *idx = dynamic_cast<IndexExpr *>(expr.get())) {
    return evaluateIndex(idx);
  }
  if (auto *bin = dynamic_cast<BinaryExpr *>(expr.get())) {
    return evaluateBinary(bin);
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

  throw runtimeError("Unknown expression type", expr->line, expr->column);
}

void Interpreter::execute(StmtPtr stmt) {
  consumeExecutionStep(stmt->line, stmt->column);

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
  } else {
    throw runtimeError("Unknown statement type", stmt->line, stmt->column);
  }
}

Value Interpreter::evaluateLiteral(LiteralExpr *expr) { return expr->value; }

Value Interpreter::evaluateVariable(VariableExpr *expr) {
  try {
    return _currentEnv->get(expr->name);
  } catch (const std::runtime_error &) {
    auto extIt = _externalVariables.find(expr->name);
    if (extIt != _externalVariables.end()) {
      if (!extIt->second.getter) {
        throw runtimeError(
            "External variable '" + expr->name + "' has no getter",
            expr->line, expr->column);
      }
      return extIt->second.getter();
    }

    throw runtimeError("Undefined variable: " + expr->name, expr->line,
                       expr->column);
  }
}

Value Interpreter::evaluateArrayLiteral(ArrayLiteralExpr *expr) {
  std::vector<Value> elements;
  elements.reserve(expr->elements.size());
  for (auto &e : expr->elements) {
    elements.push_back(evaluate(e));
  }

  TypeInfo elemType = elements.empty() ? TypeInfo(DataType::VOID)
                                       : ValueHelper::getType(elements[0]);
  return ValueHelper::createArray(elemType, elements);
}

Value Interpreter::evaluateIndex(IndexExpr *expr) {
  Value arrayVal = evaluate(expr->arrayExpr);
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

Value Interpreter::evaluateCall(CallExpr *expr) {
  // Built-in functions for arrays
  if (expr->functionName == "len") {
    if (expr->arguments.size() != 1) {
      throw runtimeError("len expects 1 argument", expr->line, expr->column);
    }
    Value arrayVal = evaluate(expr->arguments[0]);
    if (!ValueHelper::isArray(arrayVal)) {
      throw runtimeError("len expects an array", expr->line, expr->column);
    }
    auto size = static_cast<int64_t>(ValueHelper::arrayElements(arrayVal).size());
    return ValueHelper::createValue(DataType::INT32, size);
  }

  if (expr->functionName == "push") {
    if (expr->arguments.size() != 2) {
      throw runtimeError("push expects 2 arguments", expr->line, expr->column);
    }
    Value arrayVal = evaluate(expr->arguments[0]);
    if (!ValueHelper::isArray(arrayVal)) {
      throw runtimeError("push expects an array as first argument", expr->line, expr->column);
    }
    TypeInfo elementType = ValueHelper::arrayElementType(arrayVal);
    Value raw = evaluate(expr->arguments[1]);
    Value converted = convertToType(raw, TypeInfo(elementType.baseType));
    ValueHelper::arrayElements(arrayVal).push_back(converted);
    auto size = static_cast<int64_t>(ValueHelper::arrayElements(arrayVal).size());
    return ValueHelper::createValue(DataType::INT32, size);
  }

  if (expr->functionName == "pop") {
    if (expr->arguments.size() != 1) {
      throw runtimeError("pop expects 1 argument", expr->line, expr->column);
    }
    Value arrayVal = evaluate(expr->arguments[0]);
    if (!ValueHelper::isArray(arrayVal)) {
      throw runtimeError("pop expects an array", expr->line, expr->column);
    }
    auto &elems = ValueHelper::arrayElements(arrayVal);
    if (elems.empty()) {
      throw runtimeError("Cannot pop from empty array", expr->line, expr->column);
    }
    Value result = elems.back();
    elems.pop_back();
    return result;
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
        return executeProcedure(proc, args);
      }
    }
    if (expr->cachedIsExternal && expr->cachedExternal) {
      std::vector<Value> args;
      args.reserve(expr->arguments.size());
      for (auto &argExpr : expr->arguments) {
        args.push_back(evaluate(argExpr));
      }
      return expr->cachedExternal(args);
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
    return extIt->second(args);
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
    // Default initialization
    if (stmt->type.isArray) {
      value = ValueHelper::createArray(TypeInfo(stmt->type.baseType), {});
    } else {
      switch (stmt->type.baseType) {
      case DataType::INT8:
        value = static_cast<int8_t>(0);
        break;
      case DataType::UINT8:
        value = static_cast<uint8_t>(0);
        break;
      case DataType::INT16:
        value = static_cast<int16_t>(0);
        break;
      case DataType::UINT16:
        value = static_cast<uint16_t>(0);
        break;
      case DataType::INT32:
        value = static_cast<int32_t>(0);
        break;
      case DataType::UINT32:
        value = static_cast<uint32_t>(0);
        break;
      case DataType::INT64:
        value = static_cast<int64_t>(0);
        break;
      case DataType::FLOAT:
        value = static_cast<float>(0.0f);
        break;
      case DataType::DOUBLE:
        value = static_cast<double>(0.0);
        break;
      case DataType::UINT64:
        value = static_cast<uint64_t>(0);
        break;
      case DataType::STRING:
        value = std::string("");
        break;
      case DataType::BOOL:
        value = false;
        break;
      case DataType::CHAR:
        value = static_cast<char>(0);
        break;
      case DataType::VOID:
        value = static_cast<int32_t>(0);
        break;
      }
    }
  }

  _currentEnv->define(stmt->name, value);
}

void Interpreter::executeAssign(AssignStmt *stmt) {
  Value value = evaluate(stmt->value);

  try {
    Value currentValue = _currentEnv->get(stmt->variableName);

    switch (stmt->op) {
    case AssignStmt::Operator::ASSIGN:
      _currentEnv->assign(stmt->variableName, value);
      break;
    case AssignStmt::Operator::PLUS_ASSIGN:
      _currentEnv->assign(stmt->variableName,
                          ValueHelper::add(currentValue, value));
      break;
    case AssignStmt::Operator::MINUS_ASSIGN:
      _currentEnv->assign(stmt->variableName,
                          ValueHelper::subtract(currentValue, value));
      break;
    case AssignStmt::Operator::MULT_ASSIGN:
      _currentEnv->assign(stmt->variableName,
                          ValueHelper::multiply(currentValue, value));
      break;
    case AssignStmt::Operator::DIV_ASSIGN:
      _currentEnv->assign(stmt->variableName,
                          ValueHelper::divide(currentValue, value));
      break;
    }
  } catch (const std::runtime_error &) {
    auto extIt = _externalVariables.find(stmt->variableName);
    if (extIt == _externalVariables.end()) {
      throw runtimeError("Undefined variable: " + stmt->variableName,
                         stmt->line, stmt->column);
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

    Value currentValue = extVar.getter();
    Value result;
    switch (stmt->op) {
    case AssignStmt::Operator::ASSIGN:
      result = value;
      break;
    case AssignStmt::Operator::PLUS_ASSIGN:
      result = ValueHelper::add(currentValue, value);
      break;
    case AssignStmt::Operator::MINUS_ASSIGN:
      result = ValueHelper::subtract(currentValue, value);
      break;
    case AssignStmt::Operator::MULT_ASSIGN:
      result = ValueHelper::multiply(currentValue, value);
      break;
    case AssignStmt::Operator::DIV_ASSIGN:
      result = ValueHelper::divide(currentValue, value);
      break;
    }

    extVar.setter(result);
  }
}

void Interpreter::executeIndexAssign(IndexAssignStmt *stmt) {
  Value arrayVal = evaluate(stmt->arrayExpr);
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
  Value converted = convertToType(rawValue, TypeInfo(elementType.baseType));

  elems[idx] = converted;
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

  if (targetType.isArray) {
    if (!ValueHelper::isArray(val)) {
      throw std::runtime_error("Expected array value");
    }
    TypeInfo elemType = ValueHelper::arrayElementType(val);
    if (elemType.baseType == targetType.baseType) {
      return val; // already correct element type
    }
    const auto &elems = ValueHelper::arrayElements(val);
    std::vector<Value> converted;
    converted.reserve(elems.size());
    for (const auto &e : elems) {
      converted.push_back(convertToType(e, TypeInfo(targetType.baseType)));
    }
    return ValueHelper::createArray(TypeInfo(targetType.baseType), converted);
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
