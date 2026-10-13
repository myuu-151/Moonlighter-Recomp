// The BabySlime recordings (data/oracle/slime.jsonl, slime_bis.jsonl) replayed against the
// game's own code translated (tools/translate: BabySlimeBehaviour, EnemyBehaviour, Enemy,
// EnemyStats, EnemyAttackCollisionDetection, PolyNavAgent, PolyNav2D, AStar, the stats' JSON
// reader...), running on the Unity facade (recomp/src/bind) over the hand-written Unity layer.
//
// The replay is the hand port's (tests/enemy_replay.h, slime_test: the same recording, the same
// rows, the same pass criterion) with the translated objects in place of game::Enemy: the room's
// slimes made from their prefab (prefabs.json, animators/BabySlime.json) under the room's
// objects, Init'ed by the game's code (EnemyStats.Init from enemy_stats.json through
// EnemyStatManager), their recorded state put back into the translated fields, then every frame
// as the game ran it: the fixed steps (the hero's body as each found it, the physics with its
// trigger messages to EnemyAttackCollisionDetection, the weapon's hits and pushes where they
// landed: Enemy.DealDamageToEnemy, Weapon.PushEnemyBackwards), DOTween, the slimes' Updates in
// the game's order (each from the Random state and the hero position it had), the delayed calls
// (Invoke, WaitForSeconds, Destroy(obj, t)), the coroutines on yield null (the paths), the
// Animators, PolyNav2D's and the agents' LateUpdates. Compared bit for bit per frame and slime as
// the hand port compares them. The scene's other services are the stand-ins of
// recomp/tests/translated/scene/Standins.cpp.
//
// Usage: slime_translated_test [slime.jsonl]   (ML_DATA: the extracted data, default data/extracted;
//        ML_SLIME_DEBUG=1: every mismatch)

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
#include "scene/Standins.h"
#include "unity/Animator.h"
#include "unity/Physics2D.h"
#include "unity/PhysicsWorld.h"
#include "unity/Random.h"
#include "util/Json.h"

