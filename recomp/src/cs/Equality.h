#pragma once
// The hand port's equality hooks, found by EqualityComparer<T> (cs/Collections.h) through
// argument-dependent lookup when a value type has no cs_box_equals: reference types that don't
// override Equals compare by identity, value types bring Equals / Hash overloads in their own
// namespace (unity::Equals(Vector2, Vector2)), std::string compares by value.

#include <cstddef>
#include <functional>
#include <string>
#include <type_traits>

#include "cs/Prim.h"

namespace cs {

template <typename T>
inline bool Equals(T* a, T* b) { return a == b; }

template <typename T>
inline size_t Hash(T* p) { return std::hash<T*>()(p); }

inline bool Equals(int a, int b) { return a == b; }
inline size_t Hash(int v) { return std::hash<int>()(v); }

inline bool Equals(const std::string& a, const std::string& b) { return a == b; }
inline size_t Hash(const std::string& s) { return std::hash<std::string>()(s); }

template <typename T, typename = std::enable_if_t<std::is_enum<T>::value>>
inline bool Equals(T a, T b) { return a == b; }
template <typename T, typename = std::enable_if_t<std::is_enum<T>::value>>
inline size_t Hash(T v) { return std::hash<long long>()(static_cast<long long>(v)); }

struct DefaultHasher {
    template <typename T>
    size_t operator()(const T& v) const { return Hash(v); }
};

struct DefaultEq {
    template <typename T>
    bool operator()(const T& a, const T& b) const { return Equals(a, b); }
};

} // namespace cs
