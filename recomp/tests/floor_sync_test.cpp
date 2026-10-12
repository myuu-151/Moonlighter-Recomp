// Whole-floor sync: runs DungeonFloor (layout, room selection, every room's fill, secret
// rooms, next-floor door, boss room, Desert shrine) from the RNG state at CaveGenerator.Init to
// the end of SpawnSpecialRoomsAfterGeneration, and requires every room to start exactly where
// the game's did and the floor to end in the game's state. Engine answers come from the trace.
// Resyncs are only allowed where the trace lacks inputs (older sessions without chest
// generation data); they are counted and listed. Then the Wanderer DLC post-process (after
// frame-timed asset loading, so its start is searched for) must pick the game's host room and
// end, with the intermediate room and its advisor, in the floor's final state.
//
// Usage: floor_sync_test <oracle data dir> <generation_data_counts.json> <wanderer_specialRooms.json>
//                        <bundle_assets.json>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "cs/Object.h"
#include "game/Constants.h"
#include "game/ChestGeneration.h"
#include "game/DungeonFloor.h"
#include "game/PrefabCatalog.h"
#include "game/Prefabs.h"
#include "game/RoomScene.h"
#include "unity/Random.h"
#include "util/Json.h"

using json::Value;
using namespace game;
namespace Random = unity::Random;

