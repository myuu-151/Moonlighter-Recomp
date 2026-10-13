#pragma once
// System.Collections.Generic.List<T> as mscorlib (the reference source Mono ships) has it:
//  - capacity 0 -> 4 -> 8 -> ... (EnsureCapacity), copies from a collection sized to its count;
//  - _version bumps on every change, and its Enumerator throws InvalidOperationException when
//    the list changed under a foreach;
//  - Sort is ArraySortHelper's introsort, whose depth limit comes from the backing array's
//    length (the capacity), so the capacity is tracked exactly;
//  - IndexOf / Contains / Remove compare with EqualityComparer<T>.Default.
// List is a reference type in C#: always allocate it with cs::gcnew and pass List<T>*.
//
// Two spellings of the API: the C#-shaped one translated code calls (get_Count, get_Item,
// set_Item, Add, ...) and the hand port's (Count(), operator[], Set, begin/end, CopyOf).

#include <algorithm>
#include <vector>

#include "cs/Array.h"
#include "cs/Collections.h"
#include "cs/Delegate.h"
#include "cs/Object.h"
#include "cs/Vector.h"

namespace cs {

template <typename T>
class List : public Object, public virtual IList_1<T> {
public:
    List() = default;

    const char* cs_typename() const override { return "System.Collections.Generic.List`1"; }
    bool cs_traced() const override { return true; }
    void cs_trace(Tracer& t) override
    {
        if constexpr (std::is_pointer<T>::value || detail::HasValueTrace<T>::value)
            for (const T& v : items_) TraceField(t, v);
    }

    // ---- constructors (translated `new List<T>(...)` calls __ctor on a fresh object) ----
    void __ctor() {}
    void __ctor(int32_t capacity)
    {
        if (capacity < 0) ThrowArgumentOutOfRange();
        capacity_ = capacity;
        items_.reserve(static_cast<size_t>(capacity));
    }
    void __ctor(IEnumerable_1<T>* collection)
    {
        if (!collection) ThrowArgumentNull("collection");
        if (auto* l = dynamic_cast<List<T>*>(collection)) {
            items_ = l->items_;
            capacity_ = static_cast<int32_t>(items_.size());
        } else if (auto* c = dynamic_cast<ICollection_1<T>*>(collection)) {
            int32_t n = c->ICollection_1_get_Count();
            if (n > 0) {
                Array<T>* tmp = Array<T>::New(n);
                c->ICollection_1_CopyTo(tmp, 0);
                items_ = tmp->vec();
            }
            capacity_ = n;
        } else {
            IEnumerator_1<T>* e = collection->IEnumerable_1_GetEnumerator();
            auto fin = MakeFinally([&] { e->IDisposable_Dispose(); });
            while (e->IEnumerator_MoveNext()) Add(e->IEnumerator_1_get_Current());
        }
    }

    // new List<T>(IEnumerable<T> collection) for a List source (the hand port's spelling).
    static List* CopyOf(const List* src)
    {
        List* l = gcnew<List>();
        l->items_ = NullCheck(src)->items_;
        l->capacity_ = static_cast<int32_t>(src->items_.size());
        return l;
    }

    // ---- size ----
    int32_t get_Count() const { return static_cast<int32_t>(items_.size()); }
    int32_t Count() const { return get_Count(); }
    int32_t get_Capacity() const { return capacity_; }
    int32_t Capacity() const { return capacity_; }
    void set_Capacity(int32_t value)
    {
        if (value < get_Count()) ThrowArgumentOutOfRange();
        capacity_ = value;
        items_.reserve(static_cast<size_t>(value));
    }

