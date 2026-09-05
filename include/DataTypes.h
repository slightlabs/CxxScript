#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <stdexcept>
#include <vector>
#include <memory>

namespace Script {

enum class DataType {
    CHAR,
    INT8,
    UINT8,
    INT16,
    UINT16,
    INT32,
    UINT32,
    INT64,
    UINT64,
    FLOAT,
    DOUBLE,
    STRING,
    BOOL,
    VOID
};

struct TypeInfo {
    DataType baseType;
    bool isArray;

    TypeInfo(DataType b = DataType::VOID, bool arr = false)
        : baseType(b), isArray(arr) {}

    bool operator==(const TypeInfo &other) const {
        return baseType == other.baseType && isArray == other.isArray;
    }

    bool operator!=(const TypeInfo &other) const { return !(*this == other); }
};

struct ArrayValue {
    DataType elementType;
    std::vector<std::variant<
        char,
        int8_t,
        uint8_t,
        int16_t,
        uint16_t,
        int32_t,
        uint32_t,
        int64_t,
        uint64_t,
        float,
        double,
        std::string,
        bool,
        std::shared_ptr<ArrayValue>>> elements;
};

using ArrayPtr = std::shared_ptr<ArrayValue>;

// Variant to hold any script value
using Value = std::variant<
    char,
    int8_t,
    uint8_t,
    int16_t,
    uint16_t,
    int32_t,
    uint32_t,
    int64_t,
    uint64_t,
    float,
    double,
    std::string,
    bool,
    ArrayPtr
>;

class ValueHelper {
public:
    static TypeInfo getType(const Value& val);
    static std::string typeToString(const TypeInfo &type);
    static TypeInfo stringToType(const std::string& str);
    
    // Conversion helpers
    static int64_t toInt64(const Value& val);
    static uint64_t toUInt64(const Value& val);
    static double toDouble(const Value& val);
    static bool toBool(const Value& val);
    static std::string toString(const Value& val);
    
    // Arithmetic operations
    static Value add(const Value& a, const Value& b);
    static Value subtract(const Value& a, const Value& b);
    static Value multiply(const Value& a, const Value& b);
    static Value divide(const Value& a, const Value& b);
    static Value modulo(const Value& a, const Value& b);
    
    // Comparison operations
    static bool greaterThan(const Value& a, const Value& b);
    static bool lessThan(const Value& a, const Value& b);
    static bool greaterOrEqual(const Value& a, const Value& b);
    static bool lessOrEqual(const Value& a, const Value& b);
    static bool equals(const Value& a, const Value& b);
    static bool notEquals(const Value& a, const Value& b);
    
    // Logical operations
    static bool logicalAnd(const Value& a, const Value& b);
    static bool logicalOr(const Value& a, const Value& b);
    static bool logicalNot(const Value& a);
    static Value bitNot(const Value& a);

    // Bitwise operations (integers only)
    static Value bitAnd(const Value& a, const Value& b);
    static Value bitOr(const Value& a, const Value& b);
    static Value bitXor(const Value& a, const Value& b);
    static Value lshift(const Value& a, const Value& b);
    static Value rshift(const Value& a, const Value& b);
    
    // Create value of specific type
    static Value createValue(DataType type, int64_t intVal);
    static Value createValue(DataType type, uint64_t uintVal);
    static Value createValue(DataType type, double doubleVal);
    static Value createValue(DataType type, const std::string& strVal);
    static Value createValue(DataType type, bool boolVal);

    // Array helpers
    static ArrayPtr createArray(const TypeInfo &elementType, const std::vector<Value> &values);
    static bool isArray(const Value &val);
    static TypeInfo arrayElementType(const Value &val);
    static std::vector<Value> &arrayElements(Value &val);
    static const std::vector<Value> &arrayElements(const Value &val);
    static Value convertElement(const Value &val, const TypeInfo &target);
};

} // namespace Script
