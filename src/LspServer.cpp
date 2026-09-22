#include "LspServer.h"

#include "Builtins.h"
#include "Formatter.h"
#include "Json.h"
#include "JsonRpc.h"
#include "Lexer.h"
#include "ScriptManager.h"
#include "Token.h"

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Script {

namespace {

// --- file URI <-> path -----------------------------------------------------

std::string uriToPath(const std::string &uri) {
  if (uri.rfind("file://", 0) != 0) {
    return uri;
  }
  std::string p = uri.substr(7);
  std::string out;
  out.reserve(p.size());
  for (size_t i = 0; i < p.size(); ++i) {
    if (p[i] == '%' && i + 2 < p.size()) {
      int hi = std::isxdigit(p[i + 1]) ? std::stoi(p.substr(i + 1, 2), nullptr, 16)
                                      : -1;
      if (hi >= 0) {
        out += static_cast<char>(hi);
        i += 2;
        continue;
      }
    }
    out += p[i];
  }
  return out;
}

// --- per-document model ----------------------------------------------------

struct DocSymbol {
  std::string name;
  std::string kind; // "proc" | "struct" | "enum" | "field" | "method" |
                    // "enumMember" | "var"
  std::string detail;   // signature / type text
  std::string typeName; // for vars/methods/fields: declared type lexeme
  std::string container;
  int line = 0, col = 0;       // 0-based
  int endLine = 0, endCol = 0; // end of decl scope
};

struct DocModel {
  std::vector<Token> tokens;
  std::vector<DocSymbol> symbols;
  // var name -> type lexeme (best-effort, for member completion)
  std::unordered_map<std::string, std::string> varTypes;
};

bool isTypeStart(const Token &t) {
  switch (t.type) {
  case TokenType::INT8:
  case TokenType::INT16:
  case TokenType::INT32:
  case TokenType::INT64:
  case TokenType::UINT8:
  case TokenType::UINT16:
  case TokenType::UINT32:
  case TokenType::UINT64:
  case TokenType::FLOAT:
  case TokenType::DOUBLE:
  case TokenType::STRING:
  case TokenType::BOOL:
  case TokenType::CHAR:
  case TokenType::VOID:
    return true;
  default:
    return t.type == TokenType::IDENTIFIER;
  }
}

// Find the token index of the matching '}' for the '{' at `openIdx`.
size_t matchBrace(const std::vector<Token> &toks, size_t openIdx) {
  int depth = 0;
  for (size_t i = openIdx; i < toks.size(); ++i) {
    if (toks[i].type == TokenType::LBRACE) {
      ++depth;
    } else if (toks[i].type == TokenType::RBRACE) {
      if (--depth == 0) {
        return i;
      }
    }
  }
  return toks.empty() ? 0 : toks.size() - 1;
}

// Best-effort model: scans tokens (works on syntactically incomplete code)
// to find proc/struct/enum decls and `Type var` declarations.
void buildModel(DocModel &m) {
  const auto &t = m.tokens;
  auto addVar = [&](size_t typeIdx, size_t nameIdx) {
    DocSymbol s;
    s.name = t[nameIdx].lexeme;
    s.kind = "var";
    s.typeName = t[typeIdx].lexeme;
    s.line = t[nameIdx].line - 1;
    s.col = t[nameIdx].column - 1;
    s.endLine = s.line;
    s.endCol = s.col + static_cast<int>(s.name.size());
    m.symbols.push_back(s);
    m.varTypes[s.name] = s.typeName;
  };
  // Index of the last token of the type starting at i: an optional
  // `map<...>`/`fn<...>` generic group, then any number of `[]` suffixes.
  auto typeEnd = [&](size_t i) -> size_t {
    size_t j = i;
    if (t[j].type == TokenType::IDENTIFIER &&
        (t[j].lexeme == "map" || t[j].lexeme == "fn") &&
        j + 1 < t.size() && t[j + 1].type == TokenType::LESS_THAN) {
      int depth = 0;
      ++j;
      for (; j < t.size(); ++j) {
        if (t[j].type == TokenType::LESS_THAN) {
          ++depth;
        } else if (t[j].type == TokenType::GREATER_THAN && --depth == 0) {
          break;
        }
      }
    }
    while (j + 2 < t.size() && t[j + 1].type == TokenType::LBRACKET &&
           t[j + 2].type == TokenType::RBRACKET) {
      j += 2;
    }
    return j;
  };

  for (size_t i = 0; i < t.size(); ++i) {
    if (t[i].type == TokenType::STRUCT && i + 1 < t.size() &&
        t[i + 1].type == TokenType::IDENTIFIER) {
      DocSymbol s;
      s.name = t[i + 1].lexeme;
      s.kind = "struct";
      s.detail = "struct " + s.name;
      s.line = t[i].line - 1;
      s.col = t[i].column - 1;
      size_t close = i;
      if (i + 2 < t.size() && t[i + 2].type == TokenType::LBRACE) {
        close = matchBrace(t, i + 2);
        // Members between i+3 and close
        for (size_t j = i + 3; j < close;) {
          if (!isTypeStart(t[j]) || j + 1 >= close ||
              t[j + 1].type != TokenType::IDENTIFIER) {
            ++j;
            continue;
          }
          size_t te = typeEnd(j);
          if (te + 1 >= close || t[te + 1].type != TokenType::IDENTIFIER) {
            ++j;
            continue;
          }
          DocSymbol m2;
          m2.name = t[te + 1].lexeme;
          m2.typeName = t[j].lexeme;
          m2.container = s.name;
          m2.line = t[j].line - 1;
          m2.col = t[j].column - 1;
          m2.endLine = m2.line;
          if (te + 2 < close && t[te + 2].type == TokenType::LPAREN) {
            m2.kind = "method";
            m2.detail = m2.typeName + " " + m2.name + "(...)";
            for (size_t k = te + 2; k < close; ++k) {
              if (t[k].type == TokenType::LBRACE) {
                size_t mc = matchBrace(t, k);
                m2.endLine = t[mc].line - 1;
                m2.endCol = t[mc].column;
                j = mc + 1;
                break;
              }
            }
          } else {
            m2.kind = "field";
            m2.detail = m2.typeName + " " + m2.name;
            ++j;
          }
          m.symbols.push_back(m2);
        }
      }
      s.endLine = t[close].line - 1;
      s.endCol = t[close].column;
      m.symbols.push_back(s);
      i = close;
      continue;
    }
    if (t[i].type == TokenType::ENUM && i + 1 < t.size() &&
        t[i + 1].type == TokenType::IDENTIFIER) {
      DocSymbol s;
      s.name = t[i + 1].lexeme;
      s.kind = "enum";
      s.detail = "enum " + s.name;
      s.line = t[i].line - 1;
      s.col = t[i].column - 1;
      size_t close = i;
      if (i + 2 < t.size() && t[i + 2].type == TokenType::LBRACE) {
        close = matchBrace(t, i + 2);
        for (size_t j = i + 3; j < close; ++j) {
          if (t[j].type == TokenType::IDENTIFIER) {
            DocSymbol m2;
            m2.name = t[j].lexeme;
            m2.kind = "enumMember";
            m2.container = s.name;
            m2.detail = s.name + "." + m2.name;
            m2.line = t[j].line - 1;
            m2.col = t[j].column - 1;
            m2.endLine = m2.line;
            m2.endCol = m2.col + static_cast<int>(m2.name.size());
            m.symbols.push_back(m2);
          }
        }
      }
      s.endLine = t[close].line - 1;
      s.endCol = t[close].column;
      m.symbols.push_back(s);
      i = close;
      continue;
    }
    // `retType name ( ... ) {` at any depth => procedure/method decl.
    // `type name (=|;) ...` => variable decl.
    if (isTypeStart(t[i]) && i + 1 < t.size() &&
        t[i + 1].type == TokenType::IDENTIFIER) {
      size_t te = typeEnd(i);
      if (te + 1 < t.size() && t[te + 1].type == TokenType::IDENTIFIER) {
        if (te + 2 < t.size() && t[te + 2].type == TokenType::LPAREN) {
          DocSymbol s;
          s.name = t[te + 1].lexeme;
          s.kind = "proc";
          s.typeName = t[i].lexeme;
          s.detail = s.typeName + " " + s.name + "()";
          s.line = t[i].line - 1;
          s.col = t[i].column - 1;
          s.endLine = s.line;
          // Find body '{' after matching ')'
          int depth = 0;
          for (size_t k = te + 2; k < t.size(); ++k) {
            if (t[k].type == TokenType::LPAREN) {
              ++depth;
            } else if (t[k].type == TokenType::RPAREN) {
              --depth;
            } else if (depth == 0 && t[k].type == TokenType::LBRACE) {
              size_t close = matchBrace(t, k);
              s.endLine = t[close].line - 1;
              s.endCol = t[close].column;
              break;
            } else if (depth == 0 && t[k].type == TokenType::SEMICOLON) {
              break;
            }
          }
          m.symbols.push_back(s);
          i = te + 1;
          continue;
        }
        if (te + 2 < t.size() &&
            (t[te + 2].type == TokenType::ASSIGN ||
             t[te + 2].type == TokenType::SEMICOLON)) {
          addVar(i, te + 1);
          i = te + 1;
          continue;
        }
      }
    }
  }
}

DocModel analyze(const std::string &source, const std::string &filename) {
  DocModel m;
  Lexer lexer(source, filename);
  // Drive nextToken() so a lex error mid-file still yields usable prefix.
  try {
    for (;;) {
      Token t = lexer.nextToken();
      if (t.type == TokenType::END_OF_FILE) {
        break;
      }
      m.tokens.push_back(t);
    }
  } catch (...) {
    // Keep the tokens we got.
  }
  buildModel(m);
  return m;
}

// --- position helpers ------------------------------------------------------

// Split into lines (without newline chars).
std::vector<std::string> splitLines(const std::string &text) {
  std::vector<std::string> lines;
  size_t start = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n') {
      lines.push_back(text.substr(start, i - start));
      start = i + 1;
    }
  }
  lines.push_back(text.substr(start));
  return lines;
}

