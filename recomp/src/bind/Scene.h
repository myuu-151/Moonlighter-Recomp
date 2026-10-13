#pragma once
// The scene graph translated MonoBehaviours live in: GameObject, Component, Transform,
// Behaviour, MonoBehaviour (its messages, Invoke, coroutines, SendMessage), as Unity 2019.2
// runs them, on the hand-written Unity layer's schedulers (unity/DelayedCalls.h).
//
// A driver (a replay, the game's loop) owns a bind::Scene and calls its passes in Unity's
// order; the scene holds the services the components use (the delayed calls, DOTween's
// tweens, the physics, the frame count). The order a frame runs, as the enemy replays found it
// (game/Enemy.h's header): the fixed steps (FixedUpdate, the physics step and its trigger
// messages, the fixed pass of the delayed calls), DOTween, the Updates, the delayed calls
// (Invoke, WaitForSeconds, Destroy(obj, t)), the coroutines on `yield return null`, the
// Animators, the LateUpdates.
//
// Transforms: world positions go through the parents from the object up, each parent's local
// TRS applied in turn (the hand port's PolyNavAgent, checked against the game: with no rotation
// or scale, the local position plus each parent's in that order, floats).

#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

#include "bind/Core.h"
#include "bind/Values.h"
#include "unity/Animator.h"
#include "unity/DelayedCalls.h"
#include "unity/Tween.h"

namespace UnityEngine {

class GameObject;
class Transform;
class Component;
class MonoBehaviour;
class Collider2D;
class Collision2D;
class Coroutine;
class Animator;

// SendMessage's argument, or the Animator event that names the method.
struct MessageArgs {
    cs::Object* value = nullptr;
    const unity::AnimationEvent* event = nullptr;
};
inline bool MessageIs(cs::String* name, cs::String* literal) { return name && literal && name->str() == literal->str(); }

enum class SendMessageOptions : int32_t { RequireReceiver = 0, DontRequireReceiver = 1 };

// ---- Component ------------------------------------------------------------------------------------

class Component : public Object {
public:
    const char* cs_typename() const override { return "UnityEngine.Component"; }
    static const char* cs_static_typename() { return "UnityEngine.Component"; }
    void cs_trace(cs::Tracer& t) override;

    GameObject* get_gameObject() { return gameObject_; }
    Transform* get_transform();
    cs::String* get_tag();
    void set_tag(cs::String* tag);
    bool CompareTag(cs::String* tag);
    cs::String* get_name() override;
    void set_name(cs::String* n) override;

    template <typename T>
    T GetComponent();
    template <typename T>
    T GetComponentInChildren();
    template <typename T>
    T GetComponentInChildren(bool includeInactive);
    template <typename T>
    cs::Array<T>* GetComponentsInChildren();
    template <typename T>
    cs::Array<T>* GetComponentsInChildren(bool includeInactive);
    template <typename T>
    T GetComponentInParent();
    template <typename T>
    cs::Array<T>* GetComponents();
    void SendMessage(cs::String* name);
    void SendMessage(cs::String* name, cs::Object* value);
    void SendMessage(cs::String* name, cs::Object* value, SendMessageOptions options);

    // ---- the engine's side ----
    // Added to its object (before any message).
    virtual void cs_Attached() {}
    // Its object became active or inactive in the hierarchy (with this enabled).
    virtual void cs_ActiveChanged(bool active) { (void)active; }
    // Destroyed (after OnDisable / OnDestroy).
    virtual void cs_Destroyed() {}

    GameObject* gameObject_ = nullptr;
};

// ---- GameObject -----------------------------------------------------------------------------------

class GameObject : public Object {
public:
    GameObject();
    const char* cs_typename() const override { return "UnityEngine.GameObject"; }
    static const char* cs_static_typename() { return "UnityEngine.GameObject"; }
    void cs_trace(cs::Tracer& t) override;

    // new GameObject() / new GameObject(name): with its Transform, at the scene's root.
    void __ctor();
    void __ctor(cs::String* name);
    static GameObject* Create(const std::string& name, Transform* parent = nullptr);

    Transform* get_transform() { return transform_; }
    int32_t get_layer() { return layer_; }
    void set_layer(int32_t layer);
    cs::String* get_tag() { return tag_ ? tag_ : CS_LIT(u"Untagged"); }
    void set_tag(cs::String* tag) { tag_ = tag; }
    bool CompareTag(cs::String* tag);
    bool get_activeSelf() { return activeSelf_; }
    bool get_activeInHierarchy();
    void SetActive(bool value);
    GameObject* get_gameObject() { return this; }

