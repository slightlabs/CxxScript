#pragma once

#include "AST.h"
#include "DataTypes.h"
#include "Interpreter.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Script {

// ---------------------------------------------------------------------------
// Bytecode execution engine.
//
// Procedures and lambdas are compiled to bytecode chunks executed by a stack
// machine. Variable references that can be resolved lexically at compile time
// become slot accesses; everything else falls back to the same dynamic
// environment walk the tree-walking interpreter performs (caller frames,
// captured variables, globals, external variables, procedure references),
// preserving CxxScript's dynamic-scope semantics exactly.
//
// The VM shares the interpreter's semantic core: arithmetic/comparison via
// ValueHelper, argument conversion via Interpreter::convertToType, builtins
// via Builtins::call, and the identical guardrails (call depth, step count,
// native stack, memory limits), debug hook, and stack-trace format.
// ---------------------------------------------------------------------------

enum class Op : uint8_t {
  NOP,
  CONST,          // a = constants index -> push
  LOAD_SLOT,      // a = slot -> push slots[a]
  STORE_SLOT,     // a = slot -> pop into slots[a]
  DECL_SLOT,      // a = slot, b = name index -> pop, declare local (slot+name)
  LOAD_NAME,      // a = name index -> dynamic lookup -> push
  STORE_NAME,     // a = name index -> pop, dynamic write
  DEFINE_NAME,    // a = name index -> pop, env->define (top-level/snippet)
  ENTER_SCOPE,    // push scope mark
  LEAVE_SCOPE,    // pop scope mark, roll back dynamic name bindings
  POP,            // pop 1
  DUP,            // duplicate top
  JUMP,           // a = target pc
  JUMP_FALSE,     // a = target pc; pop cond, jump if !toBool
  JUMP_TRUE,      // a = target pc; pop cond, jump if toBool
  TOBOOL,         // pop -> push bool(toBool)
  // Binary ops (pop rhs, lhs -> push result). Order matches BinaryExpr::Operator.
  ADD,
  SUB,
  MUL,
  DIV,
  MOD,
  EQ,
  NE,
  LT,
  GT,
  LE,
  GE,
  BAND,
  BOR,
  BXOR,
  SHL,
  SHR,
  NEG,            // unary minus
  NOT,            // logical not
  BNOT,           // bitwise not
  ASSIGN_OP,      // a = AssignStmt::Operator (compound): cur v -> applyAssignOp
  ERROR,          // a = constants index (string): raise RuntimeError
  CONCAT_N,       // a = count: pop n values, toString each, concat -> push
  CALL_RESOLVE,   // a = name index, c = call-site index: resolve callee kind
  CALL,           // c = call-site index, b = argc: dispatch resolved callee
  CALL_VALUE,     // b = argc; stack: callee arg1..argN -> push result
  INDEX,          // stack: container index -> push element
  INDEX_SET,      // a = AssignStmt::Operator; stack: container index value
  SLICE,          // a = flags (1=hasBegin 2=hasEnd); stack: container [b] [e]
  MEMBER_GET,     // a = name index; stack: object -> push member/bound method
  MEMBER_SET,     // a = name index, b = AssignStmt::Operator; stack: obj value
  MAKE_ARRAY,     // a = count -> pop n -> push array
  MAKE_MAP,       // a = pair count -> pop 2n -> push map
  MAKE_LAMBDA,    // a = functions index -> push FunctionValue (env snapshot)
  ENUM_MEMBER,    // a = enum name idx, b = member name idx -> push int64
  UPDATE_SLOT,    // a = slot, b = flags (1=inc, 2=prefix) -> push old/new
  UPDATE_NAME,    // a = name idx, b = flags -> push old/new
  UPDATE_INDEX,   // b = flags; stack: container index -> push old/new
  UPDATE_MEMBER,  // a = name idx, b = flags; stack: object -> push old/new
  CONVERT,        // a = types index; pop -> convert -> push
  DEFAULT_VALUE,  // a = types index -> push default value for type
  STEP,           // consume one execution step (no debug hook)
  STEP_DBG,       // consume one execution step + debug hook
  PUSH_HANDLER,   // a = handler table index -> activate
  ENTER_FINALLY,  // a = handler table index: normal path enters finally block
  FINALLY_END,    // a = handler table index: finally block completed
  BREAK,          // a = target pc, b = scope depth -> unwind across finallys
  CONTINUE,       // a = target pc, b = scope depth
  RETURN,         // pop value -> unwind frame (finallys run first)
  THROW,          // pop value -> ScriptException unwind
  FOR_INIT,       // a = iterator index; pop iterable -> iterator state
  FOR_NEXT,       // a = iter index, b = end pc; push next elem or jump
  END_DEFAULT,    // a = param idx: pop -> convert -> bind param -> next default
  END_PROC,       // implicit end of body: void -> 0, else error
  HALT            // end of snippet chunk
};

