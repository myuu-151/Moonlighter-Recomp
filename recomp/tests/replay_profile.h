#pragma once
// Where an enemy replay's time goes, phase by phase: the game's own work in each frame (the physics,
// the scripts' Updates, the Animators...), not the replay's reading and comparing around it
// (tests/enemy_replay.h, tests/translated/enemy_translated_test.cpp). Off unless a clock is set:
// MLGC's test builds (TTEST=1) set the Gekko's timebase (MLGC's Source/TranslatedTest.cpp); on the
// PC, ML_REPLAY_PROFILE=1 sets std::chrono's. When on, the replay prints the table after its rows.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace replay_profile {

enum Phase { Physics, FixedSteps, Tweens, Updates, Delayed, Animators, LateUpdates, Destroys, Collector, kPhases };

inline const char* const kNames[kPhases] = {
    "physics (Box2D step, trigger messages)",
    "fixed steps' game code (hits, pushes, Invokes)",
    "DOTween",
    "the enemies' Updates",
    "Invoke, coroutines",
    "Animators",
    "LateUpdates (PolyNav)",
    "destroys",
    "collector (mark-sweep)",
};

struct State {
    uint64_t (*clock)() = nullptr; // ticks
    double ticksPerMicrosecond = 1.0;
    const char* clockName = "";
    void (*frameEnd)() = nullptr; // after each frame's phases (MLGC: the memory)
    // At each phase's start and end, outside its time (MLGC: the collector's memory, the sampler's phase).
    void (*phaseHook)(int phase, bool begin) = nullptr;
    uint64_t frame[kPhases] = {}, total[kPhases] = {}, max[kPhases] = {}, calls[kPhases] = {};
    uint64_t worstTotal = 0;
    int frames = 0, worstFrame = -1;
};
inline State gState;

inline bool On() { return gState.clock != nullptr; }

// A phase's time from here to the end of the scope.
class Scope {
public:
    explicit Scope(Phase p) : p_(p)
    {
        if (gState.phaseHook) gState.phaseHook(p_, true);
        t0_ = gState.clock ? gState.clock() : 0;
    }
    ~Scope()
    {
        if (gState.clock) {
            gState.frame[p_] += gState.clock() - t0_;
            gState.calls[p_]++;
        }
        if (gState.phaseHook) gState.phaseHook(p_, false);
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Phase p_;
    uint64_t t0_;
};

// A replay's start: the counts back to zero (the clock and the hooks stay).
inline void Reset()
{
    State s;
    s.clock = gState.clock;
    s.ticksPerMicrosecond = gState.ticksPerMicrosecond;
    s.clockName = gState.clockName;
    s.frameEnd = gState.frameEnd;
    s.phaseHook = gState.phaseHook;
    gState = s;
}

inline void EndFrame()
{
    if (!gState.clock) return;
    uint64_t sum = 0;
    for (int p = 0; p < kPhases; p++) {
        gState.total[p] += gState.frame[p];
        if (gState.frame[p] > gState.max[p]) gState.max[p] = gState.frame[p];
        sum += gState.frame[p];
        gState.frame[p] = 0;
    }
    if (sum > gState.worstTotal) {
        gState.worstTotal = sum;
        gState.worstFrame = gState.frames;
    }
    gState.frames++;
    if (gState.frameEnd) gState.frameEnd();
}

inline uint64_t ChronoClock()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// ML_REPLAY_PROFILE=1 (the PC): std::chrono's clock.
inline void FromEnv()
{
    if (gState.clock || !std::getenv("ML_REPLAY_PROFILE")) return;
    gState.clock = &ChronoClock;
    gState.ticksPerMicrosecond = 1000.0;
    gState.clockName = "std::chrono";
}

inline void Print(const char* what)
{
    if (!gState.clock || gState.frames == 0) return;
    const double us = 1.0 / gState.ticksPerMicrosecond, n = static_cast<double>(gState.frames);
    std::printf("\ntime per frame, %s (%d frames, %s):\n", what, gState.frames, gState.clockName);
    std::printf("  %-48s %10s %10s %8s\n", "phase", "mean us", "max us", "calls");
    uint64_t all = 0;
    for (int p = 0; p < kPhases; p++) {
        if (!gState.calls[p]) continue;
        all += gState.total[p];
        std::printf("  %-48s %10.1f %10.1f %8llu\n", kNames[p], static_cast<double>(gState.total[p]) * us / n,
                    static_cast<double>(gState.max[p]) * us, static_cast<unsigned long long>(gState.calls[p]));
    }
    std::printf("  %-48s %10.1f %10.1f   (the slowest: frame %d of the replay)\n", "all of them", static_cast<double>(all) * us / n,
                static_cast<double>(gState.worstTotal) * us, gState.worstFrame);
    // (the raw ticks, for tools: phase, total, max, calls)
    for (int p = 0; p < kPhases; p++)
        if (gState.calls[p])
            std::printf("PROFILE %s %d %llu %llu %llu %d\n", what, p, static_cast<unsigned long long>(gState.total[p]),
                        static_cast<unsigned long long>(gState.max[p]), static_cast<unsigned long long>(gState.calls[p]), gState.frames);
}

} // namespace replay_profile
