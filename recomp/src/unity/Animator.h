#pragma once
// Mecanim for one-layer sprite controllers: the extracted controller and clips
// (data/extracted/animators/<name>.json, tools/extract/animators.py) and an Animator that
// runs them the way Unity's does each frame: parameters and triggers, transitions in order
// (any-state first) with exit times and conditions, crossfades, state enter/exit callbacks
// (StateMachineBehaviours), clip events, and the sprite the SpriteRenderer shows.
//
// The exact format (tools/extract/animators.py --exact: the Broom Spear's) also brings the
// clips' other curves (streamed, dense and constant, as Unity compiles them), the states'
// writeDefaults and the renderer each clip's sprite curve drives: the Animator evaluates them
// after the state machine each Update into its animated properties (path below the Animator's
// object + attribute), which the owner reads and applies. The older format (the hero's,
// BabySlime's) has neither: its controllers animate no properties.
//
// Checked against the game frame by frame (data/oracle/walk.jsonl, player_walk_test); the
// curves against the extracted data (animator_curves_test).

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "util/Json.h"

namespace unity {

// Animator.StringToHash: CRC-32 of the name.
int32_t StringToHash(const std::string& name);

struct AnimatorCondition {
    enum Mode { If = 1, IfNot = 2, Greater = 3, Less = 4, Equals = 6, NotEqual = 7 };
    int mode = If;
    int param = -1; // index into the controller's params
    float threshold = 0.0f;
};

struct AnimatorTransition {
    int dest = 0;
    float duration = 0.0f, offset = 0.0f, exitTime = 0.0f;
    bool hasExitTime = false, fixedDuration = false, canSelf = false, ordered = true;
    int interruption = 0;
    std::vector<AnimatorCondition> conditions;
};

struct AnimationEvent {
    float time = 0.0f;
    std::string function, stringParameter;
    float floatParameter = 0.0f;
    int intParameter = 0;
};

// A property the controller's clips animate: an object below the Animator's (path) and an
// attribute as the clip names it ("m_IsActive", "m_Offset.x"; a Transform's with its component,
// "m_LocalPosition.x"; a sprite curve's "m_Sprite").
struct AnimatedProperty {
    std::string path, attribute;
    int type = 0;          // the component's class ID: 1 GameObject, 4 Transform, 61 BoxCollider2D, 212 SpriteRenderer
    bool discrete = false; // not blended in a crossfade: a bool (m_IsActive, m_Enabled) or the sprite
    bool sprite = false;
};

// One float curve of a clip, as Unity's compiled clip holds it.
struct AnimationCurve {
    enum Source { Streamed, Dense, Constant };
    // From `time` on (the first key at -FLT_MAX), ((c0 d + c1) d + c2) d + c3, d = t - time.
    struct Key {
        float time = 0.0f;
        float c[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    };
    int property = -1; // index into the controller's properties
    int source = Constant;
    std::vector<Key> keys;       // Streamed
    float begin = 0.0f, rate = 0.0f;
    std::vector<float> samples;  // Dense: from `begin`, 1 / rate apart
    float value = 0.0f;          // Constant
    float Evaluate(float time) const;
};

struct AnimationClip {
    std::string name;
    float length = 0.0f;
    bool loop = false;
    std::vector<std::pair<float, std::string>> sprites; // keys: from this time on, this sprite ("" None)
    std::vector<AnimationEvent> events;
    std::vector<AnimationCurve> curves;
    std::string spritePath;  // the renderer the sprite curve drives (exact format; "" none)
    int spriteProperty = -1; // its property
    const std::string* SpriteAt(float time) const;
};

struct AnimatorState {
    std::string name, tag;
    int32_t hash = 0, fullPathHash = 0, tagHash = 0;
    float speed = 1.0f;
    bool loop = false;
    bool writeDefaults = true; // m_WriteDefaultValues (the older format doesn't say: Unity's default)
    int clip = -1;
    std::vector<AnimatorTransition> transitions;
};

struct AnimatorParameter {
    enum Type { Float = 1, Int = 3, Bool = 4, Trigger = 9 };
    std::string name;
    int32_t hash = 0;
    int type = Bool;
    float defaultValue = 0.0f;
};

struct AnimatorController {
    std::string name;
    std::vector<AnimatorParameter> params;
    std::vector<AnimatorState> states;
    std::vector<AnimatorTransition> anyState;
    std::vector<AnimationClip> clips;
    std::vector<AnimatedProperty> properties; // every property the clips animate
    int defaultState = 0;
    int updateMode = -1; // the Animator's: 0 normal, 1 animate physics, 2 unscaled time (-1 unknown)

