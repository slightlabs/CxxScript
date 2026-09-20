#include "DataTypes.h"
#include "AST.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Script {

namespace {
bool isFloatingType(DataType t) {
  return t == DataType::FLOAT || t == DataType::DOUBLE;
}

// When either operand is DOUBLE the result widens to DOUBLE; otherwise (both
// FLOAT, or FLOAT mixed with an integer) the result stays FLOAT.
DataType floatingResultType(DataType a, DataType b) {
  return (a == DataType::DOUBLE || b == DataType::DOUBLE) ? DataType::DOUBLE
                                                          : DataType::FLOAT;
}
} // namespace

TypeInfo ValueHelper::getType(const Value &val) {
  if (std::holds_alternative<ArrayPtr>(val)) {
    ArrayPtr arr = std::get<ArrayPtr>(val);
    if (!arr) {
      return TypeInfo(DataType::VOID, true);
    }
    return TypeInfo::arrayOf(arr->elementType);
  }
  if (std::holds_alternative<FuncPtr>(val)) {
    FuncPtr fn = std::get<FuncPtr>(val);
    if (!fn) {
      return TypeInfo::opaqueFunction();
    }
    if (fn->procs.size() > 1) {
      return TypeInfo::opaqueFunction();
    }
    return fn->signature();
  }
  if (std::holds_alternative<StructPtr>(val)) {
    StructPtr sv = std::get<StructPtr>(val);
    if (!sv) {
      return TypeInfo(DataType::VOID);
    }
    return TypeInfo::structOf(sv->typeName);
  }
  if (std::holds_alternative<MapPtr>(val)) {
    MapPtr m = std::get<MapPtr>(val);
    if (!m) {
      return TypeInfo(DataType::VOID, false, true, DataType::VOID);
    }
    return TypeInfo::mapOf(m->keyType, m->valueType);
  }
  if (std::holds_alternative<char>(val))
    return TypeInfo(DataType::CHAR);
  if (std::holds_alternative<int8_t>(val))
    return TypeInfo(DataType::INT8);
  if (std::holds_alternative<uint8_t>(val))
    return TypeInfo(DataType::UINT8);
  if (std::holds_alternative<int16_t>(val))
    return TypeInfo(DataType::INT16);
  if (std::holds_alternative<uint16_t>(val))
    return TypeInfo(DataType::UINT16);
  if (std::holds_alternative<int32_t>(val))
    return TypeInfo(DataType::INT32);
  if (std::holds_alternative<uint32_t>(val))
    return TypeInfo(DataType::UINT32);
  if (std::holds_alternative<int64_t>(val))
    return TypeInfo(DataType::INT64);
  if (std::holds_alternative<uint64_t>(val))
    return TypeInfo(DataType::UINT64);
  if (std::holds_alternative<float>(val))
    return TypeInfo(DataType::FLOAT);
  if (std::holds_alternative<double>(val))
    return TypeInfo(DataType::DOUBLE);
  if (std::holds_alternative<std::string>(val))
    return TypeInfo(DataType::STRING);
  if (std::holds_alternative<bool>(val))
    return TypeInfo(DataType::BOOL);
  return TypeInfo(DataType::VOID);
}

std::string ValueHelper::typeToString(const TypeInfo &type) {
  if (type.isFunction) {
    std::string sig = "fn(";
    for (size_t i = 0; i < type.paramTypes.size(); ++i) {
      if (i > 0)
        sig += ", ";
      sig += typeToString(type.paramTypes[i]);
    }
    sig += ")";
    if (type.retType) {
      sig += "->" + typeToString(*type.retType);
    }
    return sig;
  }
  if (type.isArray && type.arrayElem) {
    return typeToString(*type.arrayElem) + "[]";
  }
  if (type.isStruct) {
    return type.structName + (type.isArray ? "[]" : "");
  }
  std::string base;
  switch (type.baseType) {
  case DataType::INT8:
    base = "int8";
    break;
  case DataType::UINT8:
    base = "uint8";
    break;
  case DataType::INT16:
    base = "int16";
    break;
  case DataType::UINT16:
    base = "uint16";
    break;
  case DataType::INT32:
    base = "int32";
    break;
  case DataType::UINT32:
    base = "uint32";
    break;
  case DataType::INT64:
    base = "int64";
    break;
  case DataType::UINT64:
    base = "uint64";
    break;
  case DataType::FLOAT:
    base = "float";
    break;
  case DataType::DOUBLE:
    base = "double";
    break;
  case DataType::STRING:
    base = "string";
    break;
  case DataType::BOOL:
    base = "bool";
    break;
  case DataType::CHAR:
    base = "char";
    break;
  case DataType::VOID:
    base = "void";
    break;
  }
  if (type.isMap) {
    std::string valStr =
        type.mapValueType ? typeToString(*type.mapValueType) : base;
    return "map<" + typeToString(TypeInfo(type.keyType)) + ", " + valStr + ">";
  }
  if (type.isArray) {
    base += "[]";
  }
  return base;
}

