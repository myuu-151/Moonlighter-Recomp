#pragma once
// UnityEngine.Mathf and the Vector2/Vector3 members the ported code uses, as Unity 2019.2's
// managed code computes them under the game's Mono: float operations in the C# order, but an
// expression's intermediate results are kept in doubles and rounded to a float only where
// they're stored (a field, a local, an argument, a return value). So a + (b - a) * t or
// x * x + y * y round once, at the end (data/oracle/slime.jsonl: the enemies' agents, in the
// last bit). Each operator call (Vector2 + Vector2, ...) returns a stored Vector2: one rounding
// per operation there. Vector3 versions of 2D values carry z = 0 and keep its terms where they
// can change a result's sign of zero.

#include <cmath>

#include "unity/Vector2.h"

namespace unity {
namespace Mathf {

// Mathf.Epsilon on the desktop (denormals not flushed): float.Epsilon.
constexpr float Epsilon = 1.401298464e-45f;

inline float Min(float a, float b) { return a < b ? a : b; }
inline float Max(float a, float b) { return a > b ? a : b; }
inline float Abs(float f) { return std::fabs(f); }
inline float Sign(float f) { return f >= 0.0f ? 1.0f : -1.0f; }
inline float Clamp(float value, float min, float max)
{
    if (value < min) value = min;
    else if (value > max) value = max;
    return value;
}
inline float Clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}
inline float Lerp(float a, float b, float t)
{
    const double da = static_cast<double>(a);
    return static_cast<float>(da + (static_cast<double>(b) - da) * static_cast<double>(Clamp01(t)));
}
// Mathf.Pow: Math.Pow in doubles.
inline float Pow(float f, float p) { return static_cast<float>(std::pow(static_cast<double>(f), static_cast<double>(p))); }
// Mathf.RoundToInt: Math.Round in doubles, halves to even (the default rounding mode's nearbyint).
inline int RoundToInt(float f) { return static_cast<int>(std::nearbyint(static_cast<double>(f))); }
inline float Floor(float f) { return static_cast<float>(std::floor(static_cast<double>(f))); }
inline float Atan2(float y, float x) { return static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x))); }
inline float Repeat(float t, float length) { return Clamp(t - Floor(t / length) * length, 0.0f, length); }
inline float DeltaAngle(float current, float target)
{
    float delta = Repeat(target - current, 360.0f);
    if (delta > 180.0f) delta -= 360.0f;
    return delta;
}
inline float MoveTowards(float current, float target, float maxDelta)
{
    if (Abs(target - current) <= maxDelta) return target;
    return current + Sign(target - current) * maxDelta;
}
inline float MoveTowardsAngle(float current, float target, float maxDelta)
{
    float deltaAngle = DeltaAngle(current, target);
    if (-maxDelta < deltaAngle && deltaAngle < maxDelta) return target;
    target = current + deltaAngle;
    return MoveTowards(current, target, maxDelta);
}

} // namespace Mathf

// Vector2 members.
inline Vector2 Div(Vector2 a, float d) { return {a.x / d, a.y / d}; } // operator /
inline float SqrMagnitude(Vector2 v)
{
    const double x = static_cast<double>(v.x), y = static_cast<double>(v.y);
    return static_cast<float>(x * x + y * y);
}
// Vector2.magnitude: (float)Math.Sqrt(x * x + y * y): the sum in doubles (the squares exact),
// its root there, rounded to a float once.
inline float MagnitudeOf(float x, float y)
{
    const double dx = static_cast<double>(x), dy = static_cast<double>(y);
    return static_cast<float>(std::sqrt(dx * dx + dy * dy));
}
inline float Magnitude(Vector2 v) { return MagnitudeOf(v.x, v.y); }
inline float Distance(Vector2 a, Vector2 b) { return MagnitudeOf(a.x - b.x, a.y - b.y); }
// Vector2.normalized / Normalize(): divided by the magnitude above kEpsilon (1e-5), else zero.
inline Vector2 Normalized(Vector2 v)
{
    float mag = Magnitude(v);
    if (mag > 1e-05f) return Div(v, mag);
    return Vector2::zero();
}
// Vector2.Lerp (t clamped).
inline Vector2 Lerp(Vector2 a, Vector2 b, float t)
{
    const double d = static_cast<double>(Mathf::Clamp01(t));
    const double ax = static_cast<double>(a.x), ay = static_cast<double>(a.y);
    return {static_cast<float>(ax + (static_cast<double>(b.x) - ax) * d), static_cast<float>(ay + (static_cast<double>(b.y) - ay) * d)};
}
// Vector3.Lerp of two z = 0 vectors, back to a Vector2.
inline Vector2 Lerp3(Vector2 a, Vector2 b, float t) { return Lerp(a, b, t); }
// (Vector2)Vector3.Project(vector, onNormal) of z = 0 vectors.
inline Vector2 Project3(Vector2 vector, Vector2 onNormal)
{
    const double nx = static_cast<double>(onNormal.x), ny = static_cast<double>(onNormal.y);
    const float sqrMag = static_cast<float>(nx * nx + ny * ny + 0.0 * 0.0);
    if (sqrMag < Mathf::Epsilon) return Vector2::zero();
    const float dot = static_cast<float>(static_cast<double>(vector.x) * nx + static_cast<double>(vector.y) * ny + 0.0 * 0.0);
    return {onNormal.x * dot / sqrMag, onNormal.y * dot / sqrMag};
}

} // namespace unity
