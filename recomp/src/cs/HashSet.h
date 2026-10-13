#pragma once
// System.Collections.Generic.HashSet<T> as System.Core's reference source (Mono's) has it, so
// enumeration order is the game's: slots in insertion order, removed slots on a free list the
// next insertion reuses (last freed, first reused), and the whole set reset (no free slots)
// when the last element is removed.

#include <cstdint>
#include <vector>

#include "cs/Array.h"
#include "cs/Collections.h"
#include "cs/Dictionary.h"
#include "cs/Object.h"

namespace cs {

template <typename T>
class HashSet : public Object, public virtual ICollection_1<T> {
public:
    HashSet() = default;

    const char* cs_typename() const override { return "System.Collections.Generic.HashSet`1"; }
    bool cs_traced() const override { return true; }
    void cs_trace(Tracer& t) override
    {
        for (int32_t i = 0; i < lastIndex_; i++)
            if (slots_[static_cast<size_t>(i)].hashCode >= 0) TraceField(t, slots_[static_cast<size_t>(i)].value);
    }

    void __ctor() {}
    void __ctor(IEnumerable_1<T>* collection)
    {
        if (!collection) ThrowArgumentNull("collection");
        int32_t suggested = 0;
        if (auto* c = dynamic_cast<ICollection_1<T>*>(collection)) suggested = c->ICollection_1_get_Count();
        Initialize(suggested);
        UnionWith(collection);
        if ((count_ == 0 && static_cast<int32_t>(slots_.size()) > 3) || (count_ > 0 && static_cast<int32_t>(slots_.size()) / count_ > 3)) TrimExcess();
    }

    int32_t get_Count() const { return count_; }

    bool Add(T value)
    {
        if (buckets_.empty()) Initialize(0);
        int32_t hashCode = Hash(value);
        int32_t bucket = hashCode % static_cast<int32_t>(buckets_.size());
        for (int32_t i = buckets_[static_cast<size_t>(bucket)] - 1; i >= 0; i = slots_[static_cast<size_t>(i)].next)
            if (slots_[static_cast<size_t>(i)].hashCode == hashCode && EqualityComparer<T>::Equals(slots_[static_cast<size_t>(i)].value, value)) return false;
        int32_t index;
        if (freeList_ >= 0) {
            index = freeList_;
            freeList_ = slots_[static_cast<size_t>(index)].next;
        } else {
            if (lastIndex_ == static_cast<int32_t>(slots_.size())) {
                IncreaseCapacity();
                bucket = hashCode % static_cast<int32_t>(buckets_.size());
            }
            index = lastIndex_;
            lastIndex_++;
        }
        Slot& s = slots_[static_cast<size_t>(index)];
        s.hashCode = hashCode;
        s.value = value;
        s.next = buckets_[static_cast<size_t>(bucket)] - 1;
        buckets_[static_cast<size_t>(bucket)] = index + 1;
        count_++;
        version_++;
        return true;
    }

    bool Contains(T item) const
    {
        if (!buckets_.empty()) {
            int32_t hashCode = Hash(item);
            for (int32_t i = buckets_[static_cast<size_t>(hashCode % static_cast<int32_t>(buckets_.size()))] - 1; i >= 0; i = slots_[static_cast<size_t>(i)].next)
                if (slots_[static_cast<size_t>(i)].hashCode == hashCode && EqualityComparer<T>::Equals(slots_[static_cast<size_t>(i)].value, item)) return true;
        }
        return false;
    }

    bool Remove(T item)
    {
        if (!buckets_.empty()) {
            int32_t hashCode = Hash(item);
            int32_t bucket = hashCode % static_cast<int32_t>(buckets_.size());
            int32_t last = -1;
            for (int32_t i = buckets_[static_cast<size_t>(bucket)] - 1; i >= 0; last = i, i = slots_[static_cast<size_t>(i)].next) {
                Slot& s = slots_[static_cast<size_t>(i)];
                if (s.hashCode == hashCode && EqualityComparer<T>::Equals(s.value, item)) {
                    if (last < 0) buckets_[static_cast<size_t>(bucket)] = s.next + 1;
                    else slots_[static_cast<size_t>(last)].next = s.next;
                    s.hashCode = -1;
                    s.value = T{};
                    s.next = freeList_;
                    count_--;
                    version_++;
                    if (count_ == 0) {
                        lastIndex_ = 0;
                        freeList_ = -1;
                    } else {
                        freeList_ = i;
                    }
                    return true;
                }
            }
        }
        return false;
    }

    void Clear()
    {
        if (lastIndex_ > 0) {
            for (int32_t i = 0; i < lastIndex_; i++) slots_[static_cast<size_t>(i)] = Slot{};
            for (auto& b : buckets_) b = 0;
            lastIndex_ = 0;
            count_ = 0;
            freeList_ = -1;
        }
        version_++;
    }

    void UnionWith(IEnumerable_1<T>* other)
    {
        if (!other) ThrowArgumentNull("other");
        IEnumerator_1<T>* e = other->IEnumerable_1_GetEnumerator();
        auto fin = MakeFinally([&] { e->IDisposable_Dispose(); });
        while (e->IEnumerator_MoveNext()) Add(e->IEnumerator_1_get_Current());
    }