bool isIdentChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// Identifier bounds [start,end) on the given line covering `col`.
bool wordAt(const std::string &line, size_t col, size_t &start, size_t &end) {
  if (col > line.size()) {
    col = line.size();
  }
  if (col == line.size() && col > 0 && !isIdentChar(line[col]) &&
      isIdentChar(line[col - 1])) {
    --col; // cursor just past end of word
  }
  if (col >= line.size() || !isIdentChar(line[col])) {
    return false;
  }
  start = col;
  while (start > 0 && isIdentChar(line[start - 1])) {
    --start;
  }
  end = col;
  while (end < line.size() && isIdentChar(line[end])) {
    ++end;
  }
  return true;
}

Json makeRange(int line, int col, int endLine, int endCol) {
  return Json::Object{
      {"start", Json::Object{{"line", line}, {"character", col}}},
      {"end", Json::Object{{"line", endLine}, {"character", endCol}}}};
}

// LSP SymbolKind / CompletionItemKind values we use.
constexpr int kSymFile = 1, kSymStruct = 23, kSymEnum = 10, kSymEnumMember = 22,
              kSymFunction = 12, kSymField = 8, kSymVariable = 13;
constexpr int kItemKeyword = 14, kItemFunction = 3, kItemStruct = 7,
              kItemEnum = 13, kItemField = 5, kItemEnumMember = 20,
              kItemVariable = 6;

