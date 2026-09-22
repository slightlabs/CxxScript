#pragma once

#include <iosfwd>

namespace Script {

// Run a Language Server Protocol session over the given streams using
// JSON-RPC with Content-Length framing (the standard stdio transport).
// Implements: initialize/shutdown/exit lifecycle, full-document sync,
// publishDiagnostics, completion, hover, definition, documentSymbol and
// formatting. Returns 0 on a clean exit notification.
int runLanguageServer(std::istream &in, std::ostream &out);

} // namespace Script