    template <typename T>
    T GetComponent();
    template <typename T>
    T GetComponentInChildren() { return GetComponentInChildren<T>(false); }
    template <typename T>
    T GetComponentInChildren(bool includeInactive);
    template <typename T>
    cs::Array<T>* GetComponentsInChildren() { return GetComponentsInChildren<T>(false); }
    template <typename T>
    cs::Array<T>* GetComponentsInChildren(bool includeInactive);
    template <typename T>
    T GetComponentInParent();
    template <typename T>
    cs::Array<T>* GetComponents();
    // AddComponent<T>(): T as translated code names it (a class pointer).
    template <typename T>
    T AddComponent();
    // A component made elsewhere put on this object: Awake, then OnEnable if active (Unity's order).
    Component* Attach(Component* c);

    void SendMessage(cs::String* name) { SendMessage(name, nullptr); }
    void SendMessage(cs::String* name, cs::Object* value);
    void SendMessage(cs::String* name, cs::Object* value, SendMessageOptions) { SendMessage(name, value); }
    // An Animator's event: every MonoBehaviour of the object with the method (Unity logs "has no
    // receiver" when none has it). Returns whether one did.
    bool SendAnimationEvent(const unity::AnimationEvent& e);

    // GameObject.Find(name): an active object with that name, or a path below a root ("A/B").
    static GameObject* Find(cs::String* name);

    std::vector<Component*>& Components() { return components_; }

    Transform* transform_ = nullptr;
    std::vector<Component*> components_;
    int32_t layer_ = 0;
    cs::String* tag_ = nullptr;
    bool activeSelf_ = true;

private:
    void Activate(bool active); // the hierarchy below changed activeInHierarchy
    template <typename T>
    void Collect(std::vector<T>& out, bool includeInactive, bool first);
};

// ---- Transform -------------------------------------------------------------------------------------

class Transform : public Component {
public:
    const char* cs_typename() const override { return "UnityEngine.Transform"; }
    static const char* cs_static_typename() { return "UnityEngine.Transform"; }
    void cs_trace(cs::Tracer& t) override;

    Vector3 get_position();
    void set_position(Vector3 p);
    Vector3 get_localPosition() { return localPosition_; }
    void set_localPosition(Vector3 p)
    {
        localPosition_ = p;
        Changed();
    }
    Vector3 get_localScale() { return localScale_; }
    void set_localScale(Vector3 s)
    {
        localScale_ = s;
        Changed();
    }
    Vector3 get_lossyScale();
    // Rotations: about z (the 2D game's), in degrees as set.
    Vector3 get_localEulerAngles();
    void set_localEulerAngles(Vector3 e);
    Vector3 get_eulerAngles();
    void set_eulerAngles(Vector3 e);
    Quaternion get_localRotation();
    void set_localRotation(Quaternion q);
    Quaternion get_rotation();
    void set_rotation(Quaternion q);
    Vector3 get_up();
    Vector3 get_right();
    Vector3 TransformPoint(Vector3 local);
    Vector3 InverseTransformPoint(Vector3 world);

    Transform* get_parent() { return parent_; }
    void set_parent(Transform* p) { SetParent(p, true); }
    void SetParent(Transform* p) { SetParent(p, true); }
    void SetParent(Transform* p, bool worldPositionStays);
    int32_t get_childCount() { return static_cast<int32_t>(children_.size()); }
    Transform* GetChild(int32_t i);
    Transform* Find(cs::String* name);
    Transform* get_root();
    int32_t GetSiblingIndex();

    // The physics' sync: the transform moved since the physics last read or wrote it.
    bool physicsDirty_ = false;
    void Changed();