std::string signatureOf(const DocSymbol &s) {
  if (s.kind == "proc" || s.kind == "method") {
    return s.detail;
  }
  return s.detail.empty() ? s.name : s.detail;
}

// --- builtin hover docs ----------------------------------------------------

const std::unordered_map<std::string, std::string> &builtinDocs() {
  static const std::unordered_map<std::string, std::string> docs = {
      {"len", "len(x) -> int64: length of a string, array, or map"},
      {"push", "push(array, value): append to array"},
      {"pop", "pop(array) -> value: remove and return last element"},
      {"insert", "insert(array, index, value): insert element"},
      {"removeAt", "removeAt(array, index): remove element at index"},
      {"clear", "clear(x): remove all elements/entries"},
      {"has", "has(map, key) -> bool: key membership test"},
      {"remove", "remove(map, key): erase entry"},
      {"keys", "keys(map) -> array: all keys"},
      {"values", "values(map) -> array: all values"},
      {"size", "size(x) -> int64: element count"},
      {"substr", "substr(s, start[, len]) -> string"},
      {"charAt", "charAt(s, i) -> char"},
      {"indexOf", "indexOf(s, sub) -> int64"},
      {"contains", "contains(s, sub) -> bool"},
      {"trim", "trim(s) -> string"},
      {"replace", "replace(s, from, to) -> string"},
      {"split", "split(s[, sep]) -> array<string>"},
      {"join", "join(array[, sep]) -> string"},
      {"repeat", "repeat(s, n) -> string"},
      {"reverse", "reverse(x): reverse array or string in place"},
      {"format", "format(fmt, args...) -> string: {} substitution"},
      {"abs", "abs(x) -> number"},
      {"min", "min(a, b) -> number"},
      {"max", "max(a, b) -> number"},
      {"clamp", "clamp(x, lo, hi) -> number"},
      {"pow", "pow(x, y) -> double"},
      {"sqrt", "sqrt(x) -> double"},
      {"floor", "floor(x) -> double"},
      {"ceil", "ceil(x) -> double"},
      {"round", "round(x) -> double"},
      {"trunc", "trunc(x) -> double"},
      {"fmod", "fmod(x, y) -> double"},
      {"sin", "sin(x) -> double"},
      {"cos", "cos(x) -> double"},
      {"tan", "tan(x) -> double"},
      {"asin", "asin(x) -> double"},
      {"acos", "acos(x) -> double"},
      {"atan", "atan(x) -> double"},
      {"atan2", "atan2(y, x) -> double"},
      {"exp", "exp(x) -> double"},
      {"log", "log(x) -> double"},
      {"log10", "log10(x) -> double"},
      {"random", "random() -> double in [0,1)"},
      {"randInt", "randInt(lo, hi) -> int64"},
      {"srand", "srand(seed): seed the RNG"},
      {"pi", "pi() -> double"},
      {"parseInt", "parseInt(s) -> int64"},
      {"parseDouble", "parseDouble(s) -> double"},
      {"typeof", "typeof(x) -> string: runtime type name"},
      {"print", "print(args...): write to stdout"},
      {"println", "println(args...): write line to stdout"},
      {"error", "error(msg): throw a runtime error"},
      {"assert", "assert(cond[, msg]): abort when false"},
  };
  return docs;
}

