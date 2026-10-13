#include "bind/Scene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "bind/Components.h"
#include "bind/Physics.h"

namespace bind {

Scene* Scene::current = nullptr;

namespace {
const bool gSceneRoots = cs::RegisterRoots(&Scene::TraceRootsEntry);
} // namespace

} // namespace bind

namespace UnityEngine {

namespace {
bind::Scene* S() { return bind::Scene::current; }

double D(float f) { return static_cast<double>(f); }
} // namespace

// ---- Component ---------------------------------------------------------------------------------

void Component::cs_trace(cs::Tracer& t)
{
    Object::cs_trace(t);
    t.Mark(gameObject_);
}

Transform* Component::get_transform() { return cs::nc(gameObject_)->transform_; }
cs::String* Component::get_tag() { return cs::nc(gameObject_)->get_tag(); }
void Component::set_tag(cs::String* tag) { cs::nc(gameObject_)->set_tag(tag); }
bool Component::CompareTag(cs::String* tag) { return cs::nc(gameObject_)->CompareTag(tag); }
cs::String* Component::get_name() { return gameObject_ ? gameObject_->get_name() : Object::get_name(); }
void Component::set_name(cs::String* n)
{
    if (gameObject_) gameObject_->set_name(n);
    else Object::set_name(n);
}
void Component::SendMessage(cs::String* name) { cs::nc(gameObject_)->SendMessage(name, nullptr); }
void Component::SendMessage(cs::String* name, cs::Object* value) { cs::nc(gameObject_)->SendMessage(name, value); }
void Component::SendMessage(cs::String* name, cs::Object* value, SendMessageOptions) { cs::nc(gameObject_)->SendMessage(name, value); }

// ---- GameObject --------------------------------------------------------------------------------

GameObject::GameObject() = default;

void GameObject::cs_trace(cs::Tracer& t)
{
    Object::cs_trace(t);
    t.Mark(tag_);
    for (Component* c : components_) t.Mark(c);
}

void GameObject::__ctor() { __ctor(CS_LIT(u"New Game Object")); }

void GameObject::__ctor(cs::String* name)
{
    name_ = name;
    transform_ = cs::New<Transform>();
    transform_->gameObject_ = this;
    components_.push_back(transform_);
    if (S()) S()->roots.push_back(this);
}

GameObject* GameObject::Create(const std::string& name, Transform* parent)
{
    auto* go = cs::gcnew<GameObject>();
    go->__ctor(cs::String::FromUtf8(name));
    if (parent) go->transform_->SetParent(parent, false);
    return go;
}

void GameObject::set_layer(int32_t layer)
{
    if (layer_ == layer) return;
    layer_ = layer;
    if (S() && S()->physics) S()->physics->LayerChanged(this);
}

bool GameObject::CompareTag(cs::String* tag) { return cs::String::op_Equality(get_tag(), tag); }

bool GameObject::get_activeInHierarchy()
{
    for (Transform* t = transform_; t; t = t->parent_)
        if (!t->gameObject_->activeSelf_) return false;
    return !destroyed_;
}

void GameObject::SetActive(bool value)
{
    if (activeSelf_ == value || destroyed_) return;
    const bool before = get_activeInHierarchy();
    activeSelf_ = value;
    const bool after = get_activeInHierarchy();
    if (before != after) Activate(after);
}

// activeInHierarchy changed for this object (and the children that are active themselves):
// the components hear it in order, the parent's before the children's.
void GameObject::Activate(bool active)
{
    std::vector<Component*> comps = components_;
    for (Component* c : comps)
        if (!c->destroyed_) c->cs_ActiveChanged(active);
    std::vector<Transform*> kids = transform_->children_;
    for (Transform* child : kids)
        if (child->gameObject_->activeSelf_) child->gameObject_->Activate(active);
}

Component* GameObject::Attach(Component* c)
{
    c->gameObject_ = this;
    components_.push_back(c);
    c->cs_Attached();
    return c;
}

void GameObject::SendMessage(cs::String* name, cs::Object* value)
{
    MessageArgs a;
    a.value = value;
    std::vector<Component*> comps = components_;
    for (Component* c : comps)
        if (auto* m = dynamic_cast<MonoBehaviour*>(c)) m->cs_Message(name, a);
}

bool GameObject::SendAnimationEvent(const unity::AnimationEvent& e)
{
    MessageArgs a;
    a.event = &e;
    cs::String* name = cs::String::FromUtf8(e.function);
    bool any = false;
    std::vector<Component*> comps = components_;
    for (Component* c : comps)
        if (auto* m = dynamic_cast<MonoBehaviour*>(c))
            if (m->cs_Message(name, a)) any = true;
    return any;
}

namespace {
GameObject* FindBelow(Transform* t, const std::u16string& path)
{
    size_t slash = path.find(u'/');
    std::u16string head = path.substr(0, slash);
    for (Transform* c : t->children_)
        if (c->gameObject_->get_name()->str() == head) {
            if (slash == std::u16string::npos) return c->gameObject_;
            if (GameObject* g = FindBelow(c, path.substr(slash + 1))) return g;
        }
    return nullptr;
}
GameObject* FindNamed(Transform* t, const std::u16string& name)
{
    if (t->gameObject_->get_activeInHierarchy() && t->gameObject_->get_name()->str() == name) return t->gameObject_;
    for (Transform* c : t->children_)
        if (GameObject* g = FindNamed(c, name)) return g;
    return nullptr;
}
} // namespace

GameObject* GameObject::Find(cs::String* name)
{
    if (!S() || !name) return nullptr;
    const std::u16string& n = name->str();
    for (GameObject* r : S()->roots) {
        if (r->destroyed_) continue;
        if (n.find(u'/') != std::u16string::npos) {
            size_t slash = n.find(u'/');
            if (r->get_name()->str() == n.substr(0, slash))
                if (GameObject* g = FindBelow(r->transform_, n.substr(slash + 1))) return g;
            continue;
        }
        if (GameObject* g = FindNamed(r->transform_, n)) return g;
    }
    return nullptr;
}

// ---- Transform ----------------------------------------------------------------------------------

namespace {
bool Identity(const Transform* t)
{
    return t->rotationZ_ == 0.0f && t->localScale_.x == 1.0f && t->localScale_.y == 1.0f && t->localScale_.z == 1.0f;
}
// The parent's local TRS applied to a point in its space (rotation about z, then translation).
Vector3 ApplyLocal(const Transform* t, Vector3 v)
{
    if (Identity(t)) return {v.x + t->localPosition_.x, v.y + t->localPosition_.y, v.z + t->localPosition_.z};
    Vector3 s = V3Scale(t->localScale_, v);
    const double rad = D(t->rotationZ_) * 3.14159265358979323846 / 180.0;
    const float c = static_cast<float>(std::cos(rad)), sn = static_cast<float>(std::sin(rad));
    Vector3 r{c * s.x - sn * s.y, sn * s.x + c * s.y, s.z};
    return {r.x + t->localPosition_.x, r.y + t->localPosition_.y, r.z + t->localPosition_.z};
}
Vector3 UnapplyLocal(const Transform* t, Vector3 v)
{
    Vector3 d{v.x - t->localPosition_.x, v.y - t->localPosition_.y, v.z - t->localPosition_.z};
    if (Identity(t)) return d;
    const double rad = -D(t->rotationZ_) * 3.14159265358979323846 / 180.0;
    const float c = static_cast<float>(std::cos(rad)), sn = static_cast<float>(std::sin(rad));
    Vector3 r{c * d.x - sn * d.y, sn * d.x + c * d.y, d.z};
    return {r.x / t->localScale_.x, r.y / t->localScale_.y, r.z / t->localScale_.z};
}
} // namespace

void Transform::cs_trace(cs::Tracer& t)
{
    Component::cs_trace(t);
    t.Mark(parent_);
    for (Transform* c : children_) t.Mark(c);
}

void Transform::Changed()
{
    physicsDirty_ = true;
    for (Transform* c : children_) c->Changed();
}

Vector3 Transform::get_position()
{
    Vector3 v = localPosition_;
    for (Transform* p = parent_; p; p = p->parent_) v = ApplyLocal(p, v);
    return v;
}

void Transform::set_position(Vector3 world)
{
    std::vector<Transform*> chain;
    for (Transform* p = parent_; p; p = p->parent_) chain.push_back(p);
    Vector3 v = world;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) v = UnapplyLocal(*it, v);
    localPosition_ = v;
    Changed();
}