struct Instruction {
  Op op = Op::NOP;
  int32_t a = 0;
  int32_t b = 0;
  int32_t c = 0;
  int32_t line = 0;
  int32_t column = 0;
};

// Per-call-site inline cache (mirrors CallExpr::cached* in the interpreter).
struct CallSite {
  uint64_t version = 0;
  bool isProcedure = false;
  bool isExternal = false;
  std::weak_ptr<ProcedureDecl> proc;
  ExternalFunctionCallback external;
  // Resolution performed before argument evaluation (mirrors evaluateCall's
  // ordering: dispatch errors precede arg side effects).
  enum class Kind { NONE, VAR_FUNC, PROC, EXT_VAR, EXTERN, STRUCT_CTOR,
                    BUILTIN } kind = Kind::NONE;
  // Resolved callees are borrowed, not owned: function values ride the
  // operand stack between CALL_RESOLVE and CALL (so nothing here keeps a
  // FunctionValue/ProcedureDecl alive — strong refs would close
  // shared_ptr cycles through ProcedureDecl::vmFunc and leak chunks).
  std::weak_ptr<ProcedureDecl> procDecl;
  std::weak_ptr<StructDecl> structDecl;
  // Pooled argument wrappers for BUILTIN dispatch — avoids allocating a
  // CallExpr + LiteralExpr per call; values are overwritten in place.
  std::shared_ptr<CallExpr> builtinExpr;
  std::vector<std::shared_ptr<LiteralExpr>> builtinArgs;
};

// Static exception-handler description for one try/catch/finally statement.
// The protected region is [tryBegin, finallyPC): the try body plus the catch
// body (finally code itself starts at finallyPC and is unprotected by this
// handler).
struct HandlerInfo {
  uint32_t tryBegin = 0;
  uint32_t catchPC = UINT32_MAX;   // UINT32_MAX = no catch clause
  uint32_t finallyPC = UINT32_MAX; // UINT32_MAX = no finally block
  uint32_t endPC = 0;              // pc after the entire statement
  uint32_t regionEnd = 0;          // end of protected code (== finallyPC|endPC)
  int32_t catchSlot = -1;          // slot for the caught value, -1 = none
  int32_t catchNameIdx = -1;       // names index for the catch variable
  int32_t catchTypeIdx = -1;       // types index, -1 = auto/no conversion
  int32_t line = 0;                // try statement's source position
  int32_t column = 0;
};

struct BytecodeChunk {
  std::vector<Instruction> code;
  std::vector<Value> constants;
  std::vector<std::string> names;
  std::vector<std::shared_ptr<struct VMFunction>> functions;
  std::vector<TypeInfo> types;
  std::vector<HandlerInfo> handlers;
  std::vector<CallSite> callSites;
  uint16_t numSlots = 0;
};

// A compiled function: procedure or lambda body. Procedures keep a back-link
// to their declaration so hot reload drops the code with the decl.
struct VMFunction {
  std::string name;
  std::string file;
  std::vector<Parameter> params;
  TypeInfo retType;
  BytecodeChunk chunk;
  // Default-argument code entry points inside `chunk`, indexed by parameter
  // position (UINT32_MAX = no default). Each ends with END_DEFAULT.
  std::vector<uint32_t> defaultEntries;
  uint32_t bodyStart = 0;   // pc where the function body begins
  StmtPtr body;             // original AST body (interop with the tree-walker)
  bool hasDeclaredRetType = false; // lambda `-> T` present
  // Weak back-link to the declaring procedure (decl owns the compiled form
  // via ProcedureDecl::vmFunc — a strong ref would create a cycle).
  std::weak_ptr<ProcedureDecl> decl;
};

using VMFunctionPtr = std::shared_ptr<VMFunction>;

class VM {
public:
  explicit VM(Interpreter &interp) : _interp(interp) {}

  // Compile a procedure's body (and nested lambdas/defaults) to bytecode.
  // Returns false if the body uses a construct the compiler does not cover;
  // callers should then fall back to the tree-walking interpreter.
  bool compileProcedure(const ProcedureDeclPtr &proc);

