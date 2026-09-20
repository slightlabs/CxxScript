// libFuzzer target: end-to-end — compile arbitrary input through
// ScriptManager (lexer + parser + semantic validator), then run `main`
// under tight resource limits when it exists. Hangs are prevented by the
// engine's step/call/memory limits; anything that escapes them is a bug.
//
//   ./build/fuzz/fuzz_script corpus/ -max_total_time=60
#include <cstddef>
#include <cstdint>
#include <string>

#include "ScriptManager.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  // Deeply nested inputs can burn validator stack — cap the size.
  if (size > 64 * 1024) {
    return 0;
  }
  std::string input(reinterpret_cast<const char *>(data), size);
  try {
    Script::ScriptManager manager;
    std::vector<Script::CompilationError> errors;
    if (!manager.loadScriptSource(input, "<fuzz>", errors)) {
      return 0;
    }
    if (!manager.hasProcedure("main")) {
      return 0;
    }
    // Hard limits so hostile scripts terminate quickly.
    manager.setExecutionLimits(64, 100000);
    manager.setMemoryLimits(4096, 4096, 4096);
    Script::Value result;
    std::string error;
    manager.executeProcedure("main", {}, result, error);
  } catch (...) {
    // Compilation and runtime errors are both fine.
  }
  return 0;
}
