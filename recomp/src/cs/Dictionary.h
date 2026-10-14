#pragma once
// System.Collections.Generic.Dictionary<K, V> as mscorlib's reference source (which Mono ships)
// implements it, so enumeration order is the game's: entries live in an array in insertion
// order; Remove puts an entry on a free list that the next insertion reuses (last freed, first
// reused); Clear empties both; a resize (to the next prime of at least twice the count) keeps
// the entries' order. Keys hash with EqualityComparer<K>.Default (cs/Collections.h).
// Enumerators (of the pairs, Keys and Values) throw InvalidOperationException when the
// dictionary changed under them.
//
// Two spellings: the C#-shaped API translated code calls, and the hand port's (Count(), Get,
// Set, TryGetValue(k, V&)).

#include <cstdint>
#include <vector>

#include "cs/Array.h"
#include "cs/Collections.h"
#include "cs/Object.h"

namespace cs {

namespace HashHelpers {
// HashHelpers.primes and GetPrime / ExpandPrime.
int32_t GetPrime(int32_t min);
int32_t ExpandPrime(int32_t oldSize);
} // namespace HashHelpers

namespace shared {

template <typename K, typename V>
class Dictionary : public Object, public virtual ICollection_1<KeyValuePair<K, V>> {
public:
    using Pair = KeyValuePair<K, V>;

    Dictionary() = default;

    const char* cs_typename() const override { return "System.Collections.Generic.Dictionary`2"; }
    bool cs_traced() const override { return true; }
    void cs_trace(Tracer& t) override
    {
        for (const Entry& e : entries_)
            if (e.hashCode >= 0) {
                TraceField(t, e.key);
                TraceField(t, e.value);
            }
        t.Mark(AsObject(comparer_));
        t.Mark(keys_);
        t.Mark(values_);
    }

    void __ctor() {}
    void __ctor(int32_t capacity)
    {
        if (capacity < 0) ThrowArgumentOutOfRange();
        if (capacity > 0) Initialize(capacity);
    }
    void __ctor(IEqualityComparer_1<K>* comparer) { comparer_ = comparer; }
    void __ctor(int32_t capacity, IEqualityComparer_1<K>* comparer)
    {
        __ctor(capacity);
        comparer_ = comparer;
    }
    void __ctor(Dictionary* source)
    {
        if (!source) ThrowArgumentNull("dictionary");
        __ctor(source->get_Count());
        for (const Entry& e : source->entries_)
            if (e.hashCode >= 0) Add(e.key, e.value);
    }

    // ---- size ----
    int32_t get_Count() const { return count_ - freeCount_; }
    int32_t Count() const { return get_Count(); }

    // ---- lookups ----
    V get_Item(const K& key)
    {
        int32_t i = FindEntry(key);
        if (i < 0) ThrowKeyNotFound();
        return entries_[static_cast<size_t>(i)].value;
    }
    const V& Get(const K& key) const
    {
        int32_t i = FindEntry(key);
        if (i < 0) ThrowKeyNotFound();
        return entries_[static_cast<size_t>(i)].value;
    }
    void set_Item(const K& key, const V& value) { Insert(key, value, false); }
    void Set(const K& key, const V& value) { Insert(key, value, false); }
    void Add(const K& key, const V& value) { Insert(key, value, true); }

    bool ContainsKey(const K& key) const { return FindEntry(key) >= 0; }
    bool ContainsValue(const V& value) const
    {
        for (int32_t i = 0; i < count_; i++) {
            const Entry& e = entries_[static_cast<size_t>(i)];
            if (e.hashCode >= 0 && EqualityComparer<V>::Equals(e.value, value)) return true;
        }
        return false;
    }
    // (the out parameter of the C# type: the value cast back to it, for an erased reference)
    template <typename U>
    bool TryGetValue(const K& key, U& value) const
    {
        int32_t i = FindEntry(key);
        if (i >= 0) {
            value = Down<U>(entries_[static_cast<size_t>(i)].value);
            return true;
        }
        value = U{};
        return false;
    }

