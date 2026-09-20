#include "Formatter.h"
#include "Lexer.h"
#include "Parser.h"
#include <algorithm>
#include <sstream>

namespace Script {
namespace {

// A comment lifted out of the source text before lexing (the lexer drops
// them); re-emitted ahead of the node whose line follows it.
struct Comment {
  int line;
  std::string text;
};

std::vector<Comment> collectComments(const std::string &src) {
  std::vector<Comment> out;
  int line = 1;
  bool inStr = false, inChar = false;
  for (size_t i = 0; i < src.size(); ++i) {
    char c = src[i];
    if (c == '\n') {
      ++line;
    }
    if (inStr) {
      if (c == '\\')
        ++i;
      else if (c == '"')
        inStr = false;
      continue;
    }
    if (inChar) {
      if (c == '\\')
        ++i;
      else if (c == '\'')
        inChar = false;
      continue;
    }
    if (c == '"') {
      inStr = true;
      continue;
    }
    if (c == '\'') {
      inChar = true;
      continue;
    }
    if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
      size_t end = src.find('\n', i);
      if (end == std::string::npos)
        end = src.size();
      out.push_back({line, src.substr(i, end - i)});
      i = end;
      continue;
    }
    if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
      size_t start = i;
      i += 2;
      while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/')) {
        if (src[i] == '\n')
          ++line;
        ++i;
      }
      i = std::min(i + 1, src.size() - 1); // consume closing '/'
      out.push_back({line, src.substr(start, i - start + 1)});
      continue;
    }
  }
  return out;
}

std::string escapeString(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\t': out += "\\t"; break;
    case '\r': out += "\\r"; break;
    default: out += c;
    }
  }
  return out;
}

std::string escapeChar(char c) {
  switch (c) {
  case '\'': return "\\'";
  case '\\': return "\\\\";
  case '\n': return "\\n";
  case '\t': return "\\t";
  case '\r': return "\\r";
  case '"': return "\\\"";
  default: return std::string(1, c);
  }
}

// "fn(int32)->int32" -> "fn(int32) -> int32" (arrows only appear as the
// function-type separator in type strings).
std::string prettyType(const TypeInfo &t) {
  std::string s = ValueHelper::typeToString(t);
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '-' && i + 1 < s.size() && s[i + 1] == '>') {
      out += " -> ";
      ++i;
    } else {
      out += s[i];
    }
  }
  return out;
}

class Printer {
public:
  explicit Printer(std::vector<Comment> comments)
      : _comments(std::move(comments)) {}

  std::string run(const ScriptPtr &script) {
    // Merge top-level declarations back into source order by line.
    struct TopDecl {
      int line;
      enum Kind { Import, Struct, Enum, Proc } kind;
      size_t index;
    };
    std::vector<TopDecl> decls;
    for (size_t i = 0; i < script->imports.size(); ++i)
      decls.push_back({script->imports[i].second, TopDecl::Import, i});
    for (size_t i = 0; i < script->structs.size(); ++i)
      decls.push_back({script->structs[i]->line, TopDecl::Struct, i});
    for (size_t i = 0; i < script->enums.size(); ++i)
      decls.push_back({script->enums[i]->line, TopDecl::Enum, i});
    for (size_t i = 0; i < script->procedures.size(); ++i)
      decls.push_back({script->procedures[i]->line, TopDecl::Proc, i});
    std::stable_sort(decls.begin(), decls.end(),
                     [](const TopDecl &a, const TopDecl &b) {
                       return a.line < b.line;
                     });

    bool first = true;
    for (const auto &d : decls) {
      if (!first) {
        line("");
      }
      first = false;
      flushCommentsBefore(d.line, 0);
      switch (d.kind) {
      case TopDecl::Import:
        emit(0, "import \"" + escapeString(script->imports[d.index].first) +
                    "\";");
        break;
      case TopDecl::Struct:
        emitStruct(script->structs[d.index]);
        break;
      case TopDecl::Enum:
        emitEnum(script->enums[d.index]);
        break;
      case TopDecl::Proc:
        emitProcedure(script->procedures[d.index], 0);
        break;
      }
    }
    flushCommentsBefore(INT32_MAX, 0);
    return _out.str();
  }

private:
  std::vector<Comment> _comments;
  size_t _nextComment = 0;
  std::ostringstream _out;
  int _indent = 0;