const std::unordered_map<std::string, std::string> &keywordDocs() {
  static const std::unordered_map<std::string, std::string> docs = {
      {"if", "conditional statement"},
      {"else", "alternative branch"},
      {"while", "loop while condition holds"},
      {"do", "do-while loop"},
      {"for", "for loop / for-each over collections"},
      {"switch", "multi-way branch on a value"},
      {"case", "switch branch"},
      {"default", "default switch branch / default param marker"},
      {"break", "exit loop or switch"},
      {"continue", "next loop iteration"},
      {"return", "return from procedure"},
      {"const", "immutable variable"},
      {"import", "include another script file"},
      {"struct", "declare a struct type"},
      {"enum", "declare an enum type"},
      {"try", "begin protected block"},
      {"catch", "handle an exception"},
      {"finally", "always-run cleanup block"},
      {"throw", "raise an exception"},
      {"true", "boolean literal"},
      {"false", "boolean literal"},
      {"fn", "function type / lambda expression"},
      {"auto", "deduced variable type"},
  };
  return docs;
}

} // namespace

// --- server ----------------------------------------------------------------

namespace {

class LspServer {
public:
  LspServer(std::istream &in, std::ostream &out) : _in(in), _out(out) {}

  int run() {
    Json msg;
    while (readMessage(msg)) {
      const std::string method = msg.stringOr("method");
      const Json *id = msg.find("id");
      const Json *params = msg.find("params");
      Json emptyObj = Json::Object{};
      const Json &p = params ? *params : emptyObj;

      if (method == "exit") {
        return 0;
      }
      if (id) {
        handleRequest(*id, method, p);
      } else {
        handleNotification(method, p);
      }
      if (_shutdown && method != "exit") {
        // Spec: keep serving until exit; nothing special needed.
      }
    }
    return 0;
  }

private:
  std::istream &_in;
  std::ostream &_out;
  std::unordered_map<std::string, std::string> _docs; // uri -> text
  std::unordered_map<std::string, DocModel> _models;
  bool _shutdown = false;

  // --- transport ---

  bool readMessage(Json &out) { return jsonrpc::readMessage(_in, out); }

  void send(const Json &msg) { jsonrpc::writeMessage(_out, msg); }