    bool Remove(const K& key)
    {
        CheckKey(key);
        if (buckets_.empty()) return false;
        int32_t hashCode = Hash(key);
        int32_t bucket = hashCode % static_cast<int32_t>(buckets_.size());
        int32_t last = -1;
        for (int32_t i = buckets_[static_cast<size_t>(bucket)]; i >= 0; last = i, i = entries_[static_cast<size_t>(i)].next) {
            Entry& e = entries_[static_cast<size_t>(i)];
            if (e.hashCode == hashCode && KeyEquals(e.key, key)) {
                if (last < 0) buckets_[static_cast<size_t>(bucket)] = e.next;
                else entries_[static_cast<size_t>(last)].next = e.next;
                e.hashCode = -1;
                e.next = freeList_;
                e.key = K{};
                e.value = V{};
                freeList_ = i;
                freeCount_++;
                version_++;
                return true;
            }
        }
        return false;
    }

    void Clear()
    {
        if (count_ > 0) {
            for (auto& b : buckets_) b = -1;
            for (int32_t i = 0; i < count_; i++) entries_[static_cast<size_t>(i)] = Entry{};
            freeList_ = -1;
            count_ = 0;
            freeCount_ = 0;
            version_++;
        }
    }

    // ---- enumeration ----
    struct Enumerator {
        Dictionary* d = nullptr;
        int32_t version = 0;
        int32_t index = 0;
        Pair current{};

        bool MoveNext()
        {
            if (version != d->version_) ThrowInvalidOperation("Collection was modified; enumeration operation may not execute.");
            while (static_cast<uint32_t>(index) < static_cast<uint32_t>(d->count_)) {
                const Entry& e = d->entries_[static_cast<size_t>(index)];
                if (e.hashCode >= 0) {
                    current = Pair(e.key, e.value);
                    index++;
                    return true;
                }
                index++;
            }
            index = d->count_ + 1;
            current = Pair();
            return false;
        }
        Pair get_Current() const { return current; }
        void Dispose() {}
        void Reset()
        {
            if (version != d->version_) ThrowInvalidOperation("Collection was modified; enumeration operation may not execute.");
            index = 0;
            current = Pair();
        }
        // The collector: it may be all that holds its dictionary (cs/List.h's Enumerator).
        friend void cs_trace_value(Tracer& t, const Enumerator& e)
        {
            t.Mark(e.d);
            TraceField(t, e.current);
        }
    };
    Enumerator GetEnumerator()
    {
        Enumerator e;
        e.d = this;
        e.version = version_;
        return e;
    }

    // Keys / Values: views in the same order.
    template <bool Keys, typename T>
    class Collection : public Object, public virtual ICollection_1<T> {
    public:
        explicit Collection(Dictionary* d) : d_(d) {}
        const char* cs_typename() const override { return Keys ? "System.Collections.Generic.Dictionary`2+KeyCollection" : "System.Collections.Generic.Dictionary`2+ValueCollection"; }
        bool cs_traced() const override { return true; }
        void cs_trace(Tracer& t) override { t.Mark(d_); }

