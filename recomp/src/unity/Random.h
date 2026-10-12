#pragma once
// UnityEngine.Random: one global xorshift128 stream.
// Verified bit-exact against the game (data/oracle/rng.jsonl): seeding, value, and both
// Range overloads, including how many draws each call consumes.
// Must be compiled without FMA contraction (-ffp-contract=off), or Range(float) drifts.

#include <cstdint>

#include "unity/Vector2.h"

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

// Random.insideUnitCircle: two draws, as the engine's RandomPointInsideUnitCircle makes it:
// an angle Range(0, 2 pi) to the unit circle (cos, sin in floats), then scaled by
// sqrt(Range(0, 1)). Not yet checked against the game (the slime oracle's "rngProbe" lines
// hold its results; slime_test compares them). cos and sin are the C library's, as the
// engine's are its compiler's: a platform whose sinf/cosf round differently drifts here.
Vector2 insideUnitCircle();

} // namespace Random
} // namespace unity