namespace {

using json::Value;
using unity::Vector2;
using UnityEngine::GameObject;
using UnityEngine::Transform;

// ---- the recording's values (as tests/enemy_replay.h reads them) ---------------------------------------

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
Vector2 XY(UnityEngine::Vector3 v) { return {v.x, v.y}; }
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
std::string ReadFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
cs::String* S(const std::string& s) { return cs::String::FromUtf8(s); }

struct Tally {
    std::map<std::string, std::pair<int, int>> rows;
    std::map<std::string, int> shownOf;
    std::vector<std::string> order, info;
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

// A hole region's outline back to the tiles it covers (tests/enemy_replay.h's).
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

bool HerosTrigger(const std::string& name)
{
    const std::string area = "ActionArea";
    return name == "Collider" || (name.size() >= area.size() && name.compare(name.size() - area.size(), area.size(), area) == 0);
}

std::string Leaf(const std::string& path)
{
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// ---- the translated slimes ------------------------------------------------------------------------------

struct Slime {
    int id = 0;
    GameObject* go = nullptr;
    gen::Enemy* enemy = nullptr;
    gen::EnemyBehaviour* behaviour = nullptr;
    gen::PolyNavAgent* agent = nullptr;
    UnityEngine::Rigidbody2D* rb = nullptr;
    UnityEngine::Animator* animator = nullptr;
    GameObject* attack = nullptr;
    UnityEngine::BoxCollider2D* attackBox = nullptr;
    bool Gone() const { return !go || go->destroyed_; }
};

Vector2 Position(const Slime& s) { return XY(s.go->transform_->get_position()); }
Vector2 Local(const Slime& s) { return XY(s.go->transform_->localPosition_); }
float Health(const Slime& s) { return s.enemy->enemyStats->_currentHealth; }
std::string SpriteOf(const Slime& s) { return s.enemy->enemySprite ? s.enemy->enemySprite->SpriteName() : std::string(); }
UnityEngine::Material* MaterialOf(const Slime& s) { return s.enemy->enemySprite->get_material(); }

bool SameAttack(const Slime& s, bool on, Vector2 size, Vector2 offset, Vector2 position)
{
    return s.attackBox->enabled_ == on && Same(s.attackBox->size_, size) && Same(s.attackBox->offset_, offset) &&
           Same(XY(s.attack->transform_->localPosition_), position);
}
std::string AttackStr(const Slime& s)
{
    return std::string(s.attackBox->enabled_ ? "on" : "off") + " size " + Str(s.attackBox->size_) + " offset " + Str(s.attackBox->offset_) + " at " +
           Str(XY(s.attack->transform_->localPosition_));
}

struct Data {
    std::string dir;
    Value enemyStats, physics2d, soundGroups;
    std::unique_ptr<unity::AnimatorController> controller;
    const game::Prefab* prefab = nullptr;
};

bool LoadData(Data& d)
{
    d.dir = std::getenv("ML_DATA") ? std::getenv("ML_DATA") : "data/extracted";
    const std::string prefabs = ReadFile(d.dir + "/prefabs.json"), anim = ReadFile(d.dir + "/animators/BabySlime.json"),
                      physics = ReadFile(d.dir + "/physics2d.json"), sounds = ReadFile(d.dir + "/sound_groups.json");
    if (prefabs.empty() || anim.empty() || ReadFile(d.dir + "/constants.json").empty() || ReadFile(d.dir + "/enemy_stats.json").empty()) return false;
    game::PrefabLibrary::Load(json::Parse(prefabs));
    d.prefab = game::PrefabLibrary::Find("BabySlime", "enemies_golem");
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
        std::printf("(no %s: skipping the oracle comparison; record it with -oracle-slime)\n", file.c_str());
        return 0;
    }
    Value start;
    std::vector<Value> enemyLines, frames, maps, probes, colliders;
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
    }
    if (start.IsNull() || enemyLines.empty() || frames.empty() || maps.empty()) {
        std::printf("%s: no start, enemies, frames or map\n", file.c_str());
        return 1;
    }
    Tally t;
    t.debug = std::getenv("ML_SLIME_DEBUG") != nullptr;
    std::printf("recording: room %s, %zu %s, %zu frames (translated)\n", start["room"].s.c_str(), enemyLines.size(), start["enemy"].s.c_str(), frames.size());

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
        else if (c["type"].s == "PolygonCollider2D" && c["tag"].s == "Hole") roomColliders.Add(HoleFrom(c));
    }
    for (const unity::Collider2D& c : roomColliders.Colliders()) {
        std::string body = "none";
        for (const Value& v : colliders)
            if (v["path"].s == c.path && v.Find("body")) body = v["body"].s;
        world.AddCollider(c, body == "Kinematic" ? unity::RigidbodyType2D::Kinematic : unity::RigidbodyType2D::Static, body != "none");
    }
    unity::Physics2D roomTriggers;
    for (const Value& c : colliders) {
        const std::string body = c.Find("body") ? c["body"].s : "none";
        if (body != "none" || !c["trigger"].b || !c["enabled"].b || c["type"].s != "BoxCollider2D") continue;
        roomTriggers.Add(ColliderFrom(c));
    }
    for (const unity::Collider2D& c : roomTriggers.Colliders()) world.AddCollider(c);
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
    bind::PhysicsScene physics(world, scene);
    physics.queries = &roomColliders;

    // The objects the colliders' messages name: the room's (by their objects' names, tags and
    // layers) and the hero's body (the "Player").
    auto standIn = [&](const std::string& name, const std::string& tag, int layer, bool trigger) {
        GameObject* g = GameObject::Create(name);
        if (!tag.empty()) g->tag_ = S(tag);
        g->layer_ = layer;
        auto* c = cs::gcnew<UnityEngine::BoxCollider2D>();
        c->isTrigger_ = trigger;
        g->Attach(c);
        return c;
    };
    for (b2Body* b = world.World().GetBodyList(); b; b = b->GetNext())
        for (b2Fixture* f = b->GetFixtureList(); f; f = f->GetNext())
            if (const unity::Collider2D* c = world.ColliderOf(f)) physics.Register(f, standIn(Leaf(c->path), c->tag, c->layer, c->trigger));

    // The hero: HeroMerchant (HeroMerchant.Instance finds it), his controller and stats, his body's collider.
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

