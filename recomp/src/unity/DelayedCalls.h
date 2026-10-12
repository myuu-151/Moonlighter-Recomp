#pragma once
// Unity's DelayedCallManager: what MonoBehaviour.Invoke / InvokeRepeating / CancelInvoke, a
// coroutine's `yield return new WaitForSeconds(t)` and Object.Destroy(obj, t) schedule, as the
// game shows them (data/oracle/slime.jsonl, a slime killed by a hit inside a fixed step):
//   - Invoke and Destroy(obj, t) count from the clock of the moment they're made: Time.time in
//     an Update, the fixed clock inside a fixed step (a physics callback). Wherever it was
//     made, one runs in the pass after a fixed step (at Time.fixedTime, before the frame's
//     DOTween update: a flash colour set there is gone by the frame's end) if that finds it due
//     before the frame's pass after the Updates does (tangle.jsonl, slime_bis.jsonl: the
//     targeting a second after an EnableEnemies run in a frame starts in a fixed step, before
//     the Updates). The fixed clock runs on by the float fixedDeltaTime (0.0099999998): a
//     second after a fixed step is a hair past the hundredth step after it (slime.jsonl: the
//     targeting a second after an EnableEnemies run in a fixed step's pass starts in the frame);
//   - a coroutine's WaitForSeconds counts from the frame's Time.time even when it's started in
//     a fixed step, and resumes in the frame's pass only.
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
    double fixedNow = 0.0;    // Time.fixedTime of the fixed step running
    bool inFixedStep = false; // calls made now count from fixedNow (Invoke, Destroy)

    // Invoke(name, delay) on `owner` (a behaviour); repeatRate > 0: InvokeRepeating.
    void Invoke(const void* owner, const std::string& name, float delay, std::function<void()> fn, float repeatRate = 0.0f);
    // CancelInvoke(name) on `owner`; "" cancels all of its invokes (CancelInvoke()).
    void CancelInvoke(const void* owner, const std::string& name);
    bool IsInvoking(const void* owner, const std::string& name) const;
    // A coroutine waiting on WaitForSeconds: `resume` once due. Not cancelled by CancelInvoke.
    void Wait(const void* owner, float seconds, std::function<void()> resume);
    // Object.Destroy(obj, t): `destroy` once due.
    void Destroy(const void* owner, float seconds, std::function<void()> destroy);
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
    };
    void Add(const void* owner, const std::string& name, Kind kind, double base, float delay, std::function<void()> fn, float repeat);
    void Pass(double time, bool fixedPass);
    std::vector<Call> calls_;
    long long seq_ = 0, pass_ = 0;
    bool inPass_ = false;
};

} // namespace unity
