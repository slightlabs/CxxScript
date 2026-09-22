#include "Json.h"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <limits>

namespace Script {

Json::Kind Json::kind() const {
  switch (_v.index()) {
  case 0:
    return Kind::Null;
  case 1:
    return Kind::Bool;
  case 2:
    return Kind::Int;
  case 3:
    return Kind::Double;
  case 4:
    return Kind::String;
  case 5:
    return Kind::Array;
  default:
    return Kind::Object;
  }
}

const Json &Json::at(const std::string &key) const {
  if (!isObject()) {
    throw JsonError("not an object", 0);
  }
  auto it = asObject().find(key);
  if (it == asObject().end()) {
    throw JsonError("missing key: " + key, 0);
  }
  return it->second;
}

const Json *Json::find(const std::string &key) const {
  if (!isObject()) {
    return nullptr;
  }
  auto it = asObject().find(key);
  return it == asObject().end() ? nullptr : &it->second;
}

const Json &Json::operator[](size_t i) const {
  if (!isArray() || i >= asArray().size()) {
    throw JsonError("array index out of range", 0);
  }
  return asArray()[i];
}

size_t Json::size() const {
  if (isArray()) {
    return asArray().size();
  }
  if (isObject()) {
    return asObject().size();
  }
  return 0;
}

std::string Json::stringOr(const std::string &key,
                           const std::string &fallback) const {
  const Json *v = find(key);
  return v && v->isString() ? v->asString() : fallback;
}

int64_t Json::intOr(const std::string &key, int64_t fallback) const {
  const Json *v = find(key);
  if (!v) {
    return fallback;
  }
  if (v->isInt()) {
    return v->asInt();
  }
  if (v->isDouble()) {
    return static_cast<int64_t>(v->asDouble());
  }
  return fallback;
}

bool Json::boolOr(const std::string &key, bool fallback) const {
  const Json *v = find(key);
  return v && v->isBool() ? v->asBool() : fallback;
}

// ---------------------------------------------------------------------------
// Serialization

namespace {

void escapeString(const std::string &s, std::string &out) {
  out += '"';
  for (char c : s) {
    unsigned char u = static_cast<unsigned char>(c);
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (u < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04x", u);
        out += buf;
      } else {
        out += c; // UTF-8 bytes pass through untouched
      }
    }
  }
  out += '"';
}

void dumpValue(const Json &v, std::string &out, int indent, int depth) {
  auto newline = [&]() {
    if (indent >= 0) {
      out += '\n';
      out.append(static_cast<size_t>(indent) * (depth + 1), ' ');
    }
  };
  switch (v.kind()) {
  case Json::Kind::Null:
    out += "null";
    break;
  case Json::Kind::Bool:
    out += v.asBool() ? "true" : "false";
    break;
  case Json::Kind::Int:
    out += std::to_string(v.asInt());
    break;
  case Json::Kind::Double: {
    double d = v.asDouble();
    if (!std::isfinite(d)) {
      out += "null"; // JSON has no NaN/Infinity
      break;
    }
    // Shortest round-trip-ish: %.17g is exact; trim when %.15g round-trips.
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.15g", d);
    double parsed = std::strtod(buf, nullptr);
    if (parsed != d) {
      std::snprintf(buf, sizeof(buf), "%.17g", d);
    }
    out += buf;
    // Keep it parseable as a number when %g yields an integer form.
    if (std::string(buf).find_first_of(".eEnN") == std::string::npos) {
      out += ".0";
    }
    break;
  }
  case Json::Kind::String:
    escapeString(v.asString(), out);
    break;
  case Json::Kind::Array: {
    out += '[';
    bool first = true;
    for (const Json &el : v.asArray()) {
      if (!first) {
        out += ',';
      }
      first = false;
      newline();
      dumpValue(el, out, indent, depth + 1);
    }
    if (!first) {
      if (indent >= 0) {
        out += '\n';
        out.append(static_cast<size_t>(indent) * depth, ' ');
      }
    }
    out += ']';
    break;
  }
  case Json::Kind::Object: {
    out += '{';
    bool first = true;
    for (const auto &[k, el] : v.asObject()) {
      if (!first) {
        out += ',';
      }
      first = false;
      newline();
      escapeString(k, out);
      out += ':';
      if (indent >= 0) {
        out += ' ';
      }
      dumpValue(el, out, indent, depth + 1);
    }
    if (!first && indent >= 0) {
      out += '\n';
      out.append(static_cast<size_t>(indent) * depth, ' ');
    }
    out += '}';
    break;
  }
  }
}

} // namespace

std::string Json::dump(int indent) const {
  std::string out;
  dumpValue(*this, out, indent, 0);
  return out;
}

// ---------------------------------------------------------------------------
// Parsing

namespace {

struct JsonParser {
  const std::string &s;
  size_t pos = 0;

  [[noreturn]] void fail(const std::string &msg) {
    throw JsonError(msg, pos);
  }