Vector3 Transform::TransformPoint(Vector3 local)
{
    Vector3 v = ApplyLocal(this, local);
    for (Transform* p = parent_; p; p = p->parent_) v = ApplyLocal(p, v);
    return v;
}

Vector3 Transform::InverseTransformPoint(Vector3 world)
{
    std::vector<Transform*> chain;
    for (Transform* p = this; p; p = p->parent_) chain.push_back(p);
    Vector3 v = world;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) v = UnapplyLocal(*it, v);
    return v;
}

Vector3 Transform::get_lossyScale()
{
    Vector3 s = localScale_;
    for (Transform* p = parent_; p; p = p->parent_) s = V3Scale(p->localScale_, s);
    return s;
}

namespace {
float Wrap360(float a)
{
    float r = static_cast<float>(std::fmod(D(a), 360.0));
    if (r < 0.0f) r += 360.0f;
    return r;
}
Quaternion ZRotation(float degrees)
{
    const double half = D(degrees) * 3.14159265358979323846 / 360.0;
    Quaternion q;
    q.z = static_cast<float>(std::sin(half));
    q.w = static_cast<float>(std::cos(half));
    return q;
}
float ZAngle(Quaternion q)
{
    return Wrap360(static_cast<float>(std::atan2(2.0 * (D(q.w) * q.z + D(q.x) * q.y), 1.0 - 2.0 * (D(q.y) * q.y + D(q.z) * q.z)) * 180.0 /
                                      3.14159265358979323846));
}
} // namespace

