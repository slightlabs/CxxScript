#include "Builtins.h"
#include "Interpreter.h"
#include "Json.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace Script {

void Builtins::rethrowCallError(Interpreter &in, CallExpr *e,
                                const std::exception &ex, const char *prefix) {
  if (const auto *re = dynamic_cast<const RuntimeError *>(&ex);
      re && re->fatal) {
    throw *re;
  }
  std::string msg = ex.what();
  if (prefix) {
    msg = std::string(prefix) + msg;
  }
  throw in.runtimeError(msg, e->line, e->column);
}

bool Builtins::isBuiltin(const std::string &name) {
  return table().find(name) != table().end();
}

std::vector<std::string> Builtins::builtinNames() {
  std::vector<std::string> names;
  names.reserve(table().size());
  for (const auto &[name, _] : table()) {
    names.push_back(name);
  }
  return names;
}

Value Builtins::call(Interpreter &interp, const std::string &name,
                     CallExpr *expr) {
  auto it = table().find(name);
  if (it == table().end()) {
    throw interp.runtimeError("Unknown builtin: " + name, expr->line,
                              expr->column);
  }
  return it->second(interp, expr);
}

const std::unordered_map<std::string, Builtins::Handler> &Builtins::table() {
  static const std::unordered_map<std::string, Handler> handlers = {
      // Arrays / strings
      {"len", &bLen},         {"push", &bPush},
      {"pop", &bPop},         {"insert", &bInsert},
      {"removeAt", &bRemoveAt}, {"clear", &bClear},
      // Maps
      {"has", &bHas},         {"remove", &bRemove},
      {"keys", &bKeys},       {"values", &bValues},
      {"size", &bSize},
      // Strings
      {"substr", &bSubstr},   {"charAt", &bCharAt},
      {"indexOf", &bIndexOf}, {"contains", &bContains},
      {"startsWith", &bStartsWith}, {"endsWith", &bEndsWith},
      {"toUpper", &bToUpper}, {"toLower", &bToLower},
      {"trim", &bTrim},       {"replace", &bReplace},
      {"split", &bSplit},     {"join", &bJoin},
      {"repeat", &bRepeat},   {"reverse", &bReverse},
      {"format", &bFormat},
      // Math
      {"abs", &bAbs},         {"min", &bMin},
      {"max", &bMax},         {"clamp", &bClamp},
      {"pow", &bPow},         {"sqrt", &bSqrt},
      {"floor", &bFloor},     {"ceil", &bCeil},
      {"round", &bRound},     {"trunc", &bTrunc},
      {"fmod", &bFmod},       {"sin", &bSin},
      {"cos", &bCos},         {"tan", &bTan},
      {"asin", &bAsin},       {"acos", &bAcos},
      {"atan", &bAtan},       {"atan2", &bAtan2},
      {"exp", &bExp},         {"log", &bLog},
      {"log10", &bLog10},     {"random", &bRandom},
      {"randInt", &bRandInt}, {"srand", &bSrand},
      {"pi", &bPi},
      // Conversions / introspection
      {"toInt", &bToInt},     {"toUInt", &bToUInt},
      {"toDouble", &bToDouble}, {"toFloat", &bToFloat},
      {"toString", &bToString}, {"toBool", &bToBool},
      {"toChar", &bToChar},   {"parseInt", &bParseInt},
      {"parseDouble", &bParseDouble}, {"typeof", &bTypeof},
      {"isArray", &bIsArray}, {"isMap", &bIsMap},
      {"isNull", &bIsNull},
      // JSON
      {"jsonParse", &bJsonParse},
      {"jsonStringify", &bJsonStringify},
      // Output / control
      {"print", &bPrint},     {"println", &bPrintln},
      {"error", &bError},     {"assert", &bAssert},
  };
  return handlers;
}

// ---------------------------------------------------------------------------
// Argument helpers
// ---------------------------------------------------------------------------

void Builtins::arity(Interpreter &in, CallExpr *e, const char *name, size_t min,
                     size_t max) {
  size_t n = e->arguments.size();
  if (n < min || n > max) {
    std::stringstream ss;
    ss << name << " expects ";
    if (min == max) {
      ss << min;
    } else {
      ss << min << "-" << max;
    }
    ss << " argument(s), got " << n;
    throw in.runtimeError(ss.str(), e->line, e->column);
  }
}

Value Builtins::arg(Interpreter &in, CallExpr *e, size_t i) {
  return in.evaluate(e->arguments[i]);
}

std::string Builtins::str(Interpreter &in, CallExpr *e, size_t i) {
  Value v = arg(in, e, i);
  if (!std::holds_alternative<std::string>(v)) {
    throw in.runtimeError(std::string(e->functionName) +
                              " expects a string argument",
                          e->line, e->column);
  }
  return std::get<std::string>(v);
}