TypeInfo ValueHelper::stringToType(const std::string &str) {
  if (str == "int8")
    return TypeInfo(DataType::INT8);
  if (str == "uint8")
    return TypeInfo(DataType::UINT8);
  if (str == "int16")
    return TypeInfo(DataType::INT16);
  if (str == "uint16")
    return TypeInfo(DataType::UINT16);
  if (str == "int32")
    return TypeInfo(DataType::INT32);
  if (str == "uint32")
    return TypeInfo(DataType::UINT32);
  if (str == "int64")
    return TypeInfo(DataType::INT64);
  if (str == "uint64")
    return TypeInfo(DataType::UINT64);
  if (str == "float")
    return TypeInfo(DataType::FLOAT);
  if (str == "double")
    return TypeInfo(DataType::DOUBLE);
  if (str == "string")
    return TypeInfo(DataType::STRING);
  if (str == "bool")
    return TypeInfo(DataType::BOOL);
  if (str == "char")
    return TypeInfo(DataType::CHAR);
  if (str == "void")
    return TypeInfo(DataType::VOID);
  throw std::runtime_error("Unknown type: " + str);
}

int64_t ValueHelper::toInt64(const Value &val) {
  if (std::holds_alternative<ArrayPtr>(val)) {
    throw std::runtime_error("Cannot convert array to int64");
  }
  return std::visit(
      [](auto &&arg) -> int64_t {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) {
          throw std::runtime_error("Cannot convert string to int64");
        } else if constexpr (std::is_same_v<T, bool>) {
          return arg ? 1 : 0;
        } else if constexpr (std::is_same_v<T, double>) {
          return static_cast<int64_t>(arg);
        } else if constexpr (std::is_same_v<T, ArrayPtr>) {
          throw std::runtime_error("Cannot convert array to int64");
        } else if constexpr (std::is_same_v<T, MapPtr>) {
          throw std::runtime_error("Cannot convert map to int64");
        } else if constexpr (std::is_same_v<T, StructPtr>) {
          throw std::runtime_error("Cannot convert struct to int64");
        } else if constexpr (std::is_same_v<T, FuncPtr>) {
          throw std::runtime_error("Cannot convert function to int64");
        } else {
          return static_cast<int64_t>(arg);
        }
      },
      val);
}

uint64_t ValueHelper::toUInt64(const Value &val) {
  if (std::holds_alternative<ArrayPtr>(val)) {
    throw std::runtime_error("Cannot convert array to uint64");
  }
  return std::visit(
      [](auto &&arg) -> uint64_t {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) {
          throw std::runtime_error("Cannot convert string to uint64");
        } else if constexpr (std::is_same_v<T, bool>) {
          return arg ? 1 : 0;
        } else if constexpr (std::is_same_v<T, double>) {
          return static_cast<uint64_t>(arg);
        } else if constexpr (std::is_same_v<T, ArrayPtr>) {
          throw std::runtime_error("Cannot convert array to uint64");
        } else if constexpr (std::is_same_v<T, MapPtr>) {
          throw std::runtime_error("Cannot convert map to uint64");
        } else if constexpr (std::is_same_v<T, StructPtr>) {
          throw std::runtime_error("Cannot convert struct to uint64");
        } else if constexpr (std::is_same_v<T, FuncPtr>) {
          throw std::runtime_error("Cannot convert function to uint64");
        } else {
          return static_cast<uint64_t>(arg);
        }
      },
      val);
}

double ValueHelper::toDouble(const Value &val) {
  if (std::holds_alternative<ArrayPtr>(val)) {
    throw std::runtime_error("Cannot convert array to double");
  }
  return std::visit(
      [](auto &&arg) -> double {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) {
          throw std::runtime_error("Cannot convert string to double");
        } else if constexpr (std::is_same_v<T, bool>) {
          return arg ? 1.0 : 0.0;
        } else if constexpr (std::is_same_v<T, ArrayPtr>) {
          throw std::runtime_error("Cannot convert array to double");
        } else if constexpr (std::is_same_v<T, MapPtr>) {
          throw std::runtime_error("Cannot convert map to double");
        } else if constexpr (std::is_same_v<T, StructPtr>) {
          throw std::runtime_error("Cannot convert struct to double");
        } else if constexpr (std::is_same_v<T, FuncPtr>) {
          throw std::runtime_error("Cannot convert function to double");
        } else {
          return static_cast<double>(arg);
        }
      },
      val);
}

bool ValueHelper::toBool(const Value &val) {
  if (std::holds_alternative<ArrayPtr>(val)) {
    return true; // non-null arrays are truthy
  }
  return std::visit(
      [](auto &&arg) -> bool {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) {
          return !arg.empty();
        } else if constexpr (std::is_same_v<T, bool>) {
          return arg;
        } else if constexpr (std::is_same_v<T, double>) {
          return arg != 0.0;
        } else if constexpr (std::is_same_v<T, StructPtr> ||
                             std::is_same_v<T, FuncPtr>) {
          return arg != nullptr;
        } else if constexpr (std::is_same_v<T, MapPtr>) {
          return arg != nullptr;
        } else {
          return arg != 0;
        }
      },
      val);
}

