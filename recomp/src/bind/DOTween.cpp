#include "bind/DOTween.h"

#include <algorithm>

namespace DG {
namespace Tweening {

using UnityEngine::Vector3;
using unity::Vector2;

namespace {

unity::Tweens* Tw()
{
    if (!bind::Scene::current) cs::NotTranslated("DOTween without a scene (bind::Scene)");
    return &bind::Scene::current->tweens;
}

void TraceRunning(cs::Tracer& t)
{
    for (Tween* tw : Running::List()) t.Mark(tw);
}
const bool gRoots = cs::RegisterRoots(&TraceRunning);

template <typename T>
T* Make()
{
    (void)gRoots;
    auto* t = cs::gcnew<T>();
    Running::Add(t);
    return t;
}

} // namespace

std::vector<Tween*>& Running::List()
{
    static std::vector<Tween*>* list = new std::vector<Tween*>();
    return *list;
}

void Running::Add(Tween* t)
{
    Prune();
    List().push_back(t);
}

void Running::Prune()
{
    auto& l = List();
    l.erase(std::remove_if(l.begin(), l.end(), [](Tween* t) { return !t->Alive(); }), l.end());
}

void Tween::cs_trace(cs::Tracer& t)
{
    for (cs::Object* o : keep) t.Mark(o);
    t.Mark(onComplete);
}

bool Tween::IsPlaying()
{
    if (!bind::Scene::current) return false;
    for (int id : ids)
        if (bind::Scene::current->tweens.IsPlaying(id)) return true;
    return false;
}

void Tween::Kill(bool complete)
{
    if (!bind::Scene::current) return;
    for (int id : ids) {
        if (complete) bind::Scene::current->tweens.Complete(id);
        else bind::Scene::current->tweens.Kill(id);
    }
}

void Tween::Pause()
{
    if (!bind::Scene::current) return;
    for (int id : ids) bind::Scene::current->tweens.Pause(id);
}

bool Tween::Alive()
{
    if (!bind::Scene::current) return false;
    for (int id : ids)
        if (bind::Scene::current->tweens.Exists(id)) return true;
    return false;
}

void Tween::Complete()
{
    if (!bind::Scene::current) return;
    for (int id : ids) bind::Scene::current->tweens.Complete(id);
}

unity::Ease EaseOf(Ease e)
{
    switch (e) {
    case Ease::Linear: return unity::Ease::Linear;
    case Ease::OutQuad: return unity::Ease::OutQuad;
    case Ease::InCirc: return unity::Ease::InCirc;
    case Ease::InCubic: return unity::Ease::InCubic;
    case Ease::OutQuint: return unity::Ease::OutQuint;
    case Ease::OutCubic: return unity::Ease::OutCubic;
    case Ease::Unset: return unity::Ease::OutQuad;
    default: break;
    }
    cs::NotTranslated("DOTween: an ease unity/Tween.h doesn't have");
}

namespace {
const unity::Ease kEase = unity::Ease::OutQuad; // Defaults::ease
const bool kIndependent = Defaults::timeScaleIndependent;
} // namespace

Tweener* DOTween::To(cs::Delegate<float()>* getter, cs::Delegate<void(float)>* setter, float endValue, float duration)
{
    auto* t = Make<Tweener>();
    t->keep = {getter, setter};
    (void)cs::nc(getter);
    (void)cs::nc(setter);
    t->ids.push_back(Tw()->To([getter] { return Vector2{getter->Invoke(), 0.0f}; }, [setter](Vector2 v) { setter->Invoke(v.x); },
                              Vector2{endValue, 0.0f}, duration, kEase, 0.0f, kIndependent));
    return t;
}

Tweener* DOTween::To(cs::Delegate<Vector2()>* getter, cs::Delegate<void(Vector2)>* setter, Vector2 endValue, float duration)
{
    auto* t = Make<Tweener>();
    t->keep = {getter, setter};
    (void)cs::nc(getter);
    (void)cs::nc(setter);
    t->ids.push_back(Tw()->To([getter] { return getter->Invoke(); }, [setter](Vector2 v) { setter->Invoke(v); }, endValue, duration, kEase, 0.0f, kIndependent));
    return t;
}

int32_t DOTween::Kill(cs::Object* targetOrId, bool complete)
{
    if (complete) cs::NotTranslated("DOTween.Kill(target, complete: true)");
    if (!targetOrId) return 0;
    return Tw()->KillTarget(targetOrId);
}

Tween* DOVirtual::DelayedCall(float delay, TweenCallback* callback, bool ignoreTimeScale)
{
    auto* t = Make<Tween>();
    t->keep = {callback};
    t->ids.push_back(Tw()->DelayedCall(delay, [callback] {
        if (callback) callback->Invoke();
    }, ignoreTimeScale));
    return t;
}

namespace {

// The setter of a transform's tween: nothing once the object is gone (DOTween's safe mode
// kills a tween whose target was destroyed).
bool Alive(UnityEngine::Transform* t) { return t && !t->destroyed_ && t->gameObject_ && !t->gameObject_->destroyed_; }

// A shortcut's tween's target (Tween.target: what DOKill / DOTween.Kill(target) finds it by).
void Targets(Tween* t, const void* target)
{
    for (int id : t->ids) Tw()->SetTarget(id, target);
}

Tweener* MoveAxis(UnityEngine::Transform* target, float endValue, float duration, int axis, bool local)
{
    (void)cs::nc(target);
    auto* t = Make<Tweener>();
    t->keep = {target};
    auto get = [target, axis, local] {
        const Vector3 p = local ? target->localPosition_ : target->get_position();
        return Vector2{axis == 0 ? p.x : p.y, 0.0f};
    };
    auto set = [target, axis, local](Vector2 v) {
        if (!Alive(target)) return;
        Vector3 p = local ? target->localPosition_ : target->get_position();
        (axis == 0 ? p.x : p.y) = v.x;
        if (local) target->set_localPosition(p);
        else target->set_position(p);
    };
    // Vector3Plugin with an axis constraint: the one component as `startValue.x + changeValue.x
    // * easeVal` (one expression), the others as the getter reads them now.
    t->ids.push_back(Tw()->To(get, set, Vector2{endValue, 0.0f}, duration, kEase, 0.0f, kIndependent));
    Targets(t, target);
    return t;
}

} // namespace

Tweener* ShortcutExtensions::DOMoveX(UnityEngine::Transform* target, float endValue, float duration, bool snapping)
{
    if (snapping) cs::NotTranslated("DOMoveX with snapping");
    return MoveAxis(target, endValue, duration, 0, false);
}
Tweener* ShortcutExtensions::DOMoveY(UnityEngine::Transform* target, float endValue, float duration, bool snapping)
{
    if (snapping) cs::NotTranslated("DOMoveY with snapping");
    return MoveAxis(target, endValue, duration, 1, false);
}
Tweener* ShortcutExtensions::DOLocalMoveX(UnityEngine::Transform* target, float endValue, float duration, bool snapping)
{
    if (snapping) cs::NotTranslated("DOLocalMoveX with snapping");
    return MoveAxis(target, endValue, duration, 0, true);
}
Tweener* ShortcutExtensions::DOLocalMoveY(UnityEngine::Transform* target, float endValue, float duration, bool snapping)
{
    if (snapping) cs::NotTranslated("DOLocalMoveY with snapping");
    return MoveAxis(target, endValue, duration, 1, true);
}

Tweener* ShortcutExtensions::DOMove(UnityEngine::Transform* target, Vector3 endValue, float duration, bool snapping)
{
    if (snapping) cs::NotTranslated("DOMove with snapping");
    (void)cs::nc(target);
    auto* t = Make<Tweener>();
    t->keep = {target};
    // Vector3Plugin: startValue + changeValue * easeVal, through Vector3's operators (the product
    // rounded first): the layer's Move, x and y; z apart.
    t->ids.push_back(Tw()->Move([target] { const Vector3 p = target->get_position(); return Vector2{p.x, p.y}; },
                                [target](Vector2 v) {
                                    if (!Alive(target)) return;
                                    Vector3 p = target->get_position();
                                    target->set_position(Vector3{v.x, v.y, p.z});
                                },
                                Vector2{endValue.x, endValue.y}, duration, kEase, 0.0f, kIndependent));
    t->ids.push_back(Tw()->Move([target] { return Vector2{target->get_position().z, 0.0f}; },
                                [target](Vector2 v) {
                                    if (!Alive(target)) return;
                                    Vector3 p = target->get_position();
                                    target->set_position(Vector3{p.x, p.y, v.x});
                                },
                                Vector2{endValue.z, 0.0f}, duration, kEase, 0.0f, kIndependent));
    Targets(t, target);
    return t;
}

Tweener* ShortcutExtensions::DOScale(UnityEngine::Transform* target, float endValue, float duration)
{
    return DOScale(target, Vector3{endValue, endValue, endValue}, duration);
}

Tweener* ShortcutExtensions::DOScale(UnityEngine::Transform* target, Vector3 endValue, float duration)
{
    (void)cs::nc(target);
    auto* t = Make<Tweener>();
    t->keep = {target};
    t->ids.push_back(Tw()->Move([target] { return Vector2{target->localScale_.x, target->localScale_.y}; },
                                [target](Vector2 v) {
                                    if (!Alive(target)) return;
                                    target->set_localScale(Vector3{v.x, v.y, target->localScale_.z});
                                },
                                Vector2{endValue.x, endValue.y}, duration, kEase, 0.0f, kIndependent));
    t->ids.push_back(Tw()->Move([target] { return Vector2{target->localScale_.z, 0.0f}; },
                                [target](Vector2 v) {
                                    if (!Alive(target)) return;
                                    target->set_localScale(Vector3{target->localScale_.x, target->localScale_.y, v.x});
                                },
                                Vector2{endValue.z, 0.0f}, duration, kEase, 0.0f, kIndependent));
    Targets(t, target);
    return t;
}

Tweener* ShortcutExtensions::DORotate(UnityEngine::Transform* target, Vector3 endValue, float duration, RotateMode mode)
{
    (void)target, (void)endValue, (void)duration, (void)mode;
    cs::NotTranslated("DOTween DORotate (QuaternionPlugin, RotateMode)");
}

Tweener* ShortcutExtensions::DOColor(UnityEngine::Material* target, UnityEngine::Color endValue, float duration)
{
    (void)cs::nc(target);
    auto* t = Make<Tweener>();
    t->keep = {target};
    // ColorPlugin: `startValue.r += changeValue.r * easeVal` per component (one expression each):
    // the layer's To, the colour as two pairs.
    t->ids.push_back(Tw()->To([target] { const auto c = target->ColorOf("_Color"); return Vector2{c.r, c.g}; },
                              [target](Vector2 v) {
                                  auto c = target->ColorOf("_Color");
                                  c.r = v.x;
                                  c.g = v.y;
                                  target->SetColor("_Color", c);
                              },
                              Vector2{endValue.r, endValue.g}, duration, kEase, 0.0f, kIndependent));
    t->ids.push_back(Tw()->To([target] { const auto c = target->ColorOf("_Color"); return Vector2{c.b, c.a}; },
                              [target](Vector2 v) {
                                  auto c = target->ColorOf("_Color");
                                  c.b = v.x;
                                  c.a = v.y;
                                  target->SetColor("_Color", c);
                              },
                              Vector2{endValue.b, endValue.a}, duration, kEase, 0.0f, kIndependent));
    Targets(t, target);
    return t;
}

Tweener* ShortcutExtensions::DOFloat(UnityEngine::Material* target, float endValue, cs::String* property, float duration)
{
    (void)cs::nc(target);
    const std::string name = cs::nc(property)->ToUtf8();
    auto* t = Make<Tweener>();
    t->keep = {target};
    t->ids.push_back(Tw()->To([target, name] { return Vector2{target->Float(name), 0.0f}; }, [target, name](Vector2 v) { target->SetFloat(name, v.x); },
                              Vector2{endValue, 0.0f}, duration, kEase, 0.0f, kIndependent));
    Targets(t, target);
    return t;
}

Tweener* ShortcutExtensions::DOFade(UnityEngine::SpriteRenderer* target, float endValue, float duration)
{
    (void)cs::nc(target);
    auto* t = Make<Tweener>();
    t->keep = {target};
    t->ids.push_back(Tw()->To([target] { return Vector2{target->color_.a, 0.0f}; },
                              [target](Vector2 v) {
                                  if (!target->destroyed_) target->color_.a = v.x;
                              },
                              Vector2{endValue, 0.0f}, duration, kEase, 0.0f, kIndependent));
    Targets(t, target);
    return t;
}

Tweener* ShortcutExtensions::DOShakePosition(UnityEngine::Transform* target, float duration, Vector3 strength, int32_t vibrato, float randomness,
                                             bool snapping, bool fadeOut)
{
    (void)strength, (void)fadeOut;
    (void)cs::nc(target);
    if (snapping) cs::NotTranslated("DOShakePosition with snapping");
    unity::ShakeDraws(duration, vibrato, randomness, true, false);
    auto* t = Make<Tweener>();
    t->keep = {target};
    t->ids.push_back(Tw()->Timer(duration, 0.0f, [] {}));
    Targets(t, target);
    return t;
}

// target.DOKill(complete): every tween whose target it is (a shortcut's), killed (its OnComplete
// not run); complete: run to its end first (not yet needed).
int32_t ShortcutExtensions::DOKill(UnityEngine::Component* target, bool complete)
{
    if (complete) cs::NotTranslated("DOTween DOKill(target, complete: true)");
    if (!target) return 0;
    return Tw()->KillTarget(target);
}

void TweenSettingsExtensions::Ease_(Tween* t, Ease ease)
{
    const unity::Ease e = EaseOf(ease);
    for (int id : t->ids) Tw()->SetEase(id, e);
}

void TweenSettingsExtensions::Update_(Tween* t, bool independent)
{
    for (int id : t->ids) Tw()->SetUnscaled(id, independent);
}

void TweenSettingsExtensions::OnComplete_(Tween* t, TweenCallback* action)
{
    // On the last of the layer's tweens it stands for (they end together).
    t->onComplete = action;
    if (t->ids.empty()) return;
    Tw()->OnComplete(t->ids.back(), [action] {
        bind::Scene* s = bind::Scene::current;
        cs::Object* target = action ? action->get_Target() : nullptr;
        if (s && s->onTweenComplete) s->onTweenComplete(target, false);
        if (action) action->Invoke();
        if (s && s->onTweenComplete) s->onTweenComplete(target, true);
    });
}

} // namespace Tweening
} // namespace DG