  void emit(int indent, const std::string &text) {
    _out << std::string(static_cast<size_t>(indent) * 2, ' ') << text << "\n";
  }

  void line(const std::string &text) { emit(_indent, text); }

  void flushCommentsBefore(int lineNo, int indent) {
    while (_nextComment < _comments.size() &&
           _comments[_nextComment].line < lineNo) {
      emit(indent, _comments[_nextComment].text);
      ++_nextComment;
    }
  }

  // ---------------------------------------------------------------------
  // Declarations
  // ---------------------------------------------------------------------

  void emitEnum(const EnumDeclPtr &e) {
    std::string s = "enum " + e->name + " { ";
    for (size_t i = 0; i < e->members.size(); ++i) {
      if (i > 0)
        s += ", ";
      s += e->members[i].first;
      // Auto-incremented members are re-emitted explicitly so formatting
      // never changes values.
      s += " = " + std::to_string(e->members[i].second);
    }
    s += " }";
    line(s);
  }

  void emitStruct(const StructDeclPtr &s) {
    line("struct " + s->name + " {");
    ++_indent;
    for (const auto &f : s->fields) {
      line(prettyType(f.type) + " " + f.name + ";");
    }
    if (!s->fields.empty() && !s->methods.empty()) {
      line("");
    }
    for (size_t i = 0; i < s->methods.size(); ++i) {
      flushCommentsBefore(s->methods[i]->line, _indent);
      emitProcedure(s->methods[i], _indent);
      if (i + 1 < s->methods.size()) {
        line("");
      }
    }
    --_indent;
    line("}");
  }

  void emitProcedure(const ProcedureDeclPtr &p, int indent) {
    std::string sig = prettyType(p->returnType) + " " + p->name + "(";
    for (size_t i = 0; i < p->parameters.size(); ++i) {
      if (i > 0)
        sig += ", ";
      sig += paramText(p->parameters[i]);
    }
    sig += ")";
    emit(indent, sig + " {");
    ++_indent;
    emitBlockBody(p->body);
    --_indent;
    emit(indent, "}");
  }

  std::string paramText(const Parameter &p) {
    std::string s;
    if (p.type.isAuto) {
      s = p.name; // untyped lambda/parameter
    } else {
      s = prettyType(p.type) + " " + p.name;
    }
    if (p.defaultValue) {
      s += " = " + exprText(p.defaultValue);
    }
    return s;
  }

  // ---------------------------------------------------------------------
  // Statements
  // ---------------------------------------------------------------------

  void emitBlockBody(const StmtPtr &block) {
    if (auto *b = dynamic_cast<BlockStmt *>(block.get())) {
      for (const auto &st : b->statements) {
        flushCommentsBefore(st->line, _indent);
        emitStmt(st);
      }
    }
  }