    // ---- the game's services the slimes reach ----
    const Value& sc = start["stats"];
    if (sc["ngp"].b || sc["dlc"].b) {
        std::printf("a New Game+ or DLC recording: GameManager's game slot isn't stood in for\n");
        return 1;
    }
    standins::S().difficulty = static_cast<int>(sc["difficulty"].AsInt());
    standins::S().afternoon = sc["afternoon"].b;
    standins::S().timeManager = cs::gcnew<gen::TimeManagerV2>();
    standins::S().gameManager = nullptr; // (its slot: not New Game+, no DLC)
    gen::CameraManager::_instance = cs::gcnew<gen::CameraManager>();
    gen::HUDManager::Instance = cs::gcnew<gen::HUDManager>();
    gen::HUDManager::Instance->minimap = cs::gcnew<gen::MinimapPanel>();
    game::MasterAudio audio;
    if (!data.soundGroups.IsNull() && audio.Load(data.soundGroups, {"Sounds - GolemDungeon", "Sounds - GeneralDungeons"}))
        DarkTonic::MasterAudio::MasterAudio::engine = &audio;
    else std::printf("(no sound_groups.json: MasterAudio's draws not made)\n");

    // The room: Dungeon / <room> (DungeonRoom) / DungeonLevelEnemies / EnemySpawn / the slime.
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
    GameObject* levelEnemies = GameObject::Create("DungeonLevelEnemies", roomGo->transform_);
    at(levelEnemies, V2(chain0[2]));
    auto* dungeonManager = cs::New<gen::DungeonManager>();
    dungeonManager->intermediateInfo.dungeonType = gen::DungeonManager_DungeonType::Normal;
    dungeonManager->intermediateInfo.dungeonCulture = static_cast<gen::ItemMaster_Culture>(sc["dungeonCulture"].AsInt());
    dungeonManager->intermediateInfo.dungeonLevel = static_cast<int32_t>(sc["level"].AsInt());
    dungeonManager->currentDungeonRoom = room;
    gen::DungeonManager::_instance = dungeonManager;

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

