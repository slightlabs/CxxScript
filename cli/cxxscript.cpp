// cxxscript — command-line runner and REPL for CxxScript
//
// Usage:
//   cxxscript run <file> [proc] [args...]   Load file and call proc (default main)
//   cxxscript check <file>...               Compile-check files without running
//   cxxscript eval '<statements>'           Evaluate top-level statements
//   cxxscript fmt <file>... [-w]            Pretty-print (or rewrite with -w)
//   cxxscript debug <file> [proc] [args...] Step-through debugger
//   cxxscript                               Interactive REPL
//
// REPL commands:
//   .help            show help
//   .quit / .exit    leave the REPL
//   .procs           list loaded procedures
//   .load <file>     load a script file's procedures
//   .reset           discard all state and start fresh

#include "DapServer.h"
#include "Formatter.h"
#include "LspServer.h"
#include "ScriptManager.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>

using namespace Script;

namespace {

// Cap native stack consumed by the evaluator well below the platform stack
// (8 MB reserve on MSVC, ~8 MB default elsewhere) so deep script recursion
// reports a fatal error instead of crashing the process.
constexpr size_t kStackBudget = 6 * 1024 * 1024;

// --vm global flag: execute via the bytecode VM instead of the tree-walker.
bool g_useVM = false;

void printErrors(const std::vector<CompilationError> &errors) {
  for (const auto &e : errors) {
    std::cerr << e.toString() << "\n";
  }
}

Value parseArg(const std::string &s) {
  if (s == "true")
    return true;
  if (s == "false")
    return false;
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
    return s.substr(1, s.size() - 2);
  if (s.size() == 3 && s.front() == '\'' && s.back() == '\'')
    return static_cast<char>(s[1]);
  try {
    size_t pos = 0;
    long long iv = std::stoll(s, &pos);
    if (pos == s.size())
      return static_cast<int64_t>(iv);
  } catch (const std::exception &) {
  }
  try {
    size_t pos = 0;
    double dv = std::stod(s, &pos);
    if (pos == s.size())
      return dv;
  } catch (const std::exception &) {
  }
  return s;
}

int runFile(const std::string &file, const std::string &proc,
            const std::vector<std::string> &argStrings) {
  ScriptManager manager;
  manager.setExecutionLimits(0, 0, kStackBudget);
  manager.setVMEnabled(g_useVM);
  std::vector<CompilationError> errors;
  bool loaded = file.size() >= 8 && file.compare(file.size() - 8, 8, ".scriptc") == 0
                    ? manager.loadCompiled(file, errors)
                    : manager.loadScriptFile(file, errors);
  if (!loaded) {
    printErrors(errors);
    return 1;
  }
  printErrors(errors); // warnings

  if (!manager.hasProcedure(proc)) {
    std::cerr << "Procedure not found: " << proc << "\n";
    return 1;
  }

  std::vector<Value> args;
  args.reserve(argStrings.size());
  for (const auto &s : argStrings) {
    args.push_back(parseArg(s));
  }

  Value result;
  std::string error;
  if (!manager.executeProcedure(proc, args, result, error)) {
    std::cerr << error << "\n";
    return 2;
  }

  ScriptManager::ProcedureInfo info;
  if (manager.getProcedureInfo(proc, info) &&
      info.returnType.baseType != DataType::VOID) {
    std::cout << ValueHelper::toString(result) << "\n";
  }
  return 0;
}

int checkFiles(const std::vector<std::string> &files) {
  int rc = 0;
  for (const auto &file : files) {
    ScriptManager manager;
    std::vector<CompilationError> errors;
    if (!manager.checkScript(file, errors)) {
      printErrors(errors);
      rc = 1;
    } else {
      printErrors(errors); // warnings
      std::cout << file << ": OK\n";
    }
  }
  return rc;
}

// ---------------------------------------------------------------------------
// fmt — canonical pretty-printer
// ---------------------------------------------------------------------------

int fmtFile(const std::string &file, bool write) {
  std::ifstream in(file);
  if (!in) {
    std::cerr << "Cannot open: " << file << "\n";
    return 1;
  }
  std::stringstream buf;
  buf << in.rdbuf();
  std::string source = buf.str();
  try {
    std::string formatted = Formatter::format(source, file);
    if (write) {
      if (formatted != source) {
        std::ofstream out(file);
        out << formatted;
        std::cout << file << ": reformatted\n";
      }
    } else {
      std::cout << formatted;
    }
  } catch (const ParseError &e) {
    std::cerr << file << ":" << e.line << ":" << e.column << ": " << e.what()
              << "\n";
    return 1;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// debug — interactive source-level debugger
// ---------------------------------------------------------------------------

// Drives the interpreter's per-statement debug hook. Stops at breakpoints
// or when stepping, then reads commands from stdin until the user resumes.
class CliDebugger {
public:
  CliDebugger(ScriptManager &mgr, const std::string &mainFile)
      : _mgr(mgr), _mainFile(mainFile) {}

  void run() {
    _mgr.setDebugHook(
        [this](const Interpreter::DebugContext &ctx) { onStatement(ctx); });
  }

  bool aborted() const { return _aborted; }

private:
  enum class Mode { Step, Next, Continue };
  Mode _mode = Mode::Step; // stop at the very first statement
  size_t _nextDepth = 0;
  std::set<std::pair<std::string, int>> _breakpoints;
  std::set<std::pair<std::string, int>> _stopOnce;
  ScriptManager &_mgr;
  std::string _mainFile;
  bool _aborted = false;
  const Interpreter::DebugContext *_ctx = nullptr;

  static std::string normPath(const std::string &f) {
    if (f.empty() || f.front() == '<') {
      return f;
    }
    std::error_code ec;
    auto abs = std::filesystem::absolute(f, ec);
    return ec ? f : abs.lexically_normal().string();
  }

  bool shouldStop(const Interpreter::DebugContext &ctx) {
    if (_aborted) {
      return false;
    }
    switch (_mode) {
    case Mode::Step:
      return true;
    case Mode::Next:
      if (ctx.callDepth <= _nextDepth) {
        return true;
      }
      break;
    case Mode::Continue:
      break;
    }
    return _breakpoints.count({normPath(ctx.filename), ctx.line}) > 0 ||
           _stopOnce.count({normPath(ctx.filename), ctx.line}) > 0;
  }

  void printLocation(const Interpreter::DebugContext &ctx) {
    std::cout << ctx.filename << ":" << ctx.line << " in " << ctx.procedure
              << "\n";
    const auto &lines = sourceLines(ctx.filename);
    if (ctx.line >= 1 && ctx.line <= static_cast<int>(lines.size())) {
      std::cout << "  " << ctx.line << " | " << lines[ctx.line - 1] << "\n";
    }
  }

  const std::vector<std::string> &sourceLines(const std::string &file) {
    auto it = _sources.find(file);
    if (it != _sources.end()) {
      return it->second;
    }
    std::vector<std::string> lines;
    std::ifstream in(file);
    std::string l;
    while (std::getline(in, l)) {
      lines.push_back(l);
    }
    return _sources.emplace(file, std::move(lines)).first->second;
  }
  std::unordered_map<std::string, std::vector<std::string>> _sources;

  // Evaluate `expr` against the stopped frame's variable snapshot using a
  // throwaway manager whose external variables expose the locals.
  void printExpr(const std::string &expr) {
    ScriptManager probe;
    for (const auto &kv : _ctx->variables) {
      Value captured = kv.second;
      probe.registerExternalVariableReadOnly(
          kv.first, [captured]() { return captured; });
    }
    Value result;
    std::string error;
    if (probe.evaluateSnippet("return (" + expr + ");", "<dbg>", result,
                              error)) {
      std::cout << "= " << ValueHelper::toString(result) << "\n";
    } else {
      std::cout << "error: " << error;
      if (error.empty() || error.back() != '\n') {
        std::cout << "\n";
      }
    }
  }

  void listContext(int center, const std::string &file) {
    const auto &lines = sourceLines(file);
    int lo = std::max(1, center - 2);
    int hi = std::min(static_cast<int>(lines.size()), center + 2);
    for (int i = lo; i <= hi; ++i) {
      std::cout << (i == center ? ">" : " ") << " " << i << " | " << lines[i - 1]
                << "\n";
    }
  }

  void onStatement(const Interpreter::DebugContext &ctx) {
    _ctx = &ctx;
    std::string file = normPath(ctx.filename);
    _stopOnce.erase({file, ctx.line}); // one-shot breakpoints consumed

    if (!shouldStop(ctx)) {
      return;
    }
    _mode = Mode::Continue; // after any stop, default to running

    std::string cmdline;
    while (true) {
      printLocation(ctx);
      std::cout << "(dbg) " << std::flush;
      if (!std::getline(std::cin, cmdline)) {
        _aborted = true;
        return;
      }
      std::istringstream cs(cmdline);
      std::string cmd;
      cs >> cmd;
      if (cmd == "c" || cmd == "continue") {
        _mode = Mode::Continue;
        return;
      }
      if (cmd == "s" || cmd == "step") {
        _mode = Mode::Step;
        return;
      }
      if (cmd == "n" || cmd == "next") {
        _mode = Mode::Next;
        _nextDepth = ctx.callDepth;
        return;
      }
      if (cmd == "q" || cmd == "quit") {
        _aborted = true;
        return;
      }
      if (cmd == "b" || cmd == "break") {
        std::string where;
        cs >> where;
        std::string targetFile = file;
        int ln = 0;
        auto colon = where.rfind(':');
        if (colon != std::string::npos) {
          targetFile = normPath(where.substr(0, colon));
          ln = std::atoi(where.substr(colon + 1).c_str());
        } else {
          ln = std::atoi(where.c_str());
        }
        if (ln > 0) {
          _breakpoints.insert({targetFile, ln});
          std::cout << "breakpoint at " << targetFile << ":" << ln << "\n";
        } else {
          std::cout << "usage: b [file:]line\n";
        }
        continue;
      }
      if (cmd == "rb" || cmd == "clear") {
        std::string where;
        cs >> where;
        int ln = std::atoi(where.c_str());
        std::string targetFile = file;
        auto colon = where.rfind(':');
        if (colon != std::string::npos) {
          targetFile = normPath(where.substr(0, colon));
          ln = std::atoi(where.substr(colon + 1).c_str());
        }
        _breakpoints.erase({targetFile, ln});
        continue;
      }
      if (cmd == "p" || cmd == "print") {
        std::string expr;
        std::getline(cs, expr);
        expr.erase(0, expr.find_first_not_of(" \t"));
        if (expr.empty()) {
          std::cout << "usage: p <expr>\n";
        } else {
          printExpr(expr);
        }
        continue;
      }
      if (cmd == "locals") {
        std::vector<std::string> names;
        for (const auto &kv : ctx.variables) {
          names.push_back(kv.first);
        }
        std::sort(names.begin(), names.end());
        for (const auto &n : names) {
          std::cout << "  " << n << " = "
                    << ValueHelper::toString(ctx.variables.at(n)) << "\n";
        }
        continue;
      }
      if (cmd == "bt" || cmd == "where") {
        for (size_t i = ctx.callStack.size(); i-- > 0;) {
          std::cout << "  #" << (ctx.callStack.size() - 1 - i) << " "
                    << ctx.callStack[i] << "\n";
        }
        continue;
      }
      if (cmd == "l" || cmd == "list") {
        listContext(ctx.line, ctx.filename);
        continue;
      }
      if (cmd == "h" || cmd == "help") {
        std::cout
            << "  s / step        execute the next statement (into calls)\n"
               "  n / next        next statement, stepping over calls\n"
               "  c / continue    run to the next breakpoint\n"
               "  b [f:]line      set a breakpoint (current file default)\n"
               "  rb [f:]line     remove a breakpoint\n"
               "  p <expr>        evaluate an expression in this frame\n"
               "  locals          list visible variables\n"
               "  bt / where      show the call stack\n"
               "  l / list        show source around the current line\n"
               "  q / quit        abort execution\n";
        continue;
      }
      if (cmd.empty()) {
        continue; // re-prompt
      }
      std::cout << "unknown command '" << cmd << "' (h for help)\n";
    }
  }
};

int debugFile(const std::string &file, const std::string &proc,
              const std::vector<std::string> &argStrings) {
  ScriptManager manager;
  manager.setExecutionLimits(0, 0, kStackBudget);
  std::vector<CompilationError> errors;
  if (!manager.loadScriptFile(file, errors)) {
    printErrors(errors);
    return 1;
  }
  printErrors(errors); // warnings
  if (!manager.hasProcedure(proc)) {
    std::cerr << "Procedure not found: " << proc << "\n";
    return 1;
  }
  std::vector<Value> args;
  for (const auto &s : argStrings) {
    args.push_back(parseArg(s));
  }

  CliDebugger dbg(manager, file);
  dbg.run();
  std::cout << "debugger attached — h for help\n";

  Value result;
  std::string error;
  bool ok = manager.executeProcedure(proc, args, result, error);
  manager.setDebugHook(nullptr);
  if (!ok) {
    std::cerr << error << "\n";
    return 2;
  }
  if (dbg.aborted()) {
    std::cout << "execution aborted\n";
    return 0;
  }
  ScriptManager::ProcedureInfo info;
  if (manager.getProcedureInfo(proc, info) &&
      info.returnType.baseType != DataType::VOID) {
    std::cout << ValueHelper::toString(result) << "\n";
  }
  return 0;
}

bool isReplCommand(const std::string &line) {
  return !line.empty() && line.front() == '.';
}

int braceBalance(const std::string &text) {
  int depth = 0;
  bool inString = false, inChar = false, inLineComment = false;
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (inLineComment) {
      if (c == '\n')
        inLineComment = false;
      continue;
    }
    if (inString) {
      if (c == '\\')
        ++i;
      else if (c == '"')
        inString = false;
      continue;
    }
    if (inChar) {
      if (c == '\\')
        ++i;
      else if (c == '\'')
        inChar = false;
      continue;
    }
    if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
      inLineComment = true;
      continue;
    }
    if (c == '"')
      inString = true;
    else if (c == '\'')
      inChar = true;
    else if (c == '{')
      ++depth;
    else if (c == '}')
      --depth;
  }
  return depth;
}

// Heuristic: a line parses as a bare expression if wrapping it in
// `return (...)` still tokenizes/parses cleanly.
bool looksLikeExpression(const std::string &line) {
  std::string wrapped = "return (" + line + ");";
  try {
    Lexer lexer(wrapped, "<repl>");
    auto tokens = lexer.tokenize();
    for (const auto &t : tokens) {
      if (t.type == TokenType::UNKNOWN)
        return false;
    }
    Parser parser(tokens, "<repl>");
    parser.parseStatements();
    return !parser.hasErrors();
  } catch (...) {
    return false;
  }
}

int repl() {
  std::cout << "CxxScript REPL — .help for commands, .quit to exit\n";

  auto manager = std::make_unique<ScriptManager>();
  manager->setExecutionLimits(0, 0, kStackBudget);
  manager->setVMEnabled(g_useVM);
  std::string line;

  while (true) {
    std::cout << "cxx> " << std::flush;
    if (!std::getline(std::cin, line)) {
      std::cout << "\n";
      break;
    }

    // Accumulate continuation lines while braces are unbalanced.
    while (braceBalance(line) > 0) {
      std::cout << "...  " << std::flush;
      std::string more;
      if (!std::getline(std::cin, more))
        break;
      line += "\n" + more;
    }

    std::string trimmed = line;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
    trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
    if (trimmed.empty())
      continue;

    if (isReplCommand(trimmed)) {
      if (trimmed == ".quit" || trimmed == ".exit")
        break;
      if (trimmed == ".help") {
        std::cout << "  .help          show this help\n"
                     "  .quit/.exit    leave the REPL\n"
                     "  .procs         list loaded procedures\n"
                     "  .load <file>   load a script file's procedures\n"
                     "  .reset         discard all state\n"
                     "Enter statements (persist as globals), procedure\n"
                     "definitions, or a bare expression to evaluate it.\n";
        continue;
      }
      if (trimmed == ".procs") {
        for (const auto &n : manager->getProcedureNames()) {
          std::cout << "  " << n << "\n";
        }
        continue;
      }
      if (trimmed == ".reset") {
        manager = std::make_unique<ScriptManager>();
        manager->setExecutionLimits(0, 0, kStackBudget);
        manager->setVMEnabled(g_useVM);
        std::cout << "state cleared\n";
        continue;
      }
      if (trimmed.rfind(".load ", 0) == 0) {
        std::string file = trimmed.substr(6);
        std::vector<CompilationError> errors;
        if (manager->loadScriptFile(file, errors)) {
          std::cout << "loaded " << file << "\n";
        } else {
          printErrors(errors);
        }
        continue;
      }
      std::cerr << "Unknown command: " << trimmed << " (.help for help)\n";
      continue;
    }

    // A line that defines a procedure is added to the session script.
    bool isProcDef = false;
    {
      Lexer lexer(trimmed, "<repl>");
      auto tokens = lexer.tokenize();
      if (tokens.size() > 3 && tokens[1].type == TokenType::IDENTIFIER &&
          tokens[2].type == TokenType::LPAREN) {
        isProcDef = true;
      }
    }

    std::string error;
    Value result;
    if (isProcDef) {
      std::vector<CompilationError> errors;
      if (!manager->loadScriptSource(trimmed, "<repl>", errors)) {
        printErrors(errors);
      }
      continue;
    }

    if (looksLikeExpression(trimmed)) {
      if (manager->evaluateSnippet("return (" + trimmed + ");", "<repl>",
                                 result, error)) {
        std::cout << "= " << ValueHelper::toString(result) << "\n";
      } else {
        std::cerr << error;
      }
      continue;
    }

    if (!manager->evaluateSnippet(trimmed, "<repl>", result, error) &&
        trimmed.back() != ';' && trimmed.back() != '}') {
      // REPL convenience: allow omitting the trailing semicolon.
      manager->evaluateSnippet(trimmed + ";", "<repl>", result, error);
    }
    if (!error.empty()) {
      std::cerr << error;
    }
  }
  return 0;
}

void usage(const char *argv0) {
  std::cerr
      << "Usage:\n"
      << "  " << argv0 << " run <file> [proc] [args...]\n"
      << "  " << argv0 << " check <file>...\n"
      << "  " << argv0 << " eval '<statements>'\n"
      << "  " << argv0 << " fmt <file>... [-w]   (print or rewrite)\n"
      << "  " << argv0 << " compile <file> [-o out.scriptc]  (bytecode artifact)\n"
      << "  " << argv0 << " debug <file> [proc] [args...]\n"
      << "  " << argv0 << " lsp              (Language Server Protocol on stdio)\n"
      << "  " << argv0 << " dap              (Debug Adapter Protocol on stdio)\n"
      << "  " << argv0 << "               (interactive REPL)\n"
      << "Global flags:\n"
      << "  --vm             execute via the bytecode VM instead of the tree-walker\n";
}

} // namespace

