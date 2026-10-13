#pragma once
// DOTween as translated scripts call it, on the hand-written Unity layer's tweens
// (unity/Tween.h, the scene's: bind::Scene::tweens): DOTween.To, DOVirtual.DelayedCall, the
// shortcuts the game uses (DOMoveX / DOMoveY, DOScale, DOColor) and the settings chained on
// them (SetEase, SetUpdate, OnComplete, SetDelay).
//
// A tween reads its start value and its settings when it starts, on its first update: SetEase
// and SetUpdate after the call that made it count (DOTween's). A tween that doesn't say
// SetUpdate runs on the project's default, independent of the time scale (the game's
// DOTweenSettings: the hand port's EnemyBehaviour.OnDeath found it in slime.jsonl). The ease a
// tween doesn't set is the default, OutQuad.
//
// A facade tween (Tween*) may stand for more than one of the layer's tweens (DOColor: the
// colour as two pairs, ColorPlugin's per-component formula); its settings go to each. While it
// runs, the scene keeps it (and the delegates its getters, setters and callbacks call) alive.

#include <vector>

#include "bind/Components.h"
#include "unity/Tween.h"

namespace DG {
namespace Tweening {

// DG.Tweening.Ease (DOTween 1.2.283's values).
enum class Ease : int32_t {
    Unset = 0, Linear = 1, InSine = 2, OutSine = 3, InOutSine = 4, InQuad = 5, OutQuad = 6, InOutQuad = 7,
    InCubic = 8, OutCubic = 9, InOutCubic = 10, InQuart = 11, OutQuart = 12, InOutQuart = 13, InQuint = 14,
    OutQuint = 15, InOutQuint = 16, InExpo = 17, OutExpo = 18, InOutExpo = 19, InCirc = 20, OutCirc = 21,
    InOutCirc = 22, InElastic = 23, OutElastic = 24, InOutElastic = 25, InBack = 26, OutBack = 27, InOutBack = 28,
    InBounce = 29, OutBounce = 30, InOutBounce = 31, Flash = 32, InFlash = 33, OutFlash = 34, InOutFlash = 35,
    INTERNAL_Zero = 36, INTERNAL_Custom = 37
};
enum class RotateMode : int32_t { Fast = 0, FastBeyond360 = 1, WorldAxisAdd = 2, LocalAxisAdd = 3 };
enum class UpdateType : int32_t { Normal = 0, Late = 1, Fixed = 2, Manual = 3 };

using TweenCallback = cs::Delegate<void()>;

class Tween : public cs::Object {
public:
    const char* cs_typename() const override { return "DG.Tweening.Tween"; }
    static const char* cs_static_typename() { return "DG.Tweening.Tween"; }
    bool cs_traced() const override { return true; }
    void cs_trace(cs::Tracer& t) override;

    bool IsPlaying();
    bool IsActive() { return IsPlaying(); }
    void Kill() { Kill(false); }
    void Kill(bool complete);
    void Complete();
    void Pause();
    // Still in the layer's tweens (playing or paused).
    bool Alive();