    // ---- elements ----
    T get_Item(int32_t i) const
    {
        if (static_cast<uint32_t>(i) >= items_.size()) ThrowArgumentOutOfRange();
        return items_[static_cast<size_t>(i)];
    }
    void set_Item(int32_t i, T v)
    {
        if (static_cast<uint32_t>(i) >= items_.size()) ThrowArgumentOutOfRange();
        items_[static_cast<size_t>(i)] = v;
        version_++;
    }
    const T& operator[](int32_t i) const
    {
        if (static_cast<uint32_t>(i) >= items_.size()) ThrowArgumentOutOfRange();
        return items_[static_cast<size_t>(i)];
    }
    void Set(int32_t i, const T& v) { set_Item(i, v); }

    void Add(T v)
    {
        if (get_Count() == capacity_) EnsureCapacity(get_Count() + 1);
        items_.push_back(v);
        version_++;
    }

    void AddRange(IEnumerable_1<T>* collection) { InsertRange(get_Count(), collection); }
    void AddRange(const List* other) { InsertRange(get_Count(), const_cast<List*>(other)); }
    void InsertRange(int32_t index, IEnumerable_1<T>* collection)
    {
        if (!collection) ThrowArgumentNull("collection");
        if (static_cast<uint32_t>(index) > static_cast<uint32_t>(get_Count())) ThrowArgumentOutOfRange();
        if (auto* c = dynamic_cast<ICollection_1<T>*>(collection)) {
            int32_t n = c->ICollection_1_get_Count();
            if (n > 0) {
                EnsureCapacity(get_Count() + n);
                Vector<T> copy;
                if (auto* l = dynamic_cast<List<T>*>(collection)) copy = l->items_;
                else {
                    Array<T>* tmp = Array<T>::New(n);
                    c->ICollection_1_CopyTo(tmp, 0);
                    copy = tmp->vec();
                }
                items_.insert_at(static_cast<size_t>(index), copy);
            }
        } else {
            IEnumerator_1<T>* e = collection->IEnumerable_1_GetEnumerator();
            auto fin = MakeFinally([&] { e->IDisposable_Dispose(); });
            while (e->IEnumerator_MoveNext()) Insert(index++, e->IEnumerator_1_get_Current());
        }
        version_++;
    }

    void Insert(int32_t index, T v)
    {
        if (static_cast<uint32_t>(index) > static_cast<uint32_t>(get_Count())) ThrowArgumentOutOfRange();
        if (get_Count() == capacity_) EnsureCapacity(get_Count() + 1);
        items_.insert_at(static_cast<size_t>(index), v);
        version_++;
    }

    int32_t IndexOf(const T& v) const
    {
        for (int32_t i = 0; i < get_Count(); i++)
            if (EqualityComparer<T>::Equals(items_[static_cast<size_t>(i)], v)) return i;
        return -1;
    }
    int32_t IndexOf(T v, int32_t index) const
    {
        if (static_cast<uint32_t>(index) > static_cast<uint32_t>(get_Count())) ThrowArgumentOutOfRange();
        for (int32_t i = index; i < get_Count(); i++)
            if (EqualityComparer<T>::Equals(items_[static_cast<size_t>(i)], v)) return i;
        return -1;
    }
    int32_t LastIndexOf(T v) const
    {
        for (int32_t i = get_Count() - 1; i >= 0; i--)
            if (EqualityComparer<T>::Equals(items_[static_cast<size_t>(i)], v)) return i;
        return -1;
    }
    bool Contains(const T& v) const
    {
        if constexpr (std::is_pointer<T>::value) {
            if (!AsObject(v)) {
                for (const T& x : items_)
                    if (!AsObject(x)) return true;
                return false;
            }
        }
        for (const T& x : items_)
            if (EqualityComparer<T>::Equals(x, v)) return true;
        return false;
    }

