#include "DapServer.h"

#include "Interpreter.h"
#include "Json.h"
#include "JsonRpc.h"
#include "ScriptManager.h"

#include <algorithm>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Script {

namespace {

// Thrown inside the debug hook to unwind a running script on disconnect.
struct DapAbort {};

std::string normPath(const std::string &f) {
  if (f.empty() || f.front() == '<') {
    return f;
  }
  std::error_code ec;
  auto abs = std::filesystem::absolute(f, ec);
  return ec ? f : abs.lexically_normal().string();
}

Value parseArgValue(const std::string &s) {
  if (s == "true") {
    return true;
  }
  if (s == "false") {
    return false;
  }
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
    return s.substr(1, s.size() - 2);
  }
  if (s.size() == 3 && s.front() == '\'' && s.back() == '\'') {
    return static_cast<char>(s[1]);
  }
  try {
    size_t pos = 0;
    long long iv = std::stoll(s, &pos);
    if (pos == s.size()) {
      return static_cast<int64_t>(iv);
    }
  } catch (const std::exception &) {
  }
  try {
    size_t pos = 0;
    double dv = std::stod(s, &pos);
    if (pos == s.size()) {
      return dv;
    }
  } catch (const std::exception &) {
  }
  return s;
}

// Snapshot of a stopped script, taken while the debug hook is suspended.
struct StopState {
  bool valid = false;
  std::string filename;
  int line = 0;
  int col = 0;
  std::string procedure;
  size_t callDepth = 0;
  std::vector<std::string> callStack;
  std::unordered_map<std::string, Value> variables;
};

class DapServer {
public:
  DapServer(std::istream &in, std::ostream &out) : _in(in), _out(out) {}
  ~DapServer() {
    abort();
    if (_worker.joinable()) {
      _worker.join();
    }
  }

  int run() {
    Json msg;
    bool ok = false;
    while (jsonrpc::readMessage(_in, msg, ok)) {
      if (!ok) {
        continue;
      }
      if (msg.stringOr("type") != "request") {
        continue; // only requests are inbound for a debug adapter
      }
      const std::string command = msg.stringOr("command");
      int64_t seq = msg.intOr("seq");
      Json empty = Json::Object{};
      const Json *args = msg.find("arguments");
      handleRequest(seq, command, args ? *args : empty);
      if (command == "disconnect") {
        break;
      }
    }
    return 0;
  }

private:
  std::istream &_in;
  std::ostream &_out;
  int64_t _seq = 1; // guarded by _outMu

  std::mutex _mu;          // guards state below + all output
  std::condition_variable _cv;

  enum class Mode { Idle, Running, Stopped, Finished };
  Mode _mode = Mode::Idle;

  enum class Resume { None, Continue, Step, Next, StepOut };
  Resume _resume = Resume::None;
  size_t _nextDepth = 0;
  bool _abort = false;

  // Launch configuration
  std::string _program, _procedure = "main";
  std::vector<std::string> _argStrings;
  bool _stopOnEntry = false;
  bool _launched = false;

  std::unordered_map<std::string, std::unordered_set<int>> _breakpoints;
  StopState _stop;
  std::thread _worker;
  bool _workerStarted = false; // guarded by _mu

  // --- transport ---

  int64_t allocSeq() {
    std::lock_guard<std::mutex> lk(_outMu);
    return _seq++;
  }

  void send(const Json &msg) {
    std::lock_guard<std::mutex> lk(_outMu);
    jsonrpc::writeMessage(_out, msg);
  }
  std::mutex _outMu;

  void respond(int64_t reqSeq, const std::string &command,
               const Json &body) {
    send(Json::Object{{"seq", allocSeq()},
                      {"type", "response"},
                      {"request_seq", reqSeq},
                      {"success", true},
                      {"command", command},
                      {"body", body}});
  }

  void fail(int64_t reqSeq, const std::string &command,
            const std::string &message) {
    send(Json::Object{{"seq", allocSeq()},
                      {"type", "response"},
                      {"request_seq", reqSeq},
                      {"success", false},
                      {"command", command},
                      {"message", message}});
  }

  void event(const std::string &name, const Json &body = Json::Object{}) {
    send(Json::Object{{"seq", allocSeq()},
                      {"type", "event"},
                      {"event", name},
                      {"body", body}});
  }

