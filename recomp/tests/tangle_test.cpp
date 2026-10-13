// The Tangle (game/Tangle, on game/Enemy) and Physics2D.Raycast (unity/Physics2D):
//  1. self-checks that need no recording: Raycast against boxes (the nearest, the layer mask,
//     the distance, triggers and queriesHitTriggers, a ray starting inside, a grazed edge, a zero
//     direction, the hit's fraction, point and normal); with the game's data (ML_DATA, default
//     data/extracted: constants, enemy_stats, prefabs with TangleBehaviour's fields,
//     animators/Tangle.json) the Tangle prefab made into a TangleBehaviour, its Init's two draws
//     (the same as SpawnDraws' EnemyInit from the same state), and the weave: nothing until it
//     targets the hero, nothing within a tile and a half of his feet, across the way to him
//     (amplitude * cos(2 index)) onto the agent's velocity, and nothing added when a wall is
//     within 30 along it;
//  2. with the oracle's recording (data/oracle/tangle.jsonl, `-oracle-tangle`; skipped if
//     there's none): slime_test's replay (see there: the clocks, the fixed steps, the hero's
//     body, the weapon's hits, the Updates with their Random states) for the room's Tangles,
//     with each frame's LateUpdates (the Tangles' and their agents') run in the order the game
//     ran them ("late"), and each Tangle LateUpdate checked against the game's: where it and the
//     hero were, index before and after, whether the weave ran, the amplitudes, the raycast's
//     direction and collider, the agent's velocity after. Point, normal, distance and fraction
//     of the raycast's hits inform without failing (the game never reads them). Then each frame's
//     end per Tangle, as slime_test compares it, plus the weave's fields. Also printed: the
//     script order between each Tangle's LateUpdate and its agent's.
//
// Usage: tangle_test [tangle.jsonl]   (ML_TANGLE_DEBUG=1: every mismatch)

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
#include "game/EnemyFactory.h"
#include "game/Constants.h"
#include "game/Enemy.h"
#include "game/EnemyStats.h"
#include "game/PolyNav.h"
#include "game/Prefabs.h"
#include "game/SpawnDraws.h"
#include "game/Tangle.h"
#include "unity/Animator.h"
#include "unity/DelayedCalls.h"
#include "unity/Mathf.h"
#include "unity/Physics2D.h"
#include "unity/PhysicsWorld.h"
#include "unity/Random.h"
#include "unity/Tween.h"
#include "util/Json.h"

#include "enable_clock.h"

extern int g_b2Debug; // (EXPERIMENT)
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

unity::Collider2D Box(const std::string& path, int layer, Vector2 center, Vector2 size, bool trigger = false)
{
    unity::Collider2D c;
    c.path = path;
    c.layer = layer;
    c.trigger = trigger;
    c.position = center;
    c.size = size;
    return c;
}

void CheckRaycast()
{
    unity::Physics2D q;
    q.Add(Box("wall", 11, {50.0f, 0.0f}, {10.0f, 100.0f}));       // x 45..55
    q.Add(Box("farWall", 11, {80.0f, 0.0f}, {10.0f, 100.0f}));    // x 75..85
    q.Add(Box("ground", 12, {20.0f, 0.0f}, {4.0f, 4.0f}));        // another layer, in the way
    q.Add(Box("trigger", 21, {0.0f, 40.0f}, {20.0f, 4.0f}, true)); // y 38..42
    q.Add(Box("block", 8, {0.0f, -20.0f}, {6.0f, 6.0f}));         // y -23..-17, x -3..3
    const int walls = (1 << 11) | (1 << 21) | (1 << 8);

    unity::RaycastHit2D h = q.Raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 60.0f, walls);
    Check(h.collider && h.collider->path == "wall", "Raycast: the nearest collider in the mask (not another layer's in the way)");
    Check(h && h.point.x == 45.0f && h.point.y == 0.0f && h.normal.x == -1.0f && h.normal.y == 0.0f && h.distance == 45.0f && h.fraction == 0.75f,
          "Raycast: point on the near face (45, 0), normal (-1, 0), distance 45, fraction 0.75 (" + Str(h.point) + " " +
              std::to_string(h.distance) + " " + std::to_string(h.fraction) + ")");
    Check(!q.Raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 44.9f, walls), "Raycast: nothing within the distance");
    Check(q.Raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 45.0f, walls).collider != nullptr, "Raycast: a face exactly at the distance is hit");
    Check(q.Raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 60.0f, 1 << 12).collider != nullptr && q.Raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 60.0f, 1 << 12).collider->path == "ground",
          "Raycast: the mask picks the layer");
    // The direction isn't a unit: normalized.
    h = q.Raycast({0.0f, 0.0f}, {0.0f, 37.5f}, 30.0f, walls);
    Check(!h, "Raycast: a long direction is normalized (the trigger at 38 is past 30)");
    h = q.Raycast({0.0f, 0.0f}, {0.0f, 0.001f}, 39.0f, walls);
    Check(h.collider && h.collider->path == "trigger" && h.point.y == 38.0f && h.normal.y == -1.0f, "Raycast: hits a trigger (queriesHitTriggers)");
    q.queriesHitTriggers = false;
    Check(!q.Raycast({0.0f, 0.0f}, {0.0f, 1.0f}, 39.0f, walls), "Raycast: not with queriesHitTriggers off");
    q.queriesHitTriggers = true;
    // Downward to the block: its top face.
    h = q.Raycast({0.0f, 0.0f}, {-0.0f, -50.0f}, 30.0f, walls);
    Check(h.collider && h.collider->path == "block" && h.point.y == -17.0f && h.normal.y == 1.0f && h.distance == 17.0f,
          "Raycast: (-0, -50) down onto the block's top (" + Str(h.point) + ")");
    // Starting inside: hit at once, the point the origin, the normal against the ray.
    h = q.Raycast({50.0f, 10.0f}, {1.0f, 0.0f}, 30.0f, walls);
    Check(h.collider && h.collider->path == "wall" && h.fraction == 0.0f && h.distance == 0.0f && Same(h.point, {50.0f, 10.0f}) && h.normal.x == -1.0f,
          "Raycast: from inside a collider (queriesStartInColliders): fraction 0 at the origin, normal -direction");
    q.queriesStartInColliders = false;
    h = q.Raycast({50.0f, 10.0f}, {1.0f, 0.0f}, 30.0f, walls);
    Check(h.collider && h.collider->path == "farWall", "Raycast: queriesStartInColliders off: the next one out");
    q.queriesStartInColliders = true;
    // Along an edge (x = 3, the block's right side): Box2D counts the grazed face.
    h = q.Raycast({3.0f, 0.0f}, {0.0f, -1.0f}, 30.0f, walls);
    Check(h.collider && h.collider->path == "block", "Raycast: a ray along a box's edge hits it (Box2D's numerator < 0 only)");
    Check(!q.Raycast({3.001f, 0.0f}, {0.0f, -1.0f}, 30.0f, walls), "Raycast: just outside the edge misses");
    Check(!q.Raycast({0.0f, 0.0f}, {0.0f, 0.0f}, 100.0f, walls), "Raycast: a zero direction hits nothing");
    // A compound collider (parts): the nearest part.
    unity::Collider2D compound = Box("holes", 8, {0.0f, 0.0f}, {1.0f, 1.0f});
    compound.hasWorldBox = true;
    compound.parts.push_back({Vector2{-100.0f, 60.0f}, Vector2{-90.0f, 70.0f}});
    compound.parts.push_back({Vector2{-100.0f, 20.0f}, Vector2{-90.0f, 30.0f}});
    unity::Physics2D q2;
    q2.Add(compound);
    h = q2.Raycast({-95.0f, 0.0f}, {0.0f, 1.0f}, 100.0f, 1 << 8);
    Check(h.collider && h.point.y == 20.0f, "Raycast: a compound collider's nearest part");
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
                      prefabs = ReadFile(d.dir + "/prefabs.json"), anim = ReadFile(d.dir + "/animators/Tangle.json"),
                      physics = ReadFile(d.dir + "/physics2d.json");
    if (constants.empty() || stats.empty() || prefabs.empty() || anim.empty()) return d;
    game::Constants::LoadFromText(constants);
    d.enemyStats = json::Parse(stats);
    if (!physics.empty()) d.physics2d = json::Parse(physics);
    game::PrefabLibrary::Load(json::Parse(prefabs));
    d.prefab = game::PrefabLibrary::Find("Tangle", "enemies_golem");
    d.controller = std::make_unique<unity::AnimatorController>(unity::AnimatorController::FromJson(json::Parse(anim)));
    d.ok = d.prefab != nullptr;
    return d;
}