namespace {
// Recursive core of ValueHelper::toString; depth-bounded so a cyclic
// structure (only constructible via host-injected values) can't overflow
// the native stack.
std::string toStringDepth(const Value &val, int depth) {
  if (depth > 64) {
    return "..."; // truncated: probable cycle
  }
  return std::visit(
      [depth](auto &&arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) {
          return arg;
        } else if constexpr (std::is_same_v<T, bool>) {
          return arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, char>) {
          return std::string(1, arg);
        } else if constexpr (std::is_same_v<T, double> ||
                             std::is_same_v<T, float>) {
          return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, ArrayPtr>) {
          std::string out = "[";
          const auto &elems = arg ? arg->elements
                                  : std::vector<Value>{};
          for (size_t i = 0; i < elems.size(); ++i) {
            if (i > 0)
              out += ", ";
            out += toStringDepth(elems[i], depth + 1);
          }
          out += "]";
          return out;
        } else if constexpr (std::is_same_v<T, MapPtr>) {
          std::string out = "{";
          bool first = true;
          if (arg) {
            for (const auto &kv : arg->entries) {
              if (!first)
                out += ", ";
              first = false;
              out += toStringDepth(kv.first, depth + 1) + ": " +
                     toStringDepth(kv.second, depth + 1);
            }
          }
          out += "}";
          return out;
        } else if constexpr (std::is_same_v<T, StructPtr>) {
          if (!arg) {
            return "<null struct>";
          }
          std::string out = arg->typeName + "{";
          bool first = true;
          for (const auto &name : arg->fieldOrder) {
            auto it = arg->fields.find(name);
            if (it == arg->fields.end()) {
              continue;
            }
            if (!first)
              out += ", ";
            first = false;
            out += name + ": " + toStringDepth(it->second, depth + 1);
          }
          out += "}";
          return out;
        } else if constexpr (std::is_same_v<T, FuncPtr>) {
          if (arg && !arg->displayName.empty()) {
            return "<fn " + arg->displayName + ">";
          }
          return std::string("<fn>");
        } else {
          return std::to_string(arg);
        }
      },
      val);
}
} // namespace

std::string ValueHelper::toString(const Value &val) {
  return toStringDepth(val, 0);
}

Value ValueHelper::add(const Value &a, const Value &b) {
  TypeInfo aType = getType(a);
  TypeInfo bType = getType(b);
  if (aType.isArray || bType.isArray) {
    throw std::runtime_error("Operator + does not support arrays");
  }

  if (std::holds_alternative<std::string>(a) ||
      std::holds_alternative<std::string>(b)) {
    return toString(a) + toString(b);
  }

  if (isFloatingType(aType.baseType) || isFloatingType(bType.baseType)) {
    DataType resultType = floatingResultType(aType.baseType, bType.baseType);
    double result = toDouble(a) + toDouble(b);
    return createValue(resultType, result);
  }

  // Unsigned operations
  if (aType.baseType == DataType::UINT8 || aType.baseType == DataType::UINT16 ||
      aType.baseType == DataType::UINT32 || aType.baseType == DataType::UINT64 ||
      bType.baseType == DataType::UINT8 || bType.baseType == DataType::UINT16 ||
      bType.baseType == DataType::UINT32 || bType.baseType == DataType::UINT64) {
    uint64_t result = toUInt64(a) + toUInt64(b);
    DataType resultType =
        (static_cast<int>(aType.baseType) > static_cast<int>(bType.baseType)) ? aType.baseType : bType.baseType;
    return createValue(resultType, result);
  }

  // Signed operations
  int64_t result = toInt64(a) + toInt64(b);
  DataType resultType =
      (static_cast<int>(aType.baseType) > static_cast<int>(bType.baseType)) ? aType.baseType : bType.baseType;
  return createValue(resultType, result);
}

Value ValueHelper::subtract(const Value &a, const Value &b) {
  TypeInfo aType = getType(a);
  TypeInfo bType = getType(b);
  if (aType.isArray || bType.isArray) {
    throw std::runtime_error("Operator - does not support arrays");
  }

  if (isFloatingType(aType.baseType) || isFloatingType(bType.baseType)) {
    DataType resultType = floatingResultType(aType.baseType, bType.baseType);
    double result = toDouble(a) - toDouble(b);
    return createValue(resultType, result);
  }

  if (aType.baseType == DataType::UINT8 || aType.baseType == DataType::UINT16 ||
      aType.baseType == DataType::UINT32 || aType.baseType == DataType::UINT64 ||
      bType.baseType == DataType::UINT8 || bType.baseType == DataType::UINT16 ||
      bType.baseType == DataType::UINT32 || bType.baseType == DataType::UINT64) {
    uint64_t result = toUInt64(a) - toUInt64(b);
    DataType resultType =
        (static_cast<int>(aType.baseType) > static_cast<int>(bType.baseType)) ? aType.baseType : bType.baseType;
    return createValue(resultType, result);
  }

  int64_t result = toInt64(a) - toInt64(b);
  DataType resultType =
      (static_cast<int>(aType.baseType) > static_cast<int>(bType.baseType)) ? aType.baseType : bType.baseType;
  return createValue(resultType, result);
}

Value ValueHelper::multiply(const Value &a, const Value &b) {
  TypeInfo aType = getType(a);
  TypeInfo bType = getType(b);
  if (aType.isArray || bType.isArray) {
    throw std::runtime_error("Operator * does not support arrays");
  }

  if (isFloatingType(aType.baseType) || isFloatingType(bType.baseType)) {
    DataType resultType = floatingResultType(aType.baseType, bType.baseType);
    double result = toDouble(a) * toDouble(b);
    return createValue(resultType, result);
  }

  if (aType.baseType == DataType::UINT8 || aType.baseType == DataType::UINT16 ||
      aType.baseType == DataType::UINT32 || aType.baseType == DataType::UINT64 ||
      bType.baseType == DataType::UINT8 || bType.baseType == DataType::UINT16 ||
      bType.baseType == DataType::UINT32 || bType.baseType == DataType::UINT64) {
    uint64_t result = toUInt64(a) * toUInt64(b);
    DataType resultType =
        (static_cast<int>(aType.baseType) > static_cast<int>(bType.baseType)) ? aType.baseType : bType.baseType;
    return createValue(resultType, result);
  }

  int64_t result = toInt64(a) * toInt64(b);
  DataType resultType =
      (static_cast<int>(aType.baseType) > static_cast<int>(bType.baseType)) ? aType.baseType : bType.baseType;
  return createValue(resultType, result);
}

