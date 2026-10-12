#include "unity/Animator.h"

#include <cmath>
#include <stdexcept>

namespace unity {

int32_t StringToHash(const std::string& name)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char ch : name) {
        crc ^= ch;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return static_cast<int32_t>(~crc);
}

const std::string* AnimationClip::SpriteAt(float time) const
{
    const std::string* out = nullptr;
    for (const auto& [t, sprite] : sprites) {
        if (t > time) break;
        out = &sprite;
    }
    return out ? out : (sprites.empty() ? nullptr : &sprites.front().second);
}

namespace {

AnimatorTransition ReadTransition(const json::Value& t)
{
    AnimatorTransition out;
    out.dest = static_cast<int>(t["dest"].AsInt());
    out.duration = static_cast<float>(t["duration"].AsDouble());
    out.offset = static_cast<float>(t["offset"].AsDouble());
    out.exitTime = static_cast<float>(t["exitTime"].AsDouble());
    out.hasExitTime = t["hasExitTime"].b;
    out.fixedDuration = t["fixedDuration"].b;
    out.canSelf = t["canSelf"].b;
    out.ordered = t["ordered"].b;
    out.interruption = static_cast<int>(t["interruption"].AsInt());
    for (const json::Value& c : t["conditions"].arr)
        out.conditions.push_back({static_cast<int>(c[0].AsInt()), static_cast<int>(c[1].AsInt()), static_cast<float>(c[2].AsDouble())});
    return out;
}

} // namespace

AnimatorController AnimatorController::FromJson(const json::Value& v)
{
    AnimatorController c;
    c.name = v["controller"].s;
    c.defaultState = static_cast<int>(v["defaultState"].AsInt());
    for (const json::Value& p : v["params"].arr) {
        AnimatorParameter out;
        out.name = p["name"].s;
        out.hash = static_cast<int32_t>(p["id"].AsInt());
        out.type = static_cast<int>(p["type"].AsInt());
        const json::Value& d = p["default"];
        out.defaultValue = d.kind == json::Value::Kind::Bool ? (d.b ? 1.0f : 0.0f) : static_cast<float>(d.AsDouble());
        c.params.push_back(out);
    }
    for (const json::Value& s : v["states"].arr) {
        AnimatorState out;
        out.name = s["name"].s;
        out.tag = s["tag"].s;
        out.hash = static_cast<int32_t>(s["hash"].AsInt());
        out.fullPathHash = static_cast<int32_t>(s["fullPathHash"].AsInt());
        out.tagHash = static_cast<int32_t>(s["tagHash"].AsInt());
        out.speed = static_cast<float>(s["speed"].AsDouble());
        out.loop = s["loop"].b;
        out.clip = static_cast<int>(s["clip"].AsInt());
        for (const json::Value& t : s["transitions"].arr) out.transitions.push_back(ReadTransition(t));
        c.states.push_back(std::move(out));
    }
    for (const json::Value& t : v["anyState"].arr) c.anyState.push_back(ReadTransition(t));
    for (const json::Value& k : v["clips"].arr) {
        AnimationClip out;
        out.name = k["name"].s;
        out.length = static_cast<float>(k["length"].AsDouble());
        out.loop = k["loop"].b;
        for (const json::Value& s : k["sprites"].arr) out.sprites.emplace_back(std::max(0.0f, static_cast<float>(s[0].AsDouble())), s[1].s);
        for (const json::Value& e : k["events"].arr)
            out.events.push_back({static_cast<float>(e[0].AsDouble()), e[1].s, e[2].s, static_cast<float>(e[3].AsDouble()), static_cast<int>(e[4].AsInt())});
        c.clips.push_back(std::move(out));
    }
    return c;
}

int AnimatorController::Param(const std::string& name) const
{
    for (size_t i = 0; i < params.size(); i++)
        if (params[i].name == name) return static_cast<int>(i);
    return -1;
}

int AnimatorController::State(const std::string& name) const
{
    for (size_t i = 0; i < states.size(); i++)
        if (states[i].name == name) return static_cast<int>(i);
    return -1;
}

int AnimatorController::StateByHash(int32_t hash) const
{
    for (size_t i = 0; i < states.size(); i++)
        if (states[i].hash == hash) return static_cast<int>(i);
    return -1;
}

Animator::Animator(const AnimatorController& controller) : c_(controller), current_(controller.defaultState)
{
    for (const AnimatorParameter& p : c_.params) values_.push_back(p.defaultValue);
}

int Animator::ParamOrThrow(const std::string& name, int type) const
{
    int i = c_.Param(name);
    // Unity logs "Parameter does not exist" and carries on; so do we, but quietly.
    if (i >= 0 && c_.params[static_cast<size_t>(i)].type != type && !(type == AnimatorParameter::Bool && c_.params[static_cast<size_t>(i)].type == AnimatorParameter::Trigger))
        return -1;
    return i;
}

