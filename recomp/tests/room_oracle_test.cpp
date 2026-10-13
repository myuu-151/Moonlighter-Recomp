// Checks room-design selection against floors traced in the real game (trace.jsonl,
// written by the oracle's RoomOracle hooks, plus the matching cave_game.jsonl records).
//
// For each traced floor: rebuild the layout with the port (from the recorded RNG state),
// then walk GenerateDungeonCoroutine's room loop in the port. Every GetRandomRoom call the
// port makes is matched against the next one the game made: same candidate list and
// weights, same doors and excluded neighbours, and, starting from the game's RNG state,
// the same result and RNG state afterwards. The room placed in each cell must match too.
//
// Usage: room_oracle_test <oracle data dir> <rooms.json (TextAsset)>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "cs/Object.h"
#include "game/Cave.h"
#include "game/Constants.h"
#include "game/DungeonLayout.h"
#include "game/RoomDatabase.h"
#include "game/RoomSelection.h"
#include "unity/Random.h"
#include "util/Json.h"

using json::Value;
using namespace game;
namespace Random = unity::Random;
using Doors = DungeonRoomInfo::Doors;

namespace {

std::string ReadFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::vector<Value> ReadLines(const std::string& path)
{
    std::vector<Value> out;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line))
        if (!line.empty()) out.push_back(json::Parse(line));
    return out;
}

Random::State StateFrom(const Value& v)
{
    return {static_cast<uint32_t>(v[0].i), static_cast<uint32_t>(v[1].i), static_cast<uint32_t>(v[2].i), static_cast<uint32_t>(v[3].i)};
}

bool SameState(const Random::State& a, const Value& v)
{
    Random::State b = StateFrom(v);
    return a.s0 == b.s0 && a.s1 == b.s1 && a.s2 == b.s2 && a.s3 == b.s3;
}

