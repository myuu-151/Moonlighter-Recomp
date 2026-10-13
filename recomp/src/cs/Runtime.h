#pragma once
// Everything translated game code (recomp/gen/cpp, tools/translate) includes: the .NET runtime
// pieces and the helpers the translator's output calls.

#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "cs/Array.h"
#include "cs/Box.h"
#include "cs/Collections.h"
#include "cs/Delegate.h"
#include "cs/Dictionary.h"
#include "cs/HashSet.h"
#include "cs/Linq.h"
#include "cs/List.h"
#include "cs/Object.h"
#include "cs/Prim.h"
#include "cs/String.h"
#include "cs/Text.h"
#include "cs/Type.h"

namespace cs {

// new C(args): a fresh object (fields zeroed by their initializers), then the C# constructor.
template <typename T, typename... A>
inline T* New(A&&... args)
{
    T* o = gcnew<T>();
    o->__ctor(std::forward<A>(args)...);
    return o;
}

// new S(args) for a value type.
template <typename T, typename... A>
inline T NewS(A&&... args)
{
    T v{};
    v.__ctor(std::forward<A>(args)...);
    return v;
}

// A receiver of a type parameter: a class pointer (null-checked) or a value.
template <typename T>
inline T& Deref(T* p)
{
    return *nc(p);
}
template <typename T, typename std::enable_if<!std::is_pointer<T>::value, int>::type = 0>
inline T& Deref(T& v)
{
    return v;
}

// x == null for an unconstrained type parameter.
template <typename T>
inline bool IsNullT(const T& v)
{
    if constexpr (std::is_pointer<T>::value) return AsObject(v) == nullptr;
    else return false;
}

// (T)o for a type parameter T: a reference cast or an unboxing.
template <typename T>
inline T UnboxAny(Object* o)
{
    if constexpr (std::is_pointer<T>::value) return Cast<typename std::remove_pointer<T>::type>(o);
    else return Unbox<T>(o);
}

// System.Object's virtuals on any receiver (a reference, or a value as its box would answer).
template <typename T>
inline String* ObjectCall_ToString(T* o)
{
    return nc(AsObject(o))->ToString();
}
template <typename T, typename std::enable_if<!std::is_pointer<T>::value, int>::type = 0>
inline String* ObjectCall_ToString(const T& v)
{
    return detail::BoxToString(v);
}
template <typename T>
inline int32_t ObjectCall_GetHashCode(T* o)
{
    return nc(AsObject(o))->GetHashCode();
}
template <typename T, typename std::enable_if<!std::is_pointer<T>::value, int>::type = 0>
inline int32_t ObjectCall_GetHashCode(const T& v)
{
    return detail::BoxHash(v);
}
template <typename T>
inline bool ObjectCall_Equals(T* o, Object* other)
{
    return nc(AsObject(o))->Equals(other);
}
template <typename T, typename std::enable_if<!std::is_pointer<T>::value, int>::type = 0>
inline bool ObjectCall_Equals(const T& v, Object* other)
{
    auto* b = dynamic_cast<Box<T>*>(other);
    return b && detail::BoxEquals(v, b->value);
}
// object.Equals(a, b)
inline bool ObjectEquals(Object* a, Object* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    return a->Equals(b);
}

// A method the translator could not translate (an unsupported construct, a missing binding):
// reaching it at run time is an error the report (recomp/gen/cpp/report.tsv) explains.
[[noreturn]] void NotTranslated(const char* method);

} // namespace cs

// A missing binding inside a translated body (the method is reported and not compiled in).
#define CS_GAP(what) (::cs::NotTranslated(what), 0)
