#pragma once

#include "DataTypes.h"
#include "Token.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Script {

// Forward declarations
class ASTNode;
class Expression;
class Statement;

using ASTNodePtr = std::shared_ptr<ASTNode>;
using ExprPtr = std::shared_ptr<Expression>;
using StmtPtr = std::shared_ptr<Statement>;
using ExternalFunctionCallback =
  std::function<Value(const std::vector<Value> &)>;

// Base AST Node
class ASTNode {
public:
  int line;
  int column;

  ASTNode(int ln = 0, int col = 0) : line(ln), column(col) {}
  virtual ~ASTNode() = default;
};

// Expression Nodes
class Expression : public ASTNode {
public:
  using ASTNode::ASTNode;
  virtual ~Expression() = default;
};

class LiteralExpr : public Expression {
public:
  Value value;
  TypeInfo type;

    LiteralExpr(const Value &val, TypeInfo t, int ln = 0, int col = 0)
      : Expression(ln, col), value(val), type(t) {}
};

class VariableExpr : public Expression {
public:
  std::string name;

  VariableExpr(const std::string &n, int ln = 0, int col = 0)
      : Expression(ln, col), name(n) {}
};

class BinaryExpr : public Expression {
public:
  enum class Operator {
    ADD,
    SUBTRACT,
    MULTIPLY,
    DIVIDE,
    MODULO,
    EQUAL,
    NOT_EQUAL,
    LESS_THAN,
    GREATER_THAN,
    LESS_EQUAL,
    GREATER_EQUAL,
    LOGICAL_AND,
    LOGICAL_OR,
    BIT_AND,
    BIT_OR,
    BIT_XOR,
    LSHIFT,
    RSHIFT
  };

  ExprPtr left;
  ExprPtr right;
  Operator op;

  BinaryExpr(ExprPtr l, ExprPtr r, Operator o, int ln = 0, int col = 0)
      : Expression(ln, col), left(l), right(r), op(o) {}
};

class UnaryExpr : public Expression {
public:
  enum class Operator { NEGATE, LOGICAL_NOT, BIT_NOT };

  ExprPtr operand;
  Operator op;

  UnaryExpr(ExprPtr expr, Operator o, int ln = 0, int col = 0)
      : Expression(ln, col), operand(expr), op(o) {}
};

// Increment/decrement on a variable or index target: x++, ++x, arr[i]--
class UpdateExpr : public Expression {
public:
  ExprPtr target;   // VariableExpr or IndexExpr
  bool increment;   // ++ when true, -- when false
  bool prefix;      // ++x when true, x++ when false

  UpdateExpr(ExprPtr t, bool inc, bool pre, int ln = 0, int col = 0)
      : Expression(ln, col), target(t), increment(inc), prefix(pre) {}
};

class CallExpr : public Expression {
public:
  std::string functionName;
  std::vector<ExprPtr> arguments;
  // When set, the callee is an arbitrary expression evaluating to a function
  // value or a struct method target (e.g. `getHandler()(x)`, `obj.m(x)`);
  // functionName is then informational only.
  ExprPtr calleeExpr;

  // Inline cache for call dispatch
  mutable uint64_t cacheVersion = 0;
  mutable bool cachedIsProcedure = false;
  mutable bool cachedIsExternal = false;
  mutable std::weak_ptr<class ProcedureDecl> cachedProcedure;
  mutable ExternalFunctionCallback cachedExternal;

  CallExpr(const std::string &name, const std::vector<ExprPtr> &args,
           int ln = 0, int col = 0)
      : Expression(ln, col), functionName(name), arguments(args) {}

  CallExpr(ExprPtr callee, const std::vector<ExprPtr> &args, int ln = 0,
           int col = 0)
      : Expression(ln, col), arguments(args), calleeExpr(std::move(callee)) {}
};

class ConditionalExpr : public Expression {
public:
  ExprPtr condition;
  ExprPtr thenExpr;
  ExprPtr elseExpr;

  ConditionalExpr(ExprPtr cond, ExprPtr t, ExprPtr e, int ln = 0, int col = 0)
      : Expression(ln, col), condition(cond), thenExpr(t), elseExpr(e) {}
};

class ArrayLiteralExpr : public Expression {
public:
  std::vector<ExprPtr> elements;

