#pragma once
// System.Collections.Generic.Dictionary<K, V> for lookups and inserts.
// Enumeration order is not provided: game code that enumerates a Dictionary needs
// Mono's bucket/entry order reproduced, which will be added when first needed.

#include <unordered_map>

#include "cs/Equality.h"
#include "cs/Object.h"

namespace cs {

template <typename K, typename V>
class Dictionary : public Object {
public:
    int Count() const { return static_cast<int>(map_.size()); }

    bool ContainsKey(const K& k) const { return map_.find(k) != map_.end(); }

    bool TryGetValue(const K& k, V& out) const
    {
        auto it = map_.find(k);
        if (it == map_.end()) return false;
        out = it->second;
        return true;
    }

    // dict[k] (get): throws KeyNotFoundException like .NET.
    const V& Get(const K& k) const
    {
        auto it = map_.find(k);
        if (it == map_.end()) throw KeyNotFoundException();
        return it->second;
    }

    // dict[k] = v
    void Set(const K& k, const V& v) { map_[k] = v; }

    // dict.Add(k, v): throws on duplicate key.
    void Add(const K& k, const V& v)
    {
        if (!map_.emplace(k, v).second) throw ArgumentException("An item with the same key has already been added.");
    }

    void Clear() { map_.clear(); }

private:
    std::unordered_map<K, V, DefaultHasher, DefaultEq> map_;
};

} // namespace cs
