// Whole-room sync: replays every traced room from the RNG state at its LoadGridFromInfo to
// the end of RoomPlaced, running the port for every step, and requires the game's final RNG
// state. Only engine-side inputs come from the trace: chest prefab configs (Chest.Awake), the
// chest item/effect tables and amulet state (ChestGeneration),
// the breakables array length the skeleton pick sees, and the detail data (list sizes,
// probability images, spawner count). Any draw the port doesn't reproduce, anywhere in the
// room, shows up as a mismatch; the report names the first stage that diverged.
//
// Usage: room_sync_test <oracle data dir> <rooms.json> <generation_data_counts.json>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "cs/Object.h"
#include "game/Chest.h"
#include "game/ChestGeneration.h"
#include "game/Constants.h"
#include "game/RoomDatabase.h"
#include "game/RoomDetails.h"
#include "game/RoomFill.h"
#include "game/RoomGenerator.h"
#include "game/SpawnDraws.h"
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

// Per-floor state the game keeps on the floor's DungeonManager.
struct FloorState {
    RoomGenerator* generator = nullptr;
    DetailsCache cache;
    int skeletonsDeployed = 0;
    int level = 0;
    Culture culture = Culture::Golem;
};

struct Outcome {
    bool ok = false;
    bool unsupported = false;
    std::string why;
};

} // namespace

