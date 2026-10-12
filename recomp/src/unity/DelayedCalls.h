#pragma once
// Unity's DelayedCallManager: what MonoBehaviour.Invoke / InvokeRepeating / CancelInvoke, a
// coroutine's `yield return new WaitForSeconds(t)` and Object.Destroy(obj, t) schedule. Each
// call is due at the time it was made plus its delay, on the clock the engine had then:
// Time.time in an Update, Time.fixedTime inside a fixed step (FixedUpdate and the physics
// callbacks, where Time.time reads the fixed clock). The frame's pass (after the scripts'
// Update) runs every call due by Time.time, earliest first, ties in the order they were made;
// a call made during the pass waits for the next one even if it's already due.
//
// Times are doubles, as the engine keeps them; the driver sets `now` to the clock it has.

#include <functional>
#include <string>
#include <vector>

namespace unity {

class DelayedCalls {
public:
    // The clock calls are made on (Time.time, or Time.fixedTime inside a fixed step).
    double now = 0.0;

    // Invoke(name, delay) on `owner` (a behaviour); repeatRate > 0: InvokeRepeating.
    void Invoke(const void* owner, const std::string& name, float delay, std::function<void()> fn, float repeatRate = 0.0f);
    // CancelInvoke(name) on `owner`; "" cancels all of its invokes (CancelInvoke()).
    void CancelInvoke(const void* owner, const std::string& name);
    bool IsInvoking(const void* owner, const std::string& name) const;
    // A coroutine waiting on WaitForSeconds (or Destroy(obj, t)): `resume` once due. Not
    // cancelled by CancelInvoke.
    void Wait(const void* owner, float seconds, std::function<void()> resume);
    // StopAllCoroutines (the object was deactivated): its waits go, its invokes stay.
    void StopCoroutines(const void* owner);
    // The owner is gone (destroyed): all of its calls go.
    void Forget(const void* owner);

    // The frame's pass at `time` (Time.time).
    void Update(double time);
    bool Empty() const { return calls_.empty(); }

private:
    struct Call {
        double time = 0.0;
        long long seq = 0;
        const void* owner = nullptr;
        std::string name; // "" for a coroutine's wait
        std::function<void()> fn;
        float repeat = 0.0f;
        long long pass = -1; // the pass it was made in (-1: between passes)
        bool cancelled = false;
    };
    std::vector<Call> calls_;
    long long seq_ = 0, pass_ = 0;
    bool inPass_ = false;
};

} // namespace unity
