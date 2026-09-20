// cxxscript-bench — microbenchmarks for the lexer, parser, validator, and
// interpreter. Dependency-free; measures wall-clock with steady_clock.
//
// Build: cmake -DCXXSCRIPT_BUILD_BENCHMARKS=ON ..
// Run:   ./build/bin/cxxscript_bench [iterations]
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "Lexer.h"
#include "Parser.h"
#include "ScriptManager.h"

using namespace Script;
using Clock = std::chrono::steady_clock;

namespace {

int g_iters = 20;

struct Result {
  std::string name;
  double usPerOp;
};

template <typename F>
Result bench(const std::string &name, int iters, F &&fn) {
  fn(); // warmup
  auto t0 = Clock::now();
  for (int i = 0; i < iters; ++i) {
    fn();
  }
  double us =
      std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
  return {name, us / iters};
}

const char *kFib = R"(
int32 fib(int32 n) {
  if (n < 2) { return n; }
  return fib(n - 1) + fib(n - 2);
}
int32 main() { return fib(18); }
)";

const char *kArray = R"(
int32 main() {
  int32[] a;
  for (int32 i = 0; i < 2000; i += 1) { push(a, i); }
  int32 s = 0;
  for (int32 v : a) { s += v; }
  return s;
}
)";

const char *kMap = R"(
int32 main() {
  map<string, int32> m;
  for (int32 i = 0; i < 1000; i += 1) {
    m["k" + toString(i)] = i;
  }
  int32 s = 0;
  for (int32 i = 0; i < 1000; i += 1) {
    if (has(m, "k" + toString(i))) { s += m["k" + toString(i)]; }
  }
  return s;
}
)";

const char *kStrings = R"(
int32 main() {
  string s = "";
  for (int32 i = 0; i < 200; i += 1) { s += "abc"; }
  return len(s) + len(toUpper(s));
}
)";

const char *kLambda = R"(
int32 apply(fn(int32) -> int32 f, int32 x) { return f(x); }
int32 main() {
  auto inc = fn(int32 x) -> int32 { return x + 1; };
  int32 s = 0;
  for (int32 i = 0; i < 500; i += 1) { s += apply(inc, i); }
  return s;
}
)";

const char *kExceptions = R"(
int32 main() {
  int32 n = 0;
  for (int32 i = 0; i < 500; i += 1) {
    try { throw i; } catch (int64 e) { n += e; }
  }
  return n;
}
)";

ScriptManager &compiled(const char *src,
                        std::vector<std::unique_ptr<ScriptManager>> &keep) {
  auto mgr = std::make_unique<ScriptManager>();
  std::vector<CompilationError> errors;
  if (!mgr->loadScriptSource(src, "bench.script", errors)) {
    fprintf(stderr, "bench script failed to compile\n");
    std::abort();
  }
  keep.push_back(std::move(mgr));
  return *keep.back();
}

} // namespace

int main(int argc, char **argv) {
  if (argc > 1) {
    g_iters = std::max(1, std::atoi(argv[1]));
  }

  // A representative mid-size source for lex/parse throughput.
  std::string big;
  for (int i = 0; i < 40; ++i) {
    big += "int32 fn" + std::to_string(i) +
           "(int32 x) { int32[] a = [1,2,3]; return x + a[0] * 2; }\n";
  }

  std::vector<Result> results;
  results.push_back(bench("lex 4KB source", g_iters * 4, [&] {
    Lexer lx(big, "bench");
    auto t = lx.tokenize();
    if (t.empty())
      std::abort();
  }));
  results.push_back(bench("parse 4KB source", g_iters * 4, [&] {
    Lexer lx(big, "bench");
    auto t = lx.tokenize();
    Parser p(t, "bench");
    (void)p.parse();
  }));
  results.push_back(bench("compile 4KB (validate+load)", g_iters, [&] {
    ScriptManager m;
    std::vector<CompilationError> errors;
    m.loadScriptSource(big, "bench", errors);
  }));

  std::vector<std::unique_ptr<ScriptManager>> keep;
  for (auto &[name, src] :
       std::vector<std::pair<std::string, const char *>>{
           {"fib(18) recursion", kFib},
           {"array push+foreach x2000", kArray},
           {"map put+get x1000", kMap},
           {"string concat x200", kStrings},
           {"lambda calls x500", kLambda},
           {"throw/catch x500", kExceptions}}) {
    ScriptManager &mgr = compiled(src, keep);
    results.push_back(bench("run " + name, g_iters, [&] {
      Value out;
      std::string err;
      if (!mgr.executeProcedure("main", {}, out, err)) {
        fprintf(stderr, "bench '%s' failed: %s\n", name.c_str(),
                err.c_str());
        std::abort();
      }
    }));
  }

  printf("%-32s %12s\n", "benchmark", "us/op");
  printf("%-32s %12s\n", "--------------------------------", "------------");
  for (const auto &r : results) {
    printf("%-32s %12.1f\n", r.name.c_str(), r.usPerOp);
  }
  return 0;
}
