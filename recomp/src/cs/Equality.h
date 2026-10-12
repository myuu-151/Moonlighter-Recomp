#pragma once
// EqualityComparer<T>.Default semantics.
//
// Reference types that don't override Equals compare by identity (pointer ==).
// Value types provide Equals / Hash overloads in their own namespace, next to their
// definition, where argument-dependent lookup finds them.

#include <cmath>
#include <cstddef>
#include <functional>
#include <string>
#include <type_traits>

namespace cs {

// float.Equals: == or both NaN.
inline bool FloatEquals(float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); }

template <typename T>
inline bool Equals(T* a, T* b) { return a == b; }

template <typename T>
inline size_t Hash(T* p) { return std::hash<T*>()(p); }

inline bool Equals(int a, int b) { return a == b; }
inline size_t Hash(int v) { return std::hash<int>()(v); }

// System.String compares by value.
inline bool Equals(const std::string& a, const std::string& b) { return a == b; }
inline size_t Hash(const std::string& s) { return std::hash<std::string>()(s); }

// Enums compare by underlying value.
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