void Animator::SetBool(const std::string& name, bool value)
{
    int i = ParamOrThrow(name, AnimatorParameter::Bool);
    if (i >= 0) values_[static_cast<size_t>(i)] = value ? 1.0f : 0.0f;
}

void Animator::SetFloat(const std::string& name, float value)
{
    int i = ParamOrThrow(name, AnimatorParameter::Float);
    if (i >= 0) values_[static_cast<size_t>(i)] = value;
}

void Animator::SetInteger(const std::string& name, int value)
{
    int i = ParamOrThrow(name, AnimatorParameter::Int);
    if (i >= 0) values_[static_cast<size_t>(i)] = static_cast<float>(value);
}

void Animator::SetTrigger(const std::string& name)
{
    int i = ParamOrThrow(name, AnimatorParameter::Trigger);
    if (i >= 0) values_[static_cast<size_t>(i)] = 1.0f;
}

void Animator::ResetTrigger(const std::string& name)
{
    int i = ParamOrThrow(name, AnimatorParameter::Trigger);
    if (i >= 0) values_[static_cast<size_t>(i)] = 0.0f;
}

bool Animator::GetBool(const std::string& name) const
{
    int i = c_.Param(name);
    return i >= 0 && values_[static_cast<size_t>(i)] != 0.0f;
}

float Animator::GetFloat(const std::string& name) const
{
    int i = c_.Param(name);
    return i >= 0 ? values_[static_cast<size_t>(i)] : 0.0f;
}

int Animator::GetInteger(const std::string& name) const
{
    int i = c_.Param(name);
    return i >= 0 ? static_cast<int>(values_[static_cast<size_t>(i)]) : 0;
}

float Animator::Length(int state) const
{
    const AnimatorState& s = c_.states[static_cast<size_t>(state)];
    return s.clip >= 0 ? c_.clips[static_cast<size_t>(s.clip)].length : 1.0f;
}

AnimatorStateInfo Animator::Info(int state, float time) const
{
    AnimatorStateInfo info;
    if (state < 0) return info;
    const AnimatorState& s = c_.states[static_cast<size_t>(state)];
    info.shortNameHash = s.hash;
    info.fullPathHash = s.fullPathHash;
    info.tagHash = s.tagHash;
    info.length = Length(state);
    info.loop = s.loop;
    info.normalizedTime = info.length > 0.0f ? time / info.length : 0.0f;
    return info;
}

const std::string* Animator::Sprite() const
{
    int state = inTransition_ && transitionProgress_ >= 0.5f ? next_ : current_;
    float time = state == next_ && inTransition_ ? nextTime_ : time_;
    const AnimatorState& s = c_.states[static_cast<size_t>(state)];
    if (s.clip < 0) return nullptr;
    const AnimationClip& clip = c_.clips[static_cast<size_t>(s.clip)];
    float t = clip.loop && clip.length > 0.0f ? std::fmod(time, clip.length) : std::min(time, clip.length);
    return clip.SpriteAt(t);
}

bool Animator::Conditions(const AnimatorTransition& t) const
{
    for (const AnimatorCondition& c : t.conditions) {
        float v = c.param >= 0 ? values_[static_cast<size_t>(c.param)] : 0.0f;
        switch (c.mode) {
        case AnimatorCondition::If: if (v == 0.0f) return false; break;
        case AnimatorCondition::IfNot: if (v != 0.0f) return false; break;
        case AnimatorCondition::Greater: if (!(v > c.threshold)) return false; break;
        case AnimatorCondition::Less: if (!(v < c.threshold)) return false; break;
        case AnimatorCondition::Equals: if (v != c.threshold) return false; break;
        case AnimatorCondition::NotEqual: if (v == c.threshold) return false; break;
        default: return false;
        }
    }
    return true;
}

const AnimatorTransition* Animator::Find(const std::vector<AnimatorTransition>& list, int from, float prevNorm, float norm) const
{
    for (const AnimatorTransition& t : list) {
        if (from < 0 && t.dest == current_ && !t.canSelf) continue; // any state
        if (t.hasExitTime) {
            // Crossed the exit time this frame (each loop's, below 1).
            bool crossed;
            if (t.exitTime < 1.0f) {
                float k = std::floor(norm - t.exitTime);
                crossed = k >= 0.0f && prevNorm < k + t.exitTime;
            } else {
                crossed = prevNorm < t.exitTime && norm >= t.exitTime;
            }
            if (!crossed) continue;
        }
        if (Conditions(t)) return &t;
    }
    return nullptr;
}