    // ---- the slimes ----
    std::vector<Slime> slimes;
    auto byId = [&](int id) -> Slime* {
        for (Slime& s : slimes)
            if (s.id == id) return &s;
        return nullptr;
    };
    auto idOf = [&](const gen::Enemy* e) {
        for (const Slime& s : slimes)
            if (s.enemy == e) return s.id;
        return -1;
    };
    std::vector<std::pair<int, float>> portDamage;
    std::vector<Vector2> portDeadSprites;
    standins::S().dealDamageToPlayer = [&](float strength, cs::String*, int, gen::Enemy* e) { portDamage.emplace_back(idOf(e), strength); };
    standins::S().spawnEnemyDeadSprite = [&](gen::DungeonRoom*, UnityEngine::Sprite*, UnityEngine::Vector3 p) { portDeadSprites.push_back(XY(p)); };
    std::map<int, std::vector<std::string>> portAnim;
    std::map<int, std::vector<const Value*>> gameAnim;
    long long animFrame = -1;
    int animResynced = 0;
    std::vector<std::string> skipped;
    for (const Value& v : enemyLines) {
        Slime s;
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
        s.behaviour = s.go->GetComponent<gen::EnemyBehaviour*>();
        s.agent = s.go->GetComponent<gen::PolyNavAgent*>();
        s.rb = s.go->GetComponent<UnityEngine::Rigidbody2D*>();
        s.animator = s.go->GetComponent<UnityEngine::Animator*>();
        s.attack = bind::FindBelow(s.go, "AttackCollider");
        s.attackBox = s.attack ? s.attack->GetComponent<UnityEngine::BoxCollider2D*>() : nullptr;
        if (!s.enemy || !s.behaviour || !s.agent || !s.rb || !s.animator || !s.attackBox) {
            std::printf("the BabySlime prefab didn't make its Enemy, behaviour, agent, body, Animator and attack trigger\n");
            return 1;
        }
        const int id = s.id;
        s.animator->onEvent = [&, id](UnityEngine::Animator&, const unity::AnimationEvent& ev, bool after) {
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
                  "frame " + std::to_string(animFrame) + " enemy " + std::to_string(id));
            unity::Random::SetState(State(game["rng1"]));
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
                  Same(s.go->GetComponent<UnityEngine::BoxCollider2D*>()->size_.x, Bits(v["box"][2])) &&
                  (v.Find("attack") || Same(s.attackBox->size_.x, Bits(v["attackBox"][2]))),
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
        if (const Value* ac = v.Find("attack")) {
            t.Add("attack trigger (prefab)", (*ac)["layer"].AsInt() == s.attack->layer_, "enemy " + std::to_string(s.id));
            s.attackBox->enabled_ = (*ac)["on"].b;
            s.attack->activeSelf_ = (*ac)["active"].b;
            s.attackBox->size_ = V2((*ac)["size"]);
            s.attackBox->offset_ = V2((*ac)["offset"]);
            s.attack->transform_->localPosition_ = UnityEngine::Vector3{Bits((*ac)["pos"][0]), Bits((*ac)["pos"][1]), 0.0f};
        }
        s.enemy->enemySprite->sprite_ = an["sprite"].IsString() ? UnityEngine::Sprite::Named(an["sprite"].s) : nullptr;
        MaterialOf(s)->SetFloat(std::string("_FlashAmount"), Bits(v["flash"]));
        MaterialOf(s)->SetColor(std::string("_Color"),
                                UnityEngine::Color{Bits(v["color"][0]), Bits(v["color"][1]), Bits(v["color"][2]), Bits(v["color"][3])});
        s.agent->OnDisable(); // (re-enabled below in allAgents' order)
        slimes.push_back(s);
    }
    if (!skipped.empty()) {
        std::printf("the prefab's scripts not in the translation (presentation):");
        for (const std::string& k : skipped) std::printf(" %s", k.c_str());
        std::printf("\n");
    }
    for (const Value& name : start["agents"].arr) {
        bool found = false;
        for (Slime& s : slimes)
            if (name.IsInt() && s.id == name.AsInt()) {
                s.agent->OnEnable();
                found = true;
            }
        if (!found) std::printf("note: PolyNavAgent.allAgents has %s, not one of the enemies\n", name.Dump().c_str());
    }
    for (Slime& s : slimes) {
        const Value* v = nullptr;
        for (const Value& l : enemyLines)
            if (l["s"].AsInt() == s.id) v = &l;
        physics.SyncColliders(s.rb);
        physics.PlaceBody(s.rb, V2((*v)["rb"]["pos"]));
        s.rb->body_->SetVelocity({Bits((*v)["rb"]["vel"][0]), Bits((*v)["rb"]["vel"][1])});
        if (!(*v)["rb"]["awake"].b) s.rb->body_->Body()->SetAwake(false);
    }
    unity::Random::SetState(State(f0["rng"]));

    const float fixedDt = Bits(start["fixedDeltaTime"]);
    UnityEngine::Time::fixedDeltaTime = fixedDt;
    double fixedClock = 0.0;
    unity::DelayedCalls& calls = scene.calls;
    // The targeting a second after EnableEnemies (tests/enemy_replay.h explains the clocks).
    if (const Value* et = start.Find("enableTime"); et && !et->IsNull()) {
        const double enabled = static_cast<double>(Bits(*et));
        calls.now = enabled;
        if (!f0["steps"].arr.empty()) {
            const double first = static_cast<double>(Bits(f0["steps"][0]["ft"]));
            for (int k = 1; k < 1000; k++) {
                const double at2 = first - static_cast<double>(k) * static_cast<double>(fixedDt);
                if (at2 < enabled - 0.005) break;
                if (static_cast<float>(at2) == Bits(*et)) {
                    calls.inFixedStep = true;
                    calls.fixedNow = at2;
                    std::printf("EnableEnemies ran in a fixed step's pass (%d steps before the first recorded)\n", k);
                    break;
                }
            }
        }
        for (Slime& s : slimes)
            if (!s.behaviour->targetPlayer) s.behaviour->StartTargetingPlayerAfterTime(Bits(start["targetDelay"]));
        calls.inFixedStep = false;
    }
    auto setHero = [&](const Value& hs) {
        world.SetLayer(heroBody, static_cast<int>(hs[6].AsInt()));
        world.SyncTransform(heroBody, {Bits(hs[0]), Bits(hs[1])});
        heroBody.SetVelocity({Bits(hs[4]), Bits(hs[5])});
        if (heroBody.IsAwake() != hs[7].b) heroBody.Body()->SetAwake(hs[7].b);
    };
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