        struct Enumerator {
            Dictionary* d = nullptr;
            int32_t version = 0;
            int32_t index = 0;
            T current{};
            bool MoveNext()
            {
                if (version != d->version_) ThrowInvalidOperation("Collection was modified; enumeration operation may not execute.");
                while (static_cast<uint32_t>(index) < static_cast<uint32_t>(d->count_)) {
                    const Entry& e = d->entries_[static_cast<size_t>(index)];
                    if (e.hashCode >= 0) {
                        if constexpr (Keys) current = e.key;
                        else current = e.value;
                        index++;
                        return true;
                    }
                    index++;
                }
                index = d->count_ + 1;
                current = T{};
                return false;
            }
            T get_Current() const { return current; }
            void Dispose() {}
            void Reset()
            {
                if (version != d->version_) ThrowInvalidOperation("Collection was modified; enumeration operation may not execute.");
                index = 0;
                current = T{};
            }
            friend void cs_trace_value(Tracer& t, const Enumerator& e)
            {
                t.Mark(e.d);
                TraceField(t, e.current);
            }
        };
        Enumerator GetEnumerator()
        {
            Enumerator e;
            e.d = d_;
            e.version = d_->version_;
            return e;
        }
        int32_t get_Count() const { return d_->get_Count(); }
        void CopyTo(Array<T>* dest, int32_t index)
        {
            nc(dest);
            if (index < 0 || index > dest->get_Length()) ThrowArgumentOutOfRange();
            if (dest->get_Length() - index < d_->get_Count()) ThrowArgument("Destination array is not long enough.");
            for (int32_t i = 0; i < d_->count_; i++) {
                const Entry& e = d_->entries_[static_cast<size_t>(i)];
                if (e.hashCode >= 0) {
                    if constexpr (Keys) dest->at(index++) = e.key;
                    else dest->at(index++) = e.value;
                }
            }
        }
        IEnumerator_1<T>* IEnumerable_1_GetEnumerator() override { return gcnew<BoxedEnumerator<T, Enumerator>>(GetEnumerator()); }
        IEnumerator* IEnumerable_GetEnumerator() override { return gcnew<BoxedEnumerator<T, Enumerator>>(GetEnumerator()); }
        int32_t ICollection_1_get_Count() override { return get_Count(); }
        bool ICollection_1_get_IsReadOnly() override { return true; }
        void ICollection_1_Add(T) override { Throw<NotSupportedException>(); }
        void ICollection_1_Clear() override { Throw<NotSupportedException>(); }
        bool ICollection_1_Contains(T v) override
        {
            if constexpr (Keys) return d_->ContainsKey(v);
            else return d_->ContainsValue(v);
        }
        void ICollection_1_CopyTo(Array<T>* dest, int32_t index) override { CopyTo(dest, index); }
        bool ICollection_1_Remove(T) override { Throw<NotSupportedException>(); }

    private:
        Dictionary* d_;
    };
    using KeyCollection = Collection<true, K>;
    using ValueCollection = Collection<false, V>;

    KeyCollection* get_Keys()
    {
        if (!keys_) keys_ = gcnew<KeyCollection>(this);
        return keys_;
    }
    ValueCollection* get_Values()
    {
        if (!values_) values_ = gcnew<ValueCollection>(this);
        return values_;
    }

    // ---- the interfaces ----
    IEnumerator_1<Pair>* IEnumerable_1_GetEnumerator() override { return gcnew<BoxedEnumerator<Pair, Enumerator>>(GetEnumerator()); }
    IEnumerator* IEnumerable_GetEnumerator() override { return gcnew<BoxedEnumerator<Pair, Enumerator>>(GetEnumerator()); }
    int32_t ICollection_1_get_Count() override { return get_Count(); }
    bool ICollection_1_get_IsReadOnly() override { return false; }
    void ICollection_1_Add(Pair p) override { Add(p.key, p.value); }
    void ICollection_1_Clear() override { Clear(); }
    bool ICollection_1_Contains(Pair p) override
    {
        int32_t i = FindEntry(p.key);
        return i >= 0 && EqualityComparer<V>::Equals(entries_[static_cast<size_t>(i)].value, p.value);
    }
    void ICollection_1_CopyTo(Array<Pair>* dest, int32_t index) override
    {
        nc(dest);
        if (index < 0 || index > dest->get_Length()) ThrowArgumentOutOfRange();
        if (dest->get_Length() - index < get_Count()) ThrowArgument("Destination array is not long enough.");
        for (int32_t i = 0; i < count_; i++) {
            const Entry& e = entries_[static_cast<size_t>(i)];
            if (e.hashCode >= 0) dest->at(index++) = Pair(e.key, e.value);
        }
    }
    bool ICollection_1_Remove(Pair p) override
    {
        int32_t i = FindEntry(p.key);
        if (i >= 0 && EqualityComparer<V>::Equals(entries_[static_cast<size_t>(i)].value, p.value)) {
            Remove(p.key);
            return true;
        }
        return false;
    }

private:
    struct Entry {
        int32_t hashCode = 0;
        int32_t next = 0;
        K key{};
        V value{};
    };