int Bits(float f)
{
    int i;
    std::memcpy(&i, &f, 4);
    return i;
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

struct Floor {
    std::string session;
    int64_t number = 0;
    std::vector<const Value*> events;
};

struct Result {
    int calls = 0, callFails = 0;
    int rooms = 0, roomFails = 0;
    int unverifiedPicks = 0;
    std::string firstProblem;
    void Fail(const std::string& what)
    {
        if (firstProblem.empty()) firstProblem = what;
    }
};

class FloorReplay {
public:
    // `db` is the floor's database (fresh per floor, see main).
    FloorReplay(const Floor& f, const Value& cave, DungeonRoomDatabase* db, Result& res) : floor_(f), cave_(cave), res_(res), db_(db) {}

    void Run()
    {
        const int level = static_cast<int>(cave_["level"].i);
        const Culture culture = static_cast<Culture>(cave_["culture"].i);

        // Layout (already verified by cave_oracle_test; rebuilt here to drive the loop).
        FloorGenerationSaveInfo fi = FloorInfoFrom(cave_["floorInfo"]);
        if (level != 0) fi.lastDoorDirectionIndex = DungeonLayout::GetComplementaryDoorIndex(static_cast<int>(cave_["lastDoorDirectionIndex"].i));
        Random::SetState(StateFrom(cave_["stateAtCaveInit"]));
        DungeonLayout L;
        L.level = level;
        L.culture = static_cast<int>(culture);
        L.Generate(fi, false);

        RoomSelector sel(db_, culture, level);
        cs::List<DungeonRoomInfo*>* rooms = db_->GetRoomsByCulture(culture, level);
        auto* holesOnly = cs::gcnew<cs::List<DungeonRoomInfo*>>();
        for (DungeonRoomInfo* x : *rooms) {
            if (x->obstaclesSpawnPacks->Count() != 1) continue;
            cs::List<DungeonRoomInfo::TileSpawn*>* sp = (*x->obstaclesSpawnPacks)[0]->spawns;
            if (sp->FindIndex([](DungeonRoomInfo::TileSpawn* y) { return y->tile == DungeonRoomInfo::DungeonTile::Holes; }) != -1) holesOnly->Add(x);
        }
        cs::List<DungeonRoomInfo*>* timedChestRooms = db_->GetTimedChestRoomsByCulture(culture, level);
        cs::List<DungeonRoomInfo*>* chestRooms = db_->GetChestRoomsByCulture(culture, level);
        cs::List<DungeonRoomInfo*>* corruptedRooms = db_->GetCorruptedRooms(culture);

        CaveGenerator* cave = L.cave;
        for (int i = 0; i < cave->rooms->Count(); i++) {
            CaveRoom* caveRoom = (*cave->rooms)[i];
            int x = static_cast<int>(caveRoom->position.x - cave->minPos.x);
            int y = static_cast<int>(caveRoom->position.y - cave->minPos.y);
            if (!cs::Is<SingleCaveRoom>(caveRoom)) {
                if (cs::Is<BossCaveRoom>(caveRoom)) {
                    auto* boss = cs::gcnew<PlacedRoom>();
                    boss->x = x;
                    boss->y = y;
                    sel._roomsGenerated->Add(boss); // DungeonBossRoom: roomInfo stays null
                }
                continue;
            }
            CaveRoomUnit* u = (*caveRoom->units)[0];
            bool isBoss = caveRoom == L.bossCaveRoom;
            auto* desired = cs::gcnew<cs::List<Doors>>();
            if (isBoss || u->connectedNorth) desired->Add(Doors::North);
            if (isBoss || u->connectedSouth) desired->Add(Doors::South);
            if (isBoss || u->connectedEast) desired->Add(Doors::East);
            if (isBoss || u->connectedWest) desired->Add(Doors::West);

            DungeonRoomInfo* info = nullptr;
            bool corrupted = caveRoom == L.corruptedCaveRoom;
            if (caveRoom == L.initCaveRoom) info = db_->initRoomInfo;
            else if (caveRoom == L.onsenCaveRoom) info = db_->healingRoomInfo;
            else if (caveRoom == L.crazyPeteCaveRoom) info = db_->campRoomInfo;
            else if (corrupted) {
                // corruptedRooms.GetRandom(): drawn after the previous room's frame yield,
                // with no hook in between, so take the game's pick from the trace.
                const Value* placed = RoomEventAt(x, y);
                if (placed) {
                    for (DungeonRoomInfo* c : *corruptedRooms)
                        if (c->name == (*placed)["name"].s) info = c;
                }
                res_.unverifiedPicks++;
            } else if (caveRoom == L.wavesCaveRoom || caveRoom == L.homeChestCaveRoom || caveRoom == L.wandererNestCaveRoom ||
                       caveRoom == L.abandonedItemsCaveRoom) {
                info = Pick(sel, holesOnly, desired, nullptr);
            } else {
                if (L.timedChestCaveRooms->IndexOf(caveRoom) != -1) info = Pick(sel, timedChestRooms, desired, nullptr);
                if (L.chestCaveRooms->IndexOf(caveRoom) != -1) info = Pick(sel, chestRooms, desired, nullptr);
                if (info == nullptr) info = Pick(sel, rooms, desired, sel.GetContiguousRooms(x, y));
            }
            if (!corrupted && info) info->culture = culture;
            if (isBoss) info = sel.GetMinibossRoom(MinibossOverride());
            if (info == nullptr) info = Pick(sel, rooms, desired, sel.GetContiguousRooms(x, y));

            CheckPlaced(x, y, info);
            auto* placed = cs::gcnew<PlacedRoom>();
            placed->x = x;
            placed->y = y;
            placed->roomInfo = info;
            sel._roomsGenerated->Add(placed);
        }
        if (next_ < calls_.size()) res_.Fail("game made " + std::to_string(calls_.size() - next_) + " more GetRandomRoom calls than the port");
    }

    void Index()
    {
        for (const Value* e : floor_.events) {
            const std::string& ev = (*e)["ev"].s;
            if (ev == "getRandomRoom") calls_.push_back(e);
            else if (ev == "room") roomEvents_.push_back(e);
            else if (ev == "minibossRoom") miniboss_.push_back(e);
        }
    }

private:
    DungeonRoomInfo* Pick(RoomSelector& sel, cs::List<DungeonRoomInfo*>* list, cs::List<Doors>* desired, cs::List<DungeonRoomInfo*>* ignore)
    {
        res_.calls++;
        if (next_ >= calls_.size()) {
            res_.callFails++;
            res_.Fail("port made a GetRandomRoom call the game didn't");
            return sel.GetRandomRoom(list, desired, ignore);
        }
        const Value& ev = *calls_[next_++];
        std::string where = "floor " + std::to_string(floor_.number) + " call " + std::to_string(next_ - 1) + ": ";
        bool ok = true;
        // Candidates, in order, with the weights the game had at that moment.
        const Value& cands = ev["rooms"];
        if (cands.size() != static_cast<size_t>(list->Count())) {
            ok = false;
            res_.Fail(where + "candidate count " + std::to_string(list->Count()) + " vs " + std::to_string(cands.size()));
        } else {
            for (int k = 0; k < list->Count() && ok; k++) {
                DungeonRoomInfo* r = (*list)[k];
                if (r->name != cands[k][0].s) {
                    ok = false;
                    res_.Fail(where + "candidate " + std::to_string(k) + " is " + r->name + ", game had " + cands[k][0].s);
                } else if (Bits(r->weight) != cands[k][2].i) {
                    ok = false;
                    res_.Fail(where + "weight of " + r->name + " differs");
                }
            }
        }
        const Value& doors = ev["doors"];
        bool doorsOk = doors.size() == static_cast<size_t>(desired->Count());
        for (int k = 0; doorsOk && k < desired->Count(); k++) doorsOk = static_cast<int>((*desired)[k]) == doors[k].i;
        if (!doorsOk) {
            ok = false;
            res_.Fail(where + "desired doors differ");
        }
        bool ignoreOk = ev["ignoreIsNull"].b == (ignore == nullptr);
        if (ignoreOk && ignore) {
            const Value& ig = ev["ignore"];
            ignoreOk = ig.size() == static_cast<size_t>(ignore->Count());
            for (int k = 0; ignoreOk && k < ignore->Count(); k++) ignoreOk = (*ignore)[k] && (*ignore)[k]->name == ig[k].s;
        }
        if (!ignoreOk) {
            ok = false;
            res_.Fail(where + "excluded neighbours differ");
        }
        // Same draw from the game's state.
        Random::SetState(StateFrom(ev["before"]));
        DungeonRoomInfo* got = nullptr;
        std::string ex;
        try {
            got = sel.GetRandomRoom(list, desired, ignore);
        } catch (const cs::Thrown& e) {
            ex = e.TypeName();
        }
        const Value& want = ev["result"];
        std::string gotName = got ? got->name : "<" + ex + ">";
        if (want.IsNull() ? got != nullptr : (!got || got->name != want.s)) {
            ok = false;
            res_.Fail(where + "picked " + gotName + ", game picked " + (want.IsNull() ? "null" : want.s));
        } else if (!SameState(Random::GetState(), ev["after"])) {
            ok = false;
            res_.Fail(where + "RNG state after differs");
        } else if (got && ev.Find("resultWeightAfter") && Bits(got->weight) != ev["resultWeightAfter"].i) {
            ok = false;
            res_.Fail(where + "weight after pick differs");
        }
        if (!ok) res_.callFails++;
        return got;
    }

    const Value* RoomEventAt(int x, int y)
    {
        for (const Value* e : roomEvents_)
            if (e->Find("x") && (*e)["x"].i == x && (*e)["y"].i == y) return e;
        return nullptr;
    }

    DungeonRoomInfo* MinibossOverride()
    {
        // The Wanderer DLC can override the miniboss room; take the game's answer when it
        // isn't a plain database room.
        if (minibossNext_ >= miniboss_.size()) return nullptr;
        const Value& r = (*miniboss_[minibossNext_++])["result"];
        if (r.IsNull()) return nullptr;
        for (DungeonRoomInfo* x : *db_->database.rooms)
            if (x->name == r.s) return nullptr; // plain database room: let the port find it
        auto* custom = cs::gcnew<DungeonRoomInfo>();
        custom->name = r.s;
        return custom;
    }

    void CheckPlaced(int x, int y, DungeonRoomInfo* info)
    {
        res_.rooms++;
        const Value* e = RoomEventAt(x, y);
        std::string got = info ? info->name : "<null>";
        if (!e) {
            res_.roomFails++;
            res_.Fail("floor " + std::to_string(floor_.number) + ": no traced room at " + std::to_string(x) + "," + std::to_string(y));
        } else if ((*e)["name"].s != got) {
            res_.roomFails++;
            res_.Fail("floor " + std::to_string(floor_.number) + ": room at " + std::to_string(x) + "," + std::to_string(y) + " is " + got +
                      ", game placed " + (*e)["name"].s);
        }
    }

    const Floor& floor_;
    const Value& cave_;
    Result& res_;
    DungeonRoomDatabase* db_;

    std::vector<const Value*> calls_, roomEvents_, miniboss_;
    size_t next_ = 0, minibossNext_ = 0;
};

} // namespace