Value ValueHelper::divide(const Value &a, const Value &b) {
  TypeInfo ta = getType(a);
  TypeInfo tb = getType(b);
  if (ta.isArray || tb.isArray) {
    throw std::runtime_error("Operator / does not support arrays");
  }

  if (isFloatingType(ta.baseType) || isFloatingType(tb.baseType)) {
    DataType resultType = floatingResultType(ta.baseType, tb.baseType);
    double divisor = toDouble(b);
    if (divisor == 0.0) {
      throw std::runtime_error("Division by zero");
    }
    double result = toDouble(a) / divisor;
    return createValue(resultType, result);
  }

  if (ta.baseType == DataType::UINT8 || ta.baseType == DataType::UINT16 ||
      ta.baseType == DataType::UINT32 || ta.baseType == DataType::UINT64 ||
      tb.baseType == DataType::UINT8 || tb.baseType == DataType::UINT16 ||
      tb.baseType == DataType::UINT32 || tb.baseType == DataType::UINT64) {
    uint64_t divisor = toUInt64(b);
    if (divisor == 0) {
      throw std::runtime_error("Division by zero");
    }
    uint64_t result = toUInt64(a) / divisor;
    DataType resultType =
        (static_cast<int>(ta.baseType) > static_cast<int>(tb.baseType)) ? ta.baseType : tb.baseType;
    return createValue(resultType, result);
  }

  int64_t divisor = toInt64(b);
  if (divisor == 0) {
    throw std::runtime_error("Division by zero");
  }
  int64_t result = toInt64(a) / divisor;
  DataType resultType =
      (static_cast<int>(ta.baseType) > static_cast<int>(tb.baseType)) ? ta.baseType : tb.baseType;
  return createValue(resultType, result);
}

Value ValueHelper::modulo(const Value &a, const Value &b) {
  TypeInfo ta = getType(a);
  TypeInfo tb = getType(b);
  if (ta.isArray || tb.isArray) {
    throw std::runtime_error("Operator % does not support arrays");
  }

  if (isFloatingType(ta.baseType) || isFloatingType(tb.baseType)) {
    double divisor = toDouble(b);
    if (divisor == 0.0) {
      throw std::runtime_error("Modulo by zero");
    }
    DataType resultType = floatingResultType(ta.baseType, tb.baseType);
    return createValue(resultType, std::fmod(toDouble(a), divisor));
  }

  if (ta.baseType == DataType::UINT8 || ta.baseType == DataType::UINT16 ||
      ta.baseType == DataType::UINT32 || ta.baseType == DataType::UINT64 ||
      tb.baseType == DataType::UINT8 || tb.baseType == DataType::UINT16 ||
      tb.baseType == DataType::UINT32 || tb.baseType == DataType::UINT64) {
    uint64_t divisor = toUInt64(b);
    if (divisor == 0) {
      throw std::runtime_error("Modulo by zero");
    }
    uint64_t result = toUInt64(a) % divisor;
    DataType resultType =
        (static_cast<int>(ta.baseType) > static_cast<int>(tb.baseType)) ? ta.baseType : tb.baseType;
    return createValue(resultType, result);
  }

  int64_t divisor = toInt64(b);
  if (divisor == 0) {
    throw std::runtime_error("Modulo by zero");
  }
  int64_t result = toInt64(a) % divisor;
  DataType resultType =
      (static_cast<int>(ta.baseType) > static_cast<int>(tb.baseType)) ? ta.baseType : tb.baseType;
  return createValue(resultType, result);
}