Vector3 Transform::get_localEulerAngles() { return {0.0f, 0.0f, Wrap360(rotationZ_)}; }
void Transform::set_localEulerAngles(Vector3 e)
{
    rotationZ_ = e.z;
    Changed();
}
Vector3 Transform::get_eulerAngles()
{
    float z = rotationZ_;
    for (Transform* p = parent_; p; p = p->parent_) z += p->rotationZ_;
    return {0.0f, 0.0f, Wrap360(z)};
}
void Transform::set_eulerAngles(Vector3 e)
{
    float z = e.z;
    for (Transform* p = parent_; p; p = p->parent_) z -= p->rotationZ_;
    rotationZ_ = z;
    Changed();
}
Quaternion Transform::get_localRotation() { return ZRotation(rotationZ_); }
void Transform::set_localRotation(Quaternion q)
{
    rotationZ_ = ZAngle(q);
    Changed();
}
Quaternion Transform::get_rotation() { return ZRotation(get_eulerAngles().z); }
void Transform::set_rotation(Quaternion q) { set_eulerAngles({0.0f, 0.0f, ZAngle(q)}); }
Vector3 Transform::get_up() { return QRotate(get_rotation(), V3Up()); }
Vector3 Transform::get_right() { return QRotate(get_rotation(), V3Right()); }

void Transform::SetParent(Transform* p, bool worldPositionStays)
{
    if (p == parent_) return;
    GameObject* go = gameObject_;
    const bool before = go->get_activeInHierarchy();
    const Vector3 world = get_position();
    if (parent_) {
        auto& k = parent_->children_;
        k.erase(std::remove(k.begin(), k.end(), this), k.end());
    } else if (S()) {
        auto& r = S()->roots;
        r.erase(std::remove(r.begin(), r.end(), go), r.end());
    }
    parent_ = p;
    if (p) p->children_.push_back(this);
    else if (S()) S()->roots.push_back(go);
    if (worldPositionStays) set_position(world);
    Changed();
    const bool after = go->get_activeInHierarchy();
    (void)before, (void)after; // (a reparent across inactive parents: not needed yet)
}

Transform* Transform::GetChild(int32_t i)
{
    if (i < 0 || i >= get_childCount()) cs::Throw(cs::gcnew<cs::Exception>(cs::String::FromAscii("Transform child out of bounds")));
    return children_[static_cast<size_t>(i)];
}

Transform* Transform::Find(cs::String* name)
{
    if (!name) cs::ThrowArgumentNull("name");
    GameObject* g = FindBelow(this, name->str());
    return g ? g->transform_ : nullptr;
}