    std::vector<int> ids;                         // the layer's tweens it stands for
    std::vector<cs::Object*> keep;                // the delegates its tweens call
    cs::Delegate<void()>* onComplete = nullptr;
};

class Tweener : public Tween {
public:
    const char* cs_typename() const override { return "DG.Tweening.Tweener"; }
    static const char* cs_static_typename() { return "DG.Tweening.Tweener"; }
};

class Sequence : public Tween {
public:
    const char* cs_typename() const override { return "DG.Tweening.Sequence"; }
};

// The project's DOTweenSettings the tweens start from.
struct Defaults {
    static constexpr bool timeScaleIndependent = true;
    static constexpr Ease ease = Ease::OutQuad;
};

unity::Ease EaseOf(Ease e); // the layer's (one it doesn't have: NotTranslated)

struct DOTween {
    static Tweener* To(cs::Delegate<float()>* getter, cs::Delegate<void(float)>* setter, float endValue, float duration);
    // (Vector2Plugin: each component start + change * ease, one expression: unity::Tweens::To, as
    // the hand port's FlyingKamikazeGolem return has it.)
    static Tweener* To(cs::Delegate<unity::Vector2()>* getter, cs::Delegate<void(unity::Vector2)>* setter, unity::Vector2 endValue, float duration);
    static int32_t Kill(cs::Object* targetOrId) { return Kill(targetOrId, false); }
    static int32_t Kill(cs::Object* targetOrId, bool complete);
};

struct DOVirtual {
    static Tween* DelayedCall(float delay, TweenCallback* callback) { return DelayedCall(delay, callback, true); }
    static Tween* DelayedCall(float delay, TweenCallback* callback, bool ignoreTimeScale);
};

struct ShortcutExtensions {
    static Tweener* DOMoveX(UnityEngine::Transform* target, float endValue, float duration, bool snapping);
    static Tweener* DOMoveY(UnityEngine::Transform* target, float endValue, float duration, bool snapping);
    static Tweener* DOMove(UnityEngine::Transform* target, UnityEngine::Vector3 endValue, float duration, bool snapping);
    static Tweener* DOLocalMoveX(UnityEngine::Transform* target, float endValue, float duration, bool snapping);
    static Tweener* DOLocalMoveY(UnityEngine::Transform* target, float endValue, float duration, bool snapping);
    static Tweener* DOScale(UnityEngine::Transform* target, float endValue, float duration);
    static Tweener* DOScale(UnityEngine::Transform* target, UnityEngine::Vector3 endValue, float duration);
    static Tweener* DORotate(UnityEngine::Transform* target, UnityEngine::Vector3 endValue, float duration, RotateMode mode);
    static Tweener* DOColor(UnityEngine::Material* target, UnityEngine::Color endValue, float duration);
    static Tweener* DOFloat(UnityEngine::Material* target, float endValue, cs::String* property, float duration);
    static Tweener* DOFade(UnityEngine::SpriteRenderer* target, float endValue, float duration);
    // transform.DOShakePosition(duration, Vector3 strength, vibrato, randomness, snapping, fadeOut):
    // DOTween.Shake's Random draws as it makes the tween (unity::ShakeDraws: vectorBased, z not
    // ignored); the offsets it gives the transform are presentation (a camera's shake): a tween of
    // its duration that moves nothing, its OnComplete on time.
    static Tweener* DOShakePosition(UnityEngine::Transform* target, float duration, UnityEngine::Vector3 strength, int32_t vibrato, float randomness,
                                    bool snapping, bool fadeOut);
    static int32_t DOKill(UnityEngine::Component* target, bool complete);
};

struct TweenSettingsExtensions {
    template <typename T>
    static T SetEase(T t, Ease ease)
    {
        if (t) Ease_(t, ease);
        return t;
    }
    template <typename T>
    static T SetUpdate(T t, bool isIndependentUpdate)
    {
        if (t) Update_(t, isIndependentUpdate);
        return t;
    }
    template <typename T>
    static T SetUpdate(T t, UpdateType type, bool isIndependentUpdate)
    {
        if (type != UpdateType::Normal) cs::NotTranslated("DOTween: SetUpdate with an UpdateType other than Normal");
        return SetUpdate(t, isIndependentUpdate);
    }
    template <typename T>
    static T OnComplete(T t, TweenCallback* action)
    {
        if (t) OnComplete_(t, action);
        return t;
    }
    template <typename T>
    static T SetDelay(T t, float delay)
    {
        (void)delay;
        if (t) cs::NotTranslated("DOTween: SetDelay after the tween is made");
        return t;
    }

private:
    static void Ease_(Tween* t, Ease ease);
    static void Update_(Tween* t, bool independent);
    static void OnComplete_(Tween* t, TweenCallback* action);
};

struct TweenExtensions {
    static void Kill(Tween* t, bool complete)
    {
        if (t) t->Kill(complete);
    }
    static void Complete(Tween* t)
    {
        if (t) t->Complete();
    }
    static bool IsPlaying(Tween* t) { return t && t->IsPlaying(); }
    template <typename T>
    static T Pause(T t)
    {
        if (t) t->Pause();
        return t;
    }
    static bool IsActive(Tween* t) { return t && t->IsActive(); }
};

// The facade tweens that run (the scene's roots), pruned of the finished each frame.
struct Running {
    static std::vector<Tween*>& List();
    static void Add(Tween* t);
    static void Prune();
};

} // namespace Tweening
} // namespace DG
