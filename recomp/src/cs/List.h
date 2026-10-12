#pragma once
// System.Collections.Generic.List<T> with the behaviour Mono's mscorlib shows to game code:
//  - capacity grows 0 -> 4 -> 8 -> ... (EnsureCapacity), and copies are sized to the count;
//  - Sort(Comparison) is Mono's ArraySortHelper introsort, whose depth limit is taken from
//    the backing array length (capacity), so capacity is tracked even though std::vector
//    manages the real storage.
// List is a reference type in C#: always allocate it with cs::gcnew and pass List<T>*.

#include <vector>

#include "cs/Equality.h"
#include "cs/Object.h"

namespace cs {

template <typename T>
class List : public Object {
public:
    List() = default;
    List(const List&) = delete;
    List& operator=(const List&) = delete;

    // new List<T>(IEnumerable<T> collection) for an ICollection source.
    static List* CopyOf(const List* src)
    {
        List* l = gcnew<List>();
        l->items_ = NullCheck(src)->items_;
        l->capacity_ = static_cast<int>(src->items_.size());
        return l;
    }

    int Count() const { return static_cast<int>(items_.size()); }
    int Capacity() const { return capacity_; }

    const T& operator[](int i) const
    {
        if (static_cast<unsigned>(i) >= items_.size()) throw ArgumentOutOfRangeException();
        return items_[i];
    }
    void Set(int i, const T& v)
    {
        if (static_cast<unsigned>(i) >= items_.size()) throw ArgumentOutOfRangeException();
        items_[i] = v;
    }

    void Add(const T& v)
    {
        EnsureCapacity(Count() + 1);
        items_.push_back(v);
    }

    void AddRange(const List* other)
    {
        NullCheck(other);
        int n = other->Count();
        if (n == 0) return;
        EnsureCapacity(Count() + n);
        items_.insert(items_.end(), other->items_.begin(), other->items_.end());
    }

    int IndexOf(const T& v) const
    {
        for (int i = 0; i < Count(); i++)
            if (Equals(items_[i], v)) return i;
        return -1;
    }

    bool Contains(const T& v) const { return IndexOf(v) != -1; }

    template <typename Pred>
    int FindIndex(Pred pred) const
    {
        for (int i = 0; i < Count(); i++)
            if (pred(items_[i])) return i;
        return -1;
    }

    void RemoveAt(int i)
    {
        if (static_cast<unsigned>(i) >= items_.size()) throw ArgumentOutOfRangeException();
        items_.erase(items_.begin() + i);
    }

    void RemoveRange(int index, int count)
    {
        if (index < 0 || count < 0) throw ArgumentOutOfRangeException();
        if (Count() - index < count) throw ArgumentException();
        items_.erase(items_.begin() + index, items_.begin() + index + count);
    }

    void Clear() { items_.clear(); }

    // List<T>.Sort(Comparison<T>): cmp(a, b) returns <0, 0, >0.
    template <typename Cmp>
    void Sort(Cmp cmp)
    {
        if (Count() > 0) IntrospectiveSort(items_.data(), 0, Count(), capacity_, cmp);
    }

    typename std::vector<T>::const_iterator begin() const { return items_.begin(); }
    typename std::vector<T>::const_iterator end() const { return items_.end(); }

private:
    void EnsureCapacity(int min)
    {
        if (capacity_ < min) {
            int num = capacity_ == 0 ? 4 : capacity_ * 2;
            if (static_cast<unsigned>(num) > 2146435071u) num = 2146435071;
            if (num < min) num = min;
            capacity_ = num;
            items_.reserve(num);
        }
    }

    // ---- Mono ArraySortHelper<T> (Comparison overload) -----------------------------------

    static int FloorLog2(int n)
    {
        int num = 0;
        while (n >= 1) {
            num++;
            n /= 2;
        }
        return num;
    }

    template <typename Cmp>
    static void SwapIfGreater(T* keys, Cmp& cmp, int a, int b)
    {
        if (a != b && cmp(keys[a], keys[b]) > 0) {
            T v = keys[a];
            keys[a] = keys[b];
            keys[b] = v;
        }
    }

    static void Swap(T* a, int i, int j)
    {
        if (i != j) {
            T v = a[i];
            a[i] = a[j];
            a[j] = v;
        }
    }

    template <typename Cmp>
    static void IntrospectiveSort(T* keys, int left, int length, int keysLength, Cmp& cmp)
    {
        if (length >= 2) IntroSort(keys, left, length + left - 1, 2 * FloorLog2(keysLength), cmp);
    }

    template <typename Cmp>
    static void IntroSort(T* keys, int lo, int hi, int depthLimit, Cmp& cmp)
    {
        while (hi > lo) {
            int num = hi - lo + 1;
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
            int p = PickPivotAndPartition(keys, lo, hi, cmp);
            IntroSort(keys, p + 1, hi, depthLimit, cmp);
            hi = p - 1;
        }
    }

    template <typename Cmp>
    static int PickPivotAndPartition(T* keys, int lo, int hi, Cmp& cmp)
    {
        int mid = lo + (hi - lo) / 2;
        SwapIfGreater(keys, cmp, lo, mid);
        SwapIfGreater(keys, cmp, lo, hi);
        SwapIfGreater(keys, cmp, mid, hi);
        T pivot = keys[mid];
        Swap(keys, mid, hi - 1);
        int left = lo;
        int right = hi - 1;
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
    static void Heapsort(T* keys, int lo, int hi, Cmp& cmp)
    {
        int n = hi - lo + 1;
        for (int i = n / 2; i >= 1; i--) DownHeap(keys, i, n, lo, cmp);
        for (int i = n; i > 1; i--) {
            Swap(keys, lo, lo + i - 1);
            DownHeap(keys, 1, i - 1, lo, cmp);
        }
    }

    template <typename Cmp>
    static void DownHeap(T* keys, int i, int n, int lo, Cmp& cmp)
    {
        T d = keys[lo + i - 1];
        while (i <= n / 2) {
            int child = 2 * i;
            if (child < n && cmp(keys[lo + child - 1], keys[lo + child]) < 0) child++;
            if (cmp(d, keys[lo + child - 1]) >= 0) break;
            keys[lo + i - 1] = keys[lo + child - 1];
            i = child;
        }
        keys[lo + i - 1] = d;
    }

    template <typename Cmp>
    static void InsertionSort(T* keys, int lo, int hi, Cmp& cmp)
    {
        for (int i = lo; i < hi; i++) {
            int j = i;
            T t = keys[i + 1];
            while (j >= lo && cmp(t, keys[j]) < 0) {
                keys[j + 1] = keys[j];
                j--;
            }
            keys[j + 1] = t;
        }
    }

    std::vector<T> items_;
    int capacity_ = 0;
};

// int.CompareTo(int)
inline int CompareTo(int a, int b) { return a < b ? -1 : (a > b ? 1 : 0); }

} // namespace cs