    bool Remove(T v)
    {
        int32_t i = IndexOf(v);
        if (i >= 0) {
            RemoveAt(i);
            return true;
        }
        return false;
    }
    void RemoveAt(int32_t i)
    {
        if (static_cast<uint32_t>(i) >= items_.size()) ThrowArgumentOutOfRange();
        items_.erase_at(static_cast<size_t>(i));
        version_++;
    }
    void RemoveRange(int32_t index, int32_t count)
    {
        if (index < 0 || count < 0) ThrowArgumentOutOfRange();
        if (get_Count() - index < count) ThrowArgument("Offset and length were out of bounds for the array or count is greater than the number of elements from index to the end of the source collection.");
        if (count > 0) {
            items_.erase_at(static_cast<size_t>(index), static_cast<size_t>(count));
            version_++;
        }
    }
    void Clear()
    {
        items_.clear();
        version_++;
    }

    // ---- with delegates (translated code passes cs::Delegate; the hand port, any callable) ----
    template <typename Pred>
    static bool Call(Pred& p, const T& v)
    {
        if constexpr (std::is_pointer<Pred>::value) return nc(p)->Invoke(v);
        else return p(v);
    }

    template <typename Pred>
    int32_t FindIndex(Pred pred) const { return FindIndex(0, get_Count(), pred); }
    template <typename Pred>
    int32_t FindIndex(int32_t start, Pred pred) const { return FindIndex(start, get_Count() - start, pred); }
    template <typename Pred>
    int32_t FindIndex(int32_t start, int32_t count, Pred pred) const
    {
        if (static_cast<uint32_t>(start) > static_cast<uint32_t>(get_Count())) ThrowArgumentOutOfRange();
        if (count < 0 || start > get_Count() - count) ThrowArgumentOutOfRange();
        if constexpr (std::is_pointer<Pred>::value)
            if (!pred) ThrowArgumentNull("match");
        for (int32_t i = start; i < start + count; i++)
            if (Call(pred, items_[static_cast<size_t>(i)])) return i;
        return -1;
    }
    template <typename Pred>
    int32_t FindLastIndex(Pred pred) const
    {
        for (int32_t i = get_Count() - 1; i >= 0; i--)
            if (Call(pred, items_[static_cast<size_t>(i)])) return i;
        return -1;
    }
    template <typename Pred>
    T Find(Pred pred) const
    {
        if constexpr (std::is_pointer<Pred>::value)
            if (!pred) ThrowArgumentNull("match");
        for (int32_t i = 0; i < get_Count(); i++)
            if (Call(pred, items_[static_cast<size_t>(i)])) return items_[static_cast<size_t>(i)];
        return T{};
    }
    template <typename Pred>
    T FindLast(Pred pred) const
    {
        for (int32_t i = get_Count() - 1; i >= 0; i--)
            if (Call(pred, items_[static_cast<size_t>(i)])) return items_[static_cast<size_t>(i)];
        return T{};
    }
    template <typename Pred>
    List* FindAll(Pred pred) const
    {
        List* l = gcnew<List>();
        for (int32_t i = 0; i < get_Count(); i++)
            if (Call(pred, items_[static_cast<size_t>(i)])) l->Add(items_[static_cast<size_t>(i)]);
        return l;
    }
    template <typename Pred>
    bool Exists(Pred pred) const { return FindIndex(pred) != -1; }
    template <typename Pred>
    bool TrueForAll(Pred pred) const
    {
        if constexpr (std::is_pointer<Pred>::value)
            if (!pred) ThrowArgumentNull("match");
        for (int32_t i = 0; i < get_Count(); i++)
            if (!Call(pred, items_[static_cast<size_t>(i)])) return false;
        return true;
    }
    template <typename Pred>
    int32_t RemoveAll(Pred pred)
    {
        if constexpr (std::is_pointer<Pred>::value)
            if (!pred) ThrowArgumentNull("match");
        int32_t size = get_Count();
        int32_t freeIndex = 0;
        while (freeIndex < size && !Call(pred, items_[static_cast<size_t>(freeIndex)])) freeIndex++;
        if (freeIndex >= size) return 0;
        int32_t current = freeIndex + 1;
        while (current < size) {
            while (current < size && Call(pred, items_[static_cast<size_t>(current)])) current++;
            if (current < size) items_[static_cast<size_t>(freeIndex++)] = items_[static_cast<size_t>(current++)];
        }
        items_.resize(static_cast<size_t>(freeIndex));
        version_++;
        return size - freeIndex;
    }
    // ForEach(Action<T>): stops and throws if the action changes the list.
    void ForEach(Delegate<void(T)>* action)
    {
        if (!action) ThrowArgumentNull("action");
        int32_t version = version_;
        for (int32_t i = 0; i < get_Count(); i++) {
            if (version != version_) break;
            action->Invoke(items_[static_cast<size_t>(i)]);
        }
        if (version != version_) ThrowInvalidOperation("Collection was modified; enumeration operation may not execute.");
    }
    template <typename U>
    List<U>* ConvertAll(Delegate<U(T)>* converter)
    {
        if (!converter) ThrowArgumentNull("converter");
        List<U>* l = gcnew<List<U>>();
        l->__ctor(get_Count());
        for (int32_t i = 0; i < get_Count(); i++) l->Add(converter->Invoke(items_[static_cast<size_t>(i)]));
        return l;
    }

