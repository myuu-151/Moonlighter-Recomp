// The BabySlime (game/Enemy, game/BabySlime, game/EnemyStats, unity/DelayedCalls):
//  1. self-checks that need no recording: UnityEngine.Random's xorshift128 against the
//     game's values for seed 0 (data/oracle/rng.jsonl's first draws), insideUnitCircle's
//     draws, the delayed calls' order, and with the game's data (ML_DATA, default
//     data/extracted) the stats from enemy_stats.json and a slime in a box: it waits its
//     second, chases a standing hero, touches him (his DealDamageToPlayer), takes a hit (the
//     red / white flash, invincible for 0.09 s, still), and dies (TransparentFX, the
//     Animator off, the dead sprite and the destroy 0.6 s later);
//  2. with the oracle's recording (data/oracle/slime.jsonl, `-oracle-slime`; skipped if
//     there's none): the room's slimes from their recorded start, every later frame replayed
//     from the recording's clocks (deltaTime, Time.time, each fixed step's Time.fixedTime),
//     the hero's body as each fixed step found it, where he was when each slime's Update read
//     him, the weapon's hits (DealDamageToEnemy, PushEnemyBackwards) where they landed, the
//     Updates and the agents' LateUpdates in the game's order, and the Random state each
//     Update started from (the game's other scripts draw between them: counted, then
//     resynced). Compared per frame and slime, bit for bit: position (world and local), its
//     Rigidbody2D (position, velocity, awake), the agent (velocity, goal, path, pending,
//     acceleration), the Animator (state, normalized time, sprite, on), health, the
//     behaviour's fields, the material (flash, colour), alive and destroyed; the attack
//     trigger's hits on the hero; the Random draws of the slimes' own Updates; each fixed
//     step's start; insideUnitCircle against the game's ("rngProbe").
//
// Usage: slime_test [slime.jsonl]   (ML_SLIME_DEBUG=1: every frame's slimes)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <Box2D/Box2D.h>

#include "game/BabySlime.h"
#include "game/Constants.h"
#include "game/Enemy.h"
#include "game/EnemyStats.h"
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

using json::Value;
using unity::Vector2;

