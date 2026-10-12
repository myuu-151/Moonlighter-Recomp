// unity::Animator's curves on the Broom Spear's controller (animators/broom_spear.json, the
// extractor's exact format) and its prefab (prefabs.json's "Broom Spear"):
//  - the curves: a streamed cubic is ((c0 d + c1) d + c2) d + c3 from its key (each key's
//    cubic reaches the next key's value), -FLT_MAX keys hold, constants;
//  - MainAttackDown with ComboCounter 0 -> FirstAttackDown: MainAttack/Collider active for
//    clip time [0, 0.25), offset (0, -24), size (30, 72); at exit time 0.9 -> AttackIdle,
//    which writes defaults: the prefab's (inactive, offset (5.54, -20), size (9.77, 30)), and
//    the default sprite (None) on MainAttack/Art;
//  - the combo: ComboCounter 1 -> SecondAttack*, 2 -> ThirdAttack* (Equals conditions), from
//    the idle and from inside the previous attack; ThirdAttackDown's sprite is None from 0.6667;
//  - a state without write defaults leaves the properties alone;
//  - a crossfade (a transition given a duration here; the Broom Spear's have none): floats
//    blend, the sprite and m_IsActive switch half way (unverified against Unity);
//  - triggers, bools, ints and floats a controller doesn't have are no-ops (BabySlime's code
//    sets "NotDamagedWhenHit", which its controller lacks); the older format (the hero's,
//    BabySlime's) loads with no animated properties.
//
// Usage: animator_curves_test   (ML_DATA, else data/extracted: animators/, prefabs.json)

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "game/Prefabs.h"
#include "unity/Animator.h"
#include "util/Json.h"

