// Reflection-based auto-binding (prototype).
//
// Goal: expose a C++ struct to scripts without hand-written marshaling —
// a bound type converts to/from a script StructValue, so it can flow
// through registerExternalFunction arguments/returns and callProcedure
// like any scalar.
//
// Today's C++17 implementation is descriptor-driven: specialize
// StructCodecOf<T> with a Member list (name + pointer-to-member):
//
//   struct Point { int32_t x; int32_t y; };
//
//   template <> struct Script::StructCodecOf<Point> {
//     static constexpr const char *name = "Point";
//     static auto members() {
//       return std::tuple{Script::Member{"x", &Point::x},
//                         Script::Member{"y", &Point::y}};
//     }
//   };
//
//   manager.registerExternalFunction("norm",
//       [](const Point &p) { return std::hypot(p.x, p.y); });
//   Point p = manager.callProcedure<Point>("makePoint", 3, 4);
//
// With P2996 static reflection (std::meta / ^^, currently experimental in
// the Bloomberg Clang fork) the member list can be generated instead:
//
//   template <typename T> /* requires reflection-capable compiler */
//   auto reflectedMembers() {
//     // members_of(^^T) -> (Member{"f", &T::f}...) via splice
//   }
//
// The codec boundary stays identical — StructCodecOf<T> is filled in by
// the reflection walk — so user code never changes. This header therefore
// isolates exactly the part C++26 reflection will replace, leaving the
// conversion machinery (the part reflection can't provide) stable.
#pragma once

#include "DataTypes.h"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

// Defined when a compiler with working static reflection is detected —
// the descriptor codec below is replaced by members_of(^^T) codegen.
#if defined(__cpp_reflection) && __cpp_reflection >= 202202L
#define CXXSCRIPT_HAS_STATIC_REFLECTION 1
#else
#define CXXSCRIPT_HAS_STATIC_REFLECTION 0
#endif

namespace Script {

// One bound field: script-visible name + pointer-to-member.
template <typename T, typename M> struct Member {
  const char *name;
  M T::*ptr;
  using field_type = M;
};

// Deduction guide: Member{"x", &Point::x} -> Member<Point, int32_t>.
template <typename T, typename M>
Member(const char *, M T::*) -> Member<T, M>;

// Field-type constraint for bound members: scalars, strings, arrays and
// maps map to Value alternatives directly; nested bound structs recurse.
template <typename T>
struct IsBindableField
    : std::bool_constant<
          std::is_same<T, std::string>::value ||
          std::is_same<T, ArrayPtr>::value || std::is_same<T, MapPtr>::value ||
          std::is_same<T, bool>::value || std::is_same<T, char>::value ||
          std::is_same<T, float>::value || std::is_same<T, double>::value ||
          (std::is_integral<T>::value && !std::is_same<T, bool>::value)> {};

// --- Codec -------------------------------------------------------------------

// Specialize per bound type: static constexpr const char *name and a
// members() function returning a std::tuple of Member<T, FieldT>.
template <typename T> struct StructCodecOf; // primary: unbound

// True when T has a StructCodecOf specialization.
template <typename T, typename = void>
struct HasStructCodec : std::false_type {};
template <typename T>
struct HasStructCodec<T, std::void_t<decltype(StructCodecOf<T>::name),
                                     decltype(StructCodecOf<T>::members())>>
    : std::true_type {};

namespace detail {
// Scalar conversion entry points (defined in ScriptManager.h). Forward
// declared so the codec can convert member values without include cycles;
// signatures must match the definitions exactly.
template <typename T> inline Value toValue(const T &v);
template <typename T> inline std::decay_t<T> fromValue(const Value &v);
} // namespace detail

template <typename T> Value structToValue(const T &obj);
template <typename T> T structFromValue(const Value &v);

namespace detail {

// Field conversion: bound structs recurse, scalars use the same rules as
// external-function arguments.
template <typename M> inline Value boundFieldToValue(const M &v) {
  if constexpr (HasStructCodec<M>::value) {
    return structToValue(v);
  } else {
    static_assert(IsBindableField<M>::value,
                  "bound member type is not script-representable");
    return toValue(v);
  }
}
template <typename M> inline M boundFieldFromValue(const Value &v) {
  if constexpr (HasStructCodec<M>::value) {
    return structFromValue<M>(v);
  } else {
    static_assert(IsBindableField<M>::value,
                  "bound member type is not script-representable");
    return fromValue<M>(v);
  }
}

} // namespace detail

// obj -> StructPtr Value. Member order follows the descriptor order.
template <typename T> Value structToValue(const T &obj) {
  static_assert(HasStructCodec<T>::value,
                "StructCodecOf<T> is not specialized for this type");
  auto sv = std::make_shared<StructValue>();
  sv->typeName = StructCodecOf<T>::name;
  std::apply(
      [&](const auto &...m) {
        (static_cast<void>(
             sv->fields.emplace(m.name,
                                detail::boundFieldToValue(obj.*(m.ptr))),
             sv->fieldOrder.push_back(m.name)),
         ...);
      },
      StructCodecOf<T>::members());
  return sv;
}

// StructPtr Value -> obj. Missing fields keep their default-constructed
// value; wrong-typed fields throw std::runtime_error.
template <typename T> T structFromValue(const Value &v) {
  static_assert(HasStructCodec<T>::value,
                "StructCodecOf<T> is not specialized for this type");
  const auto *sv = std::get_if<StructPtr>(&v);
  if (!sv || !*sv) {
    throw std::runtime_error(std::string("expected struct value for '") +
                             StructCodecOf<T>::name + "'");
  }
  T obj{};
  std::apply(
      [&](const auto &...m) {
        (static_cast<void>([&] {
           auto it = (*sv)->fields.find(m.name);
           if (it != (*sv)->fields.end()) {
             obj.*(m.ptr) =
                 detail::boundFieldFromValue<typename std::decay_t<
                     decltype(m)>::field_type>(it->second);
           }
         }()),
         ...);
      },
      StructCodecOf<T>::members());
  return obj;
}

} // namespace Script
