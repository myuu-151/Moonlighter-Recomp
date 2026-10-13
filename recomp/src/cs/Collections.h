#pragma once
// The collection interfaces of mscorlib, EqualityComparer<T>.Default, Comparer<T>.Default and
// KeyValuePair, for the runtime's containers and translated code.
//
// Interfaces are abstract classes that don't derive from cs::Object (an object reaches them by
// virtual inheritance; cs::AsObject crosses back). Their members are named
// <Interface>_<member> (IEnumerator_MoveNext, IEnumerable_1_GetEnumerator): a class can
// implement several interfaces with members of the same name, as C# explicit implementations
// do, and tools/translate names interface members the same way.

#include <cstdint>
#include <type_traits>

#include "cs/Box.h"
#include "cs/Interfaces.h"
#include "cs/Object.h"
#include "cs/Prim.h"
#include "cs/String.h"

namespace cs {

template <typename T>
class Array;

// ---- EqualityComparer<T>.Default ----------------------------------------------------------------



template <typename T>
struct EqualityComparer {
    static bool Equals(const T& a, const T& b)
    {
        if constexpr (std::is_pointer<T>::value) {
            // ObjectEqualityComparer: null equals null; else the first's virtual Equals.
            Object* x = AsObject(a);
            Object* y = AsObject(b);
            if (x) return y && x->Equals(y);
            return !y;
        } else if constexpr (std::is_same<T, float>::value) {
            return FloatEquals(a, b);
        } else if constexpr (std::is_same<T, double>::value) {
            return DoubleEquals(a, b);
        } else if constexpr (std::is_arithmetic<T>::value || std::is_enum<T>::value) {
            return a == b;
        } else {
            return detail::BoxEquals(a, b);
        }
    }
    static int32_t GetHashCode(const T& v)
    {
        if constexpr (std::is_pointer<T>::value) {
            Object* o = AsObject(v);
            return o ? o->GetHashCode() : 0;
        } else {
            return detail::BoxHash(v);
        }
    }
};

// ---- Comparer<T>.Default --------------------------------------------------------------------------

template <typename T>
struct Comparer {
    static int32_t Compare(const T& a, const T& b)
    {
        if constexpr (std::is_same<T, String*>::value) {
            return String::Compare(a, b);
        } else if constexpr (std::is_pointer<T>::value) {
            // ObjectComparer / GenericComparer: null first, then IComparable<T> / IComparable.
            if (!a) return b ? -1 : 0;
            if (!b) return 1;
            if (auto* c = dynamic_cast<IComparable_1<T>*>(a)) return c->IComparable_1_CompareTo(b);
            if (auto* c = dynamic_cast<IComparable*>(a)) return c->IComparable_CompareTo(AsObject(b));
            Throw<ArgumentException>();
        } else if constexpr (std::is_arithmetic<T>::value || std::is_enum<T>::value) {
            return CompareTo(a, b);
        } else {
            return cs_compare(a, b);
        }
    }
};

// ---- KeyValuePair<K, V> ------------------------------------------------------------------------------

template <typename K, typename V>
struct KeyValuePair {
    K key{};
    V value{};
    KeyValuePair() = default;
    KeyValuePair(const K& k, const V& v) : key(k), value(v) {}
    void __ctor(const K& k, const V& v)
    {
        key = k;
        value = v;
    }
    K get_Key() const { return key; }
    V get_Value() const { return value; }
};
template <typename K, typename V>
inline void cs_trace_value(Tracer& t, const KeyValuePair<K, V>& kv)
{
    TraceField(t, kv.key);
    TraceField(t, kv.value);
}
template <typename K, typename V>
inline bool cs_box_equals(const KeyValuePair<K, V>& a, const KeyValuePair<K, V>& b)
{
    return EqualityComparer<K>::Equals(a.key, b.key) && EqualityComparer<V>::Equals(a.value, b.value);
}
template <typename K, typename V>
inline int32_t cs_box_hash(const KeyValuePair<K, V>& a)
{
    return EqualityComparer<K>::GetHashCode(a.key);
}

// The common shape of an enumerator handed out through IEnumerable<T>: wraps a value-type
// enumerator (MoveNext / get_Current / Dispose), as boxing a List<T>.Enumerator does.
template <typename T, typename E>
class BoxedEnumerator : public Object, public virtual IEnumerator_1<T> {
public:
    explicit BoxedEnumerator(const E& e) : e_(e) {}
    const char* cs_typename() const override { return "System.Collections.Generic.Enumerator"; }
    bool cs_traced() const override { return true; }
    void cs_trace(Tracer& t) override { TraceField(t, e_); }
    bool IEnumerator_MoveNext() override { return e_.MoveNext(); }
    T IEnumerator_1_get_Current() override { return e_.get_Current(); }
    Object* IEnumerator_get_Current() override { return BoxAny(e_.get_Current()); }
    void IEnumerator_Reset() override { e_.Reset(); }
    void IDisposable_Dispose() override { e_.Dispose(); }

private:
    E e_;
};

} // namespace cs