  // --- worker control ---

  void abort() {
    {
      std::lock_guard<std::mutex> lk(_mu);
      _abort = true;
      _resume = Resume::Continue;
    }
    _cv.notify_all();
  }

  // Blocks until the worker is stopped or has finished. Returns true only
  // when a valid stop state is available.
  bool waitForStop() {
    std::unique_lock<std::mutex> lk(_mu);
    if (!_workerStarted) {
      return false; // never launched: nothing to wait for
    }
    _cv.wait(lk, [&] {
      return _mode == Mode::Stopped || _mode == Mode::Finished;
    });
    return _mode == Mode::Stopped && _stop.valid;
  }

  void resume(Resume r, size_t depth = 0) {
    {
      std::lock_guard<std::mutex> lk(_mu);
      _resume = r;
      _nextDepth = depth;
      _stop.valid = false;
      _mode = Mode::Running;
    }
    _cv.notify_all();
  }

  // --- request handling ---

  void handleRequest(int64_t seq, const std::string &command,
                     const Json &args) {
    if (command == "initialize") {
      respond(seq, command,
              Json::Object{
                  {"supportsConfigurationDoneRequest", true},
                  {"supportsEvaluateForHovers", true},
                  {"supportsStepBack", false},
                  {"supportsRestartRequest", false},
                  {"supportsTerminateRequest", false},
                  {"exceptionBreakpointFilters", Json::Array{}}});
      // Spec: tell the client we're ready for breakpoint configuration.
      event("initialized");
      return;
    }
    if (command == "launch") {
      _program = args.stringOr("program");
      _procedure = args.stringOr("procedure", "main");
      _stopOnEntry = args.boolOr("stopOnEntry", false);
      _argStrings.clear();
      const Json *a = args.find("args");
      if (a && a->isArray()) {
        for (const Json &v : a->asArray()) {
          _argStrings.push_back(v.isString() ? v.asString()
                                             : v.dump());
        }
      }
      _launched = true;
      respond(seq, command, Json::Object{});
      return;
    }
    if (command == "setBreakpoints") {
      std::string path = args["source"].stringOr("path");
      path = normPath(path);
      _breakpoints[path].clear();
      Json::Array bps;
      const Json *list = args.find("breakpoints");
      if (list && list->isArray()) {
        for (const Json &b : list->asArray()) {
          int line = static_cast<int>(b.intOr("line"));
          _breakpoints[path].insert(line);
          bps.push_back(Json::Object{{"verified", true},
                                     {"line", line}});
        }
      }
      respond(seq, command, Json::Object{{"breakpoints", Json(std::move(bps))}});
      return;
    }
    if (command == "setExceptionBreakpoints") {
      // All uncaught script errors already surface as a stop.
      respond(seq, command, Json::Object{});
      return;
    }
    if (command == "configurationDone") {
      if (!_launched) {
        fail(seq, command, "launch request required first");
        return;
      }
      respond(seq, command, Json::Object{});
      startWorker();
      return;
    }
    if (command == "threads") {
      respond(seq, command,
              Json::Object{{"threads", Json::Array{Json::Object{
                                          {"id", 1},
                                          {"name", "main"}}}}});
      return;
    }
    if (command == "stackTrace") {
      if (!waitForStop()) {
        respond(seq, command,
                Json::Object{{"stackFrames", Json::Array{}},
                             {"totalFrames", 0}});
        return;
      }
      Json::Array frames;
      // Frame 0: current position. Deeper frames: names only (the hook
      // reports procedure names, not per-frame sites).
      frames.push_back(Json::Object{
          {"id", 1},
          {"name", _stop.procedure},
          {"line", _stop.line},
          {"column", _stop.col},
          {"source", Json::Object{{"name", _stop.filename},
                                  {"path", _stop.filename}}}});
      int id = 2;
      for (size_t i = _stop.callStack.size(); i-- > 0;) {
        if (_stop.callStack[i] == _stop.procedure && i == _stop.callStack.size() - 1) {
          continue; // same as frame 0
        }
        frames.push_back(Json::Object{{"id", id++},
                                      {"name", _stop.callStack[i]},
                                      {"line", 1},
                                      {"column", 1}});
      }
      int64_t total = static_cast<int64_t>(frames.size());
      respond(seq, command,
              Json::Object{{"stackFrames", Json(std::move(frames))},
                           {"totalFrames", total}});
      return;
    }
    if (command == "scopes") {
      if (!waitForStop()) {
        respond(seq, command, Json::Object{{"scopes", Json::Array{}}});
        return;
      }
      respond(seq, command,
              Json::Object{{"scopes", Json::Array{Json::Object{
                                         {"name", "Locals"},
                                         {"variablesReference", 1000},
                                         {"expensive", false}}}}});
      return;
    }
    if (command == "variables") {
      if (!waitForStop()) {
        respond(seq, command, Json::Object{{"variables", Json::Array{}}});
        return;
      }
      Json::Array vars;
      // Deterministic order: sort names.
      std::vector<std::string> names;
      names.reserve(_stop.variables.size());
      for (const auto &[n, _] : _stop.variables) {
        names.push_back(n);
      }
      std::sort(names.begin(), names.end());
      for (const auto &n : names) {
        const Value &v = _stop.variables[n];
        vars.push_back(Json::Object{
            {"name", n},
            {"value", ValueHelper::toString(v)},
            {"type", ValueHelper::typeToString(ValueHelper::getType(v))},
            {"variablesReference", 0}});
      }
      respond(seq, command,
              Json::Object{{"variables", Json(std::move(vars))}});
      return;
    }
    if (command == "evaluate") {
      if (!waitForStop()) {
        fail(seq, command, "not stopped");
        return;
      }
      std::string expr = args.stringOr("expression");
      // Same approach as the CLI debugger: evaluate in a probe manager
      // whose external variables mirror the stopped frame's locals.
      ScriptManager probe;
      for (const auto &kv : _stop.variables) {
        Value captured = kv.second;
        probe.registerExternalVariableReadOnly(
            kv.first, [captured]() { return captured; });
      }
      Value result;
      std::string error;
      if (probe.evaluateSnippet("return (" + expr + ");", "<dap>", result,
                                error)) {
        respond(seq, command,
                Json::Object{{"result", ValueHelper::toString(result)},
                             {"type", ValueHelper::typeToString(
                                          ValueHelper::getType(result))},
                             {"variablesReference", 0}});
      } else {
        fail(seq, command, error);
      }
      return;
    }
    if (command == "continue") {
      resume(Resume::Continue);
      respond(seq, command, Json::Object{{"allThreadsContinued", true}});
      event("continued", Json::Object{{"threadId", 1},
                                      {"allThreadsContinued", true}});
      return;
    }
    if (command == "next") {
      resume(Resume::Next, _stop.callDepth);
      respond(seq, command, Json::Object{});
      return;
    }
    if (command == "stepIn") {
      resume(Resume::Step);
      respond(seq, command, Json::Object{});
      return;
    }
    if (command == "stepOut") {
      resume(Resume::Next,
             _stop.callDepth > 0 ? _stop.callDepth - 1 : 0);
      respond(seq, command, Json::Object{});
      return;
    }
    if (command == "pause") {
      // Stop at the next executed statement.
      {
        std::lock_guard<std::mutex> lk(_mu);
        _resume = Resume::Step;
      }
      respond(seq, command, Json::Object{});
      return;
    }
    if (command == "disconnect" || command == "terminate") {
      respond(seq, command, Json::Object{});
      abort();
      return;
    }
    if (command == "source") {
      fail(seq, command, "source request not supported");
      return;
    }
    respond(seq, command, Json::Object{});
  }