  ArrayLiteralExpr(const std::vector<ExprPtr> &elems, int ln = 0, int col = 0)
      : Expression(ln, col), elements(elems) {}
};

// Map literal, e.g. {"a": 1, "b": 2}
class MapLiteralExpr : public Expression {
public:
  std::vector<std::pair<ExprPtr, ExprPtr>> entries; // key, value

  MapLiteralExpr(std::vector<std::pair<ExprPtr, ExprPtr>> e, int ln = 0,
                 int col = 0)
      : Expression(ln, col), entries(std::move(e)) {}
};

class IndexExpr : public Expression {
public:
  ExprPtr arrayExpr;
  ExprPtr indexExpr;            // null for slices like arr[:end]
  ExprPtr endIndex;             // non-null for slices arr[begin:end]
  bool isSlice = false;

  IndexExpr(ExprPtr arr, ExprPtr idx, int ln = 0, int col = 0)
      : Expression(ln, col), arrayExpr(arr), indexExpr(idx) {}

  IndexExpr(ExprPtr arr, ExprPtr begin, ExprPtr end, bool slice, int ln = 0,
            int col = 0)
      : Expression(ln, col), arrayExpr(arr), indexExpr(begin),
        endIndex(end), isSlice(slice) {}
};

// Member access on a struct value: obj.field
class MemberExpr : public Expression {
public:
  ExprPtr object;
  std::string member;

  MemberExpr(ExprPtr obj, const std::string &m, int ln = 0, int col = 0)
      : Expression(ln, col), object(obj), member(m) {}
};

// Function/lambda parameter (also used for struct fields and enum entries).
struct Parameter {
  TypeInfo type;
  std::string name;
  ExprPtr defaultValue; // optional default argument expression
};

// Lambda expression: fn(type p, ...) { body } or fn(type p, ...) -> T { ... }.
// Captures the visible environment by value at creation.
class LambdaExpr : public Expression {
public:
  std::vector<Parameter> parameters; // type.isAuto => dynamic param
  StmtPtr body;
  bool hasRetType = false;
  TypeInfo declaredRetType;

  LambdaExpr(std::vector<Parameter> params, StmtPtr b, TypeInfo ret,
             bool hasRet, int ln = 0, int col = 0)
      : Expression(ln, col), parameters(std::move(params)), body(b),
        hasRetType(hasRet), declaredRetType(ret) {}
};

// Enum member reference: Color.RED
class EnumMemberExpr : public Expression {
public:
  std::string enumName;
  std::string memberName;

  EnumMemberExpr(const std::string &e, const std::string &m, int ln = 0,
                 int col = 0)
      : Expression(ln, col), enumName(e), memberName(m) {}
};

// Interpolated string literal, e.g. "name=${name}, id=${id + 1}"
class InterpolatedStringExpr : public Expression {
public:
  struct Part {
    bool isExpr;
    std::string text;
    ExprPtr expr;
  };

  std::vector<Part> parts;

  InterpolatedStringExpr(std::vector<Part> p, int ln = 0, int col = 0)
      : Expression(ln, col), parts(std::move(p)) {}
};

// Statement Nodes
class Statement : public ASTNode {
public:
  using ASTNode::ASTNode;
  virtual ~Statement() = default;
};

class ExpressionStmt : public Statement {
public:
  ExprPtr expression;

  ExpressionStmt(ExprPtr expr, int ln = 0, int col = 0)
      : Statement(ln, col), expression(expr) {}
};

class VarDeclStmt : public Statement {
public:
  TypeInfo type; // type.isAuto => deduced from initializer at compile time
  std::string name;
  ExprPtr initializer;
  bool isConst;

  VarDeclStmt(TypeInfo t, const std::string &n, ExprPtr init, int ln = 0,
              int col = 0, bool isConst = false)
      : Statement(ln, col), type(t), name(n), initializer(init),
        isConst(isConst) {}
};

class AssignStmt : public Statement {
public:
  enum class Operator {
    ASSIGN,
    PLUS_ASSIGN,
    MINUS_ASSIGN,
    MULT_ASSIGN,
    DIV_ASSIGN,
    MOD_ASSIGN,
    BAND_ASSIGN,
    BOR_ASSIGN,
    BXOR_ASSIGN,
    SHL_ASSIGN,
    SHR_ASSIGN
  };

  std::string variableName;
  ExprPtr value;
  Operator op;

