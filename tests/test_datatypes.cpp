// Direct unit coverage for ValueHelper / TypeInfo: conversions, arithmetic
// promotion, equality, string<->type round-trips, and container helpers.
#include "DataTypes.h"
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>

using namespace Script;

// --- getType / typeToString / stringToType ------------------------------------

TEST(DataTypesTest, GetTypeScalars) {
  EXPECT_EQ(ValueHelper::getType(static_cast<int8_t>(1)).baseType,
            DataType::INT8);
  EXPECT_EQ(ValueHelper::getType(static_cast<uint8_t>(1)).baseType,
            DataType::UINT8);
  EXPECT_EQ(ValueHelper::getType(static_cast<int16_t>(1)).baseType,
            DataType::INT16);
  EXPECT_EQ(ValueHelper::getType(static_cast<uint16_t>(1)).baseType,
            DataType::UINT16);
  EXPECT_EQ(ValueHelper::getType(static_cast<int32_t>(1)).baseType,
            DataType::INT32);
  EXPECT_EQ(ValueHelper::getType(static_cast<uint32_t>(1)).baseType,
            DataType::UINT32);
  EXPECT_EQ(ValueHelper::getType(static_cast<int64_t>(1)).baseType,
            DataType::INT64);
  EXPECT_EQ(ValueHelper::getType(static_cast<uint64_t>(1)).baseType,
            DataType::UINT64);
  EXPECT_EQ(ValueHelper::getType(1.5f).baseType, DataType::FLOAT);
  EXPECT_EQ(ValueHelper::getType(1.5).baseType, DataType::DOUBLE);
  EXPECT_EQ(ValueHelper::getType(std::string("x")).baseType,
            DataType::STRING);
  EXPECT_EQ(ValueHelper::getType(true).baseType, DataType::BOOL);
  EXPECT_EQ(ValueHelper::getType('c').baseType, DataType::CHAR);
}

TEST(DataTypesTest, TypeToStringScalars) {
  EXPECT_EQ(ValueHelper::typeToString(TypeInfo(DataType::INT8)), "int8");
  EXPECT_EQ(ValueHelper::typeToString(TypeInfo(DataType::UINT64)), "uint64");
  EXPECT_EQ(ValueHelper::typeToString(TypeInfo(DataType::FLOAT)), "float");
  EXPECT_EQ(ValueHelper::typeToString(TypeInfo(DataType::DOUBLE)), "double");
  EXPECT_EQ(ValueHelper::typeToString(TypeInfo(DataType::STRING)), "string");
  EXPECT_EQ(ValueHelper::typeToString(TypeInfo(DataType::BOOL)), "bool");
  EXPECT_EQ(ValueHelper::typeToString(TypeInfo(DataType::CHAR)), "char");
  EXPECT_EQ(ValueHelper::typeToString(TypeInfo(DataType::VOID)), "void");
}

TEST(DataTypesTest, TypeToStringContainers) {
  TypeInfo arr = TypeInfo::arrayOf(TypeInfo(DataType::INT32));
  EXPECT_EQ(ValueHelper::typeToString(arr), "int32[]");

  TypeInfo nested = TypeInfo::arrayOf(arr);
  EXPECT_EQ(ValueHelper::typeToString(nested), "int32[][]");

  TypeInfo map =
      TypeInfo::mapOf(TypeInfo(DataType::STRING), TypeInfo(DataType::INT32));
  EXPECT_EQ(ValueHelper::typeToString(map), "map<string, int32>");

  TypeInfo s = TypeInfo::structOf("Point");
  EXPECT_EQ(ValueHelper::typeToString(s), "Point");
  EXPECT_EQ(ValueHelper::typeToString(TypeInfo::arrayOf(s)), "Point[]");
}

