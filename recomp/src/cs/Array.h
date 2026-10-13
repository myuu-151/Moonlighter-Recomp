#pragma once
// C# arrays: T[] is cs::Array<T>* (a reference type with a fixed length, bounds-checked
// elements and the generic collection interfaces), T[,] is cs::Array2<T>*.

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "cs/Collections.h"
#include "cs/Object.h"
#include "cs/Vector.h"

namespace cs {

template <typename T>
class Array : public Object, public virtual IList_1<T> {
public:
    explicit Array(int32_t n) : items_(static_cast<size_t>(n)) {}

    static Array* New(int32_t n)
    {
        if (n < 0) Throw<OverflowException>();
        return gcnew<Array>(n);
    }
    static Array* Of(std::initializer_list<T> items)
    {
        Array* a = gcnew<Array>(0);
        a->items_ = Vector<T>(items);
        return a;
    }
    static Array* FromVector(const std::vector<T>& v)
    {
        Array* a = gcnew<Array>(0);
        a->items_ = Vector<T>(v);
        return a;
    }
    static Array* FromVector(const Vector<T>& v)
    {
        Array* a = gcnew<Array>(0);
        a->items_ = v;
        return a;
    }

    const char* cs_typename() const override { return "System.Array"; }
    bool cs_traced() const override { return true; }
    void cs_trace(Tracer& t) override
    {
        if constexpr (std::is_pointer<T>::value || detail::HasValueTrace<T>::value)
            for (const T& v : items_) TraceField(t, v);
    }

    int32_t get_Length() const { return static_cast<int32_t>(items_.size()); }
    int64_t get_LongLength() const { return static_cast<int64_t>(items_.size()); }
    int32_t get_Rank() const { return 1; }
    int32_t GetLength(int32_t dim) const
    {
        if (dim != 0) ThrowIndexOutOfRange();
        return get_Length();
    }

    // a[i] as an lvalue (reads, stores, ref arguments).
    T& at(int32_t i)
    {
        if (static_cast<uint32_t>(i) >= items_.size()) ThrowIndexOutOfRange();
        return items_[static_cast<size_t>(i)];
    }
    T& at(int64_t i)
    {
        if (static_cast<uint64_t>(i) >= items_.size()) ThrowIndexOutOfRange();
        return items_[static_cast<size_t>(i)];
    }
    T& at(uint32_t i) { return at(static_cast<int64_t>(i)); }
    T* data() { return items_.data(); }
    Vector<T>& vec() { return items_; }

    Object* Clone() { return FromVector(items_); }
    // Array.Clear(array, index, length), Array.Copy, CopyTo.
    void ClearRange(int32_t index, int32_t length)
    {
        if (index < 0 || length < 0 || index + length > get_Length()) ThrowIndexOutOfRange();
        for (int32_t k = index; k < index + length; k++) items_[static_cast<size_t>(k)] = T{};
    }
    void CopyTo(Array* dest, int32_t index)
    {
        nc(dest);
        if (index < 0 || index + get_Length() > dest->get_Length()) Throw<ArgumentException>();
        for (size_t k = 0; k < items_.size(); k++) dest->items_[static_cast<size_t>(index) + k] = items_[k];
    }

    auto begin() { return items_.begin(); }
    auto end() { return items_.end(); }

    // ---- the interfaces arrays implement ----
    IEnumerator_1<T>* IEnumerable_1_GetEnumerator() override;
    IEnumerator* IEnumerable_GetEnumerator() override;
    int32_t ICollection_1_get_Count() override { return get_Length(); }
    bool ICollection_1_get_IsReadOnly() override { return true; }
    void ICollection_1_Add(T) override { Throw<NotSupportedException>(); }
    void ICollection_1_Clear() override { Throw<NotSupportedException>(); }
    bool ICollection_1_Contains(T v) override { return IndexOf(v) >= 0; }
    void ICollection_1_CopyTo(Array<T>* dest, int32_t index) override { CopyTo(dest, index); }
    bool ICollection_1_Remove(T) override { Throw<NotSupportedException>(); }
    T IList_1_get_Item(int32_t i) override { return at(i); }
    void IList_1_set_Item(int32_t i, T v) override { at(i) = v; }
    int32_t IList_1_IndexOf(T v) override { return IndexOf(v); }
    void IList_1_Insert(int32_t, T) override { Throw<NotSupportedException>(); }
    void IList_1_RemoveAt(int32_t) override { Throw<NotSupportedException>(); }

    int32_t IndexOf(const T& v) const
    {
        for (size_t i = 0; i < items_.size(); i++)
            if (EqualityComparer<T>::Equals(items_[i], v)) return static_cast<int32_t>(i);
        return -1;
    }

private:
    Vector<T> items_;
};

// The enumerator arrays hand out through IEnumerable<T> (SZGenericArrayEnumerator).
template <typename T>
class ArrayEnumerator : public Object, public virtual IEnumerator_1<T>, public virtual IDisposable {
public:
    explicit ArrayEnumerator(Array<T>* a) : a_(a) {}
    const char* cs_typename() const override { return "System.SZGenericArrayEnumerator"; }
    bool cs_traced() const override { return true; }
    void cs_trace(Tracer& t) override { t.Mark(a_); }
    bool IEnumerator_MoveNext() override
    {
        if (i_ < a_->get_Length()) {
            i_++;
            return i_ < a_->get_Length();
        }
        return false;
    }
    T IEnumerator_1_get_Current() override
    {
        if (i_ < 0 || i_ >= a_->get_Length()) ThrowInvalidOperation("Enumeration has not started or has already finished.");
        return a_->at(i_);
    }
    Object* IEnumerator_get_Current() override { return BoxAny(IEnumerator_1_get_Current()); }
    void IEnumerator_Reset() override { i_ = -1; }
    void IDisposable_Dispose() override {}

private:
    Array<T>* a_;
    int32_t i_ = -1;
};

template <typename T>
IEnumerator_1<T>* Array<T>::IEnumerable_1_GetEnumerator()
{
    return gcnew<ArrayEnumerator<T>>(this);
}
template <typename T>
IEnumerator* Array<T>::IEnumerable_GetEnumerator()
{
    return gcnew<ArrayEnumerator<T>>(this);
}

// T[,]
template <typename T>
class Array2 : public Object {
public:
    Array2(int32_t n0, int32_t n1) : n0_(n0), n1_(n1), items_(static_cast<size_t>(n0) * static_cast<size_t>(n1)) {}
    static Array2* New(int32_t n0, int32_t n1)
    {
        if (n0 < 0 || n1 < 0) Throw<OverflowException>();
        return gcnew<Array2>(n0, n1);
    }
    const char* cs_typename() const override { return "System.Array"; }
    bool cs_traced() const override { return true; }
    void cs_trace(Tracer& t) override
    {
        if constexpr (std::is_pointer<T>::value || detail::HasValueTrace<T>::value)
            for (const T& v : items_) TraceField(t, v);
    }
    T& at(int32_t i, int32_t j)
    {
        if (static_cast<uint32_t>(i) >= static_cast<uint32_t>(n0_) || static_cast<uint32_t>(j) >= static_cast<uint32_t>(n1_))
            ThrowIndexOutOfRange();
        return items_[static_cast<size_t>(i) * n1_ + j];
    }
    int32_t GetLength(int32_t dim) const
    {
        if (dim == 0) return n0_;
        if (dim == 1) return n1_;
        ThrowIndexOutOfRange();
    }
    int32_t get_Length() const { return n0_ * n1_; }
    int32_t get_Rank() const { return 2; }

private:
    int32_t n0_, n1_;
    Vector<T> items_;
};

} // namespace cs