namespace {

// Three-way comparison: -1, 0, +1. Arrays compare lexicographically;
// maps/structs/functions reject ordering. `depth` bounds recursion on
// host-constructed cyclic structures.
int compareValues(const Value &a, const Value &b, int depth) {
  if (depth > 64) {
    throw std::runtime_error(
        "Comparison depth limit exceeded (cyclic structure?)");
  }
  bool aArr = ValueHelper::isArray(a);
  bool bArr = ValueHelper::isArray(b);
  if (aArr && bArr) {
    const auto &ea = ValueHelper::arrayElements(a);
    const auto &eb = ValueHelper::arrayElements(b);
    size_t n = std::min(ea.size(), eb.size());
    for (size_t i = 0; i < n; ++i) {
      int c = compareValues(ea[i], eb[i], depth + 1);
      if (c != 0) {
        return c;
      }
    }
    if (ea.size() != eb.size()) {
      return ea.size() < eb.size() ? -1 : 1;
    }
    return 0;
  }
  if (aArr != bArr) {
    throw std::runtime_error("Cannot order array against non-array");
  }
  if (ValueHelper::isMap(a) || ValueHelper::isMap(b) ||
      ValueHelper::isStruct(a) || ValueHelper::isStruct(b) ||
      std::holds_alternative<FuncPtr>(a) ||
      std::holds_alternative<FuncPtr>(b)) {
    throw std::runtime_error(
        "Ordering comparison not supported for this type");
  }
  if (std::holds_alternative<std::string>(a) &&
      std::holds_alternative<std::string>(b)) {
    const auto &sa = std::get<std::string>(a);
    const auto &sb = std::get<std::string>(b);
    return sa < sb ? -1 : (sa > sb ? 1 : 0);
  }
  TypeInfo ta = ValueHelper::getType(a);
  TypeInfo tb = ValueHelper::getType(b);
  if (isFloatingType(ta.baseType) || isFloatingType(tb.baseType)) {
    double da = ValueHelper::toDouble(a);
    double db = ValueHelper::toDouble(b);
    return da < db ? -1 : (da > db ? 1 : 0);
  }
  // Mixed signed/unsigned compares via double to stay correct at extremes.
  bool aUns = ta.baseType == DataType::UINT8 || ta.baseType == DataType::UINT16 ||
              ta.baseType == DataType::UINT32 || ta.baseType == DataType::UINT64;
  bool bUns = tb.baseType == DataType::UINT8 || tb.baseType == DataType::UINT16 ||
              tb.baseType == DataType::UINT32 || tb.baseType == DataType::UINT64;
  if (aUns && bUns) {
    uint64_t ua = ValueHelper::toUInt64(a);
    uint64_t ub = ValueHelper::toUInt64(b);
    return ua < ub ? -1 : (ua > ub ? 1 : 0);
  }
  if (aUns != bUns) {
    double da = static_cast<double>(aUns ? ValueHelper::toUInt64(a)
                                         : ValueHelper::toInt64(a));
    double db = static_cast<double>(bUns ? ValueHelper::toUInt64(b)
                                         : ValueHelper::toInt64(b));
    return da < db ? -1 : (da > db ? 1 : 0);
  }
  int64_t ia = ValueHelper::toInt64(a);
  int64_t ib = ValueHelper::toInt64(b);
  return ia < ib ? -1 : (ia > ib ? 1 : 0);
}

bool equalsDepth(const Value &a, const Value &b, int depth);

bool arraysEqual(const ArrayPtr &lhs, const ArrayPtr &rhs, int depth) {
  if (!lhs || !rhs) {
    return lhs == rhs;
  }
  if (lhs->elementType != rhs->elementType) {
    return false;
  }
  if (lhs->elements.size() != rhs->elements.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs->elements.size(); ++i) {
    if (!equalsDepth(lhs->elements[i], rhs->elements[i], depth + 1)) {
      return false;
    }
  }
  return true;
}

bool mapsEqual(const MapPtr &lhs, const MapPtr &rhs, int depth) {
  if (!lhs || !rhs) {
    return lhs == rhs;
  }
  if (lhs->entries.size() != rhs->entries.size()) {
    return false;
  }
  for (const auto &kv : lhs->entries) {
    auto it = rhs->entries.find(kv.first);
    if (it == rhs->entries.end() ||
        !equalsDepth(kv.second, it->second, depth + 1)) {
      return false;
    }
  }
  return true;
}

} // namespace

bool ValueHelper::greaterThan(const Value &a, const Value &b) {
  return compareValues(a, b, 0) > 0;
}

bool ValueHelper::lessThan(const Value &a, const Value &b) {
  return compareValues(a, b, 0) < 0;
}

bool ValueHelper::greaterOrEqual(const Value &a, const Value &b) {
  return compareValues(a, b, 0) >= 0;
}

bool ValueHelper::lessOrEqual(const Value &a, const Value &b) {
  return compareValues(a, b, 0) <= 0;
}

namespace {
bool equalsDepth(const Value &a, const Value &b, int depth) {
  if (depth > 64) {
    throw std::runtime_error(
        "Equality depth limit exceeded (cyclic structure?)");
  }
  TypeInfo ta = ValueHelper::getType(a);
  TypeInfo tb = ValueHelper::getType(b);
  if (ta.isStruct || tb.isStruct) {
    if (!ta.isStruct || !tb.isStruct || ta.structName != tb.structName) {
      return false;
    }
    StructPtr sa = std::get<StructPtr>(a);
    StructPtr sb = std::get<StructPtr>(b);
    if (!sa || !sb) {
      return sa == sb;
    }
    if (sa->fields.size() != sb->fields.size()) {
      return false;
    }
    for (const auto &kv : sa->fields) {
      auto it = sb->fields.find(kv.first);
      if (it == sb->fields.end() ||
          !equalsDepth(kv.second, it->second, depth + 1)) {
        return false;
      }
    }
    return true;
  }
  if (ta.isArray || tb.isArray) {
    if (!ta.isArray || !tb.isArray) {
      return false;
    }
    return arraysEqual(std::get<ArrayPtr>(a), std::get<ArrayPtr>(b), depth);
  }
  if (ValueHelper::isMap(a) || ValueHelper::isMap(b)) {
    if (!ValueHelper::isMap(a) || !ValueHelper::isMap(b)) {
      return false;
    }
    return mapsEqual(std::get<MapPtr>(a), std::get<MapPtr>(b), depth);
  }
  if (std::holds_alternative<FuncPtr>(a) ||
      std::holds_alternative<FuncPtr>(b)) {
    if (!std::holds_alternative<FuncPtr>(a) ||
        !std::holds_alternative<FuncPtr>(b)) {
      return false;
    }
    const FuncPtr &fa = std::get<FuncPtr>(a);
    const FuncPtr &fb = std::get<FuncPtr>(b);
    if (!fa || !fb) {
      return fa == fb;
    }
    // Two function values are equal iff they wrap the same target: the same
    // lambda body with equal captures, or the same overload set with the
    // same bound `this` (for methods).
    if (fa->body.get() != fb->body.get() ||
        fa->procs.size() != fb->procs.size() ||
        fa->captured.size() != fb->captured.size()) {
      return false;
    }
    for (size_t i = 0; i < fa->procs.size(); ++i) {
      if (fa->procs[i].get() != fb->procs[i].get()) {
        return false;
      }
    }
    for (const auto &kv : fa->captured) {
      auto it = fb->captured.find(kv.first);
      if (it == fb->captured.end() ||
          !equalsDepth(kv.second, it->second, depth + 1)) {
        return false;
      }
    }
    return true;
  }
  if (isFloatingType(ta.baseType) || isFloatingType(tb.baseType)) {
    return ValueHelper::toDouble(a) == ValueHelper::toDouble(b);
  }
  if (std::holds_alternative<std::string>(a) ||
      std::holds_alternative<std::string>(b)) {
    if (!std::holds_alternative<std::string>(a) ||
        !std::holds_alternative<std::string>(b)) {
      return false;
    }
    return std::get<std::string>(a) == std::get<std::string>(b);
  }
  if (std::holds_alternative<bool>(a) || std::holds_alternative<bool>(b)) {
    if (!std::holds_alternative<bool>(a) ||
        !std::holds_alternative<bool>(b)) {
      return false;
    }
    return std::get<bool>(a) == std::get<bool>(b);
  }
  return ValueHelper::toInt64(a) == ValueHelper::toInt64(b);
}
} // namespace

