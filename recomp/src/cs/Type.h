#pragma once
// System.Type, as far as game code uses it: typeof(T) and obj.GetType() give one canonical
// object per type (so == and Equals compare types), with its FullName / Name / ToString.
// No reflection over members.

#include <cstring>
#include <type_traits>

#include "cs/Box.h"
#include "cs/Object.h"
#include "cs/String.h"

namespace cs {

class Type : public Object {
public:
    explicit Type(const char* fullName) : fullName_(fullName) {}
    const char* cs_typename() const override { return "System.RuntimeType"; }
    bool cs_traced() const override { return true; }
    String* ToString() override { return get_FullName(); }
    String* get_FullName() { return String::FromAscii(fullName_); }
    String* get_Name()
    {
        const char* n = fullName_;
        for (const char* p = fullName_; *p; p++)
            if (*p == '.' || *p == '+') n = p + 1;
        return String::FromAscii(n);
    }
    const char* FullNameC() const { return fullName_; }
    // The canonical Type object of a type name (permanent).
    static Type* ForName(const char* fullName);
    static bool op_Equality(Type* a, Type* b) { return a == b; }
    static bool op_Inequality(Type* a, Type* b) { return a != b; }

private:
    const char* fullName_;
};

// obj.GetType()
inline Type* GetTypeOf(Object* o) { return Type::ForName(nc(o)->cs_typename()); }

namespace detail {
template <typename T, typename = void>
struct HasStaticTypeName : std::false_type {};
template <typename T>
struct HasStaticTypeName<T, decltype(T::cs_static_typename(), void())> : std::true_type {};
} // namespace detail

// typeof(T), T as translated code names it (a class pointer, or a value type).
template <typename T>
inline Type* TypeOf()
{
    if constexpr (std::is_pointer<T>::value) {
        using C = typename std::remove_pointer<T>::type;
        if constexpr (detail::HasStaticTypeName<C>::value) return Type::ForName(C::cs_static_typename());
        else return Type::ForName("System.Object");
    } else {
        return Type::ForName(detail::BoxTypeName<T>());
    }
}

// `x is T` for a type parameter T (a class pointer: a type test; a value type: a box of it).
template <typename T>
inline bool IsT(Object* o)
{
    if constexpr (std::is_pointer<T>::value) return Is<typename std::remove_pointer<T>::type>(o);
    else return IsBoxed<T>(o);
}

} // namespace cs