TEST(DataTypesTest, TypeToStringFunction) {
  TypeInfo fn = TypeInfo::functionOf(
      {TypeInfo(DataType::INT32), TypeInfo(DataType::STRING)},
      std::make_shared<TypeInfo>(DataType::BOOL));
  EXPECT_EQ(ValueHelper::typeToString(fn), "fn(int32, string)->bool");

  TypeInfo voidFn = TypeInfo::functionOf({}, nullptr);
  EXPECT_EQ(ValueHelper::typeToString(voidFn), "fn()");

  TypeInfo opaque = TypeInfo::opaqueFunction();
  EXPECT_EQ(ValueHelper::typeToString(opaque), "fn()");
}

TEST(DataTypesTest, StringToTypeRoundTrip) {
  for (const char *name :
       {"int8", "uint8", "int16", "uint16", "int32", "uint32", "int64",
        "uint64", "float", "double", "string", "bool", "char", "void"}) {
    TypeInfo t = ValueHelper::stringToType(name);
    EXPECT_EQ(ValueHelper::typeToString(t), name) << name;
  }
}

TEST(DataTypesTest, StringToTypeRejectsNonScalars) {
  // stringToType only recognizes plain scalar names — composite and
  // user-declared names throw.
  EXPECT_THROW(ValueHelper::stringToType("int32[]"), std::runtime_error);
  EXPECT_THROW(ValueHelper::stringToType("map<string, int32>"),
               std::runtime_error);
  EXPECT_THROW(ValueHelper::stringToType("Widget"), std::runtime_error);
  EXPECT_THROW(ValueHelper::stringToType(""), std::runtime_error);
}

TEST(DataTypesTest, TypeInfoEquality) {
  EXPECT_TRUE(TypeInfo(DataType::INT32) == TypeInfo(DataType::INT32));
  EXPECT_FALSE(TypeInfo(DataType::INT32) == TypeInfo(DataType::INT64));
  EXPECT_TRUE(TypeInfo::arrayOf(TypeInfo(DataType::INT32)) ==
              TypeInfo::arrayOf(TypeInfo(DataType::INT32)));
  EXPECT_FALSE(TypeInfo::arrayOf(TypeInfo(DataType::INT32)) ==
               TypeInfo::arrayOf(TypeInfo(DataType::INT64)));
  EXPECT_TRUE(TypeInfo::structOf("A") == TypeInfo::structOf("A"));
  EXPECT_FALSE(TypeInfo::structOf("A") == TypeInfo::structOf("B"));
  // Array of int32 != map<string, int32>
  EXPECT_FALSE(TypeInfo::arrayOf(TypeInfo(DataType::INT32)) ==
               TypeInfo::mapOf(TypeInfo(DataType::STRING),
                               TypeInfo(DataType::INT32)));
}

TEST(DataTypesTest, ElementTypeFlattensArrays) {
  TypeInfo arr = TypeInfo::arrayOf(TypeInfo(DataType::INT32));
  TypeInfo elem = arr.elementType();
  EXPECT_FALSE(elem.isArray);
  EXPECT_EQ(elem.baseType, DataType::INT32);

  TypeInfo nested = TypeInfo::arrayOf(arr);
  EXPECT_TRUE(nested.elementType().isArray);
}

// --- Numeric conversions -------------------------------------------------------

TEST(DataTypesTest, ToInt64FromAllIntegerTypes) {
  EXPECT_EQ(ValueHelper::toInt64(static_cast<int8_t>(-5)), -5);
  EXPECT_EQ(ValueHelper::toInt64(static_cast<uint8_t>(250)), 250);
  EXPECT_EQ(ValueHelper::toInt64(static_cast<int16_t>(-300)), -300);
  EXPECT_EQ(ValueHelper::toInt64(static_cast<uint16_t>(60000)), 60000);
  EXPECT_EQ(ValueHelper::toInt64(static_cast<int32_t>(-70000)), -70000);
  EXPECT_EQ(ValueHelper::toInt64(static_cast<uint32_t>(4000000000u)),
            4000000000LL);
  EXPECT_EQ(ValueHelper::toInt64(static_cast<int64_t>(-5e18)), -5000000000000000000LL);
  EXPECT_EQ(ValueHelper::toInt64(static_cast<uint64_t>(18000000000000000000ULL)),
            -446744073709551616LL); // wraps past INT64_MAX
  EXPECT_EQ(ValueHelper::toInt64('A'), 65);
  EXPECT_EQ(ValueHelper::toInt64(true), 1);
}