    Array<T>* ToArray() const { return Array<T>::FromVector(items_); }
    List* GetRange(int32_t index, int32_t count)
    {
        if (index < 0 || count < 0) ThrowArgumentOutOfRange();
        if (get_Count() - index < count) ThrowArgument("Offset and length were out of bounds.");
        List* l = gcnew<List>();
        l->__ctor(count);
        for (int32_t k = index; k < index + count; k++) l->items_.push_back(items_[static_cast<size_t>(k)]);
        return l;
    }
    void Reverse()
    {
        items_.reverse(0, items_.size());
        version_++;
    }
    void Reverse(int32_t index, int32_t count)
    {
        if (index < 0 || count < 0 || get_Count() - index < count) ThrowArgumentOutOfRange();
        items_.reverse(static_cast<size_t>(index), static_cast<size_t>(count));
        version_++;
    }
    void CopyTo(Array<T>* dest) { CopyTo(dest, 0); }
    void CopyTo(Array<T>* dest, int32_t index)
    {
        nc(dest);
        if (index < 0 || index + get_Count() > dest->get_Length()) ThrowArgument("Destination array was not long enough.");
        for (size_t k = 0; k < items_.size(); k++) dest->vec()[static_cast<size_t>(index) + k] = items_[k];
    }
    void TrimExcess()
    {
        int32_t threshold = static_cast<int32_t>(static_cast<double>(capacity_) * 0.9);
        if (get_Count() < threshold) set_Capacity(get_Count());
    }

    // ---- sorting ----
    // Sort(Comparison<T>) and the hand port's Sort(callable): ArraySortHelper's comparer introsort.
    template <typename Cmp>
    void Sort(Cmp cmp)
    {
        if (get_Count() > 0) {
            auto call = [&](const T& a, const T& b) -> int32_t {
                if constexpr (std::is_pointer<Cmp>::value) return nc(cmp)->Invoke(a, b);
                else return cmp(a, b);
            };
            IntrospectiveSort(items_.data(), 0, get_Count(), capacity_, call);
        }
        version_++;
    }
    // Sort(IComparer<T>) (null: the default comparer).
    void Sort(IComparer_1<T>* comparer)
    {
        if (!comparer) {
            Sort();
            return;
        }
        Sort(0, get_Count(), comparer);
    }
    void Sort(int32_t index, int32_t count, IComparer_1<T>* comparer)
    {
        if (index < 0 || count < 0 || get_Count() - index < count) ThrowArgumentOutOfRange();
        if (!comparer) {
            DefaultSort(index, count);
        } else {
            auto call = [&](const T& a, const T& b) -> int32_t { return comparer->IComparer_1_Compare(a, b); };
            if (count > 1) IntrospectiveSort(items_.data(), index, count, capacity_, call);
        }
        version_++;
    }
    // Sort(): Comparer<T>.Default (the same introsort, comparing with Comparer<T>.Default).
    void Sort()
    {
        DefaultSort(0, get_Count());
        version_++;
    }