  void sendResult(const Json &id, const Json &result) {
    send(Json::Object{{"jsonrpc", "2.0"},
                      {"id", id},
                      {"result", result}});
  }

  void sendErr(const Json &id, int code, const std::string &message) {
    send(Json::Object{{"jsonrpc", "2.0"},
                      {"id", id},
                      {"error", Json::Object{{"code", code},
                                             {"message", message}}}});
  }

  void notify(const std::string &method, const Json &params) {
    send(Json::Object{{"jsonrpc", "2.0"},
                      {"method", method},
                      {"params", params}});
  }

  // --- request dispatch ---

  void handleRequest(const Json &id, const std::string &method,
                     const Json &p) {
    if (method == "initialize") {
      sendResult(id,
                 Json::Object{
                     {"capabilities",
                      Json::Object{
                          // 1 = full document sync
                          {"textDocumentSync", 1},
                          {"completionProvider",
                           Json::Object{{"triggerCharacters",
                                         Json::Array{Json(".")}}}},
                          {"hoverProvider", true},
                          {"definitionProvider", true},
                          {"documentSymbolProvider", true},
                          {"documentFormattingProvider", true}}},
                     {"serverInfo",
                      Json::Object{{"name", "cxxscript-lsp"},
                                   {"version", "1.0"}}}});
      return;
    }
    if (method == "shutdown") {
      _shutdown = true;
      sendResult(id, Json(nullptr));
      return;
    }
    if (method == "textDocument/completion") {
      sendResult(id, completion(p));
      return;
    }
    if (method == "textDocument/hover") {
      sendResult(id, hover(p));
      return;
    }
    if (method == "textDocument/definition") {
      sendResult(id, definition(p));
      return;
    }
    if (method == "textDocument/documentSymbol") {
      sendResult(id, documentSymbols(p));
      return;
    }
    if (method == "textDocument/formatting") {
      sendResult(id, formatting(p));
      return;
    }
    // Unknown request: reply null (MethodNotFound is also acceptable,
    // but null is friendlier to clients that probe capabilities).
    sendResult(id, Json(nullptr));
  }

  void handleNotification(const std::string &method, const Json &p) {
    if (method == "textDocument/didOpen") {
      const Json &td = p["textDocument"];
      std::string uri = td.stringOr("uri");
      _docs[uri] = td.stringOr("text");
      reanalyze(uri);
      return;
    }
    if (method == "textDocument/didChange") {
      const Json &td = p["textDocument"];
      std::string uri = td.stringOr("uri");
      // Full sync: last change holds the whole document.
      const Json *changes = p.find("contentChanges");
      if (changes && changes->isArray() && !changes->asArray().empty()) {
        _docs[uri] = changes->asArray().back().stringOr("text");
      }
      reanalyze(uri);
      return;
    }
    if (method == "textDocument/didClose") {
      std::string uri = p["textDocument"].stringOr("uri");
      _docs.erase(uri);
      _models.erase(uri);
      notify("textDocument/publishDiagnostics",
             Json::Object{{"uri", uri}, {"diagnostics", Json::Array{}}});
      return;
    }
    if (method == "textDocument/didSave") {
      std::string uri = p["textDocument"].stringOr("uri");
      reanalyze(uri);
      return;
    }
  }

  void reanalyze(const std::string &uri) {
    auto it = _docs.find(uri);
    if (it == _docs.end()) {
      return;
    }
    _models[uri] = analyze(it->second, uriToPath(uri));
    publishDiagnostics(uri);
  }

  void publishDiagnostics(const std::string &uri) {
    const std::string &text = _docs[uri];
    ScriptManager mgr;
    std::vector<CompilationError> errors;
    mgr.checkScriptSource(text, uriToPath(uri), errors);

    Json::Array diags;
    for (const auto &e : errors) {
      int line = e.line > 0 ? e.line - 1 : 0;
      int col = e.column > 0 ? e.column - 1 : 0;
      diags.push_back(Json::Object{
          {"range", makeRange(line, col, line, col + 1)},
          {"severity", e.isWarning ? 2 : 1},
          {"source", "cxxscript"},
          {"message", e.message}});
    }
    notify("textDocument/publishDiagnostics",
           Json::Object{{"uri", uri}, {"diagnostics", Json(std::move(diags))}});
  }

  // --- language features ---