TEST(DataTypesTest, ToDoubleAndToBool) {
  EXPECT_DOUBLE_EQ(ValueHelper::toDouble(2.5f), 2.5);
  EXPECT_DOUBLE_EQ(ValueHelper::toDouble(-7), -7.0);
  EXPECT_DOUBLE_EQ(ValueHelper::toDouble('B'), 66.0);
  EXPECT_TRUE(ValueHelper::toBool(true));
  EXPECT_TRUE(ValueHelper::toBool(5));
  EXPECT_FALSE(ValueHelper::toBool(0));
  EXPECT_TRUE(ValueHelper::toBool(std::string("x")));
  EXPECT_FALSE(ValueHelper::toBool(std::string("")));
}

TEST(DataTypesTest, ToInt64OnStringThrows) {
  EXPECT_THROW(ValueHelper::toInt64(std::string("x")), std::runtime_error);
  EXPECT_THROW(ValueHelper::toDouble(std::string("x")), std::runtime_error);
}

TEST(DataTypesTest, ToStringFormats) {
  EXPECT_EQ(ValueHelper::toString(std::string("s")), "s");
  EXPECT_EQ(ValueHelper::toString(true), "true");
  EXPECT_EQ(ValueHelper::toString(false), "false");
  EXPECT_EQ(ValueHelper::toString('z'), "z");
  EXPECT_EQ(ValueHelper::toString(static_cast<int32_t>(-3)), "-3");
  EXPECT_EQ(ValueHelper::toString(static_cast<uint64_t>(7)), "7");
}

// --- Arithmetic ---------------------------------------------------------------

TEST(DataTypesTest, AddPromotesAndConcatenates) {
  Value v = ValueHelper::add(static_cast<int8_t>(1), static_cast<int64_t>(2));
  EXPECT_EQ(std::get<int64_t>(v), 3);

  Value s = ValueHelper::add(std::string("a"), 5);
  EXPECT_EQ(std::get<std::string>(s), "a5");

  Value s2 = ValueHelper::add(5, std::string("a"));
  EXPECT_EQ(std::get<std::string>(s2), "5a");

  Value f = ValueHelper::add(1.5, 2);
  EXPECT_DOUBLE_EQ(std::get<double>(f), 3.5);
}

TEST(DataTypesTest, SubtractMultiplyDividePromote) {
  // Results keep the wider operand's type (int32*int32 -> int32).
  EXPECT_DOUBLE_EQ(std::get<double>(ValueHelper::subtract(1, 2.5)), -1.5);
  EXPECT_EQ(std::get<int32_t>(ValueHelper::multiply(3, 4)), 12);
  EXPECT_DOUBLE_EQ(std::get<double>(ValueHelper::divide(7.0, 2)), 3.5);
  EXPECT_EQ(std::get<int64_t>(ValueHelper::multiply(static_cast<int8_t>(3),
                                                  static_cast<int64_t>(4))),
            12);
}

TEST(DataTypesTest, IntegerDivisionAndModulo) {
  EXPECT_EQ(std::get<int32_t>(ValueHelper::divide(7, 2)), 3);
  EXPECT_EQ(std::get<int32_t>(ValueHelper::divide(-7, 2)), -3);
  EXPECT_EQ(std::get<int32_t>(ValueHelper::modulo(-7, 3)), -1);
  EXPECT_EQ(std::get<int32_t>(ValueHelper::modulo(7, -3)), 1);
}

TEST(DataTypesTest, DivideByZeroThrows) {
  EXPECT_THROW(ValueHelper::divide(1, 0), std::runtime_error);
  EXPECT_THROW(ValueHelper::divide(1.5, 0.0), std::runtime_error);
  EXPECT_THROW(ValueHelper::modulo(1, 0), std::runtime_error);
}

