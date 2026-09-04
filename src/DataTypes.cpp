#include "DataTypes.h"
#include <limits>
#include <stdexcept>

namespace Script {

TypeInfo ValueHelper::getType(const Value &val) {
  if (std::holds_alternative<ArrayPtr>(val)) {
    ArrayPtr arr = std::get<ArrayPtr>(val);
    if (!arr) {
      return TypeInfo(DataType::VOID, true);
    }
    return TypeInfo(arr->elementType, true);
  }
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
  if (std::holds_alternative<double>(val))
    return TypeInfo(DataType::DOUBLE);
  if (std::holds_alternative<std::string>(val))
    return TypeInfo(DataType::STRING);
  if (std::holds_alternative<bool>(val))
    return TypeInfo(DataType::BOOL);
  return TypeInfo(DataType::VOID);
}

std::string ValueHelper::typeToString(const TypeInfo &type) {
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
  case DataType::DOUBLE:
    base = "double";
    break;
  case DataType::STRING:
    base = "string";
    break;
  case DataType::BOOL:
    base = "bool";
    break;
  case DataType::VOID:
    base = "void";
    break;
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
  if (str == "double")
    return TypeInfo(DataType::DOUBLE);
  if (str == "string")
    return TypeInfo(DataType::STRING);
  if (str == "bool")
    return TypeInfo(DataType::BOOL);
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
        } else {
          return arg != 0;
        }
      },
      val);
}

