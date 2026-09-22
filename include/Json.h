#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace Script {

class JsonError : public std::runtime_error {
public:
  size_t offset;
  JsonError(const std::string &message, size_t off)
      : std::runtime_error(message), offset(off) {}
};

// Standalone JSON value (RFC 8259). Used by the LSP server and the json.*
// builtins; deliberately independent of the script Value variant so the
// language server and any host code can use it without semantics.
class Json {
public:
  using Array = std::vector<Json>;
  using Object = std::map<std::string, Json>;

  enum class Kind { Null, Bool, Int, Double, String, Array, Object };

  Json() : _v(nullptr) {}
  Json(std::nullptr_t) : _v(nullptr) {}
  Json(bool b) : _v(b) {}
  Json(int i) : _v(static_cast<int64_t>(i)) {}
  Json(int64_t i) : _v(i) {}
  Json(double d) : _v(d) {}
  Json(const char *s) : _v(std::string(s)) {}
  Json(const std::string &s) : _v(s) {}
  Json(std::string &&s) : _v(std::move(s)) {}
  Json(const Array &a) : _v(a) {}
  Json(Array &&a) : _v(std::move(a)) {}
  Json(const Object &o) : _v(o) {}
  Json(Object &&o) : _v(std::move(o)) {}

  Kind kind() const;
  bool isNull() const { return kind() == Kind::Null; }
  bool isBool() const { return kind() == Kind::Bool; }
  bool isInt() const { return kind() == Kind::Int; }
  bool isDouble() const { return kind() == Kind::Double; }
  bool isNumber() const { return isInt() || isDouble(); }
  bool isString() const { return kind() == Kind::String; }
  bool isArray() const { return kind() == Kind::Array; }
  bool isObject() const { return kind() == Kind::Object; }

  bool asBool() const { return std::get<bool>(_v); }
  int64_t asInt() const { return std::get<int64_t>(_v); }
  double asDouble() const {
    return isDouble() ? std::get<double>(_v) : static_cast<double>(asInt());
  }
  const std::string &asString() const { return std::get<std::string>(_v); }
  const Array &asArray() const { return std::get<Array>(_v); }
  Array &asArray() { return std::get<Array>(_v); }
  const Object &asObject() const { return std::get<Object>(_v); }
  Object &asObject() { return std::get<Object>(_v); }

  // Object access: `at` throws JsonError on missing key/non-object;
  // `find` returns nullptr.
  const Json &at(const std::string &key) const;
  const Json *find(const std::string &key) const;
  const Json &operator[](const std::string &key) const { return at(key); }
  const Json &operator[](size_t i) const;
  size_t size() const; // array/object element count, 0 otherwise

  // Convenience getters with defaults for protocol code.
  std::string stringOr(const std::string &key,
                       const std::string &fallback = "") const;
  int64_t intOr(const std::string &key, int64_t fallback = 0) const;
  bool boolOr(const std::string &key, bool fallback = false) const;

  // Serialize. indent < 0 = compact; otherwise pretty-print with that
  // many spaces per level.
  std::string dump(int indent = -1) const;

  // Parse a JSON document; throws JsonError on malformed input or
  // trailing garbage.
  static Json parse(const std::string &text);

private:
  std::variant<std::nullptr_t, bool, int64_t, double, std::string, Array,
               Object>
      _v;
};

} // namespace Script
