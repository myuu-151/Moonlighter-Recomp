// The glue between the hand port's test drivers and the translated game code (ML_TRANSLATED=ON):
// see the headers next to this file. Nothing here decides anything the game decides: values
// are copied in and out, and DungeonLayout::Generate runs the layout part of
// DungeonGenerator.GenerateDungeonCoroutine statement for statement on the translated classes.

#include <cstdio>
#include <fstream>

#include "bind/UnityEngine.h"
#include "game.h"
#include "game/Constants.h"
#include "game/DungeonLayout.h"

namespace translated {

namespace {
cs::String* Str(const std::string& s) { return cs::String::FromUtf8(s); }
} // namespace

// ---- Constants ---------------------------------------------------------------------------------

namespace Constants {

bool LoadFromFile(const std::string& path)
{
    if (!std::ifstream(path)) return false;
    size_t slash = path.find_last_of("/\\");
    std::string dir = slash == std::string::npos ? "." : path.substr(0, slash);
    std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = file.find_last_of('.');
    if (dot != std::string::npos) file = file.substr(0, dot);
    UnityEngine::Resources::SetTextRoot(dir);
    cs::StaticScope keep; // the Constants instance outlives the tests' heaps, as it does the game's scenes
    try {
        gen::Constants::ReadFile(Str(file));
    } catch (const cs::Thrown& e) {
        std::fprintf(stderr, "Constants.ReadFile threw %s\n", e.TypeName());
        return false;
    }
    return true;
}

int GetInt(const std::string& key) { return gen::Constants::GetInt(Str(key)); }
float GetFloat(const std::string& key) { return gen::Constants::GetFloat(Str(key)); }
bool GetBool(const std::string& key) { return gen::Constants::GetBool(Str(key)); }

std::vector<float> GetFloatArray(const std::string& key)
{
    std::vector<float> r;
    if (auto* a = gen::Constants::GetFloatArray(Str(key)))
        for (float f : a->vec()) r.push_back(f);
    return r;
}

std::vector<std::string> GetStringArray(const std::string& key)
{
    std::vector<std::string> r;
    if (auto* a = gen::Constants::GetStringArray(Str(key)))
        for (cs::String* s : a->vec()) r.push_back(s ? s->ToUtf8() : std::string());
    return r;
}

std::vector<std::vector<float>> GetFloat2DArray(const std::string& key)
{
    std::vector<std::vector<float>> r;
    if (auto* a = gen::Constants::GetFloat2DArray(Str(key)))
        for (auto* row_ : a->vec()) {
            cs::Array<float>* row = cs::Down<cs::Array<float>*>(row_);
            std::vector<float> v;
            if (row)
                for (float f : row->vec()) v.push_back(f);
            r.push_back(v);
        }
    return r;
}

} // namespace Constants

// ---- FloorGenerationSaveInfo -----------------------------------------------------------------------

namespace {
void CopyIn(gen::DungeonGenerator_FloorGenerationSaveInfo* t, const FloorGenerationSaveInfo& f)
{
    t->lastDoorDirectionIndex = f.lastDoorDirectionIndex;
    t->wavesRoomFloor = f.wavesRoomFloor;
    t->wandererNestRoomFloor = f.wandererNestRoomFloor;
    t->abandonedItemsFloor = f.abandonedItemsFloor;
    t->dungeonToHomeFloor = f.dungeonToHomeFloor;
    t->corruptedRoomFloor = f.corruptedRoomFloor;
    t->crazyPeteRoomFloor = f.crazyPeteRoomFloor;
    t->specialDungeonRoom = f.specialDungeonRoom;
    t->desertHardEnemiesRoom = f.desertHardEnemiesRoom;
}
void CopyOut(FloorGenerationSaveInfo& f, const gen::DungeonGenerator_FloorGenerationSaveInfo* t)
{
    f.lastDoorDirectionIndex = t->lastDoorDirectionIndex;
    f.wavesRoomFloor = t->wavesRoomFloor;
    f.wandererNestRoomFloor = t->wandererNestRoomFloor;
    f.abandonedItemsFloor = t->abandonedItemsFloor;
    f.dungeonToHomeFloor = t->dungeonToHomeFloor;
    f.corruptedRoomFloor = t->corruptedRoomFloor;
    f.crazyPeteRoomFloor = t->crazyPeteRoomFloor;
    f.specialDungeonRoom = t->specialDungeonRoom;
    f.desertHardEnemiesRoom = t->desertHardEnemiesRoom;
}
} // namespace

void FloorGenerationSaveInfo::ResetValues()
{
    auto* t = cs::New<gen::DungeonGenerator_FloorGenerationSaveInfo>();
    CopyIn(t, *this);
    t->ResetValues();
    CopyOut(*this, t);
}

// ---- DungeonLayout -------------------------------------------------------------------------------------

int DungeonLayout::GetComplementaryDoorIndex(int index) { return gen::DungeonGenerator::GetComplementaryDoorIndex(index); }

void DungeonLayout::Generate(const FloorGenerationSaveInfo& floorInfo, bool specialRoomAvailable)
{
    gen::DungeonGenerator* g = cs::New<gen::DungeonGenerator>();
    g->level = level;
    g->culture = static_cast<gen::ItemMaster_Culture>(culture);
    auto* fi = cs::New<gen::DungeonGenerator_FloorGenerationSaveInfo>();
    CopyIn(fi, floorInfo);
    gen::DungeonGenerator::floorGenerationSaveInfo = fi;

    // GenerateDungeonCoroutine, from `if (level == 0) lastDoorDirectionIndex = 1;` on:
    if (g->level == 0) g->lastDoorDirectionIndex = 1;
    else g->lastDoorDirectionIndex = gen::DungeonGenerator::GetComplementaryDoorIndex(fi->lastDoorDirectionIndex);
    int num;
    if (g->level != 0) num = g->level != 1 ? gen::Constants::GetInt(CS_LIT(u"roomsInFloor3")) : gen::Constants::GetInt(CS_LIT(u"roomsInFloor2"));
    else num = gen::Constants::GetInt(CS_LIT(u"roomsInFloor1"));
    g->cave = cs::New<gen::CaveGenerator>();
    g->cave->Init();
    auto* list = cs::New<cs::List<gen::Miner*>>();
    for (int num2 = 0; num2 < 5; num2++) list->Add(g->cave->CreateMiner(cs::New<gen::CommonRoomMiner>()));
    g->initCaveRoom = cs::New<gen::SingleCaveRoom>();
    switch (g->lastDoorDirectionIndex) {
    case 0: cs::Down<gen::CaveRoomUnit*>(g->initCaveRoom->get_units()->get_Item(0))->blockedNorth = true; break;
    case 1: cs::Down<gen::CaveRoomUnit*>(g->initCaveRoom->get_units()->get_Item(0))->blockedSouth = true; break;
    case 2: cs::Down<gen::CaveRoomUnit*>(g->initCaveRoom->get_units()->get_Item(0))->blockedWest = true; break;
    case 3: cs::Down<gen::CaveRoomUnit*>(g->initCaveRoom->get_units()->get_Item(0))->blockedEast = true; break;
    }
    g->cave->AddRoom(g->initCaveRoom);
    int num3 = 100;
    while (g->cave->rooms->get_Count() < num && g->cave->get_areMinersAlive() && num3 > 0) {
        g->cave->IterateMinners();
        num3--;
    }
    g->cave->KillAllMiners();
    if (g->level == 2) {
        g->cave->CreateMiner(cs::New<gen::BossRoomMiner>());
        g->bossCaveRoom = g->GenerateBossRoomCaveRoom();
    } else {
        g->bossCaveRoom = g->GenerateNextLevelCaveRoom();
    }
    g->usedSpecialCaveRooms->Add(g->initCaveRoom);
    g->timedChestCaveRooms = cs::New<cs::List<gen::CaveRoom*>>();
    g->chestCaveRooms = cs::New<cs::List<gen::CaveRoom*>>();
    g->onsenCaveRoom = g->GenerateCaveOnsen();
    if (g->level == fi->dungeonToHomeFloor) g->homeChestCaveRoom = g->GenerateHomeChestCaveRoom();
    if (g->level == fi->wavesRoomFloor) g->wavesCaveRoom = g->GenerateWavesCaveRoom();
    if (g->level == fi->abandonedItemsFloor) g->abandonedItemsCaveRoom = g->GenerateAbandonedItemsCaveRoom();
    if (g->level == fi->wandererNestRoomFloor) g->wandererNestCaveRoom = g->GenerateWandererCaveRoom();
    g->timedChestCaveRooms = g->GenerateTimedChestCaveRooms();
    g->crazyPeteCaveRoom = g->GenerateCrazyPeteCaveRoom();
    if (g->level == fi->specialDungeonRoom && specialRoomAvailable) g->specialDungeonCaveRoom = g->GenerateCrazyPeteCaveRoom();
    if (g->level == fi->corruptedRoomFloor) g->corruptedCaveRoom = g->GenerateCorruptedCaveRoom();
    g->chestCaveRooms = g->GeneratChestRooms();

    // the test reads the hand port's DungeonLayout fields
    cave = g->cave;
    lastDoorDirectionIndex = g->lastDoorDirectionIndex;
    bossCaveRoom = g->bossCaveRoom;
    initCaveRoom = g->initCaveRoom;
    onsenCaveRoom = g->onsenCaveRoom;
    homeChestCaveRoom = g->homeChestCaveRoom;
    wavesCaveRoom = g->wavesCaveRoom;
    wandererNestCaveRoom = g->wandererNestCaveRoom;
    abandonedItemsCaveRoom = g->abandonedItemsCaveRoom;
    timedChestCaveRooms = g->timedChestCaveRooms;
    chestCaveRooms = g->chestCaveRooms;
    crazyPeteCaveRoom = g->crazyPeteCaveRoom;
    specialDungeonCaveRoom = g->specialDungeonCaveRoom;
    corruptedCaveRoom = g->corruptedCaveRoom;
    usedSpecialCaveRooms = g->usedSpecialCaveRooms;
}

} // namespace translated