std::string ValueHelper::toString(const Value &val) {
  if (std::holds_alternative<ArrayPtr>(val)) {
    return "[array]";
  }
  return std::visit(
      [](auto &&arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) {
          return arg;
        } else if constexpr (std::is_same_v<T, bool>) {
          return arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, double>) {
          return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, ArrayPtr>) {
          return std::string("[array]");
        } else {
          return std::to_string(arg);
        }
      },
      val);
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

  if (aType.baseType == DataType::DOUBLE || bType.baseType == DataType::DOUBLE) {
    double result = toDouble(a) + toDouble(b);
    return createValue(DataType::DOUBLE, result);
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

  if (aType.baseType == DataType::DOUBLE || bType.baseType == DataType::DOUBLE) {
    double result = toDouble(a) - toDouble(b);
    return createValue(DataType::DOUBLE, result);
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

  if (aType.baseType == DataType::DOUBLE || bType.baseType == DataType::DOUBLE) {
    double result = toDouble(a) * toDouble(b);
    return createValue(DataType::DOUBLE, result);
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

  if (ta.baseType == DataType::DOUBLE || tb.baseType == DataType::DOUBLE) {
    double divisor = toDouble(b);
    if (divisor == 0.0) {
      throw std::runtime_error("Division by zero");
    }
    double result = toDouble(a) / divisor;
    return createValue(DataType::DOUBLE, result);
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

  if (ta.baseType == DataType::DOUBLE || tb.baseType == DataType::DOUBLE) {
    throw std::runtime_error("Modulo not supported for floating point");
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

bool ValueHelper::greaterThan(const Value &a, const Value &b) {
  TypeInfo ta = getType(a);
  TypeInfo tb = getType(b);
  if (ta.isArray || tb.isArray) {
    throw std::runtime_error("Comparison not supported for arrays");
  }
  if (std::holds_alternative<std::string>(a) &&
      std::holds_alternative<std::string>(b)) {
    return std::get<std::string>(a) > std::get<std::string>(b);
  }
  if (ta.baseType == DataType::DOUBLE || tb.baseType == DataType::DOUBLE) {
    return toDouble(a) > toDouble(b);
  }
  return toInt64(a) > toInt64(b);
}

bool ValueHelper::lessThan(const Value &a, const Value &b) {
  TypeInfo ta = getType(a);
  TypeInfo tb = getType(b);
  if (ta.isArray || tb.isArray) {
    throw std::runtime_error("Comparison not supported for arrays");
  }
  if (std::holds_alternative<std::string>(a) &&
      std::holds_alternative<std::string>(b)) {
    return std::get<std::string>(a) < std::get<std::string>(b);
  }
  if (ta.baseType == DataType::DOUBLE || tb.baseType == DataType::DOUBLE) {
    return toDouble(a) < toDouble(b);
  }
  return toInt64(a) < toInt64(b);
}

bool ValueHelper::greaterOrEqual(const Value &a, const Value &b) {
  TypeInfo ta = getType(a);
  TypeInfo tb = getType(b);
  if (ta.isArray || tb.isArray) {
    throw std::runtime_error("Comparison not supported for arrays");
  }
  if (std::holds_alternative<std::string>(a) &&
      std::holds_alternative<std::string>(b)) {
    return std::get<std::string>(a) >= std::get<std::string>(b);
  }
  if (ta.baseType == DataType::DOUBLE || tb.baseType == DataType::DOUBLE) {
    return toDouble(a) >= toDouble(b);
  }
  return toInt64(a) >= toInt64(b);
}

bool ValueHelper::lessOrEqual(const Value &a, const Value &b) {
  TypeInfo ta = getType(a);
  TypeInfo tb = getType(b);
  if (ta.isArray || tb.isArray) {
    throw std::runtime_error("Comparison not supported for arrays");
  }
  if (std::holds_alternative<std::string>(a) &&
      std::holds_alternative<std::string>(b)) {
    return std::get<std::string>(a) <= std::get<std::string>(b);
  }
  if (ta.baseType == DataType::DOUBLE || tb.baseType == DataType::DOUBLE) {
    return toDouble(a) <= toDouble(b);
  }
  return toInt64(a) <= toInt64(b);
}

namespace {
bool arraysEqual(const ArrayPtr &lhs, const ArrayPtr &rhs) {
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
    if (!ValueHelper::equals(lhs->elements[i], rhs->elements[i])) {
      return false;
    }
  }
  return true;
}
} // namespace

bool ValueHelper::equals(const Value &a, const Value &b) {
  TypeInfo ta = getType(a);
  TypeInfo tb = getType(b);
  if (ta.isArray || tb.isArray) {
    if (!ta.isArray || !tb.isArray) {
      return false;
    }
    return arraysEqual(std::get<ArrayPtr>(a), std::get<ArrayPtr>(b));
  }

  if (ta.baseType == DataType::DOUBLE || tb.baseType == DataType::DOUBLE) {
    return toDouble(a) == toDouble(b);
  }

  // String comparison requires both strings
  if (std::holds_alternative<std::string>(a) || std::holds_alternative<std::string>(b)) {
    if (!std::holds_alternative<std::string>(a) || !std::holds_alternative<std::string>(b)) {
      return false;
    }
    return std::get<std::string>(a) == std::get<std::string>(b);
  }

  // Bool comparison requires both bools
  if (std::holds_alternative<bool>(a) || std::holds_alternative<bool>(b)) {
    if (!std::holds_alternative<bool>(a) || !std::holds_alternative<bool>(b)) {
      return false;
    }
    return std::get<bool>(a) == std::get<bool>(b);
  }

  // Numeric comparison
  return toInt64(a) == toInt64(b);
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
  if (elementType.isArray) {
    throw std::runtime_error("Nested arrays are not supported");
  }
  auto arr = std::make_shared<ArrayValue>();
  arr->elementType = elementType.baseType;
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
  return TypeInfo(arr->elementType);
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

Value ValueHelper::convertElement(const Value &val, const TypeInfo &target) {
  if (target.isArray) {
    throw std::runtime_error("Nested arrays are not supported");
  }
  DataType t = target.baseType;
  switch (t) {
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
  case DataType::INT8:
    return static_cast<int8_t>(intVal);
  case DataType::INT16:
    return static_cast<int16_t>(intVal);
  case DataType::INT32:
    return static_cast<int32_t>(intVal);
  case DataType::INT64:
    return intVal;
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
  case DataType::UINT8:
    return static_cast<uint8_t>(uintVal);
  case DataType::UINT16:
    return static_cast<uint16_t>(uintVal);
  case DataType::UINT32:
    return static_cast<uint32_t>(uintVal);
  case DataType::UINT64:
    return uintVal;
  case DataType::DOUBLE:
    return static_cast<double>(uintVal);
  default:
    return static_cast<uint32_t>(uintVal);
  }
}

Value ValueHelper::createValue(DataType type, double doubleVal) {
  switch (type) {
  case DataType::DOUBLE:
    return doubleVal;
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
