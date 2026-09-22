#pragma once

#include <iosfwd>

namespace Script {

// Run a Debug Adapter Protocol session over the given streams using
// Content-Length framed messages (the standard stdio transport).
// Supports: initialize, launch (program/procedure/args/stopOnEntry),
// setBreakpoints, configurationDone, threads, stackTrace, scopes,
// variables, evaluate, continue/next/stepIn/stepOut, pause, disconnect.
// Returns 0 on disconnect or EOF.
int runDebugAdapter(std::istream &in, std::ostream &out);

} // namespace Script
