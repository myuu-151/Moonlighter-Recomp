#include "unity/Animator.h"

#include <algorithm>
#include <cfloat>
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

float AnimationCurve::Evaluate(float time) const
{
    switch (source) {
    case Streamed: {
        // The key in force: the last at or before `time` (the first is at -FLT_MAX, holding
        // the value before the clip's first key: a constant, its d would be ~FLT_MAX).
        const Key* k = keys.empty() ? nullptr : &keys.front();
        for (const Key& key : keys) {
            if (key.time > time) break;
            k = &key;
        }
        if (!k) return 0.0f;
        if (k->time == -FLT_MAX) return k->c[3];
        const float d = time - k->time;
        return ((k->c[0] * d + k->c[1]) * d + k->c[2]) * d + k->c[3];
    }
    case Dense: {
        // Unverified: linear between the samples about `time`, held at both ends.
        if (samples.empty()) return 0.0f;
        const float at = (time - begin) * rate;
        if (!(at > 0.0f)) return samples.front();
        const size_t last = samples.size() - 1;
        if (at >= static_cast<float>(last)) return samples.back();
        const size_t i = static_cast<size_t>(std::floor(at));
        const float f = at - static_cast<float>(i);
        return samples[i] + (samples[i + 1] - samples[i]) * f;
    }
    default: return value;
    }
}