bool ValueHelper::equals(const Value &a, const Value &b) {
  return equalsDepth(a, b, 0);
}

bool ValueHelper::notEquals(const Value &a, const Value &b) {
  return !equals(a, b);
}

bool ValueHelper::logicalAnd(const Value &a, const Value &b) {
  if (!toBool(a)) {
    return false;
  }
  return toBool(b);
}

bool ValueHelper::logicalOr(const Value &a, const Value &b) {
  if (toBool(a)) {
    return true;
  }
  return toBool(b);
}

bool ValueHelper::logicalNot(const Value &a) { return !toBool(a); }

namespace {
bool isSignedIntegerType(DataType t) {
  switch (t) {
  case DataType::INT8:
  case DataType::INT16:
  case DataType::INT32:
  case DataType::INT64:
    return true;
  default:
    return false;
  }
}

void ensureIntegerType(DataType t, const char *opName) {
  switch (t) {
  case DataType::CHAR:
  case DataType::INT8:
  case DataType::UINT8:
  case DataType::INT16:
  case DataType::UINT16:
  case DataType::INT32:
  case DataType::UINT32:
  case DataType::INT64:
  case DataType::UINT64:
    return;
  default:
    throw std::runtime_error(std::string("Operator ") + opName +
                             " only supports integers");
  }
}

void validateShiftAmount(const Value &rhs, DataType rhsType, const char *opName) {
  if (isSignedIntegerType(rhsType) && ValueHelper::toInt64(rhs) < 0) {
    throw std::runtime_error(std::string("Operator ") + opName +
                             " requires a non-negative shift amount");
  }
  uint64_t amount = ValueHelper::toUInt64(rhs);
  if (amount >= 64) {
    throw std::runtime_error(std::string("Operator ") + opName +
                             " shift amount must be in range [0, 63]");
  }
}

template <typename Func>
Value applyIntBinary(const Value &a, const Value &b, Func fn, const char *opName) {
  TypeInfo aType = ValueHelper::getType(a);
  TypeInfo bType = ValueHelper::getType(b);

  if (aType.isArray || bType.isArray) {
    throw std::runtime_error(std::string("Operator ") + opName + " does not support arrays");
  }

  auto ensureInt = [&](DataType t) {
    switch (t) {
    case DataType::CHAR:
    case DataType::INT8:
    case DataType::UINT8:
    case DataType::INT16:
    case DataType::UINT16:
    case DataType::INT32:
    case DataType::UINT32:
    case DataType::INT64:
    case DataType::UINT64:
      return;
    default:
      throw std::runtime_error(std::string("Operator ") + opName + " only supports integers");
    }
  };

  ensureInt(aType.baseType);
  ensureInt(bType.baseType);

  bool unsignedResult = aType.baseType == DataType::UINT8 || aType.baseType == DataType::UINT16 ||
                        aType.baseType == DataType::UINT32 || aType.baseType == DataType::UINT64 ||
                        bType.baseType == DataType::UINT8 || bType.baseType == DataType::UINT16 ||
                        bType.baseType == DataType::UINT32 || bType.baseType == DataType::UINT64;

  if (unsignedResult) {
    uint64_t res = fn(ValueHelper::toUInt64(a), ValueHelper::toUInt64(b));
    return ValueHelper::createValue(DataType::UINT64, res);
  }

  int64_t res = fn(ValueHelper::toInt64(a), ValueHelper::toInt64(b));
  return ValueHelper::createValue(DataType::INT64, res);
}
} // namespace