    // ---- enumeration ----
    struct Enumerator {
        List* list = nullptr;
        int32_t index = 0;
        int32_t version = 0;
        T current{};

        bool MoveNext()
        {
            if (version == list->version_ && static_cast<uint32_t>(index) < static_cast<uint32_t>(list->get_Count())) {
                current = list->items_[static_cast<size_t>(index)];
                index++;
                return true;
            }
            if (version != list->version_) ThrowInvalidOperation("Collection was modified; enumeration operation may not execute.");
            index = list->get_Count() + 1;
            current = T{};
            return false;
        }
        T get_Current() const { return current; }
        void Dispose() {}
        void Reset()
        {
            if (version != list->version_) ThrowInvalidOperation("Collection was modified; enumeration operation may not execute.");
            index = 0;
            current = T{};
        }
        // The collector: an enumerator a lowered foreach keeps in an iterator's state machine
        // (or a boxed one) may be all that holds its list (`foreach (x in new List<T>(...))`).
        friend void cs_trace_value(Tracer& t, const Enumerator& e)
        {
            t.Mark(e.list);
            TraceField(t, e.current);
        }
    };
    Enumerator GetEnumerator()
    {
        Enumerator e;
        e.list = this;
        e.version = version_;
        return e;
    }

    auto begin() const { return items_.begin(); }
    auto end() const { return items_.end(); }
    Vector<T>& vec() { return items_; }

    // ---- the interfaces ----
    IEnumerator_1<T>* IEnumerable_1_GetEnumerator() override { return gcnew<BoxedEnumerator<T, Enumerator>>(GetEnumerator()); }
    IEnumerator* IEnumerable_GetEnumerator() override { return gcnew<BoxedEnumerator<T, Enumerator>>(GetEnumerator()); }
    int32_t ICollection_1_get_Count() override { return get_Count(); }
    bool ICollection_1_get_IsReadOnly() override { return false; }
    void ICollection_1_Add(T v) override { Add(v); }
    void ICollection_1_Clear() override { Clear(); }
    bool ICollection_1_Contains(T v) override { return Contains(v); }
    void ICollection_1_CopyTo(Array<T>* dest, int32_t index) override { CopyTo(dest, index); }
    bool ICollection_1_Remove(T v) override { return Remove(v); }
    T IList_1_get_Item(int32_t i) override { return get_Item(i); }
    void IList_1_set_Item(int32_t i, T v) override { set_Item(i, v); }
    int32_t IList_1_IndexOf(T v) override { return IndexOf(v); }
    void IList_1_Insert(int32_t i, T v) override { Insert(i, v); }
    void IList_1_RemoveAt(int32_t i) override { RemoveAt(i); }

private:
    void EnsureCapacity(int32_t min)
    {
        if (capacity_ < min) {
            int32_t num = capacity_ == 0 ? 4 : capacity_ * 2;
            if (static_cast<uint32_t>(num) > 2146435071u) num = 2146435071;
            if (num < min) num = min;
            capacity_ = num;
            items_.reserve(static_cast<size_t>(num));
        }
    }

    void DefaultSort(int32_t index, int32_t count)
    {
        auto call = [](const T& a, const T& b) -> int32_t { return Comparer<T>::Compare(a, b); };
        if (count > 1) IntrospectiveSort(items_.data(), index, count, capacity_, call);
    }

    // ---- ArraySortHelper<T> (the comparer overload) ----

    static int32_t FloorLog2(int32_t n)
    {
        int32_t num = 0;
        while (n >= 1) {
            num++;
            n /= 2;
        }
        return num;
    }

    template <typename Cmp>
    static void SwapIfGreater(T* keys, Cmp& cmp, int32_t a, int32_t b)
    {
        if (a != b && cmp(keys[a], keys[b]) > 0) {
            T v = keys[a];
            keys[a] = keys[b];
            keys[b] = v;
        }
    }

