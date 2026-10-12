#include "unity/Tween.h"

#include <cmath>

namespace unity {

float EaseEvaluate(Ease ease, float time, float duration)
{
    switch (ease) {
    case Ease::Linear: return time / duration;
    case Ease::OutQuad: time /= duration; return -time * (time - 2.0f);
    case Ease::InCirc: time /= duration; return -(static_cast<float>(std::sqrt(static_cast<double>(1.0f - time * time))) - 1.0f);
    case Ease::InCubic: time /= duration; return time * time * time;
    }
    return time / duration;
}

void Tweens::DelayedCall(float delay, std::function<void()> callback)
{
    Tween t;
    t.delay = delay;
    t.onComplete = std::move(callback);
    tweens_.push_back(std::move(t));
}

void Tweens::Move(std::function<Vector2()> get, std::function<void(Vector2)> set, Vector2 end, float duration, Ease ease, float delay)
{
    Tween t;
    t.get = std::move(get);
    t.set = std::move(set);
    t.end = end;
    t.duration = duration;
    t.ease = ease;
    t.delay = delay;
    tweens_.push_back(std::move(t));
}

void Tweens::Timer(float duration, float delay, std::function<void()> onComplete)
{
    Tween t;
    t.duration = duration;
    t.delay = delay;
    t.onComplete = std::move(onComplete);
    tweens_.push_back(std::move(t));
}

void Tweens::Update(float deltaTime)
{
    // Callbacks can add tweens: those wait for the next update (they're past this pass's end).
    const size_t count = tweens_.size();
    std::vector<size_t> done;
    for (size_t i = 0; i < count; i++) {
        Tween& t = tweens_[i];
        float dt = deltaTime;
        if (!t.delayComplete) {
            t.elapsedDelay += dt;
            if (t.elapsedDelay < t.delay) continue;
            dt = t.elapsedDelay - t.delay;
            t.delayComplete = true;
        }
        if (!t.started) {
            t.started = true;
            if (t.get) {
                t.start = t.get();
                t.change = t.end - t.start;
            }
        }
        t.position += dt;
        const bool complete = t.position >= t.duration;
        if (complete) t.position = t.duration;
        if (t.set) {
            float e = t.duration > 0.0f ? EaseEvaluate(t.ease, t.position, t.duration) : 1.0f;
            t.set(t.start + t.change * e);
        }
        if (complete) done.push_back(i);
    }
    // Completed ones go (their callbacks run in order, after this frame's values are set).
    std::vector<std::function<void()>> callbacks;
    for (size_t k = done.size(); k-- > 0;) {
        if (tweens_[done[k]].onComplete) callbacks.insert(callbacks.begin(), std::move(tweens_[done[k]].onComplete));
        tweens_.erase(tweens_.begin() + static_cast<long>(done[k]));
    }
    for (auto& cb : callbacks) cb();
}

} // namespace unity
