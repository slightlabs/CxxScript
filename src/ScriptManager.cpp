#include "ScriptManager.h"
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace Script {

namespace {

struct InferredType {
  bool known = false;
  TypeInfo type{};
};

bool isSignedInteger(DataType t) {
  return t == DataType::CHAR || t == DataType::INT8 || t == DataType::INT16 ||
         t == DataType::INT32 || t == DataType::INT64;
}

bool isUnsignedInteger(DataType t) {
  return t == DataType::UINT8 || t == DataType::UINT16 || t == DataType::UINT32 ||
         t == DataType::UINT64;
}

bool isInteger(DataType t) { return isSignedInteger(t) || isUnsignedInteger(t); }

bool isNumeric(DataType t) {
  return isInteger(t) || t == DataType::FLOAT || t == DataType::DOUBLE;
}

bool canConvertCompileTime(const TypeInfo &from, const TypeInfo &to) {
  if (from == to) {
    return true;
  }
  if (from.isArray != to.isArray) {
    return false;
  }
  if (from.isArray) {
    if (from.baseType == DataType::VOID) {
      return true;
    }
    return from.baseType == to.baseType;
  }
  if (to.baseType == DataType::VOID) {
    return true;
  }
  if (from.baseType == DataType::VOID) {
    return false;
  }
  if (to.baseType == DataType::STRING) {
    return true;
  }
  if (to.baseType == DataType::BOOL) {
    return true;
  }
  if (from.baseType == DataType::BOOL && isNumeric(to.baseType)) {
    return true;
  }
  if (isNumeric(from.baseType) && isNumeric(to.baseType)) {
    return true;
  }
  return false;
}

std::string typeName(const TypeInfo &type) { return ValueHelper::typeToString(type); }

class SemanticValidator {
public:
  SemanticValidator(const std::string &filename, Interpreter *interpreter,
                    std::vector<CompilationError> &errors)
      : _filename(filename), _interpreter(interpreter), _errors(errors) {}

  void validate(const ScriptPtr &script) {
    _procedures.clear();
    for (const auto &proc : script->procedures) {
      _procedures[proc->name] = proc;
    }
    for (const auto &proc : script->procedures) {
      validateProcedure(proc);
    }
  }

private:
  const std::string &_filename;
  Interpreter *_interpreter;
  std::vector<CompilationError> &_errors;
  std::unordered_map<std::string, ProcedureDeclPtr> _procedures;
  std::vector<std::unordered_map<std::string, TypeInfo>> _scopes;
  TypeInfo _currentReturnType = TypeInfo(DataType::VOID);
  std::string _currentProcedure;
  int _loopDepth = 0;
  int _switchDepth = 0;

  void emit(const std::string &message, int line, int column) {
    _errors.emplace_back(message, _filename, _currentProcedure, line, column);
  }

  void enterScope() { _scopes.emplace_back(); }
  void exitScope() { _scopes.pop_back(); }

  void defineVar(const std::string &name, const TypeInfo &type) {
    if (_scopes.empty()) {
      enterScope();
    }
    _scopes.back()[name] = type;
  }

  std::optional<TypeInfo> resolveVar(const std::string &name) const {
    for (size_t i = _scopes.size(); i-- > 0;) {
      auto it = _scopes[i].find(name);
      if (it != _scopes[i].end()) {
        return it->second;
      }
    }
    return std::nullopt;
  }

  void validateProcedure(const ProcedureDeclPtr &proc) {
    _scopes.clear();
    _currentProcedure = proc->name;
    _currentReturnType = proc->returnType;
    _loopDepth = 0;
    _switchDepth = 0;

    enterScope();
    for (const auto &param : proc->parameters) {
      defineVar(param.name, param.type);
    }
    validateStmt(proc->body);
    exitScope();
  }

  void expectConvertible(const InferredType &from, const TypeInfo &to,
                         int line, int column, const std::string &context) {
    if (!from.known) {
      return;
    }
    if (!canConvertCompileTime(from.type, to)) {
      emit("Type mismatch in " + context + ": cannot convert '" +
               typeName(from.type) + "' to '" + typeName(to) + "'",
           line, column);
    }
  }

  void validateStmt(const StmtPtr &stmt) {
    if (!stmt) {
      return;
    }

    if (auto *exprStmt = dynamic_cast<ExpressionStmt *>(stmt.get())) {
      (void)inferExpr(exprStmt->expression);
      return;
    }
    if (auto *varDecl = dynamic_cast<VarDeclStmt *>(stmt.get())) {
      if (varDecl->initializer) {
        InferredType initType = inferExpr(varDecl->initializer);
        expectConvertible(initType, varDecl->type, varDecl->line, varDecl->column,
                          "variable declaration");
      }
      defineVar(varDecl->name, varDecl->type);
      return;
    }
    if (auto *assign = dynamic_cast<AssignStmt *>(stmt.get())) {
      InferredType valueType = inferExpr(assign->value);
      auto targetType = resolveVar(assign->variableName);
      if (targetType.has_value()) {
        expectConvertible(valueType, *targetType, assign->line, assign->column,
                          "assignment");
      }
      return;
    }
    if (auto *idxAssign = dynamic_cast<IndexAssignStmt *>(stmt.get())) {
      InferredType arrayType = inferExpr(idxAssign->arrayExpr);
      (void)inferExpr(idxAssign->indexExpr);
      InferredType valueType = inferExpr(idxAssign->value);
      if (arrayType.known && arrayType.type.isArray) {
        expectConvertible(valueType, TypeInfo(arrayType.type.baseType),
                          idxAssign->line, idxAssign->column, "index assignment");
      }
      return;
    }
    if (auto *block = dynamic_cast<BlockStmt *>(stmt.get())) {
      enterScope();
      for (const auto &s : block->statements) {
        validateStmt(s);
      }
      exitScope();
      return;
    }
    if (auto *ifStmt = dynamic_cast<IfStmt *>(stmt.get())) {
      InferredType condType = inferExpr(ifStmt->condition);
      if (condType.known && condType.type.baseType == DataType::VOID &&
          !condType.type.isArray) {
        emit("If condition cannot be void", ifStmt->line, ifStmt->column);
      }
      validateStmt(ifStmt->thenBranch);
      validateStmt(ifStmt->elseBranch);
      return;
    }
    if (auto *whileStmt = dynamic_cast<WhileStmt *>(stmt.get())) {
      InferredType condType = inferExpr(whileStmt->condition);
      if (condType.known && condType.type.baseType == DataType::VOID &&
          !condType.type.isArray) {
        emit("While condition cannot be void", whileStmt->line, whileStmt->column);
      }
      ++_loopDepth;
      validateStmt(whileStmt->body);
      --_loopDepth;
      return;
    }
    if (auto *forStmt = dynamic_cast<ForStmt *>(stmt.get())) {
      enterScope();
      validateStmt(forStmt->initializer);
      if (forStmt->condition) {
        InferredType condType = inferExpr(forStmt->condition);
        if (condType.known && condType.type.baseType == DataType::VOID &&
            !condType.type.isArray) {
          emit("For condition cannot be void", forStmt->line, forStmt->column);
        }
      }
      ++_loopDepth;
      validateStmt(forStmt->body);
      if (forStmt->increment) {
        validateStmt(forStmt->increment);
      }
      --_loopDepth;
      exitScope();
      return;
    }
    if (auto *doWhile = dynamic_cast<DoWhileStmt *>(stmt.get())) {
      ++_loopDepth;
      validateStmt(doWhile->body);
      --_loopDepth;
      InferredType condType = inferExpr(doWhile->condition);
      if (condType.known && condType.type.baseType == DataType::VOID &&
          !condType.type.isArray) {
        emit("Do-while condition cannot be void", doWhile->line, doWhile->column);
      }
      return;
    }
    if (auto *switchStmt = dynamic_cast<SwitchStmt *>(stmt.get())) {
      InferredType controlType = inferExpr(switchStmt->expression);
      ++_switchDepth;
      for (const auto &entry : switchStmt->cases) {
        if (!entry.isDefault && entry.matchExpr) {
          InferredType caseType = inferExpr(entry.matchExpr);
          if (controlType.known && caseType.known &&
              !canCompare(controlType.type, caseType.type)) {
            emit("Switch case type '" + typeName(caseType.type) +
                     "' is not comparable with switch expression type '" +
                     typeName(controlType.type) + "'",
                 entry.matchExpr->line, entry.matchExpr->column);
          }
        }
        for (const auto &s : entry.statements) {
          validateStmt(s);
        }
      }
      --_switchDepth;
      return;
    }
    if (auto *ret = dynamic_cast<ReturnStmt *>(stmt.get())) {
      if (_currentReturnType.baseType == DataType::VOID &&
          !_currentReturnType.isArray) {
        if (ret->value) {
          emit("Void procedure cannot return a value", ret->line, ret->column);
        }
        return;
      }
      if (!ret->value) {
        emit("Non-void procedure must return a value", ret->line, ret->column);
        return;
      }
      InferredType returnType = inferExpr(ret->value);
      expectConvertible(returnType, _currentReturnType, ret->line, ret->column,
                        "return statement");
      return;
    }
    if (auto *brk = dynamic_cast<BreakStmt *>(stmt.get())) {
      if (_loopDepth == 0 && _switchDepth == 0) {
        emit("'break' used outside loop/switch", brk->line, brk->column);
      }
      return;
    }
    if (auto *cont = dynamic_cast<ContinueStmt *>(stmt.get())) {
      if (_loopDepth == 0) {
        emit("'continue' used outside loop", cont->line, cont->column);
      }
      return;
    }
  }

  bool canCompare(const TypeInfo &a, const TypeInfo &b) const {
    if (a == b) {
      return true;
    }
    if (a.isArray || b.isArray) {
      return false;
    }
    if (isNumeric(a.baseType) && isNumeric(b.baseType)) {
      return true;
    }
    return false;
  }

  InferredType inferExpr(const ExprPtr &expr) {
    if (!expr) {
      return {};
    }

    if (auto *lit = dynamic_cast<LiteralExpr *>(expr.get())) {
      return {true, lit->type};
    }
    if (auto *var = dynamic_cast<VariableExpr *>(expr.get())) {
      auto type = resolveVar(var->name);
      if (type.has_value()) {
        return {true, *type};
      }
      return {};
    }
    if (auto *arr = dynamic_cast<ArrayLiteralExpr *>(expr.get())) {
      if (arr->elements.empty()) {
        return {true, TypeInfo(DataType::VOID, true)};
      }
      InferredType first = inferExpr(arr->elements.front());
      if (!first.known || first.type.isArray) {
        return {};
      }
      for (size_t i = 1; i < arr->elements.size(); ++i) {
        InferredType elem = inferExpr(arr->elements[i]);
        if (elem.known &&
            !canConvertCompileTime(elem.type, TypeInfo(first.type.baseType))) {
          emit("Array literal element type mismatch: cannot convert '" +
                   typeName(elem.type) + "' to '" +
                   typeName(TypeInfo(first.type.baseType)) + "'",
               arr->elements[i]->line, arr->elements[i]->column);
        }
      }
      return {true, TypeInfo(first.type.baseType, true)};
    }
    if (auto *idx = dynamic_cast<IndexExpr *>(expr.get())) {
      InferredType arrayType = inferExpr(idx->arrayExpr);
      (void)inferExpr(idx->indexExpr);
      if (arrayType.known && arrayType.type.isArray) {
        return {true, TypeInfo(arrayType.type.baseType)};
      }
      return {};
    }
    if (auto *call = dynamic_cast<CallExpr *>(expr.get())) {
      return inferCall(call);
    }
    if (auto *cond = dynamic_cast<ConditionalExpr *>(expr.get())) {
      InferredType c = inferExpr(cond->condition);
      if (c.known && c.type.baseType == DataType::VOID && !c.type.isArray) {
        emit("Conditional expression condition cannot be void", cond->line,
             cond->column);
      }
      InferredType t = inferExpr(cond->thenExpr);
      InferredType e = inferExpr(cond->elseExpr);
      if (t.known && e.known) {
        if (canConvertCompileTime(t.type, e.type)) {
          return {true, e.type};
        }
        if (canConvertCompileTime(e.type, t.type)) {
          return {true, t.type};
        }
        emit("Conditional branches have incompatible types '" + typeName(t.type) +
                 "' and '" + typeName(e.type) + "'",
             cond->line, cond->column);
      }
      return {};
    }
    if (auto *un = dynamic_cast<UnaryExpr *>(expr.get())) {
      InferredType operand = inferExpr(un->operand);
      if (!operand.known) {
        return {};
      }
      switch (un->op) {
      case UnaryExpr::Operator::NEGATE:
        if (!operand.type.isArray && isNumeric(operand.type.baseType)) {
          if (operand.type.baseType == DataType::DOUBLE ||
              operand.type.baseType == DataType::FLOAT) {
            return InferredType{true, TypeInfo(operand.type.baseType)};
          }
          return InferredType{true, TypeInfo(DataType::INT32)};
        }
        emit("Unary '-' requires numeric operand", un->line, un->column);
        return {};
      case UnaryExpr::Operator::LOGICAL_NOT:
        if (!operand.type.isArray && operand.type.baseType == DataType::VOID) {
          emit("Unary '!' cannot be applied to void", un->line, un->column);
        }
        return {true, TypeInfo(DataType::BOOL)};
      case UnaryExpr::Operator::BIT_NOT:
        if (!operand.type.isArray && isInteger(operand.type.baseType)) {
          return {true, TypeInfo(isUnsignedInteger(operand.type.baseType)
                                     ? DataType::UINT64
                                     : DataType::INT64)};
        }
        emit("Unary '~' requires integer operand", un->line, un->column);
        return {};
      }
      return {};
    }
    if (auto *bin = dynamic_cast<BinaryExpr *>(expr.get())) {
      return inferBinary(bin);
    }

    return {};
  }

  InferredType inferCall(CallExpr *call) {
    std::vector<InferredType> argTypes;
    argTypes.reserve(call->arguments.size());
    for (const auto &arg : call->arguments) {
      argTypes.push_back(inferExpr(arg));
    }

    if (call->functionName == "len") {
      if (argTypes.size() != 1) {
        emit("len expects 1 argument", call->line, call->column);
      }
      return {true, TypeInfo(DataType::INT32)};
    }
    if (call->functionName == "push") {
      if (argTypes.size() != 2) {
        emit("push expects 2 arguments", call->line, call->column);
        return {true, TypeInfo(DataType::INT32)};
      }
      return {true, TypeInfo(DataType::INT32)};
    }
    if (call->functionName == "pop") {
      if (argTypes.size() != 1) {
        emit("pop expects 1 argument", call->line, call->column);
        return {};
      }
      if (argTypes[0].known) {
        if (!argTypes[0].type.isArray) {
          return {};
        }
        return {true, TypeInfo(argTypes[0].type.baseType)};
      }
      return {};
    }

    auto localProcIt = _procedures.find(call->functionName);
    ProcedureDeclPtr proc = nullptr;
    if (localProcIt != _procedures.end()) {
      proc = localProcIt->second;
    } else {
      proc = _interpreter->getProcedure(call->functionName);
    }

    if (!proc) {
      // Might be external function; skip strict type checks.
      return {};
    }

    if (argTypes.size() != proc->parameters.size()) {
      emit("Procedure '" + call->functionName + "' expects " +
               std::to_string(proc->parameters.size()) + " arguments, got " +
               std::to_string(argTypes.size()),
           call->line, call->column);
    } else {
      for (size_t i = 0; i < argTypes.size(); ++i) {
        expectConvertible(argTypes[i], proc->parameters[i].type, call->line,
                          call->column, "procedure call argument");
      }
    }

    return {true, proc->returnType};
  }

  InferredType inferBinary(BinaryExpr *bin) {
    InferredType left = inferExpr(bin->left);
    InferredType right = inferExpr(bin->right);
    if (!left.known || !right.known) {
      if (bin->op == BinaryExpr::Operator::EQUAL ||
          bin->op == BinaryExpr::Operator::NOT_EQUAL ||
          bin->op == BinaryExpr::Operator::LESS_THAN ||
          bin->op == BinaryExpr::Operator::GREATER_THAN ||
          bin->op == BinaryExpr::Operator::LESS_EQUAL ||
          bin->op == BinaryExpr::Operator::GREATER_EQUAL ||
          bin->op == BinaryExpr::Operator::LOGICAL_AND ||
          bin->op == BinaryExpr::Operator::LOGICAL_OR) {
        return {true, TypeInfo(DataType::BOOL)};
      }
      return {};
    }

    auto numericResult = [&]() -> InferredType {
      if (left.type.baseType == DataType::DOUBLE ||
          right.type.baseType == DataType::DOUBLE) {
        return {true, TypeInfo(DataType::DOUBLE)};
      }
      if (left.type.baseType == DataType::FLOAT ||
          right.type.baseType == DataType::FLOAT) {
        return {true, TypeInfo(DataType::FLOAT)};
      }
      if (isUnsignedInteger(left.type.baseType) ||
          isUnsignedInteger(right.type.baseType)) {
        return {true, TypeInfo(DataType::UINT64)};
      }
      return {true, TypeInfo(DataType::INT64)};
    };

    switch (bin->op) {
    case BinaryExpr::Operator::ADD:
      if (!left.type.isArray && !right.type.isArray &&
          (left.type.baseType == DataType::STRING ||
           right.type.baseType == DataType::STRING)) {
        return {true, TypeInfo(DataType::STRING)};
      }
      if (!left.type.isArray && !right.type.isArray &&
          isNumeric(left.type.baseType) && isNumeric(right.type.baseType)) {
        return numericResult();
      }
      return {};
    case BinaryExpr::Operator::SUBTRACT:
    case BinaryExpr::Operator::MULTIPLY:
    case BinaryExpr::Operator::DIVIDE:
      if (!left.type.isArray && !right.type.isArray &&
          isNumeric(left.type.baseType) && isNumeric(right.type.baseType)) {
        return numericResult();
      }
      return {};
    case BinaryExpr::Operator::MODULO:
      if (!left.type.isArray && !right.type.isArray &&
          isInteger(left.type.baseType) && isInteger(right.type.baseType)) {
        return numericResult();
      }
      return {};
    case BinaryExpr::Operator::BIT_AND:
    case BinaryExpr::Operator::BIT_OR:
    case BinaryExpr::Operator::BIT_XOR:
    case BinaryExpr::Operator::LSHIFT:
    case BinaryExpr::Operator::RSHIFT:
      if (!left.type.isArray && !right.type.isArray &&
          isInteger(left.type.baseType) && isInteger(right.type.baseType)) {
        return {true, TypeInfo(isUnsignedInteger(left.type.baseType) ||
                                       isUnsignedInteger(right.type.baseType)
                                   ? DataType::UINT64
                                   : DataType::INT64)};
      }
      return {};
    case BinaryExpr::Operator::EQUAL:
    case BinaryExpr::Operator::NOT_EQUAL:
      if (canCompare(left.type, right.type) ||
          (left.type.isArray && right.type.isArray &&
           left.type.baseType == right.type.baseType)) {
        return {true, TypeInfo(DataType::BOOL)};
      }
      return {true, TypeInfo(DataType::BOOL)};
    case BinaryExpr::Operator::LESS_THAN:
    case BinaryExpr::Operator::GREATER_THAN:
    case BinaryExpr::Operator::LESS_EQUAL:
    case BinaryExpr::Operator::GREATER_EQUAL:
      if (!left.type.isArray && !right.type.isArray &&
          ((isNumeric(left.type.baseType) && isNumeric(right.type.baseType)) ||
           (left.type.baseType == DataType::STRING &&
            right.type.baseType == DataType::STRING))) {
        return {true, TypeInfo(DataType::BOOL)};
      }
      return {true, TypeInfo(DataType::BOOL)};
    case BinaryExpr::Operator::LOGICAL_AND:
    case BinaryExpr::Operator::LOGICAL_OR:
      if (!left.type.isArray && left.type.baseType == DataType::VOID) {
        emit("Logical operator cannot use void left operand", bin->line,
             bin->column);
      }
      if (!right.type.isArray && right.type.baseType == DataType::VOID) {
        emit("Logical operator cannot use void right operand", bin->line,
             bin->column);
      }
      return {true, TypeInfo(DataType::BOOL)};
    }
    return {};
  }
};

} // namespace

std::string CompilationError::toString() const {
  std::stringstream ss;
  ss << filename << ":" << line << ":" << column << ": error: " << message;
  if (!procedureName.empty()) {
    ss << " in procedure '" << procedureName << "'";
  }
  return ss.str();
}

ScriptManager::ScriptManager()
    : _interpreter(std::make_unique<Interpreter>()) {}

ScriptManager::~ScriptManager() = default;

bool ScriptManager::loadScriptFile(const std::string &filename,
                                   std::vector<CompilationError> &errors) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    errors.push_back(
        CompilationError("Failed to open file", filename, "", 0, 0));
    return false;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string source = buffer.str();

  return loadScriptSource(source, filename, errors);
}