    Vector3 localPosition_;
    Vector3 localScale_{1.0f, 1.0f, 1.0f};
    float rotationZ_ = 0.0f; // localEulerAngles.z as set
    Transform* parent_ = nullptr;
    std::vector<Transform*> children_;
};

// ---- Behaviour, MonoBehaviour ----------------------------------------------------------------------

class Behaviour : public Component {
public:
    const char* cs_typename() const override { return "UnityEngine.Behaviour"; }
    static const char* cs_static_typename() { return "UnityEngine.Behaviour"; }
    bool get_enabled() { return enabled_; }
    void set_enabled(bool v);
    bool get_isActiveAndEnabled();
    bool enabled_ = true;

protected:
    // enabled changed while the object is active (OnEnable / OnDisable for a MonoBehaviour).
    virtual void cs_EnabledChanged(bool enabled) { (void)enabled; }
};

// A yield instruction is a plain managed object in Unity (not a UnityEngine.Object): no native
// side, no `== null` of its own, collected as any object once nothing references it.
class YieldInstruction : public cs::Object {
public:
    const char* cs_typename() const override { return "UnityEngine.YieldInstruction"; }
    bool cs_traced() const override { return true; }
};
class WaitForSeconds : public YieldInstruction {
public:
    const char* cs_typename() const override { return "UnityEngine.WaitForSeconds"; }
    void __ctor(float seconds) { m_Seconds = seconds; }
    float m_Seconds = 0.0f;
};
class WaitForFixedUpdate : public YieldInstruction {
public:
    const char* cs_typename() const override { return "UnityEngine.WaitForFixedUpdate"; }
    void __ctor() {}
};
class WaitForEndOfFrame : public YieldInstruction {
public:
    const char* cs_typename() const override { return "UnityEngine.WaitForEndOfFrame"; }
    void __ctor() {}
};

// A running coroutine (what StartCoroutine returns and yield returns to wait on).
class Coroutine : public YieldInstruction {
public:
    const char* cs_typename() const override { return "UnityEngine.Coroutine"; }
    void cs_trace(cs::Tracer& t) override;
    MonoBehaviour* owner = nullptr;
    cs::IEnumerator* routine = nullptr;
    Coroutine* waitingFor = nullptr;        // a nested coroutine it waits on
    std::vector<Coroutine*> waiters;        // coroutines waiting on this one
    bool done = false;
};

class MonoBehaviour : public Behaviour {
public:
    const char* cs_typename() const override { return "UnityEngine.MonoBehaviour"; }
    static const char* cs_static_typename() { return "UnityEngine.MonoBehaviour"; }
    void cs_trace(cs::Tracer& t) override;

    // ---- the messages (overridden by translated classes that have them) ----
    virtual void cs_Awake() {}
    virtual void cs_OnEnable() {}
    virtual void cs_Start() {}
    virtual void cs_Update() {}
    virtual void cs_FixedUpdate() {}
    virtual void cs_LateUpdate() {}
    virtual void cs_OnDisable() {}
    virtual void cs_OnDestroy() {}
    virtual void cs_OnTriggerEnter2D(Collider2D*) {}
    virtual void cs_OnTriggerExit2D(Collider2D*) {}
    virtual void cs_OnTriggerStay2D(Collider2D*) {}
    virtual void cs_OnCollisionEnter2D(Collision2D*) {}
    virtual void cs_OnCollisionExit2D(Collision2D*) {}
    virtual void cs_OnCollisionStay2D(Collision2D*) {}
    virtual void cs_OnBecameVisible() {}
    virtual void cs_OnBecameInvisible() {}
    virtual void cs_OnApplicationQuit() {}
    virtual void cs_OnApplicationPause(bool) {}
    virtual void cs_OnApplicationFocus(bool) {}
    // A method by name (SendMessage, Invoke, an Animator event): true if this has one.
    virtual bool cs_Message(cs::String* name, const MessageArgs& args)
    {
        (void)name, (void)args;
        return false;
    }

    // ---- MonoBehaviour's API ----
    void Invoke(cs::String* methodName, float time);
    void InvokeRepeating(cs::String* methodName, float time, float repeatRate);
    void CancelInvoke();
    void CancelInvoke(cs::String* methodName);
    bool IsInvoking();
    bool IsInvoking(cs::String* methodName);
    Coroutine* StartCoroutine(cs::IEnumerator* routine);
    Coroutine* StartCoroutine(cs::String* methodName);
    void StopCoroutine(Coroutine* routine);
    void StopCoroutine(cs::IEnumerator* routine);
    void StopCoroutine(cs::String* methodName);
    void StopAllCoroutines();
    static void print(cs::Object* message);

    // ---- the engine's side ----
    void cs_Attached() override;
    void cs_ActiveChanged(bool active) override;
    void cs_Destroyed() override;
    bool awoken_ = false, started_ = false;

protected:
    void cs_EnabledChanged(bool enabled) override;
};

// ---- the scene -------------------------------------------------------------------------------------

} // namespace UnityEngine

namespace bind {

class PhysicsScene;

// The world translated objects live in, and the frame's passes (the driver calls them).
class Scene {
public:
    Scene();
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    static Scene* current;

    unity::DelayedCalls calls;   // Invoke, WaitForSeconds, Destroy(obj, t)
    unity::Tweens tweens;        // DOTween
    PhysicsScene* physics = nullptr;
    long long frame = 0;         // the frame being run (BeginFrame counts them)

    std::vector<UnityEngine::GameObject*> roots;
    std::vector<UnityEngine::Animator*> animators; // in the order they were added (bind/Components.h)

    void BeginFrame() { frame++; }
    // Start for the behaviours that haven't run it, before their first Update (Unity's order:
    // the ones made since, in the order they were made).
    void RunStarts();
    // The coroutines waiting on `yield return null` from an earlier frame.
    void ResumeYieldNull();
    // The coroutines waiting on WaitForFixedUpdate (after a fixed step's physics).
    void ResumeFixed();
    // The objects Destroy(obj) marked, at the frame's end.
    void ProcessDestroys();
    // The Animators' pass (bind/Components.cpp): each enabled one on an active object.
    void UpdateAnimators(float deltaTime);

