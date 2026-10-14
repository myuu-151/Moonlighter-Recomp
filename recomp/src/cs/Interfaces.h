#pragma once
// The collection interfaces of mscorlib (cs/Collections.h explains their naming: members are
// <Interface>_<member>). Apart, so String can implement IEnumerable<char>.

#include <cstdint>

#include "cs/Object.h"

namespace cs {

struct IDisposable {
    virtual ~IDisposable() = default;
    virtual void IDisposable_Dispose() = 0;
};

struct IEnumerator {
    virtual ~IEnumerator() = default;
    virtual bool IEnumerator_MoveNext() = 0;
    virtual Object* IEnumerator_get_Current() = 0;
    virtual void IEnumerator_Reset() = 0;
};

struct IEnumerable {
    virtual ~IEnumerable() = default;
    virtual IEnumerator* IEnumerable_GetEnumerator() = 0;
};

namespace shared {

template <typename T>
struct IEnumerator_1 : virtual IEnumerator, virtual IDisposable {
    virtual T IEnumerator_1_get_Current() = 0;
};

template <typename T>
struct IEnumerable_1 : virtual IEnumerable {
    virtual IEnumerator_1<T>* IEnumerable_1_GetEnumerator() = 0;
};

template <typename T>
struct ICollection_1 : virtual IEnumerable_1<T> {
    virtual int32_t ICollection_1_get_Count() = 0;
    virtual bool ICollection_1_get_IsReadOnly() = 0;
    virtual void ICollection_1_Add(T item) = 0;
    virtual void ICollection_1_Clear() = 0;
    virtual bool ICollection_1_Contains(T item) = 0;
    virtual void ICollection_1_CopyTo(Array<T>* array, int32_t index) = 0;
    virtual bool ICollection_1_Remove(T item) = 0;
};

template <typename T>
struct IList_1 : virtual ICollection_1<T> {
    virtual T IList_1_get_Item(int32_t index) = 0;
    virtual void IList_1_set_Item(int32_t index, T value) = 0;
    virtual int32_t IList_1_IndexOf(T item) = 0;
    virtual void IList_1_Insert(int32_t index, T item) = 0;
    virtual void IList_1_RemoveAt(int32_t index) = 0;
};

template <typename T>
struct IComparer_1 {
    virtual ~IComparer_1() = default;
    virtual int32_t IComparer_1_Compare(T x, T y) = 0;
};

template <typename T>
struct IEqualityComparer_1 {
    virtual ~IEqualityComparer_1() = default;
    virtual bool IEqualityComparer_1_Equals(T x, T y) = 0;
    virtual int32_t IEqualityComparer_1_GetHashCode(T obj) = 0;
};

} // namespace shared

struct IComparable {
    virtual ~IComparable() = default;
    virtual int32_t IComparable_CompareTo(Object* obj) = 0;
};

namespace shared {

template <typename T>
struct IComparable_1 {
    virtual ~IComparable_1() = default;
    virtual int32_t IComparable_1_CompareTo(T other) = 0;
};

template <typename T>
struct IEquatable_1 {
    virtual ~IEquatable_1() = default;
    virtual bool IEquatable_1_Equals(T other) = 0;
};

} // namespace shared

} // namespace cs