// A Tangle and the hero in an empty room (a wall to the south for the raycast).
void CheckWeave(const Data& data)
{
    game::PolyNav2D nav;
    nav.inflateRadius = 8.0f;
    nav.map = game::PolyNav2D::PolyMap({{{{-200.0f, 120.0f}, {-200.0f, -120.0f}, {200.0f, -120.0f}, {200.0f, 120.0f}}}}, {});
    nav.GenerateNodes();
    game::PolyNav2D::current = &nav;
    unity::DelayedCalls calls;
    unity::Tweens tweens;
    unity::Physics2D queries;
    game::EnemyHero hero;
    game::EnemyScene scene;
    scene.calls = &calls;
    scene.tweens = &tweens;
    scene.hero = &hero;
    scene.queries = &queries;

    auto enemy = game::MakeEnemy(*data.prefab, scene, data.controller.get(), {{0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}});
    auto* tangle = dynamic_cast<game::TangleBehaviour*>(enemy->enemyBehaviour.get());
    Check(tangle != nullptr, "the Tangle prefab makes a TangleBehaviour");
    if (!tangle) return;
    Check(tangle->tangleCulture == game::EnemyBehaviour::CommonEnemyCulture::Golem && tangle->fallInHoles && tangle->canMove &&
              tangle->attackPushBackwardsForce == 100.0f,
          "its fields (prefabs.json): Golem, falls into holes, moves, push 100");
    Check(enemy->colliderSize.x == 10.0f && enemy->attackSize.x == 11.0f && enemy->navAgent.maxSpeed == 50.0f && enemy->navAgent.mass == 2.0f &&
              enemy->navAgent.avoidRadius == 11.0f,
          "its colliders (10, 11) and agent (maxSpeed 50, mass 2, avoidRadius 11)");
    Check(enemy->sprite == "tangle_cycle_01", "its sprite: " + enemy->sprite);
    game::EnemyStatModificator m;
    Check(game::EnemyStatModificator::FromJson(data.enemyStats, "Tangle", m), "enemy_stats.json has Tangle");

    // Init: the base's, then two flips; the same two draws as SpawnDraws' EnemyInit.
    for (int seed = 1; seed <= 8; seed++) {
        unity::Random::InitState(seed);
        game::TangleInit signs;
        game::EnemyInit("Tangle", &signs);
        const unity::Random::State afterSpawn = unity::Random::GetState();
        unity::Random::InitState(seed);
        auto e = game::MakeEnemy(*data.prefab, scene, data.controller.get(), {{0.0f, 0.0f}});
        auto* t = static_cast<game::TangleBehaviour*>(e->enemyBehaviour.get());
        e->Init("Tangle", m, game::EnemyStatsContext{});
        Check(t->originalAmplitudeX == 50.0f * static_cast<float>(signs.amplitudeXSign) && t->originalAmplitudeY == 50.0f * static_cast<float>(signs.amplitudeYSign) &&
                  SameState(unity::Random::GetState(), afterSpawn),
              "Init (seed " + std::to_string(seed) + "): the amplitudes' signs and the draws as EnemyInit has them");
    }

    unity::Random::InitState(3);
    enemy->Init("Tangle", m, game::EnemyStatsContext{});
    tangle->originalAmplitudeX = 50.0f;
    tangle->originalAmplitudeY = -50.0f;
    const float dt = 1.0f / 60.0f;
    hero.position = {100.0f, 3.5f}; // his feet (the collider's offset (1, -3.5)) at (101, 0)
    tangle->LateUpdate(dt);
    Check(!tangle->lastWeave.ran && tangle->index == 0.0f && enemy->navAgent.velocity.x == 0.0f, "not targeting: no weave");
    tangle->StartTargetingPlayer();
    hero.position = {35.0f, 3.5f}; // feet at (36, 0): a tile and a half, not more
    tangle->LateUpdate(dt);
    Check(!tangle->lastWeave.ran, "within a tile and a half of his feet (diamond distance 36): no weave");
    hero.position = {35.0f, 3.5f + 0.01f};
    tangle->LateUpdate(dt);
    Check(tangle->lastWeave.ran, "just past it: the weave");
    // He's further in x: the weave is along y, the Y amplitude.
    tangle->index = 0.0f;
    enemy->navAgent.velocity = {1.0f, 2.0f};
    hero.position = {100.0f, 3.5f};
    tangle->LateUpdate(dt);
    const float y = -50.0f * static_cast<float>(std::cos(static_cast<double>(2.0f * dt)));
    Check(tangle->amplitudeX == 0.0f && tangle->amplitudeY == -50.0f && tangle->index == dt, "further in x: amplitudes (0, -50), index += dt");
    Check(Same(tangle->lastWeave.direction, {0.0f * static_cast<float>(std::cos(static_cast<double>(2.0f * dt))), y}) && !tangle->lastWeave.hit &&
              Same(enemy->navAgent.velocity, {1.0f, 2.0f + y}),
          "the weave (0, -50 cos(2 index)) onto the agent's velocity: " + Str(enemy->navAgent.velocity));
    // Further in y: along x.
    hero.position = {0.0f, 103.5f};
    enemy->navAgent.velocity = {};
    tangle->LateUpdate(dt);
    const float x = 50.0f * static_cast<float>(std::cos(static_cast<double>(2.0f * (dt + dt))));
    Check(tangle->amplitudeX == 50.0f && tangle->amplitudeY == 0.0f && Same(enemy->navAgent.velocity.x, x), "further in y: along x, 50 cos(2 index)");
    // A wall 20 below it: the weave down (-50 cos) is cast into it and not added.
    queries.Add(Box("LowerBoundary", 11, {0.0f, -30.0f}, {400.0f, 20.0f})); // its top at y -20
    hero.position = {100.0f, 3.5f};
    tangle->index = 0.0f;
    enemy->navAgent.velocity = {};
    tangle->LateUpdate(dt);
    Check(tangle->lastWeave.ran && tangle->lastWeave.hit.collider && tangle->lastWeave.hit.collider->path == "LowerBoundary" &&
              enemy->navAgent.velocity.y == 0.0f && tangle->index == dt,
          "a wall within 30 along the weave: the ray hits it, nothing added (the index runs on)");
    // The other way (+50 originalAmplitudeY): up, away from the wall: added.
    tangle->originalAmplitudeY = 50.0f;
    tangle->LateUpdate(dt);
    Check(!tangle->lastWeave.hit && enemy->navAgent.velocity.y > 0.0f, "away from the wall: added");
    // Off when dead (OnDeath turns the behaviour off; Unity skips its LateUpdate).
    tangle->enabled = false;
    enemy->navAgent.velocity = {};
    tangle->LateUpdate(dt);
    Check(!tangle->lastWeave.ran && enemy->navAgent.velocity.y == 0.0f, "disabled: no LateUpdate");
    game::PolyNav2D::current = nullptr;
}

