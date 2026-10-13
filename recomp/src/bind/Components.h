#pragma once
// The engine components translated scripts reach besides the scene graph and the physics:
// the Animator (on the hand-written Mecanim, unity/Animator.h), the renderers with their
// material's properties, sprites, particle systems.
//
// The Animator runs in the scene's animation pass (bind::Scene::UpdateAnimators, after the
// Updates and the delayed calls, Unity's order): its state machine and clips (unity::Animator),
// then what the clips write, applied to the objects below it as Unity binds them (path +
// attribute): m_IsActive (GameObject.SetActive), m_Enabled (a Behaviour's or Collider2D's
// enabled), m_Offset / m_Size (a BoxCollider2D's), m_LocalPosition (the Transform), m_Sprite
// (the SpriteRenderer). The defaults those properties go back to (write defaults) are what the
// objects have when the Animator binds them: its first update with a controller. A controller
// in the older extracted format (no property curves, no sprite path) drives the sprite of the
// first SpriteRenderer below the Animator (what the game's sprite enemies have). Clip events
// go to the Animator's object's scripts by name (GameObject::SendAnimationEvent).
//
// Renderer.material is the renderer's own copy (Unity instantiates it on first access): its
// properties are kept by name; a property never set reads as the shader's default the game's
// materials have (_FlashAmount 0, _Color white).

#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "bind/Physics.h"
#include "bind/Scene.h"
#include "unity/Animator.h"

namespace UnityEngine {

// ---- assets --------------------------------------------------------------------------------------

// Textures: their size (their pixels are presentation).
class Texture : public Object {
public:
    const char* cs_typename() const override { return "UnityEngine.Texture"; }
    static const char* cs_static_typename() { return "UnityEngine.Texture"; }
    int32_t get_width() { return width_; }
    int32_t get_height() { return height_; }
    int32_t width_ = 0, height_ = 0;
};
class Texture2D : public Texture {
public:
    const char* cs_typename() const override { return "UnityEngine.Texture2D"; }
    static const char* cs_static_typename() { return "UnityEngine.Texture2D"; }
    void __ctor(int32_t width, int32_t height)
    {
        width_ = width;
        height_ = height;
    }
};

class Sprite : public Object {
public:
    const char* cs_typename() const override { return "UnityEngine.Sprite"; }
    static const char* cs_static_typename() { return "UnityEngine.Sprite"; }
    // A sprite by name (the extracted sprites'), one object per name.
    static Sprite* Named(const std::string& name);
    std::string spriteName;
};

class Material : public Object {
public:
    const char* cs_typename() const override { return "UnityEngine.Material"; }
    static const char* cs_static_typename() { return "UnityEngine.Material"; }
    float GetFloat(cs::String* name);
    void SetFloat(cs::String* name, float value);
    Color GetColor(cs::String* name);
    void SetColor(cs::String* name, Color value);
    Color get_color() { return GetColor(CS_LIT(u"_Color")); }
    void set_color(Color c) { SetColor(CS_LIT(u"_Color"), c); }
    bool HasProperty(cs::String* name);
    // By name, for drivers and checks.
    float Float(const std::string& name) const;
    Color ColorOf(const std::string& name) const;
    void SetFloat(const std::string& name, float value) { floats_[name] = value; }
    void SetColor(const std::string& name, Color value) { colors_[name] = value; }

private:
    std::map<std::string, float> floats_;
    std::map<std::string, Color> colors_;
};

class RuntimeAnimatorController : public Object {
public:
    const char* cs_typename() const override { return "UnityEngine.RuntimeAnimatorController"; }
    const unity::AnimatorController* controller = nullptr;
};

// ---- renderers ------------------------------------------------------------------------------------

class Renderer : public Component {
public:
    const char* cs_typename() const override { return "UnityEngine.Renderer"; }
    static const char* cs_static_typename() { return "UnityEngine.Renderer"; }
    void cs_trace(cs::Tracer& t) override;
    bool get_enabled() { return enabled_; }
    void set_enabled(bool v) { enabled_ = v; }
    Material* get_material();
    void set_material(Material* m) { material_ = m; }
    Material* get_sharedMaterial() { return get_material(); }
    int32_t get_sortingOrder() { return sortingOrder_; }
    void set_sortingOrder(int32_t o) { sortingOrder_ = o; }
    cs::String* get_sortingLayerName() { return sortingLayer_ ? sortingLayer_ : cs::String::get_Empty(); }
    void set_sortingLayerName(cs::String* s) { sortingLayer_ = s; }
    bool get_isVisible() { return enabled_ && gameObject_ && gameObject_->get_activeInHierarchy(); }
    Object* cs_Clone() override;

