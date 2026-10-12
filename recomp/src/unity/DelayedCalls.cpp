#include "unity/DelayedCalls.h"

#include <algorithm>

namespace unity {

namespace {
// A call is due at `time` from a hair before: the engine's fixed clock reaches a delay that's a
// whole number of fixed steps in sums that can round a last bit under it (slime.jsonl: a 0.02 s
// Invoke made in a step runs in the step two later).
constexpr double kDueSlack = 1e-9;
} // namespace

void DelayedCalls::Add(const void* owner, const std::string& name, Kind kind, double base, float delay, std::function<void()> fn, float repeat)
{
    Call c;
    c.time = base + static_cast<double>(delay);
    c.seq = seq_++;
    c.owner = owner;
    c.name = name;
    c.kind = kind;
    c.fn = std::move(fn);
    c.repeat = repeat;
    c.pass = inPass_ ? pass_ : -1;
    c.fixedPass = kind != Kind::Coroutine; // (wherever it's made)
    calls_.push_back(std::move(c));
}

void DelayedCalls::Invoke(const void* owner, const std::string& name, float delay, std::function<void()> fn, float repeatRate)
{
    Add(owner, name, Kind::Invoke, inFixedStep ? fixedNow : now, delay, std::move(fn), repeatRate);
}

void DelayedCalls::Wait(const void* owner, float seconds, std::function<void()> resume)
{
    Add(owner, "", Kind::Coroutine, now, seconds, std::move(resume), 0.0f);
}

void DelayedCalls::Destroy(const void* owner, float seconds, std::function<void()> destroy)
{
    Add(owner, "", Kind::Destroy, inFixedStep ? fixedNow : now, seconds, std::move(destroy), 0.0f);
}

void DelayedCalls::CancelInvoke(const void* owner, const std::string& name)
{
    for (Call& c : calls_)
        if (c.owner == owner && c.kind == Kind::Invoke && (name.empty() || c.name == name)) c.cancelled = true;
}

bool DelayedCalls::IsInvoking(const void* owner, const std::string& name) const
{
    for (const Call& c : calls_)
        if (!c.cancelled && c.owner == owner && c.kind == Kind::Invoke && (name.empty() || c.name == name)) return true;
    return false;
}

void DelayedCalls::StopCoroutines(const void* owner)
{
    for (Call& c : calls_)
        if (c.owner == owner && c.kind == Kind::Coroutine) c.cancelled = true;
}

void DelayedCalls::Forget(const void* owner)
{
    for (Call& c : calls_)
        if (c.owner == owner) c.cancelled = true;
}

void DelayedCalls::UpdateFixed(double fixedTime) { Pass(fixedTime, true); }

void DelayedCalls::Update(double time)
{
    now = time;
    Pass(time, false);
}

void DelayedCalls::Pass(double time, bool fixedPass)
{
    pass_++;
    inPass_ = true;
    for (;;) {
        // The earliest due call not made in this pass.
        int best = -1;
        for (size_t i = 0; i < calls_.size(); i++) {
            const Call& c = calls_[i];
            if (c.cancelled || c.pass == pass_ || c.time > time + kDueSlack || (fixedPass && !c.fixedPass)) continue;
            if (best < 0 || c.time < calls_[static_cast<size_t>(best)].time ||
                (c.time == calls_[static_cast<size_t>(best)].time && c.seq < calls_[static_cast<size_t>(best)].seq))
                best = static_cast<int>(i);
        }
        if (best < 0) break;
        Call& c = calls_[static_cast<size_t>(best)];
        std::function<void()> fn = c.fn;
        if (c.repeat > 0.0f) {
            // InvokeRepeating: the next one a period after this one was due.
            c.time += static_cast<double>(c.repeat);
            c.pass = pass_;
        } else {
            c.cancelled = true;
        }
        if (fn) fn(); // (may add or cancel calls: `c` isn't used after this)
    }
    inPass_ = false;
    for (Call& c : calls_)
        if (c.pass == pass_) c.pass = -1;
    calls_.erase(std::remove_if(calls_.begin(), calls_.end(), [](const Call& c) { return c.cancelled; }), calls_.end());
}

} // namespace unity