  void emitStmt(const StmtPtr &stmt) {
    if (auto *e = dynamic_cast<ExpressionStmt *>(stmt.get())) {
      line(exprText(e->expression) + ";");
    } else if (auto *vd = dynamic_cast<VarDeclStmt *>(stmt.get())) {
      std::string s;
      if (vd->isConst)
        s += "const ";
      s += vd->type.isAuto ? "auto" : prettyType(vd->type);
      s += " " + vd->name;
      if (vd->initializer) {
        s += " = " + exprText(vd->initializer);
      }
      line(s + ";");
    } else if (auto *as = dynamic_cast<AssignStmt *>(stmt.get())) {
      line(as->variableName + " " + assignOp(as->op) + " " +
           exprText(as->value) + ";");
    } else if (auto *ia = dynamic_cast<IndexAssignStmt *>(stmt.get())) {
      line(exprText(ia->arrayExpr) + "[" + exprText(ia->indexExpr) + "] " +
           assignOp(ia->op) + " " + exprText(ia->value) + ";");
    } else if (auto *ma = dynamic_cast<MemberAssignStmt *>(stmt.get())) {
      line(exprText(ma->object) + "." + ma->member + " " +
           assignOp(ma->op) + " " + exprText(ma->value) + ";");
    } else if (auto *b = dynamic_cast<BlockStmt *>(stmt.get())) {
      line("{");
      ++_indent;
      emitBlockBody(stmt);
      --_indent;
      line("}");
      (void)b;
    } else if (auto *ifS = dynamic_cast<IfStmt *>(stmt.get())) {
      emitClause("if (" + exprText(ifS->condition) + ")", ifS->thenBranch);
      while (ifS->elseBranch) {
        if (auto *ei =
                dynamic_cast<IfStmt *>(ifS->elseBranch.get())) {
          _pendingTail += " else if (" + exprText(ei->condition) + ")";
          emitClause(_pendingTail, ei->thenBranch);
          ifS = ei;
        } else {
          emitClause(_pendingTail + " else", ifS->elseBranch);
          break;
        }
      }
      flushPendingTail();
    } else if (auto *w = dynamic_cast<WhileStmt *>(stmt.get())) {
      emitClause("while (" + exprText(w->condition) + ")", w->body);
      flushPendingTail();
    } else if (auto *dw = dynamic_cast<DoWhileStmt *>(stmt.get())) {
      emitClause("do", dw->body);
      line(_pendingTail + " while (" + exprText(dw->condition) + ");");
      _pendingTail.clear();
    } else if (auto *f = dynamic_cast<ForStmt *>(stmt.get())) {
      emitClause("for (" + forPart(f->initializer) + "; " +
                     (f->condition ? exprText(f->condition) : "") + "; " +
                     forPart(f->increment) + ")",
                 f->body);
      flushPendingTail();
    } else if (auto *fe = dynamic_cast<ForEachStmt *>(stmt.get())) {
      std::string s = "for (";
      if (fe->elemConst)
        s += "const ";
      s += (fe->elemType.isAuto ? "auto" : prettyType(fe->elemType));
      s += " " + fe->varName + " : " + exprText(fe->iterable) + ")";
      emitClause(s, fe->body);
      flushPendingTail();
    } else if (auto *sw = dynamic_cast<SwitchStmt *>(stmt.get())) {
      line("switch (" + exprText(sw->expression) + ") {");
      ++_indent;
      for (const auto &c : sw->cases) {
        if (c.isDefault) {
          line("default:");
        } else {
          line("case " + exprText(c.matchExpr) + ":");
        }
        ++_indent;
        for (const auto &st : c.statements) {
          flushCommentsBefore(st->line, _indent);
          emitStmt(st);
        }
        --_indent;
      }
      --_indent;
      line("}");
    } else if (auto *r = dynamic_cast<ReturnStmt *>(stmt.get())) {
      line(r->value ? "return " + exprText(r->value) + ";" : "return;");
    } else if (dynamic_cast<BreakStmt *>(stmt.get())) {
      line("break;");
    } else if (dynamic_cast<ContinueStmt *>(stmt.get())) {
      line("continue;");
    } else if (auto *t = dynamic_cast<ThrowStmt *>(stmt.get())) {
      line("throw " + exprText(t->value) + ";");
    } else if (auto *tc = dynamic_cast<TryCatchStmt *>(stmt.get())) {
      emitClause("try", tc->tryBlock);
      if (tc->catchBlock) {
        std::string head = _pendingTail + " catch (";
        if (!tc->catchVar.empty()) {
          head += tc->catchType.isAuto
                      ? tc->catchVar
                      : prettyType(tc->catchType) + " " + tc->catchVar;
        }
        head += ")";
        emitClause(head, tc->catchBlock);
      }
      if (tc->finallyBlock) {
        emitClause(_pendingTail + " finally", tc->finallyBlock);
      }
      flushPendingTail();
    } else {
      line("/* unsupported statement */");
    }
  }