  Json docPosition(const Json &p, std::string &uri, int &line, int &col) {
    uri = p["textDocument"].stringOr("uri");
    const Json &pos = p["position"];
    line = static_cast<int>(pos.intOr("line"));
    col = static_cast<int>(pos.intOr("character"));
    return p;
  }

  Json completion(const Json &p) {
    std::string uri;
    int line = 0, col = 0;
    docPosition(p, uri, line, col);
    auto dit = _docs.find(uri);
    if (dit == _docs.end()) {
      return Json(nullptr);
    }
    const std::string &text = dit->second;
    const DocModel &model = _models[uri];
    std::vector<std::string> lines = splitLines(text);

    Json::Array items;
    auto add = [&](const std::string &label, int kind,
                   const std::string &detail) {
      Json::Object item{{"label", label}, {"kind", kind}};
      if (!detail.empty()) {
        item["detail"] = detail;
      }
      items.push_back(Json(std::move(item)));
    };

    // Member completion after '.'
    bool member = false;
    std::string receiver;
    if (line >= 0 && line < static_cast<int>(lines.size())) {
      const std::string &l = lines[line];
      size_t c = static_cast<size_t>(col);
      if (c > l.size()) {
        c = l.size();
      }
      // Walk back over a partial identifier, then '.'.
      size_t i = c;
      while (i > 0 && isIdentChar(l[i - 1])) {
        --i;
      }
      if (i > 0 && l[i - 1] == '.') {
        member = true;
        size_t rend = i - 1;
        size_t rstart = rend;
        while (rstart > 0 && isIdentChar(l[rstart - 1])) {
          --rstart;
        }
        receiver = l.substr(rstart, rend - rstart);
      }
    }

    if (member) {
      // Struct type of the receiver variable, or the receiver is itself a
      // type name (enum member access).
      std::string typeName;
      auto vt = model.varTypes.find(receiver);
      if (vt != model.varTypes.end()) {
        typeName = vt->second;
      } else {
        typeName = receiver;
      }
      for (const DocSymbol &s : model.symbols) {
        if (s.container == typeName &&
            (s.kind == "field" || s.kind == "method" ||
             s.kind == "enumMember")) {
          add(s.name,
              s.kind == "method"    ? kItemFunction
              : s.kind == "field"   ? kItemField
                                    : kItemEnumMember,
              s.detail);
        }
      }
      return Json(std::move(items));
    }

    for (const auto &[kw, _] : Lexer::keywords()) {
      add(kw, kItemKeyword, "keyword");
    }
    for (const std::string &b : Builtins::builtinNames()) {
      auto doc = builtinDocs().find(b);
      add(b, kItemFunction,
          doc != builtinDocs().end() ? doc->second : "builtin");
    }
    for (const DocSymbol &s : model.symbols) {
      int kind = s.kind == "proc"     ? kItemFunction
                 : s.kind == "struct" ? kItemStruct
                 : s.kind == "enum"   ? kItemEnum
                                      : kItemVariable;
      if (s.kind == "field" || s.kind == "method" ||
          s.kind == "enumMember") {
        continue; // only offered in member context
      }
      add(s.name, kind, signatureOf(s));
    }
    return Json(std::move(items));
  }

  Json hover(const Json &p) {
    std::string uri;
    int line = 0, col = 0;
    docPosition(p, uri, line, col);
    auto dit = _docs.find(uri);
    if (dit == _docs.end()) {
      return Json(nullptr);
    }
    std::vector<std::string> lines = splitLines(dit->second);
    if (line < 0 || line >= static_cast<int>(lines.size())) {
      return Json(nullptr);
    }
    size_t s = 0, e = 0;
    if (!wordAt(lines[line], static_cast<size_t>(col), s, e)) {
      return Json(nullptr);
    }
    std::string word = lines[line].substr(s, e - s);
    const DocModel &model = _models[uri];

    std::string text;
    for (const DocSymbol &sym : model.symbols) {
      if (sym.name == word) {
        text = signatureOf(sym);
        break;
      }
      // `Type.member` member access / field of a var whose type is known
      if (!sym.container.empty() && sym.name == word) {
        text = signatureOf(sym);
        break;
      }
    }
    if (text.empty()) {
      if (Lexer::keywords().count(word)) {
        auto it = keywordDocs().find(word);
        text = it != keywordDocs().end() ? it->second : word + ": keyword";
      } else if (Builtins::isBuiltin(word)) {
        auto it = builtinDocs().find(word);
        text = it != builtinDocs().end() ? it->second
                                        : word + ": builtin function";
      }
    }
    if (text.empty()) {
      return Json(nullptr);
    }
    return Json::Object{
        {"contents",
         Json::Object{{"kind", "markdown"},
                      {"value", "```cxxscript\n" + text + "\n```"}}},
        {"range", makeRange(line, static_cast<int>(s), line,
                            static_cast<int>(e))}};
  }

