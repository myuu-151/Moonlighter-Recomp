// The Golem enemies' recordings (data/oracle/slime.jsonl, slime_bis.jsonl, enemy_<prefab>.jsonl)
// replayed against the game's own code translated (tools/translate: BabySlimeBehaviour,
// GolemSoldierBehaviour, HermitSlimeBehaviourNEW, GolemKamikazeBehaviour,
// FlyingKamikazeGolemBehaviour, BrokenGolemTurretBehaviour and its shots (BrokenGolemTurretProjectile,
// PoolManager), EnemyBehaviour, Enemy, EnemyStats, EnemyAttackCollisionDetection,
// EnemyDamageReceiver, PolyNavAgent, PolyNav2D, AStar, the stats' JSON reader...), running on the
// Unity facade (recomp/src/bind) over the hand-written Unity layer.
//
// The replay is the hand port's (tests/enemy_replay.h with enemy_replay_colliders.h, as slime_test
// and enemy_test run it: the same recording, the same rows, the same pass criterion) with the
// translated objects in place of game::Enemy: the room's enemies made from their prefab
// (prefabs.json, animators/<prefab>.json) under the room's objects, Init'ed by the game's code
// (EnemyStats.Init from enemy_stats.json through EnemyStatManager), their recorded state put back
// into the translated fields, then every frame as the game ran it: the fixed steps (the hero's
// body as each found it, the physics with its trigger and collision messages, the weapon's hits
// and pushes where they landed: Enemy.DealDamageToEnemy, or a hurtbox's
// EnemyDamageReceiver.DealDamageToEnemy, Weapon.PushEnemyBackwards), DOTween, the enemies'
// Updates in the game's order (each from the Random state and the hero position it had), the
// delayed calls (Invoke, WaitForSeconds, Destroy(obj, t)), the coroutines on yield null (the
// paths), the Animators, PolyNav2D's and the agents' LateUpdates, MasterAudio's. Compared bit for
// bit per frame and enemy as the hand port compares them. The scene's other services are the
// stand-ins of recomp/tests/translated/scene/Standins.cpp. The probes newer recordings carry are
// read by the hand port's own helpers (tests/enable_clock.h, fixture_probe.h, polygon_probe.h,
// enemy_replay_critters.h); the room's DisableEnemies (the hero leaving it, placed as
// enemy_replay_room.h places it) runs the game's own DungeonRoom.DisableEnemies.
//
// What one enemy brings beyond the common replay is a Hooks of its own, as the hand port's
// ReplayHooks (tests/translated/turret_translated.h, kamikaze_translated.h,
// flykami_translated.h); the HermitSlime's rows (its patrol, ItemDrain) are the common replay's,
// as enemy_replay_colliders.h has them. Where a hook needs to see a translated method run (the
// turret's look, the kamikaze's MoveEnemy), the prefab's script is a probe: the translated class
// with that virtual method wrapped (Hooks::AddScript), the game's code unchanged.
//
// Usage: enemy_translated_test [recording.jsonl] [prefab]   (default data/oracle/slime.jsonl
//        BabySlime; ML_DATA: the extracted data, default data/extracted; ML_SLIME_DEBUG=1: every
//        mismatch; ML_GC_EVERY_FRAME=1: the collector at every frame boundary; ML_REPLAY_PROFILE=1:
//        the time per frame of each phase of the game's work, after the rows: tests/replay_profile.h)
//
// MLGC's test builds (TTEST=1) run it on the GameCube, its main() renamed (ML_TEST_MAIN).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <Box2D/Box2D.h>

#include "bind/Prefabs.h"
#include "bind/UnityEngine.h"
#include "game.h"
#include "game/HeroDamage.h"
#include "game/MasterAudio.h"
#include "game/Prefabs.h"
#include "game/RoomPhysics.h"
#include "scene/Standins.h"
#include "unity/Animator.h"
#include "unity/Physics2D.h"
#include "unity/PhysicsWorld.h"
#include "unity/PolygonCollider2D.h"
#include "unity/Random.h"
#include "util/Json.h"

#include "../enable_clock.h"
#include "../fixture_probe.h"
#include "../polygon_probe.h"
#include "../replay_frames.h"
#include "../replay_profile.h"
#include "hero_replay.h"

namespace {

using json::Value;
using unity::Vector2;
using UnityEngine::GameObject;
using UnityEngine::Transform;

#include "replay_common.h" // (the values, the tally, the room's colliders)

#include "../enemy_replay_critters.h" // (the room's critters: CritterReplay; game-independent)

// ---- the translated enemies ------------------------------------------------------------------------------

struct Replayed {
    int id = 0;
    GameObject* go = nullptr;
    gen::Enemy* enemy = nullptr;
    gen::EnemyBehaviour* behaviour = nullptr;
    gen::PolyNavAgent* agent = nullptr;
    UnityEngine::Rigidbody2D* rb = nullptr;
    UnityEngine::BoxCollider2D* box = nullptr; // the root's (the body's own)
    UnityEngine::Animator* animator = nullptr;
    GameObject* attack = nullptr; // its AttackCollider (none: the turret's)
    UnityEngine::BoxCollider2D* attackBox = nullptr;
    UnityEngine::Rigidbody2D* attackRb = nullptr; // the AttackCollider's own (the KamikazeGolem's)
    bool Gone() const { return !go || go->destroyed_; }
};

Vector2 Position(const Replayed& s) { return XY(s.go->transform_->get_position()); }
Vector2 Local(const Replayed& s) { return XY(s.go->transform_->localPosition_); }
float Health(const Replayed& s) { return s.enemy->enemyStats->_currentHealth; }
std::string SpriteOf(const Replayed& s) { return s.enemy->enemySprite ? s.enemy->enemySprite->SpriteName() : std::string(); }
UnityEngine::Material* MaterialOf(const Replayed& s) { return s.enemy->enemySprite->get_material(); }

// A behaviour whose AttackHit override never calls EnemyBehaviour.AttackHit (enemy_replay.h's
// AttackHitSkipsBase, by the class the game's prefab names).
bool AttackHitSkipsBase(gen::EnemyBehaviour* b)
{
    const std::string n = b->cs_typename();
    return n == "HermitSlimeBehaviourNEW" || n == "BrokenGolemTurretBehaviour" || n == "GolemKamikazeBehaviour";
}

bool SameAttack(const Replayed& s, bool on, Vector2 size, Vector2 offset, Vector2 position)
{
    return s.attackBox && s.attackBox->enabled_ == on && Same(s.attackBox->size_, size) && Same(s.attackBox->offset_, offset) &&
           Same(XY(s.attack->transform_->localPosition_), position);
}
std::string AttackStr(const Replayed& s)
{
    if (!s.attackBox) return "none";
    return std::string(s.attackBox->enabled_ ? "on" : "off") + " size " + Str(s.attackBox->size_) + " offset " + Str(s.attackBox->offset_) + " at " +
           Str(XY(s.attack->transform_->localPosition_));
}

// ---- the colliders' rows (tests/enemy_replay_colliders.h's: the root box, the body's centre of mass,
// the hurtboxes, the damage types, the HermitSlime's patrol and ItemDrain) ---------------------------------

// The hurtboxes as the oracle lists them: GetComponentsInChildren<EnemyDamageReceiver>(true) of
// the behaviour, each by its object's name ("" the enemy's own).
std::vector<gen::EnemyDamageReceiver*> Receivers(const Replayed& s)
{
    std::vector<gen::EnemyDamageReceiver*> out;
    if (cs::Array<gen::EnemyDamageReceiver*>* a = s.behaviour->GetComponentsInChildren<gen::EnemyDamageReceiver*>(true))
        for (int32_t i = 0; i < a->get_Length(); i++) out.push_back(a->at(i));
    return out;
}
std::string ReceiverPath(const Replayed& s, gen::EnemyDamageReceiver* r) { return r->gameObject_ == s.go ? std::string() : Utf8(r->gameObject_->get_name()); }
gen::EnemyDamageReceiver* Receiver(const Replayed& s, const std::string& path)
{
    for (gen::EnemyDamageReceiver* r : Receivers(s))
        if (ReceiverPath(s, r) == path) return r;
    return nullptr;
}
bool SameReceiver(gen::EnemyDamageReceiver* r, const Value& g)
{
    GameObject* go = r->gameObject_;
    auto* rbox = go->GetComponent<UnityEngine::BoxCollider2D*>();
    bool ok = go->activeSelf_ == g["active"].b && go->layer_ == g["layer"].AsInt() && Utf8(go->get_tag()) == g["tag"].s &&
              Same(XY(go->transform_->localPosition_), V2(g["pos"])) && (r->gameObjectToAdvise != nullptr) == g["advise"].b;
    if (g.Find("on"))
        ok = ok && rbox && rbox->enabled_ == g["on"].b && rbox->isTrigger_ == g["trigger"].b && Same(rbox->offset_, V2(g["offset"])) &&
             Same(rbox->size_, V2(g["size"]));
    return ok;
}
void ReceiverRows(Tally& t, const Replayed& s, const Value& list, const std::string& what, const std::string& at)
{
    const std::vector<gen::EnemyDamageReceiver*> mine = Receivers(s);
    bool ok = list.size() == mine.size();
    for (const Value& g : list.arr) {
        gen::EnemyDamageReceiver* r = Receiver(s, g["path"].s);
        ok = ok && r && SameReceiver(r, g);
    }
    t.Add(what, ok, at + ": port " + std::to_string(mine.size()) + " game " + list.Dump());
}

gen::HermitSlimeBehaviourNEW* Hermit(const Replayed& s) { return dynamic_cast<gen::HermitSlimeBehaviourNEW*>(s.behaviour); }

// The room's DisableEnemies where the game ran it (enemy_replay_room.h's RoomExitReplay, on the
// translated enemies): the hero leaving the room is his doing, fed from the recording as his hits
// are; the game's own DungeonRoom.DisableEnemies does the rest.
struct RoomExit {
    bool explicitEvents = false;
    int inferred = 0;
    int step = -2; // -2 none; else the fixed step (-1: the frame's part)
    size_t updatesBefore = 0;