  // --- helpers for the `head { body } tail` emission pattern --------------
  // emitClause writes "<head> {" on one line, the body, then leaves "}" in
  // _pendingTail so a caller can append `else`/`catch`/`finally`/`while`.
  std::string _pendingTail;

  void emitClause(const std::string &head, const StmtPtr &stmt) {
    if (dynamic_cast<BlockStmt *>(stmt.get())) {
      line(head + " {");
      ++_indent;
      emitBlockBody(stmt);
      --_indent;
      _pendingTail = "}";
    } else {
      line(head);
      ++_indent;
      emitStmt(stmt);
      --_indent;
      _pendingTail.clear();
    }
  }

  void flushPendingTail() {
    if (!_pendingTail.empty()) {
      line(_pendingTail);
      _pendingTail.clear();
    }
  }

  // For-init/increment parts print without their trailing semicolon.
  std::string forPart(const StmtPtr &stmt) {
    if (!stmt)
      return "";
    if (auto *vd = dynamic_cast<VarDeclStmt *>(stmt.get())) {
      std::string s;
      if (vd->isConst)
        s += "const ";
      s += vd->type.isAuto ? "auto" : prettyType(vd->type);
      s += " " + vd->name;
      if (vd->initializer)
        s += " = " + exprText(vd->initializer);
      return s;
    }
    if (auto *e = dynamic_cast<ExpressionStmt *>(stmt.get())) {
      return exprText(e->expression);
    }
    if (auto *as = dynamic_cast<AssignStmt *>(stmt.get())) {
      return as->variableName + " " + assignOp(as->op) + " " +
             exprText(as->value);
    }
    return "";
  }

  static std::string assignOp(AssignStmt::Operator op) {
    switch (op) {
    case AssignStmt::Operator::ASSIGN: return "=";
    case AssignStmt::Operator::PLUS_ASSIGN: return "+=";
    case AssignStmt::Operator::MINUS_ASSIGN: return "-=";
    case AssignStmt::Operator::MULT_ASSIGN: return "*=";
    case AssignStmt::Operator::DIV_ASSIGN: return "/=";
    case AssignStmt::Operator::MOD_ASSIGN: return "%=";
    case AssignStmt::Operator::BAND_ASSIGN: return "&=";
    case AssignStmt::Operator::BOR_ASSIGN: return "|=";
    case AssignStmt::Operator::BXOR_ASSIGN: return "^=";
    case AssignStmt::Operator::SHL_ASSIGN: return "<<=";
    case AssignStmt::Operator::SHR_ASSIGN: return ">>=";
    }
    return "=";
  }

  // ---------------------------------------------------------------------
  // Expressions — precedence-aware so redundant parens are dropped.
  // ---------------------------------------------------------------------

  static int binaryPrec(BinaryExpr::Operator op) {
    switch (op) {
    case BinaryExpr::Operator::LOGICAL_OR: return 1;
    case BinaryExpr::Operator::LOGICAL_AND: return 2;
    case BinaryExpr::Operator::BIT_OR: return 3;
    case BinaryExpr::Operator::BIT_XOR: return 4;
    case BinaryExpr::Operator::BIT_AND: return 5;
    case BinaryExpr::Operator::EQUAL:
    case BinaryExpr::Operator::NOT_EQUAL: return 6;
    case BinaryExpr::Operator::LESS_THAN:
    case BinaryExpr::Operator::GREATER_THAN:
    case BinaryExpr::Operator::LESS_EQUAL:
    case BinaryExpr::Operator::GREATER_EQUAL: return 7;
    case BinaryExpr::Operator::LSHIFT:
    case BinaryExpr::Operator::RSHIFT: return 8;
    case BinaryExpr::Operator::ADD:
    case BinaryExpr::Operator::SUBTRACT: return 9;
    case BinaryExpr::Operator::MULTIPLY:
    case BinaryExpr::Operator::DIVIDE:
    case BinaryExpr::Operator::MODULO: return 10;
    }
    return 0;
  }

