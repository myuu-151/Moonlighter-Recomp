#pragma once
// UnityEngine.Random: one global xorshift128 stream.
// Verified bit-exact against the game (data/oracle/rng.jsonl): seeding, value, and both
// Range overloads, including how many draws each call consumes.
// Must be compiled without FMA contraction (-ffp-contract=off), or Range(float) drifts.

#include <cstdint>

namespace unity {
namespace Random {

struct State {
    uint32_t s0, s1, s2, s3;
};

void InitState(int32_t seed);
State GetState();
void SetState(const State& s);

// Number of draws that take `from` to `to` (each value/Range call is one draw), or -1 if
// `to` isn't reached within `maxDraws`. Used to measure RNG use the port can't see.
int DrawsBetween(State from, const State& to, int maxDraws);

float value();
float Range(float min, float max);
int32_t Range(int32_t min, int32_t max);

} // namespace Random
} // namespace unity
