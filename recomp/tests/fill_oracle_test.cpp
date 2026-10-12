// Checks the data-logic stages of room filling against trace.jsonl, stage by stage, each
// from the RNG state the game had at that point:
//   LoadGridFromInfo   spawn pack, the full obstacle grid, RNG state after
//   SpawnEnemies       champion decision: RNG must end where the game's first SpawnEnemy
//                      began (or where SpawnEnemies ended, for rooms without enemies)
//   GetChestNameByFloor chest tier / mimic name and RNG state after
// It also measures the RNG draws made by engine-side work the port doesn't model yet
// (enemy Init, Chest.Awake, prefab spawning...) by stepping from each stage's pre state to
// its post state, and prints them grouped by prefab name.
//
// Usage: fill_oracle_test <oracle data dir> <rooms.json>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <tuple>
#include <sstream>
#include <string>
#include <vector>

#include "cs/Object.h"
#include "game/Chest.h"
#include "game/ChestGeneration.h"
#include "game/Constants.h"
#include "game/RoomDatabase.h"
#include "game/RoomFill.h"
#include "game/RoomDetails.h"
#include "game/RoomGenerator.h"
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

struct Tally {
    const char* name;
    int pass = 0, fail = 0;
    std::vector<std::string> notes;
    void Check(bool ok, const std::string& why)
    {
        (ok ? pass : fail)++;
        if (!ok && notes.size() < 5) notes.push_back(why);
    }
    void Report() const
    {
        std::printf("%-26s %6d match  %6d differ\n", name, pass, fail);
        for (const std::string& n : notes) std::printf("    %s\n", n.c_str());
    }
};

struct DrawStats {
    int samples = 0, min = 1 << 30, max = -1, unknown = 0;
    void Add(int d)
    {
        if (d < 0) {
            unknown++;
            return;
        }
        samples++;
        if (d < min) min = d;
        if (d > max) max = d;
    }
};

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
                if (pos == std::string::npos) continue; // padding
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

float FloatBits(int64_t b)
{
    int32_t v = static_cast<int32_t>(b);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

// ChestTables from the oracle's chestLists / effectLists events.
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

} // namespace