int64_t Builtins::integer(Interpreter &in, CallExpr *e, size_t i) {
  try {
    return ValueHelper::toInt64(arg(in, e, i));
  } catch (const std::exception &ex) {
    rethrowCallError(in, e, ex);
  }
}

double Builtins::real(Interpreter &in, CallExpr *e, size_t i) {
  try {
    return ValueHelper::toDouble(arg(in, e, i));
  } catch (const std::exception &ex) {
    rethrowCallError(in, e, ex);
  }
}

bool Builtins::boolean(Interpreter &in, CallExpr *e, size_t i) {
  return ValueHelper::toBool(arg(in, e, i));
}

ArrayPtr Builtins::array(Interpreter &in, CallExpr *e, size_t i) {
  Value v = arg(in, e, i);
  if (!ValueHelper::isArray(v)) {
    throw in.runtimeError(std::string(e->functionName) +
                              " expects an array argument",
                          e->line, e->column);
  }
  return std::get<ArrayPtr>(v);
}

// ---------------------------------------------------------------------------
// Arrays
// ---------------------------------------------------------------------------

Value Builtins::bLen(Interpreter &in, CallExpr *e) {
  arity(in, e, "len", 1, 1);
  Value v = arg(in, e, 0);
  if (ValueHelper::isArray(v)) {
    return static_cast<int32_t>(ValueHelper::arrayElements(v).size());
  }
  if (ValueHelper::isMap(v)) {
    return static_cast<int32_t>(ValueHelper::mapEntries(v).size());
  }
  if (std::holds_alternative<std::string>(v)) {
    return static_cast<int32_t>(std::get<std::string>(v).size());
  }
  throw in.runtimeError("len expects an array, map, or string", e->line,
                        e->column);
}

Value Builtins::bPush(Interpreter &in, CallExpr *e) {
  arity(in, e, "push", 2, 2);
  Value arrVal = arg(in, e, 0);
  if (!ValueHelper::isArray(arrVal)) {
    throw in.runtimeError("push expects an array as first argument", e->line,
                          e->column);
  }
  TypeInfo elemType = ValueHelper::arrayElementType(arrVal);
  Value raw = arg(in, e, 1);
  Value converted;
  try {
    converted = in.convertToType(raw, elemType);
  } catch (const std::exception &ex) {
    rethrowCallError(in, e, ex);
  }
  auto &elems = ValueHelper::arrayElements(arrVal);
  try {
    in.checkArraySize(elems.size() + 1);
  } catch (const std::exception &ex) {
    rethrowCallError(in, e, ex);
  }
  elems.push_back(converted);
  return static_cast<int32_t>(elems.size());
}

Value Builtins::bPop(Interpreter &in, CallExpr *e) {
  arity(in, e, "pop", 1, 1);
  Value arrVal = arg(in, e, 0);
  if (!ValueHelper::isArray(arrVal)) {
    throw in.runtimeError("pop expects an array", e->line, e->column);
  }
  auto &elems = ValueHelper::arrayElements(arrVal);
  if (elems.empty()) {
    throw in.runtimeError("Cannot pop from empty array", e->line, e->column);
  }
  Value result = elems.back();
  elems.pop_back();
  return result;
}

Value Builtins::bInsert(Interpreter &in, CallExpr *e) {
  arity(in, e, "insert", 3, 3);
  Value arrVal = arg(in, e, 0);
  if (!ValueHelper::isArray(arrVal)) {
    throw in.runtimeError("insert expects an array as first argument", e->line,
                          e->column);
  }
  auto &elems = ValueHelper::arrayElements(arrVal);
  int64_t idx = integer(in, e, 1);
  if (idx < 0 || static_cast<size_t>(idx) > elems.size()) {
    throw in.runtimeError("insert index out of bounds", e->line, e->column);
  }
  try {
    in.checkArraySize(elems.size() + 1);
  } catch (const std::exception &ex) {
    rethrowCallError(in, e, ex);
  }
  TypeInfo elemType = ValueHelper::arrayElementType(arrVal);
  Value converted;
  try {
    converted = in.convertToType(arg(in, e, 2), elemType);
  } catch (const std::exception &ex) {
    rethrowCallError(in, e, ex);
  }
  elems.insert(elems.begin() + idx, converted);
  return static_cast<int32_t>(elems.size());
}

Value Builtins::bRemoveAt(Interpreter &in, CallExpr *e) {
  arity(in, e, "removeAt", 2, 2);
  Value arrVal = arg(in, e, 0);
  if (!ValueHelper::isArray(arrVal)) {
    throw in.runtimeError("removeAt expects an array", e->line, e->column);
  }
  auto &elems = ValueHelper::arrayElements(arrVal);
  int64_t idx = integer(in, e, 1);
  if (idx < 0 || static_cast<size_t>(idx) >= elems.size()) {
    throw in.runtimeError("removeAt index out of bounds", e->line, e->column);
  }
  Value removed = elems[static_cast<size_t>(idx)];
  elems.erase(elems.begin() + idx);
  return removed;
}

