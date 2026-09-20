#pragma once

#include <cstdint>
#include <map>
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
    DataType baseType;      // element/value type (array element or map value)
    bool isArray;
    bool isMap;
    bool isStruct = false;
    DataType keyType;       // valid only when isMap
    std::shared_ptr<TypeInfo> mapValueType; // full value type when isMap
    std::string structName; // declared name when isStruct

    TypeInfo(DataType b = DataType::VOID, bool arr = false, bool map = false,
             DataType key = DataType::VOID,
             std::shared_ptr<TypeInfo> valType = nullptr, bool isStruct = false,
             const std::string &structName = "")
        : baseType(b), isArray(arr), isMap(map), isStruct(isStruct),
          keyType(key), mapValueType(std::move(valType)),
          structName(structName) {}

    // map<K, V>
    static TypeInfo mapOf(const TypeInfo &key, const TypeInfo &val) {
        return TypeInfo(val.baseType, false, true, key.baseType,
                        std::make_shared<TypeInfo>(val));
    }

    // User-declared struct type. Also used with isArray for T[] fields/arrays.
    static TypeInfo structOf(const std::string &name) {
        TypeInfo t(DataType::VOID, false, false, DataType::VOID, nullptr, true,
                   name);
        return t;
    }

    // T[] preserving the element's full type (incl. struct name).
    static TypeInfo arrayOf(const TypeInfo &elem) {
        TypeInfo t = elem;
        t.isArray = true;
        return t;
    }

    // Element type when this describes an array; handles struct elements.
    TypeInfo elementType() const {
        TypeInfo t = *this;
        t.isArray = false;
        return t;
    }

    bool operator==(const TypeInfo &other) const {
        if (baseType != other.baseType || isArray != other.isArray ||
            isMap != other.isMap || isStruct != other.isStruct ||
            keyType != other.keyType || structName != other.structName)
            return false;
        if (isMap && (mapValueType || other.mapValueType)) {
            if (!mapValueType || !other.mapValueType)
                return false;
            return *mapValueType == *other.mapValueType;
        }
        return true;
    }

    bool operator!=(const TypeInfo &other) const { return !(*this == other); }
};

struct ArrayValue;
struct MapValue;
struct StructValue;
using ArrayPtr = std::shared_ptr<ArrayValue>;
using MapPtr = std::shared_ptr<MapValue>;
using StructPtr = std::shared_ptr<StructValue>;

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
    ArrayPtr,
    MapPtr,
    StructPtr
>;

struct ArrayValue {
    TypeInfo elementType;
    std::vector<Value> elements;
};

// map<K, V> — keys are scalar Values ordered by variant operator<
struct MapValue {
    TypeInfo keyType;
    TypeInfo valueType;
    std::map<Value, Value> entries;
};

// Instance of a user-declared struct. Shared via pointer, so writes through
// member access are visible to every reference (same model as arrays/maps).
struct StructValue {
    std::string typeName;
    std::vector<std::string> fieldOrder; // declaration order, for printing
    std::map<std::string, Value> fields;
};

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

    // Map helpers
    static MapPtr createMap(const TypeInfo &keyType, const TypeInfo &valueType);
    static bool isMap(const Value &val);
    static std::map<Value, Value> &mapEntries(Value &val);
    static const std::map<Value, Value> &mapEntries(const Value &val);
    static TypeInfo mapKeyType(const Value &val);
    static TypeInfo mapValueType(const Value &val);

    // Struct helpers
    static bool isStruct(const Value &val);
    static std::string structTypeName(const Value &val);
};

} // namespace Script
