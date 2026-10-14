#pragma once
// System.Linq.Enumerable, as System.Core's reference source has it: the eager operators read
// the source through ICollection<T> when it is one (Count, ToList, ToArray), else by
// enumerating; the lazy ones (Where, Select) return iterator objects that run only when
// enumerated, as .NET's do. Only what translated code calls so far. (T is the C# type; the
// interfaces' values come back as cs::Erase<T>, cast down: cs/Object.h.)

#include <cstdint>

#include "cs/Array.h"
#include "cs/Collections.h"
#include "cs/Delegate.h"
#include "cs/List.h"
#include "cs/Object.h"

namespace cs {

namespace Enumerable {

template <typename T>
inline int32_t Count(IEnumerable_1<T>* source)
{
    if (!source) ThrowArgumentNull("source");
    if (auto* c = DynCast<ICollection_1<T>>(source)) return c->ICollection_1_get_Count();
    int32_t n = 0;
    IEnumerator_1<T>* e = source->IEnumerable_1_GetEnumerator();
    auto fin = MakeFinally([&] { e->IDisposable_Dispose(); });
    while (e->IEnumerator_MoveNext()) {
        if (n == INT32_MAX) Throw<OverflowException>();
        n++;
    }
    return n;
}

template <typename T>
inline int32_t Count(IEnumerable_1<T>* source, Delegate<bool(T)>* predicate)
{
    if (!source) ThrowArgumentNull("source");
    if (!predicate) ThrowArgumentNull("predicate");
    int32_t n = 0;
    IEnumerator_1<T>* e = source->IEnumerable_1_GetEnumerator();
    auto fin = MakeFinally([&] { e->IDisposable_Dispose(); });
    while (e->IEnumerator_MoveNext())
        if (predicate->Invoke(Down<T>(e->IEnumerator_1_get_Current()))) n++;
    return n;
}

template <typename T>
inline bool Any(IEnumerable_1<T>* source)
{
    if (!source) ThrowArgumentNull("source");
    IEnumerator_1<T>* e = source->IEnumerable_1_GetEnumerator();
    auto fin = MakeFinally([&] { e->IDisposable_Dispose(); });
    return e->IEnumerator_MoveNext();
}

template <typename T>
inline bool Any(IEnumerable_1<T>* source, Delegate<bool(T)>* predicate)
{
    if (!source) ThrowArgumentNull("source");
    if (!predicate) ThrowArgumentNull("predicate");
    IEnumerator_1<T>* e = source->IEnumerable_1_GetEnumerator();
    auto fin = MakeFinally([&] { e->IDisposable_Dispose(); });
    while (e->IEnumerator_MoveNext())
        if (predicate->Invoke(Down<T>(e->IEnumerator_1_get_Current()))) return true;
    return false;
}

template <typename T>
inline List<T>* ToList(IEnumerable_1<T>* source)
{
    if (!source) ThrowArgumentNull("source");
    List<T>* l = gcnew<List<T>>();
    l->__ctor(source);
    return l;
}

template <typename T>
inline Array<T>* ToArray(IEnumerable_1<T>* source)
{
    if (!source) ThrowArgumentNull("source");
    // Buffer<T>: a collection copies at its count, else the items grow 4, 8, 16... and trim.
    if (auto* c = DynCast<ICollection_1<T>>(source)) {
        Array<T>* a = Array<T>::New(c->ICollection_1_get_Count());
        if (a->get_Length() > 0) c->ICollection_1_CopyTo(a, 0);
        return a;
    }
    std::vector<Erase<T>> items;
    IEnumerator_1<T>* e = source->IEnumerable_1_GetEnumerator();
    auto fin = MakeFinally([&] { e->IDisposable_Dispose(); });
    while (e->IEnumerator_MoveNext()) items.push_back(e->IEnumerator_1_get_Current());
    return Array<T>::FromVector(std::move(items));
}

template <typename T>
inline bool Contains(IEnumerable_1<T>* source, T value)
{
    if (auto* c = DynCast<ICollection_1<T>>(source)) return c->ICollection_1_Contains(value);
    if (!source) ThrowArgumentNull("source");
    IEnumerator_1<T>* e = source->IEnumerable_1_GetEnumerator();
    auto fin = MakeFinally([&] { e->IDisposable_Dispose(); });
    while (e->IEnumerator_MoveNext())
        if (EqualityComparer<T>::Equals(Down<T>(e->IEnumerator_1_get_Current()), value)) return true;
    return false;
}

template <typename T>
inline T First(IEnumerable_1<T>* source)
{
    if (!source) ThrowArgumentNull("source");
    if (auto* l = DynCast<IList_1<T>>(source)) {
        if (l->ICollection_1_get_Count() > 0) return Down<T>(l->IList_1_get_Item(0));
    } else {
        IEnumerator_1<T>* e = source->IEnumerable_1_GetEnumerator();
        auto fin = MakeFinally([&] { e->IDisposable_Dispose(); });
        if (e->IEnumerator_MoveNext()) return Down<T>(e->IEnumerator_1_get_Current());
    }
    ThrowInvalidOperation("Sequence contains no elements");
}

template <typename T>
inline T FirstOrDefault(IEnumerable_1<T>* source)
{
    if (!source) ThrowArgumentNull("source");
    if (auto* l = DynCast<IList_1<T>>(source)) {
        if (l->ICollection_1_get_Count() > 0) return Down<T>(l->IList_1_get_Item(0));
    } else {
        IEnumerator_1<T>* e = source->IEnumerable_1_GetEnumerator();
        auto fin = MakeFinally([&] { e->IDisposable_Dispose(); });
        if (e->IEnumerator_MoveNext()) return Down<T>(e->IEnumerator_1_get_Current());
    }
    return T{};
}

template <typename T>
inline T FirstOrDefault(IEnumerable_1<T>* source, Delegate<bool(T)>* predicate)
{
    if (!source) ThrowArgumentNull("source");
    if (!predicate) ThrowArgumentNull("predicate");
    IEnumerator_1<T>* e = source->IEnumerable_1_GetEnumerator();
    auto fin = MakeFinally([&] { e->IDisposable_Dispose(); });
    while (e->IEnumerator_MoveNext()) {
        T v = Down<T>(e->IEnumerator_1_get_Current());
        if (predicate->Invoke(v)) return v;
    }
    return T{};
}

} // namespace Enumerable

} // namespace cs