    static void Swap(T* a, int32_t i, int32_t j)
    {
        if (i != j) {
            T v = a[i];
            a[i] = a[j];
            a[j] = v;
        }
    }

    template <typename Cmp>
    static void IntrospectiveSort(T* keys, int32_t left, int32_t length, int32_t keysLength, Cmp& cmp)
    {
        if (length >= 2) IntroSort(keys, left, length + left - 1, 2 * FloorLog2(keysLength), cmp);
    }

    template <typename Cmp>
    static void IntroSort(T* keys, int32_t lo, int32_t hi, int32_t depthLimit, Cmp& cmp)
    {
        while (hi > lo) {
            int32_t num = hi - lo + 1;
            if (num <= 16) {
                switch (num) {
                case 1:
                    break;
                case 2:
                    SwapIfGreater(keys, cmp, lo, hi);
                    break;
                case 3:
                    SwapIfGreater(keys, cmp, lo, hi - 1);
                    SwapIfGreater(keys, cmp, lo, hi);
                    SwapIfGreater(keys, cmp, hi - 1, hi);
                    break;
                default:
                    InsertionSort(keys, lo, hi, cmp);
                    break;
                }
                break;
            }
            if (depthLimit == 0) {
                Heapsort(keys, lo, hi, cmp);
                break;
            }
            depthLimit--;
            int32_t p = PickPivotAndPartition(keys, lo, hi, cmp);
            IntroSort(keys, p + 1, hi, depthLimit, cmp);
            hi = p - 1;
        }
    }

    template <typename Cmp>
    static int32_t PickPivotAndPartition(T* keys, int32_t lo, int32_t hi, Cmp& cmp)
    {
        int32_t mid = lo + (hi - lo) / 2;
        SwapIfGreater(keys, cmp, lo, mid);
        SwapIfGreater(keys, cmp, lo, hi);
        SwapIfGreater(keys, cmp, mid, hi);
        T pivot = keys[mid];
        Swap(keys, mid, hi - 1);
        int32_t left = lo;
        int32_t right = hi - 1;
        while (left < right) {
            while (cmp(keys[++left], pivot) < 0) {
            }
            while (cmp(pivot, keys[--right]) < 0) {
            }
            if (left >= right) break;
            Swap(keys, left, right);
        }
        Swap(keys, left, hi - 1);
        return left;
    }

    template <typename Cmp>
    static void Heapsort(T* keys, int32_t lo, int32_t hi, Cmp& cmp)
    {
        int32_t n = hi - lo + 1;
        for (int32_t i = n / 2; i >= 1; i--) DownHeap(keys, i, n, lo, cmp);
        for (int32_t i = n; i > 1; i--) {
            Swap(keys, lo, lo + i - 1);
            DownHeap(keys, 1, i - 1, lo, cmp);
        }
    }

    template <typename Cmp>
    static void DownHeap(T* keys, int32_t i, int32_t n, int32_t lo, Cmp& cmp)
    {
        T d = keys[lo + i - 1];
        while (i <= n / 2) {
            int32_t child = 2 * i;
            if (child < n && cmp(keys[lo + child - 1], keys[lo + child]) < 0) child++;
            if (cmp(d, keys[lo + child - 1]) >= 0) break;
            keys[lo + i - 1] = keys[lo + child - 1];
            i = child;
        }
        keys[lo + i - 1] = d;
    }

    template <typename Cmp>
    static void InsertionSort(T* keys, int32_t lo, int32_t hi, Cmp& cmp)
    {
        for (int32_t i = lo; i < hi; i++) {
            int32_t j = i;
            T t = keys[i + 1];
            while (j >= lo && cmp(t, keys[j]) < 0) {
                keys[j + 1] = keys[j];
                j--;
            }
            keys[j + 1] = t;
        }
    }

    Vector<T> items_;
    int32_t capacity_ = 0;
    int32_t version_ = 0;
};

} // namespace cs
