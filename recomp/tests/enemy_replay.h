#pragma once
// The replay of an enemy recording (`-oracle-slime`: slime.jsonl; `-oracle-enemy=<prefab>`:
// enemy_<prefab>.jsonl) against the port, shared by slime_test and enemy_test (one test
// program each: what's here has internal linkage).
//
// The room's enemies of the recording's prefab from their recorded start, every later frame
// replayed from the recording's clocks (deltaTime, Time.time, each fixed step's Time.fixedTime,
// as a double run on from the first: the floats are too coarse for what's due in which step;
// DOTween's unscaled delta where recorded), the steps between EnableEnemies and the start
// (towards the bodies' sleep), the hero's body as each fixed step found it (layer, transform,
// velocity, awake), where he was when each enemy's Update read him, the weapon's hits
// (DealDamageToEnemy, PushEnemyBackwards) where they landed (and its hitbox's new fixture the
// step before: PhysicsWorld::FixtureMadeElsewhere), the Updates and the agents' LateUpdates in
// the game's order, and the Random state each Update started from (the game's other scripts
// draw between them: counted, then resynced). Compared per frame and enemy, bit for bit:
// position (world and local), its Rigidbody2D (position, velocity, awake), the agent
// (velocity, goal, path, pending, acceleration), the Animator (state, normalized time, sprite,
// on), health, the behaviour's fields, the material (flash, colour), alive and destroyed; the
// attack trigger's hits on the hero; the Random draws of the enemies' own Updates; each fixed
// step's start; insideUnitCircle against the game's ("rngProbe"). Where the recording has them
// (`-oracle-enemy`): the attack trigger (enabled, size, offset, local position) at each fixed
// step's start and at the frame's end, the look lock (GolemSoldierBehaviour), and the Animator's
// clip events per enemy in order (the methods the oracle saw called from no script: the
// Animator's SendMessage), each with the Random state before (resynced to) and after (compared).
// Two rows inform without failing: the order of two hits in one step (Box2D's contact order,
// from proxy ids Unity's whole history set), and the death fade bit for bit when the recording
// lacks DOTween's clock (then it's held to 0.03, about a frame).

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

#include "game/Constants.h"
#include "game/Enemy.h"
#include "game/EnemyFactory.h"
#include "game/EnemyRoom.h"
#include "game/EnemyStats.h"
#include "game/GolemSoldier.h"
#include "game/HermitSlime.h"
#include "game/HeroDamage.h"
#include "game/MasterAudio.h"
#include "game/PolyNav.h"
#include "game/Prefabs.h"
#include "unity/Animator.h"
#include "unity/DelayedCalls.h"
#include "unity/Mathf.h"
#include "unity/Physics2D.h"
#include "unity/PhysicsWorld.h"
#include "unity/Random.h"
#include "unity/Tween.h"
#include "util/Json.h"