  // Entry points mirroring the interpreter's public API.
  Value callProc(const ProcedureDeclPtr &proc, std::vector<Value> args,
                 int line, int column,
                 const std::unordered_map<std::string, Value> *captures);
  Value callFunction(const FuncPtr &fn, std::vector<Value> args, int line,
                     int column);
  Value runSnippet(const std::vector<StmtPtr> &statements);

private:
  friend class BytecodeCompiler;

  Interpreter &_interp;

  // for-each iterator state (lives off the value stack).
  struct IterState {
    enum class Kind { ARRAY, STRING, MAP } kind;
    ArrayPtr arr;
    std::string str;
    MapPtr map;
    std::map<Value, Value>::iterator mapIt;
    size_t index = 0;
  };

  // Pending unwind action while finally blocks run.
  struct Pending {
    enum class Kind { NONE, RET, BRK, CNT, SCRIPT_EXC, RUNTIME_ERR } kind =
        Kind::NONE;
    Value value;                      // return value / thrown value
    uint32_t targetPC = 0;            // break/continue landing pc
    uint32_t scopeDepth = 0;          // scope depth at landing
    std::string file;                 // ScriptException fields
    int line = 0;
    int column = 0;
    std::string proc;
    RuntimeError error;               // RUNTIME_ERR payload
    Pending() : value(int32_t(0)), error("", "", 0, 0) {}
  };

  // Active try handler on a frame.
  struct ActiveHandler {
    uint32_t tableIdx;
    bool inCatch = false;
    bool inFinally = false;
    uint32_t stackDepth = 0; // operand-stack depth at PUSH_HANDLER
    uint32_t scopeDepth = 0; // scope-mark depth at PUSH_HANDLER
    // try/finally-without-catch rethrows as a ScriptException carrying the
    // mapped thrown value once finally completes (interpreter parity).
    bool remapToScriptExc = false;
    Value remapped;
    // The in-flight unwind action is parked here while the finally block's
    // bytecode executes; FINALLY_END restores it into _pending.
    Pending parked;
  };

  struct Frame {
    VMFunctionPtr fn; // keepalive
    BytecodeChunk *chunk = nullptr;
    uint32_t pc = 0;
    // Default-argument mini-programs pending evaluation (trailing params).
    std::vector<int32_t> pendingDefaults;
    // Locals: statically-resolved accesses use `slots`; every declare/assign
    // is mirrored into `env` so the Environment parent chain reproduces the
    // interpreter's dynamic scoping and stays coherent for tree-walker
    // interop (AST-only callees, debug snapshots, lambda captures).
    std::vector<Value> slots;
    Interpreter::Environment *env = nullptr;
    std::unique_ptr<Interpreter::Environment> ownedEnv;
    std::unique_ptr<Interpreter::Environment> ownedCapEnv; // captures layer
    std::vector<size_t> scopeMarks; // count of open env scopes
    // Saved interpreter state restored on frame exit (invokeCallable parity).
    std::string prevProcedure;
    std::string prevFile;
    int prevCallLine = 0;
    int prevCallCol = 0;
    Interpreter::Environment *prevEnv = nullptr;
    std::string name;
    std::string file;
    size_t stackBase = 0; // operand-stack base
    std::vector<ActiveHandler> handlers;
    std::vector<IterState> iters;
    int callLine = 0;
    int callCol = 0;
    bool isSnippet = false;
    int32_t inDefaultIdx = -1; // param idx whose default program is running
  };

  std::vector<Frame> _frames;
  std::vector<Value> _stack;
  Pending _pending;
  Value _snippetResult;
  bool _snippetDone = false;
  bool _topLevel = false; // whether this VM run owns _executionActive

  Value run(size_t base); // dispatch loop until _frames shrinks to `base`

  // Frame setup/teardown mirroring Interpreter::invokeCallable.

  // Unwind machinery: deliver _pending through handlers/frames.
  void unwind(size_t base); // called when _pending.kind != NONE
  void popFrame();
  // Jump into a handler's finally block: parks _pending on the handler and
  // grants the bounded step reserve (released when the handler is dropped).
  void enterFinally(Frame &f, ActiveHandler &h, const HandlerInfo &hi);
  void dropHandler(Frame &f); // pop f.handlers.back(), releasing step reserve

  // Variable access reproducing readVariable/writeVariable.
  Value readName(const std::string &name, int line, int column);
  void writeName(const std::string &name, const Value &value, int line,
                 int column);
  void leaveScopes(Frame &f, size_t depth);
  std::unordered_map<std::string, Value> snapshotVars();