  AssignStmt(const std::string &var, ExprPtr val, Operator o, int ln = 0,
             int col = 0)
      : Statement(ln, col), variableName(var), value(val), op(o) {}
};

class IndexAssignStmt : public Statement {
public:
  ExprPtr arrayExpr;
  ExprPtr indexExpr;
  ExprPtr value;
  AssignStmt::Operator op;

  IndexAssignStmt(ExprPtr arr, ExprPtr idx, ExprPtr val,
                  AssignStmt::Operator o = AssignStmt::Operator::ASSIGN,
                  int ln = 0, int col = 0)
      : Statement(ln, col), arrayExpr(arr), indexExpr(idx), value(val), op(o) {}
};

// Field write on a struct value: obj.field = expr, obj.field += expr, ...
class MemberAssignStmt : public Statement {
public:
  ExprPtr object;
  std::string member;
  ExprPtr value;
  AssignStmt::Operator op;

  MemberAssignStmt(ExprPtr obj, const std::string &m, ExprPtr val,
                   AssignStmt::Operator o = AssignStmt::Operator::ASSIGN,
                   int ln = 0, int col = 0)
      : Statement(ln, col), object(obj), member(m), value(val), op(o) {}
};

// for (type name : iterable) { ... }
class ForEachStmt : public Statement {
public:
  TypeInfo elemType;
  std::string varName;
  ExprPtr iterable;
  StmtPtr body;
  bool elemConst;

  ForEachStmt(TypeInfo t, const std::string &name, ExprPtr it, StmtPtr b,
              int ln = 0, int col = 0, bool elemConst = false)
      : Statement(ln, col), elemType(t), varName(name), iterable(it), body(b),
        elemConst(elemConst) {}
};

class BlockStmt : public Statement {
public:
  std::vector<StmtPtr> statements;

  BlockStmt(const std::vector<StmtPtr> &stmts, int ln = 0, int col = 0)
      : Statement(ln, col), statements(stmts) {}
};

class IfStmt : public Statement {
public:
  ExprPtr condition;
  StmtPtr thenBranch;
  StmtPtr elseBranch;

  IfStmt(ExprPtr cond, StmtPtr thenB, StmtPtr elseB, int ln = 0, int col = 0)
      : Statement(ln, col), condition(cond), thenBranch(thenB),
        elseBranch(elseB) {}
};

class WhileStmt : public Statement {
public:
  ExprPtr condition;
  StmtPtr body;

  WhileStmt(ExprPtr cond, StmtPtr b, int ln = 0, int col = 0)
      : Statement(ln, col), condition(cond), body(b) {}
};

class ForStmt : public Statement {
public:
  StmtPtr initializer;
  ExprPtr condition;
  StmtPtr increment;
  StmtPtr body;

  ForStmt(StmtPtr init, ExprPtr cond, StmtPtr inc, StmtPtr b, int ln = 0,
          int col = 0)
      : Statement(ln, col), initializer(init), condition(cond), increment(inc),
        body(b) {}
};

class ReturnStmt : public Statement {
public:
  ExprPtr value;

  ReturnStmt(ExprPtr val, int ln = 0, int col = 0)
      : Statement(ln, col), value(val) {}
};

class BreakStmt : public Statement {
public:
  BreakStmt(int ln = 0, int col = 0) : Statement(ln, col) {}
};

class ContinueStmt : public Statement {
public:
  ContinueStmt(int ln = 0, int col = 0) : Statement(ln, col) {}
};

struct SwitchCase {
  ExprPtr matchExpr; // nullptr for default
  std::vector<StmtPtr> statements;
  bool isDefault;
};

class SwitchStmt : public Statement {
public:
  ExprPtr expression;
  std::vector<SwitchCase> cases;

  SwitchStmt(ExprPtr expr, const std::vector<SwitchCase> &cs, int ln = 0,
             int col = 0)
      : Statement(ln, col), expression(expr), cases(cs) {}
};

class DoWhileStmt : public Statement {
public:
  StmtPtr body;
  ExprPtr condition;

  DoWhileStmt(StmtPtr b, ExprPtr cond, int ln = 0, int col = 0)
      : Statement(ln, col), body(b), condition(cond) {}
};

// throw expr;
class ThrowStmt : public Statement {
public:
  ExprPtr value;

