#pragma once
// UnityEngine.Mathf and the Vector2/Vector3 members the ported code uses, as Unity 2019.2's
// managed code computes them on the game's Mono.
//
// Mono's JIT there evaluates float arithmetic in double: the operands of one C# expression are
// widened, and the value is rounded to float only where it's stored as one (a float local, a
// field, a return value, a float argument). Measured: Vector3.Project's
// `onNormal.x * dot / sqrMag` rounded once matches all 2772 GetCloserEdgePoint probes of the
// PolyNav recording, rounding the product first misses 48; rounding nothing in between (the
// locals kept in double) misses 35 of those. A single operation of two floats comes out the
// same either way, so only expressions of several operations need the double: they're
// written as `F(D(a) * D(b) + ...)` (D widens, F rounds) where the C# has them.
// Vector3 versions of 2D values carry z = 0.

#include <cmath>

#include "unity/Vector2.h"

namespace unity {

namespace mono {
inline double D(float f) { return static_cast<double>(f); }
inline float F(double d) { return static_cast<float>(d); }
} // namespace mono

namespace Mathf {

using mono::D;
using mono::F;

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
// a + (b - a) * Clamp01(t)
inline float Lerp(float a, float b, float t) { return F(D(a) + (D(b) - D(a)) * D(Clamp01(t))); }
inline float Floor(float f) { return F(std::floor(D(f))); }
inline float Atan2(float y, float x) { return F(std::atan2(D(y), D(x))); }
// Clamp(t - Floor(t / length) * length, 0, length)
inline float Repeat(float t, float length) { return Clamp(F(D(t) - D(Floor(t / length)) * D(length)), 0.0f, length); }
// Mathf.Pow: Math.Pow in doubles.
inline float Pow(float f, float p) { return static_cast<float>(std::pow(static_cast<double>(f), static_cast<double>(p))); }
// Mathf.RoundToInt: Math.Round in doubles, halves to even (the default rounding mode's nearbyint).
inline int RoundToInt(float f) { return static_cast<int>(std::nearbyint(static_cast<double>(f))); }
inline float DeltaAngle(float current, float target)
{
    float delta = Repeat(target - current, 360.0f);
    if (delta > 180.0f) delta -= 360.0f;
    return delta;
}
inline float MoveTowards(float current, float target, float maxDelta)
{
    if (Abs(target - current) <= maxDelta) return target;
    return F(D(current) + D(Sign(target - current)) * D(maxDelta));
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
// x * x + y * y
inline float SqrMagnitude(Vector2 v)
{
    using mono::D;
    using mono::F;
    return F(D(v.x) * D(v.x) + D(v.y) * D(v.y));
}
// (float)Math.Sqrt(x * x + y * y)
inline float Magnitude(Vector2 v)
{
    using mono::D;
    using mono::F;
    return F(std::sqrt(D(v.x) * D(v.x) + D(v.y) * D(v.y)));
}
// float diff_x = a.x - b.x, diff_y = ...; (float)Math.Sqrt(diff_x * diff_x + diff_y * diff_y)
inline float Distance(Vector2 a, Vector2 b)
{
    using mono::D;
    using mono::F;
    float dx = a.x - b.x, dy = a.y - b.y;
    return F(std::sqrt(D(dx) * D(dx) + D(dy) * D(dy)));
}
// Vector2.normalized / Normalize(): divided by the magnitude above kEpsilon (1e-5), else zero.
inline Vector2 Normalized(Vector2 v)
{
    float mag = Magnitude(v);
    if (mag > 1e-05f) return Div(v, mag);
    return Vector2::zero();
}
// Vector2.Lerp: new Vector2(a.x + (b.x - a.x) * t, ...), t clamped.
inline Vector2 Lerp(Vector2 a, Vector2 b, float t)
{
    using mono::D;
    using mono::F;
    t = Mathf::Clamp01(t);
    return {F(D(a.x) + (D(b.x) - D(a.x)) * D(t)), F(D(a.y) + (D(b.y) - D(a.y)) * D(t))};
}
// Vector3.Lerp of two z = 0 vectors, back to a Vector2 (the same expression per component).
inline Vector2 Lerp3(Vector2 a, Vector2 b, float t) { return Lerp(a, b, t); }
// (Vector2)Vector3.Project(vector, onNormal) of z = 0 vectors:
//     float sqrMag = Dot(onNormal, onNormal); ... float dot = Dot(vector, onNormal);
//     return new Vector3(onNormal.x * dot / sqrMag, ...);
// (Dot's own sum in double or float: the recording can't tell; double, as the model says.)
inline Vector2 Project3(Vector2 vector, Vector2 onNormal)
{
    using mono::D;
    using mono::F;
    const float sqrMag = F(D(onNormal.x) * D(onNormal.x) + D(onNormal.y) * D(onNormal.y) + 0.0);
    if (sqrMag < Mathf::Epsilon) return Vector2::zero();
    const float dot = F(D(vector.x) * D(onNormal.x) + D(vector.y) * D(onNormal.y) + 0.0);
    return {F(D(onNormal.x) * D(dot) / D(sqrMag)), F(D(onNormal.y) * D(dot) / D(sqrMag))};
}

} // namespace unity