void Animator::Advance(int state, float from, float to, bool includeEnd)
{
    if (!onEvent || state < 0) return;
    const AnimatorState& s = c_.states[static_cast<size_t>(state)];
    if (s.clip < 0) return;
    const AnimationClip& clip = c_.clips[static_cast<size_t>(s.clip)];
    if (clip.events.empty() || clip.length <= 0.0f) return;
    int first = clip.loop ? static_cast<int>(std::floor(from / clip.length)) : 0;
    int last = clip.loop ? static_cast<int>(std::floor(to / clip.length)) : 0;
    for (int k = first; k <= last; k++) {
        for (const AnimationEvent& e : clip.events) {
            float at = static_cast<float>(k) * clip.length + e.time;
            if (at > from && (includeEnd ? at <= to : at < to)) onEvent(e);
        }
    }
}

void Animator::FireAt(int state, float time)
{
    if (!onEvent || state < 0) return;
    const AnimatorState& s = c_.states[static_cast<size_t>(state)];
    if (s.clip < 0) return;
    const AnimationClip& clip = c_.clips[static_cast<size_t>(s.clip)];
    float t = clip.loop && clip.length > 0.0f ? std::fmod(time, clip.length) : time;
    for (const AnimationEvent& e : clip.events)
        if (e.time == t) onEvent(e);
}

void Animator::Take(const AnimatorTransition& t, float overshoot)
{
    for (const AnimatorCondition& c : t.conditions)
        if (c.param >= 0 && c_.params[static_cast<size_t>(c.param)].type == AnimatorParameter::Trigger) values_[static_cast<size_t>(c.param)] = 0.0f;
    float start = t.offset * Length(t.dest) + overshoot;
    if (t.duration <= 0.0f) {
        if (onStateExit) onStateExit(c_.states[static_cast<size_t>(current_)].hash);
        current_ = t.dest;
        time_ = start;
        if (onStateEnter) onStateEnter(c_.states[static_cast<size_t>(current_)].hash);
        // The new state is evaluated this frame: from its start through the time carried over.
        const float base = start - overshoot;
        FireAt(current_, base);
        if (overshoot > 0.0f) Advance(current_, base, start, true);
        return;
    }
    inTransition_ = true;
    next_ = t.dest;
    nextTime_ = start;
    transitionProgress_ = 0.0f;
    transitionDuration_ = t.fixedDuration ? t.duration : t.duration * Length(current_);
    if (onStateEnter) onStateEnter(c_.states[static_cast<size_t>(next_)].hash);
}

void Animator::Play(const std::string& state, float normalizedTime)
{
    int s = c_.State(state);
    if (s < 0) return;
    if (onStateExit) onStateExit(c_.states[static_cast<size_t>(current_)].hash);
    if (inTransition_ && onStateExit) onStateExit(c_.states[static_cast<size_t>(next_)].hash);
    inTransition_ = false;
    current_ = s;
    time_ = normalizedTime * Length(s);
    if (onStateEnter) onStateEnter(c_.states[static_cast<size_t>(current_)].hash);
}

void Animator::Update(float deltaTime)
{
    deltaTime = deltaTime * speed;
    if (!entered_) {
        entered_ = true;
        if (onStateEnter) onStateEnter(c_.states[static_cast<size_t>(current_)].hash);
    }
    const AnimatorState& s = c_.states[static_cast<size_t>(current_)];
    float prev = time_;
    time_ += deltaTime * s.speed;
    if (inTransition_) {
        Advance(current_, prev, time_, true);
        const AnimatorState& n = c_.states[static_cast<size_t>(next_)];
        float nprev = nextTime_;
        nextTime_ += deltaTime * n.speed;
        Advance(next_, nprev, nextTime_, true);
        transitionProgress_ += transitionDuration_ > 0.0f ? deltaTime / transitionDuration_ : 1.0f;
        if (transitionProgress_ >= 1.0f) {
            if (onStateExit) onStateExit(s.hash);
            current_ = next_;
            time_ = nextTime_;
            next_ = -1;
            inTransition_ = false;
            transitionProgress_ = 0.0f;
        }
        return;
    }
    float len = Length(current_);
    float prevNorm = prev / len, norm = time_ / len;
    const AnimatorTransition* t = Find(c_.anyState, -1, prevNorm, norm);
    if (!t) t = Find(s.transitions, current_, prevNorm, norm);
    if (t && t->hasExitTime) {
        // The state plays up to its exit time (an event right at it doesn't fire: the clip
        // looping back to its start as the roll ends doesn't start it again), and the rest of
        // the frame carries into the next state.
        float crossing = t->exitTime < 1.0f ? std::floor(norm - t->exitTime) + t->exitTime : t->exitTime;
        Advance(current_, prev, crossing * len, false);
        Take(*t, (norm - crossing) * len);
        return;
    }
    Advance(current_, prev, time_, true);
    if (t) Take(*t, 0.0f);
}

} // namespace unity