    void Scan(ReplayFrames& frames)
    {
        for (const Value& f : frames)
            for (const Value& ev : f["events"].arr)
                if (ev["k"].s == "disableEnemies") explicitEvents = true;
    }
    static const Value* RowOf(const Value* frame, int id)
    {
        if (!frame) return nullptr;
        const Value* list = frame->Find("enemies");
        if (!list) list = frame->Find("slimes");
        if (!list) return nullptr;
        for (const Value& v : list->arr)
            if (v["s"].AsInt() == id) return &v;
        return nullptr;
    }
    static bool WentInactive(const Value* row, const Value* before)
    {
        if (!row || !before || (*row)["gone"].b) return false;
        const Value& an = (*row)["anim"];
        const Value& was = (*before)["anim"];
        return an["on"].b && an["state"].AsInt() == 0 && !an.Find("dx") && was.Find("dx") != nullptr;
    }
    void Plan(const Value& f, const Value* previous, const std::vector<Replayed>& enemies, gen::DungeonRoom* room)
    {
        step = -2;
        updatesBefore = 0;
        if (explicitEvents) {
            for (const Value& ev : f["events"].arr)
                if (ev["k"].s == "disableEnemies") {
                    step = static_cast<int>(ev["step"].AsInt());
                    updatesBefore = static_cast<size_t>(ev["updates"].AsInt());
                }
            return;
        }
        int out = 0, gone = 0;
        for (const Replayed& e : enemies) {
            if (e.Gone() || !e.go->get_activeInHierarchy() || !room->roomEnemies->Contains(e.enemy)) continue;
            out++;
            gone += WentInactive(RowOf(&f, e.id), RowOf(previous, e.id));
        }
        if (out == 0 || gone != out) return;
        step = -1;
        updatesBefore = f["updates"].size();
        inferred++;
    }
};

// Animator.GetCurrentAnimatorStateInfo(0): zeros while the object is inactive.
unity::AnimatorStateInfo InfoOf(const Replayed& s)
{
    if (!s.go->get_activeInHierarchy()) return unity::AnimatorStateInfo{};
    return s.animator->Mecanim()->GetCurrentAnimatorStateInfo();
}

struct Data {
    std::string dir, name;
    Value enemyStats, physics2d, soundGroups;
    std::unique_ptr<unity::AnimatorController> controller;
    const game::Prefab* prefab = nullptr;
};

// ---- what one enemy adds (tests/enemy_replay.h's ReplayHooks) ---------------------------------------------

struct Ctx {
    bind::Scene* scene = nullptr;
    bind::PhysicsScene* physics = nullptr;
    unity::PhysicsWorld* world = nullptr;
    unity::Physics2D* roomColliders = nullptr;
    unity::Rigidbody2D* heroBody = nullptr;
    UnityEngine::BoxCollider2D* heroCollider = nullptr;
    game::MasterAudio* audio = nullptr;
    gen::DungeonRoom* room = nullptr;
    gen::HeroMerchant* hero = nullptr;
    const Data* data = nullptr;
    std::vector<Replayed>* enemies = nullptr;
    std::function<void(Vector2)> setHeroPosition;
    Replayed* ById(int id) const
    {
        for (Replayed& s : *enemies)
            if (s.id == id) return &s;
        return nullptr;
    }
    int IdOf(const UnityEngine::Object* o) const
    {
        for (const Replayed& s : *enemies)
            if (s.enemy == o || s.behaviour == o || s.go == o) return s.id;
        return -1;
    }
};

struct Hooks {
    virtual ~Hooks() = default;
    // The script to add for a prefab's class (a probe of the translated class), or nullptr.
    virtual UnityEngine::Component* AddScript(GameObject* go, const char* cls)
    {
        (void)go, (void)cls;
        return nullptr;
    }
    // The scene is made, before the enemies.
    virtual void OnScene(const Value& start, Ctx& c, Tally& t) { (void)start, (void)c, (void)t; }
    // An enemy made and set as its recorded line has it; then all of them.
    virtual void OnEnemy(Replayed& e, const Value& line, Tally& t) { (void)e, (void)line, (void)t; }
    virtual void OnEnemiesBuilt(const Value& start, Tally& t) { (void)start, (void)t; }
    virtual void BeginFrame(long long frame, const Value& f) { (void)frame, (void)f; }
    virtual void BeforeStep(long long frame, size_t k, const Value& f, Tally& t) { (void)frame, (void)k, (void)f, (void)t; }
    virtual void AfterStep(long long frame, size_t k, const Value& f, Tally& t) { (void)frame, (void)k, (void)f, (void)t; }
    // Each clip event of an enemy's Animator, before (after the common replay's resync) and after
    // the scripts got it (before the common replay compares its draws).
    virtual void OnAnimEvent(Replayed& e, const unity::AnimationEvent& ev, bool after) { (void)e, (void)ev, (void)after; }
    // The room's DisableEnemies, before and after (the frame's part or a step: `step`).
    virtual void AroundRoomExit(bool after, int step) { (void)after, (void)step; }
    virtual void AtFrameEnd(long long frame, const Value& f, Tally& t) { (void)frame, (void)f, (void)t; }
    virtual void AfterFrames(const std::vector<Value>& otherLines, Tally& t) { (void)otherLines, (void)t; }
    virtual void Summary() const {}
};

Hooks* gHooks = nullptr;

// The prefabs' scripts: a hook's probe, else the translation's.
UnityEngine::Component* AddScript(GameObject* go, const char* cls)
{
    if (gHooks)
        if (UnityEngine::Component* c = gHooks->AddScript(go, cls)) return c;
    return gen::cs_AddComponent(go, cls);
}

} // namespace

#include "turret_translated.h"   // (the BrokenGolemTurret's shots, looks and pool)
#include "kamikaze_translated.h" // (the KamikazeGolem's MoveEnemy, jump and explosion)
#include "flykami_translated.h"  // (the FlyingKamikazeGolem's charge)