int main(int argc, char** argv)
{
    std::string dir = argc > 1 ? argv[1] : "data/oracle";
    // Game data made from the Steam copy by tools/make_data.py (ML_DATA overrides).
    std::string data = std::getenv("ML_DATA") ? std::getenv("ML_DATA") : "data/extracted";
    std::string roomsPath = argc > 2 ? argv[2] : "data/extracted/rooms.json";
    if (!Constants::LoadFromFile(data + "/constants.json")) return 2;
    Value asset = json::Parse(ReadFile(data + "/roomdb_asset.json"));

    cs::Heap heap;
    cs::SetCurrentHeap(&heap);
    auto* db = cs::gcnew<DungeonRoomDatabase>();
    db->LoadSpecialRooms(asset);
    const Value* serialized = asset.Find("database");
    Value roomsText;
    if (!(serialized && serialized->Find("rooms"))) {
        roomsText = json::Parse(ReadFile(roomsPath));
        serialized = &roomsText;
    }
    db->LoadFromJSON(*serialized);
    DungeonRoomInfo* specials[] = {db->campRoomInfo, db->initRoomInfo, db->healingRoomInfo, db->waveRoomInfo, db->sendToHomeRoomInfo,
                                   db->testRoomInfo, db->wandererNestRoomInfo, db->abandonedItemsRoomInfo, db->specialForestRoomInfo};
    // Room names aren't unique (e.g. two different "Golem_Enemies_13"), so return every
    // candidate; the grid check picks the one that reproduces the game. Newer traces carry
    // the database index ("dbIndex") and need no guessing.
    auto findInfos = [&](int culture, const std::string& name, int dbIndex) {
        std::vector<DungeonRoomInfo*> out;
        if (dbIndex >= 0 && dbIndex < db->database.rooms->Count()) {
            out.push_back((*db->database.rooms)[dbIndex]);
            return out;
        }
        for (DungeonRoomInfo* r : *db->database.rooms)
            if (static_cast<int>(r->culture) == culture && r->name == name) out.push_back(r);
        for (DungeonRoomInfo* s : specials)
            if (out.empty() && s && s->name == name) out.push_back(s);
        for (DungeonRoomInfo* r : *db->database.rooms) // corrupted rooms keep their own culture
            if (out.empty() && r->name == name) out.push_back(r);
        return out;
    };
    std::map<std::tuple<std::string, int64_t, int64_t>, std::vector<DungeonRoomInfo*>> candidates;

    std::vector<Value> trace;
    {
        std::ifstream f(dir + "/trace.jsonl");
        std::string line;
        while (std::getline(f, line))
            if (!line.empty()) trace.push_back(json::Parse(line));
    }

    Tally grid{"LoadGridFromInfo"}, champion{"SpawnEnemies (champion)"}, chests{"GetChestNameByFloor"};
    Tally lightShadow{"AddLightShadow"}, holes{"GenerateHolesPrefab"}, slime{"Slime/Acid/Lava/Electric"};
    Tally chestAwake{"Chest.Awake (fixed drops)"};
    Tally zones{"Middle detail zones"}, details{"SpawnDetails"}, detailCache{"Detail texture cache"};
    Tally chestGen{"ChestGeneration (contents)"};
    std::map<std::string, ChestTables> chestTables; // per session; lists replaced per floor
    std::map<std::string, bool> haveEffects, haveLists;
    std::map<std::tuple<std::string, int, int>, DetailsData> detailsData; // session, culture, level
    std::map<std::pair<std::string, int64_t>, DetailsCache> floorCache;

    // Per-culture generation data (sprite list sizes), cached for a whole session like the
    // game's static generationDataByCulture; one RoomGenerator per floor (new DungeonManager).
    Value genCounts = json::Parse(ReadFile(std::string(argc > 3 ? argv[3] : dir + "/../extracted/generation_data_counts.json")));
    // generationDataByCulture is static (whole session). On each floor transition the
    // registers call UnloadBundlesExcept(true, culture), whose test is inverted: it unloads
    // only *that* culture's bundle. So only the floor's own culture data comes back fresh;
    // other cultures' lists stay as AddLightShadow's aliasing left them (emptied).
    const char* cultureNames[6] = {nullptr, "Golem", "Forest", "Desert", "Tech", "Unknown"};
    auto freshData = [&](int c) {
        const Value* entry = cultureNames[c] ? genCounts.Find(cultureNames[c]) : nullptr;
        RoomGenerator::generationDataByCulture[c] = entry ? RoomGenerationData::FromCounts((*entry)["counts"]) : nullptr;
    };
    std::string genSession;
    std::map<std::pair<std::string, int64_t>, RoomGenerator*> floorGen;
    std::map<std::pair<std::string, int64_t>, int> floorCulture;
    auto generatorFor = [&](const std::string& session, int64_t floor) {
        if (session != genSession) {
            genSession = session;
            for (int c = 0; c < 6; c++) freshData(c);
        }
        RoomGenerator*& g = floorGen[{session, floor}];
        if (!g) {
            auto fc = floorCulture.find({session, floor});
            if (fc != floorCulture.end() && fc->second > 0 && fc->second < 6) freshData(fc->second);
            g = cs::gcnew<RoomGenerator>();
        }
        return g;
    };
    std::map<std::tuple<std::string, int64_t, int64_t>, int> roomCulture;
    std::map<std::string, DrawStats> engineDraws; // "stage:object" -> draws
    std::map<std::pair<std::string, int64_t>, int> floorLevel;
    std::map<std::tuple<std::string, int64_t, int64_t>, RoomFill*> fills;
    std::map<std::tuple<std::string, int64_t, int64_t>, const Value*> pendingPre; // last "pre" per room+stage

    for (size_t k = 0; k < trace.size(); k++) {
        const Value& e = trace[k];
        const std::string& ev = e["ev"].s;
        std::string session = e["session"].s;
        int64_t floor = e["floor"].i;
        if (ev == "floorBegin" && e.Find("level")) floorLevel[{session, floor}] = static_cast<int>(e["level"].i);
        if (ev == "floorBegin" && e.Find("culture")) floorCulture[{session, floor}] = static_cast<int>(e["culture"].i);
        if (ev == "room" && e.Find("culture")) {
            int dbIndex = e.Find("dbIndex") ? static_cast<int>(e["dbIndex"].i) : -1;
            std::vector<DungeonRoomInfo*> infos = findInfos(static_cast<int>(e["culture"].i), e["name"].s, dbIndex);
            if (infos.empty()) continue;
            DungeonRoomInfo* info = infos[0];
            candidates[{session, floor, e["room"].i}] = infos;
            auto* f = cs::gcnew<RoomFill>(info, static_cast<int>(e["gridSlicesX"].i), static_cast<int>(e["gridSlicesY"].i));
            fills[{session, floor, e["room"].i}] = f;
            roomCulture[{session, floor, e["room"].i}] = static_cast<int>(e["culture"].i);
        }
        if (ev == "chestConfig") {
            // Chest.Awake: the config is recorded right before its draws; the stage hook's
            // post event (next Chest.Awake post) has the state after.
            const Value* post = nullptr;
            for (size_t m = k + 1; m < trace.size() && !post; m++)
                if (trace[m]["ev"].s == "stage" && trace[m]["name"].s == "Chest.Awake" && trace[m]["phase"].s == "post") post = &trace[m];
            if (!post) continue;
            ChestConfig cfg;
            cfg.capacity = static_cast<int>(e["capacity"].i);
            int32_t inc = static_cast<int32_t>(e["dropIncrement"].i);
            std::memcpy(&cfg.dropIncrement, &inc, 4);
            cfg.generateRandomContentOnInit = e["generateRandomContentOnInit"].b;
            for (const Value& d : e["fixedDrop"].arr) {
                ChestDrop cd;
                cd.itemName = d["item"].s;
                cd.minQuantity = static_cast<int>(d["min"].i);
                cd.maxQuantity = static_cast<int>(d["max"].i);
                cd.ngpOnly = d["ngpOnly"].b;
                cd.ownedAmulet = d["ownedAmulet"].b;
                int32_t pb = static_cast<int32_t>(d["prob"].i);
                std::memcpy(&cd.prob, &pb, 4);
                cfg.fixedDrop.push_back(cd);
            }
            if (cfg.generateRandomContentOnInit) continue; // ChestGeneration not ported yet
            Random::SetState(StateFrom(e["state"]));
            GenerateFixedDrop(cfg, e["ngp"].b);
            chestAwake.Check(Same(Random::GetState(), StateFrom((*post)["state"])), e["object"].s);
            continue;
        }
        if (ev == "effectLists") {
            LoadEffects(e, chestTables[session]);
            haveEffects[session] = true;
            continue;
        }
        if (ev == "chestLists") {
            LoadChestLists(e, chestTables[session]);
            haveLists[session] = true;
            continue;
        }
        if (ev == "chestGen") {
            if (!haveEffects[session] || !haveLists[session]) continue;
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
                if (x[3].b) continue; // owned amulets aren't in the list after Init
                am.amulets.push_back({x[0].s, FloatBits(x[1].i), static_cast<int>(x[2].i)});
                am.maxWeight += FloatBits(x[1].i);
            }
            Random::SetState(StateFrom(e["before"]));
            std::string ex;
            std::vector<ChestStack> got;
            try {
                got = GenerateChestContent(a, chestTables[session], owned, am);
            } catch (const std::exception& x) {
                ex = x.what();
            }
            bool ok = ex.empty() && Same(Random::GetState(), StateFrom(e["after"]));
            // Contents: same item, quantity and effect in every slot.
            const Value& want = e["content"];
            if (ok && want.size() == got.size()) {
                for (size_t sl = 0; ok && sl < got.size(); sl++) {
                    const Value& w = want[sl];
                    if (w.IsNull()) ok = got[sl].itemName.empty();
                    else ok = got[sl].itemName == w[0].s && got[sl].quantity == w[1].i &&
                              (w[2].IsNull() ? got[sl].effect == nullptr : (got[sl].effect && got[sl].effect->name == w[2].s));
                }
            }
            int d = Random::DrawsBetween(StateFrom(e["before"]), StateFrom(e["after"]), 100000);
            chestGen.Check(ok, "floor " + std::to_string(floor) + " type " + std::to_string(a.chestType) + " game drew " + std::to_string(d) +
                                   (ex.empty() ? "" : " (" + ex + ")") + (am.initialized ? "" : " [amulets not initialised yet]"));
            continue;
        }
        if (ev == "detailsData") {
            detailsData[{session, static_cast<int>(e["culture"].i), static_cast<int>(e["level"].i)}] = DetailsFrom(e);
            continue;
        }
        if (ev == "detailsRoom") {
            auto key3 = std::make_tuple(session, floor, e["room"].i);
            RoomFill* rf = fills.count(key3) ? fills[key3] : nullptr;
            auto dd = detailsData.find({session, static_cast<int>(e["culture"].i), static_cast<int>(e["level"].i)});
            if (dd == detailsData.end()) continue;
            DetailsRoom room;
            room.culture = static_cast<Culture>(e["culture"].i);
            room.floorCulture = static_cast<Culture>(e["floorCulture"].i);
            room.spawners = static_cast<int>(e["spawners"].i);
            room.roomFloorWidth = static_cast<int>(e["roomFloorWidth"].i);
            room.roomFloorHeight = static_cast<int>(e["roomFloorHeight"].i);
            for (const Value& z : e["zones"].arr) room.zones.push_back({FloatBits(z[0].i), FloatBits(z[1].i), FloatBits(z[2].i), FloatBits(z[3].i)});
            const Value& pr = e["probs"];
            room.wallProb = FloatBits(pr[0].i);
            room.middleProb = FloatBits(pr[1].i);
            room.particlesProb = FloatBits(pr[2].i);
            room.breakablesProb = FloatBits(pr[3].i);
            room.creaturesProb = FloatBits(pr[4].i);
            room.middlePerZone = static_cast<int>(e["middlePerZone"].i);
            room.breakablesPerZone = static_cast<int>(e["breakablesPerZone"].i);
            room.creaturesPerZone = static_cast<int>(e["creaturesPerZone"].i);
            std::string where = "floor " + std::to_string(floor) + " room " + std::to_string(e["room"].i);

            // Zones the port computes from its own grid (rooms with a LoadGridFromInfo grid).
            if (rf && !rf->obstaclesGrid.empty()) {
                std::vector<unity::Rect> mine = CreateMiddleDetailZones(rf->obstaclesGrid, rf->gridSlicesX, rf->gridSlicesY, Constants::GetFloat("tileSize"));
                bool ok = mine.size() == room.zones.size();
                for (size_t z = 0; ok && z < mine.size(); z++)
                    ok = mine[z].x == room.zones[z].x && mine[z].y == room.zones[z].y && mine[z].width == room.zones[z].width && mine[z].height == room.zones[z].height;
                zones.Check(ok, where + " port " + std::to_string(mine.size()) + " zones, game " + std::to_string(room.zones.size()));
            }

            // Floor-texture cache: the port's counters, carried per floor, against the game's.
            DetailsCache& cache = floorCache[{session, floor}];
            bool cacheOk = cache.lowerDetailsIndex == e["lowerDetailsIndex"].i && cache.lower2DetailsIndex == e["lower2DetailsIndex"].i;
            detailCache.Check(cacheOk, where + " port " + std::to_string(cache.lowerDetailsIndex) + "/" + std::to_string(cache.lower2DetailsIndex) +
                                           ", game " + std::to_string(e["lowerDetailsIndex"].i) + "/" + std::to_string(e["lower2DetailsIndex"].i));
            DetailsCache run;
            run.lowerDetailsIndex = static_cast<int>(e["lowerDetailsIndex"].i);
            run.lower2DetailsIndex = static_cast<int>(e["lower2DetailsIndex"].i);

            const Value* post = nullptr;
            for (size_t m = k + 1; m < trace.size() && !post; m++)
                if (trace[m]["ev"].s == "stage" && trace[m]["name"].s == "DungeonRoom.SpawnDetails" && trace[m]["phase"].s == "post" &&
                    trace[m]["room"].i == e["room"].i)
                    post = &trace[m];
            if (!post) continue;
            Random::SetState(StateFrom(e["state"]));
            std::string ex;
            try {
                SpawnDetails(room, dd->second, run);
            } catch (const std::exception& x) {
                ex = x.what();
            }
            int d = Random::DrawsBetween(StateFrom(e["state"]), StateFrom((*post)["state"]), 100000);
            details.Check(ex.empty() && Same(Random::GetState(), StateFrom((*post)["state"])), where + " game drew " + std::to_string(d) + (ex.empty() ? "" : " (" + ex + ")"));
            cache = run;
            continue;
        }
        if (ev != "stage") continue;
        const std::string& name = e["name"].s;
        auto key = std::make_tuple(session, floor, e["room"].i);
        RoomFill* f = fills.count(key) ? fills[key] : nullptr;
        bool pre = e["phase"].s == "pre";
        std::string where = "floor " + std::to_string(floor) + " room " + std::to_string(e["room"].i);

        // Find the matching post event (same stage, same room) for pre events.
        auto findPost = [&](size_t from) -> const Value* {
            for (size_t m = from + 1; m < trace.size(); m++) {
                const Value& p = trace[m];
                if (p["ev"].s == "stage" && p["name"].s == name && p["phase"].s == "post" && p["room"].i == e["room"].i &&
                    p["floor"].i == floor && p["session"].s == session)
                    return &p;
            }
            return nullptr;
        };

        if (pre && name == "DungeonRoom.LoadGridFromInfo" && f) {
            const Value* post = findPost(k);
            if (!post) continue;
            bool ok = false;
            for (DungeonRoomInfo* cand : candidates[key]) {
                auto* trial = cs::gcnew<RoomFill>(cand, f->gridSlicesX, f->gridSlicesY);
                Random::SetState(StateFrom(e["state"]));
                trial->LoadGridFromInfo();
                ok = Same(Random::GetState(), StateFrom((*post)["state"])) && (*post)["currentSpawnPack"].i == trial->currentSpawnPack;
                const Value& g = (*post)["grid"];
                ok = ok && g.size() == trial->obstaclesGrid.size();
                for (size_t t = 0; ok && t < g.size(); t++) ok = g[t].i == trial->obstaclesGrid[t];
                if (ok) {
                    *f = *trial;
                    fills[key] = f;
                    break;
                }
            }
            grid.Check(ok, where + " (" + f->roomInfo->name + ")");
        }

        if (pre && name == "DungeonRoom.SpawnEnemies" && f) {
            // Where the port's draws must end: the first SpawnEnemy pre, or SpawnEnemies post.
            const Value* stop = nullptr;
            int championArg = -2, spawnIndex = 0, championAt = -1;
            for (size_t m = k + 1; m < trace.size(); m++) {
                const Value& p = trace[m];
                if (p["ev"].s != "stage" || p["room"].i != e["room"].i) continue;
                if (p["name"].s == "DungeonRoom.SpawnEnemy" && p["phase"].s == "pre") {
                    if (!stop) stop = &p;
                    if (p.Find("args") && p["args"].size() >= 3 && p["args"][2].kind == Value::Kind::Bool && p["args"][2].b) championAt = spawnIndex;
                    spawnIndex++;
                    championArg = 0;
                }
                if (p["name"].s == "DungeonRoom.SpawnEnemies" && p["phase"].s == "post") {
                    if (!stop) stop = &p;
                    break;
                }
            }
            if (!stop) continue;
            Random::SetState(StateFrom(e["state"]));
            int champ = f->ChooseChampion(false);
            bool ok = Same(Random::GetState(), StateFrom((*stop)["state"]));
            if (ok && championArg == 0) ok = champ == championAt;
            champion.Check(ok, where + " (" + f->roomInfo->name + ") port champion " + std::to_string(champ) + ", game " + std::to_string(championAt));
        }

        if (pre && name == "DungeonRoom.GetChestNameByFloor" && f) {
            const Value* post = findPost(k);
            const Value* result = nullptr;
            for (size_t m = k + 1; m < trace.size() && !result; m++)
                if (trace[m]["ev"].s == "result" && trace[m]["name"].s == "DungeonRoom.GetChestNameByFloor") result = &trace[m];
            if (!post || !result) continue;
            int level = static_cast<int>(e["args"][0].i);
            Random::SetState(StateFrom(e["state"]));
            std::string got = f->GetChestNameByFloor(level, true, false);
            bool ok = got == (*result)["value"].s && Same(Random::GetState(), StateFrom((*post)["state"]));
            chests.Check(ok, where + " port " + got + ", game " + (*result)["value"].s);
        }

        if (pre && f && !f->obstaclesGrid.empty() && name.rfind("DungeonRoomGenerator.", 0) == 0) {
            const Value* post = findPost(k);
            Culture culture = static_cast<Culture>(roomCulture[key]);
            RoomGenerator* gen = generatorFor(session, floor);
            std::string what = where + " (" + f->roomInfo->name + ")";
            auto check = [&](Tally& t) {
                bool ok = post && Same(Random::GetState(), StateFrom((*post)["state"]));
                int d = post ? Random::DrawsBetween(StateFrom(e["state"]), StateFrom((*post)["state"]), 100000) : -1;
                t.Check(ok, what + " game drew " + std::to_string(d));
            };
            Random::SetState(StateFrom(e["state"]));
            if (name == "DungeonRoomGenerator.AddLightShadow") {
                // RoomPlaced: SetLights(new List()), SetShadows(new List()), then AddLightShadow.
                gen->SetLights(cs::gcnew<cs::List<int>>());
                gen->SetShadows(cs::gcnew<cs::List<int>>());
                gen->AddLightShadow(culture);
                check(lightShadow);
            } else if (name == "DungeonRoomGenerator.GenerateHolesPrefab") {
                gen->RasterizeHoles(culture, f->obstaclesGrid, f->gridSlicesX, f->gridSlicesY);
                check(holes);
            } else {
                int tile = name == "DungeonRoomGenerator.GenerateSlimePrefab" ? 16
                         : name == "DungeonRoomGenerator.GenerateAcidPrefab" ? 64
                         : name == "DungeonRoomGenerator.GenerateLavaPrefab" ? 128
                         : name == "DungeonRoomGenerator.GenerateElectricPrefab" ? 256 : 0;
                if (tile) {
                    gen->RasterizeSlime(f->obstaclesGrid, f->gridSlicesX, f->gridSlicesY, tile);
                    check(slime);
                }
            }
        }

        // Engine-side draws: stages whose RNG use isn't modelled yet.
        static const char* engineStages[] = {"DungeonRoom.SpawnEnemy", "Chest.Awake", "DungeonRoom.SpawnRoomPrefabs", "DungeonRoom.SpawnDetails",
                                             "DungeonRoomGenerator.AddLightShadow", "DungeonRoomGenerator.GenerateHolesPrefab",
                                             "DungeonRoomGenerator.GenerateRocksPrefab", "DungeonRoomGenerator.GenerateSlimePrefab",
                                             "DungeonRoom.SpawnChest", "BreakableObjectBehaviour.TransformToSkeleton",
                                             "DungeonGenerator.AddNextLevelDoor", "DungeonGenerator.AddSecretRoom", "DungeonGenerator.AddBossRoom",
                                             "DungeonGenerator.SpawnSpecialRoomsAfterGeneration"};
        if (pre) {
            for (const char* s : engineStages)
                if (name == s) {
                    const Value* post = findPost(k);
                    if (!post) break;
                    std::string what = name;
                    if (e.Find("object")) what += " [" + e["object"].s + "]";
                    else if (e.Find("args") && e["args"].size() > 0 && e["args"][0].IsString()) what += " [" + e["args"][0].s + "]";
                    engineDraws[what].Add(Random::DrawsBetween(StateFrom(e["state"]), StateFrom((*post)["state"]), 100000));
                }
        }
    }
    cs::SetCurrentHeap(nullptr);

    grid.Report();
    champion.Report();
    chests.Report();
    lightShadow.Report();
    holes.Report();
    slime.Report();
    chestAwake.Report();
    zones.Report();
    details.Report();
    detailCache.Report();
    chestGen.Report();
    std::printf("\nRNG draws by engine-side stages (not yet ported):\n");
    for (const auto& kv : engineDraws) {
        const DrawStats& s = kv.second;
        if (s.samples == 0) std::printf("  %-70s  n/a (%d unresolved)\n", kv.first.c_str(), s.unknown);
        else std::printf("  %-70s  %4d-%-4d draws  (%d samples%s)\n", kv.first.c_str(), s.min, s.max, s.samples, s.unknown ? ", some unresolved" : "");
    }
    bool ok = grid.fail == 0 && champion.fail == 0 && chests.fail == 0 && lightShadow.fail == 0 && holes.fail == 0 && slime.fail == 0 && chestAwake.fail == 0 && zones.fail == 0 && details.fail == 0 && detailCache.fail == 0 && chestGen.fail == 0 && grid.pass > 0;
    std::printf("\n%s\n", ok ? "ALL MATCH" : (grid.pass == 0 ? "NO DATA" : "MISMATCHES FOUND"));
    return ok ? 0 : 1;
}
