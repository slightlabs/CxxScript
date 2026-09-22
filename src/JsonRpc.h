#pragma once

// Internal helpers shared by the LSP server and DAP adapter: JSON-RPC
// messages framed with `Content-Length` headers over iostreams.

#include "Json.h"

#include <cstdlib>
#include <istream>
#include <ostream>
#include <string>

namespace Script {
namespace jsonrpc {

// Read one framed message. Returns false on EOF/stream error; a malformed
// body is reported via `out` being left default-constructed (Null) with
// `ok` set false.
inline bool readMessage(std::istream &in, Json &out, bool &ok) {
  ok = false;
  size_t length = 0;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      break;
    }
    if (line.rfind("Content-Length:", 0) == 0) {
      length =
          static_cast<size_t>(std::strtoull(line.c_str() + 15, nullptr, 10));
    }
  }
  if (!in || length == 0) {
    return false;
  }
  std::string body(length, '\0');
  in.read(&body[0], static_cast<std::streamsize>(length));
  if (!in) {
    return false;
  }
  try {
    out = Json::parse(body);
    ok = true;
  } catch (const JsonError &) {
    // Skip malformed payload; keep the stream alive.
  }
  return true;
}

inline bool readMessage(std::istream &in, Json &out) {
  bool ok;
  return readMessage(in, out, ok);
}

inline void writeMessage(std::ostream &out, const Json &msg) {
  std::string body = msg.dump();
  out << "Content-Length: " << body.size() << "\r\n\r\n" << body;
  out.flush();
}

} // namespace jsonrpc
} // namespace Script
