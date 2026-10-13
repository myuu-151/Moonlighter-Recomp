#pragma once
// Unity's DelayedCallManager: what MonoBehaviour.Invoke / InvokeRepeating / CancelInvoke, a
// coroutine's `yield return new WaitForSeconds(t)` and Object.Destroy(obj, t) schedule, as the
// game shows them (data/oracle/slime.jsonl, a slime killed by a hit inside a fixed step):
//   - Invoke counts from the fixed clock (Time.fixedTime: the last fixed step's time), wherever
//     it's made, an Update or the frame's passes included (the recordings of 2026-10-12/13: a
//     targeting Invoke(1 s) from an EnableEnemies run in a frame starts in the pass of the frame
//     after the 100th step since, where Time.time + 1 isn't reached yet: enemy_GolemSoldier.jsonl
//     frame 36, tangle.jsonl frame 47; the FlyingKamikazeGolem's EnableCharge, Invoked from a
//     tween's OnComplete in the frame, runs in the first step past fixedTime + delay, a step
//     before Time.time + delay: enemy_FlyingKamikazeGolem.jsonl frame 445). Destroy(obj, t)
//     counts from the clock of the moment it's made: Time.time in an Update, the fixed clock
//     inside a fixed step (a physics callback; none made in a frame is recorded). Wherever it
//     was made, one runs in the pass after a fixed step (at Time.fixedTime, before the frame's
//     DOTween update: a flash colour set there is gone by the frame's end) if that finds it due
//     before the frame's pass after the Updates does. The fixed clock runs on by the float
//     fixedDeltaTime (0.0099999998): a second after a fixed step is a hair past the hundredth
//     step after it (slime.jsonl: the targeting a second after an EnableEnemies starts in the
//     frame's pass, not in that step);
//   - a coroutine's WaitForSeconds counts from the frame's Time.time even when it's started in
//     a fixed step, and resumes in the frame's pass only.
// (The replays make the targeting an EnableEnemies started before the recording with fixedNow at
// the last fixed step at or before its Time.time: recomp/tests/enable_clock.h. The oracle's
// EnableClockProbe tests the rule on a frame-made Invoke of its own; a recording with it checks it
// on a row.)
// Due ones run earliest first, ties in the order they were made; one made during a pass waits
// for the next pass even if it's already due. Times are doubles, as the engine keeps them; the
// driver sets the clocks (now, fixedNow, inFixedStep).

#include <functional>
#include <string>
#include <vector>

namespace unity {

class DelayedCalls {
public:
    double now = 0.0;         // the frame's Time.time
    double fixedNow = 0.0;    // Time.fixedTime: the fixed step running, or the last one (Invoke counts from it)
    bool inFixedStep = false; // a Destroy made now counts from fixedNow

    // Invoke(name, delay) on `owner` (a behaviour); repeatRate > 0: InvokeRepeating.
    void Invoke(const void* owner, const std::string& name, float delay, std::function<void()> fn, float repeatRate = 0.0f);
    // CancelInvoke(name) on `owner`; "" cancels all of its invokes (CancelInvoke()).
    void CancelInvoke(const void* owner, const std::string& name);
    bool IsInvoking(const void* owner, const std::string& name) const;
    // A coroutine waiting on WaitForSeconds: `resume` once due. Not cancelled by CancelInvoke.
    void Wait(const void* owner, float seconds, std::function<void()> resume);
    // Object.Destroy(obj, t): `destroy` once due.
    void Destroy(const void* owner, float seconds, std::function<void()> destroy);
    // Object.Destroy(obj) (no delay): `destroy` at the frame's end, after the LateUpdates (the
    // engine's delayed pass after them; EndOfFrame runs these). Forget drops it too.
    void DestroyAtFrameEnd(const void* owner, std::function<void()> destroy);
    void EndOfFrame();
    // StopAllCoroutines (the object was deactivated): its waits go, its invokes stay.
    void StopCoroutines(const void* owner);
    // The owner is gone (destroyed): all of its calls go.
    void Forget(const void* owner);

    // The pass after a fixed step, at fixedTime (Invoke and Destroy only).
    void UpdateFixed(double fixedTime);
    // The frame's pass at `time` (Time.time).
    void Update(double time);
    bool Empty() const { return calls_.empty(); }

private:
    enum class Kind { Invoke, Coroutine, Destroy };
    struct Call {
        double time = 0.0;
        long long seq = 0;
        const void* owner = nullptr;
        std::string name;
        Kind kind = Kind::Invoke;
        std::function<void()> fn;
        float repeat = 0.0f;
        long long pass = -1; // the pass it was made in (-1: between passes)
        bool fixedPass = false; // made inside a fixed step: the fixed passes run it too
        bool cancelled = false;
        bool frameEnd = false; // DestroyAtFrameEnd's
    };
    void Add(const void* owner, const std::string& name, Kind kind, double base, float delay, std::function<void()> fn, float repeat);
    void Pass(double time, bool fixedPass);
    std::vector<Call> calls_;
    long long seq_ = 0, pass_ = 0;
    bool inPass_ = false;
};

} // namespace unity