    // ---- what the components call ----
    void Started(UnityEngine::MonoBehaviour* m) { pendingStarts_.push_back(m); }
    void Step(UnityEngine::Coroutine* c);     // MoveNext, then what its Current says
    void Forget(UnityEngine::MonoBehaviour* owner, bool coroutinesOnly);
    void StopCoroutine(UnityEngine::Coroutine* c);
    void MarkDestroy(UnityEngine::Object* o);
    void DestroyNow(UnityEngine::Object* o);
    std::vector<UnityEngine::Coroutine*>& Coroutines() { return coroutines_; }

private:
    struct YieldNull {
        UnityEngine::Coroutine* c;
        long long frame;
    };
    std::vector<UnityEngine::MonoBehaviour*> pendingStarts_;
    std::vector<YieldNull> yieldNull_;
    std::vector<UnityEngine::Coroutine*> fixedWaits_;
    std::vector<UnityEngine::Coroutine*> coroutines_; // running (for StopCoroutine and the collector)
    // Coroutines with a WaitForSeconds pending in `calls` (one entry per pending wait): the
    // collector's roots until it resumes them or their owner's calls are dropped (a coroutine
    // StopCoroutine took out of coroutines_ is still referenced by its pending wait).
    std::vector<UnityEngine::Coroutine*> secondsWaits_;
    std::vector<UnityEngine::Object*> destroys_;

public:
    static void TraceRootsEntry(cs::Tracer& t);
};

} // namespace bind

namespace UnityEngine {

// ---- templates ----------------------------------------------------------------------------------

template <typename T>
T GameObject::GetComponent()
{
    for (Component* c : components_)
        if (auto t = dynamic_cast<T>(c)) return t;
    return nullptr;
}

template <typename T>
void GameObject::Collect(std::vector<T>& out, bool includeInactive, bool first)
{
    if (!includeInactive && !get_activeInHierarchy()) return;
    for (Component* c : components_)
        if (auto t = dynamic_cast<T>(c)) {
            out.push_back(t);
            if (first) return;
        }
    for (Transform* child : transform_->children_) {
        if (first && !out.empty()) return;
        child->gameObject_->Collect(out, includeInactive, first);
    }
}

template <typename T>
T GameObject::GetComponentInChildren(bool includeInactive)
{
    std::vector<T> out;
    Collect(out, includeInactive, true);
    return out.empty() ? nullptr : out[0];
}

template <typename T>
cs::Array<T>* GameObject::GetComponentsInChildren(bool includeInactive)
{
    std::vector<T> out;
    Collect(out, includeInactive, false);
    return cs::Array<T>::FromVector(out);
}

template <typename T>
T GameObject::GetComponentInParent()
{
    for (Transform* t = transform_; t; t = t->parent_)
        if (t->gameObject_->get_activeInHierarchy())
            if (auto c = t->gameObject_->GetComponent<T>()) return c;
    return nullptr;
}

template <typename T>
cs::Array<T>* GameObject::GetComponents()
{
    std::vector<T> out;
    for (Component* c : components_)
        if (auto t = dynamic_cast<T>(c)) out.push_back(t);
    return cs::Array<T>::FromVector(out);
}

template <typename T>
T GameObject::AddComponent()
{
    using C = typename std::remove_pointer<T>::type;
    C* c = cs::New<C>();
    Attach(c);
    return c;
}

template <typename T>
T Component::GetComponent() { return cs::nc(gameObject_)->GetComponent<T>(); }
template <typename T>
T Component::GetComponentInChildren() { return cs::nc(gameObject_)->GetComponentInChildren<T>(false); }
template <typename T>
T Component::GetComponentInChildren(bool includeInactive) { return cs::nc(gameObject_)->GetComponentInChildren<T>(includeInactive); }
template <typename T>
cs::Array<T>* Component::GetComponentsInChildren() { return cs::nc(gameObject_)->GetComponentsInChildren<T>(false); }
template <typename T>
cs::Array<T>* Component::GetComponentsInChildren(bool includeInactive) { return cs::nc(gameObject_)->GetComponentsInChildren<T>(includeInactive); }
template <typename T>
T Component::GetComponentInParent() { return cs::nc(gameObject_)->GetComponentInParent<T>(); }
template <typename T>
cs::Array<T>* Component::GetComponents() { return cs::nc(gameObject_)->GetComponents<T>(); }

// Object.FindObjectOfType: the first active object's component of the type (by Type: the
// scene's components whose class has that name).
Object* FindObjectOfTypeByName(cs::Type* type);

} // namespace UnityEngine
