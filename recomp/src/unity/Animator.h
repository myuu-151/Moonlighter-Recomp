#pragma once
// Mecanim for one-layer sprite controllers: the extracted controller and clips
// (data/extracted/animators/<name>.json, tools/extract/animators.py) and an Animator that
// runs them the way Unity's does each frame: parameters and triggers, transitions in order
// (any-state first) with exit times and conditions, crossfades, state enter/exit callbacks
// (StateMachineBehaviours), clip events, and the sprite the SpriteRenderer shows.
//
// Checked against the game frame by frame (data/oracle/walk.jsonl, player_walk_test).

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

struct AnimationClip {
    std::string name;
    float length = 0.0f;
    bool loop = false;
    std::vector<std::pair<float, std::string>> sprites; // keys: from this time on, this sprite
    std::vector<AnimationEvent> events;
    const std::string* SpriteAt(float time) const;
};

struct AnimatorState {
    std::string name, tag;
    int32_t hash = 0, fullPathHash = 0, tagHash = 0;
    float speed = 1.0f;
    bool loop = false;
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
    int defaultState = 0;

    static AnimatorController FromJson(const json::Value& v);
    int Param(const std::string& name) const; // -1 if none
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
    // The sprite the SpriteRenderer shows (nullptr: the clip doesn't set one).
    const std::string* Sprite() const;

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

    const AnimatorController& c_;
    std::vector<float> values_;
    int current_ = 0, next_ = -1;
    float time_ = 0.0f, nextTime_ = 0.0f; // seconds into the state
    bool inTransition_ = false;
    float transitionProgress_ = 0.0f, transitionDuration_ = 0.0f;
    bool entered_ = false;
};

} // namespace unity
