// libFuzzer target: lex + parse arbitrary input as both a script file and
// a statement sequence. The parser recovers via synchronize(), so errors
// are expected; crashes are not.
//
//   ./build/fuzz/fuzz_parser corpus/ -max_total_time=60
#include <cstddef>
#include <cstdint>
#include <string>

#include "Lexer.h"
#include "Parser.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  std::string input(reinterpret_cast<const char *>(data), size);
  try {
    Script::Lexer lexer(input, "<fuzz>");
    auto tokens = lexer.tokenize();

    {
      Script::Parser parser(tokens, "<fuzz>");
      (void)parser.parse();
    }
    {
      Script::Parser parser(tokens, "<fuzz>");
      (void)parser.parseStatements();
    }
    {
      Script::Parser parser(tokens, "<fuzz>");
      (void)parser.parseExpression();
    }
  } catch (...) {
    // ParseError is expected for malformed input.
  }
  return 0;
}
