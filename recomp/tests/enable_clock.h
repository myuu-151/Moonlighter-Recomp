#pragma once
// When the room's EnableEnemies ran, for the enemy replays (enemy_replay.h, tangle_test): the
// fixed steps the enemies' bodies took before the recording's first, and Time.fixedTime then,
// which the targeting's Invoke (StartTargetingPlayerAfterTime, a second on) counts from as every
// Invoke does (unity/DelayedCalls.h; the replays set DelayedCalls::fixedNow to it).
//
// EnableEnemies is itself an Invoke (half a second after EnterRoom). The recording has its
// Time.time ("enableTime") and frame; the fixed steps are on a grid the replay runs back from
// the first recorded one, a float fixedDeltaTime (0.0099999998) per step, as a double (the
// engine's fixed clock). What the recordings of 2026-10-12 22:00 show (enemy_GolemSoldier,
// enemy_HermitSlime, slime, tangle; every one fits):
//   - the bodies are switched on after the last fixed step at or before that Time.time: they
//     step from the next one on (enemy_GolemSoldier.jsonl, tangle.jsonl: asleep in the step
//     where 51 steps of rest add up to Box2D's half second; one fewer and they'd sleep a step
//     late);
//   - the targeting's Invoke counts from that step's fixed time, not from the Time.time
//     EnableEnemies read: a second on is a hair past the hundredth step after it, so it starts
//     in the frame's pass after that step. enemy_GolemSoldier.jsonl (Time.time 178.269150 at
//     EnableEnemies, the last step 178.26) starts targeting in the frame at 179.260971, which a
//     second after 178.269150 isn't yet due in; tangle.jsonl the same (77.345261, 77.34: the
//     frame at 78.343430). When EnableEnemies ran in a step's pass the two are the same clock.
// Not settled by these recordings: why Time.time and the clock the Invoke was made on differ (an
// EnableEnemies run in the frame at 178.269150 should count from there). Earlier recordings
// (replaced since) had a frame-made targeting start in a fixed step's pass; this rule gives that
// too when no frame comes between the fixed step a second on and the one after it, and whether
// it fit them can no longer be checked.
// One explanation the recordings can't rule out: EnableEnemies ran twice (the oracle keeps the
// last one's Time.time), the targeting counting from the first. The oracle's EnableClockProbe
// settles it: the start line's "enableClock" has the clocks at the first EnableEnemies ("enable":
// time, fixedTime, inFixedStep) and two Invokes made there; the second, due fixedDeltaTime less
// half the gap from Time.fixedTime to Time.time later, runs in the pass after the next fixed step
// only if Invokes count from the fixed clock. With it, the replay takes those clocks and checks
// that answer against unity/DelayedCalls.h's rule on a row of its own (a recording that
// contradicts it fails there, rather than the targeting alone counting from another clock);
// without it, the rule above.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "util/Json.h"

namespace {

struct EnableClock {
    bool known = false;
    double time = 0.0;      // Time.time at EnableEnemies
    double fixedTime = 0.0; // the last fixed step at or before it
    int stepsBefore = 0;    // fixed steps after it and before the first recorded one
    // The probe's answer, where it gave one (probed: EnableEnemies ran in a frame and the probe's
    // second Invoke ran): an Invoke made in a frame counts from fixedTime (unity/DelayedCalls.h's
    // rule), else from time.
    bool probed = false;
    bool invokeOnFixedClock = true;
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
    float enabled = EnableClockBits(*et);
    // The oracle's probe, if the recording has it (EnableClockProbe.cs): the first EnableEnemies.
    const json::Value* probe = start.Find("enableClock");
    const json::Value* at = probe && probe->Find("enable") && !(*probe)["enable"].IsNull() ? &(*probe)["enable"] : nullptr;
    if (at) {
        const float first = EnableClockBits((*at)["time"]);
        if (first != enabled)
            std::printf("note: EnableEnemies ran more than once: first at %.7f, last at %.7f (the bodies and the targeting count from the first)\n",
                        static_cast<double>(first), static_cast<double>(enabled));
        enabled = first;
    }
    c.time = static_cast<double>(enabled);
    if (firstFrame["steps"].arr.empty()) {
        // (No step to run the grid back from: the frame's clock.)
        c.known = true;
        c.fixedTime = c.time;
        return c;
    }
    const double first = static_cast<double>(EnableClockBits(firstFrame["steps"][0]["ft"]));
    for (int k = 1; k < 100000; k++) {
        const double step = first - static_cast<double>(k) * static_cast<double>(fixedDt);
        if (static_cast<float>(step) <= enabled) {
            c.known = true;
            c.fixedTime = step;
            c.stepsBefore = k - 1;
            break;
        }
    }
    if (!c.known) return c;
    const char* how = "the rule the recordings fit";
    if (at) {
        if ((*at)["inFixedStep"].b) {
            how = "it ran in a fixed step's pass";
            c.invokeOnFixedClock = true;
        } else if (const json::Value* probes = probe->Find("probes"); probes && probes->size() >= 2 && !(*probes)[1]["fired"].IsNull()) {
            // Due fixedDeltaTime less half the gap after the first EnableEnemies: in the pass after
            // the next fixed step only if it counted from the fixed clock.
            const json::Value& fired = (*probes)[1]["fired"];
            c.probed = true;
            c.invokeOnFixedClock = fired["inFixedStep"].b && EnableClockBits(fired["fixedTime"]) == static_cast<float>(c.fixedTime + static_cast<double>(fixedDt));
            how = c.invokeOnFixedClock ? "the probe ran in the next fixed step's pass" : "the probe didn't run in the next fixed step's pass";
        } else
            how = "the probe ran after the recording started: the rule the recordings fit";
    }
    std::printf("EnableEnemies at %.7f: after the fixed step at %.7f (%s), %d steps before the first recorded; its Invokes count from %s (%s)\n",
                c.time, c.fixedTime, static_cast<float>(c.fixedTime) == enabled ? "in its pass" : "in a frame", c.stepsBefore,
                c.invokeOnFixedClock ? "that step" : "that step all the same, which the probe contradicts: a failed row", how);
    return c;
}

} // namespace