    void CheckKey(const K& key) const
    {
        if constexpr (std::is_pointer<K>::value)
            if (!AsObject(key)) ThrowArgumentNull("key");
    }
    int32_t Hash(const K& key) const
    {
        int32_t h = comparer_ ? comparer_->IEqualityComparer_1_GetHashCode(key) : EqualityComparer<K>::GetHashCode(key);
        return h & 0x7FFFFFFF;
    }
    bool KeyEquals(const K& a, const K& b) const
    {
        return comparer_ ? comparer_->IEqualityComparer_1_Equals(a, b) : EqualityComparer<K>::Equals(a, b);
    }

    void Initialize(int32_t capacity)
    {
        int32_t size = HashHelpers::GetPrime(capacity);
        buckets_.assign(static_cast<size_t>(size), -1);
        entries_.assign(static_cast<size_t>(size), Entry{});
        freeList_ = -1;
    }

    int32_t FindEntry(const K& key) const
    {
        CheckKey(key);
        if (!buckets_.empty()) {
            int32_t hashCode = Hash(key);
            for (int32_t i = buckets_[static_cast<size_t>(hashCode % static_cast<int32_t>(buckets_.size()))]; i >= 0; i = entries_[static_cast<size_t>(i)].next) {
                const Entry& e = entries_[static_cast<size_t>(i)];
                if (e.hashCode == hashCode && KeyEquals(e.key, key)) return i;
            }
        }
        return -1;
    }

    void Insert(const K& key, const V& value, bool add)
    {
        CheckKey(key);
        if (buckets_.empty()) Initialize(0);
        int32_t hashCode = Hash(key);
        int32_t targetBucket = hashCode % static_cast<int32_t>(buckets_.size());
        for (int32_t i = buckets_[static_cast<size_t>(targetBucket)]; i >= 0; i = entries_[static_cast<size_t>(i)].next) {
            Entry& e = entries_[static_cast<size_t>(i)];
            if (e.hashCode == hashCode && KeyEquals(e.key, key)) {
                if (add) ThrowArgument("An item with the same key has already been added.");
                e.value = value;
                version_++;
                return;
            }
        }
        int32_t index;
        if (freeCount_ > 0) {
            index = freeList_;
            freeList_ = entries_[static_cast<size_t>(index)].next;
            freeCount_--;
        } else {
            if (count_ == static_cast<int32_t>(entries_.size())) {
                Resize(HashHelpers::ExpandPrime(count_));
                targetBucket = hashCode % static_cast<int32_t>(buckets_.size());
            }
            index = count_;
            count_++;
        }
        Entry& e = entries_[static_cast<size_t>(index)];
        e.hashCode = hashCode;
        e.next = buckets_[static_cast<size_t>(targetBucket)];
        e.key = key;
        e.value = value;
        buckets_[static_cast<size_t>(targetBucket)] = index;
        version_++;
    }

    void Resize(int32_t newSize)
    {
        std::vector<int32_t> newBuckets(static_cast<size_t>(newSize), -1);
        std::vector<Entry> newEntries(static_cast<size_t>(newSize));
        for (int32_t i = 0; i < count_; i++) newEntries[static_cast<size_t>(i)] = entries_[static_cast<size_t>(i)];
        for (int32_t i = 0; i < count_; i++) {
            Entry& e = newEntries[static_cast<size_t>(i)];
            if (e.hashCode >= 0) {
                int32_t bucket = e.hashCode % newSize;
                e.next = newBuckets[static_cast<size_t>(bucket)];
                newBuckets[static_cast<size_t>(bucket)] = i;
            }
        }
        buckets_ = std::move(newBuckets);
        entries_ = std::move(newEntries);
    }

    std::vector<int32_t> buckets_;
    std::vector<Entry> entries_;
    int32_t count_ = 0;
    int32_t version_ = 0;
    int32_t freeList_ = -1;
    int32_t freeCount_ = 0;
    IEqualityComparer_1<K>* comparer_ = nullptr;
    KeyCollection* keys_ = nullptr;
    ValueCollection* values_ = nullptr;
};

} // namespace shared
} // namespace cs
