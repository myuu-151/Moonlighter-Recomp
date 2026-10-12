#include "unity/DelayedCalls.h"

#include <algorithm>

namespace unity {

void DelayedCalls::Invoke(const void* owner, const std::string& name, float delay, std::function<void()> fn, float repeatRate)
{
    Call c;
    c.time = now + static_cast<double>(delay);
    c.seq = seq_++;
    c.owner = owner;
    c.name = name;
    c.fn = std::move(fn);
    c.repeat = repeatRate;
    c.pass = inPass_ ? pass_ : -1;
    calls_.push_back(std::move(c));
}

void DelayedCalls::CancelInvoke(const void* owner, const std::string& name)
{
    for (Call& c : calls_)
        if (c.owner == owner && !c.name.empty() && (name.empty() || c.name == name)) c.cancelled = true;
}

bool DelayedCalls::IsInvoking(const void* owner, const std::string& name) const
{
    for (const Call& c : calls_)
        if (!c.cancelled && c.owner == owner && !c.name.empty() && (name.empty() || c.name == name)) return true;
    return false;
}

void DelayedCalls::Wait(const void* owner, float seconds, std::function<void()> resume)
{
    Invoke(owner, "", seconds, std::move(resume));
}

void DelayedCalls::StopCoroutines(const void* owner)
{
    for (Call& c : calls_)
        if (c.owner == owner && c.name.empty()) c.cancelled = true;
}

void DelayedCalls::Forget(const void* owner)
{
    for (Call& c : calls_)
        if (c.owner == owner) c.cancelled = true;
}

void DelayedCalls::Update(double time)
{
    pass_++;
    inPass_ = true;
    now = time;
    for (;;) {
        // The earliest due call not made in this pass.
        int best = -1;
        for (size_t i = 0; i < calls_.size(); i++) {
            const Call& c = calls_[i];
            if (c.cancelled || c.pass == pass_ || c.time > time) continue;
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