  // --- worker ---

  void startWorker() {
    if (_worker.joinable()) {
      return; // already launched
    }
    {
      std::lock_guard<std::mutex> lk(_mu);
      _workerStarted = true;
      _mode = Mode::Running;
    }
    _worker = std::thread([this] { workerMain(); });
  }

  void workerMain() {
    ScriptManager mgr;
    mgr.setExecutionLimits(0, 0, 64 * 1024);

    std::vector<CompilationError> errors;
    if (!mgr.loadScriptFile(_program, errors)) {
      std::string msg;
      for (const auto &e : errors) {
        msg += e.toString() + "\n";
      }
      event("output",
            Json::Object{{"category", "stderr"}, {"output", msg}});
      finish(1);
      return;
    }
    if (!mgr.hasProcedure(_procedure)) {
      event("output", Json::Object{{"category", "stderr"},
                                   {"output", "Procedure not found: " +
                                                  _procedure + "\n"}});
      finish(1);
      return;
    }

    // Print builtins stream to the client as console output.
    mgr.setOutputCallback([this](const std::string &s) {
      event("output",
            Json::Object{{"category", "stdout"}, {"output", s}});
    });

    std::vector<Value> args;
    for (const auto &s : _argStrings) {
      args.push_back(parseArgValue(s));
    }

    mgr.setDebugHook(
        [this](const Interpreter::DebugContext &ctx) { onStatement(ctx); });

    Value result;
    std::string error;
    bool ok = false;
    try {
      ok = mgr.executeProcedure(_procedure, args, result, error);
    } catch (const DapAbort &) {
      finish(0);
      return;
    } catch (const std::exception &e) {
      error = e.what();
      ok = false;
    }

    if (!ok && !error.empty()) {
      event("output",
            Json::Object{{"category", "stderr"}, {"output", error + "\n"}});
      // Stay stopped so the client can inspect state, then terminate when
      // it continues/disconnects.
      {
        std::unique_lock<std::mutex> lk(_mu);
        _stop.valid = true;
        _stop.filename = "";
        _stop.line = 1;
        _stop.procedure = _procedure;
        _stop.callStack = {_procedure};
        _mode = Mode::Stopped;
      }
      _cv.notify_all();
      event("stopped",
            Json::Object{{"reason", "exception"},
                         {"threadId", 1},
                         {"text", error},
                         {"allThreadsStopped", true}});
      {
        std::unique_lock<std::mutex> lk(_mu);
        _cv.wait(lk, [&] { return _resume != Resume::None || _abort; });
      }
      finish(1);
      return;
    }
    finish(0);
  }

