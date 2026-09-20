#pragma once

#include <string>
#include <vector>

namespace Script {

// Pretty-prints CxxScript source: canonical two-space indentation,
// operator spacing, and brace placement. Comments are collected from the
// original text and re-emitted in source order ahead of the declaration
// or statement that follows them.
//
// Known limitations:
//  - A trailing comment moves to the line above its statement.
//  - Numeric literal spellings normalize to decimal (0x10 -> 16).
// Throws ParseError (via Parser) when the source does not parse.
class Formatter {
public:
  static std::string format(const std::string &source,
                            const std::string &filename = "<fmt>");
};

} // namespace Script