Value Builtins::bClear(Interpreter &in, CallExpr *e) {
  arity(in, e, "clear", 1, 1);
  Value v = arg(in, e, 0);
  if (ValueHelper::isMap(v)) {
    std::get<MapPtr>(v)->entries.clear();
    return static_cast<int32_t>(0);
  }
  if (!ValueHelper::isArray(v)) {
    throw in.runtimeError("clear expects an array or map", e->line, e->column);
  }
  std::get<ArrayPtr>(v)->elements.clear();
  return static_cast<int32_t>(0);
}

// ---------------------------------------------------------------------------
// Maps
// ---------------------------------------------------------------------------

Value Builtins::bHas(Interpreter &in, CallExpr *e) {
  arity(in, e, "has", 2, 2);
  Value mv = arg(in, e, 0);
  if (!ValueHelper::isMap(mv)) {
    throw in.runtimeError("has expects a map as first argument", e->line,
                          e->column);
  }
  MapPtr m = std::get<MapPtr>(mv);
  Value key = arg(in, e, 1);
  try {
    key = in.convertToType(key, m->keyType);
  } catch (const std::exception &ex) {
    rethrowCallError(in, e, ex, "has: ");
  }
  return m->entries.find(key) != m->entries.end();
}

Value Builtins::bRemove(Interpreter &in, CallExpr *e) {
  arity(in, e, "remove", 2, 2);
  Value mv = arg(in, e, 0);
  if (!ValueHelper::isMap(mv)) {
    throw in.runtimeError("remove expects a map as first argument", e->line,
                          e->column);
  }
  MapPtr m = std::get<MapPtr>(mv);
  Value key = arg(in, e, 1);
  try {
    key = in.convertToType(key, m->keyType);
  } catch (const std::exception &ex) {
    rethrowCallError(in, e, ex, "remove: ");
  }
  return m->entries.erase(key) > 0;
}

Value Builtins::bKeys(Interpreter &in, CallExpr *e) {
  arity(in, e, "keys", 1, 1);
  Value mv = arg(in, e, 0);
  if (!ValueHelper::isMap(mv)) {
    throw in.runtimeError("keys expects a map", e->line, e->column);
  }
  MapPtr m = std::get<MapPtr>(mv);
  std::vector<Value> ks;
  ks.reserve(m->entries.size());
  for (const auto &kv : m->entries) {
    ks.push_back(kv.first);
  }
  return ValueHelper::createArray(m->keyType, ks);
}

Value Builtins::bValues(Interpreter &in, CallExpr *e) {
  arity(in, e, "values", 1, 1);
  Value mv = arg(in, e, 0);
  if (!ValueHelper::isMap(mv)) {
    throw in.runtimeError("values expects a map", e->line, e->column);
  }
  MapPtr m = std::get<MapPtr>(mv);
  std::vector<Value> vs;
  vs.reserve(m->entries.size());
  for (const auto &kv : m->entries) {
    vs.push_back(kv.second);
  }
  return ValueHelper::createArray(m->valueType, vs);
}

Value Builtins::bSize(Interpreter &in, CallExpr *e) {
  arity(in, e, "size", 1, 1);
  Value v = arg(in, e, 0);
  if (ValueHelper::isMap(v)) {
    return static_cast<int32_t>(ValueHelper::mapEntries(v).size());
  }
  if (ValueHelper::isArray(v)) {
    return static_cast<int32_t>(ValueHelper::arrayElements(v).size());
  }
  if (std::holds_alternative<std::string>(v)) {
    return static_cast<int32_t>(std::get<std::string>(v).size());
  }
  throw in.runtimeError("size expects an array, map, or string", e->line,
                        e->column);
}

Value Builtins::bIsMap(Interpreter &in, CallExpr *e) {
  arity(in, e, "isMap", 1, 1);
  return ValueHelper::isMap(arg(in, e, 0));
}

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

Value Builtins::bSubstr(Interpreter &in, CallExpr *e) {
  arity(in, e, "substr", 2, 3);
  std::string s = str(in, e, 0);
  int64_t start = integer(in, e, 1);
  if (start < 0 || static_cast<size_t>(start) > s.size()) {
    throw in.runtimeError("substr start out of bounds", e->line, e->column);
  }
  size_t len = std::string::npos;
  if (e->arguments.size() == 3) {
    int64_t n = integer(in, e, 2);
    if (n < 0) {
      throw in.runtimeError("substr length must be non-negative", e->line,
                            e->column);
    }
    len = static_cast<size_t>(n);
  }
  return s.substr(static_cast<size_t>(start), len);
}