namespace {

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

// ---- 1. Self-checks ---------------------------------------------------------------------

void CheckRandom()
{
    // InitState(0) and its first value()s, as the game drew them (rng.jsonl: seed 0, "value").
    unity::Random::InitState(0);
    const unity::Random::State s = unity::Random::GetState();
    Check(s.s0 == 0 && s.s1 == 1 && s.s2 == 1812433254u && s.s3 == 1900727103u, "Random.InitState(0)");
    const uint32_t want[4] = {1058376237u, 1058375277u, 1059877249u, 1061435854u};
    bool ok = true;
    for (uint32_t w : want) ok &= U(unity::Random::value()) == w;
    Check(ok, "Random.value after InitState(0): the game's first four");

    // insideUnitCircle: two draws, inside the circle.
    unity::Random::InitState(12345);
    bool inside = true;
    for (int i = 0; i < 1000; i++) {
        const unity::Random::State before = unity::Random::GetState();
        const Vector2 p = unity::Random::insideUnitCircle();
        inside &= p.x * p.x + p.y * p.y <= 1.0001f;
        inside &= unity::Random::DrawsBetween(before, unity::Random::GetState(), 4) == 2;
    }
    Check(inside, "Random.insideUnitCircle: two draws, inside the unit circle");
}

void CheckDelayedCalls()
{
    unity::DelayedCalls calls;
    std::string log;
    int owner = 0, other = 0;
    calls.now = 1.0;
    calls.Invoke(&owner, "B", 0.5f, [&] { log += "B"; });
    calls.Invoke(&owner, "A", 0.25f, [&] { log += "A"; });
    calls.Invoke(&other, "B", 0.5f, [&] { log += "b"; });
    calls.Wait(&owner, 0.5f, [&] { log += "w"; });
    calls.Invoke(&owner, "C", 0.1f, [&] {
        log += "C";
        // Made during the pass and due at once: waits for the next pass.
        calls.Invoke(&owner, "D", 0.0f, [&] { log += "D"; });
    });
    calls.CancelInvoke(&owner, "B"); // only owner's B; the wait stays
    calls.Update(1.2);
    Check(log == "C", "delayed calls: only what's due (" + log + ")");
    calls.Update(1.5);
    Check(log == "CDAbw", "delayed calls: earliest first, ties in order, a call made in a pass waits, CancelInvoke by owner and name (" +
                              log + ")");
    calls.now = 2.0;
    calls.Invoke(&owner, "R", 0.1f, [&] { log += "r"; }, 0.1f);
    calls.Update(2.35);
    Check(log == "CDAbwr", "InvokeRepeating: once per pass (" + log + ")");
    calls.Update(2.36);
    Check(log == "CDAbwrr", "InvokeRepeating: the next period");
    calls.Forget(&owner);
    calls.Update(10.0);
    Check(calls.Empty() && log == "CDAbwrr", "Forget: the owner's calls go");
}

struct Data {
    std::string dir;
    Value enemyStats, physics2d;
    std::unique_ptr<unity::AnimatorController> controller;
    const game::Prefab* prefab = nullptr;
    bool ok = false;
};

Data LoadData()
{
    Data d;
    d.dir = std::getenv("ML_DATA") ? std::getenv("ML_DATA") : "data/extracted";
    const std::string constants = ReadFile(d.dir + "/constants.json"), stats = ReadFile(d.dir + "/enemy_stats.json"),
                      prefabs = ReadFile(d.dir + "/prefabs.json"), anim = ReadFile(d.dir + "/animators/BabySlime.json"),
                      physics = ReadFile(d.dir + "/physics2d.json");
    if (constants.empty() || stats.empty() || prefabs.empty() || anim.empty()) return d;
    game::Constants::LoadFromText(constants);
    d.enemyStats = json::Parse(stats);
    if (!physics.empty()) d.physics2d = json::Parse(physics);
    game::PrefabLibrary::Load(json::Parse(prefabs));
    d.prefab = game::PrefabLibrary::Find("BabySlime", "enemies_golem");
    d.controller = std::make_unique<unity::AnimatorController>(unity::AnimatorController::FromJson(json::Parse(anim)));
    d.ok = d.prefab != nullptr;
    return d;
}

void CheckStats(const Data& data)
{
    game::EnemyStatModificator m;
    Check(game::EnemyStatModificator::FromJson(data.enemyStats, "BabySlime", m), "enemy_stats.json has BabySlime");
    Check(m.isModifiedByFloor && m.maxHealth == 5.0f && m.strength == 10.0f && m.speed == 50.0f && m.weight == 6.0f && m.culture == 1,
          "BabySlime's base stats (5 health, 10 strength, 50 speed, 6 weight, Golem)");
    game::EnemyStats s;
    game::EnemyStatsContext ctx; // the first floor of the Golem dungeon, no difficulty option
    s.Init(m, ctx);
    Check(s.maxHealth() == 5.0f && s.strength() == 10.0f && s.CurrentHealth() == 5.0f && s.isAlive(), "Init on floor 1: as the file has them");
    ctx.difficulty = 1; // difficultyMod[1] = 1.25
    s.Init(m, ctx);
    Check(s.maxHealth() == 6.25f && s.strength() == 12.5f, "Init with the difficulty option 1: x1.25");
    // Floor 2 (level 1): health + 0.7 * health; strength + 0.7 * max(0, 1 - 0.5) * strength.
    ctx = {};
    ctx.level = 1;
    s.Init(m, ctx);
    Check(s.maxHealth() == 5.0f + 0.7f * 5.0f && s.strength() == 10.0f + (0.7f * 0.5f) * 10.0f, "Init on floor 2: the leveling");
    // A Golem enemy in the Forest dungeon: the autoscale tables (2.85, 2.5).
    ctx = {};
    ctx.dungeonCulture = 2;
    s.Init(m, ctx);
    Check(s.maxHealth() == 5.0f * 2.85f && s.strength() == 10.0f * 2.5f, "Init in the Forest: the autoscale tables");
    // The health's setter: invincible, decreased, death.
    s.Init(m, game::EnemyStatsContext{});
    int decreased = 0, died = 0;
    s.OnHealthDecreased = [&](float) { decreased++; };
    s.OnDeath = [&] { died++; };
    s.SetInvencible(true);
    s.SetCurrentHealth(1.0f);
    Check(s.CurrentHealth() == 5.0f && decreased == 0, "invincible: the setter does nothing");
    s.SetInvencible(false);
    s.SetCurrentHealth(-3.0f);
    Check(s.CurrentHealth() == 0.0f && !s.isAlive() && decreased == 1 && died == 1, "below zero: 0, dead, once");
    s.SetCurrentHealth(2.0f);
    Check(s.CurrentHealth() == 0.0f, "dead: the setter does nothing");
}

// A slime in an empty box with the hero standing at the origin.
void CheckSlimeInABox(const Data& data)
{
    game::PolyNav2D nav;
    nav.inflateRadius = 8.0f;
    nav.map = game::PolyNav2D::PolyMap({{{{-200.0f, 120.0f}, {-200.0f, -120.0f}, {200.0f, -120.0f}, {200.0f, 120.0f}}}}, {});
    nav.GenerateNodes();
    game::PolyNav2D::current = &nav;

    unity::Physics2DSettings settings;
    if (!data.physics2d.IsNull()) settings.Load(data.physics2d);
    unity::PhysicsWorld world(settings);
    game::EnemyPhysics physics(world);
    unity::Rigidbody2DDef heroDef;
    heroDef.mass = 1.0f;
    heroDef.linearDrag = 5.0f;
    heroDef.gravityScale = 0.0f;
    heroDef.constraints = unity::Rigidbody2DDef::FreezeRotation;
    heroDef.collisionDetection = unity::Rigidbody2DDef::Continuous;
    unity::Rigidbody2D& heroBody = world.AddBody(heroDef, {0.0f, 0.0f}, 0, {1.0f, -3.5f}, {13.0f, 13.0f});
    physics.SetHero(&heroBody);

    unity::DelayedCalls calls;
    unity::Tweens tweens;
    game::EnemyHero hero;
    int hits = 0;
    float hitStrength = 0.0f;
    hero.dealDamage = [&](float strength, const std::string& source, game::Enemy&) {
        hits++;
        hitStrength = strength;
        Check(source == "BabySlime", "the damage's source is the enemy's name");
    };
    game::EnemyScene scene;
    scene.calls = &calls;
    scene.tweens = &tweens;
    scene.hero = &hero;
    int deadSprites = 0;
    scene.spawnEnemyDeadSprite = [&](const std::string& sprite, Vector2) { deadSprites += sprite == "babyslime_dead"; };

    auto slime = game::MakeEnemy(*data.prefab, scene, data.controller.get(), {{80.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}});
    auto* behaviour = dynamic_cast<game::BabySlimeBehaviour*>(slime->enemyBehaviour.get());
    Check(behaviour != nullptr, "the BabySlime prefab makes a BabySlimeBehaviour");
    Check(slime->hasAttackCollider && slime->attackSize.x == 8.0f && slime->colliderSize.x == 6.0f && slime->attackLayer == game::kLayerEnemyAttack &&
              slime->layer == game::kLayerGroundEnemy,
          "its colliders: a 6x6 box on GroundEnemy, an 8x8 trigger on EnemyAttack");
    game::EnemyStatModificator m;
    game::EnemyStatModificator::FromJson(data.enemyStats, "BabySlime", m);
    slime->enemyDeadSprite = "babyslime_dead";
    slime->Init("BabySlime", m, game::EnemyStatsContext{});
    Check(!slime->enemyBehaviour->fallInHoles && slime->navAgent.maxSpeed == 50.0f && slime->rigidbody.mass == 6.0f,
          "Init: no holes for it, the agent's speed and the body's mass from the stats");
    Check(U(slime->enemyBehaviour->playerOffset.x) == 0x80000000u && slime->enemyBehaviour->playerOffset.y == -3.5f, "playerOffset (-0, -3.5)");
    physics.Add(*slime);
    slime->enemyBehaviour->StartTargetingPlayerAfterTime(1.0f);

    const float dt = 1.0f / 60.0f;
    double time = 0.0, fixedTime = 0.0;
    int targetFrame = -1, firstHitFrame = -1;
    float closest = 1e9f;
    auto frame = [&](int f) {
        time += static_cast<double>(dt);
        nav.BeginFrame();
        while (fixedTime + 0.01 <= time) {
            fixedTime += 0.01;
            calls.now = fixedTime;
            world.SyncTransform(heroBody, {0.0f, 0.0f});
            heroBody.SetVelocity({0.0f, 0.0f});
            physics.Step(0.01f);
        }
        calls.now = time;
        tweens.Update(dt);
        slime->enemyBehaviour->Update(dt);
        calls.Update(time);
        nav.ResumeCoroutines();
        slime->UpdateAnimator(dt);
        nav.LateUpdate();
        if (!slime->destroyed && slime->active) slime->navAgent.LateUpdate(dt);
        if (targetFrame < 0 && slime->enemyBehaviour->targetPlayer) targetFrame = f;
        if (firstHitFrame < 0 && hits > 0) firstHitFrame = f;
        closest = std::min(closest, unity::Distance(slime->position(), {0.0f, 0.0f}));
    };
    int f = 0;
    for (; f < 300; f++) frame(f);
    Check(targetFrame >= 59 && targetFrame <= 61, "targets the hero a second after (frame " + std::to_string(targetFrame) + ")");
    Check(firstHitFrame > targetFrame && hits >= 1 && hitStrength == 10.0f,
          "chases and touches him: DealDamageToPlayer(10) (first at frame " + std::to_string(firstHitFrame) + ", " + std::to_string(hits) + " in all)");
    Check(closest < 14.0f, "it got to him (" + std::to_string(closest) + ")");
    Check(slime->sprite.rfind("babyslime_walk_", 0) == 0, "walks in place: " + slime->sprite);

    // A hit of 3: 2 left, red, then white at 0.02 s, red at 0.07, the default at 0.09; invincible
    // and still meanwhile.
    float dealt = slime->DealDamageToEnemy(3.0f);
    Check(dealt == 3.0f && slime->enemyStats.CurrentHealth() == 2.0f, "a hit of 3 (no defense): 2 left");
    Check(slime->flashAmount == 1.0f && slime->color[1] == 0.0f && slime->enemyStats.Invencible() && !slime->enemyBehaviour->canMove &&
              slime->enemyBehaviour->IsEnemyPlayingDamagedEffect() && !slime->navAgent.hasPath(),
          "hit: flash on, red, invincible, stopped");
    Check(slime->DealDamageToEnemy(3.0f) == 3.0f && slime->enemyStats.CurrentHealth() == 2.0f, "a second hit while invincible: nothing");
    frame(f++);
    frame(f++);
    Check(slime->color[1] == 1.0f && slime->flashAmount == 1.0f, "0.033 s later: white");
    frame(f++);
    frame(f++);
    frame(f++);
    Check(slime->color[1] == 0.0f, "0.083 s: red again");
    frame(f++);
    Check(slime->color[1] == 1.0f && slime->flashAmount == 0.0f && !slime->enemyStats.Invencible() && slime->enemyBehaviour->canMove,
          "0.1 s: the default, vulnerable, moving again");

    // The killing hit.
    const int before = hits;
    slime->DealDamageToEnemy(10.0f);
    Check(!slime->enemyStats.isAlive() && slime->layer == game::kLayerTransparentFX && !slime->animatorEnabled && !slime->enemyBehaviour->enabled,
          "dead: TransparentFX, the Animator off, the behaviour off");
    Check(!slime->inRoomEnemies && slime->navAgent.parents.size() == 2, "EnemyDefeated: out of its spawn point");
    for (int k = 0; k < 34; k++) frame(f++);
    Check(!slime->destroyed && slime->flashAmount > 0.0f && slime->color[0] < 1.0f, "0.57 s: fading");
    for (int k = 0; k < 4; k++) frame(f++);
    Check(slime->destroyed && deadSprites == 1, "0.6 s: destroyed, its dead sprite spawned");
    Check(hits == before, "no more damage once dead");
    game::PolyNav2D::current = nullptr;
}

// ---- 2. The recording -------------------------------------------------------------------

struct Slime {
    int id = 0;
    std::unique_ptr<game::Enemy> enemy;
};

struct Tally {
    std::map<std::string, std::pair<int, int>> rows; // what -> (match, differ)
    std::vector<std::string> order;
    int shown = 0;
    bool debug = false;
    void Add(const std::string& what, bool ok, const std::string& detail = "")
    {
        if (!rows.count(what)) order.push_back(what);
        auto& r = rows[what];
        (ok ? r.first : r.second)++;
        if (!ok && (debug || shown < 25)) {
            shown++;
            std::printf("  %s: %s\n", what.c_str(), detail.c_str());
        }
    }
    bool Ok() const
    {
        for (const auto& [k, v] : rows)
            if (v.second) return false;
        return true;
    }
    void Print() const
    {
        for (const std::string& k : order) {
            const auto& r = rows.at(k);
            std::printf("%-44s %6d match %6d differ\n", k.c_str(), r.first, r.second);
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

int ReplayRecording(const std::string& file, const Data& data)
{
    std::ifstream in(file);
    if (!in) {
        std::printf("(no %s: skipping the oracle comparison; record it with -oracle-slime)\n", file.c_str());
        return 0;
    }
    Value start;
    std::vector<Value> slimeLines, frames, maps, probes, colliders;
    for (std::string line; std::getline(in, line);) {
        if (line.empty()) continue;
        Value v = json::Parse(line);
        const std::string& ev = v["ev"].s;
        if (ev == "slimeStart") start = v;
        else if (ev == "slime") slimeLines.push_back(std::move(v));
        else if (ev == "frame") frames.push_back(std::move(v));
        else if (ev == "navMap") maps.push_back(std::move(v));
        else if (ev == "rngProbe") probes.push_back(std::move(v));
        else if (ev == "collider") colliders.push_back(std::move(v));
    }
    if (start.IsNull() || slimeLines.empty() || frames.empty() || maps.empty()) {
        std::printf("%s: no slimeStart, slimes, frames or map\n", file.c_str());
        return 1;
    }
    Tally t;
    t.debug = std::getenv("ML_SLIME_DEBUG") != nullptr;
    std::printf("recording: room %s, %zu slimes, %zu frames\n", start["room"].s.c_str(), slimeLines.size(), frames.size());

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
    game::EnemyPhysics::Options physicsOptions;
    physicsOptions.writeBackUnmoved = std::getenv("ML_SLIME_WRITEBACK_MOVED") == nullptr;
    game::EnemyPhysics physics(world, physicsOptions);
    physics.SetHero(&heroBody);

    // The scene.
    unity::DelayedCalls calls;
    unity::Tweens tweens;
    game::EnemyHero hero;
    std::vector<std::pair<int, float>> portDamage; // (slime, strength) this step
    std::vector<std::pair<int, Vector2>> portDeadSprites;
    std::vector<Slime> slimes;
    auto idOf = [&](const game::Enemy& e) {
        for (const Slime& s : slimes)
            if (s.enemy.get() == &e) return s.id;
        return -1;
    };
    hero.dealDamage = [&](float strength, const std::string&, game::Enemy& e) { portDamage.emplace_back(idOf(e), strength); };
    game::EnemyScene scene;
    scene.calls = &calls;
    scene.tweens = &tweens;
    scene.hero = &hero;
    scene.spawnEnemyDeadSprite = [&](const std::string&, Vector2 p) { portDeadSprites.emplace_back(-1, p); };

    // The slimes as recorded (built from their prefab, then their state set).
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
    for (const Value& v : slimeLines) {
        Slime s;
        s.id = static_cast<int>(v["s"].AsInt());
        std::vector<Vector2> parents;
        const Value& chain = v["chain"];
        for (size_t k = 1; k < chain.size(); k++) parents.push_back(V2(chain[k]));
        if (!data.prefab) {
            std::printf("no BabySlime prefab (ML_DATA): can't build the slimes\n");
            return 1;
        }
        s.enemy = game::MakeEnemy(*data.prefab, scene, data.controller.get(), parents);
        game::Enemy& e = *s.enemy;
        e.navAgent.localPosition = V2(chain[0]);
        e.enemyDeadSprite = v["deadSprite"].IsString() ? v["deadSprite"].s : "";
        if (haveBase) e.Init(start["enemy"].s, base, context);
        else e.enemyBehaviour->Init();
        // The stats Init made against the game's.
        const Value& st = v["stats"];
        t.Add("stats from enemy_stats.json (Init)",
              haveBase && Same(e.enemyStats.maxHealth(), Bits(st["maxHealth"])) && Same(e.enemyStats.strength(), Bits(st["strength"])) &&
                  Same(e.enemyStats.speed(), Bits(st["speed"])) && Same(e.enemyStats.defense(), Bits(st["defense"])) &&
                  Same(e.enemyStats.weight(), Bits(st["weight"])),
              "slime " + std::to_string(s.id) + ": maxHealth " + std::to_string(e.enemyStats.maxHealth()) + " vs " + std::to_string(Bits(st["maxHealth"])));
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
              "slime " + std::to_string(s.id));
        a.maxSpeed = Bits(ag["maxSpeed"]);
        a.velocity = V2(ag["vel"]);
        std::vector<Vector2> path;
        for (size_t k = 0; k + 1 < ag["path"].size(); k += 2) path.push_back({Bits(ag["path"][k]), Bits(ag["path"][k + 1])});
        a.RestoreState(V2(ag["goal"]), Bits(ag["acc"]), static_cast<int>(ag["req"].AsInt()), path);
        if (ag["req"].AsInt() > 0) std::printf("note: slime %d starts with a path pending (not replayable)\n", s.id);
        const Value& rb = v["rb"];
        t.Add("Rigidbody2D settings (prefab, stats)",
              Same(e.rigidbody.mass, Bits(rb["mass"])) && Same(e.rigidbody.linearDrag, Bits(rb["drag"])) &&
                  e.rigidbody.constraints == static_cast<int>(rb["constraints"].AsInt()) && e.colliderSize.x == Bits(v["box"][2]) &&
                  e.attackSize.x == Bits(v["attackBox"][2]),
              "slime " + std::to_string(s.id));
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
        t.Add("behaviour Init (playerOffset)", Same(b.playerOffset, V2(be["playerOffset"])),
              Str(b.playerOffset) + " vs " + Str(V2(be["playerOffset"])));
        // The Animator and the sprite.
        const Value& an = v["anim"];
        const int state = data.controller->StateByHash(static_cast<int32_t>(an["state"].AsInt()));
        if (state >= 0) e.enemyAnimator->Play(data.controller->states[static_cast<size_t>(state)].name, Bits(an["t"]));
        e.enemyAnimator->speed = Bits(an["speed"]);
        e.animatorEnabled = an["on"].b;
        e.sprite = an["sprite"].IsString() ? an["sprite"].s : "";
        e.flashAmount = Bits(v["flash"]);
        for (int k = 0; k < 4; k++) e.color[k] = Bits(v["color"][static_cast<size_t>(k)]);
        e.navAgent.OnDisable(); // (re-enabled below in allAgents' order)
        slimes.push_back(std::move(s));
    }
    for (const Value& name : start["agents"].arr) {
        bool found = false;
        for (Slime& s : slimes)
            if (name.IsInt() && s.id == name.AsInt()) {
                s.enemy->navAgent.OnEnable();
                found = true;
            }
        if (!found) std::printf("note: PolyNavAgent.allAgents has %s, not one of the slimes\n", name.Dump().c_str());
    }
    for (Slime& s : slimes) {
        physics.Add(*s.enemy);
        const Value* v = nullptr;
        for (const Value& l : slimeLines)
            if (l["s"].AsInt() == s.id) v = &l;
        physics.PlaceBody(*s.enemy, V2((*v)["rb"]["pos"]));
        s.enemy->body->SetVelocity({Bits((*v)["rb"]["vel"][0]), Bits((*v)["rb"]["vel"][1])});
        if (!(*v)["rb"]["awake"].b) s.enemy->body->Body()->SetAwake(false);
    }
    auto slime = [&](int id) -> game::Enemy* {
        for (Slime& s : slimes)
            if (s.id == id) return s.enemy.get();
        return nullptr;
    };
    // The targeting a second after EnableEnemies, if it hasn't come yet.
    if (const Value* et = start.Find("enableTime"); et && !et->IsNull()) {
        calls.now = static_cast<double>(Bits(*et));
        for (Slime& s : slimes)
            if (!s.enemy->enemyBehaviour->targetPlayer) s.enemy->enemyBehaviour->StartTargetingPlayerAfterTime(Bits(start["targetDelay"]));
    }
    unity::Random::SetState(State(f0["rng"]));

    const float fixedDt = Bits(start["fixedDeltaTime"]);
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
        const Value& events = f["events"];
        // The fixed steps.
        for (size_t k = 0; k < f["steps"].size(); k++) {
            const Value& st = f["steps"][k];
            calls.now = static_cast<double>(Bits(st["ft"]));
            const Value& hs = st["hero"];
            world.SetLayer(heroBody, static_cast<int>(hs[6].AsInt()));
            world.SyncTransform(heroBody, {Bits(hs[0]), Bits(hs[1])});
            heroBody.SetVelocity({Bits(hs[4]), Bits(hs[5])});
            for (const Value& ss : st["s"].arr) {
                game::Enemy* e = slime(static_cast<int>(ss[0].AsInt()));
                if (!e || e->destroyed) continue;
                const bool ok = Same(e->position(), {Bits(ss[1]), Bits(ss[2])}) && Same(e->body->position(), {Bits(ss[3]), Bits(ss[4])}) &&
                                Same(e->body->velocity(), {Bits(ss[5]), Bits(ss[6])});
                t.Add("fixed step's start (transform, body)", ok,
                      "frame " + std::to_string(fn) + " step " + std::to_string(k) + " slime " + std::to_string(ss[0].AsInt()) + ": port " +
                          Str(e->position()) + " body " + Str(e->body->position()) + " v " + Str(e->body->velocity()) + "; game " +
                          Str({Bits(ss[1]), Bits(ss[2])}) + " body " + Str({Bits(ss[3]), Bits(ss[4])}) + " v " + Str({Bits(ss[5]), Bits(ss[6])}));
            }
            portDamage.clear();
            physics.Step(fixedDt);
            // The weapon's hits in this step, in the game's order; the slimes' own hits on him.
            std::vector<std::pair<int, float>> gameDamage;
            for (const Value& ev : events.arr) {
                if (ev["step"].AsInt() != static_cast<long long>(k)) continue;
                const std::string& kind = ev["k"].s;
                game::Enemy* e = slime(static_cast<int>(ev["s"].AsInt()));
                if (kind == "dmgToPlayer") gameDamage.emplace_back(static_cast<int>(ev["s"].AsInt()), Bits(ev["str"]));
                if (!e) continue;
                if (kind == "dmgToEnemy") {
                    hero.position = V2(ev["hero"]);
                    t.Add("health before a weapon hit", Same(e->enemyStats.CurrentHealth(), Bits(ev["hp0"])),
                          "frame " + std::to_string(fn) + " slime " + std::to_string(ev["s"].AsInt()));
                    e->DealDamageToEnemy(Bits(ev["str"]), static_cast<game::EnemyDamageReceivedType>(ev["type"].AsInt()));
                    t.Add("health after a weapon hit", Same(e->enemyStats.CurrentHealth(), Bits(ev["hp1"])),
                          "frame " + std::to_string(fn) + " slime " + std::to_string(ev["s"].AsInt()) + ": " +
                              std::to_string(e->enemyStats.CurrentHealth()) + " vs " + std::to_string(Bits(ev["hp1"])));
                } else if (kind == "push") {
                    t.Add("weapon push: the slime where the game had it", Same(e->position(), V2(ev["pos"])),
                          Str(e->position()) + " vs " + Str(V2(ev["pos"])));
                    e->PushBackwards(V2(ev["hero"]), Bits(ev["force"]));
                }
            }
            bool sameHits = portDamage.size() == gameDamage.size();
            for (size_t n = 0; sameHits && n < gameDamage.size(); n++)
                sameHits = portDamage[n].first == gameDamage[n].first && Same(portDamage[n].second, gameDamage[n].second);
            if (!portDamage.empty() || !gameDamage.empty())
                t.Add("attack trigger: DealDamageToPlayer", sameHits,
                      "frame " + std::to_string(fn) + " step " + std::to_string(k) + ": port " + std::to_string(portDamage.size()) + " game " +
                          std::to_string(gameDamage.size()));
        }
        // Update: DOTween, then the slimes in the game's order (each from the Random state and
        // the hero's position it had).
        calls.now = time;
        tweens.Update(dt);
        for (const Value& u : f["updates"].arr) {
            game::Enemy* e = slime(static_cast<int>(u["s"].AsInt()));
            if (!e) continue;
            hero.position = V2(u["hero"]);
            if (!SameState(unity::Random::GetState(), State(u["rng0"]))) {
                rngElsewhere++;
                unity::Random::SetState(State(u["rng0"]));
            }
            t.Add("Update called on an enabled slime", e->enemyBehaviour->enabled && !e->destroyed, "frame " + std::to_string(fn));
            e->enemyBehaviour->Update(dt);
            t.Add("Random draws in the slimes' Update", SameState(unity::Random::GetState(), State(u["rng1"])),
                  "frame " + std::to_string(fn) + " slime " + std::to_string(u["s"].AsInt()));
            unity::Random::SetState(State(u["rng1"]));
        }
        calls.Update(time);
        nav.ResumeCoroutines();
        for (Slime& s : slimes) s.enemy->UpdateAnimator(dt);
        nav.LateUpdate();
        for (const Value& id : f["late"].arr) {
            game::Enemy* e = slime(static_cast<int>(id.AsInt()));
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

        // The frame's end, per slime.
        for (const Value& v : f["slimes"].arr) {
            const int id = static_cast<int>(v["s"].AsInt());
            game::Enemy* e = slime(id);
            if (!e) continue;
            const std::string at = "frame " + std::to_string(fn) + " slime " + std::to_string(id);
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
                  at + ": port " + Str(e->body->position()) + " v " + Str(e->body->velocity()) + " game " + Str({Bits(rb[0]), Bits(rb[1])}) + " v " +
                      Str({Bits(rb[2]), Bits(rb[3])}));
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
            const unity::AnimatorStateInfo info = e->enemyAnimator->GetCurrentAnimatorStateInfo();
            // (A disabled Animator's state info isn't the game's to keep: only that it's off.)
            t.Add("Animator state, normalized time (bits)",
                  e->animatorEnabled == an["on"].b && (!an["on"].b || (info.shortNameHash == static_cast<int32_t>(an["state"].AsInt()) &&
                                                                        Same(info.normalizedTime, Bits(an["t"])))),
                  at + ": port t " + std::to_string(info.normalizedTime) + " game " + std::to_string(Bits(an["t"])));
            t.Add("sprite", an["sprite"].IsString() && e->sprite == an["sprite"].s, at + ": port " + e->sprite + " game " + an["sprite"].Dump());
            const game::EnemyBehaviour& b = *e->enemyBehaviour;
            t.Add("health, alive, invincible", Same(e->enemyStats.CurrentHealth(), Bits(v["hp"])) && e->enemyStats.isAlive() == v["alive"].b &&
                                                   e->enemyStats.Invencible() == v["inv"].b,
                  at);
            t.Add("behaviour (state, targeting, canMove, lapse, damaged, enabled, layer)",
                  static_cast<int>(b.enemyState) == be["es"].AsInt() && b.targetPlayer == be["target"].b && b.canMove == be["canMove"].b &&
                      Same(b._lapse, Bits(be["lapse"])) && b.playingDamagedEffect == be["dmgFx"].b && b.enabled == be["enabled"].b &&
                      e->layer == v["layer"].AsInt(),
                  at + ": port lapse " + std::to_string(b._lapse) + " target " + std::to_string(b.targetPlayer) + " game " +
                      std::to_string(Bits(be["lapse"])) + " " + std::to_string(be["target"].b));
            bool color = Same(e->flashAmount, Bits(v["flash"]));
            for (int k = 0; k < 4; k++) color = color && Same(e->color[k], Bits(v["color"][static_cast<size_t>(k)]));
            t.Add("material (_FlashAmount, _Color)", color, at + ": port flash " + std::to_string(e->flashAmount) + " game " + std::to_string(Bits(v["flash"])));
            if (t.debug)
                std::printf("frame %lld slime %d: %s %s | game %s\n", fn, id, Str(e->position()).c_str(), e->sprite.c_str(), Str(V2(v["pos"])).c_str());
        }
    }
    game::PolyNav2D::current = nullptr;
    std::printf("\n");
    t.Print();
    std::printf("Random drawn by the game's other scripts (resynced) %d times\n", rngElsewhere);
    std::printf("\n%s\n", t.Ok() ? "ALL MATCH" : "MISMATCHES");
    return t.Ok() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string file = argc > 1 ? argv[1] : "data/oracle/slime.jsonl";
    CheckRandom();
    CheckDelayedCalls();
    Data data = LoadData();
    if (data.ok) {
        CheckStats(data);
        CheckSlimeInABox(data);
    } else {
        std::printf("(no game data in %s (ML_DATA): skipping the stats and the slime in a box)\n", data.dir.c_str());
    }
    std::printf("self-checks: %d passed, %d failed\n", gPassed, gFailed);
    const int replay = ReplayRecording(file, data);
    return gFailed || replay ? 1 : 0;
}