  static const char *binaryOp(BinaryExpr::Operator op) {
    switch (op) {
    case BinaryExpr::Operator::ADD: return "+";
    case BinaryExpr::Operator::SUBTRACT: return "-";
    case BinaryExpr::Operator::MULTIPLY: return "*";
    case BinaryExpr::Operator::DIVIDE: return "/";
    case BinaryExpr::Operator::MODULO: return "%";
    case BinaryExpr::Operator::EQUAL: return "==";
    case BinaryExpr::Operator::NOT_EQUAL: return "!=";
    case BinaryExpr::Operator::LESS_THAN: return "<";
    case BinaryExpr::Operator::GREATER_THAN: return ">";
    case BinaryExpr::Operator::LESS_EQUAL: return "<=";
    case BinaryExpr::Operator::GREATER_EQUAL: return ">=";
    case BinaryExpr::Operator::LOGICAL_AND: return "&&";
    case BinaryExpr::Operator::LOGICAL_OR: return "||";
    case BinaryExpr::Operator::BIT_AND: return "&";
    case BinaryExpr::Operator::BIT_OR: return "|";
    case BinaryExpr::Operator::BIT_XOR: return "^";
    case BinaryExpr::Operator::LSHIFT: return "<<";
    case BinaryExpr::Operator::RSHIFT: return ">>";
    }
    return "?";
  }

  int exprPrec(const ExprPtr &e) {
    if (dynamic_cast<ConditionalExpr *>(e.get()))
      return 0;
    if (auto *b = dynamic_cast<BinaryExpr *>(e.get()))
      return binaryPrec(b->op);
    if (dynamic_cast<UnaryExpr *>(e.get()) ||
        dynamic_cast<UpdateExpr *>(e.get()))
      return 11;
    return 12; // postfix & primary
  }

  std::string exprText(const ExprPtr &e, int parentPrec = -1,
                       bool isRight = false) {
    int prec = exprPrec(e);
    bool paren = prec < parentPrec || (isRight && prec == parentPrec);
    std::string s = exprRaw(e);
    return paren ? "(" + s + ")" : s;
  }

