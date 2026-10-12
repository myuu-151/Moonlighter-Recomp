#include "unity/Random.h"

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

} // namespace Random
} // namespace unity