    bool enabled_ = true;
    Material* material_ = nullptr;
    int32_t sortingOrder_ = 0;
    cs::String* sortingLayer_ = nullptr;
};

class SpriteRenderer : public Renderer {
public:
    const char* cs_typename() const override { return "UnityEngine.SpriteRenderer"; }
    static const char* cs_static_typename() { return "UnityEngine.SpriteRenderer"; }
    void cs_trace(cs::Tracer& t) override;
    Sprite* get_sprite() { return sprite_; }
    void set_sprite(Sprite* s) { sprite_ = s; }
    Color get_color() { return color_; }
    void set_color(Color c) { color_ = c; }
    bool get_flipX() { return flipX_; }
    void set_flipX(bool f) { flipX_ = f; }
    bool get_flipY() { return flipY_; }
    void set_flipY(bool f) { flipY_ = f; }
    // The sprite's name ("" none).
    std::string SpriteName() const { return sprite_ ? sprite_->spriteName : std::string(); }
    Object* cs_Clone() override;

    Sprite* sprite_ = nullptr;
    Color color_{1.0f, 1.0f, 1.0f, 1.0f};
    bool flipX_ = false, flipY_ = false;
};

class MeshRenderer : public Renderer {
public:
    const char* cs_typename() const override { return "UnityEngine.MeshRenderer"; }
    static const char* cs_static_typename() { return "UnityEngine.MeshRenderer"; }
    Object* cs_Clone() override;
};

// A particle system: presentation (its particles aren't simulated), the calls do nothing.
class ParticleSystem : public Component {
public:
    const char* cs_typename() const override { return "UnityEngine.ParticleSystem"; }
    static const char* cs_static_typename() { return "UnityEngine.ParticleSystem"; }
    void Play() {}
    void Play(bool) {}
    void Stop() {}
    void Stop(bool) {}
    void Clear() {}
    bool get_isPlaying() { return false; }
    Object* cs_Clone() override;
};

// ---- the Animator -------------------------------------------------------------------------------------

struct AnimatorStateInfo {
    unity::AnimatorStateInfo info;
    int32_t get_shortNameHash() { return info.shortNameHash; }
    int32_t get_fullPathHash() { return info.fullPathHash; }
    int32_t get_tagHash() { return info.tagHash; }
    float get_normalizedTime() { return info.normalizedTime; }
    float get_length() { return info.length; }
    bool get_loop() { return info.loop; }
    bool IsName(cs::String* name) { return info.IsName(cs::nc(name)->ToUtf8()); }
    bool IsTag(cs::String* tag) { return info.IsTag(cs::nc(tag)->ToUtf8()); }
};
inline void cs_trace_value(cs::Tracer&, const AnimatorStateInfo&) {}

class Animator : public Behaviour {
public:
    const char* cs_typename() const override { return "UnityEngine.Animator"; }
    static const char* cs_static_typename() { return "UnityEngine.Animator"; }
    void cs_trace(cs::Tracer& t) override;

    // The controller (a prefab's: the builder's, or runtimeAnimatorController's set).
    void SetController(const unity::AnimatorController* controller);
    RuntimeAnimatorController* get_runtimeAnimatorController() { return runtime_; }
    void set_runtimeAnimatorController(RuntimeAnimatorController* c);

    void SetFloat(cs::String* name, float value);
    void SetBool(cs::String* name, bool value);
    void SetInteger(cs::String* name, int32_t value);
    void SetTrigger(cs::String* name);
    void ResetTrigger(cs::String* name);
    float GetFloat(cs::String* name);
    bool GetBool(cs::String* name);
    int32_t GetInteger(cs::String* name);
    float get_speed() { return anim_ ? anim_->speed : speed_; }
    void set_speed(float s);
    void Play(cs::String* state) { Play(state, -1, -std::numeric_limits<float>::infinity()); }
    void Play(cs::String* state, int32_t layer) { Play(state, layer, -std::numeric_limits<float>::infinity()); }
    void Play(cs::String* state, int32_t layer, float normalizedTime);
    AnimatorStateInfo GetCurrentAnimatorStateInfo(int32_t layer);
    bool IsInTransition(int32_t layer);
    static int32_t StringToHash(cs::String* name) { return unity::StringToHash(cs::nc(name)->ToUtf8()); }

    // The animation pass (Scene::UpdateAnimators): the state machine, the clips' writes applied.
    void UpdatePass(float deltaTime);
    unity::Animator* Mecanim() { return anim_.get(); }
    // Each clip event before (after = false) and after the scripts got it (drivers' checks).
    std::function<void(Animator&, const unity::AnimationEvent&, bool after)> onEvent;

    void cs_Attached() override;
    Object* cs_Clone() override;

private:
    void Bind(); // the properties' targets and defaults, as the objects are now
    void Apply(); // the properties the pass wrote
    float Read(GameObject* target, const unity::AnimatedProperty& p);
    void Write(GameObject* target, const unity::AnimatedProperty& p, float value);

    const unity::AnimatorController* controller_ = nullptr;
    RuntimeAnimatorController* runtime_ = nullptr;
    std::unique_ptr<unity::Animator> anim_;
    float speed_ = 1.0f;
    bool bound_ = false;
    std::vector<GameObject*> targets_; // per controller property (nullptr: no such object)
    SpriteRenderer* spriteRenderer_ = nullptr; // the older format's
};

} // namespace UnityEngine
