// The Tangle's recording (data/oracle/tangle.jsonl, `-oracle-tangle`) replayed against the game's own
// TangleBehaviour translated (tools/translate), with EnemyBehaviour, Enemy, EnemyStats, PolyNavAgent,
// PolyNav2D..., on the Unity facade (recomp/src/bind): tangle_test's replay (the same recording, the
// same rows, the same pass criterion) with the translated objects in place of game::Enemy.
//
// The room's Tangles made from their prefab (prefabs.json, animators/Tangle.json) under the room's
// objects, Init'ed by the game's code, their recorded state (the weave's included) put back into the
// translated fields; then every frame as the game ran it: the fixed steps (the hero's body as each
// found it, the weapon's hits and pushes), DOTween, the Tangles' Updates in the game's order (each
// from the Random state and the hero position it had), the delayed calls, the coroutines, the
// Animators, PolyNav2D's LateUpdate, then each LateUpdate, the Tangles' and their agents', in the
// order the game ran them ("late"), each Tangle's checked against the game's: where it and the hero
// were, index before and after, whether the weave ran (its Physics2D.Raycast made:
// bind::PhysicsScene::onRaycast), the amplitudes, the raycast's direction and collider, the agent's
// velocity after; point, normal, distance and fraction of the hits inform. Then each frame's end per
// Tangle, as tangle_test compares it.
//
// Usage: tangle_translated_test [tangle.jsonl]   (ML_TANGLE_DEBUG=1: every mismatch;
//        ML_GC_EVERY_FRAME=1: the collector at every frame boundary)

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

namespace {

using json::Value;
using unity::Vector2;
using UnityEngine::GameObject;
using UnityEngine::Transform;

#include "replay_common.h" // (the values, the tally, the room's colliders)

struct Data {
    std::string dir;
    Value physics2d;
    std::unique_ptr<unity::AnimatorController> controller;
    const game::Prefab* prefab = nullptr;
};

bool LoadData(Data& d)
{
    d.dir = std::getenv("ML_DATA") ? std::getenv("ML_DATA") : "data/extracted";
    const std::string prefabs = ReadFile(d.dir + "/prefabs.json"), anim = ReadFile(d.dir + "/animators/Tangle.json"),
                      physics = ReadFile(d.dir + "/physics2d.json");
    if (prefabs.empty() || anim.empty() || ReadFile(d.dir + "/constants.json").empty() || ReadFile(d.dir + "/enemy_stats.json").empty()) return false;
    game::PrefabLibrary::Load(json::Parse(prefabs));
    d.prefab = game::PrefabLibrary::Find("Tangle", "enemies_golem");
    d.controller = std::make_unique<unity::AnimatorController>(unity::AnimatorController::FromJson(json::Parse(anim)));
    if (!physics.empty()) d.physics2d = json::Parse(physics);
    UnityEngine::Resources::SetTextRoot(d.dir);
    cs::StaticScope keep;
    gen::Constants::ReadFile(S("constants"));
    gen::EnemyStatManager::get_Instance()->ReadDefaultFile();
    return d.prefab != nullptr;
}

struct TangleObj {
    int id = 0;
    GameObject* go = nullptr;
    gen::Enemy* enemy = nullptr;
    gen::TangleBehaviour* tangle = nullptr;
    gen::PolyNavAgent* agent = nullptr;
    UnityEngine::Rigidbody2D* rb = nullptr;
    UnityEngine::BoxCollider2D* box = nullptr;
    UnityEngine::Animator* animator = nullptr;
    GameObject* attack = nullptr;
    UnityEngine::BoxCollider2D* attackBox = nullptr;
    bool Gone() const { return !go || go->destroyed_; }
};

Vector2 Position(const TangleObj& s) { return XY(s.go->transform_->get_position()); }
Vector2 Local(const TangleObj& s) { return XY(s.go->transform_->localPosition_); }
float Health(const TangleObj& s) { return s.enemy->enemyStats->_currentHealth; }
std::string SpriteOf(const TangleObj& s) { return s.enemy->enemySprite ? s.enemy->enemySprite->SpriteName() : std::string(); }
UnityEngine::Material* MaterialOf(const TangleObj& s) { return s.enemy->enemySprite->get_material(); }

int Replay(const std::string& file, const Data& data)
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
    Tally t;
    t.width = 52;
    t.debug = std::getenv("ML_TANGLE_DEBUG") != nullptr;
    std::printf("recording: room %s, %zu tangles, %zu frames (translated)\n", start["room"].s.c_str(), tangleLines.size(), frames.size());

    for (const Value& p : probes) {
        unity::Random::SetState(State(p["state"]));
        const Vector2 got = unity::Random::insideUnitCircle();
        t.Add("Random.insideUnitCircle (probes)", Same(got, V2(p["out"])) && SameState(unity::Random::GetState(), State(p["after"])),
              Str(got) + " vs " + Str(V2(p["out"])));
    }