  // Operation helpers (value-level ports of evaluate*/execute* bodies).
  Value indexGet(const Value &container, const Value &index, int line,
                 int column);
  Value sliceGet(const Value &container, bool hasBegin, const Value &begin,
                 bool hasEnd, const Value &end, int line, int column);
  Value memberGet(const Value &object, const std::string &member, int line,
                  int column);
  void indexSet(const Value &container, const Value &index, Value value,
                AssignStmt::Operator op, int line, int column);
  void memberSet(const Value &object, const std::string &member, Value value,
                 AssignStmt::Operator op, int line, int column);
  void resolveCallSite(Frame &f, const std::string &name, CallSite &site,
                       int line, int column);
  // Pushes the result on the operand stack, or pushes a callee frame (the
  // RETURN op then pushes the result when it fires).
  void performCall(Frame &f, const std::string &name, CallSite &site,
                   int32_t argc, int line, int column);
  void callValue(const Value &callee, std::vector<Value> args, int line,
                 int column);
  void enterCall(const VMFunctionPtr &fn, std::vector<Value> args,
                 const std::unordered_map<std::string, Value> *captures,
                 int line, int column);
  // Interop: run an AST-body callable through the tree-walking interpreter
  // with a materialized environment chain (compile-failure fallback and
  // interpreter-created function values).
  Value callViaInterpreter(const FuncPtr &fn, std::vector<Value> args,
                           int line, int column);
  std::vector<std::unique_ptr<Interpreter::Environment>> _scratchEnvs;
  Value binaryOp(Op op, const Value &l, const Value &r, int line, int column);
  void debugHook(Frame &f, int line, int column);
  RuntimeError vmError(const std::string &msg, int line, int column);
};

// Compiles AST bodies to BytecodeChunks. Returns false on any construct it
// cannot lower; callers fall back to the tree-walking interpreter.
class BytecodeCompiler {
public:
  // Compile a procedure/lambda body. `globalMode` compiles top-level
  // statements whose variable declarations bind into the REPL environment.
  static VMFunctionPtr compileFunction(const std::string &name,
                                       const std::vector<Parameter> &params,
                                       StmtPtr body, const TypeInfo &retType,
                                       const std::string &file);
  static BytecodeChunk compileSnippet(const std::vector<StmtPtr> &statements);

private:
  struct Scope {
    std::unordered_map<std::string, int32_t> vars; // name -> slot
    size_t declMark = 0;
  };
  struct LoopCtx {
    std::vector<int32_t> breakSites; // instr indices to patch to loop end
    std::vector<int32_t> contSites;  // patch to continue target
    size_t scopeDepth = 0;
    bool isSwitch = false;
  };

  BytecodeChunk _chunk;
  std::vector<Scope> _scopes;
  std::vector<LoopCtx> _loops;
  int32_t _nextSlot = 0;
  int32_t _iterCount = 0;
  bool _globalMode = false;
  bool _ok = true;

  int32_t emit(Op op, int32_t a, int32_t b, int32_t c, int line, int col);
  int32_t addConst(const Value &v);
  int32_t addName(const std::string &n);
  int32_t addType(const TypeInfo &t);
  int32_t addFunction(const VMFunctionPtr &f);
  int32_t addHandler(const HandlerInfo &h);
  void patchJump(int32_t instrIdx, int32_t target);
  void patchOperandA(int32_t instrIdx, int32_t value) {
    _chunk.code[instrIdx].a = value;
  }

  int32_t resolveLocal(const std::string &name); // -1 = dynamic
  int32_t allocSlot();
  void pushScope();
  void popScope(); // emits LEAVE_SCOPE
  void declareVar(const std::string &name, int32_t slot, int line, int col);
  void emitAssignOp(int32_t opIdx, int line, int col); // compound apply

  void compileStmt(StmtPtr stmt);
  void compileExpr(ExprPtr expr);
  void compileBlock(BlockStmt *stmt, bool scoped);
  void compileVarDecl(VarDeclStmt *stmt);
  void compileAssign(AssignStmt *stmt);
  void compileIndexAssign(IndexAssignStmt *stmt);
  void compileMemberAssign(MemberAssignStmt *stmt);
  void compileIf(IfStmt *stmt);
  void compileWhile(WhileStmt *stmt);
  void compileFor(ForStmt *stmt);
  void compileDoWhile(DoWhileStmt *stmt);
  void compileSwitch(SwitchStmt *stmt);
  void compileForEach(ForEachStmt *stmt);
  void compileTryCatch(TryCatchStmt *stmt);
  void compileUpdateTarget(ExprPtr target, bool inc, bool prefix, int line,
                           int col);
  void compileCall(CallExpr *expr);
  VMFunctionPtr compileLambda(LambdaExpr *expr);
};

} // namespace Script
