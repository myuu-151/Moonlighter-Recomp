#include "unity/Tween.h"

#include <cmath>

#include "unity/Random.h"

namespace unity {

int ShakeDraws(float duration, int vibrato, float randomness, bool vectorBased, bool ignoreZAxis)
{
    // int num2 = (int)((float)vibrato * duration) (Mono: the product in double).
    int count = static_cast<int>(static_cast<double>(static_cast<float>(vibrato)) * static_cast<double>(duration));
    if (count < 2) count = 2;
    int draws = 0;
    Random::Range(0.0f, 360.0f);
    draws++;
    for (int k = 0; k < count - 1; k++) {
        if (k > 0) {
            Random::Range(0.0f - randomness, randomness);
            draws++;
        }
        if (vectorBased || !ignoreZAxis) {
            Random::Range(0.0f - randomness, randomness);
            draws++;
        }
    }
    return draws;
}

float EaseEvaluate(Ease ease, float time, float duration)
{
    switch (ease) {
    case Ease::Linear: return time / duration;
    // As the game's Mono runs EaseManager's IL: each return one expression in doubles, rounded
    // once; `(time /= duration)` is `div; dup; starg`: that factor is the unrounded quotient,
    // while `time` read after it is the float the argument holds (slime_bis.jsonl,
    // tangle.jsonl: the death fade's DOColor, 88 of 88 frames bit for bit; rounding both or
    // neither misses 13). Math.Sqrt takes a double.
    case Ease::OutQuad: {
        const double q = static_cast<double>(time) / static_cast<double>(duration);
        const double t = static_cast<double>(static_cast<float>(q));
        return static_cast<float>((0.0 - q) * (t - 2.0));
    }
    case Ease::InCirc: {
        const double q = static_cast<double>(time) / static_cast<double>(duration);
        const double t = static_cast<double>(static_cast<float>(q));
        return static_cast<float>(0.0 - (static_cast<double>(static_cast<float>(std::sqrt(1.0 - q * t))) - 1.0));
    }
    case Ease::InCubic: {
        const double q = static_cast<double>(time) / static_cast<double>(duration);
        const double t = static_cast<double>(static_cast<float>(q));
        return static_cast<float>(q * t * t);
    }
    // ((time = time / duration - 1) * time * time * time * time + 1): `div; ldc 1; sub; dup;
    // starg`: the first factor the unrounded difference, the four after it the float the
    // argument holds (DOTween 1.2.283's IL, as OutQuad's).
    case Ease::OutQuint: {
        const double q = static_cast<double>(time) / static_cast<double>(duration) - 1.0;
        const double t = static_cast<double>(static_cast<float>(q));
        return static_cast<float>(q * t * t * t * t + 1.0);
    }
    }
    return time / duration;
}

int Tweens::DelayedCall(float delay, std::function<void()> callback, bool ignoreTimeScale)
{
    Tween t;
    t.id = nextId_++;
    t.delay = delay;
    t.unscaled = ignoreTimeScale;
    t.onComplete = std::move(callback);
    tweens_.push_back(std::move(t));
    return tweens_.back().id;
}

bool Tweens::IsPlaying(int id) const
{
    for (const Tween& t : tweens_)
        if (t.id == id) return true;
    return false;
}

void Tweens::Complete(int id)
{
    for (size_t i = 0; i < tweens_.size(); i++)
        if (tweens_[i].id == id) {
            std::function<void()> cb = std::move(tweens_[i].onComplete);
            tweens_.erase(tweens_.begin() + static_cast<long>(i));
            if (cb) cb();
            return;
        }
}

void Tweens::Kill(int id)
{
    if (id == 0) return;
    for (size_t i = 0; i < tweens_.size(); i++)
        if (tweens_[i].id == id) {
            tweens_.erase(tweens_.begin() + static_cast<long>(i));
            return;
        }
    killedPending_.push_back(id); // (it may be completing this update: its callback won't run)
}

int Tweens::Move(std::function<Vector2()> get, std::function<void(Vector2)> set, Vector2 end, float duration, Ease ease, float delay,
                 bool ignoreTimeScale)
{
    Tween t;
    t.id = nextId_++;
    t.unscaled = ignoreTimeScale;
    t.get = std::move(get);
    t.set = std::move(set);
    t.end = end;
    t.duration = duration;
    t.ease = ease;
    t.delay = delay;
    tweens_.push_back(std::move(t));
    return tweens_.back().id;
}

int Tweens::To(std::function<Vector2()> get, std::function<void(Vector2)> set, Vector2 end, float duration, Ease ease, float delay,
               bool ignoreTimeScale)
{
    const int id = Move(std::move(get), std::move(set), end, duration, ease, delay, ignoreTimeScale);
    tweens_.back().fused = true;
    return id;
}

void Tweens::OnComplete(int id, std::function<void()> callback)
{
    for (Tween& t : tweens_)
        if (t.id == id) {
            t.onComplete = std::move(callback);
            return;
        }
}

int Tweens::Timer(float duration, float delay, std::function<void()> onComplete)
{
    Tween t;
    t.id = nextId_++;
    t.duration = duration;
    t.delay = delay;
    t.onComplete = std::move(onComplete);
    tweens_.push_back(std::move(t));
    return tweens_.back().id;
}

void Tweens::Update(float deltaTime, float unscaledDeltaTime)
{
    // Callbacks can add tweens: those wait for the next update (they're past this pass's end).
    const size_t count = tweens_.size();
    std::vector<size_t> done;
    for (size_t i = 0; i < count; i++) {
        Tween& t = tweens_[i];
        float dt = t.unscaled ? unscaledDeltaTime : deltaTime;
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
            if (t.fused) {
                const double d = static_cast<double>(e);
                t.set({static_cast<float>(static_cast<double>(t.start.x) + static_cast<double>(t.change.x) * d),
                       static_cast<float>(static_cast<double>(t.start.y) + static_cast<double>(t.change.y) * d)});
            } else {
                t.set(t.start + t.change * e);
            }
        }
        if (complete) done.push_back(i);
    }
    // Completed ones go (their callbacks run in order, after this frame's values are set; one
    // that an earlier callback kills doesn't run).
    std::vector<std::pair<int, std::function<void()>>> callbacks;
    for (size_t k = done.size(); k-- > 0;) {
        if (tweens_[done[k]].onComplete) callbacks.insert(callbacks.begin(), {tweens_[done[k]].id, std::move(tweens_[done[k]].onComplete)});
        tweens_.erase(tweens_.begin() + static_cast<long>(done[k]));
    }
    killedPending_.clear();
    for (auto& [id, cb] : callbacks) {
        bool killed = false;
        for (int k : killedPending_) killed |= k == id;
        if (!killed) cb();
    }
    killedPending_.clear();
}

} // namespace unity