int main(int argc, char **argv) {
  // Strip global flags (currently just --vm) before dispatching commands;
  // argv[0] stays the program name so downstream indexing is unchanged.
  std::vector<char *> filtered;
  filtered.push_back(argv[0]);
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--vm") == 0) {
      g_useVM = true;
    } else {
      filtered.push_back(argv[i]);
    }
  }
  filtered.push_back(nullptr);
  argc = static_cast<int>(filtered.size()) - 1;
  argv = filtered.data();

  if (argc < 2) {
    return repl();
  }

  std::string cmd = argv[1];
  if (cmd == "run") {
    if (argc < 3) {
      usage(argv[0]);
      return 3;
    }
    std::string file = argv[2];
    std::string proc = argc > 3 ? argv[3] : "main";
    std::vector<std::string> args;
    if (argc > 4)
      args.assign(argv + 4, argv + argc);
    return runFile(file, proc, args);
  }
  if (cmd == "check") {
    if (argc < 3) {
      usage(argv[0]);
      return 3;
    }
    std::vector<std::string> files(argv + 2, argv + argc);
    return checkFiles(files);
  }
  if (cmd == "eval") {
    if (argc < 3) {
      usage(argv[0]);
      return 3;
    }
    ScriptManager manager;
    manager.setVMEnabled(g_useVM);
    Value result;
    std::string error;
    if (!manager.evaluateSnippet(argv[2], "<eval>", result, error)) {
      std::cerr << error;
      return 2;
    }
    std::cout << ValueHelper::toString(result) << "\n";
    return 0;
  }
  if (cmd == "fmt") {
    if (argc < 3) {
      usage(argv[0]);
      return 3;
    }
    bool write = false;
    int rc = 0;
    for (int i = 2; i < argc; ++i) {
      std::string a = argv[i];
      if (a == "-w") {
        write = true;
      } else {
        if (fmtFile(a, write) != 0) {
          rc = 1;
        }
      }
    }
    return rc;
  }
  if (cmd == "debug") {
    if (argc < 3) {
      usage(argv[0]);
      return 3;
    }
    std::string file = argv[2];
    std::string proc = argc > 3 ? argv[3] : "main";
    std::vector<std::string> args;
    if (argc > 4)
      args.assign(argv + 4, argv + argc);
    return debugFile(file, proc, args);
  }
  if (cmd == "compile") {
    if (argc < 3) {
      usage(argv[0]);
      return 3;
    }
    std::string file = argv[2];
    std::string out = file;
    if (out.size() >= 7 && out.compare(out.size() - 7, 7, ".script") == 0) {
      out += "c";
    } else {
      out += ".scriptc";
    }
    for (int i = 3; i + 1 < argc; ++i) {
      if (std::string(argv[i]) == "-o") {
        out = argv[i + 1];
      }
    }
    ScriptManager manager;
    std::vector<CompilationError> errors;
    if (!manager.loadScriptFile(file, errors) ||
        !manager.saveCompiled(file, out, errors)) {
      printErrors(errors);
      return 1;
    }
    printErrors(errors); // warnings
    std::cout << out << "\n";
    return 0;
  }
  if (cmd == "lsp") {
    return ::Script::runLanguageServer(std::cin, std::cout);
  }
  if (cmd == "dap") {
    return ::Script::runDebugAdapter(std::cin, std::cout);
  }
  if (cmd == "repl") {
    return repl();
  }

  usage(argv[0]);
  return 3;
}
