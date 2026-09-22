#pragma once

// Read a process environment variable portably. MSVC's /sdl elevates the
// std::getenv deprecation (C4996) to a hard error; _dupenv_s is the checked
// equivalent there.
#include <cstdlib>
#include <optional>
#include <string>
#ifdef _MSC_VER
#include <stdlib.h> // _dupenv_s lives in the global namespace
#endif

namespace Script {

inline std::optional<std::string> envVar(const char *name) {
#ifdef _MSC_VER
  char *buf = nullptr;
  size_t len = 0;
  if (_dupenv_s(&buf, &len, name) != 0 || buf == nullptr) {
    return std::nullopt;
  }
  std::string out(buf, len ? len - 1 : 0);
  std::free(buf);
  return out;
#else
  const char *v = std::getenv(name);
  if (!v) {
    return std::nullopt;
  }
  return std::string(v);
#endif
}

} // namespace Script