  std::string exprRaw(const ExprPtr &e) {
    if (auto *lit = dynamic_cast<LiteralExpr *>(e.get())) {
      if (std::holds_alternative<std::string>(lit->value))
        return "\"" + escapeString(std::get<std::string>(lit->value)) + "\"";
      if (std::holds_alternative<char>(lit->value))
        return "'" + escapeChar(std::get<char>(lit->value)) + "'";
      return ValueHelper::toString(lit->value);
    }
    if (auto *v = dynamic_cast<VariableExpr *>(e.get())) {
      return v->name;
    }
    if (auto *b = dynamic_cast<BinaryExpr *>(e.get())) {
      int p = binaryPrec(b->op);
      return exprText(b->left, p) + " " + binaryOp(b->op) + " " +
             exprText(b->right, p, /*isRight=*/true);
    }
    if (auto *u = dynamic_cast<UnaryExpr *>(e.get())) {
      const char *op = u->op == UnaryExpr::Operator::NEGATE   ? "-"
                       : u->op == UnaryExpr::Operator::LOGICAL_NOT ? "!"
                                                                 : "~";
      // No space between unary op and operand; parenthesize operands that
      // aren't tightly bound (e.g. -(a + b)).
      return op + exprText(u->operand, 11);
    }
    if (auto *up = dynamic_cast<UpdateExpr *>(e.get())) {
      const char *op = up->increment ? "++" : "--";
      return up->prefix ? std::string(op) + exprText(up->target, 11)
                        : exprText(up->target, 11) + op;
    }
    if (auto *c = dynamic_cast<CallExpr *>(e.get())) {
      std::string callee =
          c->calleeExpr ? exprText(c->calleeExpr, 12) : c->functionName;
      std::string s = callee + "(";
      for (size_t i = 0; i < c->arguments.size(); ++i) {
        if (i > 0)
          s += ", ";
        s += exprText(c->arguments[i]);
      }
      return s + ")";
    }
    if (auto *cond = dynamic_cast<ConditionalExpr *>(e.get())) {
      return exprText(cond->condition, 1) + " ? " +
             exprText(cond->thenExpr, 0) + " : " +
             exprText(cond->elseExpr, 0, true);
    }
    if (auto *arr = dynamic_cast<ArrayLiteralExpr *>(e.get())) {
      std::string s = "[";
      for (size_t i = 0; i < arr->elements.size(); ++i) {
        if (i > 0)
          s += ", ";
        s += exprText(arr->elements[i]);
      }
      return s + "]";
    }
    if (auto *m = dynamic_cast<MapLiteralExpr *>(e.get())) {
      std::string s = "{";
      for (size_t i = 0; i < m->entries.size(); ++i) {
        if (i > 0)
          s += ", ";
        s += exprText(m->entries[i].first) + ": " +
             exprText(m->entries[i].second);
      }
      return s + "}";
    }
    if (auto *idx = dynamic_cast<IndexExpr *>(e.get())) {
      std::string s = exprText(idx->arrayExpr, 12) + "[";
      if (idx->isSlice) {
        if (idx->indexExpr)
          s += exprText(idx->indexExpr);
        s += ":";
        if (idx->endIndex)
          s += exprText(idx->endIndex);
      } else {
        s += exprText(idx->indexExpr);
      }
      return s + "]";
    }
    if (auto *mem = dynamic_cast<MemberExpr *>(e.get())) {
      return exprText(mem->object, 12) + "." + mem->member;
    }
    if (auto *em = dynamic_cast<EnumMemberExpr *>(e.get())) {
      return em->enumName + "." + em->memberName;
    }
    if (auto *lam = dynamic_cast<LambdaExpr *>(e.get())) {
      std::string s = "fn(";
      for (size_t i = 0; i < lam->parameters.size(); ++i) {
        if (i > 0)
          s += ", ";
        s += paramText(lam->parameters[i]);
      }
      s += ")";
      if (lam->hasRetType) {
        s += " -> " + prettyType(lam->declaredRetType);
      }
      s += " {\n";
      // Lambda bodies print on their own lines at one level deeper. Build
      // them into a temporary buffer so exprText stays string-returning.
      std::ostringstream saved;
      saved.swap(_out);
      int savedIndent = _indent;
      _indent = savedIndent + 1;
      emitBlockBody(lam->body);
      std::string bodyText = _out.str();
      _out.str("");
      _indent = savedIndent;
      _out.swap(saved);
      s += bodyText;
      s += std::string(static_cast<size_t>(savedIndent) * 2, ' ') + "}";
      return s;
    }
    if (auto *interp = dynamic_cast<InterpolatedStringExpr *>(e.get())) {
      std::string s = "\"";
      for (const auto &part : interp->parts) {
        if (part.isExpr) {
          s += "${" + exprText(part.expr) + "}";
        } else {
          s += escapeString(part.text);
        }
      }
      return s + "\"";
    }
    return "<?>";
  }
};

} // namespace

std::string Formatter::format(const std::string &source,
                              const std::string &filename) {
  Lexer lexer(source, filename);
  auto tokens = lexer.tokenize();
  Parser parser(tokens, filename);
  ScriptPtr script = parser.parse();
  if (parser.hasErrors()) {
    throw parser.getErrors().front();
  }
  Printer printer(collectComments(source));
  std::string out = printer.run(script);
  // Collapse leading blank lines and guarantee a single trailing newline.
  while (!out.empty() && out.front() == '\n')
    out.erase(out.begin());
  while (out.size() >= 2 && out.substr(out.size() - 2) == "\n\n")
    out.pop_back();
  if (out.empty() || out.back() != '\n')
    out += '\n';
  return out;
}

} // namespace Script
