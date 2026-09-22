#include "VM.h"
#include "Builtins.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <utility>

namespace Script {

// ===========================================================================
// BytecodeCompiler — lowers validated AST bodies to bytecode.
//
// Lexical variables resolve to frame slots at compile time; names that cannot
// be resolved in the current function emit LOAD_NAME/STORE_NAME, which the VM
// resolves through the dynamic environment chain (captures, caller frames,
// globals, external variables, procedure references) exactly like the
// tree-walking interpreter's Environment chain.
// ===========================================================================

int32_t BytecodeCompiler::emit(Op op, int32_t a, int32_t b, int32_t c,
                               int line, int col) {
  Instruction in;
  in.op = op;
  in.a = a;
  in.b = b;
  in.c = c;
  in.line = line;
  in.column = col;
  _chunk.code.push_back(in);
  return static_cast<int32_t>(_chunk.code.size() - 1);
}

int32_t BytecodeCompiler::addConst(const Value &v) {
  _chunk.constants.push_back(v);
  return static_cast<int32_t>(_chunk.constants.size() - 1);
}

int32_t BytecodeCompiler::addName(const std::string &n) {
  for (size_t i = 0; i < _chunk.names.size(); ++i) {
    if (_chunk.names[i] == n) {
      return static_cast<int32_t>(i);
    }
  }
  _chunk.names.push_back(n);
  return static_cast<int32_t>(_chunk.names.size() - 1);
}

int32_t BytecodeCompiler::addType(const TypeInfo &t) {
  _chunk.types.push_back(t);
  return static_cast<int32_t>(_chunk.types.size() - 1);
}

int32_t BytecodeCompiler::addFunction(const VMFunctionPtr &f) {
  _chunk.functions.push_back(f);
  return static_cast<int32_t>(_chunk.functions.size() - 1);
}

int32_t BytecodeCompiler::addHandler(const HandlerInfo &h) {
  _chunk.handlers.push_back(h);
  return static_cast<int32_t>(_chunk.handlers.size() - 1);
}

void BytecodeCompiler::patchJump(int32_t instrIdx, int32_t target) {
  _chunk.code[instrIdx].a = target;
}

int32_t BytecodeCompiler::resolveLocal(const std::string &name) {
  if (_globalMode) {
    return -1;
  }
  for (size_t i = _scopes.size(); i-- > 0;) {
    auto it = _scopes[i].vars.find(name);
    if (it != _scopes[i].vars.end()) {
      return it->second;
    }
  }
  return -1;
}

int32_t BytecodeCompiler::allocSlot() {
  int32_t s = _nextSlot++;
  if (_nextSlot > _chunk.numSlots) {
    _chunk.numSlots = static_cast<uint16_t>(_nextSlot);
  }
  return s;
}

void BytecodeCompiler::pushScope() {
  _scopes.push_back(Scope{});
  _scopes.back().declMark = 0;
  emit(Op::ENTER_SCOPE, 0, 0, 0, 0, 0);
}

void BytecodeCompiler::popScope() {
  if (_scopes.empty()) {
    return;
  }
  // Free the scope's slots for reuse and drop the name bindings.
  int32_t minSlot = _nextSlot;
  for (const auto &kv : _scopes.back().vars) {
    minSlot = std::min(minSlot, kv.second);
  }
  if (!_scopes.back().vars.empty()) {
    _nextSlot = minSlot;
  }
  _scopes.pop_back();
  emit(Op::LEAVE_SCOPE, 0, 0, 0, 0, 0);
}

void BytecodeCompiler::declareVar(const std::string &name, int32_t slot,
                                  int line, int col) {
  if (_globalMode) {
    emit(Op::DEFINE_NAME, addName(name), 0, 0, line, col);
    return;
  }
  if (_scopes.empty()) {
    pushScope();
  }
  _scopes.back().vars[name] = slot;
  emit(Op::DECL_SLOT, slot, addName(name), 0, line, col);
}

// --- Statements ------------------------------------------------------------

void BytecodeCompiler::compileStmt(StmtPtr stmt) {
  if (!_ok || !stmt) {
    return;
  }
  bool isBlock = dynamic_cast<BlockStmt *>(stmt.get()) != nullptr;
  emit(isBlock ? Op::STEP : Op::STEP_DBG, 0, 0, 0, stmt->line, stmt->column);

  if (auto *s = dynamic_cast<ExpressionStmt *>(stmt.get())) {
    compileExpr(s->expression);
    emit(Op::POP, 0, 0, 0, stmt->line, stmt->column);
  } else if (auto *s = dynamic_cast<VarDeclStmt *>(stmt.get())) {
    compileVarDecl(s);
  } else if (auto *s = dynamic_cast<AssignStmt *>(stmt.get())) {
    compileAssign(s);
  } else if (auto *s = dynamic_cast<BlockStmt *>(stmt.get())) {
    compileBlock(s, true);
  } else if (auto *s = dynamic_cast<IfStmt *>(stmt.get())) {
    compileIf(s);
  } else if (auto *s = dynamic_cast<WhileStmt *>(stmt.get())) {
    compileWhile(s);
  } else if (auto *s = dynamic_cast<ForStmt *>(stmt.get())) {
    compileFor(s);
  } else if (auto *s = dynamic_cast<DoWhileStmt *>(stmt.get())) {
    compileDoWhile(s);
  } else if (auto *s = dynamic_cast<SwitchStmt *>(stmt.get())) {
    compileSwitch(s);
  } else if (auto *s = dynamic_cast<ReturnStmt *>(stmt.get())) {
    if (s->value) {
      compileExpr(s->value);
    } else {
      emit(Op::CONST, addConst(static_cast<int32_t>(0)), 0, 0, stmt->line,
           stmt->column);
    }
    emit(Op::RETURN, 0, 0, 0, stmt->line, stmt->column);
  } else if (dynamic_cast<BreakStmt *>(stmt.get())) {
    if (_loops.empty()) {
      _ok = false;
      return;
    }
    int32_t site =
        emit(Op::BREAK, 0, static_cast<int32_t>(_loops.back().scopeDepth), 0,
             stmt->line, stmt->column);
    _loops.back().breakSites.push_back(site);
  } else if (dynamic_cast<ContinueStmt *>(stmt.get())) {
    // Continue binds to the innermost real loop (not a switch).
    for (size_t i = _loops.size(); i-- > 0;) {
      if (_loops[i].isSwitch) {
        continue;
      }
      int32_t site =
          emit(Op::CONTINUE, 0, static_cast<int32_t>(_loops[i].scopeDepth), 0,
               stmt->line, stmt->column);
      _loops[i].contSites.push_back(site);
      return;
    }
    _ok = false;
  } else if (auto *s = dynamic_cast<IndexAssignStmt *>(stmt.get())) {
    compileIndexAssign(s);
  } else if (auto *s = dynamic_cast<MemberAssignStmt *>(stmt.get())) {
    compileMemberAssign(s);
  } else if (auto *s = dynamic_cast<ForEachStmt *>(stmt.get())) {
    compileForEach(s);
  } else if (auto *s = dynamic_cast<ThrowStmt *>(stmt.get())) {
    compileExpr(s->value);
    emit(Op::THROW, 0, 0, 0, stmt->line, stmt->column);
  } else if (auto *s = dynamic_cast<TryCatchStmt *>(stmt.get())) {
    compileTryCatch(s);
  } else {
    _ok = false;
  }
}

void BytecodeCompiler::compileBlock(BlockStmt *stmt, bool scoped) {
  if (scoped) {
    pushScope();
  }
  for (auto &s : stmt->statements) {
    compileStmt(s);
  }
  if (scoped) {
    popScope();
  }
}

void BytecodeCompiler::compileVarDecl(VarDeclStmt *stmt) {
  int32_t slot = _globalMode ? -1 : allocSlot();

  if (stmt->type.isAuto) {
    if (!stmt->initializer) {
      emit(Op::ERROR,
           addConst(std::string("'auto' variable requires an initializer")), 0,
           0, stmt->line, stmt->column);
      return;
    }
    compileExpr(stmt->initializer);
  } else if (stmt->initializer) {
    compileExpr(stmt->initializer);
    emit(Op::CONVERT, addType(stmt->type), 0, 0, stmt->line, stmt->column);
  } else {
    emit(Op::DEFAULT_VALUE, addType(stmt->type), 0, 0, stmt->line,
         stmt->column);
  }

  if (_globalMode) {
    emit(Op::DEFINE_NAME, addName(stmt->name), 0, 0, stmt->line, stmt->column);
  } else {
    declareVar(stmt->name, slot, stmt->line, stmt->column);
  }
}

void BytecodeCompiler::compileAssign(AssignStmt *stmt) {
  int32_t slot = resolveLocal(stmt->variableName);
  int32_t nameIdx = addName(stmt->variableName);
  int32_t opIdx = static_cast<int32_t>(stmt->op);

  if (stmt->op == AssignStmt::Operator::ASSIGN) {
    compileExpr(stmt->value);
  } else {
    // RHS evaluates before the LHS is read (mirrors executeAssign, which
    // evaluates `value` first — observable when the target is a `this` field
    // or external variable that the RHS mutates).
    compileExpr(stmt->value);
    if (slot >= 0) {
      emit(Op::LOAD_SLOT, slot, 0, 0, stmt->line, stmt->column);
    } else {
      emit(Op::LOAD_NAME, nameIdx, 0, 0, stmt->line, stmt->column);
    }
    emit(Op::ASSIGN_OP, opIdx, 0, 0, stmt->line, stmt->column);
  }

  if (slot >= 0) {
    emit(Op::STORE_SLOT, slot, nameIdx, 0, stmt->line, stmt->column);
  } else {
    emit(Op::STORE_NAME, nameIdx, 0, 0, stmt->line, stmt->column);
  }
}

void BytecodeCompiler::compileIndexAssign(IndexAssignStmt *stmt) {
  compileExpr(stmt->arrayExpr);
  compileExpr(stmt->indexExpr);
  compileExpr(stmt->value);
  emit(Op::INDEX_SET, static_cast<int32_t>(stmt->op), 0, 0, stmt->line,
       stmt->column);
}

void BytecodeCompiler::compileMemberAssign(MemberAssignStmt *stmt) {
  compileExpr(stmt->object);
  compileExpr(stmt->value);
  emit(Op::MEMBER_SET, addName(stmt->member), static_cast<int32_t>(stmt->op), 0,
       stmt->line, stmt->column);
}

void BytecodeCompiler::compileIf(IfStmt *stmt) {
  compileExpr(stmt->condition);
  int32_t jf = emit(Op::JUMP_FALSE, 0, 0, 0, stmt->line, stmt->column);
  compileStmt(stmt->thenBranch);
  if (stmt->elseBranch) {
    int32_t j = emit(Op::JUMP, 0, 0, 0, stmt->line, stmt->column);
    patchJump(jf, static_cast<int32_t>(_chunk.code.size()));
    compileStmt(stmt->elseBranch);
    patchJump(j, static_cast<int32_t>(_chunk.code.size()));
  } else {
    patchJump(jf, static_cast<int32_t>(_chunk.code.size()));
  }
}

void BytecodeCompiler::compileWhile(WhileStmt *stmt) {
  size_t scopeDepth = _scopes.size();
  _loops.push_back(LoopCtx{});
  _loops.back().scopeDepth = scopeDepth;

  int32_t condPC = static_cast<int32_t>(_chunk.code.size());
  compileExpr(stmt->condition);
  int32_t jf = emit(Op::JUMP_FALSE, 0, 0, 0, stmt->line, stmt->column);
  compileStmt(stmt->body);
  emit(Op::JUMP, condPC, 0, 0, stmt->line, stmt->column);
  int32_t end = static_cast<int32_t>(_chunk.code.size());
  patchJump(jf, end);
  for (int32_t s : _loops.back().breakSites) {
    patchJump(s, end);
  }
  for (int32_t s : _loops.back().contSites) {
    patchJump(s, condPC);
  }
  _loops.pop_back();
}

void BytecodeCompiler::compileFor(ForStmt *stmt) {
  pushScope();
  size_t breakDepth = _scopes.size() - 1; // break exits the for-scope
  _loops.push_back(LoopCtx{});
  _loops.back().scopeDepth = breakDepth;

  if (stmt->initializer) {
    compileStmt(stmt->initializer);
  }
  int32_t condPC = static_cast<int32_t>(_chunk.code.size());
  int32_t jf = -1;
  if (stmt->condition) {
    compileExpr(stmt->condition);
    jf = emit(Op::JUMP_FALSE, 0, 0, 0, stmt->line, stmt->column);
  }
  compileStmt(stmt->body);
  int32_t contPC = static_cast<int32_t>(_chunk.code.size());
  if (stmt->increment) {
    compileStmt(stmt->increment);
  }
  emit(Op::JUMP, condPC, 0, 0, stmt->line, stmt->column);
  int32_t end = static_cast<int32_t>(_chunk.code.size());
  if (jf >= 0) {
    patchJump(jf, end);
  }
  // `continue` skips to the increment when present, else the condition.
  int32_t contTarget = stmt->increment ? contPC : condPC;
  for (int32_t s : _loops.back().breakSites) {
    patchJump(s, end);
  }
  for (int32_t s : _loops.back().contSites) {
    patchJump(s, contTarget);
  }
  _loops.pop_back();
  popScope();
}

void BytecodeCompiler::compileDoWhile(DoWhileStmt *stmt) {
  _loops.push_back(LoopCtx{});
  _loops.back().scopeDepth = _scopes.size();

  int32_t bodyPC = static_cast<int32_t>(_chunk.code.size());
  compileStmt(stmt->body);
  int32_t condPC = static_cast<int32_t>(_chunk.code.size());
  compileExpr(stmt->condition);
  emit(Op::JUMP_TRUE, bodyPC, 0, 0, stmt->line, stmt->column);
  int32_t end = static_cast<int32_t>(_chunk.code.size());
  for (int32_t s : _loops.back().breakSites) {
    patchJump(s, end);
  }
  for (int32_t s : _loops.back().contSites) {
    patchJump(s, condPC);
  }
  _loops.pop_back();
}

void BytecodeCompiler::compileSwitch(SwitchStmt *stmt) {
  _loops.push_back(LoopCtx{});
  _loops.back().scopeDepth = _scopes.size();
  _loops.back().isSwitch = true;

  compileExpr(stmt->expression);
  int32_t valSlot = allocSlot();
  emit(Op::STORE_SLOT, valSlot, -1, 0, stmt->line, stmt->column);

  // Compare chain: first match jumps to its body; default auto-matches.
  std::vector<int32_t> jumpSites; // parallel to cases
  for (size_t i = 0; i < stmt->cases.size(); ++i) {
    const auto &c = stmt->cases[i];
    if (c.isDefault) {
      jumpSites.push_back(
          emit(Op::JUMP, 0, 0, 0, stmt->line, stmt->column)); // -> body i
    } else {
      emit(Op::LOAD_SLOT, valSlot, 0, 0, c.matchExpr->line,
           c.matchExpr->column);
      compileExpr(c.matchExpr);
      emit(Op::EQ, 0, 0, 0, c.matchExpr->line, c.matchExpr->column);
      jumpSites.push_back(
          emit(Op::JUMP_TRUE, 0, 0, 0, c.matchExpr->line, c.matchExpr->column));
    }
  }
  int32_t jEnd =
      emit(Op::JUMP, 0, 0, 0, stmt->line, stmt->column); // no case matched

  // Bodies laid out sequentially; fallthrough is natural.
  for (size_t i = 0; i < stmt->cases.size(); ++i) {
    patchJump(jumpSites[i], static_cast<int32_t>(_chunk.code.size()));
    for (auto &s : stmt->cases[i].statements) {
      compileStmt(s);
    }
  }
  int32_t end = static_cast<int32_t>(_chunk.code.size());
  patchJump(jEnd, end);
  for (int32_t s : _loops.back().breakSites) {
    patchJump(s, end);
  }
  _loops.pop_back();
}

void BytecodeCompiler::compileForEach(ForEachStmt *stmt) {
  pushScope();
  _loops.push_back(LoopCtx{});
  _loops.back().scopeDepth = _scopes.size() - 1; // exits the for-each scope

  compileExpr(stmt->iterable);
  int32_t iterIdx = _iterCount++;
  emit(Op::FOR_INIT, iterIdx, 0, 0, stmt->line, stmt->column);

  int32_t varSlot = -1;
  if (!_globalMode) {
    varSlot = allocSlot();
    _scopes.back().vars[stmt->varName] = varSlot;
    // Register the binding (placeholder value; FOR_NEXT stores each element).
    emit(Op::CONST, addConst(static_cast<int32_t>(0)), 0, 0, stmt->line,
         stmt->column);
    emit(Op::DECL_SLOT, varSlot, addName(stmt->varName), 0, stmt->line,
         stmt->column);
  }

  int32_t loopPC = static_cast<int32_t>(_chunk.code.size());
  int32_t next = emit(Op::FOR_NEXT, iterIdx, 0, 0, stmt->line, stmt->column);
  emit(Op::CONVERT, addType(stmt->elemType), 0, 0, stmt->line, stmt->column);
  if (_globalMode) {
    emit(Op::DEFINE_NAME, addName(stmt->varName), 0, 0, stmt->line,
         stmt->column);
  } else {
    emit(Op::STORE_SLOT, varSlot, addName(stmt->varName), 0, stmt->line,
         stmt->column);
  }
  compileStmt(stmt->body);
  emit(Op::JUMP, loopPC, 0, 0, stmt->line, stmt->column);
  int32_t end = static_cast<int32_t>(_chunk.code.size());
  _chunk.code[next].b = end;
  for (int32_t s : _loops.back().breakSites) {
    patchJump(s, end);
  }
  for (int32_t s : _loops.back().contSites) {
    patchJump(s, loopPC);
  }
  _loops.pop_back();
  popScope();
}

void BytecodeCompiler::compileTryCatch(TryCatchStmt *stmt) {
  HandlerInfo h; // stack/scope depths are recorded at PUSH_HANDLER time
  int32_t hIdx = addHandler(h);

  emit(Op::PUSH_HANDLER, hIdx, 0, 0, stmt->line, stmt->column);
  _chunk.handlers[hIdx].tryBegin =
      static_cast<uint32_t>(_chunk.code.size());

  compileStmt(stmt->tryBlock);
  emit(Op::ENTER_FINALLY, hIdx, 0, 0, stmt->line, stmt->column);

  if (stmt->catchBlock) {
    _chunk.handlers[hIdx].catchPC =
        static_cast<uint32_t>(_chunk.code.size());
    pushScope();
    if (!stmt->catchVar.empty()) {
      int32_t slot = allocSlot();
      _chunk.handlers[hIdx].catchSlot = slot;
      _chunk.handlers[hIdx].catchNameIdx = addName(stmt->catchVar);
      if (!stmt->catchType.isAuto) {
        _chunk.handlers[hIdx].catchTypeIdx = addType(stmt->catchType);
      }
      _scopes.back().vars[stmt->catchVar] = slot;
      // The VM binds the slot directly at catch dispatch; DECL keeps the
      // dynamic view consistent.
      emit(Op::DECL_SLOT, slot, addName(stmt->catchVar), 0, stmt->line,
           stmt->column);
    }
    compileStmt(stmt->catchBlock);
    // ENTER_FINALLY rolls scopes back to the handler's depth.
    emit(Op::ENTER_FINALLY, hIdx, 0, 0, stmt->line, stmt->column);
    // The catch scope is exited at runtime via ENTER_FINALLY; mirror the
    // compile-time bookkeeping without emitting LEAVE_SCOPE.
    int32_t minSlot = _nextSlot;
    for (const auto &kv : _scopes.back().vars) {
      minSlot = std::min(minSlot, kv.second);
    }
    if (!_scopes.back().vars.empty()) {
      _nextSlot = minSlot;
    }
    _scopes.pop_back();
  }

  if (stmt->finallyBlock) {
    _chunk.handlers[hIdx].finallyPC =
        static_cast<uint32_t>(_chunk.code.size());
    compileStmt(stmt->finallyBlock);
    emit(Op::FINALLY_END, hIdx, 0, 0, stmt->line, stmt->column);
  }
  _chunk.handlers[hIdx].endPC = static_cast<uint32_t>(_chunk.code.size());
  // Region covered by the handler: try body + catch body, up to finally start.
  _chunk.handlers[hIdx].regionEnd =
      stmt->finallyBlock ? _chunk.handlers[hIdx].finallyPC
                         : _chunk.handlers[hIdx].endPC;
  _chunk.handlers[hIdx].line = stmt->line;
  _chunk.handlers[hIdx].column = stmt->column;
}

void BytecodeCompiler::compileUpdateTarget(ExprPtr target, bool inc,
                                           bool prefix, int line, int col) {
  int32_t flags = (inc ? 1 : 0) | (prefix ? 2 : 0);
  if (auto *var = dynamic_cast<VariableExpr *>(target.get())) {
    int32_t slot = resolveLocal(var->name);
    if (slot >= 0) {
      emit(Op::UPDATE_SLOT, slot, flags, addName(var->name), line, col);
    } else {
      emit(Op::UPDATE_NAME, addName(var->name), flags, 0, line, col);
    }
    return;
  }
  if (auto *mem = dynamic_cast<MemberExpr *>(target.get())) {
    compileExpr(mem->object);
    emit(Op::UPDATE_MEMBER, addName(mem->member), flags, 0, line, col);
    return;
  }
  if (auto *idx = dynamic_cast<IndexExpr *>(target.get())) {
    compileExpr(idx->arrayExpr);
    compileExpr(idx->indexExpr);
    emit(Op::UPDATE_INDEX, 0, flags, 0, line, col);
    return;
  }
  _ok = false;
}

void BytecodeCompiler::compileCall(CallExpr *expr) {
  if (expr->calleeExpr) {
    compileExpr(expr->calleeExpr);
    for (auto &a : expr->arguments) {
      compileExpr(a);
    }
    emit(Op::CALL_VALUE, 0, static_cast<int32_t>(expr->arguments.size()), 0,
         expr->line, expr->column);
    return;
  }
  int32_t siteIdx = static_cast<int32_t>(_chunk.callSites.size());
  _chunk.callSites.push_back(CallSite{});
  int32_t nameIdx = addName(expr->functionName);
  emit(Op::CALL_RESOLVE, nameIdx, 0, siteIdx, expr->line, expr->column);
  for (auto &a : expr->arguments) {
    compileExpr(a);
  }
  emit(Op::CALL, nameIdx, static_cast<int32_t>(expr->arguments.size()),
       siteIdx, expr->line, expr->column);
}

VMFunctionPtr BytecodeCompiler::compileLambda(LambdaExpr *expr) {
  TypeInfo ret =
      expr->hasRetType ? expr->declaredRetType : TypeInfo::autoType();
  VMFunctionPtr fn =
      compileFunction("<lambda>", expr->parameters, expr->body, ret, "");
  if (fn) {
    fn->body = expr->body;
    fn->hasDeclaredRetType = expr->hasRetType;
  }
  return fn;
}

// --- Expressions -----------------------------------------------------------

void BytecodeCompiler::compileExpr(ExprPtr expr) {
  if (!_ok || !expr) {
    return;
  }
  int line = expr->line, col = expr->column;

  if (auto *e = dynamic_cast<LiteralExpr *>(expr.get())) {
    emit(Op::CONST, addConst(e->value), 0, 0, line, col);
    return;
  }
  if (auto *e = dynamic_cast<VariableExpr *>(expr.get())) {
    int32_t slot = resolveLocal(e->name);
    if (slot >= 0) {
      emit(Op::LOAD_SLOT, slot, 0, 0, line, col);
    } else {
      emit(Op::LOAD_NAME, addName(e->name), 0, 0, line, col);
    }
    return;
  }
  if (auto *e = dynamic_cast<BinaryExpr *>(expr.get())) {
    using BO = BinaryExpr::Operator;
    if (e->op == BO::LOGICAL_AND) {
      compileExpr(e->left);
      int32_t jf = emit(Op::JUMP_FALSE, 0, 0, 0, line, col);
      compileExpr(e->right);
      emit(Op::TOBOOL, 0, 0, 0, line, col);
      int32_t j = emit(Op::JUMP, 0, 0, 0, line, col);
      patchJump(jf, static_cast<int32_t>(_chunk.code.size()));
      emit(Op::CONST, addConst(false), 0, 0, line, col);
      patchJump(j, static_cast<int32_t>(_chunk.code.size()));
      return;
    }
    if (e->op == BO::LOGICAL_OR) {
      compileExpr(e->left);
      int32_t jt = emit(Op::JUMP_TRUE, 0, 0, 0, line, col);
      compileExpr(e->right);
      emit(Op::TOBOOL, 0, 0, 0, line, col);
      int32_t j = emit(Op::JUMP, 0, 0, 0, line, col);
      patchJump(jt, static_cast<int32_t>(_chunk.code.size()));
      emit(Op::CONST, addConst(true), 0, 0, line, col);
      patchJump(j, static_cast<int32_t>(_chunk.code.size()));
      return;
    }
    compileExpr(e->left);
    compileExpr(e->right);
    static const Op opMap[] = {
        Op::ADD,  Op::SUB, Op::MUL,  Op::DIV, Op::MOD, Op::EQ,  Op::NE,
        Op::LT,   Op::GT,  Op::LE,   Op::GE,  Op::NOP, Op::NOP, Op::BAND,
        Op::BOR,  Op::BXOR, Op::SHL, Op::SHR};
    Op op = opMap[static_cast<int>(e->op)];
    if (op == Op::NOP) {
      _ok = false;
      return;
    }
    emit(op, 0, 0, 0, line, col);
    return;
  }
  if (auto *e = dynamic_cast<UnaryExpr *>(expr.get())) {
    compileExpr(e->operand);
    Op op = e->op == UnaryExpr::Operator::NEGATE     ? Op::NEG
            : e->op == UnaryExpr::Operator::LOGICAL_NOT ? Op::NOT
                                                      : Op::BNOT;
    emit(op, 0, 0, 0, line, col);
    return;
  }
  if (auto *e = dynamic_cast<UpdateExpr *>(expr.get())) {
    compileUpdateTarget(e->target, e->increment, e->prefix, line, col);
    return;
  }
  if (auto *e = dynamic_cast<CallExpr *>(expr.get())) {
    compileCall(e);
    return;
  }
  if (auto *e = dynamic_cast<ConditionalExpr *>(expr.get())) {
    compileExpr(e->condition);
    int32_t jf = emit(Op::JUMP_FALSE, 0, 0, 0, line, col);
    compileExpr(e->thenExpr);
    int32_t j = emit(Op::JUMP, 0, 0, 0, line, col);
    patchJump(jf, static_cast<int32_t>(_chunk.code.size()));
    compileExpr(e->elseExpr);
    patchJump(j, static_cast<int32_t>(_chunk.code.size()));
    return;
  }
  if (auto *e = dynamic_cast<ArrayLiteralExpr *>(expr.get())) {
    for (auto &el : e->elements) {
      compileExpr(el);
    }
    emit(Op::MAKE_ARRAY, static_cast<int32_t>(e->elements.size()), 0, 0, line,
         col);
    return;
  }
  if (auto *e = dynamic_cast<MapLiteralExpr *>(expr.get())) {
    for (auto &kv : e->entries) {
      compileExpr(kv.first);
      compileExpr(kv.second);
    }
    emit(Op::MAKE_MAP, static_cast<int32_t>(e->entries.size()), 0, 0, line,
         col);
    return;
  }
  if (auto *e = dynamic_cast<IndexExpr *>(expr.get())) {
    compileExpr(e->arrayExpr);
    if (e->isSlice) {
      int32_t flags = 0;
      if (e->indexExpr) {
        compileExpr(e->indexExpr);
        flags |= 1;
      }
      if (e->endIndex) {
        compileExpr(e->endIndex);
        flags |= 2;
      }
      emit(Op::SLICE, flags, 0, 0, line, col);
    } else {
      compileExpr(e->indexExpr);
      emit(Op::INDEX, 0, 0, 0, line, col);
    }
    return;
  }
  if (auto *e = dynamic_cast<MemberExpr *>(expr.get())) {
    compileExpr(e->object);
    emit(Op::MEMBER_GET, addName(e->member), 0, 0, line, col);
    return;
  }
  if (auto *e = dynamic_cast<LambdaExpr *>(expr.get())) {
    VMFunctionPtr fn = compileLambda(e);
    if (!fn) {
      _ok = false;
      return;
    }
    emit(Op::MAKE_LAMBDA, addFunction(fn), 0, 0, line, col);
    return;
  }
  if (auto *e = dynamic_cast<EnumMemberExpr *>(expr.get())) {
    emit(Op::ENUM_MEMBER, addName(e->enumName), addName(e->memberName), 0,
         line, col);
    return;
  }
  if (auto *e = dynamic_cast<InterpolatedStringExpr *>(expr.get())) {
    for (const auto &p : e->parts) {
      if (p.isExpr) {
        compileExpr(p.expr);
      } else {
        emit(Op::CONST, addConst(p.text), 0, 0, line, col);
      }
    }
    emit(Op::CONCAT_N, static_cast<int32_t>(e->parts.size()), 0, 0, line,
         col);
    return;
  }

  _ok = false;
}

// --- Entry points ------------------------------------------------------------

VMFunctionPtr
BytecodeCompiler::compileFunction(const std::string &name,
                                  const std::vector<Parameter> &params,
                                  StmtPtr body, const TypeInfo &retType,
                                  const std::string &file) {
  BytecodeCompiler c;
  auto fn = std::make_shared<VMFunction>();
  fn->name = name;
  fn->file = file;
  fn->params = params;
  fn->retType = retType;
  fn->defaultEntries.assign(params.size(), UINT32_MAX);

  // Function scope: parameters occupy slots 0..n-1 (bound by the call
  // prologue; declared here so bodies resolve them lexically).
  c._scopes.push_back(Scope{});
  for (const auto &p : params) {
    int32_t slot = c.allocSlot();
    c._scopes.back().vars[p.name] = slot;
  }

  fn->bodyStart = static_cast<uint32_t>(c._chunk.code.size());
  c.compileStmt(body);
  c.emit(Op::END_PROC, 0, 0, 0, body ? body->line : 0,
         body ? body->column : 0);
  if (!c._ok) {
    return nullptr;
  }

  // Default-argument programs live after the body; each computes one value.
  for (size_t i = 0; i < params.size(); ++i) {
    if (!params[i].defaultValue) {
      continue;
    }
    fn->defaultEntries[i] = static_cast<uint32_t>(c._chunk.code.size());
    c.compileExpr(params[i].defaultValue);
    c.emit(Op::END_DEFAULT, static_cast<int32_t>(i), 0, 0,
           params[i].defaultValue->line, params[i].defaultValue->column);
  }
  if (!c._ok) {
    return nullptr;
  }

  fn->chunk = std::move(c._chunk);
  return fn;
}

BytecodeChunk
BytecodeCompiler::compileSnippet(const std::vector<StmtPtr> &statements) {
  BytecodeCompiler c;
  c._globalMode = true;
  for (auto &s : statements) {
    c.compileStmt(s);
  }
  c.emit(Op::HALT, 0, 0, 0, 0, 0);
  if (!c._ok) {
    return BytecodeChunk{};
  }
  return std::move(c._chunk);
}

// ===========================================================================
// VM — bytecode execution engine
// ===========================================================================

namespace {

// True if `retType` is the script `void` type (mirrors invokeCallable).
bool returnsVoidType(const TypeInfo &retType) {
  return retType.baseType == DataType::VOID && !retType.isArray &&
         !retType.isStruct && !retType.isMap && !retType.isFunction &&
         !retType.isAuto;
}

size_t requiredParams(const std::vector<Parameter> &params) {
  size_t n = params.size();
  while (n > 0 && params[n - 1].defaultValue) {
    --n;
  }
  return n;
}

} // namespace

RuntimeError VM::vmError(const std::string &msg, int line, int column) {
  const Frame &f = _frames.back();
  return RuntimeError(msg, f.file, line, column, f.name);
}

bool VM::compileProcedure(const ProcedureDeclPtr &proc) {
  if (proc->vmFunc) {
    return true;
  }
  std::string file;
  auto it = _interp._procedureFiles.find(proc.get());
  if (it != _interp._procedureFiles.end()) {
    file = it->second;
  }
  VMFunctionPtr fn =
      BytecodeCompiler::compileFunction(proc->name, proc->parameters,
                                        proc->body, proc->returnType, file);
  if (!fn) {
    static bool debugFallback =
        std::getenv("CXXSCRIPT_VM_DEBUG") != nullptr;
    if (debugFallback) {
      std::fprintf(stderr, "[vm] compile fallback: %s\n", proc->name.c_str());
    }
    return false;
  }
  fn->decl = proc;
  fn->body = proc->body;
  fn->hasDeclaredRetType = true;
  proc->vmFunc = fn;
  return true;
}

// --- Scope bookkeeping -----------------------------------------------------
// Each frame owns an Environment whose parent is the caller's env (with a
// capture env sandwiched in between for lambdas/bound methods), mirroring
// invokeCallable. Compile-time locals live in `slots`; every declare/assign
// is mirrored into `env` so unresolved names resolve through the Environment
// parent chain — reproducing the interpreter's dynamic scoping exactly, and
// keeping tree-walker interop (AST-only callees, captures, debug snapshots)
// coherent.

void VM::leaveScopes(Frame &f, size_t depth) {
  while (f.scopeMarks.size() > depth) {
    f.scopeMarks.pop_back();
    f.env->exitScope();
  }
}

// --- Dynamic name resolution ------------------------------------------------
// Ports of Interpreter::readVariable/writeVariable. `f.env->has/get/assign`
// walk the parent chain (callee scopes -> env globals -> capture env ->
// caller envs -> global env), which is the language's dynamic scope.

Value VM::readName(const std::string &name, int line, int column) {
  Frame &f = _frames.back();
  if (f.env->has(name)) {
    return f.env->get(name);
  }

  // Bare field/method names resolve through `this` inside struct methods.
  if (name != "this" && f.env->has("this")) {
    const Value th = f.env->get("this");
    if (ValueHelper::isStruct(th)) {
      auto sv = std::get<StructPtr>(th);
      auto it = sv->fields.find(name);
      if (it != sv->fields.end()) {
        return it->second;
      }
      auto declIt = _interp._structs.find(sv->typeName);
      if (declIt != _interp._structs.end()) {
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

  auto extIt = _interp._externalVariables.find(name);
  if (extIt != _interp._externalVariables.end()) {
    if (!extIt->second.getter) {
      throw vmError("External variable '" + name + "' has no getter", line,
                    column);
    }
    return extIt->second.getter();
  }

  auto pit = _interp._procedures.find(name);
  if (pit != _interp._procedures.end() && !pit->second.empty()) {
    auto fv = std::make_shared<FunctionValue>();
    fv->displayName = name;
    fv->procs = pit->second;
    return fv;
  }

  throw vmError("Undefined variable: " + name, line, column);
}

void VM::writeName(const std::string &name, const Value &value, int line,
                   int column) {
  Frame &f = _frames.back();
  if (f.env->has(name)) {
    f.env->assign(name, value);
    return;
  }

  // Field writes through `this` (mirrors Interpreter::writeVariable).
  if (name != "this" && f.env->has("this")) {
    const Value th = f.env->get("this");
    if (ValueHelper::isStruct(th)) {
      auto sv = std::get<StructPtr>(th);
      auto it = sv->fields.find(name);
      if (it != sv->fields.end()) {
        StructDeclPtr decl = _interp.getStruct(sv->typeName);
        if (decl) {
          size_t fi = 0;
          for (size_t i = 0; i < decl->fields.size(); ++i) {
            if (decl->fields[i].name == name) {
              fi = i;
              break;
            }
          }
          try {
            it->second = _interp.convertToType(value, decl->fields[fi].type);
          } catch (RuntimeError &) {
            throw;
          } catch (const std::exception &e) {
            throw vmError(e.what(), line, column);
          }
        } else {
          it->second = value;
        }
        return;
      }
    }
  }

  auto extIt = _interp._externalVariables.find(name);
  if (extIt == _interp._externalVariables.end()) {
    throw vmError("Undefined variable: " + name, line, column);
  }
  if (!extIt->second.setter) {
    throw vmError("External variable '" + name + "' is read-only", line,
                  column);
  }
  extIt->second.setter(value);
}

std::unordered_map<std::string, Value> VM::snapshotVars() {
  // Environment::snapshot walks the whole parent chain, producing exactly
  // what the interpreter hands to debug hooks and lambda captures.
  if (_frames.empty()) {
    return _interp._globalEnv.snapshot();
  }
  return _frames.back().env->snapshot();
}

void VM::debugHook(Frame &, int line, int column) {
  Interpreter::DebugContext ctx{_interp._currentFile,
                                line,
                                column,
                                _interp._currentProcedure,
                                snapshotVars(),
                                _interp._currentCallDepth,
                                _interp._callStack};
  _interp._debugHook(ctx);
}

// --- Value-level operation helpers ------------------------------------------

Value VM::indexGet(const Value &container, const Value &index, int line,
                   int column) {
  if (ValueHelper::isMap(container)) {
    MapPtr m = std::get<MapPtr>(container);
    Value key;
    try {
      key = _interp.convertToType(index, m->keyType);
    } catch (RuntimeError &) {
      throw;
    } catch (const std::exception &e) {
      throw vmError(std::string("Map key: ") + e.what(), line, column);
    }
    auto it = m->entries.find(key);
    if (it == m->entries.end()) {
      throw vmError("Map key not found", line, column);
    }
    return it->second;
  }
  if (std::holds_alternative<std::string>(container)) {
    const std::string &s = std::get<std::string>(container);
    int64_t idx;
    try {
      idx = _interp.normalizeIndex(ValueHelper::toInt64(index), s.size(), line,
                                   column);
    } catch (RuntimeError &) {
      throw;
    } catch (const std::exception &e) {
      throw vmError(e.what(), line, column);
    }
    return static_cast<char>(s[static_cast<size_t>(idx)]);
  }
  if (!ValueHelper::isArray(container)) {
    throw vmError("Indexing non-array value", line, column);
  }
  const auto &elems = ValueHelper::arrayElements(container);
  int64_t idx;
  try {
    idx = _interp.normalizeIndex(ValueHelper::toInt64(index), elems.size(),
                                 line, column);
  } catch (RuntimeError &) {
    throw;
  } catch (const std::exception &e) {
    throw vmError(e.what(), line, column);
  }
  return elems[static_cast<size_t>(idx)];
}

Value VM::sliceGet(const Value &container, bool hasBegin, const Value &beginV,
                   bool hasEnd, const Value &endV, int line, int column) {
  int64_t size;
  bool isStr = std::holds_alternative<std::string>(container);
  if (isStr) {
    size = static_cast<int64_t>(std::get<std::string>(container).size());
  } else if (ValueHelper::isArray(container)) {
    size = static_cast<int64_t>(ValueHelper::arrayElements(container).size());
  } else {
    throw vmError("Slicing requires an array or string", line, column);
  }

  int64_t begin = 0;
  int64_t end = size;
  if (hasBegin) {
    begin = ValueHelper::toInt64(beginV);
  }
  if (hasEnd) {
    end = ValueHelper::toInt64(endV);
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
    _interp.checkStringLength(out.size());
    return out;
  }
  const auto &elems = ValueHelper::arrayElements(container);
  std::vector<Value> out(elems.begin() + begin, elems.begin() + end);
  _interp.noteArrayAllocation(out.size());
  return ValueHelper::createArray(ValueHelper::arrayElementType(container),
                                  std::move(out));
}

Value VM::memberGet(const Value &object, const std::string &member, int line,
                    int column) {
  if (!ValueHelper::isStruct(object)) {
    throw vmError("Member access '.' requires a struct value", line, column);
  }
  StructPtr sv = std::get<StructPtr>(object);
  if (!sv) {
    throw vmError("Member access on null struct", line, column);
  }
  auto it = sv->fields.find(member);
  if (it != sv->fields.end()) {
    return it->second;
  }
  auto declIt = _interp._structs.find(sv->typeName);
  if (declIt != _interp._structs.end()) {
    std::vector<ProcedureDeclPtr> methods;
    for (const auto &m : declIt->second->methods) {
      if (m->name == member) {
        methods.push_back(m);
      }
    }
    if (!methods.empty()) {
      auto fv = std::make_shared<FunctionValue>();
      fv->displayName = sv->typeName + "." + member;
      fv->procs = std::move(methods);
      fv->captured["this"] = object;
      return fv;
    }
  }
  throw vmError("Struct '" + sv->typeName + "' has no field or method '" +
                    member + "'",
                line, column);
}

void VM::indexSet(const Value &container, const Value &index, Value value,
                  AssignStmt::Operator op, int line, int column) {
  if (ValueHelper::isMap(container)) {
    MapPtr m = std::get<MapPtr>(container);
    Value key;
    try {
      key = _interp.convertToType(index, m->keyType);
    } catch (RuntimeError &) {
      throw;
    } catch (const std::exception &e) {
      throw vmError(std::string("Map key: ") + e.what(), line, column);
    }
    auto it = m->entries.find(key);
    if (op != AssignStmt::Operator::ASSIGN) {
      if (it == m->entries.end()) {
        throw vmError("Map key not found for compound assignment", line,
                      column);
      }
      value = _interp.applyAssignOp(op, it->second, value, line, column);
    }
    Value converted;
    try {
      converted = _interp.convertToType(value, m->valueType);
    } catch (RuntimeError &) {
      throw;
    } catch (const std::exception &e) {
      throw vmError(e.what(), line, column);
    }
    if (it == m->entries.end()) {
      _interp.checkArraySize(m->entries.size() + 1);
      m->entries.emplace(std::move(key), std::move(converted));
    } else {
      it->second = converted;
    }
    return;
  }

  if (!ValueHelper::isArray(container)) {
    throw vmError("Index assignment on non-array value", line, column);
  }
  std::vector<Value> &elems = ValueHelper::arrayElements(
      const_cast<Value &>(container));
  int64_t idx;
  try {
    idx = _interp.normalizeIndex(ValueHelper::toInt64(index), elems.size(),
                                 line, column);
  } catch (RuntimeError &) {
    throw;
  } catch (const std::exception &e) {
    throw vmError(e.what(), line, column);
  }
  TypeInfo elementType = ValueHelper::arrayElementType(container);
  if (op != AssignStmt::Operator::ASSIGN) {
    value = _interp.applyAssignOp(op, elems[static_cast<size_t>(idx)], value,
                                  line, column);
  }
  Value converted;
  try {
    converted = _interp.convertToType(value, elementType);
  } catch (RuntimeError &) {
    throw;
  } catch (const std::exception &e) {
    throw vmError(e.what(), line, column);
  }
  elems[static_cast<size_t>(idx)] = converted;
}

void VM::memberSet(const Value &object, const std::string &member, Value value,
                   AssignStmt::Operator op, int line, int column) {
  if (!ValueHelper::isStruct(object)) {
    throw vmError("Member assignment '.' requires a struct value", line,
                  column);
  }
  StructPtr sv = std::get<StructPtr>(object);
  if (!sv) {
    throw vmError("Member assignment on null struct", line, column);
  }
  auto fieldIt = sv->fields.find(member);
  if (fieldIt == sv->fields.end()) {
    throw vmError("Struct '" + sv->typeName + "' has no field '" + member +
                      "'",
                  line, column);
  }
  if (op != AssignStmt::Operator::ASSIGN) {
    value =
        _interp.applyAssignOp(op, fieldIt->second, value, line, column);
  }
  auto declIt = _interp._structs.find(sv->typeName);
  if (declIt != _interp._structs.end()) {
    for (const auto &fd : declIt->second->fields) {
      if (fd.name == member) {
        try {
          value = _interp.convertToType(value, fd.type);
        } catch (RuntimeError &) {
          throw;
        } catch (const std::exception &e) {
          throw vmError("Field '" + member + "': " + e.what(), line, column);
        }
        break;
      }
    }
  }
  fieldIt->second = std::move(value);
}

Value VM::binaryOp(Op op, const Value &l, const Value &r, int line,
                   int column) {
  try {
    switch (op) {
    case Op::ADD: return ValueHelper::add(l, r);
    case Op::SUB: return ValueHelper::subtract(l, r);
    case Op::MUL: return ValueHelper::multiply(l, r);
    case Op::DIV: return ValueHelper::divide(l, r);
    case Op::MOD: return ValueHelper::modulo(l, r);
    case Op::EQ:  return ValueHelper::equals(l, r);
    case Op::NE:  return ValueHelper::notEquals(l, r);
    case Op::LT:  return ValueHelper::lessThan(l, r);
    case Op::GT:  return ValueHelper::greaterThan(l, r);
    case Op::LE:  return ValueHelper::lessOrEqual(l, r);
    case Op::GE:  return ValueHelper::greaterOrEqual(l, r);
    case Op::BAND: return ValueHelper::bitAnd(l, r);
    case Op::BOR:  return ValueHelper::bitOr(l, r);
    case Op::BXOR: return ValueHelper::bitXor(l, r);
    case Op::SHL:  return ValueHelper::lshift(l, r);
    case Op::SHR:  return ValueHelper::rshift(l, r);
    default: break;
    }
  } catch (const std::exception &e) {
    throw vmError(e.what(), line, column);
  }
  throw vmError("Unknown binary operator", line, column);
}

// --- Calls -------------------------------------------------------------------

// Phase 1 of a named call: resolve what `name` refers to, before any argument
// expressions run (matching evaluateCall's ordering where dispatch errors
// precede argument side effects).
void VM::resolveCallSite(Frame &f, const std::string &name, CallSite &site,
                         int line, int column) {
  site.kind = CallSite::Kind::NONE;
  site.varFunc.reset();
  site.procDecl.reset();
  site.structDecl.reset();

  // A variable holding a function value shadows everything else.
  Value v;
  if (f.env->tryGet(name, v)) {
    auto *fn = std::get_if<FuncPtr>(&v);
    if (!fn) {
      throw vmError("'" + name + "' is not a function value", line, column);
    }
    site.kind = CallSite::Kind::VAR_FUNC;
    site.varFunc = *fn;
    return;
  }

  // Inside a struct method, bare `m(...)` calls the sibling method on `this`.
  Value th;
  if (f.env->tryGet("this", th)) {
    if (ValueHelper::isStruct(th)) {
      auto sv = std::get<StructPtr>(th);
      auto declIt = _interp._structs.find(sv->typeName);
      if (declIt != _interp._structs.end()) {
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
          site.kind = CallSite::Kind::VAR_FUNC;
          site.varFunc = fv;
          return;
        }
      }
    }
  }

  // Inline cache (procedures / externals).
  if (site.version == _interp._callCacheVersion) {
    if (site.isProcedure) {
      if (auto proc = site.proc.lock()) {
        site.kind = CallSite::Kind::PROC;
        site.procDecl = proc;
        return;
      }
    }
    if (site.isExternal && site.external) {
      site.kind = CallSite::Kind::EXTERN;
      return;
    }
  }

  if (auto it = _interp._procedures.find(name);
      it != _interp._procedures.end() && !it->second.empty()) {
    site.kind = CallSite::Kind::PROC;
    if (it->second.size() == 1) {
      site.version = _interp._callCacheVersion;
      site.isProcedure = true;
      site.isExternal = false;
      site.proc = it->second.front();
      site.procDecl = it->second.front();
    } else {
      site.procDecl.reset(); // resolved per-call from the live overload set
    }
    return;
  }

  if (auto vit = _interp._externalVariables.find(name);
      vit != _interp._externalVariables.end() && vit->second.getter) {
    Value ev = vit->second.getter();
    if (std::holds_alternative<FuncPtr>(ev)) {
      site.kind = CallSite::Kind::EXT_VAR;
      site.varFunc = std::get<FuncPtr>(ev);
      return;
    }
  }

  if (auto extIt = _interp._externalFunctions.find(name);
      extIt != _interp._externalFunctions.end()) {
    site.version = _interp._callCacheVersion;
    site.isProcedure = false;
    site.isExternal = true;
    site.external = extIt->second;
    site.kind = CallSite::Kind::EXTERN;
    return;
  }

  if (auto sIt = _interp._structs.find(name); sIt != _interp._structs.end()) {
    site.kind = CallSite::Kind::STRUCT_CTOR;
    site.structDecl = sIt->second;
    return;
  }

  if (Builtins::isBuiltin(name) && _interp.isBuiltinEnabled(name)) {
    site.kind = CallSite::Kind::BUILTIN;
    return;
  }

  throw vmError("Undefined function: " + name, line, column);
}

// Phase 2: args are on the stack; dispatch the resolved callee.
void VM::performCall(Frame &, const std::string &name, CallSite &site,
                     int32_t argc, int line, int column) {
  std::vector<Value> args(static_cast<size_t>(argc));
  for (int32_t i = argc; i-- > 0;) {
    args[i] = std::move(_stack.back());
    _stack.pop_back();
  }

  switch (site.kind) {
  case CallSite::Kind::VAR_FUNC:
  case CallSite::Kind::EXT_VAR: {
    _interp._callSiteLine = line;
    _interp._callSiteColumn = column;
    callValue(site.varFunc, std::move(args), line, column);
    return;
  }
  case CallSite::Kind::PROC: {
    ProcedureDeclPtr proc = site.procDecl;
    if (!proc) {
      // Multi-overload set or cache miss: resolve from the live table.
      auto pit = _interp._procedures.find(name);
      if (pit == _interp._procedures.end() || pit->second.empty()) {
        throw vmError("Undefined function: " + name, line, column);
      }
      proc = pit->second.size() == 1
                 ? pit->second.front()
                 : _interp.resolveOverload(name, pit->second, args, line,
                                           column);
    }
    _interp._callSiteLine = line;
    _interp._callSiteColumn = column;
    if (proc->vmFunc || compileProcedure(proc)) {
      enterCall(proc->vmFunc, std::move(args), nullptr, line, column);
      return; // callee's RETURN pushes the result
    }
    auto fv = std::make_shared<FunctionValue>();
    fv->displayName = proc->name;
    fv->procs = {proc};
    _stack.push_back(callViaInterpreter(fv, std::move(args), line, column));
    return;
  }
  case CallSite::Kind::EXTERN: {
    Value r = site.external(args);
    _interp.checkResultSize(r);
    _stack.push_back(std::move(r));
    return;
  }
  case CallSite::Kind::STRUCT_CTOR: {
    _stack.push_back(
        _interp.constructStruct(site.structDecl, args, line, column));
    return;
  }
  case CallSite::Kind::BUILTIN: {
    // Builtins consume the CallExpr lazily through the interpreter; pool the
    // wrapper nodes per call site and overwrite values in place.
    if (!site.builtinExpr) {
      site.builtinExpr =
          std::make_shared<CallExpr>(name, std::vector<ExprPtr>{});
    }
    CallExpr &expr = *site.builtinExpr;
    expr.line = line;
    expr.column = column;
    expr.arguments.resize(args.size());
    if (site.builtinArgs.size() < args.size()) {
      site.builtinArgs.resize(args.size());
    }
    for (size_t i = 0; i < args.size(); ++i) {
      auto &lit = site.builtinArgs[i];
      if (!lit) {
        lit = std::make_shared<LiteralExpr>(args[i],
                                            ValueHelper::getType(args[i]),
                                            line, column);
      } else {
        lit->value = args[i];
        lit->type = ValueHelper::getType(args[i]);
        lit->line = line;
        lit->column = column;
      }
      expr.arguments[i] = lit;
    }
    Value r = Builtins::call(_interp, name, &expr);
    _interp.checkResultSize(r);
    _stack.push_back(std::move(r));
    return;
  }
  case CallSite::Kind::NONE:
    break;
  }
  throw vmError("Undefined function: " + name, line, column);
}

void VM::callValue(const Value &callee, std::vector<Value> args, int line,
                   int column) {
  auto *fnp = std::get_if<FuncPtr>(&callee);
  if (!fnp || !*fnp) {
    throw vmError("Call target is not a function value", line, column);
  }
  const FuncPtr &fn = *fnp;

  if (fn->vmFunc) {
    enterCall(fn->vmFunc, std::move(args),
              fn->captured.empty() ? nullptr : &fn->captured, line, column);
    return;
  }
  if (!fn->procs.empty()) {
    ProcedureDeclPtr proc =
        fn->procs.size() == 1
            ? fn->procs.front()
            : _interp.resolveOverload(fn->displayName, fn->procs, args, line,
                                      column);
    if (proc->vmFunc || compileProcedure(proc)) {
      enterCall(proc->vmFunc, std::move(args),
                fn->captured.empty() ? nullptr : &fn->captured, line, column);
      return;
    }
  }
  // AST-body function value (lambda from the interpreter side, or a proc that
  // failed to compile): route through invokeCallable with a materialized env.
  _stack.push_back(callViaInterpreter(fn, std::move(args), line, column));
}

Value VM::callViaInterpreter(const FuncPtr &fn, std::vector<Value> args,
                             int line, int column) {
  // The frame's Environment chain already reproduces dynamic scoping, so the
  // tree-walker resolves names through it directly.
  return _interp.callFunctionValue(fn, args, line, column);
}

// --- Frame lifecycle ---------------------------------------------------------

void VM::enterCall(const VMFunctionPtr &fn, std::vector<Value> args,
                   const std::unordered_map<std::string, Value> *captures,
                   int line, int column) {
  Frame f;
  f.prevProcedure = _interp._currentProcedure;
  f.prevFile = _interp._currentFile;
  f.prevCallLine = _interp._callSiteLine;
  f.prevCallCol = _interp._callSiteColumn;
  f.name = fn->name;
  f.file = fn->file;
  f.callLine = line;
  f.callCol = column;
  _interp._currentProcedure = fn->name;
  _interp._currentFile = fn->file;
  _interp._callStack.push_back(fn->name);

  auto restoreFrame = [&]() {
    _interp._currentProcedure = f.prevProcedure;
    _interp._currentFile = f.prevFile;
    _interp._callSiteLine = f.prevCallLine;
    _interp._callSiteColumn = f.prevCallCol;
    if (!_interp._callStack.empty()) {
      _interp._callStack.pop_back();
    }
  };

  if (_interp._maxCallDepth > 0 &&
      _interp._currentCallDepth >= _interp._maxCallDepth) {
    restoreFrame();
    throw RuntimeError("Maximum call depth exceeded", fn->file, line, column,
                       fn->name, /*fatal=*/true);
  }
  if (_interp._maxStackBytes > 0 && _interp._stackBase != nullptr) {
    // VM frames live on the heap: bill each a fixed estimate so the
    // guardrail still bounds recursion. The prospective frame's charge is
    // included in `used`, matching invokeCallable measuring its own frame.
    char marker;
    const char *cur = &marker;
    size_t used =
        (cur > _interp._stackBase
             ? static_cast<size_t>(cur - _interp._stackBase)
             : static_cast<size_t>(_interp._stackBase - cur)) +
        _interp._vmChargedStack + Interpreter::kVmFrameStackCost;
    if (used > _interp._maxStackBytes) {
      restoreFrame();
      throw RuntimeError("Native stack limit exceeded (" +
                             std::to_string(used) + " > " +
                             std::to_string(_interp._maxStackBytes) + ")",
                         fn->file, line, column, fn->name, /*fatal=*/true);
    }
  }

  size_t required = requiredParams(fn->params);
  if (args.size() < required || args.size() > fn->params.size()) {
    std::stringstream ss;
    ss << "'" << fn->name << "' expects ";
    if (required == fn->params.size()) {
      ss << fn->params.size();
    } else {
      ss << required << "-" << fn->params.size();
    }
    ss << " arguments, got " << args.size();
    restoreFrame();
    throw RuntimeError(ss.str(), fn->file, line, column, fn->name);
  }

  ++_interp._currentCallDepth;

  f.fn = fn;
  f.chunk = &fn->chunk;
  f.slots.assign(fn->chunk.numSlots, Value(static_cast<int32_t>(0)));
  f.stackBase = _stack.size();

  // Captured variables sit in an env between the caller's env and the call's
  // own bindings (mirrors invokeCallable's capEnv).
  Interpreter::Environment *parent = _interp._currentEnv;
  if (captures && !captures->empty()) {
    f.ownedCapEnv =
        std::make_unique<Interpreter::Environment>(parent);
    for (const auto &kv : *captures) {
      f.ownedCapEnv->define(kv.first, kv.second);
    }
    parent = f.ownedCapEnv.get();
  }
  f.ownedEnv = std::make_unique<Interpreter::Environment>(parent);
  f.env = f.ownedEnv.get();
  f.prevEnv = _interp._currentEnv;
  _interp._currentEnv = f.env;

  try {
    for (size_t i = 0; i < args.size(); ++i) {
      Value arg = std::move(args[i]);
      if (!fn->params[i].type.isAuto) {
        try {
          arg = _interp.convertToType(arg, fn->params[i].type);
        } catch (RuntimeError &) {
          throw;
        } catch (const std::exception &e) {
          throw RuntimeError(e.what(), fn->file, line, column, fn->name);
        }
      }
      f.slots[i] = arg;
      f.env->define(fn->params[i].name, arg);
    }
  } catch (...) {
    _interp._currentEnv = f.prevEnv;
    restoreFrame();
    --_interp._currentCallDepth;
    throw;
  }

  // Missing trailing params evaluate their default programs in callee scope.
  for (size_t i = args.size(); i < fn->params.size(); ++i) {
    if (fn->defaultEntries[i] != UINT32_MAX) {
      f.pendingDefaults.push_back(static_cast<int32_t>(i));
    }
  }
  if (f.pendingDefaults.empty()) {
    f.pc = fn->bodyStart;
    f.inDefaultIdx = -1;
  } else {
    f.pc = fn->defaultEntries[f.pendingDefaults.front()];
    f.inDefaultIdx = f.pendingDefaults.front();
  }
  _interp._vmChargedStack += Interpreter::kVmFrameStackCost;
  _frames.push_back(std::move(f));
}

void VM::popFrame() {
  Frame &f = _frames.back();
  // Release step reserves held by finally blocks dying with the frame.
  for (auto &h : f.handlers) {
    if (h.inFinally &&
        _interp._stepGrace >= Interpreter::kFinallyStepReserve) {
      _interp._stepGrace -= Interpreter::kFinallyStepReserve;
    }
  }
  _interp._currentEnv = f.prevEnv;
  if (!f.isSnippet) {
    _interp._currentProcedure = f.prevProcedure;
    _interp._currentFile = f.prevFile;
    _interp._callSiteLine = f.prevCallLine;
    _interp._callSiteColumn = f.prevCallCol;
    if (!_interp._callStack.empty()) {
      _interp._callStack.pop_back();
    }
    --_interp._currentCallDepth;
    if (_interp._vmChargedStack >= Interpreter::kVmFrameStackCost) {
      _interp._vmChargedStack -= Interpreter::kVmFrameStackCost;
    } else {
      _interp._vmChargedStack = 0;
    }
  }
  _stack.resize(f.stackBase);
  _frames.pop_back();
}

// --- Unwinding ----------------------------------------------------------------

void VM::enterFinally(Frame &f, ActiveHandler &h, const HandlerInfo &hi) {
  h.inFinally = true;
  h.parked = _pending;
  _pending.kind = Pending::Kind::NONE;
  // Finally blocks get a bounded step reserve so cleanup completes even on a
  // tripped step budget (executeTryCatch parity).
  _interp._stepGrace += Interpreter::kFinallyStepReserve;
  leaveScopes(f, h.scopeDepth);
  _stack.resize(h.stackDepth);
  f.pc = hi.finallyPC;
}

void VM::dropHandler(Frame &f) {
  if (f.handlers.back().inFinally &&
      _interp._stepGrace >= Interpreter::kFinallyStepReserve) {
    _interp._stepGrace -= Interpreter::kFinallyStepReserve;
  }
  f.handlers.pop_back();
}

void VM::unwind(size_t base) {
  while (!_frames.empty() && _pending.kind != Pending::Kind::NONE) {
    Frame &f = _frames.back();

    if (_pending.kind == Pending::Kind::RET) {
      // Run outstanding finally blocks, innermost first.
      bool enteredFinally = false;
      while (!f.handlers.empty()) {
        ActiveHandler &h = f.handlers.back();
        const HandlerInfo &hi = f.chunk->handlers[h.tableIdx];
        if (!h.inFinally && hi.finallyPC != UINT32_MAX) {
          enterFinally(f, h, hi);
          enteredFinally = true;
          break;
        }
        dropHandler(f);
      }
      if (enteredFinally) {
        return;
      }
      // Frame exit: convert the return value and deliver to the caller.
      Value ret = _pending.value;
      int callLine = f.callLine, callCol = f.callCol;
      bool snippet = f.isSnippet;
      VMFunctionPtr fn = f.fn;
      popFrame();
      _pending.kind = Pending::Kind::NONE;
      if (snippet) {
        _snippetResult = ret;
        _snippetDone = true;
        return;
      }
      // Return-type conversion happens in caller context (parity with
      // invokeCallable, which restores before converting on the fallthrough
      // path — for ReturnException it restores first too).
      Value out;
      const TypeInfo &rt = fn->retType;
      if (returnsVoidType(rt)) {
        out = static_cast<int32_t>(0);
      } else if (rt.isAuto) {
        out = ret;
      } else {
        try {
          out = _interp.convertToType(ret, rt);
        } catch (const std::exception &e) {
          _pending.kind = Pending::Kind::RUNTIME_ERR;
          _pending.error =
              RuntimeError(e.what(), _interp._currentFile, callLine, callCol,
                           _interp._currentProcedure);
          continue; // unwind into caller
        }
      }
      if (_frames.size() <= base) {
        // Result for this run() boundary; flush in run().
        _pending.kind = Pending::Kind::RET;
        _pending.value = std::move(out);
        return;
      }
      _stack.push_back(std::move(out));
      return;
    }

    if (_pending.kind == Pending::Kind::BRK ||
        _pending.kind == Pending::Kind::CNT) {
      bool landed = false;
      while (!f.handlers.empty()) {
        ActiveHandler &h = f.handlers.back();
        const HandlerInfo &hi = f.chunk->handlers[h.tableIdx];
        if (_pending.targetPC >= hi.tryBegin &&
            _pending.targetPC < hi.regionEnd) {
          landed = true; // landing inside the protected region
          break;
        }
        if (hi.finallyPC != UINT32_MAX && !h.inFinally) {
          enterFinally(f, h, hi);
          return; // pending parked; FINALLY_END resumes it
        }
        dropHandler(f);
      }
      (void)landed;
      leaveScopes(f, _pending.scopeDepth);
      _stack.resize(f.stackBase);
      f.pc = _pending.targetPC;
      _pending.kind = Pending::Kind::NONE;
      return;
    }

    // SCRIPT_EXC / RUNTIME_ERR
    bool isScriptExc = _pending.kind == Pending::Kind::SCRIPT_EXC;
    bool catchable = isScriptExc || !_pending.error.fatal;
    bool dispatched = false;
    while (!f.handlers.empty() && !dispatched) {
      ActiveHandler &h = f.handlers.back();
      const HandlerInfo &hi = f.chunk->handlers[h.tableIdx];
      if (h.inFinally) {
        // A new pending action supersedes whatever was parked here.
        dropHandler(f);
        continue;
      }
      if (!h.inCatch && catchable && hi.catchPC != UINT32_MAX) {
        h.inCatch = true;
        _stack.resize(h.stackDepth);
        leaveScopes(f, h.scopeDepth);
        f.env->enterScope(); // catch-variable scope
        f.scopeMarks.push_back(0);
        if (hi.catchSlot >= 0) {
          Value thrown = isScriptExc
                             ? _pending.value
                             : Value(std::string(_pending.error.what()));
          if (hi.catchTypeIdx >= 0) {
            try {
              thrown = _interp.convertToType(
                  thrown, f.chunk->types[hi.catchTypeIdx]);
            } catch (const std::exception &e) {
              f.env->exitScope();
              f.scopeMarks.pop_back();
              _pending.kind = Pending::Kind::RUNTIME_ERR;
              _pending.error =
                  RuntimeError(std::string("catch variable: ") + e.what(),
                               f.file, hi.line, hi.column, f.name);
              dispatched = true; // re-enter unwind with the new error
              break;
            }
          }
          // Push for DECL_SLOT at catchPC to bind into slot + env.
          _stack.push_back(std::move(thrown));
        }
        f.pc = hi.catchPC;
        _pending.kind = Pending::Kind::NONE;
        return;
      }
      if (hi.finallyPC != UINT32_MAX) {
        if (hi.catchPC == UINT32_MAX && catchable) {
          // try/finally with no catch: rethrow as ScriptException afterwards.
          h.remapToScriptExc = true;
          h.remapped = isScriptExc
                           ? _pending.value
                           : Value(std::string(_pending.error.what()));
        }
        enterFinally(f, h, hi);
        return; // pending parked; FINALLY_END resumes it
      }
      dropHandler(f);
    }
    if (dispatched) {
      continue;
    }

    // No handler caught it in this frame.
    if (!isScriptExc) {
      // Append this frame to the RuntimeError's stack trace.
      RuntimeError &e = _pending.error;
      bool raiseFrame = e.trace.empty() && e.procedureName == f.name;
      uint32_t sitePc = f.pc > 0 ? f.pc - 1 : 0;
      const Instruction &site = f.chunk->code[sitePc];
      e.trace.push_back({f.name, f.file,
                         raiseFrame ? e.line : site.line,
                         raiseFrame ? e.column : site.column});
    }
    popFrame(); // snippet frames pop without interpreter bookkeeping
    // Continue unwinding into the caller; at the run boundary pending is
    // flushed into a real throw.
  }
}

// --- Entry points --------------------------------------------------------------

Value VM::callProc(const ProcedureDeclPtr &proc, std::vector<Value> args,
                   int line, int column,
                   const std::unordered_map<std::string, Value> *captures) {
  if (!proc->vmFunc && !compileProcedure(proc)) {
    auto fv = std::make_shared<FunctionValue>();
    fv->displayName = proc->name;
    fv->procs = {proc};
    bool topLevel = !_interp._executionActive;
    char stackMarker;
    if (topLevel) {
      _interp._executionActive = true;
      _interp._currentCallDepth = 0;
      _interp._currentSteps = 0;
      _interp._allocationCount = 0;
      _interp._callSiteLine = 0;
      _interp._callSiteColumn = 0;
      _interp._stepGrace = 0;
      _interp._stackBase = &stackMarker;
    }
    try {
      Value r = _interp.callFunctionValue(fv, args, line, column);
      if (topLevel) {
        _interp._executionActive = false;
      }
      return r;
    } catch (...) {
      if (topLevel) {
        _interp._executionActive = false;
      }
      throw;
    }
  }

  bool topLevel = !_interp._executionActive;
  char stackMarker;
  if (topLevel) {
    _interp._executionActive = true;
    _interp._currentCallDepth = 0;
    _interp._currentSteps = 0;
    _interp._allocationCount = 0;
    _interp._callSiteLine = 0;
    _interp._callSiteColumn = 0;
    _interp._stepGrace = 0;
    _interp._stackBase = &stackMarker;
  }

  size_t base = _frames.size();
  try {
    enterCall(proc->vmFunc, std::move(args), captures, line, column);
    Value r = run(base);
    if (topLevel) {
      _interp._executionActive = false;
    }
    return r;
  } catch (const ScriptException &se) {
    if (topLevel) {
      _interp._executionActive = false;
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
      _interp._executionActive = false;
    }
    throw;
  }
}

Value VM::callFunction(const FuncPtr &fn, std::vector<Value> args, int line,
                       int column) {
  bool topLevel = !_interp._executionActive;
  char stackMarker;
  if (topLevel) {
    _interp._executionActive = true;
    _interp._currentCallDepth = 0;
    _interp._currentSteps = 0;
    _interp._allocationCount = 0;
    _interp._callSiteLine = 0;
    _interp._callSiteColumn = 0;
    _interp._stepGrace = 0;
    _interp._stackBase = &stackMarker;
  }
  size_t base = _frames.size();
  try {
    callValue(fn, std::move(args), line, column);
    // callValue may have pushed a frame (async) or a result (sync path).
    if (_frames.size() == base) {
      if (topLevel) {
        _interp._executionActive = false;
      }
      Value r = _stack.back();
      _stack.pop_back();
      return r;
    }
    Value r = run(base);
    if (topLevel) {
      _interp._executionActive = false;
    }
    return r;
  } catch (const ScriptException &se) {
    if (topLevel) {
      _interp._executionActive = false;
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
      _interp._executionActive = false;
    }
    throw;
  }
}

Value VM::runSnippet(const std::vector<StmtPtr> &statements) {
  BytecodeChunk chunk = BytecodeCompiler::compileSnippet(statements);
  if (chunk.code.empty()) {
    // Compile fallback: run on the tree-walker with identical bookkeeping.
    return _interp.executeStatementsImpl(statements);
  }

  bool topLevel = !_interp._executionActive;
  char stackMarker;
  if (topLevel) {
    _interp._executionActive = true;
    _interp._currentCallDepth = 0;
    _interp._currentSteps = 0;
    _interp._allocationCount = 0;
    _interp._callSiteLine = 0;
    _interp._callSiteColumn = 0;
    _interp._stepGrace = 0;
    _interp._stackBase = &stackMarker;
  }

  auto fn = std::make_shared<VMFunction>();
  fn->name = "";
  fn->file = _interp._currentFile;
  fn->retType = TypeInfo::autoType();
  fn->chunk = std::move(chunk);
  fn->bodyStart = 0;

  Frame f;
  f.fn = fn;
  f.chunk = &fn->chunk;
  f.isSnippet = true;
  f.env = &_interp._globalEnv;
  f.prevEnv = _interp._currentEnv;
  f.stackBase = _stack.size();
  f.pc = 0;

  Interpreter::Environment *prevEnv = _interp._currentEnv;
  _interp._currentEnv = &_interp._globalEnv;
  _snippetDone = false;
  _snippetResult = static_cast<int32_t>(0);

  size_t base = _frames.size();
  _frames.push_back(std::move(f));

  try {
    run(base);
    _interp._currentEnv = prevEnv;
    if (topLevel) {
      _interp._executionActive = false;
    }
    return _snippetResult;
  } catch (const ScriptException &se) {
    _interp._currentEnv = prevEnv;
    if (topLevel) {
      _interp._executionActive = false;
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
    _interp._currentEnv = prevEnv;
    if (topLevel) {
      _interp._executionActive = false;
    }
    throw;
  }
}


// --- Dispatch loop -----------------------------------------------------------

Value VM::run(size_t base) {
  static const bool kCount = std::getenv("CXXSCRIPT_VM_COUNT") != nullptr;
  uint64_t icount = 0;
  while (_frames.size() > base) {
    if (_pending.kind != Pending::Kind::NONE) {
      unwind(base);
      continue;
    }
    Frame &f = _frames.back();
    if (f.pc >= f.chunk->code.size()) {
      // Should not happen (compiler terminates chunks), treat as HALT.
      if (f.isSnippet) {
        _snippetResult = static_cast<int32_t>(0);
        _snippetDone = true;
        popFrame();
        continue;
      }
      _pending.kind = Pending::Kind::RET;
      _pending.value = static_cast<int32_t>(0);
      continue;
    }
    const Instruction &in = f.chunk->code[f.pc];
    ++f.pc;
    icount += kCount;
    try {
      switch (in.op) {
      case Op::NOP:
        break;
      case Op::CONST:
        _stack.push_back(f.chunk->constants[in.a]);
        break;
      case Op::LOAD_SLOT:
        _stack.push_back(f.slots[in.a]);
        break;
      case Op::STORE_SLOT: {
        f.slots[in.a] = _stack.back();
        if (in.b >= 0) {
          f.env->assign(f.chunk->names[in.b], _stack.back());
        }
        _stack.pop_back();
        break;
      }
      case Op::DECL_SLOT:
        f.slots[in.a] = _stack.back();
        f.env->define(f.chunk->names[in.b], _stack.back());
        _stack.pop_back();
        break;
      case Op::LOAD_NAME:
        _stack.push_back(readName(f.chunk->names[in.a], in.line, in.column));
        break;
      case Op::STORE_NAME:
        writeName(f.chunk->names[in.a], _stack.back(), in.line, in.column);
        _stack.pop_back();
        break;
      case Op::DEFINE_NAME:
        f.env->define(f.chunk->names[in.a], _stack.back());
        _stack.pop_back();
        break;
      case Op::ENTER_SCOPE:
        f.env->enterScope();
        f.scopeMarks.push_back(0);
        break;
      case Op::LEAVE_SCOPE:
        if (!f.scopeMarks.empty()) {
          f.scopeMarks.pop_back();
          f.env->exitScope();
        }
        break;
      case Op::POP:
        _stack.pop_back();
        break;
      case Op::DUP:
        _stack.push_back(_stack.back());
        break;
      case Op::JUMP:
        f.pc = static_cast<uint32_t>(in.a);
        break;
      case Op::JUMP_FALSE: {
        Value c = _stack.back();
        _stack.pop_back();
        if (!ValueHelper::toBool(c)) {
          f.pc = static_cast<uint32_t>(in.a);
        }
        break;
      }
      case Op::JUMP_TRUE: {
        Value c = _stack.back();
        _stack.pop_back();
        if (ValueHelper::toBool(c)) {
          f.pc = static_cast<uint32_t>(in.a);
        }
        break;
      }
      case Op::TOBOOL: {
        Value c = _stack.back();
        _stack.pop_back();
        _stack.push_back(ValueHelper::toBool(c));
        break;
      }
      case Op::ADD:
      case Op::SUB:
      case Op::MUL:
      case Op::DIV:
      case Op::MOD:
      case Op::EQ:
      case Op::NE:
      case Op::LT:
      case Op::GT:
      case Op::LE:
      case Op::GE:
      case Op::BAND:
      case Op::BOR:
      case Op::BXOR:
      case Op::SHL:
      case Op::SHR: {
        Value r = _stack.back();
        _stack.pop_back();
        Value l = _stack.back();
        _stack.pop_back();
        Value v = binaryOp(in.op, l, r, in.line, in.column);
        _interp.checkResultSize(v);
        _stack.push_back(std::move(v));
        break;
      }
      case Op::ASSIGN_OP: {
        // Stack: [value, current] — value was compiled first so its side
        // effects precede the LHS read (executeAssign parity).
        Value cur = _stack.back();
        _stack.pop_back();
        Value v = _stack.back();
        _stack.pop_back();
        _stack.push_back(_interp.applyAssignOp(
            static_cast<AssignStmt::Operator>(in.a), cur, v, in.line,
            in.column));
        break;
      }
      case Op::NEG: {
        Value o = _stack.back();
        _stack.pop_back();
        // Port of evaluateUnary NEGATE.
        DataType t = ValueHelper::getType(o).baseType;
        if (t == DataType::DOUBLE || t == DataType::FLOAT) {
          _stack.push_back(
              ValueHelper::createValue(t, -ValueHelper::toDouble(o)));
        } else {
          int64_t v = ValueHelper::toInt64(o);
          int64_t neg =
              static_cast<int64_t>(uint64_t(0) - static_cast<uint64_t>(v));
          DataType rt =
              (t == DataType::INT64 || t == DataType::UINT64)
                  ? DataType::INT64
                  : DataType::INT32;
          _stack.push_back(ValueHelper::createValue(rt, neg));
        }
        break;
      }
      case Op::NOT: {
        Value o = _stack.back();
        _stack.pop_back();
        _stack.push_back(ValueHelper::logicalNot(o));
        break;
      }
      case Op::BNOT: {
        Value o = _stack.back();
        _stack.pop_back();
        _stack.push_back(ValueHelper::bitNot(o));
        break;
      }
      case Op::ERROR:
        throw vmError(std::get<std::string>(f.chunk->constants[in.a]),
                      in.line, in.column);
      case Op::CONCAT_N: {
        std::string out;
        size_t n = static_cast<size_t>(in.a);
        size_t start = _stack.size() - n;
        for (size_t i = 0; i < n; ++i) {
          out += ValueHelper::toString(_stack[start + i]);
        }
        _stack.resize(start);
        _interp.checkStringLength(out.size());
        _stack.push_back(std::move(out));
        break;
      }
      case Op::CALL_RESOLVE:
        resolveCallSite(f, f.chunk->names[in.a], f.chunk->callSites[in.c],
                        in.line, in.column);
        break;
      case Op::CALL:
        performCall(f, f.chunk->names[in.a], f.chunk->callSites[in.c], in.b,
                    in.line, in.column);
        break;
      case Op::CALL_VALUE: {
        std::vector<Value> args(static_cast<size_t>(in.b));
        for (int32_t i = in.b; i-- > 0;) {
          args[i] = std::move(_stack.back());
          _stack.pop_back();
        }
        Value callee = _stack.back();
        _stack.pop_back();
        _interp._callSiteLine = in.line;
        _interp._callSiteColumn = in.column;
        callValue(callee, std::move(args), in.line, in.column);
        break;
      }
      case Op::INDEX: {
        Value idx = _stack.back();
        _stack.pop_back();
        Value cont = _stack.back();
        _stack.pop_back();
        _stack.push_back(indexGet(cont, idx, in.line, in.column));
        break;
      }
      case Op::INDEX_SET: {
        Value val = _stack.back();
        _stack.pop_back();
        Value idx = _stack.back();
        _stack.pop_back();
        Value cont = _stack.back();
        _stack.pop_back();
        indexSet(cont, idx, std::move(val),
                 static_cast<AssignStmt::Operator>(in.a), in.line, in.column);
        break;
      }
      case Op::SLICE: {
        bool hasBegin = (in.a & 1) != 0;
        bool hasEnd = (in.a & 2) != 0;
        Value endV, beginV;
        if (hasEnd) {
          endV = _stack.back();
          _stack.pop_back();
        }
        if (hasBegin) {
          beginV = _stack.back();
          _stack.pop_back();
        }
        Value cont = _stack.back();
        _stack.pop_back();
        _stack.push_back(sliceGet(cont, hasBegin, beginV, hasEnd, endV,
                                  in.line, in.column));
        break;
      }
      case Op::MEMBER_GET: {
        Value obj = _stack.back();
        _stack.pop_back();
        _stack.push_back(
            memberGet(obj, f.chunk->names[in.a], in.line, in.column));
        break;
      }
      case Op::MEMBER_SET: {
        Value val = _stack.back();
        _stack.pop_back();
        Value obj = _stack.back();
        _stack.pop_back();
        memberSet(obj, f.chunk->names[in.a], std::move(val),
                  static_cast<AssignStmt::Operator>(in.b), in.line, in.column);
        break;
      }
      case Op::MAKE_ARRAY: {
        size_t n = static_cast<size_t>(in.a);
        std::vector<Value> elems(n);
        for (size_t i = n; i-- > 0;) {
          elems[i] = std::move(_stack.back());
          _stack.pop_back();
        }
        _interp.noteArrayAllocation(elems.size());
        TypeInfo elemType = elems.empty() ? TypeInfo(DataType::VOID)
                                          : ValueHelper::getType(elems[0]);
        _stack.push_back(ValueHelper::createArray(elemType, elems));
        break;
      }
      case Op::MAKE_MAP: {
        size_t n = static_cast<size_t>(in.a);
        std::vector<std::pair<Value, Value>> entries(n);
        for (size_t i = n; i-- > 0;) {
          entries[i].second = std::move(_stack.back());
          _stack.pop_back();
          entries[i].first = std::move(_stack.back());
          _stack.pop_back();
        }
        TypeInfo keyType(DataType::VOID), valueType(DataType::VOID);
        auto m = ValueHelper::createMap(keyType, valueType);
        bool first = true;
        for (auto &kv : entries) {
          if (first) {
            keyType = ValueHelper::getType(kv.first);
            valueType = ValueHelper::getType(kv.second);
            if (keyType.isArray || keyType.isMap || keyType.isStruct ||
                keyType.baseType == DataType::VOID ||
                keyType.baseType == DataType::NIL) {
              throw vmError("Map keys must be scalar values", in.line,
                            in.column);
            }
            m->keyType = keyType;
            m->valueType = valueType;
            first = false;
          }
          Value k = kv.first, v = kv.second;
          try {
            k = _interp.convertToType(k, keyType);
            v = _interp.convertToType(v, valueType);
          } catch (RuntimeError &) {
            throw;
          } catch (const std::exception &e) {
            throw vmError(e.what(), in.line, in.column);
          }
          _interp.checkArraySize(m->entries.size() + 1);
          m->entries[std::move(k)] = std::move(v);
        }
        _interp.noteArrayAllocation(m->entries.size());
        _stack.push_back(std::move(m));
        break;
      }
      case Op::MAKE_LAMBDA: {
        const VMFunctionPtr &lf = f.chunk->functions[in.a];
        auto fv = std::make_shared<FunctionValue>();
        fv->displayName = lf->name;
        fv->parameters = lf->params;
        fv->body = lf->body;
        fv->hasRetType = lf->hasDeclaredRetType;
        fv->declaredRetType = lf->retType;
        fv->captured = snapshotVars();
        fv->vmFunc = lf;
        _stack.push_back(std::move(fv));
        break;
      }
      case Op::ENUM_MEMBER: {
        const std::string &enumName = f.chunk->names[in.a];
        const std::string &memberName = f.chunk->names[in.b];
        // A struct-valued variable of the same name shadows the enum type.
        bool done = false;
        if (f.env->has(enumName)) {
          Value v = f.env->get(enumName);
          if (ValueHelper::isStruct(v)) {
            StructPtr sv = std::get<StructPtr>(v);
            auto it = sv->fields.find(memberName);
            if (it != sv->fields.end()) {
              _stack.push_back(it->second);
              done = true;
            }
          }
        }
        if (!done) {
          auto eit = _interp._enums.find(enumName);
          if (eit == _interp._enums.end()) {
            throw vmError("Unknown enum '" + enumName + "'", in.line,
                          in.column);
          }
          bool hit = false;
          for (const auto &mm : eit->second->members) {
            if (mm.first == memberName) {
              _stack.push_back(static_cast<int64_t>(mm.second));
              hit = true;
              break;
            }
          }
          if (!hit) {
            throw vmError("Enum '" + enumName + "' has no member '" +
                              memberName + "'",
                          in.line, in.column);
          }
        }
        break;
      }
      case Op::UPDATE_SLOT:
      case Op::UPDATE_NAME:
      case Op::UPDATE_INDEX:
      case Op::UPDATE_MEMBER: {
        bool inc = (in.b & 1) != 0;
        bool prefix = (in.b & 2) != 0;
        auto bump = [&](const Value &old) -> Value {
          try {
            Value delta = static_cast<int32_t>(1);
            return inc ? ValueHelper::add(old, delta)
                       : ValueHelper::subtract(old, delta);
          } catch (RuntimeError &) {
            throw;
          } catch (const std::exception &e) {
            throw vmError(e.what(), in.line, in.column);
          }
        };
        if (in.op == Op::UPDATE_SLOT) {
          Value old = f.slots[in.a];
          Value next = bump(old);
          f.slots[in.a] = next;
          f.env->assign(f.chunk->names[in.c], next);
          _stack.push_back(prefix ? next : old);
        } else if (in.op == Op::UPDATE_NAME) {
          const std::string &nm = f.chunk->names[in.a];
          Value old = readName(nm, in.line, in.column);
          Value next = bump(old);
          writeName(nm, next, in.line, in.column);
          _stack.push_back(prefix ? next : old);
        } else if (in.op == Op::UPDATE_INDEX) {
          Value idx = _stack.back();
          _stack.pop_back();
          Value cont = _stack.back();
          _stack.pop_back();
          if (ValueHelper::isMap(cont)) {
            MapPtr m = std::get<MapPtr>(cont);
            Value key;
            try {
              key = _interp.convertToType(idx, m->keyType);
            } catch (RuntimeError &) {
              throw;
            } catch (const std::exception &e) {
              throw vmError(std::string("Map key: ") + e.what(), in.line,
                            in.column);
            }
            auto it = m->entries.find(key);
            if (it == m->entries.end()) {
              throw vmError("Map key not found", in.line, in.column);
            }
            Value old = it->second;
            Value next = bump(old);
            try {
              it->second = _interp.convertToType(next, m->valueType);
            } catch (RuntimeError &) {
              throw;
            } catch (const std::exception &e) {
              throw vmError(e.what(), in.line, in.column);
            }
            _stack.push_back(prefix ? next : old);
          } else {
            if (!ValueHelper::isArray(cont)) {
              throw vmError("++/-- index target must be an array or map",
                            in.line, in.column);
            }
            auto &elems =
                ValueHelper::arrayElements(const_cast<Value &>(cont));
            int64_t i;
            try {
              i = _interp.normalizeIndex(ValueHelper::toInt64(idx),
                                         elems.size(), in.line, in.column);
            } catch (RuntimeError &) {
              throw;
            } catch (const std::exception &e) {
              throw vmError(e.what(), in.line, in.column);
            }
            Value old = elems[static_cast<size_t>(i)];
            Value next = bump(old);
            TypeInfo elemType = ValueHelper::arrayElementType(cont);
            try {
              elems[static_cast<size_t>(i)] =
                  _interp.convertToType(next, elemType);
            } catch (RuntimeError &) {
              throw;
            } catch (const std::exception &e) {
              throw vmError(e.what(), in.line, in.column);
            }
            _stack.push_back(prefix ? next : old);
          }
        } else { // UPDATE_MEMBER
          Value obj = _stack.back();
          _stack.pop_back();
          if (!ValueHelper::isStruct(obj)) {
            throw vmError("++/-- member target must be a struct", in.line,
                          in.column);
          }
          const std::string &mem = f.chunk->names[in.a];
          StructPtr sv = std::get<StructPtr>(obj);
          auto it = sv->fields.find(mem);
          if (it == sv->fields.end()) {
            throw vmError("Struct '" + sv->typeName + "' has no field '" +
                              mem + "'",
                          in.line, in.column);
          }
          Value old = it->second;
          Value next = bump(old);
          auto declIt = _interp._structs.find(sv->typeName);
          if (declIt != _interp._structs.end()) {
            for (const auto &fd : declIt->second->fields) {
              if (fd.name == mem) {
                try {
                  next = _interp.convertToType(next, fd.type);
                } catch (RuntimeError &) {
                  throw;
                } catch (const std::exception &e) {
                  throw vmError(e.what(), in.line, in.column);
                }
                break;
              }
            }
          }
          it->second = next;
          _stack.push_back(prefix ? next : old);
        }
        break;
      }
      case Op::CONVERT: {
        Value v = _stack.back();
        _stack.pop_back();
        try {
          v = _interp.convertToType(v, f.chunk->types[in.a]);
        } catch (RuntimeError &) {
          throw;
        } catch (const std::exception &e) {
          throw vmError(e.what(), in.line, in.column);
        }
        _stack.push_back(std::move(v));
        break;
      }
      case Op::DEFAULT_VALUE: {
        try {
          _stack.push_back(
              _interp.defaultValue(f.chunk->types[in.a]));
        } catch (RuntimeError &) {
          throw;
        } catch (const std::exception &e) {
          throw vmError(e.what(), in.line, in.column);
        }
        break;
      }
      case Op::STEP:
        _interp.consumeExecutionStep(in.line, in.column);
        break;
      case Op::STEP_DBG:
        _interp.consumeExecutionStep(in.line, in.column);
        if (_interp._debugHook) {
          debugHook(f, in.line, in.column);
        }
        break;
      case Op::PUSH_HANDLER: {
        ActiveHandler h;
        h.tableIdx = static_cast<uint32_t>(in.a);
        h.stackDepth = static_cast<uint32_t>(_stack.size());
        h.scopeDepth = static_cast<uint32_t>(f.scopeMarks.size());
        f.handlers.push_back(h);
        break;
      }
      case Op::ENTER_FINALLY: {
        // Normal completion of try/catch: restore scope/stack, then either
        // jump into the finally block or skip to endPC.
        uint32_t idx = static_cast<uint32_t>(in.a);
        for (auto it = f.handlers.rbegin(); it != f.handlers.rend(); ++it) {
          if (it->tableIdx == idx) {
            const HandlerInfo &hi = f.chunk->handlers[idx];
            if (hi.finallyPC != UINT32_MAX) {
              enterFinally(f, *it, hi);
            } else {
              leaveScopes(f, it->scopeDepth);
              _stack.resize(it->stackDepth);
              f.handlers.erase(std::next(it).base());
              f.pc = hi.endPC;
            }
            break;
          }
        }
        break;
      }
      case Op::FINALLY_END: {
        uint32_t idx = static_cast<uint32_t>(in.a);
        ActiveHandler done;
        bool found = false;
        for (auto it = f.handlers.rbegin(); it != f.handlers.rend(); ++it) {
          if (it->tableIdx == idx) {
            done = *it; // copy: erase invalidates the reference
            f.handlers.erase(std::next(it).base());
            found = true;
            break;
          }
        }
        if (found && done.inFinally &&
            _interp._stepGrace >= Interpreter::kFinallyStepReserve) {
          _interp._stepGrace -= Interpreter::kFinallyStepReserve;
        }
        if (found && done.remapToScriptExc) {
          const HandlerInfo &hi = f.chunk->handlers[idx];
          _pending = Pending();
          _pending.kind = Pending::Kind::SCRIPT_EXC;
          _pending.value = done.remapped;
          _pending.file = f.file;
          _pending.line = hi.line;
          _pending.column = hi.column;
          _pending.proc = f.name;
        } else if (found && done.parked.kind != Pending::Kind::NONE) {
          // Resume the action that was in flight before finally ran.
          _pending = done.parked;
        }
        // Any restored pending unwind resumes on the next loop iteration.
        break;
      }
      case Op::BREAK:
        _pending.kind = Pending::Kind::BRK;
        _pending.targetPC = static_cast<uint32_t>(in.a);
        _pending.scopeDepth = static_cast<uint32_t>(in.b);
        break;
      case Op::CONTINUE:
        _pending.kind = Pending::Kind::CNT;
        _pending.targetPC = static_cast<uint32_t>(in.a);
        _pending.scopeDepth = static_cast<uint32_t>(in.b);
        break;
      case Op::RETURN:
        _pending.kind = Pending::Kind::RET;
        _pending.value = _stack.back();
        _stack.pop_back();
        break;
      case Op::THROW:
        _pending.kind = Pending::Kind::SCRIPT_EXC;
        _pending.value = _stack.back();
        _stack.pop_back();
        _pending.file = f.file;
        _pending.line = in.line;
        _pending.column = in.column;
        _pending.proc = f.name;
        break;
      case Op::FOR_INIT: {
        Value it = _stack.back();
        _stack.pop_back();
        IterState st;
        if (ValueHelper::isArray(it)) {
          st.kind = IterState::Kind::ARRAY;
          st.arr = std::get<ArrayPtr>(it);
        } else if (std::holds_alternative<std::string>(it)) {
          st.kind = IterState::Kind::STRING;
          st.str = std::get<std::string>(it);
        } else if (ValueHelper::isMap(it)) {
          st.kind = IterState::Kind::MAP;
          st.map = std::get<MapPtr>(it);
          st.mapIt = st.map->entries.begin();
        } else {
          throw vmError("for-each requires an array, map, or string",
                        in.line, in.column);
        }
        if (static_cast<size_t>(in.a) >= f.iters.size()) {
          f.iters.resize(static_cast<size_t>(in.a) + 1);
        }
        f.iters[in.a] = std::move(st);
        break;
      }
      case Op::FOR_NEXT: {
        IterState &st = f.iters[in.a];
        bool has = false;
        Value elem;
        switch (st.kind) {
        case IterState::Kind::ARRAY:
          // Live view: elements pushed during iteration are visited.
          has = st.index < st.arr->elements.size();
          if (has) {
            elem = st.arr->elements[st.index++];
          }
          break;
        case IterState::Kind::STRING:
          has = st.index < st.str.size();
          if (has) {
            elem = static_cast<char>(st.str[st.index++]);
          }
          break;
        case IterState::Kind::MAP:
          has = st.mapIt != st.map->entries.end();
          if (has) {
            elem = st.mapIt->first;
            ++st.mapIt;
          }
          break;
        }
        if (!has) {
          f.pc = static_cast<uint32_t>(in.b);
        } else {
          _stack.push_back(std::move(elem));
        }
        break;
      }
      case Op::END_DEFAULT: {
        int32_t pidx = in.a;
        Value v = _stack.back();
        _stack.pop_back();
        const Parameter &p = f.fn->params[pidx];
        if (!p.type.isAuto) {
          try {
            v = _interp.convertToType(v, p.type);
          } catch (RuntimeError &) {
            throw;
          } catch (const std::exception &e) {
            throw RuntimeError(e.what(), f.file, f.callLine, f.callCol,
                               f.name);
          }
        }
        f.slots[pidx] = v;
        f.env->define(p.name, v);
        f.pendingDefaults.erase(f.pendingDefaults.begin());
        if (f.pendingDefaults.empty()) {
          f.pc = f.fn->bodyStart;
          f.inDefaultIdx = -1;
        } else {
          f.inDefaultIdx = f.pendingDefaults.front();
          f.pc = f.fn->defaultEntries[f.inDefaultIdx];
        }
        break;
      }
      case Op::END_PROC: {
        // Falling off the end of the body: void/auto return 0, otherwise the
        // "must return a value" error surfaces in the caller's context.
        VMFunctionPtr fn = f.fn;
        int callLine = f.callLine, callCol = f.callCol;
        if (returnsVoidType(fn->retType) || fn->retType.isAuto) {
          _pending.kind = Pending::Kind::RET;
          _pending.value = static_cast<int32_t>(0);
        } else {
          popFrame();
          _pending.kind = Pending::Kind::RUNTIME_ERR;
          _pending.error = RuntimeError("Non-void procedure must return a "
                                        "value",
                                        _interp._currentFile, callLine,
                                        callCol, _interp._currentProcedure);
        }
        break;
      }
      case Op::HALT:
        _snippetResult = static_cast<int32_t>(0);
        _snippetDone = true;
        popFrame();
        break;
      }
    } catch (const ScriptException &se) {
      _pending.kind = Pending::Kind::SCRIPT_EXC;
      _pending.value = se.value;
      _pending.file = se.file;
      _pending.line = se.line;
      _pending.column = se.column;
      _pending.proc = se.procedure;
    } catch (RuntimeError &e) {
      _pending.kind = Pending::Kind::RUNTIME_ERR;
      _pending.error = e;
    } catch (const std::exception &e) {
      Frame &cf = _frames.back();
      std::string msg = e.what();
      if (cf.inDefaultIdx >= 0) {
        msg = "Default argument for '" +
              cf.fn->params[cf.inDefaultIdx].name + "': " + msg;
      }
      _pending.kind = Pending::Kind::RUNTIME_ERR;
      _pending.error = RuntimeError(msg, cf.file, cf.callLine, cf.callCol,
                                    cf.name);
    }
  }

  if (kCount) {
    std::fprintf(stderr, "[vm] instructions executed: %llu\n",
                 static_cast<unsigned long long>(icount));
  }
  // Flush any remaining pending state at this run boundary.
  switch (_pending.kind) {
  case Pending::Kind::RET:
    _pending.kind = Pending::Kind::NONE;
    return _pending.value;
  case Pending::Kind::SCRIPT_EXC: {
    ScriptException se(_pending.value, _pending.file, _pending.line,
                       _pending.column, _pending.proc);
    _pending.kind = Pending::Kind::NONE;
    throw se;
  }
  case Pending::Kind::RUNTIME_ERR: {
    RuntimeError e = _pending.error;
    _pending.kind = Pending::Kind::NONE;
    throw e;
  }
  case Pending::Kind::BRK:
  case Pending::Kind::CNT:
    _pending.kind = Pending::Kind::NONE;
    throw RuntimeError("Internal: break/continue outside loop", "", 0, 0);
  case Pending::Kind::NONE:
    break;
  }
  return _snippetDone ? _snippetResult : Value(static_cast<int32_t>(0));
}

} // namespace Script
