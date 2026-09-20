#include "ScriptManager.h"
#include "Builtins.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
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

// True if the vector contains at least one error (warnings don't count).
bool hasErrors(const std::vector<CompilationError> &errors) {
  return std::any_of(errors.begin(), errors.end(),
                     [](const CompilationError &e) { return !e.isWarning; });
}

bool canConvertCompileTime(const TypeInfo &from, const TypeInfo &to) {
  if (from == to) {
    return true;
  }
  if (from.isArray != to.isArray || from.isMap != to.isMap) {
    return false;
  }
  if (from.isMap) {
    // Empty {} literal (void key/value) converts to any map type.
    if (from.keyType == DataType::VOID &&
        from.baseType == DataType::VOID) {
      return true;
    }
    if (from.keyType != to.keyType &&
        !(isNumeric(from.keyType) && isNumeric(to.keyType))) {
      return false;
    }
    TypeInfo fromV = from.mapValueType ? *from.mapValueType
                                       : TypeInfo(from.baseType);
    TypeInfo toV =
        to.mapValueType ? *to.mapValueType : TypeInfo(to.baseType);
    return canConvertCompileTime(fromV, toV);
  }
  if (from.isArray) {
    if (from.baseType == DataType::VOID && !from.isStruct) {
      return true; // empty [] literal converts to any array type
    }
    return from.baseType == to.baseType && from.isStruct == to.isStruct &&
           from.structName == to.structName;
  }
  // Structs convert only to the same struct type.
  if (from.isStruct || to.isStruct) {
    return from.isStruct && to.isStruct &&
           from.structName == to.structName;
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
    _structs.clear();
    for (const auto &proc : script->procedures) {
      _procedures[proc->name] = proc;
    }
    for (const auto &s : script->structs) {
      if (_structs.count(s->name)) {
        emit("Duplicate struct name: " + s->name, s->line, s->column);
      }
      if (_procedures.count(s->name) ||
          _interpreter->hasProcedure(s->name)) {
        emit("Struct '" + s->name +
                 "' conflicts with a procedure of the same name",
             s->line, s->column);
      }
      _structs[s->name] = s;
    }
    for (const auto &s : script->structs) {
      validateStruct(s);
    }
    for (const auto &s : script->structs) {
      checkStructCycle(s);
    }
    for (const auto &proc : script->procedures) {
      validateProcedure(proc);
    }
  }

  // Validate top-level statements as if they formed a procedure body (REPL
  // snippets). globals seeds the outer scope so variables declared in earlier
  // snippets resolve. A top-level 'return <expr>' of any type is allowed.
  void validateStatements(
      const std::vector<StmtPtr> &statements,
      const std::unordered_map<std::string, std::pair<TypeInfo, bool>>
          &globals) {
    _procedures.clear();
    for (const auto &name : _interpreter->getProcedureNames()) {
      _procedures[name] = _interpreter->getProcedure(name);
    }

    _scopes.clear();
    _currentProcedure = "<repl>";
    _currentReturnType = TypeInfo(DataType::VOID);
    _loopDepth = 0;
    _switchDepth = 0;
    _allowAnyReturn = true;

    enterScope();
    for (const auto &kv : globals) {
      defineVar(kv.first, kv.second.first, kv.second.second,
                /*noWarnUnused*/ true);
    }
    for (const auto &stmt : statements) {
      validateStmt(stmt);
    }
    exitScope();
    _allowAnyReturn = false;
  }

private:
  const std::string &_filename;
  Interpreter *_interpreter;
  std::vector<CompilationError> &_errors;
  std::unordered_map<std::string, ProcedureDeclPtr> _procedures;
  std::unordered_map<std::string, StructDeclPtr> _structs;
  struct VarInfo {
    TypeInfo type;
    bool isConst = false;
    bool used = false;
    bool noWarnUnused = false; // params, REPL globals
    int line = 0;
    int column = 0;
  };
  std::vector<std::unordered_map<std::string, VarInfo>> _scopes;
  TypeInfo _currentReturnType = TypeInfo(DataType::VOID);
  std::string _currentProcedure;
  int _loopDepth = 0;
  int _switchDepth = 0;
  bool _allowAnyReturn = false;

  void emit(const std::string &message, int line, int column) {
    _errors.emplace_back(message, _filename, _currentProcedure, line, column);
  }

  void warn(const std::string &message, int line, int column) {
    _errors.emplace_back(message, _filename, _currentProcedure, line, column,
                         /*warning*/ true);
  }

  void enterScope() { _scopes.emplace_back(); }
  void exitScope() {
    if (_scopes.empty()) {
      return;
    }
    for (const auto &kv : _scopes.back()) {
      if (!kv.second.used && !kv.second.noWarnUnused) {
        warn("Unused variable '" + kv.first + "'", kv.second.line,
             kv.second.column);
      }
    }
    _scopes.pop_back();
  }

  void defineVar(const std::string &name, const TypeInfo &type,
                 bool isConst = false, bool noWarnUnused = false, int line = 0,
                 int column = 0) {
    if (_scopes.empty()) {
      enterScope();
    }
    _scopes.back()[name] = VarInfo{type, isConst, noWarnUnused, noWarnUnused,
                                   line, column};
  }

  void markUsed(const std::string &name) {
    for (size_t i = _scopes.size(); i-- > 0;) {
      auto it = _scopes[i].find(name);
      if (it != _scopes[i].end()) {
        it->second.used = true;
        return;
      }
    }
  }

  std::optional<VarInfo> resolveVar(const std::string &name) const {
    for (size_t i = _scopes.size(); i-- > 0;) {
      auto it = _scopes[i].find(name);
      if (it != _scopes[i].end()) {
        return it->second;
      }
    }
    return std::nullopt;
  }

  // Struct decl from this file, falling back to already-loaded ones.
  StructDeclPtr resolveStruct(const std::string &name) const {
    auto it = _structs.find(name);
    if (it != _structs.end()) {
      return it->second;
    }
    return _interpreter->getStruct(name);
  }

  // Root variable of an assignable target chain: for `a.b[i].c` → `a`.
  static VariableExpr *rootVariable(const ExprPtr &e) {
    const Expression *cur = e.get();
    while (cur) {
      if (auto *v = dynamic_cast<const VariableExpr *>(cur)) {
        return const_cast<VariableExpr *>(v);
      }
      if (auto *m = dynamic_cast<const MemberExpr *>(cur)) {
        cur = m->object.get();
        continue;
      }
      if (auto *i = dynamic_cast<const IndexExpr *>(cur)) {
        cur = i->arrayExpr.get();
        continue;
      }
      return nullptr;
    }
    return nullptr;
  }

  void validateStruct(const StructDeclPtr &decl) {
    std::unordered_map<std::string, const Parameter *> seen;
    for (const auto &f : decl->fields) {
      if (seen.count(f.name)) {
        emit("Duplicate field '" + f.name + "' in struct '" + decl->name + "'",
             decl->line, decl->column);
      }
      seen[f.name] = &f;
      const TypeInfo &t = f.type;
      if (!t.isStruct && !t.isArray && !t.isMap &&
          t.baseType == DataType::VOID) {
        emit("Struct field '" + f.name + "' cannot be void", decl->line,
             decl->column);
      }
      if (t.isMap && !t.isStruct && t.keyType == DataType::VOID &&
          (!t.mapValueType || t.mapValueType->baseType == DataType::VOID)) {
        emit("Struct field '" + f.name + "' has untyped map type", decl->line,
             decl->column);
      }
    }
  }

  // Struct field types that are themselves structs (direct edges for the
  // cycle check). Arrays/maps carrying structs are followed too.
  static void structRefs(const TypeInfo &t, std::vector<std::string> &out) {
    if (t.isStruct) {
      out.push_back(t.structName);
    }
    if (t.isMap && t.mapValueType) {
      structRefs(*t.mapValueType, out);
    }
  }

  // Reject field graphs that recurse back to the struct being declared —
  // values are stored by value so a cycle would be infinitely sized.
  void checkStructCycle(const StructDeclPtr &decl) {
    std::unordered_set<std::string> stack;
    std::unordered_set<std::string> done;
    if (structReaches(decl->name, decl->name, stack, done)) {
      emit("Struct '" + decl->name + "' contains itself recursively",
           decl->line, decl->column);
    }
  }

  bool structReaches(const std::string &from, const std::string &target,
                     std::unordered_set<std::string> &stack,
                     std::unordered_set<std::string> &done) {
    if (!stack.insert(from).second) {
      return from == target;
    }
    if (done.count(from)) {
      stack.erase(from);
      return false;
    }
    StructDeclPtr decl = resolveStruct(from);
    if (decl) {
      for (const auto &f : decl->fields) {
        std::vector<std::string> refs;
        structRefs(f.type, refs);
        for (const auto &r : refs) {
          if (structReaches(r, target, stack, done)) {
            stack.erase(from);
            return true;
          }
        }
      }
    }
    stack.erase(from);
    done.insert(from);
    return false;
  }

  void validateProcedure(const ProcedureDeclPtr &proc) {
    _scopes.clear();
    _currentProcedure = proc->name;
    _currentReturnType = proc->returnType;
    _loopDepth = 0;
    _switchDepth = 0;

    enterScope();
    for (const auto &param : proc->parameters) {
      defineVar(param.name, param.type, false, /*noWarnUnused*/ true);
    }
    validateStmt(proc->body);
    if (_currentReturnType.baseType != DataType::VOID ||
        _currentReturnType.isArray || _currentReturnType.isMap ||
        _currentReturnType.isStruct) {
      if (!alwaysReturns(proc->body)) {
        warn("Procedure '" + proc->name +
                 "' may reach the end without returning a value",
             proc->line, proc->column);
      }
    }
    exitScope();
  }

  void expectConvertible(const InferredType &from, const TypeInfo &to,
                         int line, int column, const std::string &context,
                         const ExprPtr &srcExpr = ExprPtr()) {
    if (!from.known) {
      return;
    }
    if (!canConvertCompileTime(from.type, to)) {
      emit("Type mismatch in " + context + ": cannot convert '" +
               typeName(from.type) + "' to '" + typeName(to) + "'",
           line, column);
      return;
    }
    if (isNarrowing(from.type, to) && !exprFitsTarget(srcExpr, to)) {
      warn("Narrowing conversion in " + context + ": '" +
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
                          "variable declaration", varDecl->initializer);
      }
      defineVar(varDecl->name, varDecl->type, varDecl->isConst, false,
                varDecl->line, varDecl->column);
      return;
    }
    if (auto *assign = dynamic_cast<AssignStmt *>(stmt.get())) {
      InferredType valueType = inferExpr(assign->value);
      auto targetVar = resolveVar(assign->variableName);
      if (targetVar.has_value()) {
        if (targetVar->isConst) {
          emit("Cannot assign to const variable '" + assign->variableName +
                   "'",
               assign->line, assign->column);
          return;
        }
        expectConvertible(valueType, targetVar->type, assign->line,
                          assign->column, "assignment", assign->value);
      }
      return;
    }
    if (auto *idxAssign = dynamic_cast<IndexAssignStmt *>(stmt.get())) {
      if (auto *arrVar =
              dynamic_cast<VariableExpr *>(idxAssign->arrayExpr.get())) {
        auto info = resolveVar(arrVar->name);
        if (info.has_value() && info->isConst) {
          emit("Cannot modify elements of const array/map '" + arrVar->name +
                   "'",
               idxAssign->line, idxAssign->column);
          return;
        }
      }
      InferredType arrayType = inferExpr(idxAssign->arrayExpr);
      InferredType keyIdxType = inferExpr(idxAssign->indexExpr);
      InferredType valueType = inferExpr(idxAssign->value);
      if (arrayType.known && arrayType.type.isArray) {
        expectConvertible(valueType, arrayType.type.elementType(),
                          idxAssign->line, idxAssign->column,
                          "index assignment", idxAssign->value);
      } else if (arrayType.known && arrayType.type.isMap) {
        expectConvertible(keyIdxType, TypeInfo(arrayType.type.keyType),
                          idxAssign->indexExpr->line,
                          idxAssign->indexExpr->column, "map key");
        TypeInfo vT = arrayType.type.mapValueType
                          ? *arrayType.type.mapValueType
                          : TypeInfo(arrayType.type.baseType);
        expectConvertible(valueType, vT, idxAssign->line, idxAssign->column,
                          "map entry assignment", idxAssign->value);
      }
      return;
    }
    if (auto *memAssign = dynamic_cast<MemberAssignStmt *>(stmt.get())) {
      if (auto *root = rootVariable(memAssign->object)) {
        auto info = resolveVar(root->name);
        if (info.has_value() && info->isConst) {
          emit("Cannot modify fields of const struct '" + root->name + "'",
               memAssign->line, memAssign->column);
          return;
        }
      }
      InferredType objType = inferExpr(memAssign->object);
      InferredType valueType = inferExpr(memAssign->value);
      if (objType.known) {
        if (!objType.type.isStruct || objType.type.isArray) {
          emit("Member assignment '.' requires a struct value",
               memAssign->line, memAssign->column);
        } else if (StructDeclPtr decl = resolveStruct(objType.type.structName)) {
          const Parameter *field = nullptr;
          for (const auto &f : decl->fields) {
            if (f.name == memAssign->member) {
              field = &f;
              break;
            }
          }
          if (!field) {
            emit("Struct '" + objType.type.structName + "' has no field '" +
                     memAssign->member + "'",
                 memAssign->line, memAssign->column);
          } else {
            expectConvertible(valueType, field->type, memAssign->line,
                              memAssign->column, "member assignment",
                              memAssign->value);
          }
        }
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
          !condType.type.isArray && !condType.type.isStruct) {
        emit("If condition cannot be void", ifStmt->line, ifStmt->column);
      }
      validateStmt(ifStmt->thenBranch);
      validateStmt(ifStmt->elseBranch);
      return;
    }
    if (auto *whileStmt = dynamic_cast<WhileStmt *>(stmt.get())) {
      InferredType condType = inferExpr(whileStmt->condition);
      if (condType.known && condType.type.baseType == DataType::VOID &&
          !condType.type.isArray && !condType.type.isStruct) {
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
    if (auto *forEach = dynamic_cast<ForEachStmt *>(stmt.get())) {
      InferredType iterType = inferExpr(forEach->iterable);
      if (iterType.known) {
        if (iterType.type.isArray) {
          expectConvertible(InferredType{true, iterType.type.elementType()},
                            forEach->elemType, forEach->line, forEach->column,
                            "for-each variable");
        } else if (iterType.type.isMap) {
          expectConvertible(InferredType{true, TypeInfo(iterType.type.keyType)},
                            forEach->elemType, forEach->line, forEach->column,
                            "for-each variable");
        } else if (iterType.type.baseType == DataType::STRING) {
          expectConvertible(InferredType{true, TypeInfo(DataType::CHAR)},
                            forEach->elemType, forEach->line, forEach->column,
                            "for-each variable");
        } else {
          emit("for-each requires an array, map, or string", forEach->line,
               forEach->column);
        }
      }
      enterScope();
      defineVar(forEach->varName, forEach->elemType, forEach->elemConst, false,
                forEach->line, forEach->column);
      ++_loopDepth;
      validateStmt(forEach->body);
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
          !condType.type.isArray && !condType.type.isStruct) {
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
      if (_allowAnyReturn) {
        if (ret->value) {
          inferExpr(ret->value);
        }
        return;
      }
      if (_currentReturnType.baseType == DataType::VOID &&
          !_currentReturnType.isArray && !_currentReturnType.isMap &&
          !_currentReturnType.isStruct) {
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
                        "return statement", ret->value);
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
    if (a.isStruct || b.isStruct) {
      // ==/!= on the same struct type compares field-wise.
      return a.isStruct && b.isStruct && a.structName == b.structName;
    }
    if (a.isArray || b.isArray || a.isMap || b.isMap) {
      return false;
    }
    if (isNumeric(a.baseType) && isNumeric(b.baseType)) {
      return true;
    }
    return false;
  }

  // ---- warnings -----------------------------------------------------------

  static int intRank(DataType t) {
    switch (t) {
    case DataType::INT8:
    case DataType::UINT8:
      return 1;
    case DataType::INT16:
    case DataType::UINT16:
      return 2;
    case DataType::INT32:
    case DataType::UINT32:
      return 3;
    default:
      return 4;
    }
  }

  // From is a wider/higher-precision type than to.
  bool isNarrowing(const TypeInfo &from, const TypeInfo &to) const {
    if (from.isArray || to.isArray || from.isMap || to.isMap ||
        from.isStruct || to.isStruct) {
      return false;
    }
    DataType f = from.baseType;
    DataType t = to.baseType;
    if (f == t) {
      return false;
    }
    if (f == DataType::DOUBLE) {
      return true; // double -> float/int always narrows
    }
    if (f == DataType::FLOAT && t != DataType::DOUBLE) {
      return true;
    }
    if (isInteger(f) && t == DataType::FLOAT) {
      return intRank(f) >= 4; // int64/uint64 lose precision in float
    }
    if (isInteger(f) && t == DataType::CHAR) {
      return true;
    }
    if (isInteger(f) && isInteger(t)) {
      return intRank(t) < intRank(f);
    }
    return false;
  }

  // True when the source expression provably fits the target: integer
  // literals in range, or composite exprs whose leaves all fit.
  bool exprFitsTarget(const ExprPtr &e, const TypeInfo &to) {
    if (!e) {
      return false;
    }
    if (auto *bin = dynamic_cast<BinaryExpr *>(e.get())) {
      return exprFitsTarget(bin->left, to) && exprFitsTarget(bin->right, to);
    }
    if (auto *un = dynamic_cast<UnaryExpr *>(e.get())) {
      return exprFitsTarget(un->operand, to);
    }
    if (auto *cond = dynamic_cast<ConditionalExpr *>(e.get())) {
      return exprFitsTarget(cond->thenExpr, to) &&
             exprFitsTarget(cond->elseExpr, to);
    }
    if (dynamic_cast<LiteralExpr *>(e.get())) {
      return literalFits(e.get(), to);
    }
    InferredType t = inferExpr(e);
    return t.known && !isNarrowing(t.type, to);
  }

  // Integer literal that fits the target type doesn't warn on narrowing.
  bool literalFits(const Expression *e, const TypeInfo &to) const {
    if (!e || !isInteger(to.baseType)) {
      return false;
    }
    auto *lit = dynamic_cast<const LiteralExpr *>(e);
    if (!lit) {
      return false;
    }
    const Value &v = lit->value;
    long double lv;
    if (std::holds_alternative<char>(v)) {
      lv = std::get<char>(v);
    } else if (std::holds_alternative<int8_t>(v)) {
      lv = std::get<int8_t>(v);
    } else if (std::holds_alternative<uint8_t>(v)) {
      lv = std::get<uint8_t>(v);
    } else if (std::holds_alternative<int16_t>(v)) {
      lv = std::get<int16_t>(v);
    } else if (std::holds_alternative<uint16_t>(v)) {
      lv = std::get<uint16_t>(v);
    } else if (std::holds_alternative<int32_t>(v)) {
      lv = std::get<int32_t>(v);
    } else if (std::holds_alternative<uint32_t>(v)) {
      lv = std::get<uint32_t>(v);
    } else if (std::holds_alternative<int64_t>(v)) {
      lv = static_cast<long double>(std::get<int64_t>(v));
    } else if (std::holds_alternative<uint64_t>(v)) {
      lv = static_cast<long double>(std::get<uint64_t>(v));
    } else {
      return false;
    }
    long double lo, hi;
    switch (to.baseType) {
    case DataType::INT8:
      lo = -128;
      hi = 127;
      break;
    case DataType::UINT8:
      lo = 0;
      hi = 255;
      break;
    case DataType::INT16:
      lo = -32768;
      hi = 32767;
      break;
    case DataType::UINT16:
      lo = 0;
      hi = 65535;
      break;
    case DataType::INT32:
      lo = -2147483648.0L;
      hi = 2147483647.0L;
      break;
    case DataType::UINT32:
      lo = 0;
      hi = 4294967295.0L;
      break;
    case DataType::INT64:
      lo = -9223372036854775808.0L;
      hi = 9223372036854775807.0L;
      break;
    case DataType::UINT64:
      lo = 0;
      hi = 18446744073709551615.0L;
      break;
    default:
      return false;
    }
    return lv >= lo && lv <= hi;
  }

  // True if every path through stmt ends in 'return'.
  bool alwaysReturns(const StmtPtr &s) const {
    if (!s) {
      return false;
    }
    if (dynamic_cast<ReturnStmt *>(s.get())) {
      return true;
    }
    if (auto *b = dynamic_cast<BlockStmt *>(s.get())) {
      for (const auto &st : b->statements) {
        if (alwaysReturns(st)) {
          return true;
        }
      }
      return false;
    }
    if (auto *i = dynamic_cast<IfStmt *>(s.get())) {
      return i->elseBranch && alwaysReturns(i->thenBranch) &&
             alwaysReturns(i->elseBranch);
    }
    if (auto *d = dynamic_cast<DoWhileStmt *>(s.get())) {
      return alwaysReturns(d->body);
    }
    if (auto *w = dynamic_cast<WhileStmt *>(s.get())) {
      if (isLiteralTrue(w->condition) && !containsBreak(w->body)) {
        return alwaysReturns(w->body);
      }
      return false;
    }
    if (auto *f = dynamic_cast<ForStmt *>(s.get())) {
      bool infinite =
          !f->condition || isLiteralTrue(f->condition);
      if (infinite && !containsBreak(f->body)) {
        return alwaysReturns(f->body);
      }
      return false;
    }
    if (auto *sw = dynamic_cast<SwitchStmt *>(s.get())) {
      bool hasDefault = false;
      for (const auto &c : sw->cases) {
        hasDefault = hasDefault || c.isDefault;
        bool returns = false;
        for (const auto &st : c.statements) {
          if (alwaysReturns(st)) {
            returns = true;
            break;
          }
        }
        if (!returns) {
          return false;
        }
      }
      return hasDefault;
    }
    return false;
  }

  bool isLiteralTrue(const ExprPtr &e) const {
    auto *lit = dynamic_cast<LiteralExpr *>(e.get());
    return lit && std::holds_alternative<bool>(lit->value) &&
           std::get<bool>(lit->value);
  }

  // A break that exits this statement's own loop/switch, not a nested one.
  bool containsBreak(const StmtPtr &s) const {
    if (!s) {
      return false;
    }
    if (dynamic_cast<BreakStmt *>(s.get())) {
      return true;
    }
    if (dynamic_cast<WhileStmt *>(s.get()) ||
        dynamic_cast<DoWhileStmt *>(s.get()) ||
        dynamic_cast<ForStmt *>(s.get()) ||
        dynamic_cast<ForEachStmt *>(s.get()) ||
        dynamic_cast<SwitchStmt *>(s.get())) {
      return false; // breaks inside bind to the inner construct
    }
    if (auto *b = dynamic_cast<BlockStmt *>(s.get())) {
      for (const auto &st : b->statements) {
        if (containsBreak(st)) {
          return true;
        }
      }
      return false;
    }
    if (auto *i = dynamic_cast<IfStmt *>(s.get())) {
      return containsBreak(i->thenBranch) || containsBreak(i->elseBranch);
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
        markUsed(var->name);
        return {true, type->type};
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
            !canConvertCompileTime(elem.type, first.type)) {
          emit("Array literal element type mismatch: cannot convert '" +
                   typeName(elem.type) + "' to '" +
                   typeName(first.type) + "'",
               arr->elements[i]->line, arr->elements[i]->column);
        }
      }
      return {true, TypeInfo::arrayOf(first.type)};
    }
    if (auto *ml = dynamic_cast<MapLiteralExpr *>(expr.get())) {
      if (ml->entries.empty()) {
        return {true, TypeInfo::mapOf(TypeInfo(DataType::VOID),
                                      TypeInfo(DataType::VOID))};
      }
      InferredType k0 = inferExpr(ml->entries.front().first);
      InferredType v0 = inferExpr(ml->entries.front().second);
      if (!k0.known || !v0.known) {
        return {};
      }
      if (k0.type.isArray || k0.type.isMap ||
          k0.type.baseType == DataType::VOID) {
        emit("Map literal keys must be scalar values",
             ml->entries.front().first->line,
             ml->entries.front().first->column);
        return {};
      }
      for (size_t i = 1; i < ml->entries.size(); ++i) {
        InferredType k = inferExpr(ml->entries[i].first);
        InferredType v = inferExpr(ml->entries[i].second);
        if (k.known && !canConvertCompileTime(k.type, k0.type)) {
          emit("Map literal key type mismatch: cannot convert '" +
                   typeName(k.type) + "' to '" + typeName(k0.type) + "'",
               ml->entries[i].first->line, ml->entries[i].first->column);
        }
        if (v.known && !canConvertCompileTime(v.type, v0.type)) {
          emit("Map literal value type mismatch: cannot convert '" +
                   typeName(v.type) + "' to '" + typeName(v0.type) + "'",
               ml->entries[i].second->line, ml->entries[i].second->column);
        }
      }
      return {true, TypeInfo::mapOf(k0.type, v0.type)};
    }
    if (auto *idx = dynamic_cast<IndexExpr *>(expr.get())) {
      InferredType arrayType = inferExpr(idx->arrayExpr);
      InferredType indexType = inferExpr(idx->indexExpr);
      if (arrayType.known && arrayType.type.isMap) {
        expectConvertible(indexType, TypeInfo(arrayType.type.keyType),
                          idx->indexExpr->line, idx->indexExpr->column,
                          "map key");
        TypeInfo vT = arrayType.type.mapValueType
                          ? *arrayType.type.mapValueType
                          : TypeInfo(arrayType.type.baseType);
        return {true, vT};
      }
      if (arrayType.known && arrayType.type.isArray) {
        return {true, arrayType.type.elementType()};
      }
      if (arrayType.known && !arrayType.type.isArray &&
          arrayType.type.baseType == DataType::STRING) {
        return {true, TypeInfo(DataType::CHAR)};
      }
      return {};
    }
    if (auto *mem = dynamic_cast<MemberExpr *>(expr.get())) {
      InferredType objType = inferExpr(mem->object);
      if (!objType.known) {
        return {};
      }
      if (!objType.type.isStruct || objType.type.isArray) {
        emit("Member access '.' requires a struct value", mem->line,
             mem->column);
        return {};
      }
      if (StructDeclPtr decl = resolveStruct(objType.type.structName)) {
        for (const auto &f : decl->fields) {
          if (f.name == mem->member) {
            return {true, f.type};
          }
        }
        emit("Struct '" + objType.type.structName + "' has no field '" +
                 mem->member + "'",
             mem->line, mem->column);
      }
      return {};
    }
    if (auto *interp = dynamic_cast<InterpolatedStringExpr *>(expr.get())) {
      for (const auto &part : interp->parts) {
        if (part.isExpr) {
          (void)inferExpr(part.expr);
        }
      }
      return {true, TypeInfo(DataType::STRING)};
    }
    if (auto *call = dynamic_cast<CallExpr *>(expr.get())) {
      return inferCall(call);
    }
    if (auto *cond = dynamic_cast<ConditionalExpr *>(expr.get())) {
      InferredType c = inferExpr(cond->condition);
      if (c.known && c.type.baseType == DataType::VOID && !c.type.isArray &&
          !c.type.isStruct) {
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
        if (!operand.type.isArray && !operand.type.isStruct &&
          operand.type.baseType == DataType::VOID) {
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
    if (auto *upd = dynamic_cast<UpdateExpr *>(expr.get())) {
      VariableExpr *targetVar = rootVariable(upd->target);
      if (targetVar) {
        auto info = resolveVar(targetVar->name);
        if (info.has_value() && info->isConst) {
          emit("Cannot modify const variable '" + targetVar->name + "'",
               upd->line, upd->column);
        }
      }
      InferredType target = inferExpr(upd->target);
      if (target.known && !target.type.isArray && !target.type.isMap &&
          !isNumeric(target.type.baseType)) {
        emit("++/-- require a numeric variable or element", upd->line,
             upd->column);
      }
      return target;
    }

    return {};
  }

  InferredType inferCall(CallExpr *call) {
    std::vector<InferredType> argTypes;
    argTypes.reserve(call->arguments.size());
    for (const auto &arg : call->arguments) {
      argTypes.push_back(inferExpr(arg));
    }

    // Procedures shadow builtins, mirroring the interpreter's precedence.
    auto localProcIt = _procedures.find(call->functionName);
    ProcedureDeclPtr proc = nullptr;
    if (localProcIt != _procedures.end()) {
      proc = localProcIt->second;
    } else {
      proc = _interpreter->getProcedure(call->functionName);
    }

    if (!proc) {
      // Struct constructor: Point(field0, field1, ...) — positional.
      if (StructDeclPtr decl = resolveStruct(call->functionName)) {
        if (argTypes.size() != decl->fields.size()) {
          emit("Struct '" + call->functionName + "' expects " +
                   std::to_string(decl->fields.size()) + " field values, got " +
                   std::to_string(argTypes.size()),
               call->line, call->column);
        } else {
          for (size_t i = 0; i < argTypes.size(); ++i) {
            expectConvertible(argTypes[i], decl->fields[i].type, call->line,
                              call->column, "struct field '" +
                                               decl->fields[i].name + "'",
                              call->arguments[i]);
          }
        }
        return {true, TypeInfo::structOf(decl->name)};
      }
      // External functions and builtins can't be fully type-checked.
      return inferBuiltinCall(call, argTypes);
    }

    if (argTypes.size() != proc->parameters.size()) {
      emit("Procedure '" + call->functionName + "' expects " +
               std::to_string(proc->parameters.size()) + " arguments, got " +
               std::to_string(argTypes.size()),
           call->line, call->column);
    } else {
      for (size_t i = 0; i < argTypes.size(); ++i) {
        expectConvertible(argTypes[i], proc->parameters[i].type, call->line,
                          call->column, "procedure call argument",
                          call->arguments[i]);
      }
    }

    return {true, proc->returnType};
  }

  // Return type inference for builtin calls; {false} means "depends on args".
  InferredType inferBuiltinCall(CallExpr *call,
                                const std::vector<InferredType> &argTypes) {
    const std::string &name = call->functionName;

    static const std::unordered_map<std::string, TypeInfo> fixed = {
        {"len", TypeInfo(DataType::INT32)},
        {"push", TypeInfo(DataType::INT32)},
        {"insert", TypeInfo(DataType::INT32)},
        {"clear", TypeInfo(DataType::INT32)},
        {"indexOf", TypeInfo(DataType::INT32)},
        {"randInt", TypeInfo(DataType::INT64)},
        {"srand", TypeInfo(DataType::INT32)},
        {"print", TypeInfo(DataType::INT32)},
        {"println", TypeInfo(DataType::INT32)},
        {"contains", TypeInfo(DataType::BOOL)},
        {"has", TypeInfo(DataType::BOOL)},
        {"remove", TypeInfo(DataType::BOOL)},
        {"size", TypeInfo(DataType::INT32)},
        {"startsWith", TypeInfo(DataType::BOOL)},
        {"endsWith", TypeInfo(DataType::BOOL)},
        {"isArray", TypeInfo(DataType::BOOL)},
        {"isMap", TypeInfo(DataType::BOOL)},
        {"toBool", TypeInfo(DataType::BOOL)},
        {"assert", TypeInfo(DataType::BOOL)},
        {"substr", TypeInfo(DataType::STRING)},
        {"toUpper", TypeInfo(DataType::STRING)},
        {"toLower", TypeInfo(DataType::STRING)},
        {"trim", TypeInfo(DataType::STRING)},
        {"replace", TypeInfo(DataType::STRING)},
        {"join", TypeInfo(DataType::STRING)},
        {"repeat", TypeInfo(DataType::STRING)},
        {"format", TypeInfo(DataType::STRING)},
        {"toString", TypeInfo(DataType::STRING)},
        {"typeof", TypeInfo(DataType::STRING)},
        {"split", TypeInfo(DataType::STRING, true)},
        {"charAt", TypeInfo(DataType::CHAR)},
        {"toChar", TypeInfo(DataType::CHAR)},
        {"toInt", TypeInfo(DataType::INT64)},
        {"parseInt", TypeInfo(DataType::INT64)},
        {"toUInt", TypeInfo(DataType::UINT64)},
        {"toDouble", TypeInfo(DataType::DOUBLE)},
        {"parseDouble", TypeInfo(DataType::DOUBLE)},
        {"pow", TypeInfo(DataType::DOUBLE)},
        {"sqrt", TypeInfo(DataType::DOUBLE)},
        {"floor", TypeInfo(DataType::DOUBLE)},
        {"ceil", TypeInfo(DataType::DOUBLE)},
        {"round", TypeInfo(DataType::DOUBLE)},
        {"trunc", TypeInfo(DataType::DOUBLE)},
        {"fmod", TypeInfo(DataType::DOUBLE)},
        {"sin", TypeInfo(DataType::DOUBLE)},
        {"cos", TypeInfo(DataType::DOUBLE)},
        {"tan", TypeInfo(DataType::DOUBLE)},
        {"asin", TypeInfo(DataType::DOUBLE)},
        {"acos", TypeInfo(DataType::DOUBLE)},
        {"atan", TypeInfo(DataType::DOUBLE)},
        {"atan2", TypeInfo(DataType::DOUBLE)},
        {"exp", TypeInfo(DataType::DOUBLE)},
        {"log", TypeInfo(DataType::DOUBLE)},
        {"log10", TypeInfo(DataType::DOUBLE)},
        {"random", TypeInfo(DataType::DOUBLE)},
        {"pi", TypeInfo(DataType::DOUBLE)},
        {"toFloat", TypeInfo(DataType::FLOAT)},
        {"error", TypeInfo(DataType::VOID)},
    };

    // Argument-dependent return types
    if (name == "keys") {
      if (argTypes.size() >= 1 && argTypes[0].known &&
          argTypes[0].type.isMap) {
        return {true, TypeInfo(argTypes[0].type.keyType, true)};
      }
      return {};
    }
    if (name == "values") {
      if (argTypes.size() >= 1 && argTypes[0].known &&
          argTypes[0].type.isMap) {
        TypeInfo vT = argTypes[0].type.mapValueType
                          ? *argTypes[0].type.mapValueType
                          : TypeInfo(argTypes[0].type.baseType);
        return {true, TypeInfo(vT.baseType, true)};
      }
      return {};
    }
    if (name == "pop" || name == "removeAt") {
      if (argTypes.size() >= 1 && argTypes[0].known &&
          argTypes[0].type.isArray) {
        return {true, TypeInfo(argTypes[0].type.baseType)};
      }
      return {};
    }
    if (name == "abs" || name == "min" || name == "max" || name == "clamp" ||
        name == "reverse") {
      if (!argTypes.empty() && argTypes[0].known) {
        return argTypes[0];
      }
      return {};
    }

    auto it = fixed.find(name);
    if (it != fixed.end()) {
      return {true, it->second};
    }
    return {};
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
      if (!left.type.isArray && !left.type.isStruct &&
          left.type.baseType == DataType::VOID) {
        emit("Logical operator cannot use void left operand", bin->line,
             bin->column);
      }
      if (!right.type.isArray && !right.type.isStruct &&
          right.type.baseType == DataType::VOID) {
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
  ss << filename << ":" << line << ":" << column << ": "
     << (isWarning ? "warning: " : "error: ") << message;
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

// Collect `struct` type names visible to a file: already-loaded decls plus
// those declared in (transitively) imported files. Token-scans only — full
// compilation of imports still happens in compileScript.
void ScriptManager::collectStructNames(
    const std::string &filename, std::unordered_set<std::string> &out,
    std::unordered_set<std::string> &visited) {
  std::string id =
      std::filesystem::absolute(filename).lexically_normal().string();
  if (!visited.insert(id).second) {
    return;
  }
  std::ifstream file(filename);
  if (!file.is_open()) {
    return;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  Lexer lexer(buffer.str(), filename);
  std::vector<Token> tokens;
  try {
    tokens = lexer.tokenize();
  } catch (...) {
    return;
  }
  std::filesystem::path baseDir =
      std::filesystem::path(filename).parent_path();
  for (size_t i = 0; i + 1 < tokens.size(); ++i) {
    if (tokens[i].type == TokenType::STRUCT &&
        tokens[i + 1].type == TokenType::IDENTIFIER) {
      out.insert(tokens[i + 1].lexeme);
    }
    if (tokens[i].type == TokenType::IMPORT &&
        tokens[i + 1].type == TokenType::STRING_LITERAL) {
      std::string resolved =
          std::filesystem::absolute(baseDir / tokens[i + 1].stringValue)
              .lexically_normal()
              .string();
      collectStructNames(resolved, out, visited);
    }
  }
}

std::unordered_set<std::string>
ScriptManager::knownStructNames(const std::string &filename,
                                const std::vector<Token> &tokens) {
  std::unordered_set<std::string> names;
  for (const auto &n : _interpreter->getStructNames()) {
    names.insert(n);
  }
  std::unordered_set<std::string> visited;
  std::filesystem::path baseDir =
      std::filesystem::path(filename).parent_path();
  for (size_t i = 0; i + 1 < tokens.size(); ++i) {
    if (tokens[i].type == TokenType::IMPORT &&
        tokens[i + 1].type == TokenType::STRING_LITERAL) {
      std::string resolved =
          std::filesystem::absolute(baseDir / tokens[i + 1].stringValue)
              .lexically_normal()
              .string();
      collectStructNames(resolved, names, visited);
    }
  }
  return names;
}

bool ScriptManager::compileScript(const std::string &source,
                                  const std::string &filename,
                                  std::vector<CompilationError> &errors,
                                  bool load,
                                  std::vector<std::string> *loadedProcNames) {
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

    if (hasErrors(errors)) {
      return false;
    }

    // Parse
    Parser parser(tokens, filename, knownStructNames(filename, tokens));
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

    if (hasErrors(errors)) {
      return false;
    }

    // Load imports before validating this file so its procedure calls resolve.
    if (load) {
      std::string selfId =
          std::filesystem::absolute(filename).lexically_normal().string();
      _loadingFiles.insert(selfId);

      std::filesystem::path baseDir =
          std::filesystem::path(filename).parent_path();
      for (const auto &imp : script->imports) {
        std::string resolved = std::filesystem::absolute(baseDir / imp.first)
                                   .lexically_normal()
                                   .string();
        if (_loadedFiles.count(resolved) || _loadingFiles.count(resolved)) {
          continue; // already loaded, or circular import in progress
        }
        std::ifstream file(resolved);
        if (!file.is_open()) {
          errors.emplace_back("Cannot open import '" + imp.first + "'",
                              filename, "", imp.second, 0);
          _loadingFiles.erase(selfId);
          return false;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        if (!compileScript(buffer.str(), resolved, errors, true)) {
          errors.emplace_back("Failed to load import '" + imp.first + "'",
                              filename, "", imp.second, 0);
          _loadingFiles.erase(selfId);
          return false;
        }
      }
      _loadingFiles.erase(selfId);
      _loadedFiles.insert(selfId);
    }

    // Semantic validation (type compatibility and basic control-flow checks)
    SemanticValidator validator(filename, _interpreter.get(), errors);
    validator.validate(script);
    if (hasErrors(errors)) {
      return false;
    }

    // Load into interpreter if requested
    if (load) {
      _interpreter->loadScript(script);

      // Track which file each procedure came from
      for (const auto &proc : script->procedures) {
        _procedureFiles[proc->name] = filename;
        if (loadedProcNames) {
          loadedProcNames->push_back(proc->name);
        }
      }
      for (const auto &s : script->structs) {
        _structFiles[s->name] = filename;
      }
    }

    return true;

  } catch (const std::exception &e) {
    errors.push_back(CompilationError(e.what(), filename, "", 0, 0));
    return false;
  }
}

bool ScriptManager::reloadScriptFile(const std::string &filename,
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

  std::string selfId =
      std::filesystem::absolute(filename).lexically_normal().string();

  // Remove this file's current procedures up front so stale declarations
  // can't satisfy validation of the new version; keep copies for rollback.
  std::vector<std::pair<std::string, std::string>> previous; // proc -> file
  for (const auto &kv : _procedureFiles) {
    if (std::filesystem::absolute(kv.second).lexically_normal().string() ==
        selfId) {
      previous.push_back(kv);
    }
  }
  std::vector<std::pair<std::string, ProcedureDeclPtr>> saved;
  for (const auto &kv : previous) {
    ProcedureDeclPtr old = _interpreter->removeProcedure(kv.first);
    if (old) {
      saved.emplace_back(kv.first, old);
    }
  }

  // Same for the file's structs.
  std::vector<std::pair<std::string, std::string>> prevStructs;
  for (const auto &kv : _structFiles) {
    if (std::filesystem::absolute(kv.second).lexically_normal().string() ==
        selfId) {
      prevStructs.push_back(kv);
    }
  }
  std::vector<std::pair<std::string, StructDeclPtr>> savedStructs;
  for (const auto &kv : prevStructs) {
    StructDeclPtr old = _interpreter->removeStruct(kv.first);
    if (old) {
      savedStructs.emplace_back(kv.first, old);
    }
  }

  std::vector<std::string> newProcs;
  if (!compileScript(source, filename, errors, true, &newProcs)) {
    // Roll back: restore the old declarations.
    for (auto &kv : saved) {
      _interpreter->addProcedure(kv.second, kv.first);
      _procedureFiles[kv.first] = filename;
    }
    for (auto &kv : savedStructs) {
      _interpreter->addStruct(kv.second, kv.first);
      _structFiles[kv.first] = filename;
    }
    return false;
  }

  // Unload procedures that no longer exist in the new version.
  std::unordered_set<std::string> keep(newProcs.begin(), newProcs.end());
  for (const auto &kv : previous) {
    if (!keep.count(kv.first)) {
      _procedureFiles.erase(kv.first);
    }
  }
  for (const auto &kv : prevStructs) {
    if (!_interpreter->hasStruct(kv.first)) {
      _structFiles.erase(kv.first);
    }
  }
  return true;
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
    if (!e.trace.empty()) {
      ss << "\nStack trace (most recent call last):";
      for (auto it = e.trace.rbegin(); it != e.trace.rend(); ++it) {
        ss << "\n  " << it->procedure;
        if (!it->file.empty())
          ss << " (" << it->file << ":" << it->line << ")";
        else if (it->line > 0)
          ss << " (line " << it->line << ")";
        else
          ss << " (host)";
      }
    }
    errorMessage = ss.str();
    return false;
  } catch (const std::exception &e) {
    errorMessage = std::string("Runtime error: ") + e.what();
    return false;
  }
}

bool ScriptManager::evaluateSnippet(const std::string &source,
                                    const std::string &name,
                                    Value &returnValue,
                                    std::string &errorMessage) {
  errorMessage.clear();

  auto fail = [&](const std::vector<CompilationError> &errors) {
    std::stringstream ss;
    for (const auto &e : errors) {
      ss << e.toString() << "\n";
    }
    errorMessage = ss.str();
    return false;
  };

  std::vector<CompilationError> errors;
  try {
    Lexer lexer(source, name);
    std::vector<Token> tokens = lexer.tokenize();
    for (const auto &token : tokens) {
      if (token.type == TokenType::UNKNOWN) {
        errors.emplace_back("Unexpected character: '" + token.lexeme + "'",
                            name, "", token.line, token.column);
      }
    }
    if (hasErrors(errors)) {
      return fail(errors);
    }

    std::unordered_set<std::string> knownStructs;
    for (const auto &n : _interpreter->getStructNames()) {
      knownStructs.insert(n);
    }
    Parser parser(tokens, name, knownStructs);
    std::vector<StmtPtr> statements;
    try {
      statements = parser.parseStatements();
    } catch (const ParseError &e) {
      errors.emplace_back(e.what(), name, e.procedureName, e.line, e.column);
      return fail(errors);
    }
    for (const auto &pe : parser.getErrors()) {
      errors.emplace_back(pe.what(), name, pe.procedureName, pe.line,
                          pe.column);
    }
    if (hasErrors(errors)) {
      return fail(errors);
    }

    SemanticValidator validator(name, _interpreter.get(), errors);
    validator.validateStatements(statements, _replGlobalTypes);
    if (hasErrors(errors)) {
      return fail(errors);
    }

    try {
      returnValue = _interpreter->executeStatements(statements);
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
      if (!e.trace.empty()) {
        ss << "\nStack trace (most recent call last):";
        for (auto it = e.trace.rbegin(); it != e.trace.rend(); ++it) {
          ss << "\n  " << it->procedure;
          if (!it->file.empty()) {
            ss << " (" << it->file << ":" << it->line << ")";
          } else if (it->line > 0) {
            ss << " (line " << it->line << ")";
          }
        }
      }
      errorMessage = ss.str();
      return false;
    } catch (const std::exception &e) {
      errorMessage = std::string("Runtime error: ") + e.what();
      return false;
    }

    // Persist top-level declarations so later snippets can reference them
    for (const auto &stmt : statements) {
      if (auto *vd = dynamic_cast<VarDeclStmt *>(stmt.get())) {
        _replGlobalTypes[vd->name] = {vd->type, vd->isConst};
      }
    }
    return true;
  } catch (const std::exception &e) {
    errorMessage = e.what();
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
  _structFiles.clear();
  _loadedFiles.clear();
  _loadingFiles.clear();
  _replGlobalTypes.clear();
}

void ScriptManager::setExecutionLimits(size_t maxCallDepth, size_t maxSteps) {
  _interpreter->setExecutionLimits(maxCallDepth, maxSteps);
}

void ScriptManager::clearExecutionLimits() { _interpreter->clearExecutionLimits(); }

void ScriptManager::setMemoryLimits(size_t maxArraySize,
                                    size_t maxStringLength,
                                    size_t maxAllocations) {
  _interpreter->setMemoryLimits(maxArraySize, maxStringLength, maxAllocations);
}

void ScriptManager::clearMemoryLimits() { _interpreter->clearMemoryLimits(); }

void ScriptManager::setDebugHook(Interpreter::DebugHook cb) {
  _interpreter->setDebugHook(std::move(cb));
}

void ScriptManager::setOutputCallback(Interpreter::OutputCallback cb) {
  _interpreter->setOutputCallback(std::move(cb));
}

} // namespace Script