TEST(DataTypesTest, BitwiseOps) {
  // Bitwise results are int64/uint64 regardless of operand width.
  EXPECT_EQ(std::get<int64_t>(ValueHelper::bitAnd(12, 10)), 8);
  EXPECT_EQ(std::get<int64_t>(ValueHelper::bitOr(12, 10)), 14);
  EXPECT_EQ(std::get<int64_t>(ValueHelper::bitXor(12, 10)), 6);
  EXPECT_EQ(std::get<int64_t>(ValueHelper::lshift(1, 4)), 16);
  EXPECT_EQ(std::get<int64_t>(ValueHelper::rshift(16, 2)), 4);
  EXPECT_EQ(std::get<int64_t>(ValueHelper::bitNot(0)), -1);
  EXPECT_EQ(std::get<uint64_t>(ValueHelper::bitAnd(
                static_cast<uint8_t>(12), static_cast<uint8_t>(10))),
            8);
}

TEST(DataTypesTest, BitwiseOnFloatThrows) {
  EXPECT_THROW(ValueHelper::bitAnd(1.5, 2), std::runtime_error);
  EXPECT_THROW(ValueHelper::lshift(1.5, 1), std::runtime_error);
  EXPECT_THROW(ValueHelper::bitNot(1.5), std::runtime_error);
}

// --- Comparison -----------------------------------------------------------------

TEST(DataTypesTest, MixedNumericComparison) {
  EXPECT_TRUE(ValueHelper::lessThan(static_cast<int8_t>(-1),
                                    static_cast<uint8_t>(200)));
  EXPECT_TRUE(ValueHelper::equals(1, 1.0));
  EXPECT_TRUE(ValueHelper::greaterOrEqual(2, 2.0));
  EXPECT_TRUE(ValueHelper::lessOrEqual(static_cast<int8_t>(5), 5.5));
  EXPECT_FALSE(ValueHelper::equals(1, 1.5));
}

TEST(DataTypesTest, MixedSignedUnsignedComparison) {
  // Negative signed values must compare below unsigned ones, not wrap to
  // huge positives (regression: ternary over uint64_t/int64_t used to
  // convert the int64_t arm to uint64_t before the double cast).
  Value neg32 = static_cast<int32_t>(-1);
  Value neg64 = static_cast<int64_t>(-1);
  Value u8v = static_cast<uint8_t>(200);
  Value u64max = std::numeric_limits<uint64_t>::max();
  EXPECT_TRUE(ValueHelper::lessThan(neg32, u8v));
  EXPECT_TRUE(ValueHelper::lessThan(neg64, u8v));
  EXPECT_TRUE(ValueHelper::greaterThan(u8v, neg64));
  EXPECT_FALSE(ValueHelper::greaterThan(neg64, u8v));
  EXPECT_TRUE(ValueHelper::lessOrEqual(neg64, u8v));
  EXPECT_TRUE(ValueHelper::greaterOrEqual(u8v, neg64));
  // uint64 above INT64_MAX must not alias to -1 for equality either.
  EXPECT_FALSE(ValueHelper::equals(neg64, u64max));
  EXPECT_FALSE(ValueHelper::equals(u64max, neg64));
  EXPECT_TRUE(ValueHelper::equals(u64max, u64max));
  EXPECT_TRUE(ValueHelper::greaterThan(u64max, neg64));
}

TEST(DataTypesTest, StringComparison) {
  EXPECT_TRUE(ValueHelper::lessThan(std::string("a"), std::string("b")));
  EXPECT_TRUE(ValueHelper::equals(std::string("x"), std::string("x")));
  EXPECT_FALSE(ValueHelper::equals(std::string("x"), std::string("y")));
}

