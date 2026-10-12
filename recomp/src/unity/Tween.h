#pragma once
// DOTween, the part the game's scripts lean on for timing: DOVirtual.DelayedCall, DOMove on a
// position, and tweens whose only effect we need is when they complete (a sprite's fade or
// spin ending a sequence). As DOTween runs them: each tween's time advances by the frame's
// deltaTime, or its unscaled deltaTime for one that ignores the time scale (DOVirtual.DelayedCall
// does by default), a delay is used up first and the rest of that frame carries into the tween,
// the start value is read on its first update, and the eases are EaseManager's float formulas.

#include <functional>
#include <vector>

#include "unity/Vector2.h"

namespace unity {

enum class Ease { Linear, OutQuad, InCirc, InCubic };

// EaseManager.Evaluate: the eased fraction at `time` of `duration`.
float EaseEvaluate(Ease ease, float time, float duration);

class Tweens {
public:
    // DOVirtual.DelayedCall(delay, callback, ignoreTimeScale): the callback once `delay` has
    // passed. Returns its handle.
    int DelayedCall(float delay, std::function<void()> callback, bool ignoreTimeScale = true);
    // Tween.IsPlaying / Tween.Complete (runs its callback now) / Tween.Kill for a handle (0:
    // none; a handle that's done or killed is ignored).
    bool IsPlaying(int id) const;
    void Complete(int id);
    void Kill(int id);
    // transform.DOMove(end, duration): from the position when it starts.
    void Move(std::function<Vector2()> get, std::function<void(Vector2)> set, Vector2 end, float duration, Ease ease = Ease::OutQuad,
              float delay = 0.0f);
    // A tween we only need the end of (OnComplete after `delay` + `duration`).
    int Timer(float duration, float delay, std::function<void()> onComplete);

    // DOTween's update for the frame: scaled tweens by deltaTime, the others by unscaledDeltaTime.
    void Update(float deltaTime, float unscaledDeltaTime);
    void Update(float deltaTime) { Update(deltaTime, deltaTime); }
    void KillAll() { tweens_.clear(); }
    bool Empty() const { return tweens_.empty(); }

private:
    struct Tween {
        int id = 0;
        float delay = 0.0f, elapsedDelay = 0.0f, duration = 0.0f, position = 0.0f;
        bool delayComplete = false, started = false, unscaled = false;
        Ease ease = Ease::OutQuad;
        Vector2 start, change, end;
        std::function<Vector2()> get;
        std::function<void(Vector2)> set;
        std::function<void()> onComplete;
    };
    std::vector<Tween> tweens_;
    std::vector<int> killedPending_; // killed while this update's callbacks run
    int nextId_ = 1;
};

} // namespace unity