    static AnimatorController FromJson(const json::Value& v);
    int Param(const std::string& name) const; // -1 if none
    int Property(const std::string& path, const std::string& attribute) const; // -1 if no clip animates it
    int State(const std::string& name) const; // -1 if none
    int StateByHash(int32_t hash) const;
};

struct AnimatorStateInfo {
    int32_t shortNameHash = 0, fullPathHash = 0, tagHash = 0;
    float normalizedTime = 0.0f, length = 0.0f;
    bool loop = false;
    bool IsName(const std::string& name) const { return StringToHash(name) == shortNameHash; }
    bool IsTag(const std::string& tag) const { return StringToHash(tag) == tagHash; }
};

class Animator {
public:
    explicit Animator(const AnimatorController& controller);

    void SetBool(const std::string& name, bool value);
    void SetFloat(const std::string& name, float value);
    void SetInteger(const std::string& name, int value);
    void SetTrigger(const std::string& name);
    void ResetTrigger(const std::string& name);
    bool GetBool(const std::string& name) const;
    float GetFloat(const std::string& name) const;
    int GetInteger(const std::string& name) const;

    // The frame's animation pass (Unity runs it after the scripts' Update).
    void Update(float deltaTime);
    // Animator.speed: scales the time every state (and transition) advances by.
    float speed = 1.0f;
    // Animator.Play: straight to a state, at a normalized time.
    void Play(const std::string& state, float normalizedTime = 0.0f);

    AnimatorStateInfo GetCurrentAnimatorStateInfo() const { return Info(current_, time_); }
    AnimatorStateInfo GetNextAnimatorStateInfo() const { return inTransition_ ? Info(next_, nextTime_) : AnimatorStateInfo{}; }
    bool IsInTransition() const { return inTransition_; }
    float TransitionNormalizedTime() const { return transitionProgress_; }
    int CurrentState() const { return current_; }
    float CurrentTime() const { return time_; } // seconds into the current state
    // The sprite the SpriteRenderer shows (nullptr: the clip doesn't set one).
    const std::string* Sprite() const;

    // The animated properties (the controller's `properties`). Each Update ends with the
    // clips' pass: a property the playing state's clip animates gets the curve's value, one it
    // doesn't gets its default if the state writes defaults, else nothing (the component keeps
    // what it has). The owner applies, after Update, the properties Driven this frame.
    // Defaults are the values the properties had when Unity bound the Animator (the prefab's:
    // set them before the first Update); unset, 0 (and the sprite None).
    void SetDefault(const std::string& path, const std::string& attribute, float value);
    void SetDefaultSprite(const std::string& path, const std::string& sprite);
    // The property's value (the last written, else its default); `fallback` if no clip animates it.
    float Property(const std::string& path, const std::string& attribute, float fallback = 0.0f) const;
    bool Driven(const std::string& path, const std::string& attribute) const;
    float Property(int property) const { return slots_[static_cast<size_t>(property)].value; }
    bool Driven(int property) const { return slots_[static_cast<size_t>(property)].driven; }
    // The sprite the renderer at `path` shows from this frame's pass ("" None); nullptr if the
    // pass didn't write it (or no clip has a sprite curve for it).
    const std::string* Sprite(const std::string& path) const;

    // StateMachineBehaviour.OnStateEnter / OnStateExit (shortNameHash), and clip events.
    std::function<void(int32_t)> onStateEnter, onStateExit;
    std::function<void(const AnimationEvent&)> onEvent;

    const AnimatorController& Controller() const { return c_; }

private:
    AnimatorStateInfo Info(int state, float time) const;
    float Length(int state) const;
    bool Conditions(const AnimatorTransition& t) const;
    const AnimatorTransition* Find(const std::vector<AnimatorTransition>& list, int from, float prevNorm, float norm) const;
    void Take(const AnimatorTransition& t, float overshoot);
    // Fires the state's clip events in (from, to] (or (from, to) without `includeEnd`).
    void Advance(int state, float from, float to, bool includeEnd);
    void FireAt(int state, float time); // the events at exactly `time`
    int ParamOrThrow(const std::string& name, int type) const;
    void Step(float deltaTime);  // the state machine
    void Evaluate();             // the clips' pass into the properties
    float ClipTime(int state, float time) const;
    // The state's clip's values at `time` into side[p] (written: the clip animates p, or the
    // state writes defaults).
    struct Sample {
        bool written = false;
        float value = 0.0f;
        const std::string* sprite = nullptr; // nullptr: the default sprite
    };
    void SampleState(int state, float time, std::vector<Sample>& side) const;

    struct Slot {
        float value = 0.0f, defaultValue = 0.0f;
        std::string defaultSprite;
        const std::string* sprite = nullptr; // a clip's key, nullptr: the default (copies stay valid)
        bool driven = false;
    };

    const AnimatorController& c_;
    std::vector<float> values_;
    int current_ = 0, next_ = -1;
    float time_ = 0.0f, nextTime_ = 0.0f; // seconds into the state
    bool inTransition_ = false;
    float transitionProgress_ = 0.0f, transitionDuration_ = 0.0f;
    bool entered_ = false;
    std::vector<Slot> slots_;
    std::vector<Sample> sampleA_, sampleB_;
};

} // namespace unity