Value Builtins::bCharAt(Interpreter &in, CallExpr *e) {
  arity(in, e, "charAt", 2, 2);
  std::string s = str(in, e, 0);
  int64_t idx = integer(in, e, 1);
  if (idx < 0 || static_cast<size_t>(idx) >= s.size()) {
    throw in.runtimeError("charAt index out of bounds", e->line, e->column);
  }
  return static_cast<char>(s[static_cast<size_t>(idx)]);
}

Value Builtins::bIndexOf(Interpreter &in, CallExpr *e) {
  arity(in, e, "indexOf", 2, 3);
  std::string s = str(in, e, 0);
  std::string sub = str(in, e, 1);
  size_t from = 0;
  if (e->arguments.size() == 3) {
    int64_t f = integer(in, e, 2);
    if (f < 0) {
      throw in.runtimeError("indexOf start must be non-negative", e->line,
                            e->column);
    }
    from = static_cast<size_t>(f);
  }
  size_t pos = s.find(sub, from);
  return static_cast<int32_t>(pos == std::string::npos
                                  ? -1
                                  : static_cast<int64_t>(pos));
}

Value Builtins::bContains(Interpreter &in, CallExpr *e) {
  arity(in, e, "contains", 2, 2);
  Value haystack = arg(in, e, 0);

  if (std::holds_alternative<std::string>(haystack)) {
    std::string needle = str(in, e, 1);
    return std::get<std::string>(haystack).find(needle) != std::string::npos;
  }

  if (ValueHelper::isArray(haystack)) {
    Value needle = arg(in, e, 1);
    for (const auto &el : ValueHelper::arrayElements(haystack)) {
      if (ValueHelper::equals(el, needle)) {
        return true;
      }
    }
    return false;
  }

  throw in.runtimeError("contains expects a string or array", e->line,
                        e->column);
}

Value Builtins::bStartsWith(Interpreter &in, CallExpr *e) {
  arity(in, e, "startsWith", 2, 2);
  std::string s = str(in, e, 0);
  std::string prefix = str(in, e, 1);
  return s.size() >= prefix.size() &&
         s.compare(0, prefix.size(), prefix) == 0;
}

