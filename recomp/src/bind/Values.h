#pragma once
// UnityEngine's value types as their managed code (UnityEngine.CoreModule, Unity 2019.2) computes
// them on the game's Mono: float expressions in double, rounded where the C# stores a float (a
// field, a local, an argument, a return). Each function below says the C# it follows, as the
// game's own UnityEngine.CoreModule.dll has it: built without optimization, so every local of its
// C# is an IL local (stloc: rounded), Vector3's operator== included; only expressions it never
// stores (Vector2's operator==) stay in double.
// Vector2 is the hand-written layer's (unity::Vector2); Vector3, Quaternion, Bounds here.

#include <cmath>
#include <cstdint>

#include "bind/Core.h"
#include "unity/Mathf.h"
#include "unity/Random.h"
#include "unity/Rect.h"
#include "unity/Vector2.h"

namespace UnityEngine {

using unity::Vector2;

namespace detail {
inline double D(float f) { return static_cast<double>(f); }
inline float F(double d) { return static_cast<float>(d); }
} // namespace detail

constexpr float kEpsilon = 1e-05f;           // Vector2/Vector3.kEpsilon
constexpr float kEpsilonNormalSqrt = 1e-15f; // Vector3.kEpsilonNormalSqrt

// ---- Vector2 ------------------------------------------------------------------------------------

// new Vector2(x, y) * d etc.: one operation per component (no double rounding to model).
inline Vector2 V2Mul(float d, Vector2 a) { return {a.x * d, a.y * d}; }
inline Vector2 V2Div(Vector2 a, float d) { return {a.x / d, a.y / d}; }
inline Vector2 V2Scale(Vector2 a, Vector2 b) { return {a.x * b.x, a.y * b.y}; } // operator*(Vector2, Vector2)
inline Vector2 V2DivV(Vector2 a, Vector2 b) { return {a.x / b.x, a.y / b.y}; }
// magnitude: (float)Math.Sqrt(x * x + y * y)
inline float V2Magnitude(Vector2 v) { return unity::Magnitude(v); }
// sqrMagnitude: x * x + y * y
inline float V2SqrMagnitude(Vector2 v) { return unity::SqrMagnitude(v); }
// normalized: Normalize(); Normalize(): mag > kEpsilon ? this / mag : zero
inline Vector2 V2Normalized(Vector2 v) { return unity::Normalized(v); }
inline void V2Normalize(Vector2& v) { v = unity::Normalized(v); }
// Distance: (float)Math.Sqrt(diff_x * diff_x + diff_y * diff_y)
inline float V2Distance(Vector2 a, Vector2 b) { return unity::Distance(a, b); }
// Dot: lhs.x * rhs.x + lhs.y * rhs.y
inline float V2Dot(Vector2 a, Vector2 b) { return detail::F(detail::D(a.x) * b.x + detail::D(a.y) * b.y); }
// Lerp: t = Clamp01(t); new Vector2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t)
inline Vector2 V2Lerp(Vector2 a, Vector2 b, float t) { return unity::Lerp(a, b, t); }
// LerpUnclamped
inline Vector2 V2LerpUnclamped(Vector2 a, Vector2 b, float t)
{
    using detail::D;
    using detail::F;
    return {F(D(a.x) + (D(b.x) - D(a.x)) * D(t)), F(D(a.y) + (D(b.y) - D(a.y)) * D(t))};
}
// MoveTowards: toVector = target - current; sqdist = x*x + y*y (in double); ...
inline Vector2 V2MoveTowards(Vector2 current, Vector2 target, float maxDistanceDelta)
{
    using detail::D;
    using detail::F;
    const float tx = target.x - current.x, ty = target.y - current.y;
    const float sqdist = F(D(tx) * tx + D(ty) * ty);
    if (sqdist == 0.0f || (maxDistanceDelta >= 0.0f && D(sqdist) <= D(maxDistanceDelta) * maxDistanceDelta)) return target;
    const float dist = F(std::sqrt(D(sqdist)));
    return {F(D(current.x) + D(tx) / dist * maxDistanceDelta), F(D(current.y) + D(ty) / dist * maxDistanceDelta)};
}
// ClampMagnitude (2019.2): float num = sqrMagnitude; if (num > maxLength * maxLength) { float num2 =
// (float)Math.Sqrt(num); float num3 = x / num2; float num4 = y / num2; return (num3 * maxLength,
// num4 * maxLength); } (the root of the rounded square, not magnitude's root of the double sum).
inline Vector2 V2ClampMagnitude(Vector2 v, float maxLength)
{
    using detail::D;
    using detail::F;
    const float sq = V2SqrMagnitude(v);
    if (D(sq) > D(maxLength) * maxLength) {
        const float n = F(std::sqrt(D(sq)));
        const float nx = v.x / n, ny = v.y / n;
        return {nx * maxLength, ny * maxLength};
    }
    return v;
}
inline Vector2 V2Perpendicular(Vector2 v) { return {-v.y, v.x}; }
// Vector2.Angle (2019.2): (float)Math.Sqrt(from.sqrMagnitude * to.sqrMagnitude) below 1e-15: 0;
// else (float)Math.Acos(Clamp(Dot(from, to) / den, -1, 1)) * Rad2Deg.
inline float V2Angle(Vector2 from, Vector2 to)
{
    using detail::D;
    using detail::F;
    const float den = F(std::sqrt(D(V2SqrMagnitude(from)) * D(V2SqrMagnitude(to))));
    if (den < 1e-15f) return 0.0f;
    const float dot = unity::Mathf::Clamp(F(D(V2Dot(from, to)) / D(den)), -1.0f, 1.0f);
    return F(D(F(std::acos(D(dot)))) * D(57.29578f));
}
inline Vector2 V2One() { return {1.0f, 1.0f}; }

// ---- Vector3 ------------------------------------------------------------------------------------

inline Vector3 V3Add(Vector3 a, Vector3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vector3 V3Sub(Vector3 a, Vector3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vector3 V3Neg(Vector3 a) { return {-a.x, -a.y, -a.z}; }
inline Vector3 V3Mul(Vector3 a, float d) { return {a.x * d, a.y * d, a.z * d}; }
inline Vector3 V3MulF(float d, Vector3 a) { return {a.x * d, a.y * d, a.z * d}; }
inline Vector3 V3Div(Vector3 a, float d) { return {a.x / d, a.y / d, a.z / d}; }
inline Vector3 V3Scale(Vector3 a, Vector3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
// operator== (2019.2): float num4 = num * num + num2 * num2 + num3 * num3; return num4 < 9.99999944E-11f.
// Unlike Vector2's, the sum is stored to a local (stloc in UnityEngine.CoreModule's IL): rounded
// to float before the comparison.
inline bool V3Equal(Vector3 a, Vector3 b)
{
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    const float sq = detail::F(detail::D(dx) * dx + detail::D(dy) * dy + detail::D(dz) * dz);
    return sq < 9.99999944e-11f;
}
// magnitude: (float)Math.Sqrt(x * x + y * y + z * z)
inline float V3Magnitude(Vector3 v)
{
    const double x = v.x, y = v.y, z = v.z;
    return static_cast<float>(std::sqrt(x * x + y * y + z * z));
}
inline float V3SqrMagnitude(Vector3 v) { return detail::F(detail::D(v.x) * v.x + detail::D(v.y) * v.y + detail::D(v.z) * v.z); }
// normalized / Normalize(): mag = Magnitude(value); mag > kEpsilon ? value / mag : zero
inline Vector3 V3Normalized(Vector3 v)
{
    const float mag = V3Magnitude(v);
    if (mag > kEpsilon) return V3Div(v, mag);
    return {};
}
inline void V3Normalize(Vector3& v) { v = V3Normalized(v); }
// Distance: (float)Math.Sqrt(diff_x * diff_x + diff_y * diff_y + diff_z * diff_z)
inline float V3Distance(Vector3 a, Vector3 b) { return V3Magnitude(V3Sub(a, b)); }
inline float V3Dot(Vector3 a, Vector3 b) { return detail::F(detail::D(a.x) * b.x + detail::D(a.y) * b.y + detail::D(a.z) * b.z); }
// Lerp: t = Clamp01(t); new Vector3(a.x + (b.x - a.x) * t, ...)
inline Vector3 V3Lerp(Vector3 a, Vector3 b, float t)
{
    using detail::D;
    using detail::F;
    t = unity::Mathf::Clamp01(t);
    return {F(D(a.x) + (D(b.x) - D(a.x)) * D(t)), F(D(a.y) + (D(b.y) - D(a.y)) * D(t)), F(D(a.z) + (D(b.z) - D(a.z)) * D(t))};
}
// Project: sqrMag = Dot(onNormal, onNormal); < Mathf.Epsilon: zero; else
// new Vector3(onNormal.x * dot / sqrMag, ...) with dot = Dot(vector, onNormal) (unity/Mathf.h).
inline Vector3 V3Project(Vector3 vector, Vector3 onNormal)
{
    using detail::D;
    using detail::F;
    const float sqrMag = V3Dot(onNormal, onNormal);
    if (sqrMag < unity::Mathf::Epsilon) return {};
    const float dot = V3Dot(vector, onNormal);
    return {F(D(onNormal.x) * dot / sqrMag), F(D(onNormal.y) * dot / sqrMag), F(D(onNormal.z) * dot / sqrMag)};
}
// MoveTowards (2019.2): as Vector2's, but without its `maxDistanceDelta >= 0` test.
inline Vector3 V3MoveTowards(Vector3 current, Vector3 target, float maxDistanceDelta)
{
    using detail::D;
    using detail::F;
    const float tx = target.x - current.x, ty = target.y - current.y, tz = target.z - current.z;
    const float sqdist = F(D(tx) * tx + D(ty) * ty + D(tz) * tz);
    if (sqdist == 0.0f || D(sqdist) <= D(maxDistanceDelta) * maxDistanceDelta) return target;
    const float dist = F(std::sqrt(D(sqdist)));
    return {F(D(current.x) + D(tx) / dist * maxDistanceDelta), F(D(current.y) + D(ty) / dist * maxDistanceDelta),
            F(D(current.z) + D(tz) / dist * maxDistanceDelta)};
}
// Cross: new Vector3(lhs.y * rhs.z - lhs.z * rhs.y, ...), each component one expression.
inline Vector3 V3Cross(Vector3 a, Vector3 b)
{
    using detail::D;
    using detail::F;
    return {F(D(a.y) * b.z - D(a.z) * b.y), F(D(a.z) * b.x - D(a.x) * b.z), F(D(a.x) * b.y - D(a.y) * b.x)};
}
inline Vector3 V3Zero() { return {}; }
inline Vector3 V3One() { return {1.0f, 1.0f, 1.0f}; }
inline Vector3 V3Up() { return {0.0f, 1.0f, 0.0f}; }
inline Vector3 V3Down() { return {0.0f, -1.0f, 0.0f}; }
inline Vector3 V3Left() { return {-1.0f, 0.0f, 0.0f}; }
inline Vector3 V3Right() { return {1.0f, 0.0f, 0.0f}; }
inline Vector3 V3Forward() { return {0.0f, 0.0f, 1.0f}; }
inline Vector3 V3Back() { return {0.0f, 0.0f, -1.0f}; }

inline void cs_trace_value(cs::Tracer&, const Vector3&) {}
// Vector3.Equals(Vector3) (2019.2): x == other.x && y == other.y && z == other.z.
inline bool cs_box_equals(const Vector3& a, const Vector3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
inline int32_t cs_box_hash(const Vector3& v)
{
    return cs::HashOf(v.x) ^ static_cast<int32_t>(static_cast<uint32_t>(cs::HashOf(v.y)) << 2) ^ (cs::HashOf(v.z) >> 2);
}
inline const char* cs_box_typename(const Vector3*) { return "UnityEngine.Vector3"; }

// ---- Quaternion (rotations about z are what the 2D game makes) -------------------------------

struct Quaternion {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
    void __ctor(float x_, float y_, float z_, float w_)
    {
        x = x_;
        y = y_;
        z = z_;
        w = w_;
    }
};
inline Quaternion QIdentity() { return {0.0f, 0.0f, 0.0f, 1.0f}; }
// Quaternion * Vector3 (Quaternion.operator*): the managed formula, its locals rounded.
inline Vector3 QRotate(Quaternion r, Vector3 p)
{
    using detail::D;
    using detail::F;
    const float num = r.x * 2.0f, num2 = r.y * 2.0f, num3 = r.z * 2.0f;
    const float num4 = r.x * num, num5 = r.y * num2, num6 = r.z * num3;
    const float num7 = r.x * num2, num8 = r.x * num3, num9 = r.y * num3;
    const float num10 = r.w * num, num11 = r.w * num2, num12 = r.w * num3;
    Vector3 res;
    res.x = F((1.0 - (D(num5) + num6)) * p.x + (D(num7) - num12) * p.y + (D(num8) + num11) * p.z);
    res.y = F((D(num7) + num12) * p.x + (1.0 - (D(num4) + num6)) * p.y + (D(num9) - num10) * p.z);
    res.z = F((D(num8) - num11) * p.x + (D(num9) + num10) * p.y + (1.0 - (D(num4) + num5)) * p.z);
    return res;
}
inline bool QIsIdentity(Quaternion q) { return q.x == 0.0f && q.y == 0.0f && q.z == 0.0f && q.w == 1.0f; }
inline void cs_trace_value(cs::Tracer&, const Quaternion&) {}
inline const char* cs_box_typename(const Quaternion*) { return "UnityEngine.Quaternion"; }

// ---- LayerMask ------------------------------------------------------------------------------------

struct LayerMask {
    int32_t m_Mask = 0;
    static int32_t op_Implicit_int(LayerMask m) { return m.m_Mask; }
    static LayerMask op_Implicit_LayerMask(int32_t v)
    {
        LayerMask m;
        m.m_Mask = v;
        return m;
    }
    int32_t get_value() const { return m_Mask; }
    void set_value(int32_t v) { m_Mask = v; }
    // LayerMask.NameToLayer: the project's layers (TagManager), -1 for an unknown name.
    static int32_t NameToLayer(cs::String* name);
    static cs::String* LayerToName(int32_t layer);
};
inline void cs_trace_value(cs::Tracer&, const LayerMask&) {}

// ---- Color ------------------------------------------------------------------------------------------

inline Color ColorOf(float r, float g, float b, float a)
{
    Color c;
    c.__ctor(r, g, b, a);
    return c;
}
inline void cs_trace_value(cs::Tracer&, const Color&) {}
inline bool cs_box_equals(const Color& a, const Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }
inline const char* cs_box_typename(const Color*) { return "UnityEngine.Color"; }

// ---- Rect (unity/Rect.h's fields) -------------------------------------------------------------------

// xMax / yMax: m_Width + m_XMin (a float return: rounded once).
inline float RectXMax(const unity::Rect& r) { return r.width + r.x; }
inline float RectYMax(const unity::Rect& r) { return r.height + r.y; }
// Contains(Vector3 / Vector2): xMin <= x < xMax and yMin <= y < yMax.
inline bool RectContains(const unity::Rect& r, Vector3 p) { return p.x >= r.x && p.x < RectXMax(r) && p.y >= r.y && p.y < RectYMax(r); }
inline bool RectContains(const unity::Rect& r, Vector2 p) { return p.x >= r.x && p.x < RectXMax(r) && p.y >= r.y && p.y < RectYMax(r); }

// ---- Random's vectors ------------------------------------------------------------------------------

inline Vector2 RandomInsideUnitCircle() { return unity::Random::insideUnitCircle(); }
inline Vector3 RandomInsideUnitSphere()
{
    const unity::Random::Vector3 v = unity::Random::insideUnitSphere();
    return {v.x, v.y, v.z};
}

} // namespace UnityEngine
