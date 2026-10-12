#pragma once
// DOTween, the part the game's scripts lean on for timing: DOVirtual.DelayedCall, DOMove on a
// position, and tweens whose only effect we need is when they complete (a sprite's fade or
// spin ending a sequence). As DOTween runs them: each tween's time advances by the frame's
// deltaTime, or its unscaled deltaTime for one that ignores the time scale (DOVirtual.DelayedCall
// does by default), a delay is used up first and the rest of that frame carries into the tween,
// the start value is read on its first update, and the eases are EaseManager's formulas as the
// game's Mono evaluates them (in doubles, rounded where stored).

#include <functional>
#include <vector>

#include "unity/Vector2.h"

namespace unity {

enum class Ease { Linear, OutQuad, InCirc, InCubic };

// EaseManager.Evaluate: the eased fraction at `time` of `duration`.
float EaseEvaluate(Ease ease, float time, float duration);

// DOTween.Shake's UnityEngine.Random draws (DOTween 1.2.283, the game's), made when a shake
// tween is created (DOShakePosition and the others): Range(0, 360) for the first angle, then
// for each of the (int)(vibrato * duration) (at least 2) waypoints but the last, a
// Range(-randomness, randomness) turning the angle (from the second on) and, vectorBased (a
// Vector3 strength) or not ignoring z, another for the rotation about y. The shake itself
// (the camera's offsets) is presentation: only the draws are made. Returns how many.
int ShakeDraws(float duration, int vibrato, float randomness, bool vectorBased, bool ignoreZAxis);

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
    // transform.DOMove(end, duration): from the position when it starts. (Any tween of one or
    // two floats: DOTween.To, DOColor as two pairs.) ignoreTimeScale: SetUpdate(true), or the
    // project's default for tweens that don't say (see EnemyBehaviour.OnDeath's).
    // Returns its handle.
    int Move(std::function<Vector2()> get, std::function<void(Vector2)> set, Vector2 end, float duration, Ease ease = Ease::OutQuad,
             float delay = 0.0f, bool ignoreTimeScale = false);
    // DOTween.To of floats, material.DOFloat, material.DOColor (the colour as two pairs): Move,
    // but each component is FloatPlugin's `startValue + changeValue * easeVal` (ColorPlugin's
    // `startValue.r += changeValue.r * easeVal`): one expression on the game's Mono, in doubles,
    // rounded once (DOMove's Vector2/Vector3 operators round the product first).
    // (Also DOMoveX / DOMoveY: Vector3Plugin's axis constraint sets the one component as
    // `startValue.x + changeValue.x * easeVal`, the others as the getter reads them now.)
    int To(std::function<Vector2()> get, std::function<void(Vector2)> set, Vector2 end, float duration, Ease ease, float delay,
           bool ignoreTimeScale);
    // Tween.OnComplete(callback) on a handle (one that's done or killed: ignored).
    void OnComplete(int id, std::function<void()> callback);
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
        bool fused = false; // To's: each component start + change * ease rounded once
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