namespace {

int checks = 0, failures = 0;

void Check(bool ok, const std::string& what)
{
    checks++;
    if (!ok) {
        failures++;
        std::printf("FAIL %s\n", what.c_str());
    }
}

std::string ReadFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string Num(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

constexpr float kDt = 1.0f / 60.0f;
const std::string kCollider = "MainAttack/Collider", kArt = "MainAttack/Art";

// MainAttack/Collider's prefab values: the defaults the Animator writes.
struct ColliderDefaults {
    float active = 0.0f, ox = 0.0f, oy = 0.0f, sx = 0.0f, sy = 0.0f;
};

void SetDefaults(unity::Animator& a, const ColliderDefaults& d)
{
    a.SetDefault(kCollider, "m_IsActive", d.active);
    a.SetDefault(kCollider, "m_Offset.x", d.ox);
    a.SetDefault(kCollider, "m_Offset.y", d.oy);
    a.SetDefault(kCollider, "m_Size.x", d.sx);
    a.SetDefault(kCollider, "m_Size.y", d.sy);
}

// The collider as the Animator left it this frame: every property driven, these values.
bool ColliderIs(const unity::Animator& a, float active, float ox, float oy, float sx, float sy)
{
    for (const char* attr : {"m_IsActive", "m_Offset.x", "m_Offset.y", "m_Size.x", "m_Size.y"})
        if (!a.Driven(kCollider, attr)) return false;
    return a.Property(kCollider, "m_IsActive") == active && a.Property(kCollider, "m_Offset.x") == ox &&
           a.Property(kCollider, "m_Offset.y") == oy && a.Property(kCollider, "m_Size.x") == sx &&
           a.Property(kCollider, "m_Size.y") == sy;
}

std::string ColliderStr(const unity::Animator& a)
{
    return "active " + Num(a.Property(kCollider, "m_IsActive")) + " offset (" + Num(a.Property(kCollider, "m_Offset.x")) + ", " +
           Num(a.Property(kCollider, "m_Offset.y")) + ") size (" + Num(a.Property(kCollider, "m_Size.x")) + ", " +
           Num(a.Property(kCollider, "m_Size.y")) + ")";
}

bool IsState(const unity::Animator& a, const std::string& name)
{
    return a.GetCurrentAnimatorStateInfo().IsName(name);
}

std::string StateName(const unity::Animator& a)
{
    return a.Controller().states[static_cast<size_t>(a.CurrentState())].name;
}

void CurveChecks(const unity::AnimatorController& c)
{
    // BroomSpearChargeAttackRight's SecondaryAttack/Collider x: cubic segments through 7, 31, 37, 36.
    const unity::AnimationClip* clip = nullptr;
    for (const auto& k : c.clips)
        if (k.name == "BroomSpearChargeAttackRight") clip = &k;
    Check(clip != nullptr, "BroomSpearChargeAttackRight is a clip");
    if (!clip) return;
    const int x = c.Property("SecondaryAttack/Collider", "m_LocalPosition.x");
    const int rz = c.Property("SecondaryAttack/Collider", "localEulerAnglesRaw.z");
    Check(x >= 0 && rz >= 0, "a Transform's curves are properties per component");
    for (const unity::AnimationCurve& curve : clip->curves) {
        if (curve.property == x) {
            Check(curve.source == unity::AnimationCurve::Streamed && curve.keys.front().time == -FLT_MAX, "x is streamed from -FLT_MAX");
            Check(curve.Evaluate(-1.0f) == 7.0f && curve.Evaluate(0.0f) == 7.0f, "before and at its first key: 7");
            const float k1 = 0.0833333358168602f, k2 = 0.1666666716337204f, k3 = 0.25f;
            Check(std::fabs(curve.Evaluate(k1) - 31.0f) < 1e-3f, "at 1/12: 31 (" + Num(curve.Evaluate(k1)) + ")");
            Check(std::fabs(curve.Evaluate(k2) - 37.0f) < 1e-3f, "at 2/12: 37 (" + Num(curve.Evaluate(k2)) + ")");
            Check(std::fabs(curve.Evaluate(k3) - 36.0f) < 1e-3f, "at 3/12: 36 (" + Num(curve.Evaluate(k3)) + ")");
            // The segment from 0 ends up at the next key's value (d measured from the key).
            const float d = k1 - 0.0f;
            const auto& c0 = curve.keys[1].c;
            const float end = ((c0[0] * d + c0[1]) * d + c0[2]) * d + c0[3];
            Check(std::fabs(end - curve.keys[2].c[3]) < 1e-3f, "a segment's cubic reaches the next key");
            const float t = 0.04f, e = ((c0[0] * t + c0[1]) * t + c0[2]) * t + c0[3];
            Check(curve.Evaluate(t) == e, "inside a segment: the cubic in t - key time");
            Check(curve.Evaluate(10.0f) == 19.0f, "past the last key: its value");
        }
        if (curve.property == rz)
            Check(curve.source == unity::AnimationCurve::Constant && curve.Evaluate(0.3f) == 90.0f, "the rotation is a constant 90");
    }
}

void AttackChecks(const unity::AnimatorController& c, const ColliderDefaults& d)
{
    unity::Animator a(c);
    SetDefaults(a, d);
    Check(a.Property(kCollider, "m_Size.x") == d.sx && !a.Driven(kCollider, "m_Size.x"), "before the first Update: the default, undriven");
    Check(a.Property("Nowhere", "m_Size.x", -5.0f) == -5.0f && !a.Driven("Nowhere", "m_Size.x"), "a property no clip animates: the fallback");

    a.Update(kDt);
    Check(IsState(a, "AttackIdle"), "starts in AttackIdle");
    Check(ColliderIs(a, d.active, d.ox, d.oy, d.sx, d.sy), "AttackIdle writes the collider's defaults: " + ColliderStr(a));
    Check(a.Sprite(kArt) && a.Sprite(kArt)->empty(), "AttackIdle writes MainAttack/Art's default sprite (None)");
    {
        unity::Animator copy = a;
        copy.SetDefaultSprite(kArt, "Copy");
        Check(copy.Sprite(kArt) && *copy.Sprite(kArt) == "Copy" && a.Sprite(kArt)->empty(), "a copy's default sprite is its own");
    }

    a.SetInteger("ComboCounter", 0);
    a.SetTrigger("MainAttackDown");
    a.Update(kDt);
    Check(IsState(a, "FirstAttackDown"), "MainAttackDown, ComboCounter 0 -> FirstAttackDown (" + StateName(a) + ")");
    Check(!a.GetBool("MainAttackDown"), "the trigger is consumed");
    Check(a.Sprite(kArt) && *a.Sprite(kArt) == "BroomSpearCombo_Main_Down_1", "its first sprite on MainAttack/Art");
    Check(a.Sprite("SecondaryAttack/Art") && a.Sprite("SecondaryAttack/Art")->empty(), "the other renderer gets its default");

    int activeFrames = 0, frames = 0;
    bool sawExit = false;
    for (int f = 0; f < 120; f++) {
        if (!IsState(a, "FirstAttackDown")) {
            sawExit = true;
            break;
        }
        const float t = a.CurrentTime();
        const bool active = t < 0.25f;
        activeFrames += active;
        frames++;
        Check(ColliderIs(a, active ? 1.0f : 0.0f, 0.0f, -24.0f, 30.0f, 72.0f),
              "FirstAttackDown at " + Num(t) + ": " + ColliderStr(a));
        a.Update(kDt);
    }
    Check(sawExit && IsState(a, "AttackIdle"), "exit time 0.9 -> AttackIdle (" + StateName(a) + ")");
    Check(activeFrames == 15, "active for 15 frames of 1/60 (" + std::to_string(activeFrames) + ")");
    const float exitAt = 0.9f * 0.5833333134651184f;
    Check(frames == static_cast<int>(std::ceil(exitAt / kDt)), "in FirstAttackDown for " + std::to_string(frames) + " frames");
    Check(ColliderIs(a, d.active, d.ox, d.oy, d.sx, d.sy), "back in AttackIdle: the defaults again: " + ColliderStr(a));
    Check(a.Sprite(kArt) && a.Sprite(kArt)->empty(), "and the default sprite");

    // The combo from the idle: ComboCounter picks the any-state transition (Equals).
    const char* dirs[] = {"Down", "Left", "Right", "Up"};
    for (const char* dir : dirs) {
        for (int combo = 1; combo <= 2; combo++) {
            a.Play("AttackIdle");
            a.Update(kDt);
            a.SetInteger("ComboCounter", combo);
            a.SetTrigger(std::string("MainAttack") + dir);
            a.Update(kDt);
            const std::string want = std::string(combo == 1 ? "SecondAttack" : "ThirdAttack") + dir;
            Check(IsState(a, want), "ComboCounter " + std::to_string(combo) + " + MainAttack" + dir + " -> " + want + " (" + StateName(a) + ")");
        }
    }

    // A chain: First -> Second -> Third while each plays.
    a.Play("AttackIdle");
    a.Update(kDt);
    a.SetInteger("ComboCounter", 0);
    a.SetTrigger("MainAttackDown");
    a.Update(kDt);
    for (int f = 0; f < 10; f++) a.Update(kDt);
    a.SetInteger("ComboCounter", 1);
    a.SetTrigger("MainAttackDown");
    a.Update(kDt);
    Check(IsState(a, "SecondAttackDown"), "inside FirstAttackDown, ComboCounter 1 -> SecondAttackDown (" + StateName(a) + ")");
    Check(ColliderIs(a, 1.0f, a.Property(kCollider, "m_Offset.x"), a.Property(kCollider, "m_Offset.y"), a.Property(kCollider, "m_Size.x"),
                     a.Property(kCollider, "m_Size.y")),
          "SecondAttackDown's collider active at its start");
    for (int f = 0; f < 10; f++) a.Update(kDt);
    a.SetInteger("ComboCounter", 2);
    a.SetTrigger("MainAttackDown");
    a.Update(kDt);
    Check(IsState(a, "ThirdAttackDown"), "inside SecondAttackDown, ComboCounter 2 -> ThirdAttackDown (" + StateName(a) + ")");

    // ThirdAttackDown's sprite: _22 from 0.5833, None from 0.6667 (its exit, 0.675, comes after).
    const unity::AnimationClip& third = c.clips[static_cast<size_t>(c.states[static_cast<size_t>(c.State("ThirdAttackDown"))].clip)];
    Check(*third.SpriteAt(0.62f) == "BroomSpearCombo_Main_Down_22", "Third: _22 at 0.62");
    Check(third.SpriteAt(0.6666666269302368f)->empty() && third.SpriteAt(0.67f)->empty(), "Third: None from 0.6667");
    a.Play("ThirdAttackDown");
    a.Update(0.0f);
    a.Update(0.62f);
    Check(IsState(a, "ThirdAttackDown") && a.Sprite(kArt) && *a.Sprite(kArt) == "BroomSpearCombo_Main_Down_22",
          "the Animator shows _22 at 0.62");
    a.Update(0.05f);
    Check(IsState(a, "ThirdAttackDown") && a.Sprite(kArt) && a.Sprite(kArt)->empty(),
          "and None at 0.67 (" + StateName(a) + " " + Num(a.CurrentTime()) + ")");
    a.Update(0.01f);
    Check(IsState(a, "AttackIdle"), "then AttackIdle past 0.675");
}

void NoWriteDefaultsChecks(const unity::AnimatorController& base, const ColliderDefaults& d)
{
    unity::AnimatorController c = base;
    c.states[static_cast<size_t>(c.State("AttackIdle"))].writeDefaults = false;
    unity::Animator a(c);
    SetDefaults(a, d);
    a.Update(kDt);
    Check(!a.Driven(kCollider, "m_IsActive") && !a.Sprite(kArt), "AttackIdle without write defaults drives nothing");
    a.SetTrigger("MainAttackDown");
    a.Update(kDt);
    Check(ColliderIs(a, 1.0f, 0.0f, -24.0f, 30.0f, 72.0f), "FirstAttackDown drives the collider");
    for (int f = 0; f < 60 && !IsState(a, "AttackIdle"); f++) a.Update(kDt);
    Check(IsState(a, "AttackIdle") && !a.Driven(kCollider, "m_Size.x"), "back in AttackIdle: undriven");
    Check(a.Property(kCollider, "m_Size.x") == 30.0f, "the property keeps the last value written");
}

void CrossfadeChecks(const unity::AnimatorController& base, const ColliderDefaults& d)
{
    // FirstAttackDown -> AttackIdle over 0.1 s (fixed) instead of at once.
    unity::AnimatorController c = base;
    for (auto& t : c.states[static_cast<size_t>(c.State("FirstAttackDown"))].transitions)
        if (t.hasExitTime) {
            t.duration = 0.1f;
            t.fixedDuration = true;
        }
    unity::Animator a(c);
    SetDefaults(a, d);
    a.Update(kDt);
    a.SetTrigger("MainAttackDown");
    a.Update(kDt);
    for (int f = 0; f < 60 && !a.IsInTransition(); f++) a.Update(kDt);
    Check(a.IsInTransition(), "the exit crossfades");
    bool blended = true, switched = true;
    for (int f = 0; f < 10 && a.IsInTransition(); f++) {
        a.Update(kDt);
        if (!a.IsInTransition()) break;
        const float w = a.TransitionNormalizedTime();
        const float sx = 30.0f + (d.sx - 30.0f) * w;
        blended = blended && a.Property(kCollider, "m_Size.x") == sx;
        const std::string* sprite = a.Sprite(kArt);
        switched = switched && sprite && (w >= 0.5f ? sprite->empty() : !sprite->empty());
    }
    Check(blended, "m_Size.x blends from 30 to the default by the transition's progress");
    Check(switched, "the sprite switches to the default half way");
    Check(!a.IsInTransition() && ColliderIs(a, d.active, d.ox, d.oy, d.sx, d.sy), "after it: the defaults");
}

void MissingParameterChecks(const unity::AnimatorController& c)
{
    unity::Animator a(c);
    a.SetTrigger("NotDamagedWhenHit");
    a.ResetTrigger("NotDamagedWhenHit");
    a.SetBool("NoSuchBool", true);
    a.SetFloat("NoSuchFloat", 1.0f);
    a.SetInteger("NoSuchInt", 3);
    a.Update(kDt);
    Check(!a.GetBool("NotDamagedWhenHit") && a.GetInteger("NoSuchInt") == 0, c.name + ": a missing parameter is a no-op");
}

} // namespace

int main()
{
    const char* env = std::getenv("ML_DATA");
    const std::string data = env ? env : "data/extracted";
    const std::string spear = ReadFile(data + "/animators/broom_spear.json");
    const std::string prefabs = ReadFile(data + "/prefabs.json");
    if (spear.empty() || prefabs.empty()) {
        std::printf("no %s/animators/broom_spear.json or prefabs.json\n", data.c_str());
        return 1;
    }
    const unity::AnimatorController c = unity::AnimatorController::FromJson(json::Parse(spear));
    Check(c.updateMode == 2, "updateMode: unscaled time");
    Check(c.states[0].writeDefaults && c.clips[2].spritePath == kArt, "writeDefaults and spritePath read");

    game::PrefabLibrary::Load(json::Parse(prefabs));
    const game::Prefab* prefab = game::PrefabLibrary::Find("Broom Spear", "items");
    Check(prefab != nullptr, "the Broom Spear prefab");
    ColliderDefaults d;
    if (prefab)
        for (const game::PrefabNode& n : prefab->nodes)
            if (n.path == kCollider && !n.colliders.empty()) {
                d.active = n.active ? 1.0f : 0.0f;
                d.ox = n.colliders[0].ox;
                d.oy = n.colliders[0].oy;
                d.sx = n.colliders[0].sx;
                d.sy = n.colliders[0].sy;
            }
    Check(d.active == 0.0f && std::fabs(d.ox - 5.54f) < 0.01f && d.oy == -20.0f && std::fabs(d.sx - 9.77f) < 0.01f && d.sy == 30.0f,
          "the prefab's collider: inactive, offset (5.54, -20), size (9.77, 30)");

    CurveChecks(c);
    AttackChecks(c, d);
    NoWriteDefaultsChecks(c, d);
    CrossfadeChecks(c, d);
    MissingParameterChecks(c);

    for (const char* name : {"BabySlime", "hero_merchant"}) {
        const std::string text = ReadFile(data + "/animators/" + name + ".json");
        if (text.empty()) {
            std::printf("no %s.json: skipped\n", name);
            continue;
        }
        const unity::AnimatorController old = unity::AnimatorController::FromJson(json::Parse(text));
        Check(old.properties.empty() && old.updateMode == -1, std::string(name) + ": the older format animates no properties");
        MissingParameterChecks(old);
    }

    std::printf("\n%d checks, %d failed\n%s\n", checks, failures, failures ? "FAILURES" : "ALL PASS");
    return failures ? 1 : 0;
}