Value Builtins::bEndsWith(Interpreter &in, CallExpr *e) {
  arity(in, e, "endsWith", 2, 2);
  std::string s = str(in, e, 0);
  std::string suffix = str(in, e, 1);
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

Value Builtins::bToUpper(Interpreter &in, CallExpr *e) {
  arity(in, e, "toUpper", 1, 1);
  std::string s = str(in, e, 0);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return s;
}

Value Builtins::bToLower(Interpreter &in, CallExpr *e) {
  arity(in, e, "toLower", 1, 1);
  std::string s = str(in, e, 0);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

Value Builtins::bTrim(Interpreter &in, CallExpr *e) {
  arity(in, e, "trim", 1, 1);
  std::string s = str(in, e, 0);
  size_t first = s.find_first_not_of(" \t\n\r");
  if (first == std::string::npos) {
    return std::string("");
  }
  size_t last = s.find_last_not_of(" \t\n\r");
  return s.substr(first, last - first + 1);
}

Value Builtins::bReplace(Interpreter &in, CallExpr *e) {
  arity(in, e, "replace", 3, 3);
  std::string s = str(in, e, 0);
  std::string from = str(in, e, 1);
  std::string to = str(in, e, 2);
  if (from.empty()) {
    throw in.runtimeError("replace: 'from' string must not be empty", e->line,
                          e->column);
  }
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
  return s;
}

Value Builtins::bSplit(Interpreter &in, CallExpr *e) {
  arity(in, e, "split", 1, 2);
  std::string s = str(in, e, 0);
  std::string delim =
      e->arguments.size() == 2 ? str(in, e, 1) : std::string(",");

  std::vector<Value> out;
  if (delim.empty()) {
    // Split into individual characters
    for (char c : s) {
      out.push_back(std::string(1, c));
    }
  } else {
    size_t pos = 0;
    while (true) {
      size_t found = s.find(delim, pos);
      if (found == std::string::npos) {
        out.push_back(s.substr(pos));
        break;
      }
      out.push_back(s.substr(pos, found - pos));
      pos = found + delim.size();
    }
  }
  return ValueHelper::createArray(TypeInfo(DataType::STRING), out);
}

Value Builtins::bJoin(Interpreter &in, CallExpr *e) {
  arity(in, e, "join", 1, 2);
  Value arrVal = arg(in, e, 0);
  if (!ValueHelper::isArray(arrVal)) {
    throw in.runtimeError("join expects an array", e->line, e->column);
  }
  std::string delim =
      e->arguments.size() == 2 ? ValueHelper::toString(arg(in, e, 1)) : "";
  std::string out;
  const auto &elems = ValueHelper::arrayElements(arrVal);
  for (size_t i = 0; i < elems.size(); ++i) {
    if (i > 0) {
      out += delim;
    }
    out += ValueHelper::toString(elems[i]);
  }
  return out;
}

Value Builtins::bRepeat(Interpreter &in, CallExpr *e) {
  arity(in, e, "repeat", 2, 2);
  std::string s = str(in, e, 0);
  int64_t n = integer(in, e, 1);
  if (n < 0) {
    throw in.runtimeError("repeat count must be non-negative", e->line,
                          e->column);
  }
  std::string out;
  out.reserve(s.size() * static_cast<size_t>(n));
  for (int64_t i = 0; i < n; ++i) {
    out += s;
  }
  return out;
}

Value Builtins::bReverse(Interpreter &in, CallExpr *e) {
  arity(in, e, "reverse", 1, 1);
  Value v = arg(in, e, 0);
  if (std::holds_alternative<std::string>(v)) {
    std::string s = std::get<std::string>(v);
    std::reverse(s.begin(), s.end());
    return s;
  }
  if (ValueHelper::isArray(v)) {
    auto &elems = ValueHelper::arrayElements(v);
    std::reverse(elems.begin(), elems.end());
    return v;
  }
  throw in.runtimeError("reverse expects a string or array", e->line,
                        e->column);
}

Value Builtins::bFormat(Interpreter &in, CallExpr *e) {
  arity(in, e, "format", 1, SIZE_MAX);
  std::string fmt = str(in, e, 0);
  std::string out;
  size_t nextArg = 1;
  for (size_t i = 0; i < fmt.size(); ++i) {
    char c = fmt[i];
    if (c == '{' && i + 1 < fmt.size() && fmt[i + 1] == '{') {
      out += '{';
      ++i;
      continue;
    }
    if (c == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
      out += '}';
      ++i;
      continue;
    }
    if (c == '{') {
      size_t close = fmt.find('}', i + 1);
      if (close == std::string::npos || close != i + 1) {
        throw in.runtimeError(
            "format: only empty '{}' placeholders are supported", e->line,
            e->column);
      }
      if (nextArg >= e->arguments.size()) {
        throw in.runtimeError("format: not enough arguments", e->line,
                              e->column);
      }
      out += ValueHelper::toString(arg(in, e, nextArg++));
      i = close;
      continue;
    }
    out += c;
  }
  if (nextArg < e->arguments.size()) {
    throw in.runtimeError("format: too many arguments", e->line, e->column);
  }
  return out;
}

// ---------------------------------------------------------------------------
// Math
// ---------------------------------------------------------------------------

Value Builtins::bAbs(Interpreter &in, CallExpr *e) {
  arity(in, e, "abs", 1, 1);
  Value v = arg(in, e, 0);
  TypeInfo t = ValueHelper::getType(v);
  if (t.isArray || t.baseType == DataType::STRING ||
      t.baseType == DataType::BOOL) {
    throw in.runtimeError("abs expects a numeric value", e->line, e->column);
  }
  if (t.baseType == DataType::DOUBLE || t.baseType == DataType::FLOAT) {
    return ValueHelper::createValue(t.baseType,
                                  std::fabs(ValueHelper::toDouble(v)));
  }
  if (t.baseType == DataType::UINT8 || t.baseType == DataType::UINT16 ||
      t.baseType == DataType::UINT32 || t.baseType == DataType::UINT64) {
    return v; // unsigned values are already non-negative
  }
  int64_t n = ValueHelper::toInt64(v);
  return ValueHelper::createValue(t.baseType, n < 0 ? -n : n);
}

Value Builtins::bMin(Interpreter &in, CallExpr *e) {
  arity(in, e, "min", 2, 2);
  Value a = arg(in, e, 0);
  Value b = arg(in, e, 1);
  return ValueHelper::lessThan(a, b) ? a : b;
}

Value Builtins::bMax(Interpreter &in, CallExpr *e) {
  arity(in, e, "max", 2, 2);
  Value a = arg(in, e, 0);
  Value b = arg(in, e, 1);
  return ValueHelper::greaterThan(a, b) ? a : b;
}

Value Builtins::bClamp(Interpreter &in, CallExpr *e) {
  arity(in, e, "clamp", 3, 3);
  Value x = arg(in, e, 0);
  Value lo = arg(in, e, 1);
  Value hi = arg(in, e, 2);
  if (ValueHelper::lessThan(x, lo)) {
    return lo;
  }
  if (ValueHelper::greaterThan(x, hi)) {
    return hi;
  }
  return x;
}

Value Builtins::bPow(Interpreter &in, CallExpr *e) {
  arity(in, e, "pow", 2, 2);
  return std::pow(real(in, e, 0), real(in, e, 1));
}

Value Builtins::bSqrt(Interpreter &in, CallExpr *e) {
  arity(in, e, "sqrt", 1, 1);
  double x = real(in, e, 0);
  if (x < 0.0) {
    throw in.runtimeError("sqrt of negative number", e->line, e->column);
  }
  return std::sqrt(x);
}

Value Builtins::bFloor(Interpreter &in, CallExpr *e) {
  arity(in, e, "floor", 1, 1);
  return std::floor(real(in, e, 0));
}

Value Builtins::bCeil(Interpreter &in, CallExpr *e) {
  arity(in, e, "ceil", 1, 1);
  return std::ceil(real(in, e, 0));
}

Value Builtins::bRound(Interpreter &in, CallExpr *e) {
  arity(in, e, "round", 1, 1);
  return std::round(real(in, e, 0));
}

Value Builtins::bTrunc(Interpreter &in, CallExpr *e) {
  arity(in, e, "trunc", 1, 1);
  return std::trunc(real(in, e, 0));
}

Value Builtins::bFmod(Interpreter &in, CallExpr *e) {
  arity(in, e, "fmod", 2, 2);
  double divisor = real(in, e, 1);
  if (divisor == 0.0) {
    throw in.runtimeError("fmod by zero", e->line, e->column);
  }
  return std::fmod(real(in, e, 0), divisor);
}

Value Builtins::bSin(Interpreter &in, CallExpr *e) {
  arity(in, e, "sin", 1, 1);
  return std::sin(real(in, e, 0));
}

Value Builtins::bCos(Interpreter &in, CallExpr *e) {
  arity(in, e, "cos", 1, 1);
  return std::cos(real(in, e, 0));
}

Value Builtins::bTan(Interpreter &in, CallExpr *e) {
  arity(in, e, "tan", 1, 1);
  return std::tan(real(in, e, 0));
}

Value Builtins::bAsin(Interpreter &in, CallExpr *e) {
  arity(in, e, "asin", 1, 1);
  return std::asin(real(in, e, 0));
}

Value Builtins::bAcos(Interpreter &in, CallExpr *e) {
  arity(in, e, "acos", 1, 1);
  return std::acos(real(in, e, 0));
}

Value Builtins::bAtan(Interpreter &in, CallExpr *e) {
  arity(in, e, "atan", 1, 1);
  return std::atan(real(in, e, 0));
}

Value Builtins::bAtan2(Interpreter &in, CallExpr *e) {
  arity(in, e, "atan2", 2, 2);
  return std::atan2(real(in, e, 0), real(in, e, 1));
}

Value Builtins::bExp(Interpreter &in, CallExpr *e) {
  arity(in, e, "exp", 1, 1);
  return std::exp(real(in, e, 0));
}

Value Builtins::bLog(Interpreter &in, CallExpr *e) {
  arity(in, e, "log", 1, 1);
  double x = real(in, e, 0);
  if (x <= 0.0) {
    throw in.runtimeError("log of non-positive number", e->line, e->column);
  }
  return std::log(x);
}

Value Builtins::bLog10(Interpreter &in, CallExpr *e) {
  arity(in, e, "log10", 1, 1);
  double x = real(in, e, 0);
  if (x <= 0.0) {
    throw in.runtimeError("log10 of non-positive number", e->line, e->column);
  }
  return std::log10(x);
}

Value Builtins::bRandom(Interpreter &in, CallExpr *e) {
  arity(in, e, "random", 0, 0);
  return std::uniform_real_distribution<double>(0.0, 1.0)(in._rng);
}

Value Builtins::bRandInt(Interpreter &in, CallExpr *e) {
  arity(in, e, "randInt", 2, 2);
  int64_t lo = integer(in, e, 0);
  int64_t hi = integer(in, e, 1);
  if (lo > hi) {
    std::swap(lo, hi);
  }
  std::uniform_int_distribution<int64_t> dist(lo, hi);
  return static_cast<int64_t>(dist(in._rng));
}

Value Builtins::bSrand(Interpreter &in, CallExpr *e) {
  arity(in, e, "srand", 1, 1);
  in._rng.seed(static_cast<uint64_t>(integer(in, e, 0)));
  return static_cast<int32_t>(0);
}

Value Builtins::bPi(Interpreter &in, CallExpr *e) {
  arity(in, e, "pi", 0, 0);
  return 3.14159265358979323846;
}

// ---------------------------------------------------------------------------
// Conversions / introspection
// ---------------------------------------------------------------------------

Value Builtins::bToInt(Interpreter &in, CallExpr *e) {
  arity(in, e, "toInt", 1, 1);
  try {
    return ValueHelper::toInt64(arg(in, e, 0));
  } catch (const std::exception &ex) {
    rethrowCallError(in, e, ex);
  }
}

Value Builtins::bToUInt(Interpreter &in, CallExpr *e) {
  arity(in, e, "toUInt", 1, 1);
  try {
    return ValueHelper::toUInt64(arg(in, e, 0));
  } catch (const std::exception &ex) {
    rethrowCallError(in, e, ex);
  }
}

Value Builtins::bToDouble(Interpreter &in, CallExpr *e) {
  arity(in, e, "toDouble", 1, 1);
  try {
    return ValueHelper::toDouble(arg(in, e, 0));
  } catch (const std::exception &ex) {
    rethrowCallError(in, e, ex);
  }
}

Value Builtins::bToFloat(Interpreter &in, CallExpr *e) {
  arity(in, e, "toFloat", 1, 1);
  try {
    return static_cast<float>(ValueHelper::toDouble(arg(in, e, 0)));
  } catch (const std::exception &ex) {
    rethrowCallError(in, e, ex);
  }
}

Value Builtins::bToString(Interpreter &in, CallExpr *e) {
  arity(in, e, "toString", 1, 1);
  return ValueHelper::toString(arg(in, e, 0));
}

Value Builtins::bToBool(Interpreter &in, CallExpr *e) {
  arity(in, e, "toBool", 1, 1);
  return ValueHelper::toBool(arg(in, e, 0));
}

Value Builtins::bToChar(Interpreter &in, CallExpr *e) {
  arity(in, e, "toChar", 1, 1);
  Value v = arg(in, e, 0);
  if (std::holds_alternative<std::string>(v)) {
    const std::string &s = std::get<std::string>(v);
    if (s.size() != 1) {
      throw in.runtimeError("toChar expects a single-character string",
                            e->line, e->column);
    }
    return static_cast<char>(s[0]);
  }
  try {
    return static_cast<char>(ValueHelper::toInt64(v));
  } catch (const std::exception &ex) {
    rethrowCallError(in, e, ex);
  }
}

Value Builtins::bParseInt(Interpreter &in, CallExpr *e) {
  arity(in, e, "parseInt", 1, 1);
  std::string s = str(in, e, 0);
  try {
    size_t pos = 0;
    int64_t v = std::stoll(s, &pos);
    while (pos < s.size() &&
           std::isspace(static_cast<unsigned char>(s[pos]))) {
      ++pos;
    }
    if (pos != s.size()) {
      throw std::invalid_argument("trailing characters");
    }
    return v;
  } catch (const std::exception &) {
    throw in.runtimeError("parseInt: cannot parse '" + s + "'", e->line,
                          e->column);
  }
}

Value Builtins::bParseDouble(Interpreter &in, CallExpr *e) {
  arity(in, e, "parseDouble", 1, 1);
  std::string s = str(in, e, 0);
  try {
    size_t pos = 0;
    double v = std::stod(s, &pos);
    while (pos < s.size() &&
           std::isspace(static_cast<unsigned char>(s[pos]))) {
      ++pos;
    }
    if (pos != s.size()) {
      throw std::invalid_argument("trailing characters");
    }
    return v;
  } catch (const std::exception &) {
    throw in.runtimeError("parseDouble: cannot parse '" + s + "'", e->line,
                          e->column);
  }
}

Value Builtins::bTypeof(Interpreter &in, CallExpr *e) {
  arity(in, e, "typeof", 1, 1);
  return ValueHelper::typeToString(ValueHelper::getType(arg(in, e, 0)));
}

Value Builtins::bIsArray(Interpreter &in, CallExpr *e) {
  arity(in, e, "isArray", 1, 1);
  return ValueHelper::isArray(arg(in, e, 0));
}

Value Builtins::bIsNull(Interpreter &in, CallExpr *e) {
  arity(in, e, "isNull", 1, 1);
  return ValueHelper::isNull(arg(in, e, 0));
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

namespace {

Value jsonToValue(const Json &j) {
  switch (j.kind()) {
  case Json::Kind::Null:
    return NullValue{};
  case Json::Kind::Bool:
    return j.asBool();
  case Json::Kind::Int:
    return j.asInt();
  case Json::Kind::Double:
    return j.asDouble();
  case Json::Kind::String:
    return j.asString();
  case Json::Kind::Array: {
    std::vector<Value> elems;
    elems.reserve(j.size());
    for (const Json &el : j.asArray()) {
      elems.push_back(jsonToValue(el));
    }
    // Same convention as array literals: element type from first element.
    TypeInfo elemType(DataType::VOID);
    for (const auto &el : elems) {
      TypeInfo t = ValueHelper::getType(el);
      if (t.baseType != DataType::NIL) {
        elemType = t;
        break;
      }
    }
    return ValueHelper::createArray(elemType, elems);
  }
  case Json::Kind::Object: {
    auto m = ValueHelper::createMap(TypeInfo(DataType::STRING),
                                    TypeInfo(DataType::VOID));
    TypeInfo valType(DataType::VOID);
    for (const auto &[k, v] : j.asObject()) {
      Value cv = jsonToValue(v);
      if (valType.baseType == DataType::VOID && !ValueHelper::isNull(cv)) {
        valType = ValueHelper::getType(cv);
      }
      m->entries[k] = cv;
    }
    m->valueType = valType;
    return m;
  }
  }
  return NullValue{};
}

Json valueToJson(const Value &v) {
  if (ValueHelper::isNull(v)) {
    return Json(nullptr);
  }
  if (auto *b = std::get_if<bool>(&v)) {
    return Json(*b);
  }
  if (auto *c = std::get_if<char>(&v)) {
    return Json(std::string(1, *c));
  }
  if (auto *s = std::get_if<std::string>(&v)) {
    return Json(*s);
  }
  if (auto *u = std::get_if<uint64_t>(&v)) {
    if (*u > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      return Json(static_cast<double>(*u));
    }
    return Json(static_cast<int64_t>(*u));
  }
  if (std::holds_alternative<uint8_t>(v) ||
      std::holds_alternative<uint16_t>(v) ||
      std::holds_alternative<uint32_t>(v)) {
    return Json(static_cast<int64_t>(ValueHelper::toUInt64(v)));
  }
  if (auto *d = std::get_if<double>(&v)) {
    return Json(*d);
  }
  if (auto *f = std::get_if<float>(&v)) {
    return Json(static_cast<double>(*f));
  }
  if (std::holds_alternative<int8_t>(v) || std::holds_alternative<int16_t>(v) ||
      std::holds_alternative<int32_t>(v) || std::holds_alternative<int64_t>(v)) {
    return Json(ValueHelper::toInt64(v));
  }
  if (auto *arr = std::get_if<ArrayPtr>(&v)) {
    Json::Array out;
    if (*arr) {
      for (const Value &el : (*arr)->elements) {
        out.push_back(valueToJson(el));
      }
    }
    return Json(std::move(out));
  }
  if (auto *mp = std::get_if<MapPtr>(&v)) {
    Json::Object out;
    if (*mp) {
      for (const auto &[k, val] : (*mp)->entries) {
        out[ValueHelper::toString(k)] = valueToJson(val);
      }
    }
    return Json(std::move(out));
  }
  if (auto *sv = std::get_if<StructPtr>(&v)) {
    Json::Object out;
    if (*sv) {
      for (const auto &name : (*sv)->fieldOrder) {
        auto it = (*sv)->fields.find(name);
        if (it != (*sv)->fields.end()) {
          out[name] = valueToJson(it->second);
        }
      }
    }
    return Json(std::move(out));
  }
  throw std::runtime_error("Cannot serialize function values to JSON");
}

} // namespace

Value Builtins::bJsonParse(Interpreter &in, CallExpr *e) {
  arity(in, e, "jsonParse", 1, 1);
  std::string text = str(in, e, 0);
  try {
    return jsonToValue(Json::parse(text));
  } catch (const JsonError &err) {
    throw in.runtimeError(std::string("jsonParse: ") + err.what(), e->line,
                          e->column);
  }
}

Value Builtins::bJsonStringify(Interpreter &in, CallExpr *e) {
  arity(in, e, "jsonStringify", 1, 2);
  Value v = arg(in, e, 0);
  int64_t indent = e->arguments.size() == 2 ? integer(in, e, 1) : -1;
  try {
    return valueToJson(v).dump(static_cast<int>(indent));
  } catch (const std::runtime_error &err) {
    throw in.runtimeError(std::string("jsonStringify: ") + err.what(), e->line,
                          e->column);
  }
}

// ---------------------------------------------------------------------------
// Output / control
// ---------------------------------------------------------------------------

Value Builtins::bPrint(Interpreter &in, CallExpr *e) {
  for (size_t i = 0; i < e->arguments.size(); ++i) {
    in._outputCallback(ValueHelper::toString(arg(in, e, i)));
  }
  return static_cast<int32_t>(0);
}

Value Builtins::bPrintln(Interpreter &in, CallExpr *e) {
  bPrint(in, e);
  in._outputCallback("\n");
  return static_cast<int32_t>(0);
}

Value Builtins::bError(Interpreter &in, CallExpr *e) {
  arity(in, e, "error", 1, 1);
  throw in.runtimeError(ValueHelper::toString(arg(in, e, 0)), e->line,
                        e->column);
}

Value Builtins::bAssert(Interpreter &in, CallExpr *e) {
  arity(in, e, "assert", 1, 2);
  if (!boolean(in, e, 0)) {
    std::string msg = e->arguments.size() == 2
                          ? ValueHelper::toString(arg(in, e, 1))
                          : "Assertion failed";
    throw in.runtimeError(msg, e->line, e->column);
  }
  return true;
}

} // namespace Script