TEST(DataTypesTest, LogicalOpsTruthiness) {
  EXPECT_TRUE(ValueHelper::logicalAnd(1, std::string("x")));
  EXPECT_FALSE(ValueHelper::logicalAnd(1, 0));
  EXPECT_TRUE(ValueHelper::logicalOr(0, "y"));
  EXPECT_TRUE(ValueHelper::logicalNot(0));
  EXPECT_FALSE(ValueHelper::logicalNot(3));
  EXPECT_TRUE(ValueHelper::logicalNot(std::string("")));
}

TEST(DataTypesTest, EqualsContainers) {
  auto a = ValueHelper::createArray(
      TypeInfo(DataType::INT32), {1, 2, 3});
  auto b = ValueHelper::createArray(
      TypeInfo(DataType::INT32), {1, 2, 3});
  auto c = ValueHelper::createArray(
      TypeInfo(DataType::INT32), {1, 2, 4});
  EXPECT_TRUE(ValueHelper::equals(a, b));
  EXPECT_FALSE(ValueHelper::equals(a, c));
  // Arrays are shared: mutating through one pointer changes equality.
  a->elements[0] = 9;
  EXPECT_FALSE(ValueHelper::equals(a, b));
}

// --- Container helpers -----------------------------------------------------------

TEST(DataTypesTest, CreateArrayAndAccess) {
  ArrayPtr arr = ValueHelper::createArray(
      TypeInfo(DataType::INT32), {1, 2});
  Value v = arr;
  EXPECT_TRUE(ValueHelper::isArray(v));
  EXPECT_EQ(ValueHelper::arrayElementType(v).baseType, DataType::INT32);
  EXPECT_EQ(ValueHelper::arrayElements(v).size(), 2u);
  EXPECT_FALSE(ValueHelper::isArray(5));
}

TEST(DataTypesTest, CreateMapAndAccess) {
  MapPtr m = ValueHelper::createMap(TypeInfo(DataType::STRING),
                                    TypeInfo(DataType::INT32));
  Value v = m;
  EXPECT_TRUE(ValueHelper::isMap(v));
  EXPECT_EQ(ValueHelper::mapKeyType(v).baseType, DataType::STRING);
  EXPECT_EQ(ValueHelper::mapValueType(v).baseType, DataType::INT32);
  ValueHelper::mapEntries(v)[std::string("k")] = 7;
  EXPECT_EQ(std::get<int32_t>(ValueHelper::mapEntries(v).at("k")), 7);
  EXPECT_FALSE(ValueHelper::isMap(5));
}

TEST(DataTypesTest, ConvertElementConvertsNumbers) {
  Value v = ValueHelper::convertElement(3.7, TypeInfo(DataType::INT32));
  EXPECT_EQ(std::get<int32_t>(v), 3);
  Value w = ValueHelper::convertElement(static_cast<int8_t>(5),
                                        TypeInfo(DataType::INT64));
  EXPECT_EQ(std::get<int64_t>(w), 5);
}

TEST(DataTypesTest, CreateValueByType) {
  EXPECT_EQ(std::get<int32_t>(
                ValueHelper::createValue(DataType::INT32, static_cast<int64_t>(9))),
            9);
  EXPECT_EQ(std::get<uint8_t>(
                ValueHelper::createValue(DataType::UINT8, static_cast<uint64_t>(200))),
            200);
  EXPECT_DOUBLE_EQ(std::get<double>(
                       ValueHelper::createValue(DataType::DOUBLE, 1.25)),
                   1.25);
  EXPECT_EQ(std::get<std::string>(
                ValueHelper::createValue(DataType::STRING, std::string("hi"))),
            "hi");
  EXPECT_TRUE(std::get<bool>(
      ValueHelper::createValue(DataType::BOOL, true)));
}

TEST(DataTypesTest, StructHelpers) {
  auto s = std::make_shared<StructValue>();
  s->typeName = "Point";
  s->fieldOrder = {"x", "y"};
  s->fields["x"] = 1;
  s->fields["y"] = 2;
  Value v = s;
  EXPECT_TRUE(ValueHelper::isStruct(v));
  EXPECT_EQ(ValueHelper::structTypeName(v), "Point");
  EXPECT_FALSE(ValueHelper::isStruct(5));
}