namespace {

using json::Value;
using unity::Vector2;

int gFailed = 0, gPassed = 0;

void Check(bool ok, const std::string& what)
{
    if (ok) gPassed++;
    else {
        gFailed++;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

std::string ReadFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

float Bits(const Value& v)
{
    int32_t i = static_cast<int32_t>(v.AsInt());
    float f;
    std::memcpy(&f, &i, 4);
    return f;
}

uint32_t U(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

bool Same(float a, float b) { return U(a) == U(b); }
bool Same(Vector2 a, Vector2 b) { return Same(a.x, b.x) && Same(a.y, b.y); }

Vector2 V2(const Value& v) { return {Bits(v[0]), Bits(v[1])}; }

unity::Random::State State(const Value& v)
{
    return {static_cast<uint32_t>(v[0].AsInt()), static_cast<uint32_t>(v[1].AsInt()), static_cast<uint32_t>(v[2].AsInt()),
            static_cast<uint32_t>(v[3].AsInt())};
}

bool SameState(const unity::Random::State& a, const unity::Random::State& b) { return a.s0 == b.s0 && a.s1 == b.s1 && a.s2 == b.s2 && a.s3 == b.s3; }

std::string Str(Vector2 v)
{
    char buf[96];
    std::snprintf(buf, sizeof buf, "(%.9g, %.9g)", v.x, v.y);
    return buf;
}

// ---- The recording ----------------------------------------------------------------------

// ---- 2. The recording -------------------------------------------------------------------

struct ReplayEnemy {
    int id = 0;
    std::unique_ptr<game::Enemy> enemy;
};

struct ReplayData;
struct Tally;

// What one enemy's replay adds to the common one (tests/turret_replay.h: the Broken turret's
// shots), called at the points named; each gets the tally to add its rows to.
struct ReplayHooks {
    virtual ~ReplayHooks() = default;
    // The scene is made, before the enemies: its services (scene.projectiles, scene.queries).
    virtual void OnScene(const Value& /*start*/, game::EnemyScene& /*scene*/, game::EnemyHero& /*hero*/, unity::PhysicsWorld& /*world*/,
                         game::EnemyPhysics& /*physics*/, unity::Physics2D& /*roomColliders*/, unity::Rigidbody2D& /*heroBody*/, Tally& /*t*/) {}
    // An enemy built and set as its recorded line has it; then all of them.
    virtual void OnEnemy(game::Enemy& /*enemy*/, int /*id*/, const Value& /*line*/, Tally& /*t*/) {}
    virtual void OnEnemiesBuilt(const Value& /*start*/, Tally& /*t*/) {}
    // A frame begins (its line, before its fixed steps).
    virtual void BeginFrame(long long /*frame*/, const Value& /*frameLine*/) {}
    // A fixed step's start (the hero's body set, before the step) and end (after the delayed
    // calls' fixed pass): step k of the frame's line.
    virtual void BeforeStep(long long /*frame*/, size_t /*k*/, const Value& /*frameLine*/, Tally& /*t*/) {}
    virtual void AfterStep(long long /*frame*/, size_t /*k*/, const Value& /*frameLine*/, Tally& /*t*/) {}
    // The frame's animation pass begins (before the enemies' Animators).
    virtual void BeforeAnimators(float /*deltaTime*/) {}
    // The frame's end (after the enemies' rows).
    virtual void AtFrameEnd(long long /*frame*/, const Value& /*frameLine*/, Tally& /*t*/) {}
    // After the last frame: the recording's lines of other kinds, in order (the
    // BrokenGolemTurret's "rootMotionProbe", made after the recording).
    virtual void AfterFrames(const std::vector<Value>& /*otherLines*/, Tally& /*t*/) {}
};

// What a replay needs of the game's data.
struct ReplayData {
    Value enemyStats, physics2d;
    const game::Prefab* prefab = nullptr; // the recording's enemy's
    const unity::AnimatorController* controller = nullptr;
    std::string recordWith = "-oracle-slime"; // what makes the recording (for the message when there's none)
    std::string debugEnv = "ML_SLIME_DEBUG", contactsEnv = "ML_SLIME_CONTACTS";
    // sound_groups.json (tools/extract/sound_groups.py): MasterAudio's draws (none: no draws).
    Value soundGroups;
    ReplayHooks* hooks = nullptr; // the enemy's own part (none: the common replay alone)
};

// The hero's own triggers (his ActionAreas, his weapon's "Collider"): not in the replay's
// world, so their OnTriggerEnter2D on an enemy's attack trigger isn't compared.
bool HerosTrigger(const std::string& name)
{
    const std::string area = "ActionArea";
    return name == "Collider" || (name.size() >= area.size() && name.compare(name.size() - area.size(), area.size(), area) == 0);
}

// The attack trigger as a recording has it: [on, size x, y, offset x, y, local position x, y]
// from `row` at `at` (a fixed step's), or {on, size, offset, pos} (a frame's end).
bool SameAttack(const game::Enemy& e, bool on, Vector2 size, Vector2 offset, Vector2 position)
{
    return e.attackEnabled == on && Same(e.attackSize, size) && Same(e.attackOffset, offset) && Same(e.attackPosition, position);
}

std::string AttackStr(const game::Enemy& e)
{
    return std::string(e.attackEnabled ? "on" : "off") + " size " + Str(e.attackSize) + " offset " + Str(e.attackOffset) + " at " +
           Str(e.attackPosition);
}

struct Tally {
    std::map<std::string, std::pair<int, int>> rows; // what -> (match, differ)
    std::map<std::string, int> shownOf;
    std::vector<std::string> order;
    int shown = 0;
    bool debug = false;
    void Add(const std::string& what, bool ok, const std::string& detail = "")
    {
        if (!rows.count(what)) order.push_back(what);
        auto& r = rows[what];
        (ok ? r.first : r.second)++;
        if (!ok && (debug || (shown < 60 && shownOf[what] < 4))) {
            shown++;
            shownOf[what]++;
            std::printf("  %s: %s\n", what.c_str(), detail.c_str());
        }
    }
    // Rows that inform without failing (what the recording can't settle; their names say why).
    std::vector<std::string> info;
    void Info(const std::string& what, bool ok, const std::string& detail = "")
    {
        if (std::find(info.begin(), info.end(), what) == info.end()) info.push_back(what);
        Add(what, ok, detail);
    }
    bool Ok() const
    {
        for (const auto& [k, v] : rows)
            if (v.second && std::find(info.begin(), info.end(), k) == info.end()) return false;
        return true;
    }
    void Print() const
    {
        for (const std::string& k : order) {
            const auto& r = rows.at(k);
            const bool isInfo = std::find(info.begin(), info.end(), k) != info.end();
            std::printf("%-44s %6d match %6d differ%s\n", k.c_str(), r.first, r.second, isInfo ? "   (not failing)" : "");
        }
    }
};

unity::Collider2D ColliderFrom(const Value& v)
{
    unity::Collider2D c;
    c.path = v["path"].s;
    c.tag = v["tag"].s;
    c.layer = static_cast<int>(v["layer"].AsInt());
    c.enabled = v["enabled"].b;
    c.trigger = v["trigger"].b;
    c.position = V2(v["pos"]);
    c.scale = V2(v["scale"]);
    c.offset = V2(v["offset"]);
    if (const Value* s = v.Find("size")) c.size = V2(*s);
    c.hasWorldBox = c.enabled;
    c.worldMin = V2(v["min"]);
    c.worldMax = V2(v["max"]);
    return c;
}

// A hole region's outline back to the tiles it covers (as player_walk_test does it).
unity::Collider2D HoleFrom(const Value& v)
{
    std::vector<std::vector<Vector2>> paths;
    for (const Value& path : v["paths"].arr) {
        paths.emplace_back();
        for (size_t k = 0; k + 1 < path.arr.size(); k += 2) paths.back().push_back({Bits(path[k]), Bits(path[k + 1])});
    }
    auto inside = [&](float x, float y) {
        bool in = false;
        for (const auto& poly : paths)
            for (size_t a = 0, b = poly.size() - 1; a < poly.size(); b = a++)
                if ((poly[a].y > y) != (poly[b].y > y) && x < (poly[b].x - poly[a].x) * (y - poly[a].y) / (poly[b].y - poly[a].y) + poly[a].x) in = !in;
        return in;
    };
    unity::Collider2D c = ColliderFrom(v);
    c.hasWorldBox = true;
    for (int j = 0; j < 11; j++)
        for (int i = 0; i < 20; i++) {
            if (!inside(24.0f * static_cast<float>(i) - 228.0f, 24.0f * static_cast<float>(j) - 120.0f)) continue;
            float x0 = 24.0f * static_cast<float>(i) - 240.0f, x1 = x0 + 24.0f, y0 = 24.0f * static_cast<float>(j) - 132.0f, y1 = y0 + 24.0f;
            if (i == 0) x0 -= 10.0f;
            if (i == 19) x1 += 10.0f;
            if (j == 0) y0 -= 10.0f;
            if (j == 10) y1 += 10.0f;
            c.parts.emplace_back(Vector2{c.position.x + x0, c.position.y + y0}, Vector2{c.position.x + x1, c.position.y + y1});
        }
    return c;
}

void SetMap(game::PolyNav2D& nav, const Value& m)
{
    auto polys = [](const Value& list) {
        std::vector<game::PolyNav2D::Polygon> out;
        for (const Value& p : list.arr) {
            game::PolyNav2D::Polygon poly;
            for (size_t k = 0; k + 1 < p.arr.size(); k += 2) poly.points.push_back({Bits(p[k]), Bits(p[k + 1])});
            out.push_back(poly);
        }
        return out;
    };
    nav.inflateRadius = Bits(m["inflate"]);
    nav.map = game::PolyNav2D::PolyMap(polys(m["master"]), polys(m["polys"]));
    nav.GenerateNodes();
}

#include "enemy_replay_colliders.h" // (the HermitSlime's rows: ColliderReplay)
#include "enemy_replay_room.h"      // (the room's DisableEnemies: RoomExitReplay)
#include "enemy_replay_critters.h"  // (the room's critters: CritterReplay)

int ReplayRecording(const std::string& file, const ReplayData& data)
{
    std::ifstream in(file);
    if (!in) {
        std::printf("(no %s: skipping the oracle comparison; record it with %s)\n", file.c_str(), data.recordWith.c_str());
        return 0;
    }
    Value start;
    std::vector<Value> enemyLines, frames, maps, probes, colliders, otherLines;
    for (std::string line; std::getline(in, line);) {
        if (line.empty()) continue;
        Value v = json::Parse(line);
        const std::string& ev = v["ev"].s;
        if (ev == "slimeStart" || ev == "enemyStart") start = v;
        else if (ev == "slime" || ev == "enemy") enemyLines.push_back(std::move(v));
        else if (ev == "frame") frames.push_back(std::move(v));
        else if (ev == "navMap") maps.push_back(std::move(v));
        else if (ev == "rngProbe") probes.push_back(std::move(v));
        else if (ev == "collider") colliders.push_back(std::move(v));
        else otherLines.push_back(std::move(v)); // (ReplayHooks::AfterFrames)
    }
    if (start.IsNull() || enemyLines.empty() || frames.empty() || maps.empty()) {
        std::printf("%s: no start, enemies, frames or map\n", file.c_str());
        return 1;
    }
    Tally t;
    t.debug = std::getenv(data.debugEnv.c_str()) != nullptr;
    RoomExitReplay roomExit; // (tests/enemy_replay_room.h)
    roomExit.Scan(frames);
    std::printf("recording: room %s, %zu %s, %zu frames\n", start["room"].s.c_str(), enemyLines.size(), start["enemy"].s.c_str(), frames.size());

    // insideUnitCircle from the game's states.
    for (const Value& p : probes) {
        unity::Random::SetState(State(p["state"]));
        const Vector2 got = unity::Random::insideUnitCircle();
        t.Add("Random.insideUnitCircle (probes)", Same(got, V2(p["out"])) && SameState(unity::Random::GetState(), State(p["after"])),
              Str(got) + " vs " + Str(V2(p["out"])));
    }

    // The map, the world: the room's colliders (all but the dynamic bodies'), the hero's body.
    game::PolyNav2D nav;
    size_t nextMap = 0;
    for (; nextMap < maps.size() && maps[nextMap]["f"].AsInt() < 0; nextMap++) SetMap(nav, maps[nextMap]);
    game::PolyNav2D::current = &nav;
    unity::Physics2DSettings settings;
    if (!data.physics2d.IsNull()) settings.Load(data.physics2d);
    unity::PhysicsWorld world(settings);
    unity::Physics2D roomColliders;
    for (const Value& c : colliders) {
        const std::string body = c.Find("body") ? c["body"].s : "none";
        if (body == "Dynamic" || c["trigger"].b || !c["enabled"].b) continue;
        if (c["type"].s == "BoxCollider2D") roomColliders.Add(ColliderFrom(c));
        else if (c["type"].s == "PolygonCollider2D" && c["tag"].s == "Hole") roomColliders.Add(HoleFrom(c));
    }
    for (const unity::Collider2D& c : roomColliders.Colliders()) {
        std::string body = "none";
        for (const Value& v : colliders)
            if (v["path"].s == c.path && v.Find("body")) body = v["body"].s;
        world.AddCollider(c, body == "Kinematic" ? unity::RigidbodyType2D::Kinematic : unity::RigidbodyType2D::Static, body != "none");
    }
    // The room's triggers on a Kinematic or Static Rigidbody2D (a chest's "Trigger" child, on the
    // chest's body), as the room's colliders: an attack trigger enters them as it enters those
    // (enemy_FlyingKamikazeGolem.jsonl, frame 76: IronChest(Clone)/Trigger). The room's only (the
    // hero's ActionAreas are such triggers too, but they're his: HerosTrigger).
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
    // The room's triggers without a body of their own (the breakables'), after: an enemy's attack
    // trigger enters them too (enemy_GolemSoldier.jsonl: OnTriggerEnter2D on a BreakableTable).
    unity::Physics2D roomTriggers;
    for (const Value& c : colliders) {
        const std::string body = c.Find("body") ? c["body"].s : "none";
        if (body != "none" || !c["trigger"].b || !c["enabled"].b || c["type"].s != "BoxCollider2D") continue;
        roomTriggers.Add(ColliderFrom(c));
    }
    for (const unity::Collider2D& c : roomTriggers.Colliders()) world.AddCollider(c);
    // Their objects' names (an OnTriggerEnter2D's other).
    std::vector<std::string> triggerNames;
    for (const unity::Collider2D& c : roomTriggers.Colliders()) triggerNames.push_back(c.path.substr(c.path.rfind('/') + 1));
    auto RoomTrigger = [&](const std::string& name) { return std::find(triggerNames.begin(), triggerNames.end(), name) != triggerNames.end(); };
    // A hit of the port's on one of those (by its fixture's collider).
    auto RoomTriggerHit = [&](const game::EnemyPhysics::Hit& hit) {
        const unity::Collider2D* c = hit.otherFixture ? world.ColliderOf(hit.otherFixture) : nullptr;
        for (const unity::Collider2D& t : roomTriggers.Colliders())
            if (c == &t) return true;
        return false;
    };
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
    unity::Rigidbody2D& heroBody = world.AddBody(heroDef, V2(h["pos"]), static_cast<int>(h["layer"].AsInt()), V2(h["colliderOffset"]),
                                                 V2(h["colliderSize"]));
    CritterReplay critterRows; // (tests/enemy_replay_critters.h)
    critterRows.Setup(start, colliders, enemyLines, world);
    game::EnemyPhysics::Options physicsOptions;
    physicsOptions.writeBackUnmoved = std::getenv("ML_SLIME_WRITEBACK_MOVED") == nullptr;
    game::EnemyPhysics physics(world, physicsOptions);
    physics.SetHero(&heroBody);

    // The scene.
    unity::DelayedCalls calls;
    unity::Tweens tweens;
    game::EnemyHero hero;
    std::vector<std::pair<int, float>> portDamage; // (enemy, strength) this step
    std::vector<std::pair<int, Vector2>> portDeadSprites;
    std::vector<ReplayEnemy> enemies;
    auto idOf = [&](const game::Enemy& e) {
        for (const ReplayEnemy& s : enemies)
            if (s.enemy.get() == &e) return s.id;
        return -1;
    };
    ColliderReplay colliderRows;
    hero.dealDamage = [&](float strength, const std::string&, game::Enemy& e) {
        portDamage.emplace_back(idOf(e), strength);
        colliderRows.portTypes.emplace_back(idOf(e), static_cast<int>(game::HeroDamageType::Normal));
    };
    hero.dealTypedDamage = [&](float strength, const std::string&, game::HeroDamageType type, game::Enemy& e) {
        portDamage.emplace_back(idOf(e), strength);
        colliderRows.portTypes.emplace_back(idOf(e), static_cast<int>(type));
    };
    game::EnemyScene scene;
    scene.calls = &calls;
    scene.tweens = &tweens;
    scene.hero = &hero;
    ColliderReplay::Scene(scene, start);
    // MasterAudio's groups of the Golem dungeon (its creators', then the general ones').
    game::MasterAudio audio;
    if (!data.soundGroups.IsNull() && audio.Load(data.soundGroups, {"Sounds - GolemDungeon", "Sounds - GeneralDungeons"})) scene.audio = &audio;
    else std::printf("(no sound_groups.json: MasterAudio's draws not made)\n");
    scene.spawnEnemyDeadSprite = [&](const std::string&, Vector2 p) { portDeadSprites.emplace_back(-1, p); };
    if (data.hooks) data.hooks->OnScene(start, scene, hero, world, physics, roomColliders, heroBody, t);
    // The Animator's clip events: the port's per enemy, in order, against the game's (the
    // methods it saw called from no script), each from the game's Random state before it
    // (resynced) and compared after it.
    std::map<int, std::vector<std::string>> portAnim;
    std::map<int, std::vector<const Value*>> gameAnim;
    long long animFrame = -1;
    int animResynced = 0;
    scene.onAnimationEvent = [&](game::Enemy& e, const unity::AnimationEvent& ev, bool after) {
        const int id = idOf(e);
        std::vector<std::string>& mine = portAnim[id];
        if (!after) mine.push_back(ev.function);
        const size_t k = mine.size() - 1;
        const auto g = gameAnim.find(id);
        if (g == gameAnim.end() || k >= g->second.size()) return;
        const Value& game = *g->second[k];
        if (game["fn"].s != ev.function || !game.Find("rng0")) return;
        if (!after) {
            if (!SameState(unity::Random::GetState(), State(game["rng0"]))) {
                animResynced++;
                unity::Random::SetState(State(game["rng0"]));
            }
            return;
        }
        t.Add("Random draws in animation events (" + ev.function + ")", SameState(unity::Random::GetState(), State(game["rng1"])),
              "frame " + std::to_string(animFrame) + " enemy " + std::to_string(id) + ": " +
                  std::to_string(unity::Random::DrawsBetween(State(game["rng0"]), unity::Random::GetState(), 64)) + " draws, the game " +
                  std::to_string(unity::Random::DrawsBetween(State(game["rng0"]), State(game["rng1"]), 64)));
        unity::Random::SetState(State(game["rng1"]));
    };

    // The enemies as recorded (built from their prefab, then their state set).
    game::EnemyStatsContext context;
    const Value& sc = start["stats"];
    context.level = static_cast<int>(sc["level"].AsInt());
    context.afternoon = sc["afternoon"].b;
    context.difficulty = static_cast<int>(sc["difficulty"].AsInt());
    context.newGamePlus = sc["ngp"].b;
    context.dlcCompleted = sc["dlc"].b;
    context.dungeonCulture = static_cast<int>(sc["dungeonCulture"].AsInt());
    game::EnemyStatModificator base;
    const bool haveBase = !data.enemyStats.IsNull() && game::EnemyStatModificator::FromJson(data.enemyStats, start["enemy"].s, base);
    for (const Value& v : enemyLines) {
        ReplayEnemy s;
        s.id = static_cast<int>(v["s"].AsInt());
        std::vector<Vector2> parents;
        const Value& chain = v["chain"];
        for (size_t k = 1; k < chain.size(); k++) parents.push_back(V2(chain[k]));
        if (!data.prefab || !data.controller) {
            std::printf("no %s prefab or controller (ML_DATA): can't build the enemies\n", start["enemy"].s.c_str());
            return 1;
        }
        s.enemy = game::MakeEnemy(*data.prefab, scene, data.controller, parents);
        game::Enemy& e = *s.enemy;
        e.navAgent.localPosition = V2(chain[0]);
        e.enemyDeadSprite = v["deadSprite"].IsString() ? v["deadSprite"].s : "";
        // A champion (SpawnEnemies' draw, at the floor's generation): Init again as one
        // (TransformToChampion), its tint on the material.
        e.enemyStats.isChampion = v["stats"]["champion"].b;
        if (haveBase) e.Init(start["enemy"].s, base, context);
        else e.enemyBehaviour->Init();
        // The stats Init made against the game's.
        const Value& st = v["stats"];
        t.Add("stats from enemy_stats.json (Init)",
              haveBase && Same(e.enemyStats.maxHealth(), Bits(st["maxHealth"])) && Same(e.enemyStats.strength(), Bits(st["strength"])) &&
                  Same(e.enemyStats.speed(), Bits(st["speed"])) && Same(e.enemyStats.defense(), Bits(st["defense"])) &&
                  Same(e.enemyStats.weight(), Bits(st["weight"])),
              "enemy " + std::to_string(s.id) + ": maxHealth " + std::to_string(e.enemyStats.maxHealth()) + " vs " + std::to_string(Bits(st["maxHealth"])));
        game::EnemyStatModificator cur = e.enemyStats.current();
        cur.maxHealth = Bits(st["maxHealth"]);
        cur.strength = Bits(st["strength"]);
        cur.speed = Bits(st["speed"]);
        cur.defense = Bits(st["defense"]);
        cur.weight = Bits(st["weight"]);
        e.enemyStats.isChampion = st["champion"].b;
        e.enemyStats.Restore(cur, Bits(v["hp"]), v["inv"].b, v["alive"].b);
        // The prefab's settings against the game's object.
        const Value& ag = v["agent"];
        game::PolyNavAgent& a = e.navAgent;
        t.Add("agent settings (prefab)",
              Same(a.mass, Bits(ag["mass"])) && Same(a.stoppingDistance, Bits(ag["stoppingDistance"])) && Same(a.slowingDistance, Bits(ag["slowingDistance"])) &&
                  Same(a.accelerationRate, Bits(ag["accelerationRate"])) && Same(a.decelerationRate, Bits(ag["decelerationRate"])) &&
                  Same(a.avoidRadius, Bits(ag["avoidRadius"])) && Same(a.lookAheadDistance, Bits(ag["lookAheadDistance"])) &&
                  a.repath == ag["repath"].b && a.restrict == ag["restrict"].b && a.closerPointOnInvalid == ag["closerPointOnInvalid"].b,
              "enemy " + std::to_string(s.id));
        a.maxSpeed = Bits(ag["maxSpeed"]);
        a.velocity = V2(ag["vel"]);
        std::vector<Vector2> path;
        for (size_t k = 0; k + 1 < ag["path"].size(); k += 2) path.push_back({Bits(ag["path"][k]), Bits(ag["path"][k + 1])});
        a.RestoreState(V2(ag["goal"]), Bits(ag["acc"]), static_cast<int>(ag["req"].AsInt()), path);
        if (ag["req"].AsInt() > 0) std::printf("note: enemy %d starts with a path pending (not replayable)\n", s.id);
        const Value& rb = v["rb"];
        t.Add("Rigidbody2D settings (prefab, stats)",
              Same(e.rigidbody.mass, Bits(rb["mass"])) && Same(e.rigidbody.linearDrag, Bits(rb["drag"])) &&
                  e.rigidbody.constraints == static_cast<int>(rb["constraints"].AsInt()) && e.colliderSize.x == Bits(v["box"][2]) &&
                  // (An animated attack box is as the clip has it now: "attack trigger" rows.)
                  // (No AttackCollider, the turret's: "attackBox" is empty.)
                  (v.Find("attack") || (v["attackBox"].size() < 4 ? !e.hasAttackCollider : e.attackSize.x == Bits(v["attackBox"][2]))),
              "enemy " + std::to_string(s.id));
        e.rigidbody.mass = Bits(rb["mass"]);
        e.rigidbody.linearDrag = Bits(rb["drag"]);
        e.layer = static_cast<int>(v["layer"].AsInt());
        // The behaviour.
        game::EnemyBehaviour& b = *e.enemyBehaviour;
        const Value& be = v["behaviour"];
        b.enemyState = static_cast<game::EnemyBehaviour::EnemyState>(be["es"].AsInt());
        b.targetPlayer = be["target"].b;
        b.canMove = be["canMove"].b;
        b._lapse = Bits(be["lapse"]);
        b.playingDamagedEffect = be["dmgFx"].b;
        b.fallInHoles = be["fallInHoles"].b;
        b.deadForce = static_cast<int>(be["deadForce"].AsInt());
        b.enabled = be["enabled"].b;
        if (const Value* locked = be.Find("locked"))
            if (auto* soldier = dynamic_cast<game::GolemSoldierBehaviour*>(&b)) soldier->iDirectionLocked = locked->b;
        t.Add("behaviour Init (playerOffset)", Same(b.playerOffset, V2(be["playerOffset"])),
              Str(b.playerOffset) + " vs " + Str(V2(be["playerOffset"])));
        // The Animator and the sprite.
        const Value& an = v["anim"];
        const int state = data.controller->StateByHash(static_cast<int32_t>(an["state"].AsInt()));
        if (state >= 0) e.enemyAnimator->Play(data.controller->states[static_cast<size_t>(state)].name, Bits(an["t"]));
        e.enemyAnimator->speed = Bits(an["speed"]);
        e.animatorEnabled = an["on"].b;
        if (const Value* dx = an.Find("dx")) {
            e.enemyAnimator->SetFloat("DistanceX", Bits(*dx));
            e.enemyAnimator->SetFloat("DistanceY", Bits(an["dy"]));
        }
        // The attack trigger as it is (the prefab's were the Animator's defaults).
        if (const Value* ac = v.Find("attack")) {
            t.Add("attack trigger (prefab)", e.hasAttackCollider && (*ac)["layer"].AsInt() == e.attackLayer, "enemy " + std::to_string(s.id));
            e.attackEnabled = (*ac)["on"].b;
            e.attackActive = (*ac)["active"].b;
            e.attackSize = V2((*ac)["size"]);
            e.attackOffset = V2((*ac)["offset"]);
            e.attackPosition = V2((*ac)["pos"]);
        }
        ColliderReplay::Restore(t, e, v, s.id);
        e.sprite = an["sprite"].IsString() ? an["sprite"].s : "";
        e.flashAmount = Bits(v["flash"]);
        for (int k = 0; k < 4; k++) e.color[k] = Bits(v["color"][static_cast<size_t>(k)]);
        e.navAgent.OnDisable(); // (re-enabled below in allAgents' order)
        if (data.hooks) data.hooks->OnEnemy(e, s.id, v, t);
        enemies.push_back(std::move(s));
    }
    if (data.hooks) data.hooks->OnEnemiesBuilt(start, t);
    for (const Value& name : start["agents"].arr) {
        bool found = false;
        for (ReplayEnemy& s : enemies)
            if (name.IsInt() && s.id == name.AsInt()) {
                s.enemy->navAgent.OnEnable();
                found = true;
            }
        if (!found) std::printf("note: PolyNavAgent.allAgents has %s, not one of the enemies\n", name.Dump().c_str());
    }
    for (ReplayEnemy& s : enemies) {
        physics.Add(*s.enemy);
        const Value* v = nullptr;
        for (const Value& l : enemyLines)
            if (l["s"].AsInt() == s.id) v = &l;
        physics.PlaceBody(*s.enemy, V2((*v)["rb"]["pos"]));
        s.enemy->body->SetVelocity({Bits((*v)["rb"]["vel"][0]), Bits((*v)["rb"]["vel"][1])});
        if (!(*v)["rb"]["awake"].b) s.enemy->body->Body()->SetAwake(false);
    }
    auto enemyById = [&](int id) -> game::Enemy* {
        for (ReplayEnemy& s : enemies)
            if (s.id == id) return s.enemy.get();
        return nullptr;
    };
    std::vector<std::pair<int, game::Enemy*>> roomList; // (the room's DisableEnemies)
    for (ReplayEnemy& s : enemies) roomList.emplace_back(s.id, s.enemy.get());
    std::sort(roomList.begin(), roomList.end());
    unity::Random::SetState(State(f0["rng"]));

    const float fixedDt = Bits(start["fixedDeltaTime"]);
    double fixedClock = 0.0;
    // The targeting a second after EnableEnemies, if it hasn't come yet. EnableEnemies is an
    // Invoke itself: if Time.time then was a fixed step's (it ran in the pass after that step),
    // the targeting's Invoke was made on the fixed clock, which the replay's runs on from the
    // first recorded step: that step's, fixedDeltaTime (a float, 0.0099999998) back per step.
    // A second later is then two hundredths of a microsecond past the hundredth step after it,
    // and the targeting starts in the frame's pass, not in that step (slime.jsonl); one made in a
    // frame can start in a fixed step (tangle.jsonl, slime_bis.jsonl).
    if (const Value* et = start.Find("enableTime"); et && !et->IsNull()) {
        const double enabled = static_cast<double>(Bits(*et));
        calls.now = enabled;
        if (!f0["steps"].arr.empty()) {
            const double first = static_cast<double>(Bits(f0["steps"][0]["ft"]));
            for (int k = 1; k < 1000; k++) {
                const double at = first - static_cast<double>(k) * static_cast<double>(fixedDt);
                // (Invoke counts from Time.fixedTime, the last step at or before EnableEnemies:
                // unity/DelayedCalls.h.)
                if (static_cast<float>(at) <= Bits(*et)) {
                    calls.fixedNow = at;
                    if (static_cast<float>(at) != Bits(*et)) break;
                }
                if (at < enabled - 0.005) break;
                if (static_cast<float>(at) == Bits(*et)) {
                    calls.inFixedStep = true;
                    calls.fixedNow = at;
                    std::printf("EnableEnemies ran in a fixed step's pass (%d steps before the first recorded)\n", k);
                    break;
                }
            }
        }
        for (ReplayEnemy& s : enemies)
            if (!s.enemy->enemyBehaviour->targetPlayer) s.enemy->enemyBehaviour->StartTargetingPlayerAfterTime(Bits(start["targetDelay"]));
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
    // starts: those steps count towards their falling asleep (Box2D's 0.5 s at rest). The
    // steps after Time.time at EnableEnemies: back from the first recorded one, 0.01 apart.
    if (const Value* et = start.Find("enableTime"); et && !et->IsNull() && !f0["steps"].arr.empty()) {
        const double first = static_cast<double>(Bits(f0["steps"][0]["ft"])), enabled = static_cast<double>(Bits(*et));
        int before = 0;
        for (double ft = first - 0.01; ft > enabled + 0.005; ft -= 0.01) before++;
        for (int k = 0; k < before; k++) {
            setHero(f0["steps"][0]["hero"]);
            physics.Step(fixedDt);
        }
        std::printf("%d fixed steps between EnableEnemies and the recording (stepped first)\n", before);
    }
    int rngElsewhere = 0;
    for (size_t fi = 0; fi < frames.size(); fi++) {
        const Value& f = frames[fi];
        const long long fn = f["f"].AsInt();
        const float dt = Bits(f["dt"]);
        const double time = static_cast<double>(Bits(f["time"]));
        while (nextMap < maps.size() && maps[nextMap]["f"].AsInt() <= fn) {
            std::printf("note: the map was regenerated in frame %lld (applied at its start)\n", fn);
            SetMap(nav, maps[nextMap++]);
        }
        if (!SameState(unity::Random::GetState(), State(f["rng"]))) {
            rngElsewhere++;
            unity::Random::SetState(State(f["rng"]));
        }
        nav.BeginFrame();
        // Time.frameCount: the frame's from its start, its fixed steps' sounds included (a
        // variation played there isn't playing yet in the frame's Update: game/MasterAudio.h).
        audio.frameCount = f.Find("fc") ? static_cast<int>(f["fc"].AsInt()) : static_cast<int>(fn);
        if (data.hooks) data.hooks->BeginFrame(fn, f);
        roomExit.Plan(f, fi > 0 ? &frames[fi - 1] : nullptr, roomList);
        const Value& events = f["events"];
        // The hero's weapon hitbox (not in this world) came on since the last step if its first
        // touch lands in this frame's first step: its new fixture has Box2D pair every moved
        // proxy before that step (e_newFixture), the hero's with the enemies' included.
        for (const Value& ev : events.arr) {
            const std::string& kind = ev["k"].s;
            // (The hero's ActionArea triggers, which touch the enemies' too, are there all along.)
            if (ev["step"].AsInt() == 0 &&
                (kind == "push" || kind == "dmgToEnemy" || (kind == "attackHit" && !ev["player"].b && ev["other"].IsString() && ev["other"].s == "Collider"))) {
                world.FixtureMadeElsewhere();
                break;
            }
        }
        // The fixed steps.
        for (size_t k = 0; k < f["steps"].size(); k++) {
            const Value& st = f["steps"][k];
            // The fixed clock: the engine's double, a float fixedDeltaTime on per step from the
            // first recorded one (the recorded floats are 1.5e-5 coarse here, while what's due in
            // which step can turn on 2e-9: a 0.07 s Invoke against seven steps of 0.0099999998).
            fixedClock = fixedClock > 0.0 ? fixedClock + static_cast<double>(fixedDt) : static_cast<double>(Bits(st["ft"]));
            if (std::fabs(fixedClock - static_cast<double>(Bits(st["ft"]))) > 1e-4)
                std::printf("note: frame %lld step %zu: the fixed clock %.9f, the recording %.9f\n", fn, k, fixedClock, static_cast<double>(Bits(st["ft"])));
            calls.now = time;
            calls.fixedNow = fixedClock;
            calls.inFixedStep = true;
            setHero(st["hero"]);
            for (const Value& ss : st["s"].arr) {
                game::Enemy* e = enemyById(static_cast<int>(ss[0].AsInt()));
                if (!e || e->destroyed) continue;
                if (ss.size() >= 15)
                    t.Add("attack trigger at a fixed step's start (on, size, offset, position)",
                          SameAttack(*e, ss[8].b, {Bits(ss[9]), Bits(ss[10])}, {Bits(ss[11]), Bits(ss[12])}, {Bits(ss[13]), Bits(ss[14])}),
                          "frame " + std::to_string(fn) + " step " + std::to_string(k) + " enemy " + std::to_string(ss[0].AsInt()) + ": port " +
                              AttackStr(*e));
                const bool ok = Same(e->position(), {Bits(ss[1]), Bits(ss[2])}) && Same(e->body->position(), {Bits(ss[3]), Bits(ss[4])}) &&
                                Same(e->body->velocity(), {Bits(ss[5]), Bits(ss[6])});
                t.Add("fixed step's start (transform, body)", ok,
                      "frame " + std::to_string(fn) + " step " + std::to_string(k) + " enemy " + std::to_string(ss[0].AsInt()) + ": port " +
                          Str(e->position()) + " body " + Str(e->body->position()) + " v " + Str(e->body->velocity()) + "; game " +
                          Str({Bits(ss[1]), Bits(ss[2])}) + " body " + Str({Bits(ss[3]), Bits(ss[4])}) + " v " + Str({Bits(ss[5]), Bits(ss[6])}));
            }
            ColliderReplay::StepStart(t, st, enemyById, fn, k);
            if (data.hooks) data.hooks->BeforeStep(fn, k, f, t);
            portDamage.clear();
            colliderRows.portTypes.clear();
            critterRows.BeforeStep(f, k, world);
            physics.Step(fixedDt);
            if (const char* cf = std::getenv(data.contactsEnv.c_str()); cf && std::atoll(cf) == fn)
                for (ReplayEnemy& s : enemies) {
                    if (!s.enemy->body || s.enemy->destroyed) continue;
                    for (const b2ContactEdge* ce = s.enemy->body->Body()->GetContactList(); ce; ce = ce->next) {
                        const b2Fixture* a = ce->contact->GetFixtureA();
                        const b2Fixture* b = ce->contact->GetFixtureB();
                        std::printf("  step %zu enemy %d contact: layers %d/%d sensors %d/%d touching %d other body at (%g, %g)\n", k, s.id,
                                    *static_cast<const int*>(a->GetUserData()), *static_cast<const int*>(b->GetUserData()), a->IsSensor(),
                                    b->IsSensor(), ce->contact->IsTouching(), ce->other->GetPosition().x, ce->other->GetPosition().y);
                    }
                }
            // The attack triggers' OnTriggerEnter2D in this step (but the hero's own triggers'):
            // which enemy, on him or on something else. Those on the room's triggers (the
            // breakables') inform without failing: a box grazing a table's edge leaves it and
            // enters it again in Box2D where the game reports the one enter (slime.jsonl, frame
            // 365, three frames after the first), and the hero can break a table without the
            // recording saying. Those on the room's critters, where the recording lacks their
            // moves, can't be made: they fail on a row of their own, which says to record again
            // (tests/enemy_replay_critters.h).
            for (int pass = 0; pass < 3; pass++) {
                const bool triggers = pass == 1, critterPass = pass == 2;
                if (critterPass && critterRows.recorded) continue;
                std::vector<std::pair<int, bool>> mine, theirs;
                if (!critterPass)
                    for (const game::EnemyPhysics::Hit& hit : physics.LastHits())
                        if (RoomTriggerHit(hit) == triggers) mine.emplace_back(idOf(*hit.enemy), hit.player);
                for (const Value& ev : events.arr) {
                    if (ev["k"].s != "attackHit" || ev["step"].AsInt() != static_cast<long long>(k)) continue;
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
            // The weapon's hits in this step, in the game's order; the enemies' own hits on him.
            std::vector<std::pair<int, float>> gameDamage;
            for (const Value& ev : events.arr) {
                if (ev["step"].AsInt() != static_cast<long long>(k)) continue;
                const std::string& kind = ev["k"].s;
                game::Enemy* e = enemyById(static_cast<int>(ev["s"].AsInt()));
                if (kind == "dmgToPlayer") gameDamage.emplace_back(static_cast<int>(ev["s"].AsInt()), Bits(ev["str"]));
                if (!e) continue;
                if (kind == "dmgToEnemy") {
                    hero.position = V2(ev["hero"]);
                    t.Add("health before a weapon hit", Same(e->enemyStats.CurrentHealth(), Bits(ev["hp0"])),
                          "frame " + std::to_string(fn) + " enemy " + std::to_string(ev["s"].AsInt()));
                    ColliderReplay::DealDamage(*e, ev); // (through the hurtbox it names, if any)
                    t.Add("health after a weapon hit", Same(e->enemyStats.CurrentHealth(), Bits(ev["hp1"])),
                          "frame " + std::to_string(fn) + " enemy " + std::to_string(ev["s"].AsInt()) + ": " +
                              std::to_string(e->enemyStats.CurrentHealth()) + " vs " + std::to_string(Bits(ev["hp1"])));
                } else if (kind == "push") {
                    t.Add("weapon push: the enemy where the game had it", Same(e->position(), V2(ev["pos"])),
                          Str(e->position()) + " vs " + Str(V2(ev["pos"])));
                    e->PushBackwards(V2(ev["hero"]), Bits(ev["force"]));
                }
            }
            colliderRows.StepTypes(t, events, k, fn);
            if (roomExit.step == static_cast<int>(k)) RoomExitReplay::Run(roomList);
            calls.UpdateFixed(fixedClock);
            calls.inFixedStep = false;
            if (data.hooks) data.hooks->AfterStep(fn, k, f, t);
            // Which enemies hit him, how hard; then in which order. Two triggers that start in the
            // same step report in Box2D's contact-list order, which follows the broadphase's proxy
            // ids, which follow every collider Unity ever made: not modelled.
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
                if (sameHits && gameDamage.size() > 1)
                    t.Info("attack trigger: order of hits in one step (Box2D's proxy ids)", sameOrder, which);
            }
        }
        // Update: DOTween, then the enemies in the game's order (each from the Random state and
        // the hero's position it had).
        calls.now = time;
        audio.now = time;
        // DOTween: the scaled tweens by deltaTime, the independent ones by its own unscaled clock
        // ("dotweenUdt" where the recording has it; else Time.unscaledDeltaTime, close to it).
        const Value* tweenUdt = f.Find("dotweenUdt");
        tweens.Update(dt, tweenUdt ? Bits(*tweenUdt) : f.Find("udt") ? Bits(f["udt"]) : dt);
        size_t updateIndex = 0;
        for (const Value& u : f["updates"].arr) {
            if (roomExit.step == -1 && roomExit.updatesBefore == updateIndex++) RoomExitReplay::Run(roomList);
            game::Enemy* e = enemyById(static_cast<int>(u["s"].AsInt()));
            if (!e) continue;
            hero.position = V2(u["hero"]);
            if (!SameState(unity::Random::GetState(), State(u["rng0"]))) {
                rngElsewhere++;
                unity::Random::SetState(State(u["rng0"]));
            }
            t.Add("Update called on an enabled enemy", e->enemyBehaviour->enabled && !e->destroyed, "frame " + std::to_string(fn));
            e->enemyBehaviour->Update(dt);
            t.Add("Random draws in the enemies' Update", SameState(unity::Random::GetState(), State(u["rng1"])),
                  "frame " + std::to_string(fn) + " enemy " + std::to_string(u["s"].AsInt()));
            unity::Random::SetState(State(u["rng1"]));
        }
        if (roomExit.step == -1 && roomExit.updatesBefore >= f["updates"].size()) RoomExitReplay::Run(roomList);
        ColliderReplay::FrameEvents(t, events, enemyById, fn);
        calls.Update(time);
        nav.ResumeCoroutines();
        portAnim.clear();
        gameAnim.clear();
        animFrame = fn;
        for (const Value& ev : events.arr)
            if (ev["k"].s == "anim" && !(ev.Find("caller") && ev["caller"].IsString())) gameAnim[static_cast<int>(ev["s"].AsInt())].push_back(&ev);
        if (data.hooks) data.hooks->BeforeAnimators(dt);
        for (ReplayEnemy& s : enemies) s.enemy->UpdateAnimator(dt);
        for (ReplayEnemy& s : enemies) {
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
        nav.LateUpdate();
        for (const Value& id : f["late"].arr) {
            game::Enemy* e = enemyById(static_cast<int>(id.AsInt()));
            if (e && !e->destroyed && e->active) e->navAgent.LateUpdate(dt);
        }
        // Dead sprites spawned this frame (in order, where).
        std::vector<Vector2> gameDead;
        for (const Value& ev : events.arr)
            if (ev["k"].s == "deadSprite") gameDead.push_back(V2(ev["pos"]));
        if (!gameDead.empty() || !portDeadSprites.empty()) {
            bool same = gameDead.size() == portDeadSprites.size();
            for (size_t k = 0; same && k < gameDead.size(); k++) same = Same(gameDead[k], portDeadSprites[k].second);
            t.Add("dead sprites spawned (where)", same,
                  "frame " + std::to_string(fn) + ": port " + std::to_string(portDeadSprites.size()) + " game " + std::to_string(gameDead.size()));
        }
        portDeadSprites.clear();

        // The frame's end, per enemy.
        for (const Value& v : (f.Find("enemies") ? f["enemies"] : f["slimes"]).arr) {
            const int id = static_cast<int>(v["s"].AsInt());
            game::Enemy* e = enemyById(id);
            if (!e) continue;
            const std::string at = "frame " + std::to_string(fn) + " enemy " + std::to_string(id);
            t.Add("destroyed", e->destroyed == v["gone"].b, at);
            if (v["gone"].b || e->destroyed) continue;
            const Value& ag = v["agent"];
            const Value& an = v["anim"];
            const Value& be = v["behaviour"];
            bool pos = Same(e->position(), V2(v["pos"])) && Same(e->navAgent.localPosition, V2(v["local"]));
            t.Add("position (world, local)", pos, at + ": port " + Str(e->position()) + " game " + Str(V2(v["pos"])));
            const Value& rb = v["rb"];
            bool body = Same(e->body->position(), {Bits(rb[0]), Bits(rb[1])}) && Same(e->body->velocity(), {Bits(rb[2]), Bits(rb[3])}) &&
                        e->body->IsAwake() == v["awake"].b;
            t.Add("Rigidbody2D (position, velocity, awake)", body,
                  at + ": port " + Str(e->body->position()) + " v " + Str(e->body->velocity()) + (e->body->IsAwake() ? " awake" : " asleep") +
                      " game " + Str({Bits(rb[0]), Bits(rb[1])}) + " v " + Str({Bits(rb[2]), Bits(rb[3])}) + (v["awake"].b ? " awake" : " asleep"));
            std::vector<Vector2> gamePath;
            for (size_t k = 0; k + 1 < ag["path"].size(); k += 2) gamePath.push_back({Bits(ag["path"][k]), Bits(ag["path"][k + 1])});
            bool path = gamePath.size() == e->navAgent.activePath().size();
            for (size_t k = 0; path && k < gamePath.size(); k++) path = Same(gamePath[k], e->navAgent.activePath()[k]);
            t.Add("agent velocity", Same(e->navAgent.velocity, V2(ag["vel"])), at + ": port " + Str(e->navAgent.velocity) + " game " + Str(V2(ag["vel"])));
            t.Add("agent goal, path, pending, acceleration",
                  path && Same(e->navAgent.primeGoal(), V2(ag["goal"])) && e->navAgent.requests() == ag["req"].AsInt() &&
                      Same(e->navAgent.accelerationValue(), Bits(ag["acc"])),
                  at + ": port goal " + Str(e->navAgent.primeGoal()) + " path " + std::to_string(e->navAgent.activePath().size()) + " req " +
                      std::to_string(e->navAgent.requests()) + "; game " + Str(V2(ag["goal"])) + " " + std::to_string(gamePath.size()) + " " +
                      std::to_string(ag["req"].AsInt()));
            const unity::AnimatorStateInfo info = RoomExitReplay::InfoOf(*e);
            // (A disabled Animator's state info isn't the game's to keep: only that it's off.)
            t.Add("Animator state, normalized time (bits)",
                  e->animatorEnabled == an["on"].b && (!an["on"].b || (info.shortNameHash == static_cast<int32_t>(an["state"].AsInt()) &&
                                                                        Same(info.normalizedTime, Bits(an["t"])))),
                  at + ": port t " + std::to_string(info.normalizedTime) + " game " + std::to_string(Bits(an["t"])));
            t.Add("sprite", an["sprite"].IsString() && e->sprite == an["sprite"].s, at + ": port " + e->sprite + " game " + an["sprite"].Dump());
            const game::EnemyBehaviour& b = *e->enemyBehaviour;
            t.Add("health, alive, invincible", Same(e->enemyStats.CurrentHealth(), Bits(v["hp"])) && e->enemyStats.isAlive() == v["alive"].b &&
                                                   e->enemyStats.Invencible() == v["inv"].b,
                  at + ": port " + std::to_string(e->enemyStats.CurrentHealth()) + (e->enemyStats.isAlive() ? " alive" : " dead") +
                      (e->enemyStats.Invencible() ? " invincible" : "") + ", game " + std::to_string(Bits(v["hp"])) +
                      (v["alive"].b ? " alive" : " dead") + (v["inv"].b ? " invincible" : ""));
            t.Add("behaviour (state, targeting, canMove, lapse, damaged, enabled, layer)",
                  static_cast<int>(b.enemyState) == be["es"].AsInt() && b.targetPlayer == be["target"].b && b.canMove == be["canMove"].b &&
                      Same(b._lapse, Bits(be["lapse"])) && b.playingDamagedEffect == be["dmgFx"].b && b.enabled == be["enabled"].b &&
                      e->layer == v["layer"].AsInt(),
                  at + ": port lapse " + std::to_string(b._lapse) + " target " + std::to_string(b.targetPlayer) + " game " +
                      std::to_string(Bits(be["lapse"])) + " " + std::to_string(be["target"].b));
            if (const Value* ac = v.Find("attack"))
                t.Add("attack trigger (on, size, offset, position, active)",
                      SameAttack(*e, (*ac)["on"].b, V2((*ac)["size"]), V2((*ac)["offset"]), V2((*ac)["pos"])) && e->attackActive == (*ac)["active"].b,
                      at + ": port " + AttackStr(*e) + "; game " + ac->Dump());
            if (const Value* locked = be.Find("locked"))
                if (const auto* soldier = dynamic_cast<const game::GolemSoldierBehaviour*>(&b))
                    t.Add("look locked (iDirectionLocked)", soldier->iDirectionLocked == locked->b, at);
            ColliderReplay::End(t, *e, v, at);
            bool color = Same(e->flashAmount, Bits(v["flash"]));
            for (int k = 0; k < 4; k++) color = color && Same(e->color[k], Bits(v["color"][static_cast<size_t>(k)]));
            const std::string colours = at + ": port flash " + std::to_string(e->flashAmount) + " colour " + Str({e->color[0], e->color[1]}) +
                                        " game " + std::to_string(Bits(v["flash"])) + " " + Str({Bits(v["color"][0]), Bits(v["color"][1])});
            if (!e->enemyStats.isAlive() && !f.Find("dotweenUdt")) {
                // The death's fade runs on DOTween's real-time clock, which recordings before
                // "dotweenUdt" don't have (Time.unscaledDeltaTime stands in): within 0.03, about a frame of it.
                bool near = std::fabs(e->flashAmount - Bits(v["flash"])) <= 0.03f;
                for (int k = 0; k < 4; k++) near = near && std::fabs(e->color[k] - Bits(v["color"][static_cast<size_t>(k)])) <= 0.03f;
                t.Add("material in the death fade (DOTween clock unrecorded: within 0.03)", near, colours);
                t.Info("material in the death fade, bit for bit (needs the DOTween clock)", color, colours);
            } else {
                t.Add("material (_FlashAmount, _Color)", color, colours);
            }
            if (t.debug)
                std::printf("frame %lld enemy %d: %s %s | game %s\n", fn, id, Str(e->position()).c_str(), e->sprite.c_str(), Str(V2(v["pos"])).c_str());
        }
        if (data.hooks) data.hooks->AtFrameEnd(fn, f, t);
    }
    if (data.hooks) data.hooks->AfterFrames(otherLines, t);
    game::PolyNav2D::current = nullptr;
    std::printf("\n");
    t.Print();
    std::printf("Random drawn by the game's other scripts (resynced) %d times\n", rngElsewhere);
    if (roomExit.inferred) std::printf("the room's DisableEnemies placed from the enemies' rows (no event recorded) %d times\n", roomExit.inferred);
    if (animResynced) std::printf("Random resynced before an animation event %d times\n", animResynced);
    std::printf("\n%s\n", t.Ok() ? "ALL MATCH" : "MISMATCHES");
    return t.Ok() ? 0 : 1;
}

} // namespace