bool ScriptManager::loadScriptSource(const std::string &source,
                                     const std::string &filename,
                                     std::vector<CompilationError> &errors) {
  return compileScript(source, filename, errors, true);
}

bool ScriptManager::checkScript(const std::string &filename,
                                std::vector<CompilationError> &errors) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    errors.push_back(
        CompilationError("Failed to open file", filename, "", 0, 0));
    return false;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string source = buffer.str();

  return checkScriptSource(source, filename, errors);
}

bool ScriptManager::checkScriptSource(const std::string &source,
                                      const std::string &filename,
                                      std::vector<CompilationError> &errors) {
  return compileScript(source, filename, errors, false);
}

bool ScriptManager::compileScript(const std::string &source,
                                  const std::string &filename,
                                  std::vector<CompilationError> &errors,
                                  bool load) {
  errors.clear();

  try {
    // Tokenize
    Lexer lexer(source, filename);
    std::vector<Token> tokens;

    try {
      tokens = lexer.tokenize();
    } catch (const std::exception &e) {
      errors.push_back(CompilationError(e.what(), filename, "", 0, 0));
      return false;
    }

    // Check for unknown tokens
    for (const auto &token : tokens) {
      if (token.type == TokenType::UNKNOWN) {
        std::stringstream ss;
        ss << "Unexpected character: '" << token.lexeme << "'";
        errors.push_back(
            CompilationError(ss.str(), filename, "", token.line, token.column));
      }
    }

    if (!errors.empty()) {
      return false;
    }

    // Parse
    Parser parser(tokens, filename);
    ScriptPtr script;

    try {
      script = parser.parse();
    } catch (const ParseError &e) {
      errors.push_back(CompilationError(e.what(), filename, e.procedureName,
                                        e.line, e.column));
      return false;
    } catch (const std::exception &e) {
      errors.push_back(CompilationError(e.what(), filename, "", 0, 0));
      return false;
    }

    if (parser.hasErrors()) {
      for (const auto &pe : parser.getErrors()) {
        errors.push_back(CompilationError(pe.what(), filename, pe.procedureName,
                                          pe.line, pe.column));
      }
      return false;
    }

    // Check for procedures with duplicate names
    std::unordered_map<std::string, int> procNames;
    for (const auto &proc : script->procedures) {
      if (procNames.find(proc->name) != procNames.end()) {
        errors.push_back(
            CompilationError("Duplicate procedure name: " + proc->name,
                             filename, proc->name, proc->line, proc->column));
      }
      procNames[proc->name]++;
    }

    if (!errors.empty()) {
      return false;
    }

    // Semantic validation (type compatibility and basic control-flow checks)
    SemanticValidator validator(filename, _interpreter.get(), errors);
    validator.validate(script);
    if (!errors.empty()) {
      return false;
    }

    // Load into interpreter if requested
    if (load) {
      _interpreter->loadScript(script);

      // Track which file each procedure came from
      for (const auto &proc : script->procedures) {
        _procedureFiles[proc->name] = filename;
      }
    }

    return true;

  } catch (const std::exception &e) {
    errors.push_back(CompilationError(e.what(), filename, "", 0, 0));
    return false;
  }
}