Transform* Transform::get_root()
{
    Transform* t = this;
    while (t->parent_) t = t->parent_;
    return t;
}

int32_t Transform::GetSiblingIndex()
{
    if (!parent_) return 0;
    auto& k = parent_->children_;
    return static_cast<int32_t>(std::find(k.begin(), k.end(), this) - k.begin());
}

// ---- Behaviour, MonoBehaviour --------------------------------------------------------------------

void Behaviour::set_enabled(bool v)
{
    if (enabled_ == v) return;
    enabled_ = v;
    if (gameObject_ && gameObject_->get_activeInHierarchy()) cs_EnabledChanged(v);
}

bool Behaviour::get_isActiveAndEnabled() { return enabled_ && gameObject_ && gameObject_->get_activeInHierarchy(); }

void Coroutine::cs_trace(cs::Tracer& t)
{
    Object::cs_trace(t);
    t.Mark(owner);
    t.Mark(cs::AsObject(routine));
    t.Mark(waitingFor);
    for (Coroutine* w : waiters) t.Mark(w);
}

void MonoBehaviour::cs_trace(cs::Tracer& t) { Behaviour::cs_trace(t); }

void MonoBehaviour::cs_Attached()
{
    if (!gameObject_->get_activeInHierarchy()) return; // Awake waits for the object's activation
    awoken_ = true;
    cs_Awake();
    if (enabled_ && !destroyed_ && gameObject_->get_activeInHierarchy()) {
        cs_OnEnable();
        if (S()) S()->Started(this);
    }
}

void MonoBehaviour::cs_ActiveChanged(bool active)
{
    if (active) {
        if (!awoken_) {
            awoken_ = true;
            cs_Awake();
        }
        if (enabled_) {
            cs_OnEnable();
            if (!started_ && S()) S()->Started(this);
        }
    } else {
        if (enabled_) cs_OnDisable();
        // A deactivated object's coroutines stop (its invokes don't).
        if (S()) S()->Forget(this, true);
    }
}

void MonoBehaviour::cs_EnabledChanged(bool enabled)
{
    if (enabled) {
        if (!awoken_) {
            awoken_ = true;
            cs_Awake();
        }
        cs_OnEnable();
        if (!started_ && S()) S()->Started(this);
    } else {
        cs_OnDisable();
    }
}

void MonoBehaviour::cs_Destroyed()
{
    if (awoken_) cs_OnDestroy();
    if (S()) S()->Forget(this, false);
}

void MonoBehaviour::Invoke(cs::String* methodName, float time)
{
    if (!S()) cs::NotTranslated("MonoBehaviour.Invoke without a scene");
    cs::String* name = cs::nc(methodName);
    MonoBehaviour* self = this;
    S()->calls.Invoke(this, name->ToUtf8(), time, [self, name] {
        MessageArgs a;
        if (!self->cs_Message(name, a)) std::fprintf(stderr, "[unity] Trying to Invoke method: %s couldn't be called.\n", name->ToUtf8().c_str());
    });
}

void MonoBehaviour::InvokeRepeating(cs::String* methodName, float time, float repeatRate)
{
    if (!S()) cs::NotTranslated("MonoBehaviour.InvokeRepeating without a scene");
    cs::String* name = cs::nc(methodName);
    MonoBehaviour* self = this;
    S()->calls.Invoke(this, name->ToUtf8(), time, [self, name] {
        MessageArgs a;
        self->cs_Message(name, a);
    }, repeatRate);
}

void MonoBehaviour::CancelInvoke()
{
    if (S()) S()->calls.CancelInvoke(this, "");
}
void MonoBehaviour::CancelInvoke(cs::String* methodName)
{
    if (S()) S()->calls.CancelInvoke(this, cs::nc(methodName)->ToUtf8());
}
bool MonoBehaviour::IsInvoking() { return S() && S()->calls.IsInvoking(this, ""); }
bool MonoBehaviour::IsInvoking(cs::String* methodName) { return S() && S()->calls.IsInvoking(this, cs::nc(methodName)->ToUtf8()); }