    void TrimExcess()
    {
        if (count_ == 0) {
            buckets_.clear();
            slots_.clear();
            version_++;
            return;
        }
        int32_t newSize = HashHelpers::GetPrime(count_);
        std::vector<Slot> newSlots(static_cast<size_t>(newSize));
        std::vector<int32_t> newBuckets(static_cast<size_t>(newSize), 0);
        int32_t newIndex = 0;
        for (int32_t i = 0; i < lastIndex_; i++) {
            if (slots_[static_cast<size_t>(i)].hashCode >= 0) {
                newSlots[static_cast<size_t>(newIndex)] = slots_[static_cast<size_t>(i)];
                int32_t bucket = newSlots[static_cast<size_t>(newIndex)].hashCode % newSize;
                newSlots[static_cast<size_t>(newIndex)].next = newBuckets[static_cast<size_t>(bucket)] - 1;
                newBuckets[static_cast<size_t>(bucket)] = newIndex + 1;
                newIndex++;
            }
        }
        lastIndex_ = newIndex;
        slots_ = std::move(newSlots);
        buckets_ = std::move(newBuckets);
        freeList_ = -1;
    }

    struct Enumerator {
        HashSet* set = nullptr;
        int32_t index = 0;
        int32_t version = 0;
        T current{};
        bool MoveNext()
        {
            if (version != set->version_) ThrowInvalidOperation("Collection was modified; enumeration operation may not execute.");
            while (index < set->lastIndex_) {
                const Slot& s = set->slots_[static_cast<size_t>(index)];
                if (s.hashCode >= 0) {
                    current = s.value;
                    index++;
                    return true;
                }
                index++;
            }
            index = set->lastIndex_ + 1;
            current = T{};
            return false;
        }
        T get_Current() const { return current; }
        void Dispose() {}
        void Reset()
        {
            if (version != set->version_) ThrowInvalidOperation("Collection was modified; enumeration operation may not execute.");
            index = 0;
            current = T{};
        }
    };
    Enumerator GetEnumerator()
    {
        Enumerator e;
        e.set = this;
        e.version = version_;
        return e;
    }

    void CopyTo(Array<T>* dest, int32_t index)
    {
        nc(dest);
        if (index < 0 || dest->get_Length() - index < count_) ThrowArgument("Destination array is not long enough.");
        for (int32_t i = 0; i < lastIndex_; i++)
            if (slots_[static_cast<size_t>(i)].hashCode >= 0) dest->at(index++) = slots_[static_cast<size_t>(i)].value;
    }

    IEnumerator_1<T>* IEnumerable_1_GetEnumerator() override { return gcnew<BoxedEnumerator<T, Enumerator>>(GetEnumerator()); }
    IEnumerator* IEnumerable_GetEnumerator() override { return gcnew<BoxedEnumerator<T, Enumerator>>(GetEnumerator()); }
    int32_t ICollection_1_get_Count() override { return count_; }
    bool ICollection_1_get_IsReadOnly() override { return false; }
    void ICollection_1_Add(T v) override { Add(v); }
    void ICollection_1_Clear() override { Clear(); }
    bool ICollection_1_Contains(T v) override { return Contains(v); }
    void ICollection_1_CopyTo(Array<T>* dest, int32_t index) override { CopyTo(dest, index); }
    bool ICollection_1_Remove(T v) override { return Remove(v); }

private:
    struct Slot {
        int32_t hashCode = 0;
        int32_t next = 0;
        T value{};
    };

    static int32_t Hash(const T& v)
    {
        if constexpr (std::is_pointer<T>::value)
            if (!AsObject(v)) return 0;
        return EqualityComparer<T>::GetHashCode(v) & 0x7FFFFFFF;
    }

    void Initialize(int32_t capacity)
    {
        int32_t size = HashHelpers::GetPrime(capacity);
        buckets_.assign(static_cast<size_t>(size), 0);
        slots_.assign(static_cast<size_t>(size), Slot{});
    }

    void IncreaseCapacity()
    {
        int32_t newSize = HashHelpers::ExpandPrime(count_);
        if (newSize <= count_) ThrowArgument("Capacity overflow");
        std::vector<Slot> newSlots(static_cast<size_t>(newSize));
        for (int32_t i = 0; i < lastIndex_; i++) newSlots[static_cast<size_t>(i)] = slots_[static_cast<size_t>(i)];
        std::vector<int32_t> newBuckets(static_cast<size_t>(newSize), 0);
        for (int32_t i = 0; i < lastIndex_; i++) {
            int32_t bucket = newSlots[static_cast<size_t>(i)].hashCode % newSize;
            newSlots[static_cast<size_t>(i)].next = newBuckets[static_cast<size_t>(bucket)] - 1;
            newBuckets[static_cast<size_t>(bucket)] = i + 1;
        }
        slots_ = std::move(newSlots);
        buckets_ = std::move(newBuckets);
    }

    std::vector<int32_t> buckets_;
    std::vector<Slot> slots_;
    int32_t count_ = 0;
    int32_t lastIndex_ = 0;
    int32_t freeList_ = -1;
    int32_t version_ = 0;
};

} // namespace cs