Value ValueHelper::bitNot(const Value &a) {
  TypeInfo aType = getType(a);
  if (aType.isArray) {
    throw std::runtime_error("Operator ~ does not support arrays");
  }

  auto ensureInt = [&](DataType t) {
    switch (t) {
    case DataType::CHAR:
    case DataType::INT8:
    case DataType::UINT8:
    case DataType::INT16:
    case DataType::UINT16:
    case DataType::INT32:
    case DataType::UINT32:
    case DataType::INT64:
    case DataType::UINT64:
      return;
    default:
      throw std::runtime_error("Operator ~ only supports integers");
    }
  };

  ensureInt(aType.baseType);

  bool unsignedResult = aType.baseType == DataType::UINT8 || aType.baseType == DataType::UINT16 ||
                        aType.baseType == DataType::UINT32 || aType.baseType == DataType::UINT64;

  if (unsignedResult) {
    uint64_t res = ~toUInt64(a);
    return createValue(DataType::UINT64, res);
  }

  int64_t res = ~toInt64(a);
  return createValue(DataType::INT64, res);
}

Value ValueHelper::bitAnd(const Value &a, const Value &b) {
  return applyIntBinary(a, b, [](auto lhs, auto rhs) { return lhs & rhs; }, "&");
}

Value ValueHelper::bitOr(const Value &a, const Value &b) {
  return applyIntBinary(a, b, [](auto lhs, auto rhs) { return lhs | rhs; }, "|");
}

Value ValueHelper::bitXor(const Value &a, const Value &b) {
  return applyIntBinary(a, b, [](auto lhs, auto rhs) { return lhs ^ rhs; }, "^");
}

Value ValueHelper::lshift(const Value &a, const Value &b) {
  TypeInfo aType = getType(a);
  TypeInfo bType = getType(b);

  if (aType.isArray || bType.isArray) {
    throw std::runtime_error("Operator << does not support arrays");
  }

  ensureIntegerType(aType.baseType, "<<");
  ensureIntegerType(bType.baseType, "<<");

  validateShiftAmount(b, bType.baseType, "<<");
  uint64_t amount = toUInt64(b);

  bool lhsUnsigned =
      aType.baseType == DataType::UINT8 || aType.baseType == DataType::UINT16 ||
      aType.baseType == DataType::UINT32 || aType.baseType == DataType::UINT64;

  if (lhsUnsigned) {
    return createValue(DataType::UINT64, toUInt64(a) << amount);
  }

  int64_t lhs = toInt64(a);
  if (lhs < 0) {
    throw std::runtime_error("Operator << on negative signed values is not allowed");
  }
  if (amount > 0 &&
      lhs > (std::numeric_limits<int64_t>::max() >> amount)) {
    throw std::runtime_error("Operator << overflow for signed integer");
  }
  return createValue(DataType::INT64, lhs << amount);
}

Value ValueHelper::rshift(const Value &a, const Value &b) {
  TypeInfo aType = getType(a);
  TypeInfo bType = getType(b);

  if (aType.isArray || bType.isArray) {
    throw std::runtime_error("Operator >> does not support arrays");
  }

  ensureIntegerType(aType.baseType, ">>");
  ensureIntegerType(bType.baseType, ">>");

  validateShiftAmount(b, bType.baseType, ">>");
  uint64_t amount = toUInt64(b);

  bool lhsUnsigned =
      aType.baseType == DataType::UINT8 || aType.baseType == DataType::UINT16 ||
      aType.baseType == DataType::UINT32 || aType.baseType == DataType::UINT64;

  if (lhsUnsigned) {
    return createValue(DataType::UINT64, toUInt64(a) >> amount);
  }
  return createValue(DataType::INT64, toInt64(a) >> amount);
}

ArrayPtr ValueHelper::createArray(const TypeInfo &elementType, const std::vector<Value> &elements) {
  auto arr = std::make_shared<ArrayValue>();
  arr->elementType = elementType;
  arr->elements = elements;
  return arr;
}

bool ValueHelper::isArray(const Value &val) { return std::holds_alternative<ArrayPtr>(val); }

TypeInfo ValueHelper::arrayElementType(const Value &val) {
  if (!isArray(val)) {
    throw std::runtime_error("Value is not an array");
  }
  ArrayPtr arr = std::get<ArrayPtr>(val);
  if (!arr) {
    return TypeInfo(DataType::VOID);
  }
  return arr->elementType;
}

std::vector<Value> &ValueHelper::arrayElements(Value &val) {
  if (!isArray(val)) {
    throw std::runtime_error("Value is not an array");
  }
  return std::get<ArrayPtr>(val)->elements;
}

const std::vector<Value> &ValueHelper::arrayElements(const Value &val) {
  if (!isArray(val)) {
    throw std::runtime_error("Value is not an array");
  }
  return std::get<ArrayPtr>(val)->elements;
}

MapPtr ValueHelper::createMap(const TypeInfo &keyType,
                              const TypeInfo &valueType) {
  auto m = std::make_shared<MapValue>();
  m->keyType = keyType;
  m->valueType = valueType;
  return m;
}

bool ValueHelper::isMap(const Value &val) {
  return std::holds_alternative<MapPtr>(val);
}

std::map<Value, Value> &ValueHelper::mapEntries(Value &val) {
  if (!isMap(val)) {
    throw std::runtime_error("Value is not a map");
  }
  return std::get<MapPtr>(val)->entries;
}

const std::map<Value, Value> &ValueHelper::mapEntries(const Value &val) {
  if (!isMap(val)) {
    throw std::runtime_error("Value is not a map");
  }
  return std::get<MapPtr>(val)->entries;
}