Coroutine* MonoBehaviour::StartCoroutine(cs::IEnumerator* routine)
{
    if (!routine) cs::ThrowArgumentNull("routine");
    if (!S()) cs::NotTranslated("MonoBehaviour.StartCoroutine without a scene");
    if (!gameObject_ || !gameObject_->get_activeInHierarchy()) {
        std::fprintf(stderr, "[unity] Coroutine couldn't be started because the the game object is inactive!\n");
        return nullptr;
    }
    auto* c = cs::gcnew<Coroutine>();
    c->owner = this;
    c->routine = routine;
    S()->Coroutines().push_back(c);
    S()->Step(c);
    return c;
}

Coroutine* MonoBehaviour::StartCoroutine(cs::String* methodName)
{
    (void)methodName;
    cs::NotTranslated("MonoBehaviour.StartCoroutine(string) (a coroutine by name)");
}

void MonoBehaviour::StopCoroutine(Coroutine* routine)
{
    if (routine && S()) S()->StopCoroutine(routine);
}

void MonoBehaviour::StopCoroutine(cs::IEnumerator* routine)
{
    if (!routine || !S()) return;
    for (Coroutine* c : std::vector<Coroutine*>(S()->Coroutines()))
        if (c->owner == this && c->routine == routine) S()->StopCoroutine(c);
}

void MonoBehaviour::StopCoroutine(cs::String* methodName)
{
    (void)methodName;
    cs::NotTranslated("MonoBehaviour.StopCoroutine(string)");
}

void MonoBehaviour::StopAllCoroutines()
{
    if (S()) S()->Forget(this, true);
}

void MonoBehaviour::print(cs::Object* message) { Debug::Log(message); }

Object* FindObjectOfTypeByName(cs::Type* type)
{
    const char* want = cs::nc(type)->FullNameC();
    for (Object* o : Object::Live()) {
        if (o->destroyed_) continue;
        auto* c = dynamic_cast<Component*>(o);
        if (!c || !c->gameObject_ || !c->gameObject_->get_activeInHierarchy()) continue;
        for (const char* n = c->cs_typename(); n; n = nullptr)
            if (std::string(n) == want) return c;
    }
    return nullptr;
}

} // namespace UnityEngine