int main(int argc, char** argv)
{
    std::string dir = argc > 1 ? argv[1] : "data/oracle";
    // Game data made from the Steam copy by tools/make_data.py (ML_DATA overrides).
    std::string data = std::getenv("ML_DATA") ? std::getenv("ML_DATA") : "data/extracted";
    std::string roomsPath = argc > 2 ? argv[2] : "data/extracted/rooms.json";
    if (!Constants::LoadFromFile(data + "/constants.json")) {
        std::fprintf(stderr, "cannot read constants\n");
        return 2;
    }
    Value roomsText = json::Parse(ReadFile(roomsPath));
    Value asset = json::Parse(ReadFile(data + "/roomdb_asset.json"));
    std::vector<Value> trace = ReadLines(dir + "/trace.jsonl");
    std::vector<Value> caves = ReadLines(dir + "/cave_game.jsonl");

    // Group trace events by floor.
    std::map<std::pair<std::string, int64_t>, Floor> floors;
    std::vector<std::pair<std::string, int64_t>> order;
    for (const Value& e : trace) {
        auto key = std::make_pair(e["session"].s, e["floor"].i);
        if (!floors.count(key)) {
            floors[key].session = key.first;
            floors[key].number = key.second;
            order.push_back(key);
        }
        floors[key].events.push_back(&e);
    }

    // The room database starts each floor fresh from the asset's serialized data: the
    // floor transition reloads the dungeon_common bundle, so DungeonGenerator.roomDatabase
    // (a destroyed object by then) is loaded again. (LoadFromJSON itself does nothing: it
    // FromJsonOverwrites a boxed copy of the `database` struct.) Weights (x0.001 per pick)
    // and culture overwrites therefore persist only within a floor.
    cs::Heap sessionHeap;
    auto freshDb = [&]() {
        auto* db = cs::gcnew<DungeonRoomDatabase>();
        db->LoadSpecialRooms(asset);
        const Value* serialized = asset.Find("database");
        db->LoadFromJSON(serialized && serialized->Find("rooms") ? *serialized : roomsText);
        return db;
    };

    int floorsChecked = 0, floorsOk = 0, skipped = 0;
    Result total;
    for (const auto& key : order) {
        Floor& f = floors[key];
        const Value* begin = nullptr;
        for (const Value* e : f.events)
            if ((*e)["ev"].s == "floorBegin") begin = e;
        // Harvests reuse seeds, so the same RNG state can start floors in different sessions:
        // take the first matching record written at or after this session started.
        const Value* cave = nullptr;
        if (begin) {
            const std::string& sid = f.session; // yyyyMMdd-HHmmss
            std::string start = sid.size() >= 15 ? sid.substr(0, 4) + "-" + sid.substr(4, 2) + "-" + sid.substr(6, 2) + "T" + sid.substr(9, 2) + ":" +
                                                       sid.substr(11, 2) + ":" + sid.substr(13, 2)
                                                 : "";
            for (const Value& c : caves) {
                if (c["stateAtCaveInit"] != (*begin)["state"]) continue;
                const Value* t = c.Find("time");
                std::string ct = t && t->IsString() ? t->s.substr(0, 19) : "";
                if (!start.empty() && !ct.empty() && ct < start) continue;
                cave = &c;
                break;
            }
        }
        if (!cave) {
            skipped++;
            continue;
        }
        cs::SetCurrentHeap(&sessionHeap);
        DungeonRoomDatabase* db = freshDb();
        Result r;
        FloorReplay replay(f, *cave, db, r);
        replay.Index();
        try {
            replay.Run();
        } catch (const std::exception& e) {
            r.Fail(std::string("exception: ") + e.what());
        }
        cs::SetCurrentHeap(nullptr);
        floorsChecked++;
        bool ok = r.callFails == 0 && r.roomFails == 0 && r.firstProblem.empty();
        if (ok) floorsOk++;
        else if (floorsChecked - floorsOk <= 5) std::printf("  floor %lld (culture %lld, level %lld): %s\n", static_cast<long long>(f.number),
                                                           static_cast<long long>((*cave)["culture"].i), static_cast<long long>((*cave)["level"].i),
                                                           r.firstProblem.c_str());
        total.calls += r.calls;
        total.callFails += r.callFails;
        total.rooms += r.rooms;
        total.roomFails += r.roomFails;
        total.unverifiedPicks += r.unverifiedPicks;
    }

    std::printf("\nfloors                 %6d match  %6d differ  (%d without cave record skipped)\n", floorsOk, floorsChecked - floorsOk, skipped);
    std::printf("GetRandomRoom calls    %6d match  %6d differ\n", total.calls - total.callFails, total.callFails);
    std::printf("rooms placed           %6d match  %6d differ\n", total.rooms - total.roomFails, total.roomFails);
    std::printf("corrupted-room picks   %6d taken from trace (draw not hookable)\n", total.unverifiedPicks);
    bool ok = floorsChecked > 0 && floorsOk == floorsChecked;
    std::printf("\n%s\n", ok ? "ALL MATCH" : (floorsChecked == 0 ? "NO FLOORS TO CHECK" : "MISMATCHES FOUND"));
    return ok ? 0 : 1;
}