TypeInfo ValueHelper::mapKeyType(const Value &val) {
  if (!isMap(val)) {
    throw std::runtime_error("Value is not a map");
  }
  MapPtr m = std::get<MapPtr>(val);
  return m ? m->keyType : TypeInfo(DataType::VOID);
}

TypeInfo ValueHelper::mapValueType(const Value &val) {
  if (!isMap(val)) {
    throw std::runtime_error("Value is not a map");
  }
  MapPtr m = std::get<MapPtr>(val);
  return m ? m->valueType : TypeInfo(DataType::VOID);
}

bool ValueHelper::isStruct(const Value &val) {
  return std::holds_alternative<StructPtr>(val);
}

std::string ValueHelper::structTypeName(const Value &val) {
  if (!isStruct(val)) {
    throw std::runtime_error("Value is not a struct");
  }
  StructPtr sv = std::get<StructPtr>(val);
  return sv ? sv->typeName : "";
}

Value ValueHelper::convertElement(const Value &val, const TypeInfo &target) {
  if (target.isArray) {
    if (!isArray(val)) {
      throw std::runtime_error("Expected array value");
    }
    ArrayPtr src = std::get<ArrayPtr>(val);
    TypeInfo elemT = target.elementType();
    if (src->elementType == elemT) {
      return val;
    }
    std::vector<Value> out;
    out.reserve(src->elements.size());
    for (const auto &e : src->elements) {
      out.push_back(convertElement(e, elemT));
    }
    return createArray(elemT, out);
  }
  if (target.isMap) {
    if (!isMap(val)) {
      throw std::runtime_error("Expected map value");
    }
    return val; // map entry conversion is the caller's concern
  }
  if (target.isFunction) {
    if (std::holds_alternative<FuncPtr>(val)) {
      return val;
    }
    throw std::runtime_error("Expected function value");
  }
  if (target.isStruct) {
    TypeInfo src = getType(val);
    if (src.isStruct && src.structName == target.structName) {
      return val;
    }
    throw std::runtime_error("Expected struct '" + target.structName + "'");
  }
  DataType t = target.baseType;
  switch (t) {
  case DataType::CHAR:
  case DataType::INT8:
  case DataType::INT16:
  case DataType::INT32:
  case DataType::INT64:
    return createValue(t, toInt64(val));
  case DataType::UINT8:
  case DataType::UINT16:
  case DataType::UINT32:
  case DataType::UINT64:
    return createValue(t, toUInt64(val));
  case DataType::FLOAT:
  case DataType::DOUBLE:
    return createValue(t, toDouble(val));
  case DataType::STRING:
    return createValue(t, toString(val));
  case DataType::BOOL:
    return createValue(t, toBool(val));
  case DataType::VOID:
    throw std::runtime_error("Cannot store void elements in array");
  }
  throw std::runtime_error("Unsupported element conversion");
}

Value ValueHelper::createValue(DataType type, int64_t intVal) {
  switch (type) {
  case DataType::CHAR:
    return static_cast<char>(intVal);
  case DataType::INT8:
    return static_cast<int8_t>(intVal);
  case DataType::INT16:
    return static_cast<int16_t>(intVal);
  case DataType::INT32:
    return static_cast<int32_t>(intVal);
  case DataType::INT64:
    return intVal;
  case DataType::FLOAT:
    return static_cast<float>(intVal);
  case DataType::DOUBLE:
    return static_cast<double>(intVal);
  case DataType::BOOL:
    return intVal != 0;
  default:
    return static_cast<int32_t>(intVal);
  }
}

Value ValueHelper::createValue(DataType type, uint64_t uintVal) {
  switch (type) {
  case DataType::CHAR:
    return static_cast<char>(uintVal);
  case DataType::UINT8:
    return static_cast<uint8_t>(uintVal);
  case DataType::UINT16:
    return static_cast<uint16_t>(uintVal);
  case DataType::UINT32:
    return static_cast<uint32_t>(uintVal);
  case DataType::UINT64:
    return uintVal;
  case DataType::FLOAT:
    return static_cast<float>(uintVal);
  case DataType::DOUBLE:
    return static_cast<double>(uintVal);
  default:
    return static_cast<uint32_t>(uintVal);
  }
}

Value ValueHelper::createValue(DataType type, double doubleVal) {
  switch (type) {
  case DataType::FLOAT:
    return static_cast<float>(doubleVal);
  case DataType::DOUBLE:
    return doubleVal;
  case DataType::CHAR:
  case DataType::INT8:
  case DataType::INT16:
  case DataType::INT32:
  case DataType::INT64:
    return createValue(type, static_cast<int64_t>(doubleVal));
  case DataType::UINT8:
  case DataType::UINT16:
  case DataType::UINT32:
  case DataType::UINT64:
    return createValue(type, static_cast<uint64_t>(doubleVal));
  case DataType::BOOL:
    return createValue(type, doubleVal != 0.0);
  default:
    throw std::runtime_error("Cannot create value of requested type from double");
  }
}

Value ValueHelper::createValue(DataType type, const std::string &strVal) {
  if (type == DataType::STRING)
    return strVal;
  throw std::runtime_error("Cannot create non-string value from string");
}

Value ValueHelper::createValue(DataType type, bool boolVal) {
  if (type == DataType::BOOL)
    return boolVal;
  throw std::runtime_error("Cannot create non-bool value from bool");
}

} // namespace Script