bool ScriptManager::executeProcedure(const std::string &procedureName,
                                     const std::vector<Value> &arguments,
                                     Value &returnValue,
                                     std::string &errorMessage) {
  try {
    returnValue = _interpreter->executeProcedure(procedureName, arguments);
    return true;
  } catch (const RuntimeError &e) {
    std::stringstream ss;
    ss << "Runtime error";
    if (!e.filename.empty()) {
      ss << " in " << e.filename;
    }
    ss << " at line " << e.line << ", column " << e.column;
    if (!e.procedureName.empty()) {
      ss << " in procedure '" << e.procedureName << "'";
    }
    ss << ": " << e.what();
    errorMessage = ss.str();
    return false;
  } catch (const std::exception &e) {
    errorMessage = std::string("Runtime error: ") + e.what();
    return false;
  }
}

bool ScriptManager::hasProcedure(const std::string &name) const {
  return _interpreter->hasProcedure(name);
}

std::vector<std::string> ScriptManager::getProcedureNames() const {
  std::vector<std::string> names;
  for (const auto &pair : _procedureFiles) {
    names.push_back(pair.first);
  }
  return names;
}

bool ScriptManager::getProcedureInfo(const std::string &name,
                                     ProcedureInfo &info) const {
  auto proc = _interpreter->getProcedure(name);
  if (!proc) {
    return false;
  }

  info.name = proc->name;
  info.returnType = proc->returnType;
  info.parameters = proc->parameters;

  auto it = _procedureFiles.find(name);
  if (it != _procedureFiles.end()) {
    info.filename = it->second;
  }

  return true;
}

