#include "bind/Components.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <unordered_map>

#include <Box2D/Box2D.h>

namespace UnityEngine {

namespace {
bind::Scene* S() { return bind::Scene::current; }
} // namespace

// ---- assets --------------------------------------------------------------------------------------

Sprite* Sprite::Named(const std::string& name)
{
    // Sprites are assets: one object per name, alive for the program (rooted by Object::Live).
    static std::unordered_map<std::string, Sprite*>* byName = new std::unordered_map<std::string, Sprite*>();
    auto it = byName->find(name);
    if (it != byName->end()) return it->second;
    cs::StaticScope scope;
    auto* s = cs::gcnew<Sprite>();
    s->spriteName = name;
    s->name_ = cs::String::Intern(cs::String::FromUtf8(name));
    (*byName)[name] = s;
    return s;
}

void Sprite::cs_trace(cs::Tracer& t)
{
    Object::cs_trace(t);
    t.Mark(texture_);
}

Texture2D* Sprite::get_texture()
{
    if (!texture_) {
        texture_ = cs::gcnew<Texture2D>();
        texture_->name_ = name_;
    }
    return texture_;
}

Sprite* Sprite::Create(Texture2D* texture, unity::Rect rect, Vector2 pivot, float pixelsPerUnit)
{
    (void)rect, (void)pivot, (void)pixelsPerUnit;
    auto* s = cs::gcnew<Sprite>();
    s->texture_ = texture;
    return s;
}

float Material::GetFloat(cs::String* name) { return Float(cs::nc(name)->ToUtf8()); }
void Material::SetFloat(cs::String* name, float value) { floats_[cs::nc(name)->ToUtf8()] = value; }
Color Material::GetColor(cs::String* name) { return ColorOf(cs::nc(name)->ToUtf8()); }
void Material::SetColor(cs::String* name, Color value) { colors_[cs::nc(name)->ToUtf8()] = value; }
bool Material::HasProperty(cs::String* name)
{
    const std::string n = cs::nc(name)->ToUtf8();
    return n == "_FlashAmount" || n == "_Color" || floats_.count(n) || colors_.count(n);
}

float Material::Float(const std::string& name) const
{
    auto it = floats_.find(name);
    return it == floats_.end() ? 0.0f : it->second;
}

Color Material::ColorOf(const std::string& name) const
{
    auto it = colors_.find(name);
    if (it != colors_.end()) return it->second;
    return name == "_Color" ? Color{1.0f, 1.0f, 1.0f, 1.0f} : Color{};
}

// ---- renderers ----------------------------------------------------------------------------------------

void Renderer::cs_trace(cs::Tracer& t)
{
    Component::cs_trace(t);
    t.Mark(material_);
    t.Mark(sortingLayer_);
}

Material* Renderer::get_material()
{
    if (!material_) {
        // Its own instance (Unity's renderer.material): the renderer's to keep, collected with it.
        material_ = cs::gcnew<Material>();
        material_->name_ = CS_LIT(u"Material (Instance)");
        material_->rooted_ = false;
    }
    return material_;
}

void SpriteRenderer::cs_trace(cs::Tracer& t)
{
    Renderer::cs_trace(t);
    t.Mark(sprite_);
}

// ---- the Animator --------------------------------------------------------------------------------------

void Animator::cs_trace(cs::Tracer& t)
{
    Behaviour::cs_trace(t);
    t.Mark(runtime_);
    for (auto& b : behaviours_) t.Mark(b.second);
    for (GameObject* g : targets_) t.Mark(g);
    t.Mark(spriteRenderer_);
    t.Mark(spriteTarget_);
}

void Animator::cs_Attached()
{
    if (S()) S()->animators.push_back(this);
}

void Animator::cs_ActiveChanged(bool active)
{
    if (!enabled_ || !anim_) return;
    if (!active) {
        if (ran_) WriteDefaults();
        return;
    }
    if (ran_) Restart();
    if (!bound_) Bind();
}

void Animator::WriteDefaults()
{
    if (!bound_ || !controller_) return;
    const auto& props = controller_->properties;
    for (size_t i = 0; i < props.size() && i < targets_.size(); i++) {
        GameObject* g = targets_[i];
        if (!g || g->destroyed_) continue;
        if (props[i].sprite) {
            if (auto* r = g->GetComponent<SpriteRenderer*>()) r->sprite_ = defaultSprites_[i].empty() ? nullptr : Sprite::Named(defaultSprites_[i]);
            continue;
        }
        Write(g, props[i], defaults_[i]);
    }
}

void Animator::Restart()
{
    // From its default state, its parameters' defaults (a new state machine), bound again.
    anim_.reset();
    const unity::AnimatorController* c = controller_;
    SetController(c);
    ran_ = false;
}

void Animator::cs_EnabledChanged(bool enabled)
{
    if (enabled && anim_ && !bound_ && gameObject_ && gameObject_->get_activeInHierarchy()) Bind();
}

void Animator::SetController(const unity::AnimatorController* controller)
{
    controller_ = controller;
    anim_.reset();
    bound_ = false;
    if (controller_) {
        anim_ = std::make_unique<unity::Animator>(*controller_);
        anim_->speed = speed_;
        if (controller_->updateMode == 2) unscaledTime_ = true;
        anim_->onStateEnter = [this](int32_t hash) { StateMessage(hash, true); };
        anim_->onStateExit = [this](int32_t hash) { StateMessage(hash, false); };
        anim_->onEvent = [this](const unity::AnimationEvent& e) {
            if (onEvent) onEvent(*this, e, false);
            if (gameObject_ && !gameObject_->SendAnimationEvent(e))
                if (std::getenv("ML_UNITY_LOG"))
                    std::fprintf(stderr, "[unity] AnimationEvent '%s' on animation has no receiver!\n", e.function.c_str());
            if (onEvent) onEvent(*this, e, true);
        };
    }
}

void Animator::set_runtimeAnimatorController(RuntimeAnimatorController* c)
{
    runtime_ = c;
    SetController(c ? c->controller : nullptr);
}

namespace {
std::string U8(cs::String* s) { return cs::nc(s)->ToUtf8(); }
} // namespace

void Animator::SetFloat(cs::String* name, float value)
{
    if (anim_) anim_->SetFloat(U8(name), value);
}
void Animator::SetBool(cs::String* name, bool value)
{
    if (anim_) anim_->SetBool(U8(name), value);
}
void Animator::SetInteger(cs::String* name, int32_t value)
{
    if (anim_) anim_->SetInteger(U8(name), value);
}
void Animator::SetTrigger(cs::String* name)
{
    if (anim_) anim_->SetTrigger(U8(name));
}
void Animator::ResetTrigger(cs::String* name)
{
    if (anim_) anim_->ResetTrigger(U8(name));
}
namespace {
// The parameter an id names (nullptr: none).
const unity::AnimatorParameter* ParamById(const unity::AnimatorController* c, int32_t id)
{
    if (!c) return nullptr;
    for (const unity::AnimatorParameter& p : c->params)
        if (p.hash == id) return &p;
    return nullptr;
}
} // namespace

void Animator::SetFloat(int32_t id, float value)
{
    if (const auto* p = ParamById(controller_, id); p && anim_) anim_->SetFloat(p->name, value);
}
void Animator::SetBool(int32_t id, bool value)
{
    if (const auto* p = ParamById(controller_, id); p && anim_) anim_->SetBool(p->name, value);
}
void Animator::SetInteger(int32_t id, int32_t value)
{
    if (const auto* p = ParamById(controller_, id); p && anim_) anim_->SetInteger(p->name, value);
}
void Animator::SetTrigger(int32_t id)
{
    if (const auto* p = ParamById(controller_, id); p && anim_) anim_->SetTrigger(p->name);
}
void Animator::ResetTrigger(int32_t id)
{
    if (const auto* p = ParamById(controller_, id); p && anim_) anim_->ResetTrigger(p->name);
}
float Animator::GetFloat(int32_t id)
{
    const auto* p = ParamById(controller_, id);
    return p && anim_ ? anim_->GetFloat(p->name) : 0.0f;
}
bool Animator::GetBool(int32_t id)
{
    const auto* p = ParamById(controller_, id);
    return p && anim_ && anim_->GetBool(p->name);
}
int32_t Animator::GetInteger(int32_t id)
{
    const auto* p = ParamById(controller_, id);
    return p && anim_ ? anim_->GetInteger(p->name) : 0;
}

cs::Array<AnimatorControllerParameter*>* Animator::get_parameters()
{
    std::vector<AnimatorControllerParameter*> out;
    if (controller_)
        for (const unity::AnimatorParameter& p : controller_->params) {
            auto* a = cs::gcnew<AnimatorControllerParameter>();
            a->name_ = cs::String::FromUtf8(p.name);
            a->nameHash_ = p.hash;
            a->type_ = static_cast<AnimatorControllerParameterType>(p.type);
            a->defaultFloat_ = p.defaultValue;
            out.push_back(a);
        }
    return cs::Array<AnimatorControllerParameter*>::FromVector(out);
}

void Animator::CrossFade(int32_t stateHashName, float normalizedTransitionDuration, int32_t layer, float normalizedTimeOffset)
{
    (void)layer;
    if (!anim_ || !controller_) return;
    const int s = controller_->StateByHash(stateHashName);
    if (s < 0) return;
    if (normalizedTransitionDuration != 0.0f) cs::NotTranslated("Animator.CrossFade with a duration");
    anim_->Play(controller_->states[static_cast<size_t>(s)].name, std::isinf(normalizedTimeOffset) ? 0.0f : normalizedTimeOffset);
}

void Animator::AddBehaviour(int32_t stateHash, StateMachineBehaviour* behaviour) { behaviours_.emplace_back(stateHash, behaviour); }

void Animator::StateMessage(int32_t hash, bool enter)
{
    if (behaviours_.empty()) return;
    AnimatorStateInfo info;
    info.info.shortNameHash = hash;
    if (controller_)
        if (const int s = controller_->StateByHash(hash); s >= 0) {
            const unity::AnimatorState& st = controller_->states[static_cast<size_t>(s)];
            info.info.fullPathHash = st.fullPathHash;
            info.info.tagHash = st.tagHash;
            info.info.loop = st.loop;
        }
    const auto list = behaviours_; // (a message may add or remove behaviours)
    for (const auto& b : list) {
        if (b.first != hash || b.second->destroyed_) continue;
        if (enter) b.second->cs_OnStateEnter(this, info, 0);
        else b.second->cs_OnStateExit(this, info, 0);
    }
}

float Animator::GetFloat(cs::String* name) { return anim_ ? anim_->GetFloat(U8(name)) : 0.0f; }
bool Animator::GetBool(cs::String* name) { return anim_ && anim_->GetBool(U8(name)); }
int32_t Animator::GetInteger(cs::String* name) { return anim_ ? anim_->GetInteger(U8(name)) : 0; }

void Animator::set_speed(float s)
{
    speed_ = s;
    if (anim_) anim_->speed = s;
}

void Animator::Play(cs::String* state, int32_t layer, float normalizedTime)
{
    (void)layer;
    if (!anim_) return;
    // (Play without a time: from the state's start when it isn't playing it already; Unity's
    // "-infinity" keeps the current time if it is. The callers here start states over.)
    anim_->Play(U8(state), std::isinf(normalizedTime) ? 0.0f : normalizedTime);
}

AnimatorStateInfo Animator::GetCurrentAnimatorStateInfo(int32_t layer)
{
    (void)layer;
    AnimatorStateInfo s;
    if (anim_) s.info = anim_->GetCurrentAnimatorStateInfo();
    return s;
}

bool Animator::IsInTransition(int32_t layer)
{
    (void)layer;
    return anim_ && anim_->IsInTransition();
}

namespace {
GameObject* Below(GameObject* root, const std::string& path)
{
    if (path.empty()) return root;
    Transform* t = root->transform_;
    size_t at = 0;
    while (t && at <= path.size()) {
        const size_t slash = path.find('/', at);
        const std::string head = path.substr(at, slash == std::string::npos ? std::string::npos : slash - at);
        Transform* next = nullptr;
        for (Transform* c : t->children_)
            if (c->gameObject_->get_name()->ToUtf8() == head) {
                next = c;
                break;
            }
        t = next;
        if (slash == std::string::npos) break;
        at = slash + 1;
    }
    return t ? t->gameObject_ : nullptr;
}
} // namespace

float Animator::Read(GameObject* g, const unity::AnimatedProperty& p)
{
    const std::string& a = p.attribute;
    if (a == "m_IsActive") return g->activeSelf_ ? 1.0f : 0.0f;
    if (a == "m_LocalPosition.x") return g->transform_->localPosition_.x;
    if (a == "m_LocalPosition.y") return g->transform_->localPosition_.y;
    if (a == "m_LocalPosition.z") return g->transform_->localPosition_.z;
    if (p.type == 61 || p.type == 58 || p.type == 60) { // BoxCollider2D, CircleCollider2D, PolygonCollider2D
        auto* c = g->GetComponent<Collider2D*>();
        auto* box = dynamic_cast<BoxCollider2D*>(c);
        if (!c) return 0.0f;
        if (a == "m_Enabled") return c->enabled_ ? 1.0f : 0.0f;
        if (a == "m_Offset.x") return c->offset_.x;
        if (a == "m_Offset.y") return c->offset_.y;
        if (box && a == "m_Size.x") return box->size_.x;
        if (box && a == "m_Size.y") return box->size_.y;
        if (a == "m_IsTrigger") return c->isTrigger_ ? 1.0f : 0.0f;
    }
    if (p.type == 212) {
        auto* r = g->GetComponent<SpriteRenderer*>();
        if (r && a == "m_Enabled") return r->enabled_ ? 1.0f : 0.0f;
        if (r && a == "m_FlipX") return r->flipX_ ? 1.0f : 0.0f;
        if (r && a == "m_Color.r") return r->color_.r;
        if (r && a == "m_Color.g") return r->color_.g;
        if (r && a == "m_Color.b") return r->color_.b;
        if (r && a == "m_Color.a") return r->color_.a;
    }
    if (a == "m_Enabled")
        for (Component* c : g->components_)
            if (auto* b = dynamic_cast<Behaviour*>(c)) return b->enabled_ ? 1.0f : 0.0f;
    if (p.type == 198) return 0.0f; // (a ParticleSystem's: presentation, not written)
    std::fprintf(stderr, "[bind] Animator: property %s/%s (class %d) not bound\n", p.path.c_str(), a.c_str(), p.type);
    return 0.0f;
}

void Animator::Write(GameObject* g, const unity::AnimatedProperty& p, float v)
{
    const std::string& a = p.attribute;
    if (a == "m_IsActive") {
        g->SetActive(v > 0.5f);
        return;
    }
    Transform* t = g->transform_;
    if (a == "m_LocalPosition.x" || a == "m_LocalPosition.y" || a == "m_LocalPosition.z") {
        Vector3 lp = t->localPosition_;
        float& c = a.back() == 'x' ? lp.x : a.back() == 'y' ? lp.y : lp.z;
        if (c == v) return;
        c = v;
        t->set_localPosition(lp);
        return;
    }
    if (p.type == 61 || p.type == 58 || p.type == 60) {
        auto* c = g->GetComponent<Collider2D*>();
        auto* box = dynamic_cast<BoxCollider2D*>(c);
        if (!c) return;
        if (a == "m_Enabled") c->set_enabled(v > 0.5f);
        else if (a == "m_Offset.x" && c->offset_.x != v) c->set_offset(Vector2{v, c->offset_.y});
        else if (a == "m_Offset.y" && c->offset_.y != v) c->set_offset(Vector2{c->offset_.x, v});
        else if (box && a == "m_Size.x" && box->size_.x != v) box->set_size(Vector2{v, box->size_.y});
        else if (box && a == "m_Size.y" && box->size_.y != v) box->set_size(Vector2{box->size_.x, v});
        else if (a == "m_IsTrigger") c->set_isTrigger(v > 0.5f);
        return;
    }
    if (p.type == 212) {
        auto* r = g->GetComponent<SpriteRenderer*>();
        if (r && a == "m_Enabled") r->enabled_ = v > 0.5f;
        if (r && a == "m_FlipX") r->flipX_ = v > 0.5f;
        if (r && a == "m_Color.r") r->color_.r = v;
        if (r && a == "m_Color.g") r->color_.g = v;
        if (r && a == "m_Color.b") r->color_.b = v;
        if (r && a == "m_Color.a") r->color_.a = v;
        return;
    }
    if (a == "m_Enabled")
        for (Component* c : g->components_)
            if (auto* b = dynamic_cast<Behaviour*>(c)) {
                b->set_enabled(v > 0.5f);
                return;
            }
}

void Animator::Bind()
{
    bound_ = true;
    targets_.clear();
    defaults_.clear();
    defaultSprites_.clear();
    spriteRenderer_ = nullptr;
    if (!anim_ || !gameObject_) return;
    const auto& props = controller_->properties;
    for (const unity::AnimatedProperty& p : props) {
        GameObject* g = Below(gameObject_, p.path);
        targets_.push_back(g);
        defaults_.push_back(0.0f);
        defaultSprites_.emplace_back();
        if (!g) continue;
        if (p.sprite) {
            auto* r = g->GetComponent<SpriteRenderer*>();
            defaultSprites_.back() = r ? r->SpriteName() : std::string();
            anim_->SetDefaultSprite(p.path, defaultSprites_.back());
        } else {
            defaults_.back() = Read(g, p);
            anim_->SetDefault(p.path, p.attribute, defaults_.back());
        }
    }
    if (props.empty()) spriteRenderer_ = spriteTarget_ ? spriteTarget_ : gameObject_->GetComponentInChildren<SpriteRenderer*>(true);
}

void Animator::Apply()
{
    const auto& props = controller_->properties;
    bool colliders = false;
    for (size_t i = 0; i < props.size(); i++) {
        GameObject* g = targets_[i];
        const unity::AnimatedProperty& p = props[i];
        if (!g || g->destroyed_) continue;
        if (p.sprite) {
            if (const std::string* s = anim_->Sprite(p.path)) {
                auto* r = g->GetComponent<SpriteRenderer*>();
                if (r) r->sprite_ = s->empty() ? nullptr : Sprite::Named(*s);
            }
            continue;
        }
        if (!anim_->Driven(static_cast<int>(i))) continue;
        Write(g, p, anim_->Property(static_cast<int>(i)));
        colliders = colliders || p.type == 61 || p.type == 58 || p.type == 60 || p.attribute == "m_IsActive" || p.attribute.rfind("m_LocalPosition", 0) == 0;
    }
    if (spriteRenderer_)
        if (const std::string* s = anim_->Sprite()) spriteRenderer_->sprite_ = s->empty() ? nullptr : Sprite::Named(*s);
    // Unity remakes the shapes of the colliders the Animator wrote as it writes them (the body's
    // physics sees them before the next step).
    if (colliders && S() && S()->physics)
        if (auto* rb = gameObject_->GetComponentInParent<Rigidbody2D*>()) S()->physics->SyncColliders(rb);
}

void Animator::UpdatePass(float deltaTime)
{
    if (!anim_ || destroyed_ || !enabled_ || !gameObject_ || !gameObject_->get_activeInHierarchy()) return;
    if (!bound_) Bind();
    anim_->Update(deltaTime);
    ran_ = true;
    if (get_applyRootMotion()) ApplyRootMotion();
    Apply();
}

// Its root motion (applyRootMotion on, the BrokenGolemTurret's): the clips have no root curves, so
// the body on its object gets their zero motion as its velocity (deltaPosition / deltaTime), and its
// rest starts over, every pass (game/EnemyRootMotion.cpp: the oracle's rootMotionProbe).
void Animator::ApplyRootMotion()
{
    if (!S() || !S()->physics || !gameObject_) return;
    if (auto* rb = gameObject_->GetComponent<Rigidbody2D*>())
        if (rb->body_ && !rb->destroyed_ && rb->body_->bodyType() == unity::RigidbodyType2D::Dynamic && rb->body_->Body()->IsActive()) {
            rb->body_->SetVelocity(Vector2{0.0f, 0.0f});
            S()->physics->world.RestartRest(*rb->body_);
        }
}

} // namespace UnityEngine

namespace bind {

void Scene::UpdateAnimators(float deltaTime) { UpdateAnimators(deltaTime, deltaTime); }

void Scene::UpdateAnimators(float deltaTime, float unscaledDeltaTime)
{
    // The Animators running as the pass starts: one its object came on during the pass (a pooled
    // shot a clip event took out) first runs in the next one (game/EnemyProjectile.h).
    std::vector<UnityEngine::Animator*> list;
    for (UnityEngine::Animator* a : animators)
        if (!a->destroyed_ && a->enabled_ && a->gameObject_ && a->gameObject_->get_activeInHierarchy()) list.push_back(a);
    for (UnityEngine::Animator* a : list) a->UpdatePass(a->unscaledTime_ ? unscaledDeltaTime : deltaTime);
    animators.erase(std::remove_if(animators.begin(), animators.end(), [](UnityEngine::Animator* a) { return a->destroyed_; }), animators.end());
}

} // namespace bind