namespace {

// A path or attribute the extractor couldn't name is its number.
std::string Name(const json::Value& v)
{
    return v.IsString() ? v.s : std::to_string(v.AsInt());
}

int AddProperty(std::vector<AnimatedProperty>& properties, const AnimatedProperty& p)
{
    for (size_t i = 0; i < properties.size(); i++)
        if (properties[i].path == p.path && properties[i].attribute == p.attribute) return static_cast<int>(i);
    properties.push_back(p);
    return static_cast<int>(properties.size() - 1);
}

// The exact format's curves of a clip (the older format's are key values only: not read).
void ReadCurves(AnimatorController& c, AnimationClip& clip, const json::Value& k)
{
    const json::Value* sp = k.Find("spritePath");
    if (!sp) return;
    if (!sp->IsNull()) {
        clip.spritePath = Name(*sp);
        AnimatedProperty p;
        p.path = clip.spritePath;
        p.attribute = "m_Sprite";
        p.type = 212;
        p.discrete = p.sprite = true;
        clip.spriteProperty = AddProperty(c.properties, p);
    }
    for (const json::Value& cv : k["curves"].arr) {
        AnimatedProperty p;
        p.path = Name(cv["path"]);
        p.attribute = Name(cv["attribute"]);
        p.type = static_cast<int>(cv["type"].AsInt());
        if (p.type == 4) p.attribute += std::string(".") + "xyzw"[cv["component"].AsInt() & 3];
        p.discrete = p.type == 1 || p.attribute == "m_Enabled";
        AnimationCurve curve;
        curve.property = AddProperty(c.properties, p);
        const std::string& source = cv["source"].s;
        if (source == "streamed") {
            curve.source = AnimationCurve::Streamed;
            for (const json::Value& key : cv["keys"].arr) {
                AnimationCurve::Key out;
                out.time = static_cast<float>(key[0].AsDouble());
                for (size_t j = 0; j < 4; j++) out.c[j] = static_cast<float>(key[j + 1].AsDouble());
                curve.keys.push_back(out);
            }
        } else if (source == "dense") {
            curve.source = AnimationCurve::Dense;
            curve.begin = static_cast<float>(cv["begin"].AsDouble());
            curve.rate = static_cast<float>(cv["rate"].AsDouble());
            for (const json::Value& x : cv["samples"].arr) curve.samples.push_back(static_cast<float>(x.AsDouble()));
        } else if (source == "constant") {
            curve.source = AnimationCurve::Constant;
            curve.value = static_cast<float>(cv["value"].AsDouble());
        } else {
            throw std::runtime_error("animator " + c.name + ": clip " + clip.name + ": curve source '" + source + "'");
        }
        clip.curves.push_back(std::move(curve));
    }
}

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
    if (const json::Value* m = v.Find("updateMode"); m && !m->IsNull()) c.updateMode = static_cast<int>(m->AsInt());
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
        if (const json::Value* w = s.Find("writeDefaults")) out.writeDefaults = w->b;
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
        ReadCurves(c, out, k);
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

int AnimatorController::Property(const std::string& path, const std::string& attribute) const
{
    for (size_t i = 0; i < properties.size(); i++)
        if (properties[i].path == path && properties[i].attribute == attribute) return static_cast<int>(i);
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
    slots_.resize(c_.properties.size());
    sampleA_.resize(slots_.size());
    sampleB_.resize(slots_.size());
}

void Animator::SetDefault(const std::string& path, const std::string& attribute, float value)
{
    int p = c_.Property(path, attribute);
    if (p < 0) return;
    Slot& slot = slots_[static_cast<size_t>(p)];
    slot.defaultValue = value;
    if (!entered_) slot.value = value;
}

void Animator::SetDefaultSprite(const std::string& path, const std::string& sprite)
{
    int p = c_.Property(path, "m_Sprite");
    if (p >= 0) slots_[static_cast<size_t>(p)].defaultSprite = sprite;
}

float Animator::Property(const std::string& path, const std::string& attribute, float fallback) const
{
    int p = c_.Property(path, attribute);
    return p >= 0 ? Property(p) : fallback;
}

bool Animator::Driven(const std::string& path, const std::string& attribute) const
{
    int p = c_.Property(path, attribute);
    return p >= 0 && Driven(p);
}

const std::string* Animator::Sprite(const std::string& path) const
{
    int p = c_.Property(path, "m_Sprite");
    if (p < 0 || !slots_[static_cast<size_t>(p)].driven) return nullptr;
    const Slot& slot = slots_[static_cast<size_t>(p)];
    return slot.sprite ? slot.sprite : &slot.defaultSprite;
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
    info.normalizedTime = time;
    return info;
}

float Animator::ClipTime(int state, float normalizedTime) const
{
    const AnimatorState& s = c_.states[static_cast<size_t>(state)];
    const float time = normalizedTime * Length(state);
    if (s.clip < 0) return time;
    const AnimationClip& clip = c_.clips[static_cast<size_t>(s.clip)];
    return clip.loop && clip.length > 0.0f ? std::fmod(time, clip.length) : std::min(time, clip.length);
}

const std::string* Animator::Sprite() const
{
    int state = inTransition_ && transitionProgress_ >= 0.5f ? next_ : current_;
    float time = state == next_ && inTransition_ ? nextTime_ : time_;
    const AnimatorState& s = c_.states[static_cast<size_t>(state)];
    if (s.clip < 0) return nullptr;
    return c_.clips[static_cast<size_t>(s.clip)].SpriteAt(ClipTime(state, time));
}

void Animator::SampleState(int state, float time, std::vector<Sample>& side) const
{
    const AnimatorState& s = c_.states[static_cast<size_t>(state)];
    for (size_t p = 0; p < side.size(); p++) {
        side[p].written = s.writeDefaults;
        side[p].value = slots_[p].defaultValue;
        side[p].sprite = nullptr; // the default
    }
    if (s.clip < 0) return;
    const AnimationClip& clip = c_.clips[static_cast<size_t>(s.clip)];
    const float t = ClipTime(state, time);
    for (const AnimationCurve& curve : clip.curves) {
        Sample& out = side[static_cast<size_t>(curve.property)];
        out.written = true;
        out.value = curve.Evaluate(t);
    }
    if (clip.spriteProperty >= 0) {
        Sample& out = side[static_cast<size_t>(clip.spriteProperty)];
        out.written = true;
        out.sprite = clip.SpriteAt(t);
    }
}

void Animator::Evaluate()
{
    if (slots_.empty()) return;
    // A state without a clip writes its defaults from the frame after it's entered: the frame
    // it's entered keeps what was there (the gloves' AttackIdle after a punch, in the game).
    if (motionlessEntered_ && !inTransition_) {
        motionlessEntered_ = false;
        for (Slot& slot : slots_) slot.driven = false;
        return;
    }
    motionlessEntered_ = false;
    SampleState(current_, time_, sampleA_);
    if (inTransition_) SampleState(next_, nextTime_, sampleB_);
    // Unverified (no transition of the Broom Spear's has a duration): in a crossfade a float
    // blends by the transition's progress, a discrete property (a bool, the sprite) takes the
    // dominant state's (the next from half way, as Sprite() does); a side that doesn't write
    // the property (no curve, no write defaults) contributes the property's current value.
    const float w = transitionProgress_;
    for (size_t p = 0; p < slots_.size(); p++) {
        Slot& slot = slots_[p];
        const Sample& a = sampleA_[p];
        if (!inTransition_) {
            slot.driven = a.written;
            if (a.written) {
                slot.value = a.value;
                slot.sprite = a.sprite;
            }
            continue;
        }
        const Sample& b = sampleB_[p];
        slot.driven = a.written || b.written;
        if (!slot.driven) continue;
        const float va = a.written ? a.value : slot.value, vb = b.written ? b.value : slot.value;
        const std::string* sa = a.written ? a.sprite : slot.sprite;
        const std::string* sb = b.written ? b.sprite : slot.sprite;
        if (c_.properties[p].discrete) {
            slot.value = w >= 0.5f ? vb : va;
            slot.sprite = w >= 0.5f ? sb : sa;
        } else {
            slot.value = va + (vb - va) * w;
        }
    }
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

void Animator::Take(const AnimatorTransition& t, float overshoot, float elapsed)
{
    for (const AnimatorCondition& c : t.conditions)
        if (c.param >= 0 && c_.params[static_cast<size_t>(c.param)].type == AnimatorParameter::Trigger) values_[static_cast<size_t>(c.param)] = 0.0f;
    const float destLength = Length(t.dest);
    const float start = t.offset + overshoot / destLength;
    if (t.duration <= 0.0f) {
        if (onStateExit) onStateExit(c_.states[static_cast<size_t>(current_)].hash);
        current_ = t.dest;
        time_ = start;
        motionlessEntered_ = c_.states[static_cast<size_t>(current_)].clip < 0;
        if (onStateEnter) onStateEnter(c_.states[static_cast<size_t>(current_)].hash);
        // The new state is evaluated this frame: from its start through the time carried over.
        const float base = t.offset * destLength;
        FireAt(current_, base);
        if (overshoot > 0.0f) Advance(current_, base, base + overshoot, true);
        return;
    }
    inTransition_ = true;
    next_ = t.dest;
    nextTime_ = start;
    transitionDuration_ = t.fixedDuration ? t.duration : t.duration * Length(current_);
    // A crossfade taken at an exit time starts as far in as the frame went past it (the gloves'
    // punch into Idle, 0.1 s: walk_attack.jsonl's transition times are the overshoot over the
    // duration, the frame's after, so the sprite turns at half way a frame sooner); one a
    // condition starts, from the frame after.
    transitionProgress_ = elapsed > 0.0f ? elapsed / transitionDuration_ : 0.0f;
    if (onStateEnter) onStateEnter(c_.states[static_cast<size_t>(next_)].hash);
    // A frame that goes past the whole crossfade ends it (a hitch: walk_attack.jsonl's frame
    // 474, 0.389 s, is in Idle at the punch's overshoot).
    if (transitionProgress_ >= 1.0f) {
        if (onStateExit) onStateExit(c_.states[static_cast<size_t>(current_)].hash);
        current_ = next_;
        time_ = nextTime_;
        next_ = -1;
        inTransition_ = false;
        transitionProgress_ = 0.0f;
    }
}

void Animator::Play(const std::string& state, float normalizedTime)
{
    int s = c_.State(state);
    if (s < 0) return;
    if (onStateExit) onStateExit(c_.states[static_cast<size_t>(current_)].hash);
    if (inTransition_ && onStateExit) onStateExit(c_.states[static_cast<size_t>(next_)].hash);
    inTransition_ = false;
    current_ = s;
    time_ = normalizedTime;
    if (onStateEnter) onStateEnter(c_.states[static_cast<size_t>(current_)].hash);
}

void Animator::Update(float deltaTime)
{
    Step(deltaTime);
    Evaluate();
}

void Animator::Step(float deltaTime)
{
    deltaTime = deltaTime * speed;
    if (!entered_) {
        entered_ = true;
        if (onStateEnter) onStateEnter(c_.states[static_cast<size_t>(current_)].hash);
    }
    const AnimatorState& s = c_.states[static_cast<size_t>(current_)];
    const float len = Length(current_);
    float prev = time_;
    time_ += deltaTime * s.speed / len;
    if (inTransition_) {
        Advance(current_, prev * len, time_ * len, true);
        const AnimatorState& n = c_.states[static_cast<size_t>(next_)];
        const float nlen = Length(next_);
        float nprev = nextTime_;
        nextTime_ += deltaTime * n.speed / nlen;
        Advance(next_, nprev * nlen, nextTime_ * nlen, true);
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
    const float prevNorm = prev, norm = time_;
    const AnimatorTransition* t = Find(c_.anyState, -1, prevNorm, norm);
    if (!t) t = Find(s.transitions, current_, prevNorm, norm);
    if (t && t->hasExitTime) {
        // The state plays up to its exit time (an event right at it doesn't fire: the clip
        // looping back to its start as the roll ends doesn't start it again), and the rest of
        // the frame carries into the next state, at that state's speed.
        float crossing = t->exitTime < 1.0f ? std::floor(norm - t->exitTime) + t->exitTime : t->exitTime;
        Advance(current_, prev * len, crossing * len, false);
        const float rest = (norm - crossing) * len / s.speed; // seconds (times the Animator's speed)
        Take(*t, rest * c_.states[static_cast<size_t>(t->dest)].speed, rest);
        return;
    }
    Advance(current_, prev * len, time_ * len, true);
    if (t) Take(*t, 0.0f);
}

} // namespace unity