  ThrowStmt(ExprPtr v, int ln = 0, int col = 0)
      : Statement(ln, col), value(v) {}
};

// try { } catch ([type] e) { } [finally { }]
// catchVar empty => plain `catch { }` (no binding).
class TryCatchStmt : public Statement {
public:
  StmtPtr tryBlock;
  TypeInfo catchType;    // isAuto (or absent var) => no conversion
  std::string catchVar;
  StmtPtr catchBlock;    // null when only finally is present
  StmtPtr finallyBlock;  // optional

  TryCatchStmt(StmtPtr t, TypeInfo ct, const std::string &cv, StmtPtr cb,
               StmtPtr fb, int ln = 0, int col = 0)
      : Statement(ln, col), tryBlock(t), catchType(ct), catchVar(cv),
        catchBlock(cb), finallyBlock(fb) {}
};

// Procedure Declaration
struct VMFunction;

class ProcedureDecl : public ASTNode {
public:
  TypeInfo returnType;
  std::string name;
  std::vector<Parameter> parameters;
  StmtPtr body;
  // Lazily-compiled bytecode form of this procedure (filled by the VM).
  std::shared_ptr<VMFunction> vmFunc;

  ProcedureDecl(TypeInfo retType, const std::string &n,
                const std::vector<Parameter> &params, StmtPtr b, int ln = 0,
                int col = 0)
      : ASTNode(ln, col), returnType(retType), name(n), parameters(params),
        body(b) {}
};

using ProcedureDeclPtr = std::shared_ptr<ProcedureDecl>;

// struct Name { type field; retType method(params) { ... } ... }
// fields reuse Parameter (type + name); methods are procedures bound to a
// `this` value on invocation.
class StructDecl : public ASTNode {
public:
  std::string name;
  std::vector<Parameter> fields;
  std::vector<ProcedureDeclPtr> methods;

  StructDecl(const std::string &n, const std::vector<Parameter> &f, int ln = 0,
             int col = 0)
      : ASTNode(ln, col), name(n), fields(f) {}
};

using StructDeclPtr = std::shared_ptr<StructDecl>;

// enum Name { A, B = 4, C } — members are int64 constants accessed as
// Name.A; values auto-increment from the previous member (default 0).
class EnumDecl : public ASTNode {
public:
  std::string name;
  std::vector<std::pair<std::string, int64_t>> members;

  EnumDecl(const std::string &n,
           std::vector<std::pair<std::string, int64_t>> m, int ln = 0,
           int col = 0)
      : ASTNode(ln, col), name(n), members(std::move(m)) {}
};

using EnumDeclPtr = std::shared_ptr<EnumDecl>;

// Runtime representation of a function value: either a lambda (params +
// body + captured variables) or a reference to a named procedure.
struct FunctionValue {
  std::string displayName;
  std::vector<Parameter> parameters;   // empty when wrapping a procedure
  StmtPtr body;                        // null when wrapping a procedure
  TypeInfo declaredRetType;            // meaningful iff hasRetType
  bool hasRetType = false;
  std::unordered_map<std::string, Value> captured; // lambda captures (by copy)
  // Procedure references and bound methods (may be an overload set).
  std::vector<ProcedureDeclPtr> procs;
  // Compiled form when this lambda was created by the bytecode VM.
  std::shared_ptr<VMFunction> vmFunc;

  TypeInfo signature() const {
    std::vector<TypeInfo> params;
    if (!procs.empty()) {
      for (const auto &p : procs.front()->parameters) {
        params.push_back(p.type);
      }
      return TypeInfo::functionOf(
          std::move(params),
          std::make_shared<TypeInfo>(procs.front()->returnType));
    }
    for (const auto &p : parameters) {
      params.push_back(p.type);
    }
    return TypeInfo::functionOf(
        std::move(params),
        hasRetType ? std::make_shared<TypeInfo>(declaredRetType) : nullptr);
  }
};

// Script (collection of procedures)
class Script {
public:
  std::string filename;
  std::vector<ProcedureDeclPtr> procedures;
  std::vector<StructDeclPtr> structs;
  std::vector<EnumDeclPtr> enums;
  // import "path"; directives: (path, line)
  std::vector<std::pair<std::string, int>> imports;

  Script(const std::string &file) : filename(file) {}
};

using ScriptPtr = std::shared_ptr<Script>;

} // namespace Script
