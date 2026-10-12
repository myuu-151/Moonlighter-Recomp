// Checks the C++ port against reference data recorded inside the real game by the
// BepInEx oracle (oracle/MoonlighterOracle):
//   rng.jsonl         UnityEngine.Random outputs per seed and call type
//   cave_mirror.jsonl dungeon layouts produced by the game's own code from fixed seeds
//   cave_game.jsonl   layouts captured from real play (optional)
//
// Usage: cave_oracle_test <oracle data dir>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>

#include "cs/Object.h"
#include "game/Cave.h"
#include "game/Constants.h"
#include "game/DungeonLayout.h"
#include "unity/Random.h"
#include "util/Json.h"

using json::Value;
using namespace game;
namespace Random = unity::Random;

namespace {

Value Int(int64_t v)
{
    Value r;
    r.kind = Value::Kind::Int;
    r.i = v;
    return r;
}

Value Str(const std::string& s)
{
    Value r;
    r.kind = Value::Kind::String;
    r.s = s;
    return r;
}

Value Arr()
{
    Value r;
    r.kind = Value::Kind::Array;
    return r;
}

Value Obj()
{
    Value r;
    r.kind = Value::Kind::Object;
    return r;
}

void Put(Value& o, const char* k, Value v) { o.obj.emplace_back(k, std::move(v)); }

int Bits(float f)
{
    int i;
    std::memcpy(&i, &f, 4);
    return i;
}

float FromBits(int64_t i)
{
    int32_t v = static_cast<int32_t>(i);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

Value Vec(unity::Vector2 v)
{
    Value a = Arr();
    a.arr.push_back(Int(Bits(v.x)));
    a.arr.push_back(Int(Bits(v.y)));
    return a;
}

Value StateValue(const Random::State& s)
{
    Value a = Arr();
    for (uint32_t v : {s.s0, s.s1, s.s2, s.s3}) a.arr.push_back(Int(v));
    return a;
}

Random::State StateFrom(const Value& v)
{
    return {static_cast<uint32_t>(v[0].i), static_cast<uint32_t>(v[1].i), static_cast<uint32_t>(v[2].i), static_cast<uint32_t>(v[3].i)};
}

// Same structure as CaveOracle.WriteLayout in the plugin.
Value LayoutValue(const DungeonLayout& L)
{
    if (!L.cave) return Value();
    CaveGenerator* cave = L.cave;
    auto idx = [&](CaveRoom* r) -> int64_t {
        if (!r) return -1;
        int i = cave->rooms->IndexOf(r);
        return i >= 0 ? i : -2;
    };
    Value out = Obj();
    Put(out, "minPos", Vec(cave->minPos));
    Put(out, "maxPos", Vec(cave->maxPos));
    Put(out, "miners", Int(cave->miners->Count()));
    Value rooms = Arr();
    for (CaveRoom* r : *cave->rooms) {
        Value ro = Obj();
        Put(ro, "t", Str(cs::Is<BossCaveRoom>(r) ? "B" : cs::Is<SingleCaveRoom>(r) ? "S" : "?"));
        Put(ro, "p", Vec(r->position));
        Value units = Arr();
        for (CaveRoomUnit* u : *r->units) {
            Value uo = Obj();
            Put(uo, "l", Vec(u->localPos));
            Put(uo, "n", Int(idx(u->connectedNorth)));
            Put(uo, "s", Int(idx(u->connectedSouth)));
            Put(uo, "e", Int(idx(u->connectedEast)));
            Put(uo, "w", Int(idx(u->connectedWest)));
            Put(uo, "b", Int((u->blockedNorth ? 1 : 0) | (u->blockedSouth ? 2 : 0) | (u->blockedEast ? 4 : 0) | (u->blockedWest ? 8 : 0)));
            units.arr.push_back(std::move(uo));
        }
        Put(ro, "u", std::move(units));
        Value path = Arr();
        for (unity::Vector2 v : *r->pathFromInit) path.arr.push_back(Vec(v));
        Put(ro, "path", std::move(path));
        rooms.arr.push_back(std::move(ro));
    }
    Put(out, "rooms", std::move(rooms));
    Value sp = Obj();
    Put(sp, "initCaveRoom", Int(idx(L.initCaveRoom)));
    Put(sp, "bossCaveRoom", Int(idx(L.bossCaveRoom)));
    Put(sp, "onsenCaveRoom", Int(idx(L.onsenCaveRoom)));
    Put(sp, "homeChestCaveRoom", Int(idx(L.homeChestCaveRoom)));
    Put(sp, "wavesCaveRoom", Int(idx(L.wavesCaveRoom)));
    Put(sp, "wandererNestCaveRoom", Int(idx(L.wandererNestCaveRoom)));
    Put(sp, "abandonedItemsCaveRoom", Int(idx(L.abandonedItemsCaveRoom)));
    Put(sp, "crazyPeteCaveRoom", Int(idx(L.crazyPeteCaveRoom)));
    Put(sp, "specialDungeonCaveRoom", Int(idx(L.specialDungeonCaveRoom)));
    Put(sp, "corruptedCaveRoom", Int(idx(L.corruptedCaveRoom)));
    auto list = [&](cs::List<CaveRoom*>* l) {
        Value a = Arr();
        if (l)
            for (CaveRoom* r : *l) a.arr.push_back(Int(idx(r)));
        return a;
    };
    Put(sp, "timedChestCaveRooms", list(L.timedChestCaveRooms));
    Put(sp, "chestCaveRooms", list(L.chestCaveRooms));
    Put(sp, "used", list(L.usedSpecialCaveRooms));
    Put(out, "special", std::move(sp));
    return out;
}

Value FloorInfoValue(const FloorGenerationSaveInfo& f)
{
    Value o = Obj();
    Put(o, "lastDoorDirectionIndex", Int(f.lastDoorDirectionIndex));
    Put(o, "wavesRoomFloor", Int(f.wavesRoomFloor));
    Put(o, "wandererNestRoomFloor", Int(f.wandererNestRoomFloor));
    Put(o, "abandonedItemsFloor", Int(f.abandonedItemsFloor));
    Put(o, "dungeonToHomeFloor", Int(f.dungeonToHomeFloor));
    Put(o, "corruptedRoomFloor", Int(f.corruptedRoomFloor));
    Put(o, "crazyPeteRoomFloor", Int(f.crazyPeteRoomFloor));
    Put(o, "specialDungeonRoom", Int(f.specialDungeonRoom));
    Put(o, "desertHardEnemiesRoom", Int(f.desertHardEnemiesRoom));
    return o;
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

struct Tally {
    const char* name;
    int pass = 0;
    int fail = 0;
    void Report() const { std::printf("%-22s %6d pass  %6d fail\n", name, pass, fail); }
};

bool ForEachLine(const std::string& path, const std::function<void(const Value&)>& fn)
{
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line))
        if (!line.empty()) fn(json::Parse(line));
    return true;
}

// Runs the layout from the given RNG state and compares with the recorded result.
// Returns "" on a match, else a description of the first difference.
std::string CheckLayout(const Value& rec, const FloorGenerationSaveInfo& fi, Random::State start, bool specialAvailable)
{
    cs::Heap heap;
    cs::SetCurrentHeap(&heap);
    Random::SetState(start);
    DungeonLayout L;
    L.level = static_cast<int>(rec["level"].i);
    L.culture = static_cast<int>(rec["culture"].i);
    std::string exception;
    try {
        L.Generate(fi, specialAvailable);
    } catch (const cs::Exception& e) {
        exception = e.TypeName();
    }
    std::string result;
    const Value* recEx = rec.Find("exception");
    if (recEx && (recEx->IsNull() ? !exception.empty() : recEx->s != exception))
        result = "exception: got '" + exception + "'";
    else if (StateValue(Random::GetState()) != rec["stateAfter"])
        result = "rng state after layout differs (different number of draws)";
    else {
        std::string d = json::FirstDifference(LayoutValue(L), rec["layout"], "layout");
        if (!d.empty()) result = "first difference at " + d;
    }
    cs::SetCurrentHeap(nullptr);
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    std::string dir = argc > 1 ? argv[1] : "data/oracle";
    // Game data made from the Steam copy by tools/make_data.py (ML_DATA overrides).
    std::string data = std::getenv("ML_DATA") ? std::getenv("ML_DATA") : "data/extracted";
    if (!Constants::LoadFromFile(data + "/constants.json")) {
        std::fprintf(stderr, "cannot read %s/constants.json\n", data.c_str());
        return 2;
    }

    // ---- RNG ----
    Tally rng{"rng"};
    ForEachLine(dir + "/rng.jsonl", [&](const Value& r) {
        int seed = static_cast<int>(r["seed"].i);
        Random::InitState(seed);
        if (const Value* init = r.Find("init")) {
            (StateValue(Random::GetState()) == *init ? rng.pass : rng.fail)++;
            return;
        }
        const std::string& op = r["op"].s;
        float a = FromBits(r["a"].i), b = FromBits(r["b"].i);
        Value out = Arr();
        for (size_t k = 0; k < r["out"].size(); k++) {
            if (op == "value") out.arr.push_back(Int(Bits(Random::value())));
            else if (op == "int") out.arr.push_back(Int(Random::Range(static_cast<int32_t>(a), static_cast<int32_t>(b))));
            else out.arr.push_back(Int(Bits(Random::Range(a, b))));
        }
        bool ok = out == r["out"] && StateValue(Random::GetState()) == r["end"];
        (ok ? rng.pass : rng.fail)++;
        if (!ok && rng.fail <= 3) std::printf("  rng mismatch: seed %d op %s\n", seed, op.c_str());
    });

    // ---- Layouts from the in-game mirror ----
    Tally floorInfo{"floor info (ResetValues)"};
    Tally mirror{"layout (mirror)"};
    ForEachLine(dir + "/cave_mirror.jsonl", [&](const Value& rec) {
        int seed = static_cast<int>(rec["seed"].i);
        int level = static_cast<int>(rec["level"].i);
        Random::InitState(seed);
        FloorGenerationSaveInfo fi;
        fi.ResetValues();
        fi.lastDoorDirectionIndex = level == 0 ? 0 : (seed & 3);
        bool fiOk = FloorInfoValue(fi) == rec["floorInfo"] && StateValue(Random::GetState()) == rec["stateAtLayout"];
        (fiOk ? floorInfo.pass : floorInfo.fail)++;
        if (!fiOk && floorInfo.fail <= 3) std::printf("  floor info mismatch: seed %d level %d\n", seed, level);

        std::string d = CheckLayout(rec, FloorInfoFrom(rec["floorInfo"]), StateFrom(rec["stateAtLayout"]), false);
        (d.empty() ? mirror.pass : mirror.fail)++;
        if (!d.empty() && mirror.fail <= 5) std::printf("  layout mismatch: seed %d level %d: %s\n", seed, level, d.c_str());
    });

    // ---- Layouts captured from real play ----
    Tally game{"layout (real play)"};
    ForEachLine(dir + "/cave_game.jsonl", [&](const Value& rec) {
        FloorGenerationSaveInfo fi = FloorInfoFrom(rec["floorInfo"]);
        int level = static_cast<int>(rec["level"].i);
        // The generator derives its own door index from the saved one; feed the inverse back.
        if (level != 0) fi.lastDoorDirectionIndex = DungeonLayout::GetComplementaryDoorIndex(static_cast<int>(rec["lastDoorDirectionIndex"].i));
        bool special = rec["layout"]["special"]["specialDungeonCaveRoom"].i >= 0;
        std::string d = CheckLayout(rec, fi, StateFrom(rec["stateAtCaveInit"]), special);
        (d.empty() ? game.pass : game.fail)++;
        if (!d.empty() && game.fail <= 5) std::printf("  real-play mismatch: level %d: %s\n", level, d.c_str());
    });

    std::printf("\n");
    rng.Report();
    floorInfo.Report();
    mirror.Report();
    game.Report();
    bool allOk = rng.fail == 0 && floorInfo.fail == 0 && mirror.fail == 0 && game.fail == 0 && mirror.pass > 0;
    std::printf("\n%s\n", allOk ? "ALL MATCH" : "MISMATCHES FOUND");
    return allOk ? 0 : 1;
}
