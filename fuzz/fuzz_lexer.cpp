// libFuzzer target: feed arbitrary bytes through the lexer. Must never
// crash or hang; lexer errors surface as UNKNOWN tokens or LexerError.
//
// Build (clang):
//   cmake -DCXXSCRIPT_BUILD_FUZZERS=ON -DCMAKE_CXX_COMPILER=clang++ ..
// Run:
//   ./build/fuzz/fuzz_lexer corpus/ -max_total_time=60
#include <cstddef>
#include <cstdint>
#include <string>

#include "Lexer.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  std::string input(reinterpret_cast<const char *>(data), size);
  try {
    Script::Lexer lexer(input, "<fuzz>");
    auto tokens = lexer.tokenize();
    // Tokenize *through* END_OF_FILE: touch every token's fields so lazy
    // defects can't hide.
    size_t len = 0;
    for (const auto &t : tokens) {
      len += t.lexeme.size() + t.stringValue.size();
    }
    (void)len;
  } catch (...) {
    // LexerError / ParseError are fine — crashes and hangs are not.
  }
  return 0;
}