  void finish(int exitCode) {
    {
      std::lock_guard<std::mutex> lk(_mu);
      _mode = Mode::Finished;
    }
    _cv.notify_all();
    event("terminated");
    event("exited", Json::Object{{"exitCode", exitCode}});
  }

  // Runs inside the interpreter thread before each statement.
  void onStatement(const Interpreter::DebugContext &ctx) {
    std::unique_lock<std::mutex> lk(_mu);
    if (_abort) {
      throw DapAbort{};
    }

    bool first = !_entrySeen;
    _entrySeen = true;
    bool stop = false;
    std::string reason;
    switch (_resume) {
    case Resume::Step:
      stop = true;
      reason = "step";
      break;
    case Resume::Next:
      if (ctx.callDepth <= _nextDepth) {
        stop = true;
        reason = "step";
      }
      break;
    default:
      break;
    }
    if (!stop && first && _stopOnEntry) {
      stop = true;
      reason = "entry";
    }
    if (!stop) {
      auto it = _breakpoints.find(normPath(ctx.filename));
      if (it != _breakpoints.end() && it->second.count(ctx.line)) {
        stop = true;
        reason = "breakpoint";
      }
    }
    if (!stop) {
      return;
    }

    _stop.valid = true;
    _stop.filename = ctx.filename;
    _stop.line = ctx.line;
    _stop.col = ctx.column;
    _stop.procedure = ctx.procedure;
    _stop.callDepth = ctx.callDepth;
    _stop.callStack = ctx.callStack;
    _stop.variables = ctx.variables;
    _resume = Resume::None;
    _mode = Mode::Stopped;
    lk.unlock();
    _cv.notify_all(); // wake waiters (stackTrace/scopes/variables/...)
    event("stopped", Json::Object{{"reason", reason},
                                  {"threadId", 1},
                                  {"allThreadsStopped", true}});
    lk.lock();
    _cv.wait(lk, [&] { return _resume != Resume::None || _abort; });
    if (_abort) {
      throw DapAbort{};
    }
  }

  bool _entrySeen = false;
};

} // namespace

int runDebugAdapter(std::istream &in, std::ostream &out) {
  DapServer srv(in, out);
  return srv.run();
}

} // namespace Script