// ---- 2. The recording -------------------------------------------------------------------

struct TangleObj {
    int id = 0;
    std::unique_ptr<game::Enemy> enemy;
    game::TangleBehaviour* tangle = nullptr;
};

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
            std::printf("%-52s %6d match %6d differ%s\n", k.c_str(), r.first, r.second, isInfo ? "   (not failing)" : "");
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

// A hole region's outline back to the tiles it covers (as slime_test and player_walk_test do it).
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
    if (const char* file = std::getenv("ML_HOLE_POLYS")) {
        // EXPERIMENT: each line "path|x y x y ...;x y ..." (local), the collider's shapes.
        std::ifstream in(file);
        for (std::string line; std::getline(in, line);) {
            const size_t bar = line.find('|');
            if (bar == std::string::npos || line.substr(0, bar) != c.path) continue;
            std::stringstream polys(line.substr(bar + 1));
            for (std::string poly; std::getline(polys, poly, ';');) {
                std::stringstream xs(poly);
                std::vector<Vector2> pts;
                for (float x, y; xs >> x >> y;) pts.push_back({c.position.x + x, c.position.y + y});
                if (!pts.empty()) c.shapes.push_back(pts);
            }
        }
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
        std::printf("(no %s: skipping the oracle comparison; record it with -oracle-tangle)\n", file.c_str());
        return 0;
    }
    Value start;
    std::vector<Value> tangleLines, frames, maps, probes, colliders;
    for (std::string line; std::getline(in, line);) {
        if (line.empty()) continue;
        Value v = json::Parse(line);
        const std::string& ev = v["ev"].s;
        if (ev == "tangleStart") start = v;
        else if (ev == "tangle") tangleLines.push_back(std::move(v));
        else if (ev == "frame") frames.push_back(std::move(v));
        else if (ev == "navMap") maps.push_back(std::move(v));
        else if (ev == "rngProbe") probes.push_back(std::move(v));
        else if (ev == "collider") colliders.push_back(std::move(v));
    }
    if (start.IsNull() || tangleLines.empty() || frames.empty() || maps.empty()) {
        std::printf("%s: no tangleStart, tangles, frames or map\n", file.c_str());
        return 1;
    }
    if (!data.prefab) {
        std::printf("no Tangle prefab or animators/Tangle.json (ML_DATA): can't build the Tangles\n");
        return 1;
    }
    Tally t;
    t.debug = std::getenv("ML_TANGLE_DEBUG") != nullptr;
    std::printf("recording: room %s, %zu tangles, %zu frames\n", start["room"].s.c_str(), tangleLines.size(), frames.size());

    for (const Value& p : probes) {
        unity::Random::SetState(State(p["state"]));
        const Vector2 got = unity::Random::insideUnitCircle();
        t.Add("Random.insideUnitCircle (probes)", Same(got, V2(p["out"])) && SameState(unity::Random::GetState(), State(p["after"])),
              Str(got) + " vs " + Str(V2(p["out"])));
    }

    // The map, the world (slime_test's), and the queries: the raycast's colliders (everything
    // enabled on its layers, triggers too) and the holes.
    game::PolyNav2D nav;
    size_t nextMap = 0;
    for (; nextMap < maps.size() && maps[nextMap]["f"].AsInt() < 0; nextMap++) SetMap(nav, maps[nextMap]);
    game::PolyNav2D::current = &nav;
    unity::Physics2DSettings settings;
    if (!data.physics2d.IsNull()) settings.Load(data.physics2d);
    unity::PhysicsWorld world(settings);
    unity::Physics2D roomColliders, queries;
    const int weaveMask = static_cast<int>(start["weaveMask"].AsInt());
    t.Add("the weave's layer mask (LevelBoundary | EnemyBoundary | RisenObstacle)", weaveMask == game::TangleBehaviour::WeaveLayerMask(),
          std::to_string(weaveMask));
    queries.queriesHitTriggers = start["queriesHitTriggers"].b;
    queries.queriesStartInColliders = start["queriesStartInColliders"].b;
    for (const Value& c : colliders) {
        const std::string body = c.Find("body") ? c["body"].s : "none";
        const bool hole = c["type"].s == "PolygonCollider2D" && c["tag"].s == "Hole";
        if (c["enabled"].b && (weaveMask & (1 << c["layer"].AsInt())) != 0) {
            if (c["type"].s == "BoxCollider2D") queries.Add(ColliderFrom(c));
            else if (!hole) std::printf("note: a %s on the weave's layers (%s): not in the queries\n", c["type"].s.c_str(), c["path"].s.c_str());
        }
        if (hole && c["enabled"].b) queries.Add(HoleFrom(c));
        if (body == "Dynamic" || c["trigger"].b || !c["enabled"].b) continue;
        if (c["type"].s == "BoxCollider2D") roomColliders.Add(ColliderFrom(c));
        else if (hole) roomColliders.Add(HoleFrom(c));
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
    game::EnemyPhysics physics(world);
    physics.SetHero(&heroBody);
    const Vector2 heroColliderOffset = V2(h["getColliderOffset"]);

    unity::DelayedCalls calls;
    unity::Tweens tweens;
    game::EnemyHero hero;
    std::vector<std::pair<int, float>> portDamage;
    std::vector<std::pair<int, Vector2>> portDeadSprites;
    std::vector<TangleObj> tangles;
    auto idOf = [&](const game::Enemy& e) {
        for (const TangleObj& s : tangles)
            if (s.enemy.get() == &e) return s.id;
        return -1;
    };
    hero.dealDamage = [&](float strength, const std::string&, game::Enemy& e) { portDamage.emplace_back(idOf(e), strength); };
    game::EnemyScene scene;
    scene.calls = &calls;
    scene.tweens = &tweens;
    scene.hero = &hero;
    scene.queries = &queries;
    scene.spawnEnemyDeadSprite = [&](const std::string&, Vector2 p) { portDeadSprites.emplace_back(-1, p); };

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
    for (const Value& v : tangleLines) {
        TangleObj s;
        s.id = static_cast<int>(v["s"].AsInt());
        std::vector<Vector2> parents;
        const Value& chain = v["chain"];
        for (size_t k = 1; k < chain.size(); k++) parents.push_back(V2(chain[k]));
        s.enemy = game::MakeEnemy(*data.prefab, scene, data.controller.get(), parents);
        game::Enemy& e = *s.enemy;
        s.tangle = dynamic_cast<game::TangleBehaviour*>(e.enemyBehaviour.get());
        if (!s.tangle) {
            std::printf("the Tangle prefab didn't make a TangleBehaviour\n");
            return 1;
        }
        e.navAgent.localPosition = V2(chain[0]);
        e.enemyDeadSprite = v["deadSprite"].IsString() ? v["deadSprite"].s : "";
        e.enemyStats.isChampion = v["stats"]["champion"].b;
        if (haveBase) e.Init(start["enemy"].s, base, context);
        else e.enemyBehaviour->Init();
        const Value& st = v["stats"];
        t.Add("stats from enemy_stats.json (Init)",
              haveBase && Same(e.enemyStats.maxHealth(), Bits(st["maxHealth"])) && Same(e.enemyStats.strength(), Bits(st["strength"])) &&
                  Same(e.enemyStats.speed(), Bits(st["speed"])) && Same(e.enemyStats.defense(), Bits(st["defense"])) &&
                  Same(e.enemyStats.weight(), Bits(st["weight"])),
              "tangle " + std::to_string(s.id) + ": maxHealth " + std::to_string(e.enemyStats.maxHealth()) + " vs " + std::to_string(Bits(st["maxHealth"])));
        game::EnemyStatModificator cur = e.enemyStats.current();
        cur.maxHealth = Bits(st["maxHealth"]);
        cur.strength = Bits(st["strength"]);
        cur.speed = Bits(st["speed"]);
        cur.defense = Bits(st["defense"]);
        cur.weight = Bits(st["weight"]);
        e.enemyStats.isChampion = st["champion"].b;
        e.enemyStats.Restore(cur, Bits(v["hp"]), v["inv"].b, v["alive"].b);
        const Value& ag = v["agent"];
        game::PolyNavAgent& a = e.navAgent;
        t.Add("agent settings (prefab)",
              Same(a.mass, Bits(ag["mass"])) && Same(a.stoppingDistance, Bits(ag["stoppingDistance"])) && Same(a.slowingDistance, Bits(ag["slowingDistance"])) &&
                  Same(a.accelerationRate, Bits(ag["accelerationRate"])) && Same(a.decelerationRate, Bits(ag["decelerationRate"])) &&
                  Same(a.avoidRadius, Bits(ag["avoidRadius"])) && Same(a.lookAheadDistance, Bits(ag["lookAheadDistance"])) &&
                  a.repath == ag["repath"].b && a.restrict == ag["restrict"].b && a.closerPointOnInvalid == ag["closerPointOnInvalid"].b,
              "tangle " + std::to_string(s.id));
        a.maxSpeed = Bits(ag["maxSpeed"]);
        a.velocity = V2(ag["vel"]);
        std::vector<Vector2> path;
        for (size_t k = 0; k + 1 < ag["path"].size(); k += 2) path.push_back({Bits(ag["path"][k]), Bits(ag["path"][k + 1])});
        a.RestoreState(V2(ag["goal"]), Bits(ag["acc"]), static_cast<int>(ag["req"].AsInt()), path);
        if (ag["req"].AsInt() > 0) std::printf("note: tangle %d starts with a path pending (not replayable)\n", s.id);
        const Value& rb = v["rb"];
        t.Add("Rigidbody2D settings (prefab, stats)",
              Same(e.rigidbody.mass, Bits(rb["mass"])) && Same(e.rigidbody.linearDrag, Bits(rb["drag"])) &&
                  e.rigidbody.constraints == static_cast<int>(rb["constraints"].AsInt()) && e.colliderSize.x == Bits(v["box"][2]) &&
                  e.attackSize.x == Bits(v["attackBox"][2]),
              "tangle " + std::to_string(s.id));
        e.rigidbody.mass = Bits(rb["mass"]);
        e.rigidbody.linearDrag = Bits(rb["drag"]);
        e.layer = static_cast<int>(v["layer"].AsInt());
        game::TangleBehaviour& b = *s.tangle;
        const Value& be = v["behaviour"];
        b.enemyState = static_cast<game::EnemyBehaviour::EnemyState>(be["es"].AsInt());
        b.targetPlayer = be["target"].b;
        b.canMove = be["canMove"].b;
        b._lapse = Bits(be["lapse"]);
        b.playingDamagedEffect = be["dmgFx"].b;
        t.Add("behaviour fields (prefab): fallInHoles", b.fallInHoles == be["fallInHoles"].b, "tangle " + std::to_string(s.id));
        b.fallInHoles = be["fallInHoles"].b;
        b.deadForce = static_cast<int>(be["deadForce"].AsInt());
        b.enabled = be["enabled"].b;
        t.Add("behaviour Init (playerOffset)", Same(b.playerOffset, V2(be["playerOffset"])), Str(b.playerOffset) + " vs " + Str(V2(be["playerOffset"])));
        // The weave as Init's draws (at the floor's generation) left it.
        const Value& w = v["weave"];
        t.Add("weave: omegas, culture", Same(b.omegaX, Bits(w["ox"])) && Same(b.omegaY, Bits(w["oy"])) && static_cast<int>(b.tangleCulture) == w["culture"].AsInt(),
              "tangle " + std::to_string(s.id));
        b.originalAmplitudeX = Bits(w["oax"]);
        b.originalAmplitudeY = Bits(w["oay"]);
        b.amplitudeX = Bits(w["ax"]);
        b.amplitudeY = Bits(w["ay"]);
        b.index = Bits(w["idx"]);
        b.heroColliderOffset = heroColliderOffset;
        const Value& an = v["anim"];
        const int state = data.controller->StateByHash(static_cast<int32_t>(an["state"].AsInt()));
        if (state >= 0) e.enemyAnimator->Play(data.controller->states[static_cast<size_t>(state)].name, Bits(an["t"]));
        e.enemyAnimator->speed = Bits(an["speed"]);
        e.animatorEnabled = an["on"].b;
        e.sprite = an["sprite"].IsString() ? an["sprite"].s : "";
        e.flashAmount = Bits(v["flash"]);
        for (int k = 0; k < 4; k++) e.color[k] = Bits(v["color"][static_cast<size_t>(k)]);
        e.navAgent.OnDisable();
        tangles.push_back(std::move(s));
    }
    for (const Value& name : start["agents"].arr) {
        bool found = false;
        for (TangleObj& s : tangles)
            if (name.IsInt() && s.id == name.AsInt()) {
                s.enemy->navAgent.OnEnable();
                found = true;
            }
        if (!found) std::printf("note: PolyNavAgent.allAgents has %s, not one of the tangles\n", name.Dump().c_str());
    }
    for (TangleObj& s : tangles) {
        physics.Add(*s.enemy);
        const Value* v = nullptr;
        for (const Value& l : tangleLines)
            if (l["s"].AsInt() == s.id) v = &l;
        physics.PlaceBody(*s.enemy, V2((*v)["rb"]["pos"]));
        s.enemy->body->SetVelocity({Bits((*v)["rb"]["vel"][0]), Bits((*v)["rb"]["vel"][1])});
        if (!(*v)["rb"]["awake"].b) s.enemy->body->Body()->SetAwake(false);
    }
    auto find = [&](int id) -> TangleObj* {
        for (TangleObj& s : tangles)
            if (s.id == id) return &s;
        return nullptr;
    };
    unity::Random::SetState(State(f0["rng"]));

    const float fixedDt = Bits(start["fixedDeltaTime"]);
    double fixedClock = 0.0;
    // EnableEnemies (an Invoke itself) on the fixed clock: the steps it came after, and the
    // targeting it starts a second later, if that hasn't come yet (EnableEnemiesClock).
    const EnableClock enable = EnableEnemiesClock(start, f0, fixedDt);
    if (enable.known) {
        calls.now = enable.time;
        calls.inFixedStep = true;
        calls.fixedNow = enable.fixedTime;
        for (TangleObj& s : tangles)
            if (!s.enemy->enemyBehaviour->targetPlayer) s.enemy->enemyBehaviour->StartTargetingPlayerAfterTime(Bits(start["targetDelay"]));
        calls.inFixedStep = false;
    }
    auto setHero = [&](const Value& hs) {
        world.SetLayer(heroBody, static_cast<int>(hs[6].AsInt()));
        world.SyncTransform(heroBody, {Bits(hs[0]), Bits(hs[1])});
        heroBody.SetVelocity({Bits(hs[4]), Bits(hs[5])});
        if (heroBody.IsAwake() != hs[7].b) heroBody.Body()->SetAwake(hs[7].b);
    };
    if (enable.known) {
        for (int k = 0; k < enable.stepsBefore; k++) {
            setHero(f0["steps"][0]["hero"]);
            physics.Step(fixedDt);
        }
        std::printf("%d fixed steps between EnableEnemies and the recording (stepped first)\n", enable.stepsBefore);
    }
    int rngElsewhere = 0;
    // The script order: per frame and Tangle with both LateUpdates, which ran first.
    int tangleFirst = 0, agentFirst = 0;
    int weaves = 0, weaveHits = 0;
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
        for (const Value& ev : events.arr) {
            const std::string& kind = ev["k"].s;
            if (ev["step"].AsInt() == 0 &&
                (kind == "push" || kind == "dmgToEnemy" || (kind == "attackHit" && !ev["player"].b && ev["other"].IsString() && ev["other"].s == "Collider"))) {
                world.FixtureMadeElsewhere();
                break;
            }
        }
        if (const char* nf = std::getenv("ML_NEWFIXTURE_FRAMES")) { // EXPERIMENT: "406,..."
            const std::string list = std::string(",") + nf + ",";
            if (list.find("," + std::to_string(fn) + ",") != std::string::npos) world.FixtureMadeElsewhere();
        }
        // The fixed steps.
        for (size_t k = 0; k < f["steps"].size(); k++) {
            const Value& st = f["steps"][k];
            fixedClock = fixedClock > 0.0 ? fixedClock + static_cast<double>(fixedDt) : static_cast<double>(Bits(st["ft"]));
            if (std::fabs(fixedClock - static_cast<double>(Bits(st["ft"]))) > 1e-4)
                std::printf("note: frame %lld step %zu: the fixed clock %.9f, the recording %.9f\n", fn, k, fixedClock, static_cast<double>(Bits(st["ft"])));
            calls.now = time;
            calls.fixedNow = fixedClock;
            calls.inFixedStep = true;
            setHero(st["hero"]);
            for (const Value& ss : st["s"].arr) {
                TangleObj* o = find(static_cast<int>(ss[0].AsInt()));
                if (!o || o->enemy->destroyed) continue;
                game::Enemy* e = o->enemy.get();
                const bool ok = Same(e->position(), {Bits(ss[1]), Bits(ss[2])}) && Same(e->body->position(), {Bits(ss[3]), Bits(ss[4])}) &&
                                Same(e->body->velocity(), {Bits(ss[5]), Bits(ss[6])});
                t.Add("fixed step's start (transform, body)", ok,
                      "frame " + std::to_string(fn) + " step " + std::to_string(k) + " tangle " + std::to_string(ss[0].AsInt()) + ": port " +
                          Str(e->position()) + " body " + Str(e->body->position()) + " v " + Str(e->body->velocity()) + "; game " +
                          Str({Bits(ss[1]), Bits(ss[2])}) + " body " + Str({Bits(ss[3]), Bits(ss[4])}) + " v " + Str({Bits(ss[5]), Bits(ss[6])}));
            }
            portDamage.clear();
            { const char* dbf = std::getenv("ML_B2DEBUG_FRAME"); g_b2Debug = dbf && std::atoll(dbf) == fn; if (g_b2Debug) std::printf("frame %lld step %zu\n", fn, k); }
            physics.Step(fixedDt);
            { g_b2Debug = 0; }
            std::vector<std::pair<int, float>> gameDamage;
            for (const Value& ev : events.arr) {
                if (ev["step"].AsInt() != static_cast<long long>(k)) continue;
                const std::string& kind = ev["k"].s;
                TangleObj* o = find(static_cast<int>(ev["s"].AsInt()));
                if (kind == "dmgToPlayer") gameDamage.emplace_back(static_cast<int>(ev["s"].AsInt()), Bits(ev["str"]));
                if (!o) continue;
                game::Enemy* e = o->enemy.get();
                if (kind == "dmgToEnemy") {
                    hero.position = V2(ev["hero"]);
                    t.Add("health before a weapon hit", Same(e->enemyStats.CurrentHealth(), Bits(ev["hp0"])),
                          "frame " + std::to_string(fn) + " tangle " + std::to_string(ev["s"].AsInt()));
                    e->DealDamageToEnemy(Bits(ev["str"]), static_cast<game::EnemyDamageReceivedType>(ev["type"].AsInt()));
                    t.Add("health after a weapon hit", Same(e->enemyStats.CurrentHealth(), Bits(ev["hp1"])),
                          "frame " + std::to_string(fn) + " tangle " + std::to_string(ev["s"].AsInt()) + ": " +
                              std::to_string(e->enemyStats.CurrentHealth()) + " vs " + std::to_string(Bits(ev["hp1"])));
                } else if (kind == "push") {
                    t.Add("weapon push: the tangle where the game had it", Same(e->position(), V2(ev["pos"])), Str(e->position()) + " vs " + Str(V2(ev["pos"])));
                    e->PushBackwards(V2(ev["hero"]), Bits(ev["force"]));
                }
            }
            calls.UpdateFixed(fixedClock);
            calls.inFixedStep = false;
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
        // Update.
        calls.now = time;
        const Value* tweenUdt = f.Find("dotweenUdt");
        tweens.Update(dt, tweenUdt ? Bits(*tweenUdt) : f.Find("udt") ? Bits(f["udt"]) : dt);
        for (const Value& u : f["updates"].arr) {
            TangleObj* o = find(static_cast<int>(u["s"].AsInt()));
            if (!o) continue;
            game::Enemy* e = o->enemy.get();
            hero.position = V2(u["hero"]);
            if (!SameState(unity::Random::GetState(), State(u["rng0"]))) {
                rngElsewhere++;
                unity::Random::SetState(State(u["rng0"]));
            }
            t.Add("Update called on an enabled tangle", e->enemyBehaviour->enabled && !e->destroyed, "frame " + std::to_string(fn));
            e->enemyBehaviour->Update(dt);
            t.Add("Random draws in the tangles' Update", SameState(unity::Random::GetState(), State(u["rng1"])),
                  "frame " + std::to_string(fn) + " tangle " + std::to_string(u["s"].AsInt()));
            unity::Random::SetState(State(u["rng1"]));
        }
        calls.Update(time);
        nav.ResumeCoroutines();
        for (TangleObj& s : tangles) s.enemy->UpdateAnimator(dt);
        nav.LateUpdate();
        // The LateUpdates, the Tangles' and their agents', in the game's order.
        std::map<int, char> firstOf;
        for (const Value& l : f["late"].arr) {
            const int id = static_cast<int>(l["s"].AsInt());
            TangleObj* o = find(id);
            if (!o) continue;
            game::Enemy* e = o->enemy.get();
            const bool isTangle = l["k"].s == "t";
            if (!firstOf.count(id)) firstOf[id] = isTangle ? 't' : 'a';
            else if (firstOf[id] != (isTangle ? 't' : 'a') && firstOf[id] != 'x') {
                (firstOf[id] == 't' ? tangleFirst : agentFirst)++;
                firstOf[id] = 'x';
            }
            const std::string at = "frame " + std::to_string(fn) + " tangle " + std::to_string(id);
            t.Add(isTangle ? "LateUpdate's start: agent velocity (Tangle's)" : "LateUpdate's start: agent velocity (agent's)", Same(e->navAgent.velocity, V2(l["v0"])),
                  at + ": port " + Str(e->navAgent.velocity) + " game " + Str(V2(l["v0"])));
            if (!isTangle) {
                if (!e->destroyed && e->active) e->navAgent.LateUpdate(dt);
                t.Add("agent LateUpdate: velocity after", Same(e->navAgent.velocity, V2(l["v1"])),
                      at + ": port " + Str(e->navAgent.velocity) + " game " + Str(V2(l["v1"])));
                continue;
            }
            game::TangleBehaviour& b = *o->tangle;
            hero.position = V2(l["hero"]);
            t.Add("Tangle LateUpdate's start (its position, target, index)",
                  Same(e->position(), V2(l["pos"])) && b.targetPlayer == l["target"].b && Same(b.index, Bits(l["i0"])) && b.enabled && !e->destroyed,
                  at + ": port " + Str(e->position()) + " index " + std::to_string(b.index) + " game " + Str(V2(l["pos"])) + " " +
                      std::to_string(Bits(l["i0"])));
            b.LateUpdate(dt);
            const bool taken = l["taken"].b;
            t.Add("Tangle LateUpdate: the weave ran (target, past 1.5 tiles)", b.lastWeave.ran == taken,
                  at + ": port " + std::to_string(b.lastWeave.ran) + " game " + std::to_string(taken));
            t.Add("Tangle LateUpdate: index, amplitudes", Same(b.index, Bits(l["i1"])) && Same(b.amplitudeX, Bits(l["ax"])) && Same(b.amplitudeY, Bits(l["ay"])),
                  at + ": port " + std::to_string(b.index) + " " + Str({b.amplitudeX, b.amplitudeY}) + " game " + std::to_string(Bits(l["i1"])) + " " +
                      Str({Bits(l["ax"]), Bits(l["ay"])}));
            if (taken && b.lastWeave.ran) {
                weaves++;
                const Value& gameHit = l["hit"];
                const unity::RaycastHit2D& ph = b.lastWeave.hit;
                if (!gameHit.IsNull()) weaveHits++;
                t.Add("Tangle LateUpdate: the weave's direction new Vector2(x, y)", Same(b.lastWeave.direction, V2(l["d"])),
                      at + ": port " + Str(b.lastWeave.direction) + " game " + Str(V2(l["d"])));
                const bool sameCollider = gameHit.IsNull() ? ph.collider == nullptr : ph.collider && ph.collider->path == gameHit.s;
                t.Add("Tangle LateUpdate: Raycast's collider", sameCollider,
                      at + ": port " + (ph.collider ? ph.collider->path : std::string("none")) + " game " + (gameHit.IsNull() ? std::string("none") : gameHit.s) +
                          " from " + Str(e->position()) + " along " + Str(b.lastWeave.direction));
                if (sameCollider && ph.collider) {
                    t.Info("Raycast's hit: point, normal (unused by the game)", Same(ph.point, V2(l["point"])) && Same(ph.normal, V2(l["normal"])),
                           at + ": port " + Str(ph.point) + " " + Str(ph.normal) + " game " + Str(V2(l["point"])) + " " + Str(V2(l["normal"])));
                    t.Info("Raycast's hit: distance, fraction (unused by the game)", Same(ph.distance, Bits(l["dist"])) && Same(ph.fraction, Bits(l["frac"])),
                           at + ": port " + std::to_string(ph.distance) + " " + std::to_string(ph.fraction) + " game " + std::to_string(Bits(l["dist"])) + " " +
                               std::to_string(Bits(l["frac"])));
                }
            }
            t.Add("Tangle LateUpdate: agent velocity after", Same(e->navAgent.velocity, V2(l["v1"])),
                  at + ": port " + Str(e->navAgent.velocity) + " game " + Str(V2(l["v1"])));
        }
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

        // The frame's end, per Tangle.
        for (const Value& v : f["tangles"].arr) {
            const int id = static_cast<int>(v["s"].AsInt());
            TangleObj* o = find(id);
            if (!o) continue;
            game::Enemy* e = o->enemy.get();
            const std::string at = "frame " + std::to_string(fn) + " tangle " + std::to_string(id);
            t.Add("destroyed", e->destroyed == v["gone"].b, at);
            if (v["gone"].b || e->destroyed) continue;
            const Value& ag = v["agent"];
            const Value& an = v["anim"];
            const Value& be = v["behaviour"];
            const Value& w = v["weave"];
            const bool pos = Same(e->position(), V2(v["pos"])) && Same(e->navAgent.localPosition, V2(v["local"]));
            t.Add("position (world, local)", pos, at + ": port " + Str(e->position()) + " game " + Str(V2(v["pos"])));
            const Value& rb = v["rb"];
            const bool body = Same(e->body->position(), {Bits(rb[0]), Bits(rb[1])}) && Same(e->body->velocity(), {Bits(rb[2]), Bits(rb[3])}) &&
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
            t.Add("Animator state, normalized time (bits)",
                  e->animatorEnabled == an["on"].b && (!an["on"].b || (info.shortNameHash == static_cast<int32_t>(an["state"].AsInt()) &&
                                                                        Same(info.normalizedTime, Bits(an["t"])))),
                  at + ": port t " + std::to_string(info.normalizedTime) + " game " + std::to_string(Bits(an["t"])));
            t.Add("sprite", an["sprite"].IsString() && e->sprite == an["sprite"].s, at + ": port " + e->sprite + " game " + an["sprite"].Dump());
            const game::TangleBehaviour& b = *o->tangle;
            t.Add("health, alive, invincible", Same(e->enemyStats.CurrentHealth(), Bits(v["hp"])) && e->enemyStats.isAlive() == v["alive"].b &&
                                                   e->enemyStats.Invencible() == v["inv"].b,
                  at + ": port " + std::to_string(e->enemyStats.CurrentHealth()) + " game " + std::to_string(Bits(v["hp"])));
            t.Add("behaviour (state, targeting, canMove, lapse, damaged, enabled, layer)",
                  static_cast<int>(b.enemyState) == be["es"].AsInt() && b.targetPlayer == be["target"].b && b.canMove == be["canMove"].b &&
                      Same(b._lapse, Bits(be["lapse"])) && b.playingDamagedEffect == be["dmgFx"].b && b.enabled == be["enabled"].b &&
                      e->layer == v["layer"].AsInt(),
                  at + ": port lapse " + std::to_string(b._lapse) + " target " + std::to_string(b.targetPlayer) + " game " +
                      std::to_string(Bits(be["lapse"])) + " " + std::to_string(be["target"].b));
            t.Add("weave (amplitudes, index)",
                  Same(b.originalAmplitudeX, Bits(w["oax"])) && Same(b.originalAmplitudeY, Bits(w["oay"])) && Same(b.amplitudeX, Bits(w["ax"])) &&
                      Same(b.amplitudeY, Bits(w["ay"])) && Same(b.index, Bits(w["idx"])),
                  at + ": port index " + std::to_string(b.index) + " game " + std::to_string(Bits(w["idx"])));
            bool color = Same(e->flashAmount, Bits(v["flash"]));
            for (int k = 0; k < 4; k++) color = color && Same(e->color[k], Bits(v["color"][static_cast<size_t>(k)]));
            const std::string colours = at + ": port flash " + std::to_string(e->flashAmount) + " game " + std::to_string(Bits(v["flash"]));
            if (!e->enemyStats.isAlive() && !f.Find("dotweenUdt")) {
                bool near = std::fabs(e->flashAmount - Bits(v["flash"])) <= 0.03f;
                for (int k = 0; k < 4; k++) near = near && std::fabs(e->color[k] - Bits(v["color"][static_cast<size_t>(k)])) <= 0.03f;
                t.Add("material in the death fade (DOTween clock unrecorded: within 0.03)", near, colours);
                t.Info("material in the death fade, bit for bit (needs the DOTween clock)", color, colours);
            } else {
                t.Add("material (_FlashAmount, _Color)", color, colours);
            }
            if (t.debug)
                std::printf("frame %lld tangle %d: %s %s | game %s\n", fn, id, Str(e->position()).c_str(), e->sprite.c_str(), Str(V2(v["pos"])).c_str());
        }
    }
    game::PolyNav2D::current = nullptr;
    std::printf("\n");
    t.Print();
    std::printf("Random drawn by the game's other scripts (resynced) %d times\n", rngElsewhere);
    std::printf("weaves: %d (%d with the raycast hitting)\n", weaves, weaveHits);
    std::printf("script order: the Tangle's LateUpdate before its agent's in %d frames, after it in %d\n", tangleFirst, agentFirst);
    std::printf("\n%s\n", t.Ok() ? "ALL MATCH" : "MISMATCHES");
    return t.Ok() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string file = argc > 1 ? argv[1] : "data/oracle/tangle.jsonl";
    CheckRaycast();
    Data data = LoadData();
    if (data.ok) {
        CheckWeave(data);
    } else {
        std::printf("(no game data in %s (ML_DATA; animators/Tangle.json from tools/extract/animators.py): skipping the weave checks)\n", data.dir.c_str());
    }
    std::printf("self-checks: %d passed, %d failed\n", gPassed, gFailed);
    const int replay = data.ok || !std::ifstream(file) ? ReplayRecording(file, data) : (std::printf("no game data for the recording\n"), 1);
    return gFailed || replay ? 1 : 0;
}
