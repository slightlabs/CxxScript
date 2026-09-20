// cxxscript — command-line runner and REPL for CxxScript
//
// Usage:
//   cxxscript run <file> [proc] [args...]   Load file and call proc (default main)
//   cxxscript check <file>...               Compile-check files without running
//   cxxscript eval '<statements>'           Evaluate top-level statements
//   cxxscript                               Interactive REPL
//
// REPL commands:
//   .help            show help
//   .quit / .exit    leave the REPL
//   .procs           list loaded procedures
//   .load <file>     load a script file's procedures
//   .reset           discard all state and start fresh

#include "ScriptManager.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace Script;

namespace {

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
      << "  " << argv0 << "               (interactive REPL)\n";
}

} // namespace

int main(int argc, char **argv) {
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
    Value result;
    std::string error;
    if (!manager.evaluateSnippet(argv[2], "<eval>", result, error)) {
      std::cerr << error;
      return 2;
    }
    std::cout << ValueHelper::toString(result) << "\n";
    return 0;
  }
  if (cmd == "repl") {
    return repl();
  }

  usage(argv[0]);
  return 3;
}