int main(int argc, char** argv)
{
    std::string dir = argc > 1 ? argv[1] : "data/oracle";
    // Game data made from the Steam copy by tools/make_data.py (ML_DATA overrides).
    std::string data = std::getenv("ML_DATA") ? std::getenv("ML_DATA") : "data/extracted";
    std::string roomsPath = argc > 2 ? argv[2] : "data/extracted/rooms.json";
    std::string countsPath = argc > 3 ? argv[3] : "data/extracted/generation_data_counts.json";
    if (!Constants::LoadFromFile(data + "/constants.json")) return 2;
    Value asset = json::Parse(ReadFile(data + "/roomdb_asset.json"));
    Value genCounts = json::Parse(ReadFile(countsPath));

    cs::Heap heap;
    cs::SetCurrentHeap(&heap);
    auto* db = cs::gcnew<DungeonRoomDatabase>();
    db->LoadSpecialRooms(asset);
    db->LoadFromJSON(asset["database"]);
    DungeonRoomInfo* specials[] = {db->campRoomInfo, db->initRoomInfo, db->healingRoomInfo, db->waveRoomInfo, db->sendToHomeRoomInfo,
                                   db->testRoomInfo, db->wandererNestRoomInfo, db->abandonedItemsRoomInfo, db->specialForestRoomInfo};
    auto findInfo = [&](const Value& roomEv) -> DungeonRoomInfo* {
        if (const Value* idx = roomEv.Find("dbIndex"))
            if (idx->i >= 0 && idx->i < db->database.rooms->Count()) return (*db->database.rooms)[static_cast<int>(idx->i)];
        for (DungeonRoomInfo* s : specials)
            if (s && s->name == roomEv["name"].s) return s;
        return nullptr; // older traces without dbIndex: names are ambiguous, skip
    };

    std::vector<Value> trace;
    {
        std::ifstream f(dir + "/trace.jsonl");
        std::string line;
        while (std::getline(f, line))
            if (!line.empty()) trace.push_back(json::Parse(line));
    }

    const char* cultureNames[6] = {nullptr, "Golem", "Forest", "Desert", "Tech", "Unknown"};
    auto freshData = [&](int c) {
        const Value* entry = cultureNames[c] ? genCounts.Find(cultureNames[c]) : nullptr;
        RoomGenerator::generationDataByCulture[c] = entry ? RoomGenerationData::FromCounts((*entry)["counts"]) : nullptr;
    };

    std::map<std::tuple<std::string, int, int>, DetailsData> detailsData;
    std::map<std::pair<std::string, int64_t>, FloorState> floors;
    std::string currentSession;
    int synced = 0, differ = 0, unsupported = 0;
    std::map<std::string, int> firstStage; // where unsynced rooms first diverge
    std::map<std::string, int> skipReasons;
    std::vector<std::string> notes;

    // Detail data is dumped once per culture/level, inside the first such room's events:
    // load it all up front so that room can use it too.
    // Same for the effect tables (dumped once per session, sometimes inside the first chest's events).
    std::map<std::string, ChestTables> chestTables; // per session; lists replaced per floor
    std::map<std::string, bool> haveLists;
    for (const Value& e : trace) {
        if (e["ev"].s == "detailsData")
            detailsData[{e["session"].s, static_cast<int>(e["culture"].i), static_cast<int>(e["level"].i)}] = DetailsFrom(e);
        if (e["ev"].s == "effectLists" && chestTables[e["session"].s].effects.empty()) LoadEffects(e, chestTables[e["session"].s]);
    }

    for (size_t k = 0; k < trace.size(); k++) {
        const Value& e = trace[k];
        const std::string& ev = e["ev"].s;
        const std::string& session = e["session"].s;
        int64_t floor = e["floor"].i;
        if (session != currentSession) {
            currentSession = session;
            for (int c = 0; c < 6; c++) freshData(c);
        }
        if (ev == "chestLists") {
            LoadChestLists(e, chestTables[session]);
            haveLists[session] = true;
            continue;
        }
        if (ev == "floorBegin") {
            FloorState& fs = floors[{session, floor}];
            fs.generator = cs::gcnew<RoomGenerator>();
            fs.level = e.Find("level") ? static_cast<int>(e["level"].i) : 0;
            fs.culture = e.Find("culture") ? static_cast<Culture>(e["culture"].i) : Culture::Golem;
            if (static_cast<int>(fs.culture) > 0 && static_cast<int>(fs.culture) < 6) freshData(static_cast<int>(fs.culture));
            continue;
        }
        if (ev != "room" || !e.Find("dbIndex") || e["type"].s != "DungeonRoom") continue;
        auto fit = floors.find({session, floor});
        if (fit == floors.end()) continue;
        FloorState& fs = fit->second;
        DungeonRoomInfo* info = findInfo(e);
        if (!info) continue;
        int64_t roomId = e["room"].i;

        // This room's events, up to the end of its RoomPlaced.
        std::vector<const Value*> evs;
        const Value* placedPost = nullptr;
        for (size_t m = k + 1; m < trace.size(); m++) {
            const Value& p = trace[m];
            if (p["session"].s != session || p["floor"].i != floor) break;
            if (p["ev"].s == "room") break; // next room (secret rooms are their own event)
            evs.push_back(&p);
            if (p["ev"].s == "stage" && p["name"].s == "DungeonRoom.RoomPlaced" && p["phase"].s == "post" && p["room"].i == roomId) {
                placedPost = &p;
                break;
            }
        }
        const Value* gridPre = nullptr;
        for (const Value* p : evs)
            if ((*p)["ev"].s == "stage" && (*p)["name"].s == "DungeonRoom.LoadGridFromInfo" && (*p)["phase"].s == "pre") gridPre = p;
        if (!placedPost || !gridPre) continue;

        // Engine-side inputs, in order.
        std::vector<const Value*> chestCfgs, detailRooms, skeletons;
        for (const Value* p : evs) {
            const std::string& t = (*p)["ev"].s;
            if (t == "chestConfig") chestCfgs.push_back(p);
            else if (t == "detailsRoom") detailRooms.push_back(p);
            else if (t == "skeleton") skeletons.push_back(p);
        }
        size_t nextChest = 0;
        const ChestTables* tables = haveLists[session] && !chestTables[session].effects.empty() ? &chestTables[session] : nullptr;
        // GenerateChestContent for chest config `c` (the next chestGen before the next config).
        auto randomContents = [&](size_t c) -> bool {
            size_t from = 0;
            while (from < evs.size() && evs[from] != chestCfgs[c]) from++;
            for (size_t m = from + 1; m < evs.size(); m++) {
                const std::string& t = (*evs[m])["ev"].s;
                if (t == "chestConfig") break;
                if (t == "chestGen" && tables) {
                    RunChestGen(*evs[m], *tables);
                    return true;
                }
            }
            return false;
        };
        auto stagePre = [&](const char* name) -> const Value* {
            for (const Value* p : evs)
                if ((*p)["ev"].s == "stage" && (*p)["name"].s == name && (*p)["phase"].s == "pre") return p;
            return nullptr;
        };
        Outcome out;
        std::string stage = "start";
        // Checkpoint: the port's state must equal the game's at the start of `name`.
        auto checkpoint = [&](const char* name) -> bool {
            const Value* p = stagePre(name);
            if (!p) return true;
            if (Same(Random::GetState(), StateFrom((*p)["state"]))) {
                stage = name;
                return true;
            }
            int gd = -1;
            if (const Value* prevPre = stagePre(stage.c_str()))
                gd = Random::DrawsBetween(StateFrom((*prevPre)["state"]), StateFrom((*p)["state"]), 100000);
            out.why = std::string("diverged before ") + name + " (after " + stage + "; game drew " + std::to_string(gd) + " there)";
            firstStage[std::string("before ") + name]++;
            return false;
        };

        cs::Heap roomHeap;
        cs::SetCurrentHeap(&roomHeap);
        Random::SetState(StateFrom((*gridPre)["state"]));
        RoomFill fill(info, static_cast<int>(e["gridSlicesX"].i), static_cast<int>(e["gridSlicesY"].i));
        Culture roomCulture = static_cast<Culture>(e["culture"].i);
        bool ok = true;
        try {
            fill.LoadGridFromInfo();
            ok = checkpoint("DungeonRoom.SpawnEnemies");
            // SpawnEnemies: champion roll, then each enemy of the pack.
            if (ok) {
                int champ = fill.ChooseChampion(false);
                (void)champ;
                if (info->enemiesSpawnPacks && info->enemiesSpawnPacks->Count() > 0) {
                    auto* pack = (*info->enemiesSpawnPacks)[fill.currentSpawnPack];
                    for (auto* spawn : *pack->spawns) EnemyInit(spawn->enemy ? spawn->enemy->name : "");
                }
                ok = checkpoint("DungeonRoom.SpawnChests");
            }
            // SpawnChests: tier/mimic per chest tile, then the chest's Awake.
            if (ok) {
                for (size_t t = 0; t < fill.obstaclesGrid.size(); t++) {
                    if (fill.obstaclesGrid[t] != 32) continue;
                    std::string name = fill.GetChestNameByFloor(fs.level, true, false);
                    if (name.find("Mimic") != std::string::npos) continue; // mimics are enemies: no Init draws
                    if (nextChest >= chestCfgs.size()) {
                        out.unsupported = true;
                        out.why = "missing chest config";
                        break;
                    }
                    ChestConfig cfg = ChestFrom(*chestCfgs[nextChest++]);
                    GenerateFixedDrop(cfg, (*chestCfgs[nextChest - 1])["ngp"].b);
                    if (cfg.generateRandomContentOnInit && !randomContents(nextChest - 1)) {
                        out.unsupported = true;
                        out.why = "chest with random contents, no generation data (older trace)";
                        break;
                    }
                }
                if (!out.unsupported) ok = checkpoint("DungeonRoomGenerator.AddLightShadow");
            }
            if (ok && !out.unsupported && !fill.obstaclesGrid.empty()) {
                // DungeonRoomGenerator passes, in RoomPlaced's order (rocks, middle zones,
                // background and falling floors draw nothing).
                fs.generator->SetLights(cs::gcnew<cs::List<int>>());
                fs.generator->SetShadows(cs::gcnew<cs::List<int>>());
                fs.generator->AddLightShadow(roomCulture);
                fs.generator->RasterizeHoles(roomCulture, fill.obstaclesGrid, fill.gridSlicesX, fill.gridSlicesY);
                for (int tile : {16, 64, 128, 256}) fs.generator->RasterizeSlime(fill.obstaclesGrid, fill.gridSlicesX, fill.gridSlicesY, tile);
                ok = checkpoint("DungeonRoom.SpawnRoomPrefabs");
                // SpawnRoomPrefabs: chests inside prefabs, then the skeleton roll.
                if (ok) {
                    while (nextChest < chestCfgs.size()) {
                        ChestConfig cfg = ChestFrom(*chestCfgs[nextChest]);
                        GenerateFixedDrop(cfg, (*chestCfgs[nextChest])["ngp"].b);
                        if (cfg.generateRandomContentOnInit && !randomContents(nextChest)) {
                            out.unsupported = true;
                            out.why = "prefab chest with random contents, no generation data (older trace)";
                            break;
                        }
                        nextChest++;
                    }
                    if (!out.unsupported && skeletons.empty() && stagePre("BreakableObjectBehaviour.TransformToSkeleton")) {
                        out.unsupported = true; // trace predates the skeleton hook: breakable count unknown
                        out.why = "skeleton pick without recorded inputs (older trace)";
                    }
                    if (!out.unsupported) {
                        int n = 0, skins = 0, deployed = fs.skeletonsDeployed;
                        if (!skeletons.empty()) {
                            n = static_cast<int>((*skeletons[0])["candidates"].size());
                            skins = static_cast<int>((*skeletons[0])["skins"].i);
                            deployed = static_cast<int>((*skeletons[0])["deployed"].i);
                        }
                        SkeletonPick pick = RollSkeleton(n, deployed, skins);
                        if (pick.rolled && skeletons.empty() && n == 0) {
                            // Rolled but the game made no pick event: the scene had no breakables.
                        }
                        fs.skeletonsDeployed = deployed;
                        ok = checkpoint("DungeonRoom.SpawnDetails");
                    }
                }
            }
            if (ok && !out.unsupported) {
                if (detailRooms.empty()) {
                    out.unsupported = true;
                    out.why = "no detail inputs recorded";
                } else {
                    const Value& d = *detailRooms[0];
                    auto dd = detailsData.find({session, static_cast<int>(d["culture"].i), static_cast<int>(d["level"].i)});
                    if (dd == detailsData.end()) {
                        out.unsupported = true;
                        out.why = "no detail data recorded";
                    } else {
                        DetailsRoom room;
                        room.culture = static_cast<Culture>(d["culture"].i);
                        room.floorCulture = static_cast<Culture>(d["floorCulture"].i);
                        room.spawners = static_cast<int>(d["spawners"].i);
                        room.roomFloorWidth = static_cast<int>(d["roomFloorWidth"].i);
                        room.roomFloorHeight = static_cast<int>(d["roomFloorHeight"].i);
                        room.zones = CreateMiddleDetailZones(fill.obstaclesGrid, fill.gridSlicesX, fill.gridSlicesY, Constants::GetFloat("tileSize"));
                        const Value& pr = d["probs"];
                        room.wallProb = FloatBits(pr[0].i);
                        room.middleProb = FloatBits(pr[1].i);
                        room.particlesProb = FloatBits(pr[2].i);
                        room.breakablesProb = FloatBits(pr[3].i);
                        room.creaturesProb = FloatBits(pr[4].i);
                        room.middlePerZone = static_cast<int>(d["middlePerZone"].i);
                        room.breakablesPerZone = static_cast<int>(d["breakablesPerZone"].i);
                        room.creaturesPerZone = static_cast<int>(d["creaturesPerZone"].i);
                        // Cache counters as the game had them (verified separately by fill_oracle_test),
                        // so rooms this test skips don't leave the floor's cache out of step.
                        fs.cache.lowerDetailsIndex = static_cast<int>(d["lowerDetailsIndex"].i);
                        fs.cache.lower2DetailsIndex = static_cast<int>(d["lower2DetailsIndex"].i);
                        SpawnDetails(room, dd->second, fs.cache);
                        stage = "DungeonRoom.SpawnDetails";
                    }
                }
            }
            if (ok && !out.unsupported) {
                if (Same(Random::GetState(), StateFrom((*placedPost)["state"]))) out.ok = true;
                else {
                    int extra = Random::DrawsBetween(Random::GetState(), StateFrom((*placedPost)["state"]), 100000);
                    out.why = "end state differs after " + stage + (extra >= 0 ? " (game drew " + std::to_string(extra) + " more)" : "");
                    firstStage["after " + stage]++;
                }
            }
        } catch (const std::exception& x) {
            out.why = std::string("exception: ") + x.what();
        }
        if (out.unsupported && stage.find("AddLightShadow") == std::string::npos && stage.find("SpawnRoomPrefabs") == std::string::npos &&
            stage.find("SpawnDetails") == std::string::npos) {
            // The room stopped before its generator passes: run AddLightShadow anyway (RNG
            // saved/restored) so the floor's light lists end up as the game left them.
            Random::State keep = Random::GetState();
            fs.generator->SetLights(cs::gcnew<cs::List<int>>());
            fs.generator->SetShadows(cs::gcnew<cs::List<int>>());
            fs.generator->AddLightShadow(roomCulture);
            Random::SetState(keep);
        }
        cs::SetCurrentHeap(&heap);
        if (out.ok) synced++;
        else if (out.unsupported) {
            unsupported++;
            skipReasons[out.why]++;
        }
        else {
            differ++;
            if (notes.size() < 8) notes.push_back("floor " + std::to_string(floor) + " room " + std::to_string(roomId) + " (" + info->name + "): " + out.why);
        }
    }

    std::printf("rooms in sync (start to end)  %6d match  %6d differ\n", synced, differ);
    std::printf("rooms not checkable yet       %6d (older traces missing inputs)\n", unsupported);
    for (const auto& kv : skipReasons) std::printf("    skipped: %-58s %d\n", kv.first.c_str(), kv.second);
    for (const std::string& n : notes) std::printf("    %s\n", n.c_str());
    for (const auto& kv : firstStage) std::printf("  first divergence %-48s %d rooms\n", kv.first.c_str(), kv.second);
    bool all = differ == 0 && synced > 0;
    std::printf("\n%s\n", all ? "ALL MATCH" : (synced == 0 ? "NO DATA" : "MISMATCHES FOUND"));
    return all ? 0 : 1;
}