  Json definition(const Json &p) {
    std::string uri;
    int line = 0, col = 0;
    docPosition(p, uri, line, col);
    auto dit = _docs.find(uri);
    if (dit == _docs.end()) {
      return Json(nullptr);
    }
    std::vector<std::string> lines = splitLines(dit->second);
    if (line < 0 || line >= static_cast<int>(lines.size())) {
      return Json(nullptr);
    }
    size_t s = 0, e = 0;
    if (!wordAt(lines[line], static_cast<size_t>(col), s, e)) {
      return Json(nullptr);
    }
    std::string word = lines[line].substr(s, e - s);
    const DocModel &model = _models[uri];
    for (const DocSymbol &sym : model.symbols) {
      if (sym.name == word) {
        return Json::Object{
            {"uri", uri},
            {"range", makeRange(sym.line, sym.col, sym.line,
                                sym.col + static_cast<int>(sym.name.size()))}};
      }
    }
    return Json(nullptr);
  }

  Json documentSymbols(const Json &p) {
    std::string uri = p["textDocument"].stringOr("uri");
    auto mit = _models.find(uri);
    if (mit == _models.end()) {
      return Json(nullptr);
    }
    // Hierarchical DocumentSymbol[]: top-level procs/structs/enums with
    // members nested under their container.
    std::unordered_map<std::string, Json::Array> children;
    Json::Array tops;
    for (const DocSymbol &s : mit->second.symbols) {
      if (s.container.empty()) {
        continue;
      }
      int kind = s.kind == "method"     ? kSymFunction
                 : s.kind == "field"    ? kSymField
                 : s.kind == "enumMember" ? kSymEnumMember
                                          : kSymVariable;
      children[s.container].push_back(Json::Object{
          {"name", s.name},
          {"kind", kind},
          {"range", makeRange(s.line, s.col, s.endLine, s.endCol)},
          {"selectionRange",
           makeRange(s.line, s.col, s.line,
                     s.col + static_cast<int>(s.name.size()))}});
    }
    for (const DocSymbol &s : mit->second.symbols) {
      if (!s.container.empty()) {
        continue;
      }
      int kind = s.kind == "proc"     ? kSymFunction
                 : s.kind == "struct" ? kSymStruct
                 : s.kind == "enum"   ? kSymEnum
                                      : kSymVariable;
      Json::Object sym{
          {"name", s.name},
          {"kind", kind},
          {"range", makeRange(s.line, s.col, s.endLine, s.endCol)},
          {"selectionRange",
           makeRange(s.line, s.col, s.line,
                     s.col + static_cast<int>(s.name.size()))}};
      auto cit = children.find(s.name);
      if (cit != children.end()) {
        sym["children"] = cit->second;
      }
      tops.push_back(Json(std::move(sym)));
    }
    return Json(std::move(tops));
  }

  Json formatting(const Json &p) {
    std::string uri = p["textDocument"].stringOr("uri");
    auto dit = _docs.find(uri);
    if (dit == _docs.end()) {
      return Json(nullptr);
    }
    try {
      std::string formatted = Formatter::format(dit->second, uriToPath(uri));
      if (formatted == dit->second) {
        return Json::Array{};
      }
      std::vector<std::string> lines = splitLines(dit->second);
      int lastLine = static_cast<int>(lines.size()) - 1;
      int lastCol = static_cast<int>(lines.back().size());
      return Json::Array{Json::Object{
          {"range", makeRange(0, 0, lastLine, lastCol)},
          {"newText", formatted}}};
    } catch (const std::exception &) {
      return Json::Array{}; // unparseable doc: no edits
    }
  }
};

} // namespace

int runLanguageServer(std::istream &in, std::ostream &out) {
  return LspServer(in, out).run();
}

} // namespace Script
