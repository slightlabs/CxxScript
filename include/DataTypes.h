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
    VOID,
    NIL
};

struct TypeInfo {
    DataType baseType;      // element/value type (array element or map value)
    bool isArray;
    bool isMap;
    bool isStruct = false;
    DataType keyType;       // valid only when isMap
    std::shared_ptr<TypeInfo> mapValueType; // full value type when isMap
    std::string structName; // declared name when isStruct
    // Full element type when isArray. When null the element is the scalar
    // described by the other fields (one-dimensional array, legacy form).
    std::shared_ptr<TypeInfo> arrayElem;

    // Function values (lambdas / procedure references)
    bool isFunction = false;
    std::vector<TypeInfo> paramTypes;              // empty = 0 params
    std::shared_ptr<TypeInfo> retType;             // null = inferred/any
    // When true the signature is intentionally unchecked: an overloaded
    // procedure reference whose target signature is chosen per call.
    bool fnOpaque = false;

    bool isAuto = false; // `auto`: type inferred from context/initializer

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

    // T[] preserving the element's full type — supports nested element
    // types: arrays, maps, and structs (e.g. int32[][], Point[], map<K,V>[]).
    static TypeInfo arrayOf(const TypeInfo &elem) {
        TypeInfo t(elem.baseType, true, false, DataType::VOID, nullptr,
                   elem.isStruct, elem.structName);
        t.arrayElem = std::make_shared<TypeInfo>(elem);
        return t;
    }

    // `auto` — resolved from the initializer at declaration time.
    static TypeInfo autoType() {
        TypeInfo t;
        t.isAuto = true;
        return t;
    }

    // Function type: fn(paramTypes) -> ret (ret null = inferred).
    static TypeInfo functionOf(std::vector<TypeInfo> params,
                               std::shared_ptr<TypeInfo> ret) {
        TypeInfo t;
        t.isFunction = true;
        t.paramTypes = std::move(params);
        t.retType = std::move(ret);
        return t;
    }

    // Opaque function reference (overloaded procedure) — signature checked
    // per call at runtime.
    static TypeInfo opaqueFunction() {
        TypeInfo t;
        t.isFunction = true;
        t.fnOpaque = true;
        return t;
    }

    // Element type when this describes an array; handles nested and
    // struct elements.
    TypeInfo elementType() const {
        if (arrayElem) {
            return *arrayElem;
        }
        TypeInfo t = *this;
        t.isArray = false;
        t.arrayElem = nullptr;
        return t;
    }

    bool operator==(const TypeInfo &other) const {
        if (baseType != other.baseType || isArray != other.isArray ||
            isMap != other.isMap || isStruct != other.isStruct ||
            keyType != other.keyType || structName != other.structName ||
            isFunction != other.isFunction || isAuto != other.isAuto)
            return false;
        if (isFunction) {
            if (fnOpaque != other.fnOpaque || paramTypes != other.paramTypes)
                return false;
            if (retType || other.retType) {
                if (!retType || !other.retType)
                    return false;
                return *retType == *other.retType;
            }
            return true;
        }
        if (isMap && (mapValueType || other.mapValueType)) {
            if (!mapValueType || !other.mapValueType)
                return false;
            if (!(*mapValueType == *other.mapValueType))
                return false;
        }
        // Normalized element comparison: handles both the legacy flat form
        // (arrayElem == null) and the nested form.
        if (isArray && !(elementType() == other.elementType()))
            return false;
        return true;
    }

    bool operator!=(const TypeInfo &other) const { return !(*this == other); }
};

struct ArrayValue;
struct MapValue;
struct StructValue;
struct FunctionValue;
using ArrayPtr = std::shared_ptr<ArrayValue>;
using MapPtr = std::shared_ptr<MapValue>;
using StructPtr = std::shared_ptr<StructValue>;
using FuncPtr = std::shared_ptr<FunctionValue>;

// Variant to hold any script value
// The `null` literal's value tag. A distinct empty struct rather than
// std::nullptr_t so std::variant's ordered comparison (used for
// map<Value, Value> keys) stays well-formed.
struct NullValue {
  friend constexpr bool operator==(NullValue, NullValue) { return true; }
  friend constexpr bool operator!=(NullValue, NullValue) { return false; }
  friend constexpr bool operator<(NullValue, NullValue) { return false; }
  friend constexpr bool operator<=(NullValue, NullValue) { return true; }
  friend constexpr bool operator>(NullValue, NullValue) { return false; }
  friend constexpr bool operator>=(NullValue, NullValue) { return true; }
};

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
    StructPtr,
    FuncPtr,
    NullValue
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

    // `null` literal value (variant alternative std::nullptr_t)
    static bool isNull(const Value &val);
    static Value nullValue();
};

} // namespace Script
