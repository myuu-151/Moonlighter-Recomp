#pragma once
// When the room's EnableEnemies ran, for the enemy replays (enemy_replay.h, tangle_test): the
// fixed steps the enemies' bodies took before the recording's first, and the clock the
// targeting's Invoke (StartTargetingPlayerAfterTime, a second on) counts from.
//
// EnableEnemies is itself an Invoke (half a second after EnterRoom). The recording has its
// Time.time ("enableTime") and frame; the fixed steps are on a grid the replay runs back from
// the first recorded one, a float fixedDeltaTime (0.0099999998) per step, as a double (the
// engine's fixed clock). What the recordings show:
//   - the bodies are switched on after the last fixed step at or before that Time.time: they
//     step from the next one on (enemy_GolemSoldier.jsonl, tangle.jsonl: asleep in the step
//     where 51 steps of rest add up to Box2D's half second; one fewer and they'd sleep a step late);
//   - the targeting's Invoke counts from that step's fixed time, not from the Time.time
//     EnableEnemies read: a second on is a hair past the hundredth step after it, so it starts
//     in the frame's pass after that step. enemy_GolemSoldier.jsonl (Time.time 178.269150 at
//     EnableEnemies, the last step 178.26) starts targeting in the frame at 179.260971, which a
//     second after 178.269150 isn't yet due in; tangle.jsonl the same (77.345261, 77.34: the
//     frame at 78.343430). When EnableEnemies ran in a step's pass the two are the same clock
//     (Time.time was that step's: the old slime.jsonl).
// Not settled by these recordings: why Time.time and the clock the Invoke was made on differ
// (an EnableEnemies run in the frame at 178.269150 should count from there). The oracle now
// records the fixed clock at EnableEnemies and an Invoke probe made beside it (enableFixedTime,
// enableProbe; see EnemyOracle.EnableHook) that settles it; until a recording has them this is
// the rule every recording fits. (Earlier recordings, now replaced, had a frame-made targeting
// start in a fixed step, which this rule can't give.)

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "util/Json.h"

namespace {

struct EnableClock {
    bool known = false;
    double time = 0.0;      // Time.time at EnableEnemies
    double fixedTime = 0.0; // the last fixed step at or before it (the targeting's clock)
    int stepsBefore = 0;    // fixed steps after it and before the first recorded one
};

inline float EnableClockBits(const json::Value& v)
{
    int32_t i = static_cast<int32_t>(v.AsInt());
    float f;
    std::memcpy(&f, &i, 4);
    return f;
}

inline EnableClock EnableEnemiesClock(const json::Value& start, const json::Value& firstFrame, float fixedDt)
{
    EnableClock c;
    const json::Value* et = start.Find("enableTime");
    if (!et || et->IsNull()) return c;
    const float enabled = EnableClockBits(*et);
    c.time = static_cast<double>(enabled);
    if (firstFrame["steps"].arr.empty()) {
        // (No step to run the grid back from: the frame's clock.)
        c.known = true;
        c.fixedTime = c.time;
        return c;
    }
    const double first = static_cast<double>(EnableClockBits(firstFrame["steps"][0]["ft"]));
    for (int k = 1; k < 100000; k++) {
        const double at = first - static_cast<double>(k) * static_cast<double>(fixedDt);
        if (static_cast<float>(at) <= enabled) {
            c.known = true;
            c.fixedTime = at;
            c.stepsBefore = k - 1;
            std::printf("EnableEnemies at %.7f: after the fixed step at %.7f (%s), %d steps before the first recorded\n", c.time, at,
                        static_cast<float>(at) == enabled ? "in its pass" : "in a frame", k - 1);
            return c;
        }
    }
    return c;
}

} // namespace
