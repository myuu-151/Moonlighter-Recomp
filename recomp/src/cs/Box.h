#pragma once
// Boxing (object o = 5;), unboxing ((int)o), and Nullable<T>.

#include <cstdint>
#include <cstring>
#include <type_traits>

#include "cs/Equality.h"
#include "cs/Object.h"
#include "cs/Prim.h"
#include "cs/String.h"

namespace cs {

// What a value type gives its box: Equals, GetHashCode, ToString and the type name. Primitives
// are handled here; other value types (structs, enums) provide, found by argument-dependent
// lookup: cs_box_equals(const S&, const S&), cs_box_hash(const S&), cs_box_tostring(const S&),
// cs_box_typename(const S*) (the generated code and the Unity layer do).
namespace detail {
template <typename T, typename = void>
struct HasBoxHash : std::false_type {};
template <typename T>
struct HasBoxHash<T, decltype(cs_box_hash(std::declval<const T&>()), void())> : std::true_type {};
template <typename T, typename = void>
struct HasBoxEquals2 : std::false_type {};
template <typename T>
struct HasBoxEquals2<T, decltype(cs_box_equals(std::declval<const T&>(), std::declval<const T&>()), void())> : std::true_type {};
// The hand port's hooks (unity::Equals / unity::Hash), by argument-dependent lookup.
template <typename U>
inline bool AdlEquals2(const U& a, const U& b)
{
    return Equals(a, b);
}
template <typename U>
inline int32_t AdlHash(const U& v)
{
    return static_cast<int32_t>(Hash(v));
}

template <typename T, typename = void>
struct HasAdlEquals2 : std::false_type {};
template <typename T>
struct HasAdlEquals2<T, decltype(Equals(std::declval<const T&>(), std::declval<const T&>()), void())> : std::true_type {};
template <typename T, typename = void>
struct HasAdlHash : std::false_type {};
template <typename T>
struct HasAdlHash<T, decltype(Hash(std::declval<const T&>()), void())> : std::true_type {};
template <typename T, typename = void>
struct HasBoxToString : std::false_type {};
template <typename T>
struct HasBoxToString<T, decltype(cs_box_tostring(std::declval<const T&>()), void())> : std::true_type {};
template <typename T, typename = void>
struct HasBoxTypeName : std::false_type {};
template <typename T>
struct HasBoxTypeName<T, decltype(cs_box_typename(static_cast<const T*>(nullptr)), void())> : std::true_type {};

template <typename T>
inline bool BoxEquals(const T& a, const T& b)
{
    if constexpr (std::is_same<T, float>::value) return FloatEquals(a, b);
    else if constexpr (std::is_same<T, double>::value) return DoubleEquals(a, b);
    else if constexpr (std::is_arithmetic<T>::value || std::is_enum<T>::value) return a == b;
    else if constexpr (HasBoxEquals2<T>::value) return cs_box_equals(a, b);
    else if constexpr (HasAdlEquals2<T>::value) return AdlEquals2(a, b);
    else if constexpr (std::is_trivially_copyable<T>::value) return std::memcmp(&a, &b, sizeof(T)) == 0; // ValueType.Equals' bitwise path
    else {
        (void)a, (void)b;
        Throw<NotImplementedException>();
    }
}
template <typename T>
inline int32_t BoxHash(const T& v)
{
    if constexpr (std::is_enum<T>::value) return HashOf(static_cast<typename std::underlying_type<T>::type>(v));
    else if constexpr (std::is_arithmetic<T>::value) return HashOf(v);
    else if constexpr (HasBoxHash<T>::value) return cs_box_hash(v);
    else if constexpr (HasAdlHash<T>::value) return AdlHash(v);
    else {
        (void)v;
        return 0;
    }
}
template <typename T>
inline String* BoxToString(const T& v)
{
    if constexpr (std::is_arithmetic<T>::value) return ::cs::ToString(v);
    else if constexpr (HasBoxToString<T>::value) return cs_box_tostring(v);
    else {
        (void)v;
        return String::FromAscii("System.ValueType");
    }
}
template <typename T>
inline const char* BoxTypeName()
{
    if constexpr (std::is_same<T, int32_t>::value) return "System.Int32";
    else if constexpr (std::is_same<T, float>::value) return "System.Single";
    else if constexpr (std::is_same<T, double>::value) return "System.Double";
    else if constexpr (std::is_same<T, bool>::value) return "System.Boolean";
    else if constexpr (std::is_same<T, int64_t>::value) return "System.Int64";
    else if constexpr (std::is_same<T, uint32_t>::value) return "System.UInt32";
    else if constexpr (std::is_same<T, uint64_t>::value) return "System.UInt64";
    else if constexpr (std::is_same<T, int16_t>::value) return "System.Int16";
    else if constexpr (std::is_same<T, uint16_t>::value) return "System.UInt16";
    else if constexpr (std::is_same<T, uint8_t>::value) return "System.Byte";
    else if constexpr (std::is_same<T, int8_t>::value) return "System.SByte";
    else if constexpr (std::is_same<T, char16_t>::value) return "System.Char";
    else if constexpr (HasBoxTypeName<T>::value) return cs_box_typename(static_cast<const T*>(nullptr));
    else return "System.ValueType";
}
} // namespace detail

template <typename T>
class Box : public Object {
public:
    explicit Box(const T& v) : value(v) {}
    const char* cs_typename() const override { return detail::BoxTypeName<T>(); }
    bool cs_traced() const override { return true; }
    void cs_trace(Tracer& t) override { TraceField(t, value); }
    bool Equals(Object* other) override
    {
        auto* o = DynCast<Box<T>>(other);
        return o && detail::BoxEquals(value, o->value);
    }
    int32_t GetHashCode() override { return detail::BoxHash(value); }
    String* ToString() override { return detail::BoxToString(value); }
    T value;
};

template <typename T>
struct Nullable;

// (object)v
template <typename T>
inline Object* BoxAny(const T& v)
{
    if constexpr (std::is_pointer<T>::value) return AsObject(v);
    else return gcnew<Box<T>>(v);
}

// (T)o for a value type T: NullReferenceException on null, InvalidCastException on another type.
template <typename T>
inline T Unbox(Object* o)
{
    if (!o) ThrowNullReference();
    if (auto* b = DynCast<Box<T>>(o)) return b->value;
    // An enum's box unboxes to its underlying type and back (the CLI allows both).
    if constexpr (std::is_enum<T>::value) {
        using U = typename std::underlying_type<T>::type;
        if (auto* b = DynCast<Box<U>>(o)) return static_cast<T>(b->value);
    }
    ThrowInvalidCast();
}

// o is T, for a value type T.
template <typename T>
inline bool IsBoxed(Object* o)
{
    return o && DynCast<Box<T>>(o) != nullptr;
}

// System.Nullable<T>
template <typename T>
struct Nullable {
    bool hasValue = false;
    T value{};

    Nullable() = default;
    Nullable(const T& v) : hasValue(true), value(v) {}
    static Nullable Null() { return Nullable(); }
    bool get_HasValue() const { return hasValue; }
    T get_Value() const
    {
        if (!hasValue) ThrowInvalidOperation("Nullable object must have a value.");
        return value;
    }
    T GetValueOrDefault() const { return value; }
    T GetValueOrDefault(const T& d) const { return hasValue ? value : d; }
};

template <typename T>
inline Object* BoxAny(const Nullable<T>& v)
{
    return v.hasValue ? BoxAny(v.value) : nullptr;
}
template <typename T>
inline void cs_trace_value(Tracer& t, const Nullable<T>& v)
{
    TraceField(t, v.value);
}

} // namespace cs