void ScriptManager::registerExternalFunction(
    const std::string &name, ExternalFunctionCallback callback) {
  _interpreter->registerExternalFunction(name, callback);
}

void ScriptManager::registerExternalFunctions(
    const std::vector<ExternalBinding> &bindings) {
  _interpreter->registerExternalFunctions(bindings);
}

void ScriptManager::registerExternalFunctions(
    std::initializer_list<ExternalBinding> bindings) {
  _interpreter->registerExternalFunctions(bindings);
}

void ScriptManager::unregisterExternalFunction(const std::string &name) {
  _interpreter->unregisterExternalFunction(name);
}

bool ScriptManager::hasExternalFunction(const std::string &name) const {
  return _interpreter->hasExternalFunction(name);
}

void ScriptManager::registerExternalVariable(
    const std::string &name, ExternalVariableGetter getter,
    ExternalVariableSetter setter) {
  _interpreter->registerExternalVariable(name, std::move(getter),
                                         std::move(setter));
}

void ScriptManager::registerExternalVariableReadOnly(
    const std::string &name, ExternalVariableGetter getter) {
  _interpreter->registerExternalVariableReadOnly(name, std::move(getter));
}

void ScriptManager::unregisterExternalVariable(const std::string &name) {
  _interpreter->unregisterExternalVariable(name);
}

bool ScriptManager::hasExternalVariable(const std::string &name) const {
  return _interpreter->hasExternalVariable(name);
}

void ScriptManager::clear() {
  _interpreter = std::make_unique<Interpreter>();
  _procedureFiles.clear();
}

void ScriptManager::setExecutionLimits(size_t maxCallDepth, size_t maxSteps) {
  _interpreter->setExecutionLimits(maxCallDepth, maxSteps);
}

void ScriptManager::clearExecutionLimits() { _interpreter->clearExecutionLimits(); }

} // namespace Script