namespace bind {

using namespace UnityEngine;

Scene::Scene()
{
    (void)gSceneRoots;
    current = this;
}

Scene::~Scene()
{
    if (current == this) current = nullptr;
}

void Scene::TraceRootsEntry(cs::Tracer& t)
{
    if (!current) return;
    for (GameObject* g : current->roots) t.Mark(g);
    for (Coroutine* c : current->coroutines_) t.Mark(c);
    for (MonoBehaviour* m : current->pendingStarts_) t.Mark(m);
    for (Object* o : current->destroys_) t.Mark(o);
    for (Object* o : current->animators) t.Mark(o);
}

void Scene::RunStarts()
{
    std::vector<MonoBehaviour*> list;
    list.swap(pendingStarts_);
    for (MonoBehaviour* m : list) {
        if (m->started_ || m->destroyed_ || !m->get_isActiveAndEnabled()) continue;
        m->started_ = true;
        m->cs_Start();
    }
}

void Scene::Step(Coroutine* c)
{
    if (c->done) return;
    bool more = c->routine->IEnumerator_MoveNext();
    if (c->done) return; // stopped from inside
    if (!more) {
        c->done = true;
        coroutines_.erase(std::remove(coroutines_.begin(), coroutines_.end(), c), coroutines_.end());
        std::vector<Coroutine*> waiters;
        waiters.swap(c->waiters);
        for (Coroutine* w : waiters) {
            w->waitingFor = nullptr;
            Step(w);
        }
        return;
    }
    cs::Object* cur = c->routine->IEnumerator_get_Current();
    if (!cur) {
        yieldNull_.push_back({c, frame});
    } else if (auto* w = dynamic_cast<WaitForSeconds*>(cur)) {
        calls.Wait(c->owner, w->m_Seconds, [this, c] { Step(c); });
    } else if (dynamic_cast<WaitForFixedUpdate*>(cur)) {
        fixedWaits_.push_back(c);
    } else if (auto* nested = dynamic_cast<Coroutine*>(cur)) {
        if (nested->done) yieldNull_.push_back({c, frame});
        else {
            c->waitingFor = nested;
            nested->waiters.push_back(c);
        }
    } else if (auto* e = dynamic_cast<cs::IEnumerator*>(cur)) {
        // yield return an IEnumerator: run as a nested coroutine
        auto* n = cs::gcnew<Coroutine>();
        n->owner = c->owner;
        n->routine = e;
        coroutines_.push_back(n);
        c->waitingFor = n;
        n->waiters.push_back(c);
        Step(n);
    } else {
        std::fprintf(stderr, "[unity] a coroutine yielded %s: resumed as after yield return null\n", cur->cs_typename());
        yieldNull_.push_back({c, frame});
    }
}

void Scene::ResumeYieldNull()
{
    std::vector<YieldNull> due, later;
    for (const YieldNull& y : yieldNull_) (y.frame < frame ? due : later).push_back(y);
    yieldNull_.swap(later);
    for (const YieldNull& y : due)
        if (!y.c->done) Step(y.c);
}

void Scene::ResumeFixed()
{
    std::vector<Coroutine*> list;
    list.swap(fixedWaits_);
    for (Coroutine* c : list)
        if (!c->done) Step(c);
}

void Scene::StopCoroutine(Coroutine* c)
{
    if (c->done) return;
    c->done = true;
    coroutines_.erase(std::remove(coroutines_.begin(), coroutines_.end(), c), coroutines_.end());
    yieldNull_.erase(std::remove_if(yieldNull_.begin(), yieldNull_.end(), [c](const YieldNull& y) { return y.c == c; }), yieldNull_.end());
    fixedWaits_.erase(std::remove(fixedWaits_.begin(), fixedWaits_.end(), c), fixedWaits_.end());
    // (a WaitForSeconds of it still due: its Step finds it done)
}

void Scene::Forget(MonoBehaviour* owner, bool coroutinesOnly)
{
    for (Coroutine* c : std::vector<Coroutine*>(coroutines_))
        if (c->owner == owner) StopCoroutine(c);
    if (coroutinesOnly) calls.StopCoroutines(owner);
    else calls.Forget(owner);
}

void Scene::MarkDestroy(Object* o)
{
    if (std::find(destroys_.begin(), destroys_.end(), o) == destroys_.end()) destroys_.push_back(o);
}

namespace {
void DisableTree(GameObject* g)
{
    if (!g->get_activeInHierarchy()) return;
    std::vector<Component*> comps = g->components_;
    for (Component* c : comps) c->cs_ActiveChanged(false);
    for (Transform* k : std::vector<Transform*>(g->transform_->children_))
        if (k->gameObject_->activeSelf_) DisableTree(k->gameObject_);
}
void DestroyTree(GameObject* g)
{
    std::vector<Component*> comps = g->components_;
    for (Component* c : comps) {
        c->cs_Destroyed();
        c->destroyed_ = true;
        if (Scene::current) Scene::current->calls.Forget(c);
    }
    for (Transform* k : std::vector<Transform*>(g->transform_->children_)) DestroyTree(k->gameObject_);
    g->destroyed_ = true;
    if (Scene::current) Scene::current->calls.Forget(g);
}
} // namespace

void Scene::DestroyNow(Object* o)
{
    if (!o || o->destroyed_) return;
    if (auto* g = dynamic_cast<GameObject*>(o)) {
        DisableTree(g);
        DestroyTree(g);
        Transform* t = g->transform_;
        if (t->parent_) {
            auto& k = t->parent_->children_;
            k.erase(std::remove(k.begin(), k.end(), t), k.end());
            t->parent_ = nullptr;
        } else {
            roots.erase(std::remove(roots.begin(), roots.end(), g), roots.end());
        }
        return;
    }
    if (auto* c = dynamic_cast<Component*>(o)) {
        if (c->gameObject_ && c->gameObject_->get_activeInHierarchy()) c->cs_ActiveChanged(false);
        c->cs_Destroyed();
        c->destroyed_ = true;
        calls.Forget(c);
        if (c->gameObject_) {
            auto& v = c->gameObject_->components_;
            v.erase(std::remove(v.begin(), v.end(), c), v.end());
        }
        return;
    }
    o->destroyed_ = true;
}

void Scene::ProcessDestroys()
{
    std::vector<Object*> list;
    list.swap(destroys_);
    for (Object* o : list) DestroyNow(o);
}

} // namespace bind