    // ---- the scene: the world (tangle_test's: the room's colliders and holes, the hero's body) and
    // the queries the weave's raycast sees (everything enabled on its layers, triggers too, and the holes) ----
    bind::Scene scene;
    unity::Physics2DSettings settings;
    if (!data.physics2d.IsNull()) settings.Load(data.physics2d);
    unity::PhysicsWorld world(settings);
    unity::Physics2D roomColliders, queries;
    const int weaveMask = static_cast<int>(start["weaveMask"].AsInt());
    // TangleBehaviour.LateUpdate's mask, as the facade's layers name them.
    const int portMask = (1 << UnityEngine::LayerMask::NameToLayer(S("LevelBoundary"))) | (1 << UnityEngine::LayerMask::NameToLayer(S("EnemyBoundary"))) |
                         (1 << UnityEngine::LayerMask::NameToLayer(S("RisenObstacle")));
    t.Add("the weave's layer mask (LevelBoundary | EnemyBoundary | RisenObstacle)", weaveMask == portMask, std::to_string(weaveMask));
    queries.queriesHitTriggers = start["queriesHitTriggers"].b;
    queries.queriesStartInColliders = start["queriesStartInColliders"].b;
    for (const Value& c : colliders) {
        const std::string body = c.Find("body") ? c["body"].s : "none";
        const bool hole = c["type"].s == "PolygonCollider2D" && c["tag"].s == "Hole";
        if (c["enabled"].b && (weaveMask & (1 << c["layer"].AsInt())) != 0) {
            if (c["type"].s == "BoxCollider2D") queries.Add(ColliderFrom(c));
            else if (!hole) std::printf("note: a %s on the weave's layers (%s): not in the queries\n", c["type"].s.c_str(), c["path"].s.c_str());
        }
        if (hole && c["enabled"].b) {
            const unity::Collider2D hc = HoleFrom(c);
            CheckPolygonProbe(t, c, hc); // (the oracle's probe of Unity's cut, where recorded)
            queries.Add(hc);
        }
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
    unity::Rigidbody2D& heroBody = world.AddBody(heroDef, V2(h["pos"]), static_cast<int>(h["layer"].AsInt()), V2(h["colliderOffset"]), V2(h["colliderSize"]));
    bind::PhysicsScene physics(world, scene);
    physics.queries = &queries;
    // The objects the colliders' messages and the queries name: one per collider.
    std::map<std::string, UnityEngine::BoxCollider2D*> objectOfPath;
    auto standIn = [&](const unity::Collider2D& c) {
        UnityEngine::BoxCollider2D*& object = objectOfPath[c.path];
        if (object) return object;
        GameObject* g = GameObject::Create(Leaf(c.path));
        if (!c.tag.empty()) g->tag_ = S(c.tag);
        g->layer_ = c.layer;
        object = cs::gcnew<UnityEngine::BoxCollider2D>();
        object->isTrigger_ = c.trigger;
        g->Attach(object);
        return object;
    };
    for (b2Body* b = world.World().GetBodyList(); b; b = b->GetNext())
        for (b2Fixture* f = b->GetFixtureList(); f; f = f->GetNext())
            if (const unity::Collider2D* c = world.ColliderOf(f)) physics.Register(f, standIn(*c));
    for (const unity::Collider2D& c : queries.Colliders()) physics.RegisterQuery(&c, standIn(c));

    // The hero: HeroMerchant, his controller (GetColliderOffsset: the recording's) and stats, his body's collider.
    GameObject* heroGo = GameObject::Create("HeroMerchant");
    heroGo->tag_ = S("Player");
    heroGo->layer_ = static_cast<int>(h["layer"].AsInt());
    auto* heroMerchant = cs::New<gen::HeroMerchant>();
    auto* heroController = cs::New<gen::HeroMerchantController>();
    auto* heroStats = cs::New<gen::HeroMerchantStats>();
    heroGo->Attach(heroMerchant);
    heroGo->Attach(heroController);
    heroGo->Attach(heroStats);
    heroMerchant->heroMerchantController = heroController;
    heroMerchant->heroMerchantStats = heroStats;
    heroStats->isDead = CodeStage::AntiCheat::ObscuredTypes::ObscuredBool::op_Implicit(false);
    auto* heroCollider = cs::gcnew<UnityEngine::BoxCollider2D>();
    heroGo->Attach(heroCollider);
    physics.Register(heroBody.Body()->GetFixtureList(), heroCollider);
    auto setHeroPosition = [&](Vector2 p) { heroGo->transform_->set_position(UnityEngine::Vector3{p.x, p.y, 0.0f}); };
    setHeroPosition(V2(h["pos"]));
    standins::S().heroColliderOffset = V2(h["getColliderOffset"]);

    // ---- the game's services the Tangles reach ----
    const Value& sc = start["stats"];
    if (sc["ngp"].b || sc["dlc"].b) {
        std::printf("a New Game+ or DLC recording: GameManager's game slot isn't stood in for\n");
        return 1;
    }
    standins::S().difficulty = static_cast<int>(sc["difficulty"].AsInt());
    standins::S().afternoon = sc["afternoon"].b;
    standins::S().timeManager = cs::gcnew<gen::TimeManagerV2>();
    standins::S().gameManager = nullptr;
    gen::CameraManager::_instance = cs::gcnew<gen::CameraManager>();
    gen::HUDManager::Instance = cs::gcnew<gen::HUDManager>();
    gen::HUDManager::Instance->minimap = cs::gcnew<gen::MinimapPanel>();
    // (tangle_test's replay has no MasterAudio: the Tangles' sounds' draws are resynced.)

    // The room: Dungeon / <room> (DungeonRoom) / DungeonLevelEnemies / EnemySpawn / the Tangle.
    const Value& chain0 = tangleLines[0]["chain"];
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
    if (const Value* rf = start.Find("roomFloor")) {
        room->roomFloorWidth = static_cast<int32_t>((*rf)[0].AsInt());
        room->roomFloorHeight = static_cast<int32_t>((*rf)[1].AsInt());
        room->roomCenterYOffset = static_cast<int32_t>((*rf)[2].AsInt());
    }
    GameObject* levelEnemies = GameObject::Create("DungeonLevelEnemies", roomGo->transform_);
    at(levelEnemies, V2(chain0[2]));
    auto* dungeonManager = cs::New<gen::DungeonManager>();
    dungeonManager->intermediateInfo.dungeonType = gen::DungeonManager_DungeonType::Normal;
    dungeonManager->intermediateInfo.dungeonCulture = static_cast<gen::ItemMaster_Culture>(sc["dungeonCulture"].AsInt());
    dungeonManager->intermediateInfo.dungeonLevel = static_cast<int32_t>(sc["level"].AsInt());
    dungeonManager->currentDungeonRoom = room;
    gen::DungeonManager::_instance = dungeonManager;

    // The map.
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

    // ---- the Tangles ----
    std::vector<TangleObj> tangles;
    tangles.reserve(tangleLines.size());
    auto find = [&](int id) -> TangleObj* {
        for (TangleObj& s : tangles)
            if (s.id == id) return &s;
        return nullptr;
    };
    auto idOf = [&](const gen::Enemy* e) {
        for (const TangleObj& s : tangles)
            if (s.enemy == e) return s.id;
        return -1;
    };
    std::vector<std::pair<int, float>> portDamage;
    std::vector<Vector2> portDeadSprites;
    standins::S().dealDamageToPlayer = [&](float strength, cs::String*, int, gen::Enemy* e) { portDamage.emplace_back(idOf(e), strength); };
    standins::S().spawnEnemyDeadSprite = [&](gen::DungeonRoom*, UnityEngine::Sprite*, UnityEngine::Vector3 p) { portDeadSprites.push_back(XY(p)); };
    // The weave's raycast in the Tangle's LateUpdate running (none: it didn't run).
    struct Weave {
        bool ran = false;
        Vector2 direction;
        unity::RaycastHit2D hit;
    } weave;
    bool inLateUpdate = false;
    physics.onRaycast = [&](Vector2, Vector2 direction, float, int32_t, const unity::RaycastHit2D& hit) {
        if (!inLateUpdate) return;
        weave.ran = true;
        weave.direction = direction;
        weave.hit = hit;
    };
    std::vector<std::string> skipped;
    for (const Value& v : tangleLines) {
        tangles.emplace_back();
        TangleObj& s = tangles.back();
        s.id = static_cast<int>(v["s"].AsInt());
        const Value& chain = v["chain"];
        GameObject* spawn = GameObject::Create("EnemySpawn", levelEnemies->transform_);
        at(spawn, V2(chain[1]));
        bind::PrefabOptions options;
        options.controller = data.controller.get();
        skipped.clear();
        s.go = bind::InstantiatePrefab(*data.prefab, spawn->transform_, options, &gen::cs_AddComponent, &skipped);
        s.go->transform_->localPosition_ = UnityEngine::Vector3{Bits(chain[0][0]), Bits(chain[0][1]), 0.0f};
        s.enemy = s.go->GetComponent<gen::Enemy*>();
        s.tangle = s.go->GetComponent<gen::TangleBehaviour*>();
        s.agent = s.go->GetComponent<gen::PolyNavAgent*>();
        s.rb = s.go->GetComponent<UnityEngine::Rigidbody2D*>();
        s.box = s.go->GetComponent<UnityEngine::BoxCollider2D*>();
        s.animator = s.go->GetComponent<UnityEngine::Animator*>();
        s.attack = bind::FindBelow(s.go, "AttackCollider");
        s.attackBox = s.attack ? s.attack->GetComponent<UnityEngine::BoxCollider2D*>() : nullptr;
        if (!s.enemy || !s.tangle || !s.agent || !s.rb || !s.box || !s.animator) {
            std::printf("the Tangle prefab didn't make its Enemy, TangleBehaviour, agent, body, box and Animator\n");
            return 1;
        }
        s.enemy->enemyRoom = room;
        room->roomEnemies->Add(s.enemy);
        s.enemy->enemyDeadSprite = v["deadSprite"].IsString() ? UnityEngine::Sprite::Named(v["deadSprite"].s) : nullptr;
        s.enemy->enemyStats->isChampion = v["stats"]["champion"].b;
        s.enemy->Init(S(start["enemy"].s));
        const Value& st = v["stats"];
        gen::EnemyStats* es = s.enemy->enemyStats;
        t.Add("stats from enemy_stats.json (Init)",
              Same(es->get_maxHealth(), Bits(st["maxHealth"])) && Same(es->get_strength(), Bits(st["strength"])) && Same(es->get_speed(), Bits(st["speed"])) &&
                  Same(es->get_defense(), Bits(st["defense"])) && Same(es->get_weight(), Bits(st["weight"])),
              "tangle " + std::to_string(s.id) + ": maxHealth " + std::to_string(es->get_maxHealth()) + " vs " + std::to_string(Bits(st["maxHealth"])));
        es->_currentStats->maxHealth = Bits(st["maxHealth"]);
        es->_currentStats->strength = Bits(st["strength"]);
        es->_currentStats->speed = Bits(st["speed"]);
        es->_currentStats->defense = Bits(st["defense"]);
        es->_currentStats->weight = Bits(st["weight"]);
        es->isChampion = st["champion"].b;
        es->_currentHealth = Bits(v["hp"]);
        es->_invincible = v["inv"].b;
        es->_isAlive = v["alive"].b;
        const Value& ag = v["agent"];
        gen::PolyNavAgent* a = s.agent;
        t.Add("agent settings (prefab)",
              Same(a->mass, Bits(ag["mass"])) && Same(a->stoppingDistance, Bits(ag["stoppingDistance"])) && Same(a->slowingDistance, Bits(ag["slowingDistance"])) &&
                  Same(a->accelerationRate, Bits(ag["accelerationRate"])) && Same(a->decelerationRate, Bits(ag["decelerationRate"])) &&
                  Same(a->avoidRadius, Bits(ag["avoidRadius"])) && Same(a->lookAheadDistance, Bits(ag["lookAheadDistance"])) && a->repath == ag["repath"].b &&
                  a->restrict == ag["restrict"].b && a->closerPointOnInvalid == ag["closerPointOnInvalid"].b,
              "tangle " + std::to_string(s.id));
        a->maxSpeed = Bits(ag["maxSpeed"]);
        a->velocity = V2(ag["vel"]);
        a->_primeGoal = V2(ag["goal"]);
        a->accelerationValue = Bits(ag["acc"]);
        a->requests = static_cast<int32_t>(ag["req"].AsInt());
        a->_activePath->Clear();
        for (size_t k = 0; k + 1 < ag["path"].size(); k += 2) a->_activePath->Add(Vector2{Bits(ag["path"][k]), Bits(ag["path"][k + 1])});
        if (ag["req"].AsInt() > 0) std::printf("note: tangle %d starts with a path pending (not replayable)\n", s.id);
        const Value& rb = v["rb"];
        t.Add("Rigidbody2D settings (prefab, stats)",
              Same(s.rb->get_mass(), Bits(rb["mass"])) && Same(s.rb->def_.linearDrag, Bits(rb["drag"])) &&
                  s.rb->def_.constraints == static_cast<int>(rb["constraints"].AsInt()) && Same(s.box->size_.x, Bits(v["box"][2])) && s.attackBox &&
                  Same(s.attackBox->size_.x, Bits(v["attackBox"][2])),
              "tangle " + std::to_string(s.id));
        s.rb->set_mass(Bits(rb["mass"]));
        s.rb->set_drag(Bits(rb["drag"]));
        s.go->set_layer(static_cast<int32_t>(v["layer"].AsInt()));
        gen::TangleBehaviour* b = s.tangle;
        const Value& be = v["behaviour"];
        b->enemyState = static_cast<gen::EnemyBehaviour_EnemyState>(be["es"].AsInt());
        b->targetPlayer = be["target"].b;
        b->canMove = be["canMove"].b;
        b->_lapse = Bits(be["lapse"]);
        b->playingDamagedEffect = be["dmgFx"].b;
        t.Add("behaviour fields (prefab): fallInHoles", b->fallInHoles == be["fallInHoles"].b, "tangle " + std::to_string(s.id));
        b->fallInHoles = be["fallInHoles"].b;
        b->deadForce = static_cast<int32_t>(be["deadForce"].AsInt());
        b->set_enabled(be["enabled"].b);
        t.Add("behaviour Init (playerOffset)", Same(XY(b->playerOffset), V2(be["playerOffset"])), Str(XY(b->playerOffset)) + " vs " + Str(V2(be["playerOffset"])));
        // The weave as Init's draws (at the floor's generation) left it.
        const Value& w = v["weave"];
        t.Add("weave: omegas, culture", Same(b->omegaX, Bits(w["ox"])) && Same(b->omegaY, Bits(w["oy"])) && static_cast<int>(b->tangleCulture) == w["culture"].AsInt(),
              "tangle " + std::to_string(s.id));
        b->originalAmplitudeX = Bits(w["oax"]);
        b->originalAmplitudeY = Bits(w["oay"]);
        b->amplitudeX = Bits(w["ax"]);
        b->amplitudeY = Bits(w["ay"]);
        b->index = Bits(w["idx"]);
        const Value& an = v["anim"];
        const int state = data.controller->StateByHash(static_cast<int32_t>(an["state"].AsInt()));
        if (state >= 0) s.animator->Mecanim()->Play(data.controller->states[static_cast<size_t>(state)].name, Bits(an["t"]));
        s.animator->set_speed(Bits(an["speed"]));
        s.animator->set_enabled(an["on"].b);
        s.enemy->enemySprite->sprite_ = an["sprite"].IsString() ? UnityEngine::Sprite::Named(an["sprite"].s) : nullptr;
        MaterialOf(s)->SetFloat(std::string("_FlashAmount"), Bits(v["flash"]));
        MaterialOf(s)->SetColor(std::string("_Color"), UnityEngine::Color{Bits(v["color"][0]), Bits(v["color"][1]), Bits(v["color"][2]), Bits(v["color"][3])});
        s.agent->OnDisable();
    }
    if (!skipped.empty()) {
        std::printf("the prefab's scripts not in the translation (presentation):");
        for (const std::string& k : skipped) std::printf(" %s", k.c_str());
        std::printf("\n");
    }
    for (const Value& name : start["agents"].arr) {
        bool found = false;
        for (TangleObj& s : tangles)
            if (name.IsInt() && s.id == name.AsInt()) {
                s.agent->OnEnable();
                found = true;
            }
        if (!found) std::printf("note: PolyNavAgent.allAgents has %s, not one of the tangles\n", name.Dump().c_str());
    }
    for (TangleObj& s : tangles) {
        const Value* v = nullptr;
        for (const Value& l : tangleLines)
            if (l["s"].AsInt() == s.id) v = &l;
        physics.SyncColliders(s.rb);
        physics.PlaceBody(s.rb, V2((*v)["rb"]["pos"]));
        s.rb->body_->SetVelocity({Bits((*v)["rb"]["vel"][0]), Bits((*v)["rb"]["vel"][1])});
        if (!(*v)["rb"]["awake"].b) s.rb->body_->Body()->SetAwake(false);
    }
    physics.Settle();
    unity::Random::SetState(State(f0["rng"]));

    const float fixedDt = Bits(start["fixedDeltaTime"]);
    UnityEngine::Time::fixedDeltaTime = fixedDt;
    double fixedClock = 0.0;
    unity::DelayedCalls& calls = scene.calls;
    const EnableClock enable = EnableEnemiesClock(start, f0, fixedDt);
    if (enable.probed)
        t.Add("EnableClockProbe: an Invoke made in a frame counts from Time.fixedTime (unity/DelayedCalls.h)", enable.invokeOnFixedClock,
              "EnableEnemies at " + std::to_string(enable.time) + ", the fixed step before at " + std::to_string(enable.fixedTime));
    if (enable.known) {
        calls.now = enable.time;
        calls.fixedNow = enable.fixedTime;
        for (TangleObj& s : tangles)
            if (!s.tangle->targetPlayer) s.tangle->StartTargetingPlayerAfterTime(Bits(start["targetDelay"]));
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
    auto* weapon = cs::gcnew<gen::Weapon>();
    cs::Root<gen::Weapon> weaponRoot(weapon);
    int rngElsewhere = 0;
    bool fixturesRecorded = false;
    for (const Value& fr : frames)
        if (!fr["steps"].arr.empty()) {
            fixturesRecorded = RecordsFixtures(fr);
            break;
        }
    if (!fixturesRecorded)
        std::printf("note: no \"fx\" in the steps (recorded before the oracle's FixtureProbe): the fixtures Unity made for objects "
                    "this replay doesn't hold are guessed from the weapon's hits\n");
    int tangleFirst = 0, agentFirst = 0, weaves = 0, weaveHits = 0;
    const bool gcEveryFrame = std::getenv("ML_GC_EVERY_FRAME") != nullptr;
    int gcRuns = 0;
    size_t gcFreed = 0;
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
            unity::Random::SetState(State(f["rng"]));
        }
        UnityEngine::Time::frameCount = f.Find("fc") ? static_cast<int>(f["fc"].AsInt()) : static_cast<int>(fn);
        const Value& events = f["events"];
        for (const Value& ev : events.arr) {
            if (fixturesRecorded) break;
            const std::string& kind = ev["k"].s;
            if (ev["step"].AsInt() == 0 &&
                (kind == "push" || kind == "dmgToEnemy" || (kind == "attackHit" && !ev["player"].b && ev["other"].IsString() && ev["other"].s == "Collider"))) {
                world.FixtureMadeElsewhere();
                break;
            }
        }
        // The fixed steps.
        for (size_t k = 0; k < f["steps"].size(); k++) {
            const Value& st = f["steps"][k];
            fixedClock = fixedClock > 0.0 ? fixedClock + static_cast<double>(fixedDt) : EngineFixedTime(Bits(st["ft"]), fixedDt);
            if (std::fabs(fixedClock - static_cast<double>(Bits(st["ft"]))) > 1e-4)
                std::printf("note: frame %lld step %zu: the fixed clock %.9f, the recording %.9f\n", fn, k, fixedClock, static_cast<double>(Bits(st["ft"])));
            calls.now = time;
            calls.fixedNow = fixedClock;
            calls.inFixedStep = true;
            UnityEngine::Time::deltaTime = fixedDt;
            UnityEngine::Time::time = static_cast<float>(fixedClock);
            scene.RunStarts();
            setHero(st["hero"]);
            for (const Value& ss : st["s"].arr) {
                TangleObj* o = find(static_cast<int>(ss[0].AsInt()));
                if (!o || o->Gone()) continue;
                const bool ok = Same(Position(*o), {Bits(ss[1]), Bits(ss[2])}) && Same(o->rb->body_->position(), {Bits(ss[3]), Bits(ss[4])}) &&
                                Same(o->rb->body_->velocity(), {Bits(ss[5]), Bits(ss[6])});
                t.Add("fixed step's start (transform, body)", ok,
                      "frame " + std::to_string(fn) + " step " + std::to_string(k) + " tangle " + std::to_string(ss[0].AsInt()) + ": port " + Str(Position(*o)) +
                          " body " + Str(o->rb->body_->position()) + " v " + Str(o->rb->body_->velocity()) + "; game " + Str({Bits(ss[1]), Bits(ss[2])}) + " body " +
                          Str({Bits(ss[3]), Bits(ss[4])}) + " v " + Str({Bits(ss[5]), Bits(ss[6])}));
            }
            if (fixturesRecorded && StepMadeFixtures(st)) world.FixtureMadeElsewhere();
            portDamage.clear();
            physics.Step(fixedDt);
            std::vector<std::pair<int, float>> gameDamage;
            for (const Value& ev : events.arr) {
                if (ev["step"].AsInt() != static_cast<long long>(k)) continue;
                const std::string& kind = ev["k"].s;
                TangleObj* o = find(static_cast<int>(ev["s"].AsInt()));
                if (kind == "dmgToPlayer") gameDamage.emplace_back(static_cast<int>(ev["s"].AsInt()), Bits(ev["str"]));
                if (!o) continue;
                if (kind == "dmgToEnemy") {
                    setHeroPosition(V2(ev["hero"]));
                    t.Add("health before a weapon hit", Same(Health(*o), Bits(ev["hp0"])), "frame " + std::to_string(fn) + " tangle " + std::to_string(ev["s"].AsInt()));
                    o->enemy->DealDamageToEnemy(Bits(ev["str"]), nullptr, static_cast<gen::Enemy_EnemyDamageReceivedType>(ev["type"].AsInt()));
                    t.Add("health after a weapon hit", Same(Health(*o), Bits(ev["hp1"])),
                          "frame " + std::to_string(fn) + " tangle " + std::to_string(ev["s"].AsInt()) + ": " + std::to_string(Health(*o)) + " vs " +
                              std::to_string(Bits(ev["hp1"])));
                } else if (kind == "push") {
                    t.Add("weapon push: the tangle where the game had it", Same(Position(*o), V2(ev["pos"])), Str(Position(*o)) + " vs " + Str(V2(ev["pos"])));
                    setHeroPosition(V2(ev["hero"]));
                    weapon->PushEnemyBackwards(o->go, Bits(ev["force"]));
                }
            }
            calls.UpdateFixed(fixedClock);
            scene.ResumeFixed();
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
        UnityEngine::Time::deltaTime = dt;
        UnityEngine::Time::time = static_cast<float>(time);
        const Value* tweenUdt = f.Find("dotweenUdt");
        scene.tweens.Update(dt, tweenUdt ? Bits(*tweenUdt) : f.Find("udt") ? Bits(f["udt"]) : dt);
        DG::Tweening::Running::Prune();
        scene.RunStarts();
        for (const Value& u : f["updates"].arr) {
            TangleObj* o = find(static_cast<int>(u["s"].AsInt()));
            if (!o) continue;
            setHeroPosition(V2(u["hero"]));
            if (!SameState(unity::Random::GetState(), State(u["rng0"]))) {
                rngElsewhere++;
                unity::Random::SetState(State(u["rng0"]));
            }
            t.Add("Update called on an enabled tangle", !o->Gone() && o->tangle->get_isActiveAndEnabled(), "frame " + std::to_string(fn));
            o->tangle->cs_Update();
            t.Add("Random draws in the tangles' Update", SameState(unity::Random::GetState(), State(u["rng1"])),
                  "frame " + std::to_string(fn) + " tangle " + std::to_string(u["s"].AsInt()));
            unity::Random::SetState(State(u["rng1"]));
        }
        calls.Update(time);
        scene.ResumeYieldNull();
        scene.UpdateAnimators(dt);
        nav->cs_LateUpdate();
        // The LateUpdates, the Tangles' and their agents', in the game's order.
        std::map<int, char> firstOf;
        for (const Value& l : f["late"].arr) {
            const int id = static_cast<int>(l["s"].AsInt());
            TangleObj* o = find(id);
            if (!o) continue;
            const bool isTangle = l["k"].s == "t";
            if (!firstOf.count(id)) firstOf[id] = isTangle ? 't' : 'a';
            else if (firstOf[id] != (isTangle ? 't' : 'a') && firstOf[id] != 'x') {
                (firstOf[id] == 't' ? tangleFirst : agentFirst)++;
                firstOf[id] = 'x';
            }
            const std::string at2 = "frame " + std::to_string(fn) + " tangle " + std::to_string(id);
            t.Add(isTangle ? "LateUpdate's start: agent velocity (Tangle's)" : "LateUpdate's start: agent velocity (agent's)", Same(o->agent->velocity, V2(l["v0"])),
                  at2 + ": port " + Str(o->agent->velocity) + " game " + Str(V2(l["v0"])));
            if (!isTangle) {
                if (!o->Gone() && o->agent->get_isActiveAndEnabled()) o->agent->cs_LateUpdate();
                t.Add("agent LateUpdate: velocity after", Same(o->agent->velocity, V2(l["v1"])), at2 + ": port " + Str(o->agent->velocity) + " game " + Str(V2(l["v1"])));
                continue;
            }
            gen::TangleBehaviour* b = o->tangle;
            setHeroPosition(V2(l["hero"]));
            t.Add("Tangle LateUpdate's start (its position, target, index)",
                  Same(Position(*o), V2(l["pos"])) && b->targetPlayer == l["target"].b && Same(b->index, Bits(l["i0"])) && b->enabled_ && !o->Gone(),
                  at2 + ": port " + Str(Position(*o)) + " index " + std::to_string(b->index) + " game " + Str(V2(l["pos"])) + " " + std::to_string(Bits(l["i0"])));
            weave = Weave{};
            inLateUpdate = true;
            if (b->get_isActiveAndEnabled()) b->cs_LateUpdate();
            inLateUpdate = false;
            const bool taken = l["taken"].b;
            t.Add("Tangle LateUpdate: the weave ran (target, past 1.5 tiles)", weave.ran == taken,
                  at2 + ": port " + std::to_string(weave.ran) + " game " + std::to_string(taken));
            t.Add("Tangle LateUpdate: index, amplitudes", Same(b->index, Bits(l["i1"])) && Same(b->amplitudeX, Bits(l["ax"])) && Same(b->amplitudeY, Bits(l["ay"])),
                  at2 + ": port " + std::to_string(b->index) + " " + Str({b->amplitudeX, b->amplitudeY}) + " game " + std::to_string(Bits(l["i1"])) + " " +
                      Str({Bits(l["ax"]), Bits(l["ay"])}));
            if (taken && weave.ran) {
                weaves++;
                const Value& gameHit = l["hit"];
                const unity::RaycastHit2D& ph = weave.hit;
                if (!gameHit.IsNull()) weaveHits++;
                t.Add("Tangle LateUpdate: the weave's direction new Vector2(x, y)", Same(weave.direction, V2(l["d"])),
                      at2 + ": port " + Str(weave.direction) + " game " + Str(V2(l["d"])));
                const bool sameCollider = gameHit.IsNull() ? ph.collider == nullptr : ph.collider && ph.collider->path == gameHit.s;
                t.Add("Tangle LateUpdate: Raycast's collider", sameCollider,
                      at2 + ": port " + (ph.collider ? ph.collider->path : std::string("none")) + " game " + (gameHit.IsNull() ? std::string("none") : gameHit.s) +
                          " from " + Str(Position(*o)) + " along " + Str(weave.direction));
                if (sameCollider && ph.collider) {
                    t.Info("Raycast's hit: point, normal (unused by the game)", Same(ph.point, V2(l["point"])) && Same(ph.normal, V2(l["normal"])),
                           at2 + ": port " + Str(ph.point) + " " + Str(ph.normal) + " game " + Str(V2(l["point"])) + " " + Str(V2(l["normal"])));
                    t.Info("Raycast's hit: distance, fraction (unused by the game)", Same(ph.distance, Bits(l["dist"])) && Same(ph.fraction, Bits(l["frac"])),
                           at2 + ": port " + std::to_string(ph.distance) + " " + std::to_string(ph.fraction) + " game " + std::to_string(Bits(l["dist"])) + " " +
                               std::to_string(Bits(l["frac"])));
                }
            }
            t.Add("Tangle LateUpdate: agent velocity after", Same(o->agent->velocity, V2(l["v1"])), at2 + ": port " + Str(o->agent->velocity) + " game " + Str(V2(l["v1"])));
        }
        scene.ProcessDestroys();
        std::vector<Vector2> gameDead;
        for (const Value& ev : events.arr)
            if (ev["k"].s == "deadSprite") gameDead.push_back(V2(ev["pos"]));
        if (!gameDead.empty() || !portDeadSprites.empty()) {
            bool same = gameDead.size() == portDeadSprites.size();
            for (size_t k = 0; same && k < gameDead.size(); k++) same = Same(gameDead[k], portDeadSprites[k]);
            t.Add("dead sprites spawned (where)", same, "frame " + std::to_string(fn) + ": port " + std::to_string(portDeadSprites.size()) + " game " + std::to_string(gameDead.size()));
        }
        portDeadSprites.clear();

        // The frame's end, per Tangle.
        for (const Value& v : f["tangles"].arr) {
            const int id = static_cast<int>(v["s"].AsInt());
            TangleObj* o = find(id);
            if (!o) continue;
            const std::string at2 = "frame " + std::to_string(fn) + " tangle " + std::to_string(id);
            t.Add("destroyed", o->Gone() == v["gone"].b, at2);
            if (v["gone"].b || o->Gone()) continue;
            const Value& ag = v["agent"];
            const Value& an = v["anim"];
            const Value& be = v["behaviour"];
            const Value& w = v["weave"];
            t.Add("position (world, local)", Same(Position(*o), V2(v["pos"])) && Same(Local(*o), V2(v["local"])),
                  at2 + ": port " + Str(Position(*o)) + " game " + Str(V2(v["pos"])));
            const Value& rb = v["rb"];
            unity::Rigidbody2D* body = o->rb->body_;
            t.Add("Rigidbody2D (position, velocity, awake)",
                  Same(body->position(), {Bits(rb[0]), Bits(rb[1])}) && Same(body->velocity(), {Bits(rb[2]), Bits(rb[3])}) && body->IsAwake() == v["awake"].b,
                  at2 + ": port " + Str(body->position()) + " v " + Str(body->velocity()) + " game " + Str({Bits(rb[0]), Bits(rb[1])}) + " v " +
                      Str({Bits(rb[2]), Bits(rb[3])}));
            std::vector<Vector2> gamePath;
            for (size_t k = 0; k + 1 < ag["path"].size(); k += 2) gamePath.push_back({Bits(ag["path"][k]), Bits(ag["path"][k + 1])});
            cs::List<Vector2>* portPath = o->agent->_activePath;
            bool path = static_cast<int32_t>(gamePath.size()) == portPath->get_Count();
            for (size_t k = 0; path && k < gamePath.size(); k++) path = Same(gamePath[k], portPath->get_Item(static_cast<int32_t>(k)));
            t.Add("agent velocity", Same(o->agent->velocity, V2(ag["vel"])), at2 + ": port " + Str(o->agent->velocity) + " game " + Str(V2(ag["vel"])));
            t.Add("agent goal, path, pending, acceleration",
                  path && Same(o->agent->_primeGoal, V2(ag["goal"])) && o->agent->requests == ag["req"].AsInt() && Same(o->agent->accelerationValue, Bits(ag["acc"])),
                  at2 + ": port goal " + Str(o->agent->_primeGoal) + " path " + std::to_string(portPath->get_Count()) + " req " + std::to_string(o->agent->requests) +
                      "; game " + Str(V2(ag["goal"])) + " " + std::to_string(gamePath.size()) + " " + std::to_string(ag["req"].AsInt()));
            const unity::AnimatorStateInfo info = o->go->get_activeInHierarchy() ? o->animator->Mecanim()->GetCurrentAnimatorStateInfo() : unity::AnimatorStateInfo{};
            t.Add("Animator state, normalized time (bits)",
                  o->animator->enabled_ == an["on"].b &&
                      (!an["on"].b || (info.shortNameHash == static_cast<int32_t>(an["state"].AsInt()) && Same(info.normalizedTime, Bits(an["t"])))),
                  at2 + ": port t " + std::to_string(info.normalizedTime) + " game " + std::to_string(Bits(an["t"])));
            t.Add("sprite", an["sprite"].IsString() && SpriteOf(*o) == an["sprite"].s, at2 + ": port " + SpriteOf(*o) + " game " + an["sprite"].Dump());
            gen::EnemyStats* es = o->enemy->enemyStats;
            t.Add("health, alive, invincible", Same(es->_currentHealth, Bits(v["hp"])) && es->_isAlive == v["alive"].b && es->_invincible == v["inv"].b,
                  at2 + ": port " + std::to_string(es->_currentHealth) + " game " + std::to_string(Bits(v["hp"])));
            gen::TangleBehaviour* b = o->tangle;
            t.Add("behaviour (state, targeting, canMove, lapse, damaged, enabled, layer)",
                  static_cast<int>(b->enemyState) == be["es"].AsInt() && b->targetPlayer == be["target"].b && b->canMove == be["canMove"].b &&
                      Same(b->_lapse, Bits(be["lapse"])) && b->playingDamagedEffect == be["dmgFx"].b && b->enabled_ == be["enabled"].b &&
                      o->go->layer_ == v["layer"].AsInt(),
                  at2 + ": port lapse " + std::to_string(b->_lapse) + " target " + std::to_string(b->targetPlayer) + " game " + std::to_string(Bits(be["lapse"])) +
                      " " + std::to_string(be["target"].b));
            t.Add("weave (amplitudes, index)",
                  Same(b->originalAmplitudeX, Bits(w["oax"])) && Same(b->originalAmplitudeY, Bits(w["oay"])) && Same(b->amplitudeX, Bits(w["ax"])) &&
                      Same(b->amplitudeY, Bits(w["ay"])) && Same(b->index, Bits(w["idx"])),
                  at2 + ": port index " + std::to_string(b->index) + " game " + std::to_string(Bits(w["idx"])));
            UnityEngine::Material* mat = MaterialOf(*o);
            const float flash = mat->Float("_FlashAmount");
            const UnityEngine::Color c = mat->ColorOf("_Color");
            const float col[4] = {c.r, c.g, c.b, c.a};
            bool color = Same(flash, Bits(v["flash"]));
            for (int k = 0; k < 4; k++) color = color && Same(col[k], Bits(v["color"][static_cast<size_t>(k)]));
            const std::string colours = at2 + ": port flash " + std::to_string(flash) + " game " + std::to_string(Bits(v["flash"]));
            if (!es->_isAlive && !f.Find("dotweenUdt")) {
                bool near = std::fabs(flash - Bits(v["flash"])) <= 0.03f;
                for (int k = 0; k < 4; k++) near = near && std::fabs(col[k] - Bits(v["color"][static_cast<size_t>(k)])) <= 0.03f;
                t.Add("material in the death fade (DOTween clock unrecorded: within 0.03)", near, colours);
                t.Info("material in the death fade, bit for bit (needs the DOTween clock)", color, colours);
            } else {
                t.Add("material (_FlashAmount, _Color)", color, colours);
            }
            if (t.debug) std::printf("frame %lld tangle %d: %s %s | game %s\n", fn, id, Str(Position(*o)).c_str(), SpriteOf(*o).c_str(), Str(V2(v["pos"])).c_str());
        }
        if (gcEveryFrame) {
            gcFreed += cs::CurrentHeap().Collect();
            gcRuns++;
        }
    }
    std::printf("\n");
    t.Print();
    if (gcEveryFrame) std::printf("collected at %d frame ends: %zu objects freed, %zu left\n", gcRuns, gcFreed, cs::CurrentHeap().Count());
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
    Data data;
    try {
        if (!LoadData(data)) {
            std::printf("no extracted data for the Tangle (ML_DATA, default data/extracted)\n");
            return 1;
        }
        return Replay(file, data);
    } catch (const cs::Thrown& e) {
        std::printf("the translated code threw %s\n", e.TypeName());
        return 1;
    }
}
