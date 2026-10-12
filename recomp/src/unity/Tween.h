#pragma once
// DOTween, the part the game's scripts lean on for timing: DOVirtual.DelayedCall, DOMove on a
// position, and tweens whose only effect we need is when they complete (a sprite's fade or
// spin ending a sequence). As DOTween runs them: each tween's time advances by the frame's
// deltaTime (unscaled for delayed calls), a delay is used up first and the rest of that frame
// carries into the tween, the start value is read on its first update, and the eases are
// EaseManager's float formulas.

#include <functional>
#include <vector>

#include "unity/Vector2.h"

namespace unity {

enum class Ease { Linear, OutQuad, InCirc, InCubic };

// EaseManager.Evaluate: the eased fraction at `time` of `duration`.
float EaseEvaluate(Ease ease, float time, float duration);

class Tweens {
public:
    // DOVirtual.DelayedCall(delay, callback): the callback once `delay` has passed.
    void DelayedCall(float delay, std::function<void()> callback);
    // transform.DOMove(end, duration): from the position when it starts.
    void Move(std::function<Vector2()> get, std::function<void(Vector2)> set, Vector2 end, float duration, Ease ease = Ease::OutQuad,
              float delay = 0.0f);
    // A tween we only need the end of (OnComplete after `delay` + `duration`).
    void Timer(float duration, float delay, std::function<void()> onComplete);

    // DOTween's update for the frame.
    void Update(float deltaTime);
    void KillAll() { tweens_.clear(); }
    bool Empty() const { return tweens_.empty(); }

private:
    struct Tween {
        float delay = 0.0f, elapsedDelay = 0.0f, duration = 0.0f, position = 0.0f;
        bool delayComplete = false, started = false;
        Ease ease = Ease::OutQuad;
        Vector2 start, change, end;
        std::function<Vector2()> get;
        std::function<void(Vector2)> set;
        std::function<void()> onComplete;
    };
    std::vector<Tween> tweens_;
};

} // namespace unity