namespace {

std::string ReadFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

Random::State StateFrom(const Value& v)
{
    return {static_cast<uint32_t>(v[0].i), static_cast<uint32_t>(v[1].i), static_cast<uint32_t>(v[2].i), static_cast<uint32_t>(v[3].i)};
}

bool Same(const Random::State& a, const Random::State& b) { return a.s0 == b.s0 && a.s1 == b.s1 && a.s2 == b.s2 && a.s3 == b.s3; }

float FloatBits(int64_t b)
{
    int32_t v = static_cast<int32_t>(b);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

DetailsData DetailsFrom(const Value& e)
{
    DetailsData d;
    d.breakables = static_cast<int>(e["breakables"].i);
    d.particles = static_cast<int>(e["particles"].i);
    d.creatures = static_cast<int>(e["creatures"].i);
    d.walls = static_cast<int>(e["walls"].i);
    auto sizes = [](const Value& v) {
        std::vector<DetailsData::Size> out;
        for (const Value& x : v.arr) {
            DetailsData::Size s;
            if (x.IsNull()) s.null = true;
            else {
                s.w = static_cast<int>(x[0].i);
                s.h = static_cast<int>(x[1].i);
            }
            out.push_back(s);
        }
        return out;
    };
    auto images = [](const Value& v) {
        static const std::string b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::vector<DetailsData::ProbImage> out;
        for (const Value& x : v.arr) {
            DetailsData::ProbImage im;
            if (x.IsNull()) {
                im.null = true;
                out.push_back(im);
                continue;
            }
            im.w = static_cast<int>(x["w"].i);
            im.h = static_cast<int>(x["h"].i);
            uint32_t buf = 0;
            int bits = 0;
            for (char c : x["red"].s) {
                size_t pos = b64.find(c);
                if (pos == std::string::npos) continue;
                buf = (buf << 6) | static_cast<uint32_t>(pos);
                bits += 6;
                if (bits >= 8) {
                    bits -= 8;
                    im.red.push_back(static_cast<uint8_t>((buf >> bits) & 0xFF));
                }
            }
            out.push_back(im);
        }
        return out;
    };
    d.lower = sizes(e["lower"]);
    d.lower2 = sizes(e["lower2"]);
    d.middle = sizes(e["middle"]);
    d.lowerProb = images(e["lowerProb"]);
    d.lower2Prob = images(e["lower2Prob"]);
    return d;
}

ChestConfig ChestFrom(const Value& e)
{
    ChestConfig cfg;
    cfg.capacity = static_cast<int>(e["capacity"].i);
    cfg.dropIncrement = FloatBits(e["dropIncrement"].i);
    cfg.generateRandomContentOnInit = e["generateRandomContentOnInit"].b;
    for (const Value& d : e["fixedDrop"].arr) {
        ChestDrop cd;
        cd.itemName = d["item"].s;
        cd.minQuantity = static_cast<int>(d["min"].i);
        cd.maxQuantity = static_cast<int>(d["max"].i);
        cd.ngpOnly = d["ngpOnly"].b;
        cd.ownedAmulet = d["ownedAmulet"].b;
        cd.prob = FloatBits(d["prob"].i);
        cfg.fixedDrop.push_back(cd);
    }
    return cfg;
}

FloorGenerationSaveInfo FloorInfoFrom(const Value& v)
{
    FloorGenerationSaveInfo f;
    f.lastDoorDirectionIndex = static_cast<int>(v["lastDoorDirectionIndex"].i);
    f.wavesRoomFloor = static_cast<int>(v["wavesRoomFloor"].i);
    f.wandererNestRoomFloor = static_cast<int>(v["wandererNestRoomFloor"].i);
    f.abandonedItemsFloor = static_cast<int>(v["abandonedItemsFloor"].i);
    f.dungeonToHomeFloor = static_cast<int>(v["dungeonToHomeFloor"].i);
    f.corruptedRoomFloor = static_cast<int>(v["corruptedRoomFloor"].i);
    f.crazyPeteRoomFloor = static_cast<int>(v["crazyPeteRoomFloor"].i);
    f.specialDungeonRoom = static_cast<int>(v["specialDungeonRoom"].i);
    f.desertHardEnemiesRoom = static_cast<int>(v["desertHardEnemiesRoom"].i);
    return f;
}

void LoadChestLists(const Value& e, ChestTables& t)
{
    t.lists.clear();
    t.itemStore.clear();
    t.itemStore.reserve(4096);
    for (const Value& byType : e["types"].arr) {
        int type = static_cast<int>(byType["type"].i);
        for (const Value& byCulture : byType["cultures"].arr) {
            Weighted<Weighted<const ChestItem*>> outer;
            outer.maxWeight = FloatBits(byCulture["maxWeight"].i);
            for (const Value& tier : byCulture["tiers"].arr) {
                Weighted<const ChestItem*> inner;
                inner.maxWeight = FloatBits(tier["maxWeight"].i);
                for (const Value& it : tier["items"].arr) {
                    const Value& m = it["item"];
                    ChestItem ci;
                    if (!m.IsNull()) {
                        ci.name = m["name"].s;
                        ci.id = static_cast<int>(m["id"].i);
                        ci.maxStack = static_cast<int>(m["maxStack"].i);
                        ci.fixedChestStack = static_cast<int>(m["fixedChestStack"].i);
                        ci.minChestStack = static_cast<int>(m["minChestStack"].i);
                        ci.maxChestStack = static_cast<int>(m["maxChestStack"].i);
                        ci.destroyedOnRunEnded = m["destroyedOnRunEnded"].b;
                        ci.culture = static_cast<int>(m["culture"].i);
                        ci.amulet = m["amulet"].b;
                    }
                    t.itemStore.push_back(ci);
                    inner.items.push_back({&t.itemStore.back(), FloatBits(it["weight"].i)});
                }
                outer.items.push_back({inner, FloatBits(tier["weight"].i)});
            }
            t.lists[{type, static_cast<int>(byCulture["culture"].i)}] = outer;
        }
    }
}

ChestEffect EffectFrom(const Value& v)
{
    ChestEffect e;
    if (v.IsNull()) return e;
    e.name = v["name"].s;
    e.action = v["action"].IsString() ? v["action"].s : "";
    if (const Value* d = v.Find("randomDirections")) {
        e.slotAction = true;
        e.randomDirections = static_cast<int>(d->i);
        e.fixedStack = static_cast<int>(v["fixedStack"].i);
    }
    if (const Value* r = v.Find("selectRandom")) e.selectRandom = r->b;
    return e;
}

void LoadEffects(const Value& e, ChestTables& t)
{
    t.effects.clear();
    t.effectStore.clear();
    t.effectStore.reserve(4096);
    t.trick = EffectFrom(e["trick"]);
    for (const Value& byType : e["types"].arr) {
        Weighted<std::vector<const ChestEffect*>> levels;
        levels.maxWeight = FloatBits(byType["maxWeight"].i);
        for (const Value& lv : byType["levels"].arr) {
            std::vector<const ChestEffect*> effects;
            for (const Value& ef : lv["effects"].arr) {
                t.effectStore.push_back(EffectFrom(ef));
                effects.push_back(&t.effectStore.back());
            }
            levels.items.push_back({effects, FloatBits(lv["weight"].i)});
        }
        t.effects.push_back(levels);
    }
}

// Runs the port's ChestGeneration with a recorded call's inputs (args, amulet state).
void RunChestGen(const Value& e, const ChestTables& tables)
{
    ChestGenArgs a;
    a.culture = static_cast<int>(e["culture"].i);
    a.listCulture = e.Find("intermediate") && e["intermediate"].b ? static_cast<int>(e["intermediateCulture"].i) : a.culture;
    a.chestType = static_cast<int>(e["type"].i);
    a.capacity = static_cast<int>(e["capacity"].i);
    a.maxDifferentItems = static_cast<int>(e["maxDifferent"].i);
    a.maxItemQuantityPerStack = static_cast<int>(e["maxStack"].i);
    a.dropIncrement = FloatBits(e["dropIncrement"].i);
    std::vector<std::string> owned;
    for (const Value& o : e["ownedAmulets"].arr) owned.push_back(o.s);
    AmuletState am;
    am.initialized = e["amuletInit"].b;
    am.remaining = static_cast<int>(e["amuletsRemaining"].i);
    for (const Value& x : e["amulets"].arr) {
        if (x[3].b) continue;
        am.amulets.push_back({x[0].s, FloatBits(x[1].i), static_cast<int>(x[2].i)});
        am.maxWeight += FloatBits(x[1].i);
    }
    GenerateChestContent(a, tables, owned, am);
}

// Engine answers for one floor, read from its trace events in order.
class TraceEngine : public FloorEngine {
public:
    TraceEngine(const std::vector<const Value*>& events, const std::map<std::pair<int, int>, DetailsData>& details, const ChestTables* tables)
        : ev_(events), details_(details), tables_(tables)
    {
    }

    std::vector<std::string> resyncs;
    std::string divergence; // first room whose start state differs

    void BeginRoom(DungeonRoomInfo* info, int x, int y, bool secret) override
    {
        (void)x;
        (void)y;
        (void)secret;
        // Next "room" event; the window runs to the following one.
        size_t i = cursor_;
        while (i < ev_.size() && (*ev_[i])["ev"].s != "room") i++;
        if (i >= ev_.size()) {
            fail("no more traced rooms");
            return;
        }
        const Value& room = *ev_[i];
        roomStart_ = i;
        size_t end = i + 1;
        while (end < ev_.size() && (*ev_[end])["ev"].s != "room") end++;
        roomEnd_ = end;
        cursor_ = end;
        chests_.clear();
        nextChest_ = 0;
        skeleton_ = nullptr;
        detailsRoom_ = nullptr;
        prefabChests_ = 0;
        bool inPrefabs = false;
        for (size_t m = i + 1; m < end; m++) {
            const Value& p = *ev_[m];
            const std::string& t = p["ev"].s;
            if (t == "chestConfig") {
                chests_.push_back(m);
                if (inPrefabs) prefabChests_++;
            } else if (t == "skeleton") skeleton_ = &p;
            else if (t == "detailsRoom") detailsRoom_ = &p;
            else if (t == "stage" && p["name"].s == "DungeonRoom.SpawnRoomPrefabs") inPrefabs = p["phase"].s == "pre";
        }
        if (room["name"].s != info->name) fail("room " + std::to_string(room["room"].i) + ": port placed " + info->name + ", game " + room["name"].s);
        else if (!Same(Random::GetState(), StateFrom(room["state"])) && divergence.empty())
            fail("room " + std::to_string(room["room"].i) + " (" + info->name + ") starts from a different RNG state");
    }

    ChestConfig NextChest(bool& ngp) override
    {
        if (nextChest_ >= chests_.size()) {
            fail("port spawned more chests than the game");
            ngp = false;
            return {};
        }
        currentChest_ = chests_[nextChest_++];
        ngp = (*ev_[currentChest_])["ngp"].b;
        return ChestFrom(*ev_[currentChest_]);
    }

    void RandomChestContents(const ChestConfig&) override
    {
        // The chest's GenerateChestContent call follows its config in the trace.
        const Value* gen = nullptr;
        const Value* post = nullptr;
        for (size_t m = currentChest_ + 1; m < ev_.size(); m++) {
            const Value& p = *ev_[m];
            if (!gen && p["ev"].s == "chestGen") gen = &p;
            if (p["ev"].s == "stage" && p["name"].s == "Chest.Awake" && p["phase"].s == "post") {
                post = &p;
                break;
            }
        }
        if (gen && tables_) {
            RunChestGen(*gen, *tables_);
            if (post && Same(Random::GetState(), StateFrom((*post)["state"]))) return;
        }
        if (post) Random::SetState(StateFrom((*post)["state"]));
        resyncs.push_back(gen && tables_ ? "chest generation mismatch" : "random chest contents (no generation data)");
    }

    int BreakablesInScene() override { return skeleton_ ? static_cast<int>((*skeleton_)["candidates"].size()) : 0; }
    int SkeletonSkins() override { return skeleton_ ? static_cast<int>((*skeleton_)["skins"].i) : 0; }

    DetailsRoom RoomPrefab(Culture, Culture) override
    {
        DetailsRoom r;
        if (detailsRoom_) {
            r.spawners = static_cast<int>((*detailsRoom_)["spawners"].i);
            r.roomFloorWidth = static_cast<int>((*detailsRoom_)["roomFloorWidth"].i);
            r.roomFloorHeight = static_cast<int>((*detailsRoom_)["roomFloorHeight"].i);
        }
        return r;
    }

    const DetailsData* Details(Culture culture, int level) override
    {
        auto it = details_.find({static_cast<int>(culture), level});
        if (it == details_.end()) {
            fail("no detail data");
            return nullptr;
        }
        return &it->second;
    }

    int ChestsInPrefabs(DungeonRoomInfo*) override { return prefabChests_; }

    DungeonRoomInfo* MinibossOverride() override
    {
        for (const Value* p : ev_)
            if ((*p)["ev"].s == "minibossRoom") {
                // Plain database rooms resolve normally; a DLC room can't be rebuilt here.
                return nullptr;
            }
        return nullptr;
    }

    // Boss room prefab: replay the chests woken inside AddBossRoom (the port makes the
    // champion roll itself).
    void BossRoomPlaced() override
    {
        bool inside = false;
        for (size_t m = 0; m < ev_.size(); m++) {
            const Value& p = *ev_[m];
            if (p["ev"].s == "stage" && p["name"].s == "DungeonGenerator.AddBossRoom") {
                if (p["phase"].s == "pre") {
                    if (!Same(Random::GetState(), StateFrom(p["state"]))) fail("state differs before the boss room");
                    inside = true;
                    continue;
                }
                break;
            }
            if (!inside) continue;
            if (p["ev"].s == "chestConfig") GenerateFixedDrop(ChestFrom(p), p["ngp"].b);
            else if (p["ev"].s == "chestGen" && tables_) RunChestGen(p, *tables_);
        }
    }

    // From the trace: the post-boss enemies spawn only when the boss was already defeated.
    bool BossDefeated() override
    {
        bool inside = false;
        for (const Value* p : ev_) {
            if ((*p)["ev"].s == "stage" && (*p)["name"].s == "DungeonGenerator.AddBossRoom") {
                if ((*p)["phase"].s == "post") break;
                inside = true;
                continue;
            }
            if (inside && (*p)["ev"].s == "stage" && (*p)["name"].s == "DungeonRoom.SpawnEnemy") return true;
        }
        return false;
    }

    // After the boss Start() frame the next step (SpawnSpecialRoomsAfterGeneration) must begin
    // exactly where the game's did.
    void BossStartFrame() override
    {
        for (const Value* p : ev_)
            if ((*p)["ev"].s == "stage" && (*p)["name"].s == "DungeonGenerator.SpawnSpecialRoomsAfterGeneration" && (*p)["phase"].s == "pre") {
                if (!Same(Random::GetState(), StateFrom((*p)["state"]))) fail("state differs after the boss room / boss Start()");
                return;
            }
    }

    void BeforeSpecialRooms() override
    {
        if (const Value* p = Stage("DungeonGenerator.SpawnSpecialRoomsAfterGeneration", "pre"))
            if (!Same(Random::GetState(), StateFrom((*p)["state"]))) fail("state differs before SpawnSpecialRoomsAfterGeneration");
    }

    // The port placed the shrine (or not): the state must be the game's after the stage.
    void SpecialRoomsAfterGeneration() override
    {
        const Value* post = Stage("DungeonGenerator.SpawnSpecialRoomsAfterGeneration", "post");
        if (!post || Same(Random::GetState(), StateFrom((*post)["state"]))) return;
        Random::SetState(StateFrom((*post)["state"]));
        resyncs.push_back("Desert shrine");
    }

    const Value* Stage(const char* name, const char* phase) const
    {
        for (const Value* p : ev_)
            if ((*p)["ev"].s == "stage" && (*p)["name"].s == name && (*p)["phase"].s == phase) return p;
        return nullptr;
    }

    // The shrine as the game left it (floorEnd snapshot): "<room>/Shrine(Clone)" and its position.
    bool RecordedShrine(std::string& room, float& x, float& y) const
    {
        for (const Value* p : ev_) {
            if ((*p)["ev"].s != "floorEnd") continue;
            for (const Value& o : (*p)["objects"].arr) {
                const std::string& path = o[0].s;
                size_t slash = path.find('/');
                if (slash == std::string::npos || path.compare(slash + 1, std::string::npos, "Shrine(Clone)") != 0) continue;
                room = path.substr(0, slash);
                x = FloatBits(o[2].i);
                y = FloatBits(o[3].i);
                return true;
            }
        }
        return false;
    }

    // Final state to compare: end of SpawnSpecialRoomsAfterGeneration.
    const Value* EndState() const
    {
        for (const Value* p : ev_)
            if ((*p)["ev"].s == "stage" && (*p)["name"].s == "DungeonGenerator.SpawnSpecialRoomsAfterGeneration" && (*p)["phase"].s == "post")
                return &(*p)["state"];
        return nullptr;
    }

    int NextDoorRecorded() const
    {
        for (const Value* p : ev_)
            if ((*p)["ev"].s == "stage" && (*p)["name"].s == "DungeonGenerator.AddNextLevelDoor" && (*p)["phase"].s == "post")
                return static_cast<int>((*p)["savedLastDoorDirectionIndex"].i);
        return -1;
    }

private:
    void fail(const std::string& why)
    {
        if (divergence.empty()) divergence = why;
    }

    // Replays chests inside the stage; if that doesn't land on the game's state, resync there.
    void ResyncAtStage(const char* stage, const char* what)
    {
        const Value* pre = nullptr;
        const Value* post = nullptr;
        size_t preIdx = 0;
        for (size_t m = 0; m < ev_.size(); m++) {
            const Value& p = *ev_[m];
            if (p["ev"].s == "stage" && p["name"].s == stage) {
                if (p["phase"].s == "pre" && !pre) {
                    pre = &p;
                    preIdx = m;
                }
                if (p["phase"].s == "post") post = &p;
            }
        }
        if (!pre || !post) return;
        if (!Same(Random::GetState(), StateFrom((*pre)["state"]))) fail(std::string("state differs before ") + stage);
        for (size_t m = preIdx + 1; m < ev_.size() && ev_[m] != post; m++) {
            const std::string& t = (*ev_[m])["ev"].s;
            if (t == "chestConfig") GenerateFixedDrop(ChestFrom(*ev_[m]), (*ev_[m])["ngp"].b);
            else if (t == "chestGen" && tables_) RunChestGen(*ev_[m], *tables_);
        }
        if (!Same(Random::GetState(), StateFrom((*post)["state"]))) {
            Random::SetState(StateFrom((*post)["state"]));
            resyncs.push_back(what ? what : stage);
        }
    }

    const std::vector<const Value*>& ev_;
    const std::map<std::pair<int, int>, DetailsData>& details_;
    const ChestTables* tables_;
    size_t cursor_ = 0, roomStart_ = 0, roomEnd_ = 0;
    std::vector<size_t> chests_;
    size_t nextChest_ = 0, currentChest_ = 0;
    const Value* skeleton_ = nullptr;
    const Value* detailsRoom_ = nullptr;
    int prefabChests_ = 0;
};

// ---- Scene check: the port's room objects against the floorEnd snapshot ----

// One snapshot room: its root and every object below it, positions relative to the room.
struct SnapRoom {
    std::string name;
    float x = 0.0f, y = 0.0f;
    std::vector<SceneObject> objects;
};

std::vector<SnapRoom> SnapshotRooms(const Value& floorEnd)
{
    std::vector<SnapRoom> rooms;
    std::vector<std::pair<float, float>> stack; // accumulated position per depth
    for (const Value& o : floorEnd["objects"].arr) {
        const std::string& path = o[0].s;
        size_t depth = static_cast<size_t>(std::count(path.begin(), path.end(), '/'));
        float lx = FloatBits(o[2].i), ly = FloatBits(o[3].i);
        stack.resize(depth + 1);
        if (depth == 0) {
            rooms.push_back({path, lx, ly, {}});
            stack[0] = {0.0f, 0.0f}; // children are measured from the room
            continue;
        }
        stack[depth] = {stack[depth - 1].first + lx, stack[depth - 1].second + ly};
        std::string rel = path.substr(path.find('/') + 1);
        // An enemy may move itself in Init (FruitTree): its spawn point is what generation set.
        bool enemy = depth == 3 && rel.rfind("DungeonLevelEnemies/EnemySpawn/", 0) == 0;
        const auto& at = enemy ? stack[depth - 1] : stack[depth];
        rooms.back().objects.push_back({rel, at.first, at.second, o[4].IsString() ? o[4].s : std::string()});
    }
    return rooms;
}

// Which objects the port places: enemy spawns and the enemy instantiated in each (other
// objects in a spawn, like TeslaTangle's Lightning, are moved there by the enemy's own
// scripts), rocks, SpawnChests chests, wall details, and the details SpawnDetails puts in
// the room (breakables, creatures, particles: `detailNames`).
const char* SceneCategory(const std::string& rel, const std::vector<std::string>& chestIds, const std::set<std::string>& detailNames)
{
    size_t segments = static_cast<size_t>(std::count(rel.begin(), rel.end(), '/')) + 1;
    auto endsWith = [&](const char* s) { size_t n = std::strlen(s); return rel.size() >= n && rel.compare(rel.size() - n, n, s) == 0; };
    if (rel == "DungeonLevelEnemies/EnemySpawn") return "enemies";
    if (segments == 3 && rel.rfind("DungeonLevelEnemies/EnemySpawn/", 0) == 0 && endsWith("(Clone)")) return "enemies";
    if (segments == 2 && rel.rfind("DungeonLevelRoomRocks/", 0) == 0) return "rocks";
    if (segments == 4 && rel.rfind("Details/WallDetails/DetailSpawn/", 0) == 0) return "walls";
    if (rel == "DungeonLevelArt/room" || rel == "DungeonLevelArt/walls" || rel == "DungeonLevelArt/Lights" || rel == "DungeonLevelArt/Shadows")
        return "art";
    if (segments == 1) {
        for (const std::string& id : chestIds)
            if (rel == id + "(Clone)") return "chests";
        if (endsWith("(Clone)") && detailNames.count(rel.substr(0, rel.size() - 7))) return "details";
    }
    return nullptr;
}

const char* const kSceneCategories[] = {"enemies", "chests", "rocks", "walls", "details", "art"};

// Art is compared by sprite (its position is the prefab's); the rest by position.
std::string SceneKey(const SceneObject& o, const char* category)
{
    if (std::strcmp(category, "art") == 0) return o.path + "=" + o.sprite;
    char buf[64];
    std::snprintf(buf, sizeof buf, "@%.9g,%.9g", o.x, o.y); // round-trips a float
    return o.path + buf;
}

} // namespace

int main(int argc, char** argv)
{
    std::string dir = argc > 1 ? argv[1] : "data/oracle";
    // Game data made from the Steam copy by tools/make_data.py (ML_DATA overrides).
    std::string data = std::getenv("ML_DATA") ? std::getenv("ML_DATA") : "data/extracted";
    std::string countsPath = argc > 2 ? argv[2] : "data/extracted/generation_data_counts.json";
    if (!Constants::LoadFromFile(data + "/constants.json")) return 2;
    Value asset = json::Parse(ReadFile(data + "/roomdb_asset.json"));
    std::string wandererPath = argc > 3 ? argv[3] : "data/extracted/wanderer_specialRooms.json";
    Value wandererRooms = json::Parse(ReadFile(wandererPath));
    std::string bundlesPath = argc > 4 ? argv[4] : "data/extracted/bundle_assets.json";
    PrefabCatalog::Load(json::Parse(ReadFile(bundlesPath)));
    PrefabLibrary::Load(json::Parse(ReadFile(data + "/prefabs.json")));
    const Value* wandererIntermediate = nullptr;
    if (const Value* sr = wandererRooms.Find("specialRooms"))
        for (const Value& r : sr->arr)
            if (r["name"].s == "WND_INTERMEDIATE") wandererIntermediate = &r;
    Value genCounts = json::Parse(ReadFile(countsPath));
    std::vector<Value> trace, caves;
    {
        std::ifstream f(dir + "/trace.jsonl");
        std::string line;
        while (std::getline(f, line))
            if (!line.empty()) trace.push_back(json::Parse(line));
        std::ifstream c(dir + "/cave_game.jsonl");
        while (std::getline(c, line))
            if (!line.empty()) caves.push_back(json::Parse(line));
    }

    // Group by session/floor, keeping only sessions recorded with every hook (detail data).
    std::map<std::pair<std::string, int64_t>, std::vector<const Value*>> floors;
    std::vector<std::pair<std::string, int64_t>> order;
    std::map<std::string, std::map<std::pair<int, int>, DetailsData>> details;
    // The details registers as extracted from the game (tools/extract/details.py; equal to
    // what the oracle recorded in the running game).
    std::map<std::pair<int, int>, DetailsData> extractedDetails = LoadDetailsData(json::Parse(ReadFile(data + "/details.json")));
    std::map<std::string, const Value*> sessionEffects;
    for (const Value& e : trace) {
        auto key = std::make_pair(e["session"].s, e["floor"].i);
        if (!floors.count(key)) order.push_back(key);
        floors[key].push_back(&e);
        if (e["ev"].s == "detailsData") details[e["session"].s][{static_cast<int>(e["culture"].i), static_cast<int>(e["level"].i)}] = DetailsFrom(e);
        if (e["ev"].s == "effectLists" && !sessionEffects.count(e["session"].s)) sessionEffects[e["session"].s] = &e;
    }

    const char* cultureNames[6] = {nullptr, "Golem", "Forest", "Desert", "Tech", "Unknown"};
    int inSync = 0, differ = 0, checked = 0;
    std::map<std::string, int> resyncKinds;
    int floorsWithoutResync = 0;
    int shrinesMatch = 0, shrinesDiffer = 0;
    int wndPinned = 0, wndAmbiguous = 0, wndNoSolution = 0, wndRoomOk = 0, wndRoomDiffer = 0;
    std::map<std::string, int> sceneMatch, sceneDiffer; // rooms, per object category
    std::map<std::string, int> sceneObjects;            // objects the game had, per category
    int sceneRoomsMissing = 0;
    std::vector<std::string> notes;
    std::string lastSession;
    for (const auto& key : order) {
        const std::string& session = key.first;
        if (!details.count(session)) continue; // older session without detail data
        const std::vector<const Value*>& evs = floors[key];
        const Value* begin = nullptr;
        for (const Value* e : evs)
            if ((*e)["ev"].s == "floorBegin") begin = e;
        if (!begin || !begin->Find("level")) continue;
        const Value* cave = nullptr;
        std::string start = session.substr(0, 4) + "-" + session.substr(4, 2) + "-" + session.substr(6, 2) + "T" + session.substr(9, 2) + ":" +
                            session.substr(11, 2) + ":" + session.substr(13, 2);
        for (const Value& c : caves)
            if (c["stateAtCaveInit"] == (*begin)["state"] && c["time"].s.substr(0, 19) >= start) {
                cave = &c;
                break;
            }
        if (!cave) continue;

        // Session-wide generator data, reset per floor for the floor's own culture.
        if (session != lastSession) {
            lastSession = session;
            for (int c = 0; c < 6; c++) {
                const Value* entry = cultureNames[c] ? genCounts.Find(cultureNames[c]) : nullptr;
                RoomGenerator::generationDataByCulture[c] = entry ? RoomGenerationData::FromExtracted(*entry) : nullptr;
            }
        }
        int level = static_cast<int>((*cave)["level"].i);
        Culture culture = static_cast<Culture>((*cave)["culture"].i);
        {
            const Value* entry = genCounts.Find(cultureNames[static_cast<int>(culture)]);
            RoomGenerator::generationDataByCulture[static_cast<int>(culture)] = entry ? RoomGenerationData::FromExtracted(*entry) : nullptr;
        }

        cs::Heap heap;
        cs::SetCurrentHeap(&heap);
        auto* db = cs::gcnew<DungeonRoomDatabase>(); // fresh per floor (the transition reloads it)
        db->LoadSpecialRooms(asset);
        db->LoadFromJSON(asset["database"]);
        FloorGenerationSaveInfo fi = FloorInfoFrom((*cave)["floorInfo"]);
        if (level != 0) fi.lastDoorDirectionIndex = DungeonLayout::GetComplementaryDoorIndex(static_cast<int>((*cave)["lastDoorDirectionIndex"].i));

        ChestTables tables;
        bool haveTables = false;
        if (sessionEffects.count(session)) {
            LoadEffects(*sessionEffects[session], tables);
            for (const Value* e : evs)
                if ((*e)["ev"].s == "chestLists") {
                    LoadChestLists(*e, tables);
                    haveTables = true;
                }
        }
        TraceEngine engine(evs, extractedDetails, haveTables ? &tables : nullptr);
        Random::SetState(StateFrom((*cave)["stateAtCaveInit"]));
        DungeonFloor floor(db, culture, level);
        std::string ex;
        try {
            floor.Generate(fi, engine);
        } catch (const std::exception& x) {
            ex = x.what();
        }
        checked++;
        const Value* end = engine.EndState();
        bool ok = ex.empty() && engine.divergence.empty() && end && Same(Random::GetState(), StateFrom(*end));
        int door = engine.NextDoorRecorded();
        if (ok && door >= 0 && floor.nextLevelDoor != door) {
            ok = false;
            engine.divergence = "next-floor door " + std::to_string(floor.nextLevelDoor) + " vs game " + std::to_string(door);
        }
        std::string shrineRoom;
        float sx = 0.0f, sy = 0.0f;
        bool recordedShrine = engine.RecordedShrine(shrineRoom, sx, sy);
        if (ex.empty() && (recordedShrine || floor.shrineCell >= 0)) {
            bool same = recordedShrine && floor.shrineCell >= 0;
            if (same) {
                const PlacedCell& c = floor.cells[static_cast<size_t>(floor.shrineCell)];
                float ts = Constants::GetFloat("tileSize");
                float px = (static_cast<float>(floor.shrineTile % c.room->gridSlicesX) + 0.5f) * ts - 242.0f + 2.0f;
                float py = (static_cast<float>(floor.shrineTile / c.room->gridSlicesX) + 0.5f) * ts - 134.0f + 2.0f;
                same = c.info->name + "_Shrine" == shrineRoom && px == sx && py == sy;
            }
            (same ? shrinesMatch : shrinesDiffer)++;
            if (!same && notes.size() < 8)
                notes.push_back("shrine: port " + (floor.shrineCell >= 0 ? floor.cells[static_cast<size_t>(floor.shrineCell)].info->name + " tile " + std::to_string(floor.shrineTile) : std::string("none")) +
                                ", game " + (recordedShrine ? shrineRoom + " at " + std::to_string(sx) + "," + std::to_string(sy) : std::string("none")));
        }
        // Wanderer DLC post-process. Between SpawnSpecialRoomsAfterGeneration and it lie frames
        // of asset loading where other scripts draw, so its start state isn't recorded: try
        // every start between the last recorded stage and the intermediate room's first draw,
        // and require the shuffle to end exactly there with the game's host room.
        const Value* wndRoom = nullptr;
        size_t wndIdx = 0;
        for (size_t m = 0; m < evs.size(); m++)
            if ((*evs[m])["ev"].s == "room" && (*evs[m])["name"].s == "WND_INTERMEDIATE") {
                wndRoom = evs[m];
                wndIdx = m;
            }
        if (ok && wndRoom && wandererIntermediate) {
            const Value* before = nullptr;
            for (size_t m = wndIdx; m-- > 0;)
                if ((*evs[m])["ev"].s == "stage" && (*evs[m])["phase"].s == "post") {
                    before = evs[m];
                    break;
                }
            Random::State s0 = StateFrom((*before)["state"]), s1 = StateFrom((*wndRoom)["state"]);
            int span = Random::DrawsBetween(s0, s1, 100000);
            std::vector<int> starts;
            for (int st = 0; st <= span; st++) {
                Random::SetState(s0);
                for (int d = 0; d < st; d++) (void)Random::value();
                int host = floor.PickWandererHost();
                if (host >= 0 && Same(Random::GetState(), s1) && floor.cells[static_cast<size_t>(host)].x == (*wndRoom)["x"].i &&
                    floor.cells[static_cast<size_t>(host)].y == (*wndRoom)["y"].i)
                    starts.push_back(st);
            }
            if (starts.empty()) {
                wndNoSolution++;
                if (notes.size() < 8) notes.push_back("Wanderer host: no start in " + std::to_string(span) + " draws gives the game's room");
            } else {
                (starts.size() == 1 ? wndPinned : wndAmbiguous)++;
                Random::SetState(s0);
                for (int d = 0; d < starts[0]; d++) (void)Random::value();
                DungeonRoomInfo* info = DungeonRoomInfo::FromJson(*wandererIntermediate);
                std::string wex;
                try {
                    floor.WandererPostprocess(info, engine);
                } catch (const std::exception& x) {
                    wex = x.what();
                }
                const Value* secretPost = nullptr;
                for (size_t m = wndIdx; m < evs.size(); m++)
                    if ((*evs[m])["ev"].s == "stage" && (*evs[m])["name"].s == "DungeonGenerator.AddSecretRoom" && (*evs[m])["phase"].s == "post") secretPost = evs[m];
                bool roomOk = wex.empty() && engine.divergence.empty() && secretPost && Same(Random::GetState(), StateFrom((*secretPost)["state"]));
                (roomOk ? wndRoomOk : wndRoomDiffer)++;
                if (!roomOk && notes.size() < 8)
                    notes.push_back("Wanderer intermediate room: " + (!wex.empty() ? "exception " + wex : !engine.divergence.empty() ? engine.divergence : std::string("end state differs")));
            }
        }
        // Scene: every room the port generated (main and secret) against the snapshot room with
        // the same name at the same position; per category the objects must be the same
        // multiset of (path, position).
        const Value* floorEnd = nullptr;
        for (const Value* p : evs)
            if ((*p)["ev"].s == "floorEnd") floorEnd = p;
        if (ok && floorEnd) {
            std::vector<SnapRoom> snap = SnapshotRooms(*floorEnd);
            std::vector<std::string> chestIds = Constants::GetStringArray("ChestsIDs");
            std::set<std::string> detailNames;
            for (const auto& kv : extractedDetails)
                for (const auto* names : {&kv.second.breakableNames, &kv.second.creatureNames, &kv.second.particleNames})
                    for (const std::string& n : *names)
                        if (!n.empty()) detailNames.insert(n);
            auto check = [&](const RoomFill* room, const std::string& name, int x, int y) {
                unity::Vector2 at = RoomPosition(x, y);
                const SnapRoom* sr = nullptr;
                for (const SnapRoom& r : snap)
                    if (r.name == name && r.x == at.x && r.y == at.y) sr = &r;
                if (!sr) {
                    sceneRoomsMissing++;
                    if (notes.size() < 8) notes.push_back("scene: no snapshot room " + name + " at " + std::to_string(x) + "," + std::to_string(y));
                    return;
                }
                std::map<std::string, std::map<std::string, int>> want, got;
                for (const SceneObject& o : sr->objects)
                    if (const char* c = SceneCategory(o.path, chestIds, detailNames)) want[c][SceneKey(o, c)]++;
                for (const SceneObject& o : RoomSceneObjects(*room, at))
                    if (const char* c = SceneCategory(o.path, chestIds, detailNames)) got[c][SceneKey(o, c)]++;
                for (const char* c : kSceneCategories) {
                    bool same = want[c] == got[c];
                    for (const auto& kv : want[c]) sceneObjects[c] += kv.second;
                    (same ? sceneMatch[c] : sceneDiffer[c])++;
                    if (!same && notes.size() < 8) {
                        std::string d;
                        for (const auto& kv : want[c])
                            if (got[c][kv.first] != kv.second) d += " game " + kv.first + " x" + std::to_string(kv.second);
                        for (const auto& kv : got[c])
                            if (want[c][kv.first] != kv.second) d += " port " + kv.first + " x" + std::to_string(kv.second);
                        notes.push_back("scene " + std::string(c) + " in " + name + ":" + d.substr(0, 300));
                    }
                }
            };
            for (size_t ci = 0; ci < floor.cells.size(); ci++) {
                const PlacedCell& c = floor.cells[ci];
                check(c.room, c.info->name + (static_cast<int>(ci) == floor.shrineCell ? "_Shrine" : ""), c.x, c.y);
                if (c.secret) check(c.secret, c.secretInfo->name, c.x, c.y);
            }
        }
        if (ok) {
            inSync++;
            if (engine.resyncs.empty()) floorsWithoutResync++;
            for (const std::string& r : engine.resyncs) resyncKinds[r.substr(0, r.find(" ("))]++;
        } else {
            differ++;
            if (notes.size() < 8)
                notes.push_back(std::string(cultureNames[static_cast<int>(culture)]) + " floor " + std::to_string(level + 1) + " (#" + std::to_string(key.second) +
                                "): " + (!ex.empty() ? "exception " + ex : (!engine.divergence.empty() ? engine.divergence : "end state differs")));
        }
        cs::SetCurrentHeap(nullptr);
    }

    std::printf("floors in sync (layout to last room)  %4d match  %4d differ   (of %d recorded with every hook)\n", inSync, differ, checked);
    std::printf("  with no resync at all                %4d\n", floorsWithoutResync);
    for (const auto& kv : resyncKinds) std::printf("  resynced at: %-40s %d times\n", kv.first.c_str(), kv.second);
    std::printf("Desert shrines (room and tile)         %4d match  %4d differ\n", shrinesMatch, shrinesDiffer);
    std::printf("Wanderer DLC host room (shuffle)       %4d match  %4d differ   (%d pinned to a unique start)\n", wndPinned + wndAmbiguous, wndNoSolution, wndPinned);
    std::printf("Wanderer intermediate room + advisor   %4d match  %4d differ\n", wndRoomOk, wndRoomDiffer);
    int sceneDiffers = sceneRoomsMissing;
    for (const char* c : kSceneCategories) {
        std::printf("Scene, %-31s %4d match  %4d differ   (rooms; %d objects)\n", c, sceneMatch[c], sceneDiffer[c], sceneObjects[c]);
        sceneDiffers += sceneDiffer[c];
    }
    if (sceneRoomsMissing) std::printf("  rooms not found in the snapshot      %4d\n", sceneRoomsMissing);
    for (const std::string& n : notes) std::printf("    %s\n", n.c_str());
    bool all = differ == 0 && shrinesDiffer == 0 && wndNoSolution == 0 && wndRoomDiffer == 0 && sceneDiffers == 0 && inSync > 0;
    std::printf("\n%s\n", all ? "ALL MATCH" : (inSync == 0 ? "NO DATA" : "MISMATCHES FOUND"));
    return all ? 0 : 1;
}