    // The weapon's push (Weapon.PushEnemyBackwards, the hero's code, translated).
    auto* weapon = cs::gcnew<gen::Weapon>();

    int rngElsewhere = 0;
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
        const Value& events = f["events"];
        for (const Value& ev : events.arr) {
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
            fixedClock = fixedClock > 0.0 ? fixedClock + static_cast<double>(fixedDt) : static_cast<double>(Bits(st["ft"]));
            calls.now = time;
            calls.fixedNow = fixedClock;
            calls.inFixedStep = true;
            UnityEngine::Time::deltaTime = fixedDt;
            UnityEngine::Time::time = static_cast<float>(fixedClock);
            setHero(st["hero"]);
            for (const Value& ss : st["s"].arr) {
                Slime* s = byId(static_cast<int>(ss[0].AsInt()));
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
            portDamage.clear();
            physics.Step(fixedDt);
            // The attack triggers' OnTriggerEnter2D in this step (but the hero's own triggers').
            struct Hit {
                int id;
                bool player, otherTrigger;
            };
            std::vector<Hit> hits;
            for (const bind::PhysicsScene::Hit& hit : physics.LastHits())
                for (const Slime& s : slimes) {
                    UnityEngine::Collider2D* other = hit.a == s.attackBox ? hit.b : hit.b == s.attackBox ? hit.a : nullptr;
                    if (!other || s.Gone()) continue;
                    hits.push_back({s.id, other == heroCollider, other->isTrigger_});
                }
            for (int pass = 0; pass < 2; pass++) {
                const bool triggers = pass == 1;
                std::vector<std::pair<int, bool>> mine, theirs;
                for (const Hit& hit : hits)
                    if (hit.otherTrigger == triggers) mine.emplace_back(hit.id, hit.player);
                for (const Value& ev : events.arr) {
                    if (ev["k"].s != "attackHit" || ev["step"].AsInt() != static_cast<long long>(k)) continue;
                    const std::string other = ev["other"].IsString() ? ev["other"].s : "";
                    if (HerosTrigger(other) || RoomTrigger(other) != triggers) continue;
                    theirs.emplace_back(static_cast<int>(ev["s"].AsInt()), ev["player"].b);
                }
                if (mine.empty() && theirs.empty()) continue;
                std::sort(mine.begin(), mine.end());
                std::sort(theirs.begin(), theirs.end());
                std::string which = "frame " + std::to_string(fn) + " step " + std::to_string(k) + ": port";
                for (const auto& hit : mine) which += " " + std::to_string(hit.first) + (hit.second ? "/hero" : "/other");
                which += ", game";
                for (const auto& hit : theirs) which += " " + std::to_string(hit.first) + (hit.second ? "/hero" : "/other");
                if (triggers) t.Info("attack trigger: OnTriggerEnter2D on the room's triggers (breakables)", mine == theirs, which);
                else t.Add("attack trigger: OnTriggerEnter2D (whose, on the hero or a collider)", mine == theirs, which);
            }
            // The weapon's hits in this step, in the game's order; the slimes' own hits on him.
            std::vector<std::pair<int, float>> gameDamage;
            for (const Value& ev : events.arr) {
                if (ev["step"].AsInt() != static_cast<long long>(k)) continue;
                const std::string& kind = ev["k"].s;
                Slime* s = byId(static_cast<int>(ev["s"].AsInt()));
                if (kind == "dmgToPlayer") gameDamage.emplace_back(static_cast<int>(ev["s"].AsInt()), Bits(ev["str"]));
                if (!s) continue;
                if (kind == "dmgToEnemy") {
                    setHeroPosition(V2(ev["hero"]));
                    t.Add("health before a weapon hit", Same(Health(*s), Bits(ev["hp0"])), "frame " + std::to_string(fn) + " enemy " + std::to_string(ev["s"].AsInt()));
                    if (ev.Find("recv") && ev["recv"].IsString()) std::printf("note: a hit through a hurtbox (%s): not a slime's\n", ev["recv"].s.c_str());
                    s->enemy->DealDamageToEnemy(Bits(ev["str"]), nullptr, static_cast<gen::Enemy_EnemyDamageReceivedType>(ev["type"].AsInt()));
                    t.Add("health after a weapon hit", Same(Health(*s), Bits(ev["hp1"])),
                          "frame " + std::to_string(fn) + " enemy " + std::to_string(ev["s"].AsInt()) + ": " + std::to_string(Health(*s)) + " vs " +
                              std::to_string(Bits(ev["hp1"])));
                } else if (kind == "push") {
                    t.Add("weapon push: the enemy where the game had it", Same(Position(*s), V2(ev["pos"])), Str(Position(*s)) + " vs " + Str(V2(ev["pos"])));
                    setHeroPosition(V2(ev["hero"]));
                    weapon->PushEnemyBackwards(s->go, Bits(ev["force"]));
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
        // The frame: DOTween, the Updates in the game's order, the delayed calls, the coroutines,
        // the Animators, the LateUpdates.
        calls.now = time;
        audio.now = time;
        audio.frameCount = f.Find("fc") ? static_cast<int>(f["fc"].AsInt()) : static_cast<int>(fn);
        UnityEngine::Time::deltaTime = dt;
        UnityEngine::Time::time = static_cast<float>(time);
        UnityEngine::Time::frameCount = audio.frameCount;
        const Value* tweenUdt = f.Find("dotweenUdt");
        scene.tweens.Update(dt, tweenUdt ? Bits(*tweenUdt) : f.Find("udt") ? Bits(f["udt"]) : dt);
        DG::Tweening::Running::Prune();
        for (const Value& u : f["updates"].arr) {
            Slime* s = byId(static_cast<int>(u["s"].AsInt()));
            if (!s) continue;
            setHeroPosition(V2(u["hero"]));
            if (!SameState(unity::Random::GetState(), State(u["rng0"]))) {
                rngElsewhere++;
                unity::Random::SetState(State(u["rng0"]));
            }
            t.Add("Update called on an enabled enemy", !s->Gone() && s->behaviour->get_isActiveAndEnabled(), "frame " + std::to_string(fn));
            s->behaviour->cs_Update();
            t.Add("Random draws in the enemies' Update", SameState(unity::Random::GetState(), State(u["rng1"])),
                  "frame " + std::to_string(fn) + " enemy " + std::to_string(u["s"].AsInt()));
            unity::Random::SetState(State(u["rng1"]));
        }
        calls.Update(time);
        scene.ResumeYieldNull();
        portAnim.clear();
        gameAnim.clear();
        animFrame = fn;
        for (const Value& ev : events.arr)
            if (ev["k"].s == "anim" && !(ev.Find("caller") && ev["caller"].IsString())) gameAnim[static_cast<int>(ev["s"].AsInt())].push_back(&ev);
        scene.UpdateAnimators(dt);
        for (Slime& s : slimes) {
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
        nav->cs_LateUpdate();
        for (const Value& id : f["late"].arr) {
            Slime* s = byId(static_cast<int>(id.AsInt()));
            if (s && !s->Gone() && s->agent->get_isActiveAndEnabled()) s->agent->cs_LateUpdate();
        }
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
        scene.ProcessDestroys();

        // The frame's end, per slime.
        for (const Value& v : (f.Find("enemies") ? f["enemies"] : f["slimes"]).arr) {
            const int id = static_cast<int>(v["s"].AsInt());
            Slime* sp = byId(id);
            if (!sp) continue;
            Slime& s = *sp;
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
            const unity::AnimatorStateInfo info = s.animator->Mecanim()->GetCurrentAnimatorStateInfo();
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
    }
    std::printf("\n");
    t.Print();
    std::printf("Random drawn by the game's other scripts (resynced) %d times\n", rngElsewhere);
    if (animResynced) std::printf("Random resynced before an animation event %d times\n", animResynced);
    std::printf("\n%s\n", t.Ok() ? "ALL MATCH" : "MISMATCHES");
    return t.Ok() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string file = argc > 1 ? argv[1] : "data/oracle/slime.jsonl";
    Data data;
    try {
        if (!LoadData(data)) {
            std::printf("no extracted data (ML_DATA, default data/extracted): can't build the slimes\n");
            return 1;
        }
        return Replay(file, data);
    } catch (const cs::Thrown& e) {
        std::printf("the translated code threw %s\n", e.TypeName());
        return 1;
    }
}