  void skipWs() {
    while (pos < s.size() &&
           (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' ||
            s[pos] == '\r')) {
      ++pos;
    }
  }

  char peek() const { return pos < s.size() ? s[pos] : '\0'; }
  char take() { return pos < s.size() ? s[pos++] : '\0'; }

  void expect(char c) {
    if (take() != c) {
      fail(std::string("expected '") + c + "'");
    }
  }

  void literal(const char *word) {
    for (const char *p = word; *p; ++p) {
      if (take() != *p) {
        fail(std::string("expected '") + word + "'");
      }
    }
  }

  Json value() {
    skipWs();
    switch (peek()) {
    case 'n':
      literal("null");
      return Json(nullptr);
    case 't':
      literal("true");
      return Json(true);
    case 'f':
      literal("false");
      return Json(false);
    case '"':
      return Json(string());
    case '[':
      return Json(array());
    case '{':
      return Json(object());
    default:
      if (peek() == '-' || (peek() >= '0' && peek() <= '9')) {
        return number();
      }
      fail("unexpected character");
    }
  }

  Json number() {
    size_t start = pos;
    if (peek() == '-') {
      ++pos;
    }
    if (peek() == '0') {
      ++pos;
    } else {
      digits();
    }
    bool isDouble = false;
    if (peek() == '.') {
      isDouble = true;
      ++pos;
      digits();
    }
    if (peek() == 'e' || peek() == 'E') {
      isDouble = true;
      ++pos;
      if (peek() == '+' || peek() == '-') {
        ++pos;
      }
      digits();
    }
    std::string text = s.substr(start, pos - start);
    if (!isDouble) {
      int64_t v;
      auto [ptr, ec] =
          std::from_chars(text.data(), text.data() + text.size(), v);
      if (ec == std::errc()) {
        return Json(v);
      }
      // Out of int64 range — fall through to double.
    }
    char *end = nullptr;
    errno = 0;
    double d = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size() || errno == ERANGE) {
      fail("invalid number");
    }
    return Json(d);
  }

  void digits() {
    if (peek() < '0' || peek() > '9') {
      fail("expected digit");
    }
    while (peek() >= '0' && peek() <= '9') {
      ++pos;
    }
  }

  void appendUtf8(uint32_t cp, std::string &out) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  uint32_t hex4() {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      char c = take();
      v <<= 4;
      if (c >= '0' && c <= '9') {
        v |= static_cast<uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        v |= static_cast<uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        v |= static_cast<uint32_t>(c - 'A' + 10);
      } else {
        fail("invalid \\u escape");
      }
    }
    return v;
  }

  std::string string() {
    expect('"');
    std::string out;
    while (true) {
      char c = take();
      if (c == '\0' && pos >= s.size()) {
        fail("unterminated string");
      }
      if (c == '"') {
        return out;
      }
      if (static_cast<unsigned char>(c) < 0x20) {
        fail("control character in string");
      }
      if (c != '\\') {
        out += c;
        continue;
      }
      switch (take()) {
      case '"':
        out += '"';
        break;
      case '\\':
        out += '\\';
        break;
      case '/':
        out += '/';
        break;
      case 'b':
        out += '\b';
        break;
      case 'f':
        out += '\f';
        break;
      case 'n':
        out += '\n';
        break;
      case 'r':
        out += '\r';
        break;
      case 't':
        out += '\t';
        break;
      case 'u': {
        uint32_t cp = hex4();
        if (cp >= 0xD800 && cp <= 0xDBFF) {
          // Surrogate pair
          if (take() != '\\' || take() != 'u') {
            fail("unpaired surrogate");
          }
          uint32_t lo = hex4();
          if (lo < 0xDC00 || lo > 0xDFFF) {
            fail("unpaired surrogate");
          }
          cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
          fail("unpaired surrogate");
        }
        appendUtf8(cp, out);
        break;
      }
      default:
        fail("invalid escape");
      }
    }
  }

  Json::Array array() {
    expect('[');
    Json::Array out;
    skipWs();
    if (peek() == ']') {
      ++pos;
      return out;
    }
    while (true) {
      out.push_back(value());
      skipWs();
      char c = take();
      if (c == ']') {
        return out;
      }
      if (c != ',') {
        fail("expected ',' or ']'");
      }
    }
  }

  Json::Object object() {
    expect('{');
    Json::Object out;
    skipWs();
    if (peek() == '}') {
      ++pos;
      return out;
    }
    while (true) {
      skipWs();
      if (peek() != '"') {
        fail("expected string key");
      }
      std::string key = string();
      skipWs();
      expect(':');
      out[key] = value();
      skipWs();
      char c = take();
      if (c == '}') {
        return out;
      }
      if (c != ',') {
        fail("expected ',' or '}'");
      }
    }
  }
};

} // namespace

Json Json::parse(const std::string &text) {
  JsonParser p{text};
  Json v = p.value();
  p.skipWs();
  if (p.pos != text.size()) {
    throw JsonError("trailing characters after JSON value", p.pos);
  }
  return v;
}

} // namespace Script