namespace {

bool LoadData(Data& d)
{
    d.dir = std::getenv("ML_DATA") ? std::getenv("ML_DATA") : "data/extracted";
    const std::string prefabs = ReadFile(d.dir + "/prefabs.json"), anim = ReadFile(d.dir + "/animators/" + d.name + ".json"),
                      physics = ReadFile(d.dir + "/physics2d.json"), sounds = ReadFile(d.dir + "/sound_groups.json");
    if (prefabs.empty() || anim.empty() || ReadFile(d.dir + "/constants.json").empty() || ReadFile(d.dir + "/enemy_stats.json").empty()) return false;
    game::PrefabLibrary::Load(json::Parse(prefabs));
    d.prefab = game::PrefabLibrary::Find(d.name, "enemies_golem");
    d.controller = std::make_unique<unity::AnimatorController>(unity::AnimatorController::FromJson(json::Parse(anim)));
    if (!physics.empty()) d.physics2d = json::Parse(physics);
    if (!sounds.empty()) d.soundGroups = json::Parse(sounds);
    // The game's own readers: Constants (FullSerializer) and EnemyStatManager (enemy_stats.json),
    // their objects outliving the replay's heap as they outlive the game's scenes.
    UnityEngine::Resources::SetTextRoot(d.dir);
    cs::StaticScope keep;
    gen::Constants::ReadFile(S("constants"));
    gen::EnemyStatManager::get_Instance()->ReadDefaultFile();
    return d.prefab != nullptr;
}

int Replay(const std::string& file, Data& data)
{
    std::ifstream in(file);
    if (!in) {
        std::printf("(no %s: skipping the oracle comparison; record it with -oracle-slime / -oracle-enemy=%s)\n", file.c_str(), data.name.c_str());
        return 0;
    }
    Value start;
    std::vector<Value> enemyLines, maps, probes, colliders, otherLines;
    ReplayFrames frames; // (parsed as they're read: tests/replay_frames.h)
    for (std::string line; std::getline(in, line);) {
        if (line.empty()) continue;
        Value v = json::Parse(line);
        const std::string& ev = v["ev"].s;
        if (ev == "slimeStart" || ev == "enemyStart") start = v;
        else if (ev == "slime" || ev == "enemy") enemyLines.push_back(std::move(v));
        else if (ev == "frame") frames.push_back(std::move(line));
        else if (ev == "navMap") maps.push_back(std::move(v));
        else if (ev == "rngProbe") probes.push_back(std::move(v));
        else if (ev == "collider") colliders.push_back(std::move(v));
        else otherLines.push_back(std::move(v)); // (Hooks::AfterFrames)
    }
    in.close(); // (all read; the GameCube's test builds free the file's copy here)
    if (start.IsNull() || enemyLines.empty() || frames.empty() || maps.empty()) {
        std::printf("%s: no start, enemies, frames or map\n", file.c_str());
        return 1;
    }
    if (start["enemy"].s != data.name) {
        std::printf("%s records %s, not %s\n", file.c_str(), start["enemy"].s.c_str(), data.name.c_str());
        return 1;
    }
    TurretHooks turretHooks;
    KamikazeHooks kamikazeHooks;
    FlyKamiHooks flykamiHooks;
    Hooks none;
    Hooks* hooks = data.name == "BrokenGolemTurret" ? static_cast<Hooks*>(&turretHooks)
                   : data.name == "KamikazeGolem"   ? static_cast<Hooks*>(&kamikazeHooks)
                   : data.name == "FlyingKamikazeGolem" ? static_cast<Hooks*>(&flykamiHooks)
                                                        : &none;
    gHooks = hooks;
    Tally t;
    t.debug = std::getenv("ML_SLIME_DEBUG") != nullptr;
    RoomExit roomExit;
    roomExit.Scan(frames);
    std::printf("recording: room %s, %zu %s, %zu frames (translated)\n", start["room"].s.c_str(), enemyLines.size(), start["enemy"].s.c_str(), frames.size());
    // MasterAudio's variationFollowerType where the recording has it: LateUpdate (0), which
    // game/MasterAudio.h's "a variation played in this frame isn't playing until its LateUpdate" takes.
    if (const Value* ma = start.Find("maFollower"))
        t.Add("MasterAudio.variationFollowerType is LateUpdate (game/MasterAudio.h's same-frame plays)", ma->AsInt() == 0,
              "game " + std::to_string(ma->AsInt()));

    for (const Value& p : probes) {
        unity::Random::SetState(State(p["state"]));
        const Vector2 got = unity::Random::insideUnitCircle();
        t.Add("Random.insideUnitCircle (probes)", Same(got, V2(p["out"])) && SameState(unity::Random::GetState(), State(p["after"])),
              Str(got) + " vs " + Str(V2(p["out"])));
    }

    // ---- the scene: its clocks, the physics, the room's colliders, the hero ----
    bind::Scene scene;
    unity::Physics2DSettings settings;
    if (!data.physics2d.IsNull()) settings.Load(data.physics2d);
    unity::PhysicsWorld world(settings);
    unity::Physics2D roomColliders;
    for (const Value& c : colliders) {
        const std::string body = c.Find("body") ? c["body"].s : "none";
        if (body == "Dynamic" || c["trigger"].b || !c["enabled"].b) continue;
        if (c["type"].s == "BoxCollider2D") roomColliders.Add(ColliderFrom(c));
        else if (c["type"].s == "PolygonCollider2D" && c["tag"].s == "Hole") {
            const unity::Collider2D hole = HoleFrom(c);
            CheckPolygonProbe(t, c, hole); // (the oracle's probe of Unity's cut, where recorded)
            roomColliders.Add(hole);
        }
    }
    for (const unity::Collider2D& c : roomColliders.Colliders()) {
        std::string body = "none";
        for (const Value& v : colliders)
            if (v["path"].s == c.path && v.Find("body")) body = v["body"].s;
        world.AddCollider(c, body == "Kinematic" ? unity::RigidbodyType2D::Kinematic : unity::RigidbodyType2D::Static, body != "none");
    }
    // The room's triggers on a Kinematic or Static Rigidbody2D (a chest's "Trigger" child), as the
    // room's colliders.
    unity::Physics2D bodyTriggers;
    const std::string roomPath = "Dungeon/" + start["room"].s + "/";
    for (const Value& c : colliders) {
        const std::string body = c.Find("body") ? c["body"].s : "none";
        if ((body != "Kinematic" && body != "Static") || !c["trigger"].b || !c["enabled"].b || c["type"].s != "BoxCollider2D") continue;
        if (c["path"].s.compare(0, roomPath.size(), roomPath) != 0) continue;
        bodyTriggers.Add(ColliderFrom(c));
    }
    for (const unity::Collider2D& c : bodyTriggers.Colliders()) {
        std::string body = "none";
        for (const Value& v : colliders)
            if (v["path"].s == c.path && v.Find("body")) body = v["body"].s;
        world.AddCollider(c, body == "Kinematic" ? unity::RigidbodyType2D::Kinematic : unity::RigidbodyType2D::Static, true);
    }
    // The room's triggers without a body of their own (the breakables'), after.
    unity::Physics2D roomTriggers;
    for (const Value& c : colliders) {
        const std::string body = c.Find("body") ? c["body"].s : "none";
        if (body != "none" || !c["trigger"].b || !c["enabled"].b || c["type"].s != "BoxCollider2D") continue;
        roomTriggers.Add(ColliderFrom(c));
    }
    for (const unity::Collider2D& c : roomTriggers.Colliders()) world.AddCollider(c);
    // The room's PolygonCollider2D triggers without a body (its slime puddles), cut into convex
    // pieces as Unity cuts them (enemy_replay.h's: the KamikazeGolem's explosion box enters them).
    unity::Physics2D roomPolygonTriggers;
    for (const Value& c : colliders) {
        const std::string body = c.Find("body") ? c["body"].s : "none";
        if (body != "none" || !c["trigger"].b || !c["enabled"].b || c["type"].s != "PolygonCollider2D") continue;
        std::vector<std::vector<Vector2>> paths;
        for (const Value& path : c["paths"].arr) {
            paths.emplace_back();
            for (size_t k = 0; k + 1 < path.arr.size(); k += 2) paths.back().push_back({Bits(path[k]), Bits(path[k + 1])});
        }
        unity::Collider2D polygon = ColliderFrom(c);
        unity::SetPolygonColliderShapes(polygon, paths);
        roomPolygonTriggers.Add(polygon);
    }
    for (const unity::Collider2D& c : roomPolygonTriggers.Colliders()) world.AddCollider(c);
    std::vector<std::string> triggerNames;
    for (const unity::Collider2D& c : roomTriggers.Colliders()) triggerNames.push_back(Leaf(c.path));
    auto RoomTrigger = [&](const std::string& name) { return std::find(triggerNames.begin(), triggerNames.end(), name) != triggerNames.end(); };

    const Value& h = start["hero"];
    unity::Rigidbody2DDef heroDef;
    heroDef.mass = Bits(h["mass"]);
    heroDef.linearDrag = Bits(h["drag"]);
    heroDef.angularDrag = Bits(h["angularDrag"]);
    heroDef.gravityScale = Bits(h["gravityScale"]);
    heroDef.constraints = static_cast<int>(h["constraints"].AsInt());
    heroDef.collisionDetection = h["continuous"].b ? unity::Rigidbody2DDef::Continuous : unity::Rigidbody2DDef::Discrete;
    heroDef.hasCenterOfMass = true;
    heroDef.centerOfMass = V2(h["centerOfMass"]);
    const Value& f0 = frames[0];
    unity::Rigidbody2D& heroBody = world.AddBody(heroDef, V2(h["pos"]), static_cast<int>(h["layer"].AsInt()), V2(h["colliderOffset"]), V2(h["colliderSize"]));
    CritterReplay critterRows;
    critterRows.Setup(start, colliders, enemyLines, world);
    bind::PhysicsScene physics(world, scene);
    physics.queries = &roomColliders;

    // The objects the colliders' messages name: the room's (one per collider, by its object's
    // name, tag and layer; Physics2D's room queries answer with them too) and the hero's body (the
    // "Player").
    auto standIn = [&](const std::string& name, const std::string& tag, int layer, bool trigger) {
        GameObject* g = GameObject::Create(name);
        if (!tag.empty()) g->tag_ = S(tag);
        g->layer_ = layer;
        auto* c = cs::gcnew<UnityEngine::BoxCollider2D>();
        c->isTrigger_ = trigger;
        g->Attach(c);
        return c;
    };
    // (Which of them are the room's body-less triggers: a hit on one is on the breakables' row.)
    std::vector<UnityEngine::Collider2D*> roomTriggerObjects;
    std::map<const unity::Collider2D*, UnityEngine::BoxCollider2D*> objectOf;
    for (b2Body* b = world.World().GetBodyList(); b; b = b->GetNext())
        for (b2Fixture* f = b->GetFixtureList(); f; f = f->GetNext())
            if (const unity::Collider2D* c = world.ColliderOf(f)) {
                UnityEngine::BoxCollider2D*& object = objectOf[c];
                if (!object) {
                    object = standIn(Leaf(c->path), c->tag, c->layer, c->trigger);
                    for (const unity::Collider2D& rt : roomTriggers.Colliders())
                        if (c == &rt) roomTriggerObjects.push_back(object);
                }
                physics.Register(f, object);
            }
    for (const unity::Collider2D& c : roomColliders.Colliders())
        if (objectOf.count(&c)) physics.RegisterQuery(&c, objectOf[&c]);
    // The critters' boxes (enemy_replay_critters.h, where recorded).
    for (const CritterReplay::Critter& k : critterRows.critters)
        if (k.body)
            for (b2Fixture* f = k.body->Body()->GetFixtureList(); f; f = f->GetNext())
                physics.Register(f, standIn(k.name, "", *static_cast<const int*>(f->GetUserData()), f->IsSensor()));
    auto RoomTriggerObject = [&](UnityEngine::Collider2D* c) {
        return std::find(roomTriggerObjects.begin(), roomTriggerObjects.end(), c) != roomTriggerObjects.end();
    };

    // The hero (hero_replay.h): HeroMerchant (HeroMerchant.Instance finds it), his controller, stats and
    // inventory, his art, his body's collider; his own code the game's, his body and doings the recording's.
    hero_replay::Hero hero;
    if (!hero_replay::Make(hero, h, file)) return 1;
    std::printf("the hero's stats: %s\n", hero.statsFrom.c_str());
    GameObject* heroGo = hero.go;
    gen::HeroMerchant* heroMerchant = hero.merchant;
    UnityEngine::BoxCollider2D* heroCollider = hero.collider;
    physics.Register(heroBody.Body()->GetFixtureList(), heroCollider);
    // His controller's Rigidbody2D (AddForce's: the enemies' pushes): outside the world, his body
    // being the recording's; the pushes are reported (bind::PhysicsScene::onAddForce).
    UnityEngine::Rigidbody2D* heroRigidbody = hero.rigidbody;
    physics.onAddForce = [&](UnityEngine::Rigidbody2D* body, Vector2 force, UnityEngine::ForceMode2D) {
        if (body == heroRigidbody && standins::S().heroAddForce) standins::S().heroAddForce(force);
    };
    auto setHeroPosition = [&](Vector2 p) { heroGo->transform_->set_position(UnityEngine::Vector3{p.x, p.y, 0.0f}); };
    setHeroPosition(V2(h["pos"]));

    // ---- the game's services the enemies reach ----
    const Value& sc = start["stats"];
    if (sc["ngp"].b || sc["dlc"].b) {
        std::printf("a New Game+ or DLC recording: GameManager's game slot isn't stood in for\n");
        return 1;
    }
    standins::S().difficulty = static_cast<int>(sc["difficulty"].AsInt());
    standins::S().afternoon = sc["afternoon"].b;
    standins::S().timeManager = cs::gcnew<gen::TimeManagerV2>();
    gen::CameraManager::_instance = cs::gcnew<gen::CameraManager>();
    // Its camera (CameraManager.Rumble shakes its transform: presentation, the shake's draws aren't).
    GameObject* cameraGo = GameObject::Create("Main Camera");
    auto* camera = cs::gcnew<UnityEngine::Camera>();
    cameraGo->Attach(camera);
    gen::CameraManager::_instance->_camera = camera;
    gen::HUDManager::Instance->minimap = cs::gcnew<gen::MinimapPanel>(); // (the hero's HUD: hero_replay.h)
    // MasterAudio's groups: the first scene's permanent creators' ("Sounds - Will" has the chests'
    // "dungeon_chest_unlock"), then the Golem dungeon's, then the general ones' (enemy_replay.h's).
    game::MasterAudio audio;
    if (!data.soundGroups.IsNull() && audio.Load(data.soundGroups, {"Sounds - Will", "Sounds - interface", "Sounds - GolemDungeon", "Sounds - GeneralDungeons"}))
        DarkTonic::MasterAudio::MasterAudio::engine = &audio;
    else std::printf("(no sound_groups.json: MasterAudio's draws not made)\n");

    // The room: Dungeon / <room> (DungeonRoom) / DungeonLevelEnemies / EnemySpawn / the enemy.
    const Value& chain0 = enemyLines[0]["chain"];
    auto at = [&](GameObject* g, Vector2 local) {
        g->transform_->localPosition_ = UnityEngine::Vector3{local.x, local.y, 0.0f};
        g->transform_->physicsDirty_ = false;
    };
    GameObject* dungeon = GameObject::Create("Dungeon");
    at(dungeon, V2(chain0[4]));
    GameObject* roomGo = GameObject::Create(start["room"].s, dungeon->transform_);
    at(roomGo, V2(chain0[3]));
    auto* room = cs::New<gen::DungeonRoom>();
    roomGo->Attach(room);
    room->roomInfo = cs::New<gen::DungeonRoomInfo>();
    room->roomInfo->culture = static_cast<gen::ItemMaster_Culture>(sc["dungeonCulture"].AsInt());
    room->roomFloorWidth = static_cast<int32_t>(start["roomFloor"][0].AsInt());
    room->roomFloorHeight = static_cast<int32_t>(start["roomFloor"][1].AsInt());
    room->roomCenterYOffset = static_cast<int32_t>(start["roomFloor"][2].AsInt());
    if (const Value* rp = start.Find("roomPos"))
        if (!Same(XY(roomGo->transform_->get_position()), V2(*rp)))
            std::printf("note: the room's transform %s isn't the recording's roomPos %s\n", Str(XY(roomGo->transform_->get_position())).c_str(),
                        Str(V2(*rp)).c_str());
    // Its middle detail (DungeonRoom.SpawnEnemyDeadSprite stamps a dead enemy's sprite on its
    // SpriteRenderer's): without one, as the recordings show it (no Random draw for the stamp's flip
    // where an enemy dies: slime.jsonl frames 310, 402 and 448, tangle.jsonl's).
    GameObject* middleDetail = GameObject::Create("MiddleDetail", roomGo->transform_);
    room->middleDetail = middleDetail->transform_;
    GameObject* levelEnemies = GameObject::Create("DungeonLevelEnemies", roomGo->transform_);
    at(levelEnemies, V2(chain0[2]));
    auto* dungeonManager = cs::New<gen::DungeonManager>();
    dungeonManager->intermediateInfo.dungeonType = gen::DungeonManager_DungeonType::Normal;
    dungeonManager->intermediateInfo.dungeonCulture = static_cast<gen::ItemMaster_Culture>(sc["dungeonCulture"].AsInt());
    dungeonManager->intermediateInfo.dungeonLevel = static_cast<int32_t>(sc["level"].AsInt());
    dungeonManager->currentDungeonRoom = room;
    gen::DungeonManager::_instance = dungeonManager;
    // Its PoolManager (DungeonManager.Awake's AddComponent: the turrets' shots).
    GameObject* managerGo = GameObject::Create("DungeonManager");
    dungeonManager->poolManager = managerGo->AddComponent<gen::PoolManager*>();

    // The map: PolyNav2D with the recorded polygons (its Awake builds them from the room's
    // colliders, which the recording gives already made).
    GameObject* navGo = GameObject::Create("PolyNav2D");
    navGo->SetActive(false);
    auto* nav = navGo->AddComponent<gen::PolyNav2D*>();
    nav->awoken_ = true;
    navGo->SetActive(true);
    auto setMap = [&](const Value& m) {
        auto polys = [](const Value& list) {
            auto* a = cs::Array<gen::PolyNav2D_Polygon>::New(static_cast<int32_t>(list.arr.size()));
            for (size_t i = 0; i < list.arr.size(); i++) {
                const Value& p = list.arr[i];
                auto* pts = cs::Array<Vector2>::New(static_cast<int32_t>(p.arr.size() / 2));
                for (size_t k = 0; k + 1 < p.arr.size(); k += 2) pts->at(static_cast<int32_t>(k / 2)) = Vector2{Bits(p[k]), Bits(p[k + 1])};
                a->at(static_cast<int32_t>(i)) = cs::NewS<gen::PolyNav2D_Polygon>(pts);
            }
            return a;
        };
        nav->inflateRadius = Bits(m["inflate"]);
        nav->map = cs::NewS<gen::PolyNav2D_PolyMap>(polys(m["master"]), polys(m["polys"]));
        nav->CreateNodes();
        nav->LinkNodes(nav->nodes);
    };
    size_t nextMap = 0;
    for (; nextMap < maps.size() && maps[nextMap]["f"].AsInt() < 0; nextMap++) setMap(maps[nextMap]);

    // ---- the enemies ----
    std::vector<Replayed> enemies;
    enemies.reserve(enemyLines.size());
    auto byId = [&](int id) -> Replayed* {
        for (Replayed& s : enemies)
            if (s.id == id) return &s;
        return nullptr;
    };
    auto idOf = [&](const gen::Enemy* e) {
        for (const Replayed& s : enemies)
            if (s.enemy == e) return s.id;
        return -1;
    };
    Ctx ctx;
    ctx.scene = &scene;
    ctx.physics = &physics;
    ctx.world = &world;
    ctx.roomColliders = &roomColliders;
    ctx.heroBody = &heroBody;
    ctx.heroCollider = heroCollider;
    ctx.audio = &audio;
    ctx.room = room;
    ctx.hero = heroMerchant;
    ctx.data = &data;
    ctx.enemies = &enemies;
    ctx.setHeroPosition = setHeroPosition;
    std::vector<std::pair<int, float>> portDamage;   // (enemy, strength) this step
    std::vector<std::pair<int, int>> portDamageTypes; // (enemy, HeroMerchant.DamageType) this step
    std::vector<Vector2> portDeadSprites;
    standins::S().dealDamageToPlayer = [&](float strength, cs::String*, int type, gen::Enemy* e) {
        portDamage.emplace_back(idOf(e), strength);
        portDamageTypes.emplace_back(idOf(e), type);
    };
    standins::S().spawnEnemyDeadSprite = [&](gen::DungeonRoom*, UnityEngine::Sprite*, UnityEngine::Vector3 p) { portDeadSprites.push_back(XY(p)); };
    hooks->OnScene(start, ctx, t);
    std::map<int, std::vector<std::string>> portAnim;
    std::map<int, std::vector<const Value*>> gameAnim;
    long long animFrame = -1;
    int animResynced = 0;
    std::vector<std::string> skipped;
    for (const Value& v : enemyLines) {
        enemies.emplace_back();
        Replayed& s = enemies.back();
        s.id = static_cast<int>(v["s"].AsInt());
        const Value& chain = v["chain"];
        GameObject* spawn = GameObject::Create("EnemySpawn", levelEnemies->transform_);
        at(spawn, V2(chain[1]));
        bind::PrefabOptions options;
        options.controller = data.controller.get();
        skipped.clear();
        s.go = bind::InstantiatePrefab(*data.prefab, spawn->transform_, options, &AddScript, &skipped);
        s.go->transform_->localPosition_ = UnityEngine::Vector3{Bits(chain[0][0]), Bits(chain[0][1]), 0.0f};
        s.enemy = s.go->GetComponent<gen::Enemy*>();
        s.behaviour = s.go->GetComponent<gen::EnemyBehaviour*>();
        s.agent = s.go->GetComponent<gen::PolyNavAgent*>();
        s.rb = s.go->GetComponent<UnityEngine::Rigidbody2D*>();
        s.box = s.go->GetComponent<UnityEngine::BoxCollider2D*>();
        s.animator = s.go->GetComponent<UnityEngine::Animator*>();
        s.attack = bind::FindBelow(s.go, "AttackCollider");
        s.attackBox = s.attack ? s.attack->GetComponent<UnityEngine::BoxCollider2D*>() : nullptr;
        s.attackRb = s.attack ? s.attack->GetComponent<UnityEngine::Rigidbody2D*>() : nullptr;
        if (!s.enemy || !s.behaviour || !s.agent || !s.rb || !s.box || !s.animator) {
            std::printf("the %s prefab didn't make its Enemy, behaviour, agent, body, box and Animator (a script not translated?)\n", data.name.c_str());
            return 1;
        }
        const int id = s.id;
        s.animator->onEvent = [&, id](UnityEngine::Animator&, const unity::AnimationEvent& ev, bool after) {
            Replayed* self = byId(id);
            std::vector<std::string>& mine = portAnim[id];
            if (!after) mine.push_back(ev.function);
            const size_t k = mine.size() - 1;
            const auto g = gameAnim.find(id);
            const Value* game = g == gameAnim.end() || k >= g->second.size() ? nullptr : g->second[k];
            const bool same = game && (*game)["fn"].s == ev.function && game->Find("rng0");
            if (!after) {
                if (same && !SameState(unity::Random::GetState(), State((*game)["rng0"]))) {
                    animResynced++;
                    unity::Random::SetState(State((*game)["rng0"]));
                }
                if (self) hooks->OnAnimEvent(*self, ev, false);
                return;
            }
            if (self) hooks->OnAnimEvent(*self, ev, true);
            if (!same) return;
            t.Add("Random draws in animation events (" + ev.function + ")", SameState(unity::Random::GetState(), State((*game)["rng1"])),
                  "frame " + std::to_string(animFrame) + " enemy " + std::to_string(id) + ": " +
                      std::to_string(unity::Random::DrawsBetween(State((*game)["rng0"]), unity::Random::GetState(), 64)) + " draws, the game " +
                      std::to_string(unity::Random::DrawsBetween(State((*game)["rng0"]), State((*game)["rng1"]), 64)));
            unity::Random::SetState(State((*game)["rng1"]));
        };
        // What spawned it set: its room, its dead sprite; a champion's flag (SpawnEnemies' draw).
        s.enemy->enemyRoom = room;
        room->roomEnemies->Add(s.enemy);
        s.enemy->enemyDeadSprite = v["deadSprite"].IsString() ? UnityEngine::Sprite::Named(v["deadSprite"].s) : nullptr;
        s.enemy->enemyStats->isChampion = v["stats"]["champion"].b;
        s.enemy->Init(S(start["enemy"].s));
        // The stats Init made against the game's, then the game's as they are now.
        const Value& st = v["stats"];
        gen::EnemyStats* es = s.enemy->enemyStats;
        t.Add("stats from enemy_stats.json (Init)",
              Same(es->get_maxHealth(), Bits(st["maxHealth"])) && Same(es->get_strength(), Bits(st["strength"])) &&
                  Same(es->get_speed(), Bits(st["speed"])) && Same(es->get_defense(), Bits(st["defense"])) && Same(es->get_weight(), Bits(st["weight"])),
              "enemy " + std::to_string(s.id) + ": maxHealth " + std::to_string(es->get_maxHealth()) + " vs " + std::to_string(Bits(st["maxHealth"])));
        es->_currentStats->maxHealth = Bits(st["maxHealth"]);
        es->_currentStats->strength = Bits(st["strength"]);
        es->_currentStats->speed = Bits(st["speed"]);
        es->_currentStats->defense = Bits(st["defense"]);
        es->_currentStats->weight = Bits(st["weight"]);
        es->isChampion = st["champion"].b;
        es->_currentHealth = Bits(v["hp"]);
        es->_invincible = v["inv"].b;
        es->_isAlive = v["alive"].b;
        // The prefab's agent against the game's object, then its state.
        const Value& ag = v["agent"];
        gen::PolyNavAgent* a = s.agent;
        t.Add("agent settings (prefab)",
              Same(a->mass, Bits(ag["mass"])) && Same(a->stoppingDistance, Bits(ag["stoppingDistance"])) && Same(a->slowingDistance, Bits(ag["slowingDistance"])) &&
                  Same(a->accelerationRate, Bits(ag["accelerationRate"])) && Same(a->decelerationRate, Bits(ag["decelerationRate"])) &&
                  Same(a->avoidRadius, Bits(ag["avoidRadius"])) && Same(a->lookAheadDistance, Bits(ag["lookAheadDistance"])) &&
                  a->repath == ag["repath"].b && a->restrict == ag["restrict"].b && a->closerPointOnInvalid == ag["closerPointOnInvalid"].b,
              "enemy " + std::to_string(s.id));
        a->maxSpeed = Bits(ag["maxSpeed"]);
        a->velocity = V2(ag["vel"]);
        a->_primeGoal = V2(ag["goal"]);
        a->accelerationValue = Bits(ag["acc"]);
        a->requests = static_cast<int32_t>(ag["req"].AsInt());
        a->_activePath->Clear();
        for (size_t k = 0; k + 1 < ag["path"].size(); k += 2) a->_activePath->Add(Vector2{Bits(ag["path"][k]), Bits(ag["path"][k + 1])});
        if (ag["req"].AsInt() > 0) std::printf("note: enemy %d starts with a path pending (not replayable)\n", s.id);
        const Value& rb = v["rb"];
        t.Add("Rigidbody2D settings (prefab, stats)",
              Same(s.rb->get_mass(), Bits(rb["mass"])) && Same(s.rb->def_.linearDrag, Bits(rb["drag"])) && s.rb->def_.constraints == static_cast<int>(rb["constraints"].AsInt()) &&
                  Same(s.box->size_.x, Bits(v["box"][2])) &&
                  // (An animated attack box is as the clip has it now: "attack trigger" rows; no AttackCollider: "attackBox" empty.)
                  (v.Find("attack") || (v["attackBox"].size() < 4 ? !s.attackBox : (s.attackBox && Same(s.attackBox->size_.x, Bits(v["attackBox"][2]))))),
              "enemy " + std::to_string(s.id));
        s.rb->set_mass(Bits(rb["mass"]));
        s.rb->set_drag(Bits(rb["drag"]));
        s.go->set_layer(static_cast<int32_t>(v["layer"].AsInt()));
        // The behaviour.
        gen::EnemyBehaviour* b = s.behaviour;
        const Value& be = v["behaviour"];
        b->enemyState = static_cast<gen::EnemyBehaviour_EnemyState>(be["es"].AsInt());
        b->targetPlayer = be["target"].b;
        b->canMove = be["canMove"].b;
        b->_lapse = Bits(be["lapse"]);
        b->playingDamagedEffect = be["dmgFx"].b;
        b->fallInHoles = be["fallInHoles"].b;
        b->deadForce = static_cast<int32_t>(be["deadForce"].AsInt());
        b->set_enabled(be["enabled"].b);
        if (const Value* locked = be.Find("locked"))
            if (auto* soldier = dynamic_cast<gen::GolemSoldierBehaviour*>(b)) soldier->iDirectionLocked = locked->b;
        t.Add("behaviour Init (playerOffset)", Same(XY(b->playerOffset), V2(be["playerOffset"])),
              Str(XY(b->playerOffset)) + " vs " + Str(V2(be["playerOffset"])));
        // The Animator and the sprite.
        const Value& an = v["anim"];
        const int state = data.controller->StateByHash(static_cast<int32_t>(an["state"].AsInt()));
        if (state >= 0) s.animator->Mecanim()->Play(data.controller->states[static_cast<size_t>(state)].name, Bits(an["t"]));
        s.animator->set_speed(Bits(an["speed"]));
        s.animator->set_enabled(an["on"].b);
        if (const Value* dx = an.Find("dx")) {
            s.animator->SetFloat(S("DistanceX"), Bits(*dx));
            s.animator->SetFloat(S("DistanceY"), Bits(an["dy"]));
        }
        // The attack trigger as it is (the prefab's were the Animator's defaults).
        if (const Value* ac = v.Find("attack")) {
            t.Add("attack trigger (prefab)", s.attackBox && (*ac)["layer"].AsInt() == s.attack->layer_, "enemy " + std::to_string(s.id));
            if (s.attackBox) {
                s.attackBox->enabled_ = (*ac)["on"].b;
                s.attack->activeSelf_ = (*ac)["active"].b;
                s.attackBox->size_ = V2((*ac)["size"]);
                s.attackBox->offset_ = V2((*ac)["offset"]);
                s.attack->transform_->localPosition_ = UnityEngine::Vector3{Bits((*ac)["pos"][0]), Bits((*ac)["pos"][1]), 0.0f};
            }
        }
        // The root box (its trigger flag against the prefab's; offset and size as the clips have
        // them now) and the hurtboxes against the prefab's (enemy_replay_colliders.h's Restore);
        // a HermitSlimeBehaviourNEW's patrol and look lock.
        const std::string who = "enemy " + std::to_string(s.id);
        if (const Value* trig = v.Find("rootTrigger")) t.Add("root box trigger (prefab)", s.box->isTrigger_ == trig->b, who);
        if (const Value* rbx = v.Find("rootBox")) {
            s.box->set_offset(Vector2{Bits((*rbx)[0]), Bits((*rbx)[1])});
            s.box->set_size(Vector2{Bits((*rbx)[2]), Bits((*rbx)[3])});
        }
        if (const Value* list = v.Find("receivers")) ReceiverRows(t, s, *list, "hurtboxes (prefab: path, box, layer, tag, advise)", who);
        if (gen::HermitSlimeBehaviourNEW* hs = Hermit(s)) {
            if (const Value* locked = be.Find("locked")) hs->iDirectionLocked = locked->b;
            if (const Value* p = be.Find("patrol")) hs->patrolLapse = Bits(*p);
            if (const Value* d = be.Find("dest")) hs->destinyPosition = UnityEngine::Vector3{Bits((*d)[0]), Bits((*d)[1]), 0.0f};
            if (const Value* o = be.Find("gridOrigin")) hs->roomGridOrigin = V2(*o);
            if (const Value* gs = be.Find("gridSize")) hs->roomGridSize = V2(*gs);
        }
        s.enemy->enemySprite->sprite_ = an["sprite"].IsString() ? UnityEngine::Sprite::Named(an["sprite"].s) : nullptr;
        MaterialOf(s)->SetFloat(std::string("_FlashAmount"), Bits(v["flash"]));
        MaterialOf(s)->SetColor(std::string("_Color"),
                                UnityEngine::Color{Bits(v["color"][0]), Bits(v["color"][1]), Bits(v["color"][2]), Bits(v["color"][3])});
        s.agent->OnDisable(); // (re-enabled below in allAgents' order)
        hooks->OnEnemy(s, v, t);
    }
    if (!skipped.empty()) {
        std::printf("the prefab's scripts not in the translation (presentation):");
        for (const std::string& k : skipped) std::printf(" %s", k.c_str());
        std::printf("\n");
    }
    hooks->OnEnemiesBuilt(start, t);
    for (const Value& name : start["agents"].arr) {
        bool found = false;
        for (Replayed& s : enemies)
            if (name.IsInt() && s.id == name.AsInt()) {
                s.agent->OnEnable();
                found = true;
            }
        if (!found) std::printf("note: PolyNavAgent.allAgents has %s, not one of the enemies\n", name.Dump().c_str());
    }
    for (Replayed& s : enemies) {
        const Value* v = nullptr;
        for (const Value& l : enemyLines)
            if (l["s"].AsInt() == s.id) v = &l;
        physics.SyncColliders(s.rb);
        if (s.attackRb) physics.SyncColliders(s.attackRb);
        physics.PlaceBody(s.rb, V2((*v)["rb"]["pos"]));
        s.rb->body_->SetVelocity({Bits((*v)["rb"]["vel"][0]), Bits((*v)["rb"]["vel"][1])});
        if (!(*v)["rb"]["awake"].b) s.rb->body_->Body()->SetAwake(false);
    }
    // (What the setup made is as the game made it, long before: not remade.)
    physics.Settle();
    unity::Random::SetState(State(f0["rng"]));

    const float fixedDt = Bits(start["fixedDeltaTime"]);
    UnityEngine::Time::fixedDeltaTime = fixedDt;
    double fixedClock = 0.0;
    unity::DelayedCalls& calls = scene.calls;
    // EnableEnemies (an Invoke itself): the fixed steps it came after, and the targeting it
    // starts a second later, if that hasn't come yet, on the clock its Invokes count from
    // (tests/enable_clock.h).
    const EnableClock enable = EnableEnemiesClock(start, f0, fixedDt);
    if (enable.probed)
        t.Add("EnableClockProbe: an Invoke made in a frame counts from Time.fixedTime (unity/DelayedCalls.h)", enable.invokeOnFixedClock,
              "EnableEnemies at " + std::to_string(enable.time) + ", the fixed step before at " + std::to_string(enable.fixedTime));
    if (enable.known) {
        calls.now = enable.time;
        calls.fixedNow = enable.fixedTime;
        for (Replayed& s : enemies)
            if (!s.behaviour->targetPlayer) s.behaviour->StartTargetingPlayerAfterTime(Bits(start["targetDelay"]));
        calls.inFixedStep = false;
    }
    // The hero's body as a fixed step found it: layer, transform, velocity, awake.
    auto setHero = [&](const Value& hs) {
        world.SetLayer(heroBody, static_cast<int>(hs[6].AsInt()));
        world.SyncTransform(heroBody, {Bits(hs[0]), Bits(hs[1])});
        heroBody.SetVelocity({Bits(hs[4]), Bits(hs[5])});
        if (heroBody.IsAwake() != hs[7].b) heroBody.Body()->SetAwake(hs[7].b);
    };
    // The bodies were switched on by EnableEnemies, some fixed steps before the recording
    // starts: those steps count towards their falling asleep (Box2D's 0.5 s at rest).
    if (enable.known) {
        for (int k = 0; k < enable.stepsBefore; k++) {
            setHero(f0["steps"][0]["hero"]);
            physics.Step(fixedDt);
        }
        std::printf("%d fixed steps between EnableEnemies and the recording (stepped first)\n", enable.stepsBefore);
    }

    // The weapon's push (Weapon.PushEnemyBackwards, the hero's code, translated).
    auto* weapon = cs::gcnew<gen::Weapon>();
    cs::Root<gen::Weapon> weaponRoot(weapon);

    int rngElsewhere = 0;
    // ML_RESYNC_DEBUG=1: each resync, where, and how many draws the game made that the replay didn't
    // (or the other way: -1, the replay's state isn't behind the game's within 64).
    const bool resyncDebug = std::getenv("ML_RESYNC_DEBUG") != nullptr;
    auto ResyncNote = [](long long frame, const std::string& where, const unity::Random::State& ours, const unity::Random::State& game) {
        std::printf("resync: frame %lld before %s: the game drew %d more\n", frame, where.c_str(), unity::Random::DrawsBetween(ours, game, 64));
    };
    // Whether the recording has the fixtures Unity made per step (fixture_probe.h).
    bool fixturesRecorded = false;
    for (const Value& fr : frames)
        if (!fr["steps"].arr.empty()) {
            fixturesRecorded = RecordsFixtures(fr);
            break;
        }
    if (!fixturesRecorded)
        std::printf("note: no \"fx\" in the steps (recorded before the oracle's FixtureProbe): the fixtures Unity made for objects "
                    "this replay doesn't hold are guessed from the weapon's hits\n");
    // The game's attack-trigger enters: "attackEnter" with "how" "trigger" where the start line has
    // "attackEnters", else the calls of EnemyBehaviour.AttackHit ("attackHit"; a behaviour whose
    // override skips the base one never reaches it: its enters aren't compared).
    const bool entersRecorded = start.Find("attackEnters") != nullptr;
    const char* const enterKind = entersRecorded ? "attackEnter" : "attackHit";
    int unseenEnters = 0;
    const bool gcEveryFrame = std::getenv("ML_GC_EVERY_FRAME") != nullptr;
    int gcRuns = 0;
    size_t gcFreed = 0;
    using replay_profile::Scope;
    replay_profile::Reset();
    std::vector<Released> released;
    auto runRoomExit = [&](int step) {
        hooks->AroundRoomExit(false, step);
        room->DisableEnemies();
        hooks->AroundRoomExit(true, step);
    };
    for (size_t fi = 0; fi < frames.size(); fi++) {
        const Value& f = frames[fi];
        const long long fn = f["f"].AsInt();
        const float dt = Bits(f["dt"]);
        const double time = static_cast<double>(Bits(f["time"]));
        scene.BeginFrame();
        while (nextMap < maps.size() && maps[nextMap]["f"].AsInt() <= fn) {
            std::printf("note: the map was regenerated in frame %lld (applied at its start)\n", fn);
            setMap(maps[nextMap++]);
        }
        if (!SameState(unity::Random::GetState(), State(f["rng"]))) {
            rngElsewhere++;
            if (resyncDebug) ResyncNote(fn, "the frame's start", unity::Random::GetState(), State(f["rng"]));
            unity::Random::SetState(State(f["rng"]));
        }
        // Time.frameCount: the frame's from its start, its fixed steps' sounds included (a
        // variation played there isn't playing yet in the frame's Update: game/MasterAudio.h).
        audio.frameCount = f.Find("fc") ? static_cast<int>(f["fc"].AsInt()) : static_cast<int>(fn);
        UnityEngine::Time::frameCount = audio.frameCount;
        hooks->BeginFrame(fn, f);
        roomExit.Plan(f, fi > 0 ? &frames[fi - 1] : nullptr, enemies, room);
        const Value& events = f["events"];
        // The hero's weapon hitbox (not in this world) came on since the last step if its first
        // touch lands in this frame's first step (e_newFixture: PhysicsWorld::FixtureMadeElsewhere).
        for (const Value& ev : events.arr) {
            if (fixturesRecorded) break; // (the steps' "fx" tell: fixture_probe.h)
            const std::string& kind = ev["k"].s;
            // (A hit an enemy dealt ("by": the KamikazeGolem's) is the enemies' own doing.)
            if (ev["step"].AsInt() == 0 && !ev.Find("by") &&
                (kind == "push" || kind == "dmgToEnemy" || (kind == "attackHit" && !ev["player"].b && ev["other"].IsString() && ev["other"].s == "Collider"))) {
                world.FixtureMadeElsewhere();
                break;
            }
        }
        // The fixed steps.
        for (size_t k = 0; k < f["steps"].size(); k++) {
            const Value& st = f["steps"][k];
            fixedClock = fixedClock > 0.0 ? fixedClock + static_cast<double>(fixedDt) : EngineFixedTime(Bits(st["ft"]), fixedDt); // (enable_clock.h: a whole number of steps since the start)
            if (std::fabs(fixedClock - static_cast<double>(Bits(st["ft"]))) > 1e-4)
                std::printf("note: frame %lld step %zu: the fixed clock %.9f, the recording %.9f\n", fn, k, fixedClock, static_cast<double>(Bits(st["ft"])));
            calls.now = time;
            calls.fixedNow = fixedClock;
            calls.inFixedStep = true;
            UnityEngine::Time::deltaTime = fixedDt;
            UnityEngine::Time::time = static_cast<float>(fixedClock);
            // Start for the behaviours enabled since (a pooled shot taken out), before their first
            // FixedUpdate or Update (Unity's).
            scene.RunStarts();
            setHero(st["hero"]);
            for (const Value& ss : st["s"].arr) {
                Replayed* s = byId(static_cast<int>(ss[0].AsInt()));
                if (!s || s->Gone()) continue;
                if (ss.size() >= 15)
                    t.Add("attack trigger at a fixed step's start (on, size, offset, position)",
                          SameAttack(*s, ss[8].b, {Bits(ss[9]), Bits(ss[10])}, {Bits(ss[11]), Bits(ss[12])}, {Bits(ss[13]), Bits(ss[14])}),
                          "frame " + std::to_string(fn) + " step " + std::to_string(k) + " enemy " + std::to_string(ss[0].AsInt()) + ": port " + AttackStr(*s));
                const bool ok = Same(Position(*s), {Bits(ss[1]), Bits(ss[2])}) && Same(s->rb->body_->position(), {Bits(ss[3]), Bits(ss[4])}) &&
                                Same(s->rb->body_->velocity(), {Bits(ss[5]), Bits(ss[6])});
                t.Add("fixed step's start (transform, body)", ok,
                      "frame " + std::to_string(fn) + " step " + std::to_string(k) + " enemy " + std::to_string(ss[0].AsInt()) + ": port " + Str(Position(*s)) +
                          " body " + Str(s->rb->body_->position()) + " v " + Str(s->rb->body_->velocity()) + "; game " + Str({Bits(ss[1]), Bits(ss[2])}) +
                          " body " + Str({Bits(ss[3]), Bits(ss[4])}) + " v " + Str({Bits(ss[5]), Bits(ss[6])}));
            }
            // The root boxes and centres of mass ("box": enemy_replay_colliders.h's StepStart).
            if (const Value* boxes = st.Find("box"))
                for (const Value& bx : boxes->arr) {
                    Replayed* s = byId(static_cast<int>(bx[0].AsInt()));
                    if (!s || s->Gone() || !s->rb->body_) continue;
                    const std::string where = "frame " + std::to_string(fn) + " step " + std::to_string(k) + " enemy " + std::to_string(bx[0].AsInt());
                    t.Add("root box at a fixed step's start (offset, size)",
                          Same(s->box->offset_, {Bits(bx[1]), Bits(bx[2])}) && Same(s->box->size_, {Bits(bx[3]), Bits(bx[4])}),
                          where + ": port " + Str(s->box->offset_) + " " + Str(s->box->size_) + " game " + Str({Bits(bx[1]), Bits(bx[2])}) + " " +
                              Str({Bits(bx[3]), Bits(bx[4])}));
                    const b2Vec2 c = s->rb->body_->Body()->GetLocalCenter();
                    t.Add("Rigidbody2D centre of mass at a fixed step's start", Same({c.x, c.y}, {Bits(bx[5]), Bits(bx[6])}),
                          where + ": port " + Str({c.x, c.y}) + " game " + Str({Bits(bx[5]), Bits(bx[6])}));
                }
            hooks->BeforeStep(fn, k, f, t);
            // Fixtures Unity made since the step before that this world doesn't hold (fixture_probe.h).
            if (fixturesRecorded && StepMadeFixtures(st)) world.FixtureMadeElsewhere();
            portDamage.clear();
            portDamageTypes.clear();
            critterRows.BeforeStep(f, k, world);
            {
                Scope p(replay_profile::Physics);
                physics.Step(fixedDt);
            }
            // The attack triggers' OnTriggerEnter2D in this step (but the hero's own triggers').
            struct Hit {
                int id;
                bool player, roomTrigger;
            };
            std::vector<Hit> hits;
            for (const bind::PhysicsScene::Hit& hit : physics.LastHits())
                for (const Replayed& s : enemies) {
                    if (!s.attackBox) continue;
                    UnityEngine::Collider2D* other = hit.a == s.attackBox ? hit.b : hit.b == s.attackBox ? hit.a : nullptr;
                    if (!other || s.Gone()) continue;
                    hits.push_back({s.id, other == heroCollider, RoomTriggerObject(other)});
                }
            // (On the room's critters where the recording lacks their moves: a row of their own.)
            for (int pass = 0; pass < 3; pass++) {
                const bool triggers = pass == 1, critterPass = pass == 2;
                if (critterPass && critterRows.recorded) continue;
                std::vector<std::pair<int, bool>> mine, theirs;
                if (!critterPass)
                    for (const Hit& hit : hits) {
                        if (hit.roomTrigger != triggers) continue;
                        if (!entersRecorded && AttackHitSkipsBase(byId(hit.id)->behaviour)) {
                            unseenEnters++;
                            continue;
                        }
                        mine.emplace_back(hit.id, hit.player);
                    }
                for (const Value& ev : events.arr) {
                    if (ev["k"].s != enterKind || ev["step"].AsInt() != static_cast<long long>(k)) continue;
                    if (entersRecorded && ev["how"].s != "trigger") continue;
                    const std::string other = ev["other"].IsString() ? ev["other"].s : "";
                    if (HerosTrigger(other)) continue;
                    if ((!critterRows.recorded && critterRows.IsCritterName(other)) != critterPass) continue;
                    if (!critterPass && RoomTrigger(other) != triggers) continue;
                    theirs.emplace_back(static_cast<int>(ev["s"].AsInt()), ev["player"].b);
                }
                if (mine.empty() && theirs.empty()) continue;
                std::sort(mine.begin(), mine.end());
                std::sort(theirs.begin(), theirs.end());
                std::string which = "frame " + std::to_string(fn) + " step " + std::to_string(k) + ": port";
                for (const auto& hit : mine) which += " " + std::to_string(hit.first) + (hit.second ? "/hero" : "/other");
                which += ", game";
                for (const auto& hit : theirs) which += " " + std::to_string(hit.first) + (hit.second ? "/hero" : "/other");
                if (critterPass)
                    t.Add("attack trigger: OnTriggerEnter2D on the room's critters (their moves unrecorded: record again, CritterOracle.cs)",
                          mine == theirs, which);
                else if (triggers) t.Info("attack trigger: OnTriggerEnter2D on the room's triggers (breakables)", mine == theirs, which);
                else t.Add("attack trigger: OnTriggerEnter2D (whose, on the hero or a collider)", mine == theirs, which);
            }
            // The attack collider's OnCollisionEnter2D ("attackEnter", "how" "collision"): the
            // attack colliders are triggers (sensors make no collisions), so each one the game had fails.
            if (entersRecorded)
                for (const Value& ev : events.arr) {
                    if (ev["k"].s != "attackEnter" || ev["step"].AsInt() != static_cast<long long>(k) || ev["how"].s == "trigger") continue;
                    t.Add("attack collider: OnCollisionEnter2D (none in the port)", false,
                          "frame " + std::to_string(fn) + " step " + std::to_string(k) + ": enemy " + std::to_string(ev["s"].AsInt()) + " on " +
                              (ev["other"].IsString() ? ev["other"].s : std::string("?")));
                }
            // The weapon's hits in this step, in the game's order; the enemies' own hits on him.
            std::vector<std::pair<int, float>> gameDamage;
            for (const Value& ev : events.arr) {
                if (ev["step"].AsInt() != static_cast<long long>(k)) continue;
                const std::string& kind = ev["k"].s;
                Replayed* s = byId(static_cast<int>(ev["s"].AsInt()));
                if (kind == "dmgToPlayer") gameDamage.emplace_back(static_cast<int>(ev["s"].AsInt()), Bits(ev["str"]));
                if (!s) continue;
                if (kind == "dmgToEnemy" && ev.Find("by")) continue; // (an enemy's hit: the translated code deals it itself)
                if (kind == "dmgToEnemy") {
                    setHeroPosition(V2(ev["hero"]));
                    t.Add("health before a weapon hit", Same(Health(*s), Bits(ev["hp0"])), "frame " + std::to_string(fn) + " enemy " + std::to_string(ev["s"].AsInt()));
                    // Through the hurtbox it names, if any (enemy_replay_colliders.h's DealDamage).
                    const auto type = static_cast<gen::Enemy_EnemyDamageReceivedType>(ev["type"].AsInt());
                    const Value* recv = ev.Find("recv");
                    gen::EnemyDamageReceiver* r = recv && recv->IsString() ? Receiver(*s, recv->s) : nullptr;
                    {
                        Scope p(replay_profile::FixedSteps);
                        if (r) r->DealDamageToEnemy(Bits(ev["str"]), type);
                        else s->enemy->DealDamageToEnemy(Bits(ev["str"]), nullptr, type);
                    }
                    t.Add("health after a weapon hit", Same(Health(*s), Bits(ev["hp1"])),
                          "frame " + std::to_string(fn) + " enemy " + std::to_string(ev["s"].AsInt()) + ": " + std::to_string(Health(*s)) + " vs " +
                              std::to_string(Bits(ev["hp1"])));
                } else if (kind == "push") {
                    t.Add("weapon push: the enemy where the game had it", Same(Position(*s), V2(ev["pos"])), Str(Position(*s)) + " vs " + Str(V2(ev["pos"])));
                    setHeroPosition(V2(ev["hero"]));
                    Scope p(replay_profile::FixedSteps);
                    weapon->PushEnemyBackwards(s->go, Bits(ev["force"]));
                }
            }
            // The hits' damage types against the game's (enemy_replay_colliders.h's StepTypes).
            {
                std::vector<std::pair<int, int>> theirs;
                bool typed = false;
                for (const Value& ev : events.arr) {
                    if (ev["k"].s != "dmgToPlayer" || ev["step"].AsInt() != static_cast<long long>(k)) continue;
                    if (const Value* type = ev.Find("type")) {
                        typed = true;
                        theirs.emplace_back(static_cast<int>(ev["s"].AsInt()), static_cast<int>(type->AsInt()));
                    }
                }
                if (typed) {
                    std::vector<std::pair<int, int>> mine = portDamageTypes;
                    std::sort(mine.begin(), mine.end());
                    std::sort(theirs.begin(), theirs.end());
                    std::string which = "frame " + std::to_string(fn) + " step " + std::to_string(k) + ": port";
                    for (const auto& d : mine) which += " " + std::to_string(d.first) + "/" + std::to_string(d.second);
                    which += ", game";
                    for (const auto& d : theirs) which += " " + std::to_string(d.first) + "/" + std::to_string(d.second);
                    t.Add("DealDamageToPlayer's damage type (ItemDrain: 1)", mine == theirs, which);
                }
            }
            {
                Scope p(replay_profile::FixedSteps);
                if (roomExit.step == static_cast<int>(k)) runRoomExit(static_cast<int>(k));
                calls.UpdateFixed(fixedClock);
                scene.ResumeFixed();
            }
            calls.inFixedStep = false;
            hooks->AfterStep(fn, k, f, t);
            if (!portDamage.empty() || !gameDamage.empty()) {
                auto sorted = [](std::vector<std::pair<int, float>> v) {
                    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
                    return v;
                };
                const auto ps = sorted(portDamage), gs = sorted(gameDamage);
                bool sameHits = ps.size() == gs.size();
                for (size_t n = 0; sameHits && n < gs.size(); n++) sameHits = ps[n].first == gs[n].first && Same(ps[n].second, gs[n].second);
                bool sameOrder = sameHits;
                for (size_t n = 0; sameOrder && n < gameDamage.size(); n++) sameOrder = portDamage[n].first == gameDamage[n].first;
                std::string which = "frame " + std::to_string(fn) + " step " + std::to_string(k) + ": port";
                for (const auto& d : portDamage) which += " " + std::to_string(d.first);
                which += ", game";
                for (const auto& d : gameDamage) which += " " + std::to_string(d.first);
                t.Add("attack trigger: DealDamageToPlayer (who, how hard)", sameHits, which);
                if (sameHits && gameDamage.size() > 1) t.Info("attack trigger: order of hits in one step (Box2D's proxy ids)", sameOrder, which);
            }
        }
        // The frame: DOTween, the Updates in the game's order, the delayed calls, the coroutines,
        // the Animators, the LateUpdates.
        calls.now = time;
        audio.now = time;
        UnityEngine::Time::deltaTime = dt;
        UnityEngine::Time::time = static_cast<float>(time);
        const Value* tweenUdt = f.Find("dotweenUdt");
        {
            Scope p(replay_profile::Tweens);
            scene.tweens.Update(dt, tweenUdt ? Bits(*tweenUdt) : f.Find("udt") ? Bits(f["udt"]) : dt);
            DG::Tweening::Running::Prune();
        }
        scene.RunStarts();
        size_t updateIndex = 0;
        for (const Value& u : f["updates"].arr) {
            if (roomExit.step == -1 && roomExit.updatesBefore == updateIndex++) runRoomExit(-1);
            Replayed* s = byId(static_cast<int>(u["s"].AsInt()));
            if (!s) continue;
            setHeroPosition(V2(u["hero"]));
            if (!SameState(unity::Random::GetState(), State(u["rng0"]))) {
                rngElsewhere++;
                if (resyncDebug) ResyncNote(fn, "enemy " + std::to_string(u["s"].AsInt()) + "'s Update", unity::Random::GetState(), State(u["rng0"]));
                unity::Random::SetState(State(u["rng0"]));
            }
            t.Add("Update called on an enabled enemy", !s->Gone() && s->behaviour->get_isActiveAndEnabled(), "frame " + std::to_string(fn));
            {
                Scope p(replay_profile::Updates);
                s->behaviour->cs_Update();
            }
            t.Add("Random draws in the enemies' Update", SameState(unity::Random::GetState(), State(u["rng1"])),
                  "frame " + std::to_string(fn) + " enemy " + std::to_string(u["s"].AsInt()));
            unity::Random::SetState(State(u["rng1"]));
        }
        if (roomExit.step == -1 && roomExit.updatesBefore >= f["updates"].size()) runRoomExit(-1);
        // The frame's events that need no step (enemy_replay_colliders.h's FrameEvents): the
        // HermitSlime's patrol and ItemDrain from the game's Random states (the state put back
        // after), the hurtbox and weapon hits.
        {
            const unity::Random::State saved = unity::Random::GetState();
            for (const Value& ev : events.arr) {
                const std::string& kind = ev["k"].s;
                Replayed* s = byId(static_cast<int>(ev["s"].AsInt()));
                const std::string where = "frame " + std::to_string(fn) + " enemy " + std::to_string(ev["s"].AsInt());
                if (kind == "patrol" && s) {
                    gen::HermitSlimeBehaviourNEW* hs = Hermit(*s);
                    if (!hs) continue;
                    // ChooseNewDestination from the game's state, the grid as the translated code has it now.
                    unity::Random::SetState(State(ev["rng0"]));
                    Vector2 origin = hs->roomGridOrigin, size = hs->roomGridSize;
                    if (size.x == 0.0f) {
                        size = {static_cast<float>(room->roomFloorWidth), static_cast<float>(room->roomFloorHeight)};
                        const Vector2 rp = XY(roomGo->transform_->get_position());
                        origin = (rp + Vector2{0.0f, static_cast<float>(room->roomCenterYOffset)}) - size * 0.5f;
                    }
                    const int x = unity::Random::Range(0, static_cast<int>(size.x));
                    const int y = unity::Random::Range(0, static_cast<int>(size.y));
                    const Vector2 dest = Vector2{static_cast<float>(x), static_cast<float>(y)} + origin;
                    t.Add("patrol: ChooseNewDestination from the game's Random state (destination, draws)",
                          Same(dest, V2(ev["dest"])) && SameState(unity::Random::GetState(), State(ev["rng1"])),
                          where + ": port " + Str(dest) + " game " + Str(V2(ev["dest"])));
                    t.Add("patrol: the port's lapse and destination after the Update",
                          Same(hs->patrolLapse, Bits(ev["lapse"])) && Same(XY(hs->destinyPosition), V2(ev["dest"])),
                          where + ": port " + std::to_string(hs->patrolLapse) + " " + Str(XY(hs->destinyPosition)) + " game " + std::to_string(Bits(ev["lapse"])) +
                              " " + Str(V2(ev["dest"])));
                } else if (kind == "drain") {
                    // HeroMerchantInventory.DropRandomItems (the hero's: game/HeroDamage.h's model)
                    // from the game's state and its count of droppable slots.
                    unity::Random::SetState(State(ev["rng0"]));
                    std::vector<int> slots;
                    for (int i = 0; i < static_cast<int>(ev["eligible"].AsInt()); i++) slots.push_back(i);
                    game::DropRandomItemsDraws(slots, static_cast<int>(ev["number"].AsInt()));
                    t.Add("ItemDrain: DropRandomItems' draws (the game's droppable slots)", SameState(unity::Random::GetState(), State(ev["rng1"])),
                          "frame " + std::to_string(fn) + ": " + std::to_string(ev["eligible"].AsInt()) + " slots, the game drew " +
                              std::to_string(unity::Random::DrawsBetween(State(ev["rng0"]), State(ev["rng1"]), 64)));
                } else if (kind == "recvHit" && s) {
                    gen::EnemyDamageReceiver* r = Receiver(*s, ev["path"].s);
                    t.Add("weapon hit through a hurtbox: the port's, advising as the game's", r && (r->gameObjectToAdvise != nullptr) == ev["advise"].b,
                          where + ": " + ev["path"].s + (ev["advise"].b ? " advises" : " advises nobody"));
                } else if (kind == "weaponHit" && s) {
                    const std::string& other = ev["other"].s;
                    gen::EnemyDamageReceiver* r = Receiver(*s, other);
                    const std::string tag = Utf8(r ? r->gameObject_->get_tag() : s->go->get_tag());
                    t.Info("weapon hit: the tag of what it hit (root or hurtbox)", tag == ev["tag"].s, where + ": " + other + " port " + tag + " game " + ev["tag"].s);
                }
            }
            unity::Random::SetState(saved);
        }
        {
            Scope p(replay_profile::Delayed);
            calls.Update(time);
            scene.ResumeYieldNull();
        }
        portAnim.clear();
        gameAnim.clear();
        animFrame = fn;
        for (const Value& ev : events.arr)
            if (ev["k"].s == "anim" && !(ev.Find("caller") && ev["caller"].IsString())) gameAnim[static_cast<int>(ev["s"].AsInt())].push_back(&ev);
        {
            Scope p(replay_profile::Animators);
            scene.UpdateAnimators(dt);
        }
        for (Replayed& s : enemies) {
            const auto g = gameAnim.find(s.id);
            const auto m = portAnim.find(s.id);
            const size_t ng = g == gameAnim.end() ? 0 : g->second.size(), nm = m == portAnim.end() ? 0 : m->second.size();
            if (!ng && !nm) continue;
            bool same = ng == nm;
            std::string list = "frame " + std::to_string(fn) + " enemy " + std::to_string(s.id) + ": port";
            for (size_t k = 0; k < nm; k++) list += " " + m->second[k];
            list += "; game";
            for (size_t k = 0; k < ng; k++) {
                list += " " + (*g->second[k])["fn"].s;
                same = same && k < nm && m->second[k] == (*g->second[k])["fn"].s;
            }
            t.Add("Animator clip events (per enemy, in order)", same, list);
        }
        {
            Scope p(replay_profile::LateUpdates);
            nav->cs_LateUpdate();
            for (const Value& id : f["late"].arr) {
                Replayed* s = byId(static_cast<int>(id.AsInt()));
                if (s && !s->Gone() && s->agent->get_isActiveAndEnabled()) s->agent->cs_LateUpdate();
            }
        }
        // MasterAudio's LateUpdate: the updaters of this frame's plays start (game/MasterAudio.h:
        // the intro silence's draws).
        audio.LateUpdate();
        // Object.Destroy(gameObject) made this frame (the KamikazeGolem's SelfDestroy): gone now.
        scene.ProcessDestroys();
        std::vector<Vector2> gameDead;
        for (const Value& ev : events.arr)
            if (ev["k"].s == "deadSprite") gameDead.push_back(V2(ev["pos"]));
        if (!gameDead.empty() || !portDeadSprites.empty()) {
            bool same = gameDead.size() == portDeadSprites.size();
            for (size_t k = 0; same && k < gameDead.size(); k++) same = Same(gameDead[k], portDeadSprites[k]);
            t.Add("dead sprites spawned (where)", same,
                  "frame " + std::to_string(fn) + ": port " + std::to_string(portDeadSprites.size()) + " game " + std::to_string(gameDead.size()));
        }
        portDeadSprites.clear();

        // The frame's end, per enemy.
        for (const Value& v : (f.Find("enemies") ? f["enemies"] : f["slimes"]).arr) {
            const int id = static_cast<int>(v["s"].AsInt());
            Replayed* sp = byId(id);
            if (!sp) continue;
            Replayed& s = *sp;
            const std::string at2 = "frame " + std::to_string(fn) + " enemy " + std::to_string(id);
            t.Add("destroyed", s.Gone() == v["gone"].b, at2);
            if (v["gone"].b || s.Gone()) continue;
            const Value& ag = v["agent"];
            const Value& an = v["anim"];
            const Value& be = v["behaviour"];
            t.Add("position (world, local)", Same(Position(s), V2(v["pos"])) && Same(Local(s), V2(v["local"])),
                  at2 + ": port " + Str(Position(s)) + " game " + Str(V2(v["pos"])));
            const Value& rb = v["rb"];
            unity::Rigidbody2D* body = s.rb->body_;
            t.Add("Rigidbody2D (position, velocity, awake)",
                  Same(body->position(), {Bits(rb[0]), Bits(rb[1])}) && Same(body->velocity(), {Bits(rb[2]), Bits(rb[3])}) && body->IsAwake() == v["awake"].b,
                  at2 + ": port " + Str(body->position()) + " v " + Str(body->velocity()) + (body->IsAwake() ? " awake" : " asleep") + " game " +
                      Str({Bits(rb[0]), Bits(rb[1])}) + " v " + Str({Bits(rb[2]), Bits(rb[3])}) + (v["awake"].b ? " awake" : " asleep"));
            std::vector<Vector2> gamePath;
            for (size_t k = 0; k + 1 < ag["path"].size(); k += 2) gamePath.push_back({Bits(ag["path"][k]), Bits(ag["path"][k + 1])});
            cs::List<Vector2>* portPath = s.agent->_activePath;
            bool path = static_cast<int32_t>(gamePath.size()) == portPath->get_Count();
            for (size_t k = 0; path && k < gamePath.size(); k++) path = Same(gamePath[k], portPath->get_Item(static_cast<int32_t>(k)));
            t.Add("agent velocity", Same(s.agent->velocity, V2(ag["vel"])), at2 + ": port " + Str(s.agent->velocity) + " game " + Str(V2(ag["vel"])));
            t.Add("agent goal, path, pending, acceleration",
                  path && Same(s.agent->_primeGoal, V2(ag["goal"])) && s.agent->requests == ag["req"].AsInt() && Same(s.agent->accelerationValue, Bits(ag["acc"])),
                  at2 + ": port goal " + Str(s.agent->_primeGoal) + " path " + std::to_string(portPath->get_Count()) + " req " +
                      std::to_string(s.agent->requests) + "; game " + Str(V2(ag["goal"])) + " " + std::to_string(gamePath.size()) + " " +
                      std::to_string(ag["req"].AsInt()));
            const unity::AnimatorStateInfo info = InfoOf(s);
            t.Add("Animator state, normalized time (bits)",
                  s.animator->enabled_ == an["on"].b &&
                      (!an["on"].b || (info.shortNameHash == static_cast<int32_t>(an["state"].AsInt()) && Same(info.normalizedTime, Bits(an["t"])))),
                  at2 + ": port t " + std::to_string(info.normalizedTime) + " game " + std::to_string(Bits(an["t"])));
            t.Add("sprite", an["sprite"].IsString() && SpriteOf(s) == an["sprite"].s, at2 + ": port " + SpriteOf(s) + " game " + an["sprite"].Dump());
            gen::EnemyStats* es = s.enemy->enemyStats;
            t.Add("health, alive, invincible", Same(es->_currentHealth, Bits(v["hp"])) && es->_isAlive == v["alive"].b && es->_invincible == v["inv"].b,
                  at2 + ": port " + std::to_string(es->_currentHealth) + (es->_isAlive ? " alive" : " dead") + (es->_invincible ? " invincible" : "") +
                      ", game " + std::to_string(Bits(v["hp"])) + (v["alive"].b ? " alive" : " dead") + (v["inv"].b ? " invincible" : ""));
            gen::EnemyBehaviour* b = s.behaviour;
            t.Add("behaviour (state, targeting, canMove, lapse, damaged, enabled, layer)",
                  static_cast<int>(b->enemyState) == be["es"].AsInt() && b->targetPlayer == be["target"].b && b->canMove == be["canMove"].b &&
                      Same(b->_lapse, Bits(be["lapse"])) && b->playingDamagedEffect == be["dmgFx"].b && b->enabled_ == be["enabled"].b &&
                      s.go->layer_ == v["layer"].AsInt(),
                  at2 + ": port lapse " + std::to_string(b->_lapse) + " target " + std::to_string(b->targetPlayer) + " game " +
                      std::to_string(Bits(be["lapse"])) + " " + std::to_string(be["target"].b));
            if (const Value* ac = v.Find("attack"))
                t.Add("attack trigger (on, size, offset, position, active)",
                      SameAttack(s, (*ac)["on"].b, V2((*ac)["size"]), V2((*ac)["offset"]), V2((*ac)["pos"])) && s.attack->activeSelf_ == (*ac)["active"].b,
                      at2 + ": port " + AttackStr(s) + "; game " + ac->Dump());
            if (const Value* locked = be.Find("locked"))
                if (auto* soldier = dynamic_cast<gen::GolemSoldierBehaviour*>(b)) t.Add("look locked (iDirectionLocked)", soldier->iDirectionLocked == locked->b, at2);
            // The root box, the body's mass and centre, the hurtboxes, the patrol (enemy_replay_colliders.h's End).
            if (const Value* rbx = v.Find("rootBox"))
                t.Add("root box (offset, size)", Same(s.box->offset_, {Bits((*rbx)[0]), Bits((*rbx)[1])}) && Same(s.box->size_, {Bits((*rbx)[2]), Bits((*rbx)[3])}),
                      at2 + ": port " + Str(s.box->offset_) + " " + Str(s.box->size_) + " game " + rbx->Dump());
            if (const Value* com = v.Find("com")) {
                const b2Vec2 c = body->Body()->GetLocalCenter();
                t.Add("Rigidbody2D mass, centre of mass", Same({c.x, c.y}, V2(*com)) && Same(body->Body()->GetMass(), Bits(v["mass"])),
                      at2 + ": port " + Str({c.x, c.y}) + " mass " + std::to_string(body->Body()->GetMass()) + " game " + Str(V2(*com)) + " mass " +
                          std::to_string(Bits(v["mass"])));
            }
            if (const Value* list = v.Find("receivers")) ReceiverRows(t, s, *list, "hurtboxes (box, active, layer, position)", at2);
            if (gen::HermitSlimeBehaviourNEW* hs = Hermit(s))
                if (be.Find("patrol"))
                    t.Add("HermitSlime (patrolLapse, destinyPosition, grid, look lock)",
                          Same(hs->patrolLapse, Bits(be["patrol"])) && Same(XY(hs->destinyPosition), V2(be["dest"])) && Same(hs->roomGridOrigin, V2(be["gridOrigin"])) &&
                              Same(hs->roomGridSize, V2(be["gridSize"])) && hs->iDirectionLocked == be["locked"].b,
                          at2 + ": port lapse " + std::to_string(hs->patrolLapse) + " dest " + Str(XY(hs->destinyPosition)) + " game " +
                              std::to_string(Bits(be["patrol"])) + " " + Str(V2(be["dest"])));
            UnityEngine::Material* mat = MaterialOf(s);
            const float flash = mat->Float("_FlashAmount");
            const UnityEngine::Color c = mat->ColorOf("_Color");
            const float col[4] = {c.r, c.g, c.b, c.a};
            bool color = Same(flash, Bits(v["flash"]));
            for (int k = 0; k < 4; k++) color = color && Same(col[k], Bits(v["color"][static_cast<size_t>(k)]));
            const std::string colours = at2 + ": port flash " + std::to_string(flash) + " colour " + Str({c.r, c.g}) + " game " + std::to_string(Bits(v["flash"])) +
                                        " " + Str({Bits(v["color"][0]), Bits(v["color"][1])});
            if (!es->_isAlive && !f.Find("dotweenUdt")) {
                bool near = std::fabs(flash - Bits(v["flash"])) <= 0.03f;
                for (int k = 0; k < 4; k++) near = near && std::fabs(col[k] - Bits(v["color"][static_cast<size_t>(k)])) <= 0.03f;
                t.Add("material in the death fade (DOTween clock unrecorded: within 0.03)", near, colours);
                t.Info("material in the death fade, bit for bit (needs the DOTween clock)", color, colours);
            } else {
                t.Add("material (_FlashAmount, _Color)", color, colours);
            }
            if (t.debug) std::printf("frame %lld enemy %d: %s %s | game %s\n", fn, id, Str(Position(s)).c_str(), SpriteOf(s).c_str(), Str(V2(v["pos"])).c_str());
        }
        hooks->AtFrameEnd(fn, f, t);
        // An enemy the game destroyed is let go of (the game's references to it are the game's
        // business now): its objects are collectable.
        for (Replayed& s : enemies)
            if (s.go && s.go->destroyed_) {
                released.push_back({s.go, s.go->GetHashCode(), s.id});
                const int id = s.id;
                s = Replayed{};
                s.id = id;
            }
        // ML_GC_EVERY_FRAME=1: the collector at every frame boundary (docs/translator_plan.md,
        // "Memory"), where no translated code is running: what the roots don't reach is freed, so
        // a live object the roots miss shows up as a crash or a mismatch in the frames after.
        if (gcEveryFrame) {
            Scope p(replay_profile::Collector);
            gcFreed += cs::CurrentHeap().Collect();
            gcRuns++;
        }
        replay_profile::EndFrame();
    }
    hooks->AfterFrames(otherLines, t);
    std::printf("\n");
    t.Print();
    if (gcEveryFrame) PrintCollected(gcRuns, gcFreed, released);
    std::printf("Random drawn by the game's other scripts (resynced) %d times\n", rngElsewhere);
    if (roomExit.inferred) std::printf("the room's DisableEnemies placed from the enemies' rows (no event recorded) %d times\n", roomExit.inferred);
    if (unseenEnters)
        std::printf("attack triggers' OnTriggerEnter2D this recording can't show (no \"attackEnter\": its oracle hooked "
                    "EnemyBehaviour.AttackHit only, which an override like HermitSlimeBehaviourNEW's never calls): %d in the port\n",
                    unseenEnters);
    if (animResynced) std::printf("Random resynced before an animation event %d times\n", animResynced);
    replay_profile::Print("translated");
    std::printf("\n%s\n", t.Ok() ? "ALL MATCH" : "MISMATCHES");
    hooks->Summary();
    gHooks = nullptr;
    return t.Ok() ? 0 : 1;
}

} // namespace

#ifndef ML_TEST_MAIN
#define ML_TEST_MAIN main
#endif
int ML_TEST_MAIN(int argc, char** argv)
{
    const std::string file = argc > 1 ? argv[1] : "data/oracle/slime.jsonl";
    Data data;
    data.name = argc > 2 ? argv[2] : "BabySlime";
    replay_profile::FromEnv();
    try {
        if (!LoadData(data)) {
            std::printf("no extracted data for %s (ML_DATA, default data/extracted): can't build the enemies\n", data.name.c_str());
            return 1;
        }
        return Replay(file, data);
    } catch (const cs::Thrown& e) {
        std::printf("the translated code threw %s\n", e.TypeName());
        return 1;
    }
}
