#include "unity/Random.h"

#include <cmath>

namespace unity {
namespace Random {

namespace {
State gState = {0, 1, 1812433254u, 1900727103u}; // InitState(0)

uint32_t Step(State& s)
{
    uint32_t t = s.s0 ^ (s.s0 << 11);
    s.s0 = s.s1;
    s.s1 = s.s2;
    s.s2 = s.s3;
    s.s3 = s.s3 ^ (s.s3 >> 19) ^ t ^ (t >> 8);
    return s.s3;
}

uint32_t NextU32() { return Step(gState); }

bool Same(const State& a, const State& b) { return a.s0 == b.s0 && a.s1 == b.s1 && a.s2 == b.s2 && a.s3 == b.s3; }

// cos and sin of a float, as the engine (MSVC's CRT, x64) rounds them. MSVC: its cosf and sinf
// (the PC port matches the game's probes with them). Elsewhere the double functions rounded to
// float: newlib's cosf and sinf (the GameCube) are a single-precision kernel that misses by an ulp
// often (2 of slime.jsonl's 16 probes), the double route agrees with MSVC's on all but about 6 in
// 100,000 floats of [0, 2 pi] (both are within an ulp; they part only near a rounding midpoint).
inline float CosF(float a)
{
#if defined(_MSC_VER)
    return std::cos(a);
#else
    return static_cast<float>(std::cos(static_cast<double>(a)));
#endif
}
inline float SinF(float a)
{
#if defined(_MSC_VER)
    return std::sin(a);
#else
    return static_cast<float>(std::sin(static_cast<double>(a)));
#endif
}
} // namespace

int DrawsBetween(State from, const State& to, int maxDraws)
{
    for (int n = 0; n <= maxDraws; n++) {
        if (Same(from, to)) return n;
        Step(from);
    }
    return -1;
}

void InitState(int32_t seed)
{
    gState.s0 = static_cast<uint32_t>(seed);
    gState.s1 = gState.s0 * 1812433253u + 1u;
    gState.s2 = gState.s1 * 1812433253u + 1u;
    gState.s3 = gState.s2 * 1812433253u + 1u;
}

State GetState() { return gState; }

void SetState(const State& s) { gState = s; }

float value()
{
    // 23 random bits over 2^23-1: both ends inclusive.
    float bits = static_cast<float>(NextU32() & 0x7FFFFFu);
    return bits / 8388607.0f;
}

float Range(float min, float max)
{
    // Note the order: Unity weights min by t, not max.
    float t = value();
    float a = t * min;
    float b = (1.0f - t) * max;
    return a + b;
}

int32_t Range(int32_t min, int32_t max)
{
    if (min < max) {
        uint32_t span = static_cast<uint32_t>(max) - static_cast<uint32_t>(min);
        return static_cast<int32_t>(static_cast<uint32_t>(min) + NextU32() % span);
    }
    if (min > max) {
        uint32_t span = static_cast<uint32_t>(min) - static_cast<uint32_t>(max);
        return static_cast<int32_t>(static_cast<uint32_t>(min) - NextU32() % span);
    }
    return min;
}

Vector2 insideUnitCircle()
{
    const float angle = Range(0.0f, 3.14159274f * 2.0f); // kPI * 2
    Vector2 v{CosF(angle), SinF(angle)};
    const float r = std::sqrt(Range(0.0f, 1.0f));
    return {v.x * r, v.y * r};
}

Vector3 insideUnitSphere()
{
    const float z = Range(-1.0f, 1.0f);
    const float a = Range(0.0f, 3.14159274f * 2.0f);
    const float r = std::sqrt(1.0f - z * z);
    Vector3 v;
    v.x = r * CosF(a);
    v.y = r * SinF(a);
    v.z = z;
    const float k = std::pow(value(), 1.0f / 3.0f);
    v.x *= k;
    v.y *= k;
    v.z *= k;
    return v;
}

} // namespace Random
} // namespace unity
