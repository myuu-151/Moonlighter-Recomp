#pragma once
// UnityEngine.Vector2 (Unity 2019.2 semantics).
//  - operator== is approximate (squared distance < 1e-10), as in Unity.
//  - cs::Equals (used by List.IndexOf / Dictionary keys) is exact per component, as
//    Vector2.Equals is; -0 equals 0.

#include <cmath>
#include <cstring>
#include <functional>

#include "cs/Equality.h"

namespace unity {

struct Vector2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vector2() = default;
    constexpr Vector2(float x_, float y_) : x(x_), y(y_) {}

    static constexpr Vector2 zero() { return {0.0f, 0.0f}; }
    static constexpr Vector2 up() { return {0.0f, 1.0f}; }
    static constexpr Vector2 down() { return {0.0f, -1.0f}; }
    static constexpr Vector2 right() { return {1.0f, 0.0f}; }
    static constexpr Vector2 left() { return {-1.0f, 0.0f}; }

    friend Vector2 operator+(Vector2 a, Vector2 b) { return {a.x + b.x, a.y + b.y}; }
    friend Vector2 operator-(Vector2 a, Vector2 b) { return {a.x - b.x, a.y - b.y}; }
    friend Vector2 operator-(Vector2 a) { return {-a.x, -a.y}; }
    friend Vector2 operator*(Vector2 a, float d) { return {a.x * d, a.y * d}; }

    friend bool operator==(Vector2 a, Vector2 b)
    {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        float sq = dx * dx;
        sq = sq + dy * dy;
        return sq < 9.99999944e-11f;
    }
    friend bool operator!=(Vector2 a, Vector2 b) { return !(a == b); }
};

// Found by argument-dependent lookup from cs::List / cs::Dictionary.
inline bool Equals(const Vector2& a, const Vector2& b)
{
    return cs::FloatEquals(a.x, b.x) && cs::FloatEquals(a.y, b.y);
}

inline size_t Hash(const Vector2& v)
{
    // Only needs to agree with Equals (+0/-0 equal); .NET's hash value itself is never observed.
    float x = v.x == 0.0f ? 0.0f : v.x;
    float y = v.y == 0.0f ? 0.0f : v.y;
    unsigned ux, uy;
    std::memcpy(&ux, &x, 4);
    std::memcpy(&uy, &y, 4);
    return std::hash<unsigned long long>()((static_cast<unsigned long long>(ux) << 32) | uy);
}

} // namespace unity
