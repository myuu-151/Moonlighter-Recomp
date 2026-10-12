#include "game/DungeonLayout.h"

#include <algorithm>
#include <climits>
#include <cstdlib>

#include "game/Constants.h"
#include "unity/Random.h"

namespace game {

namespace Random = unity::Random;

// ---- FloorGenerationSaveInfo ----------------------------------------------------------------

void FloorGenerationSaveInfo::ResetValues()
{
    if (Random::Range(0.0f, 1.0f) < Constants::GetFloat("CrazyPeteRoomProbability")) crazyPeteRoomFloor = Random::Range(0, 3);
    corruptedRoomFloor = Random::Range(0, 3);
    if (Random::Range(0.0f, 1.0f) < Constants::GetFloat("DungeonSpecialRoomProbability")) specialDungeonRoom = Random::Range(0, 2);
    if (Random::Range(0.0f, 1.0f) < Constants::GetFloat("DesertHardEnemiesRoomProbability")) desertHardEnemiesRoom = Random::Range(0, 3);
    int num = Random::Range(0, 3);
    int num2 = Random::Range(0, 2);
    num2 += (num2 >= num) ? 1 : 0;
    if (num2 > 2) num2 = 0;
    int num3 = Random::Range(0, 4);
    int num4 = Random::Range(0, 3);
    num4 += (num4 >= num3) ? 1 : 0;
    if (num2 > 3) num2 = 0; // sic: the game checks num2 here, not num4
    switch (num3) {
    case 0: dungeonToHomeFloor = num; break;
    case 1: wavesRoomFloor = num; break;
    case 2: abandonedItemsFloor = num; break;
    case 3: wandererNestRoomFloor = num; break;
    }
    switch (num4) {
    case 0: dungeonToHomeFloor = num2; break;
    case 1: wavesRoomFloor = num2; break;
    case 2: abandonedItemsFloor = num2; break;
    case 3: wandererNestRoomFloor = num2; break;
    }
}

// ---- DungeonLayout --------------------------------------------------------------------------

DungeonLayout::DungeonLayout() : usedSpecialCaveRooms(cs::gcnew<cs::List<CaveRoom*>>()) {}

int DungeonLayout::GetComplementaryDoorIndex(int index)
{
    switch (index) {
    case 0: return 1;
    case 1: return 0;
    case 2: return 3;
    case 3: return 2;
    }
    return -1;
}

void DungeonLayout::Generate(const FloorGenerationSaveInfo& fi, bool specialRoomAvailable)
{
    lastDoorDirectionIndex = level == 0 ? 1 : GetComplementaryDoorIndex(fi.lastDoorDirectionIndex);
    int num = level != 0 ? (level != 1 ? Constants::GetInt("roomsInFloor3") : Constants::GetInt("roomsInFloor2"))
                         : Constants::GetInt("roomsInFloor1");
    cave = cs::gcnew<CaveGenerator>();
    cave->Init();
    for (int i = 0; i < 5; i++) cave->CreateMiner(cs::gcnew<CommonRoomMiner>());

    initCaveRoom = cs::gcnew<SingleCaveRoom>();
    CaveRoomUnit* u0 = (*initCaveRoom->units)[0];
    switch (lastDoorDirectionIndex) {
    case 0: u0->blockedNorth = true; break;
    case 1: u0->blockedSouth = true; break;
    case 2: u0->blockedWest = true; break;
    case 3: u0->blockedEast = true; break;
    }
    cave->AddRoom(initCaveRoom);
    int num3 = 100;
    while (cave->rooms->Count() < num && cave->areMinersAlive && num3 > 0) {
        cave->IterateMinners();
        num3--;
    }
    cave->KillAllMiners();
    if (level == 2) {
        cave->CreateMiner(cs::gcnew<BossRoomMiner>());
        bossCaveRoom = GenerateBossRoomCaveRoom();
    } else {
        bossCaveRoom = GenerateNextLevelCaveRoom();
    }
    usedSpecialCaveRooms->Add(initCaveRoom);
    timedChestCaveRooms = cs::gcnew<cs::List<CaveRoom*>>();
    chestCaveRooms = cs::gcnew<cs::List<CaveRoom*>>();
    onsenCaveRoom = GenerateCaveOnsen();
    if (level == fi.dungeonToHomeFloor) homeChestCaveRoom = GenerateHomeChestCaveRoom();
    if (level == fi.wavesRoomFloor) wavesCaveRoom = GenerateWavesCaveRoom();
    if (level == fi.abandonedItemsFloor) abandonedItemsCaveRoom = GenerateAbandonedItemsCaveRoom();
    if (level == fi.wandererNestRoomFloor) wandererNestCaveRoom = GenerateWandererCaveRoom();
    timedChestCaveRooms = GenerateTimedChestCaveRooms();
    crazyPeteCaveRoom = GenerateCrazyPeteCaveRoom();
    if (level == fi.specialDungeonRoom && specialRoomAvailable) specialDungeonCaveRoom = GenerateCrazyPeteCaveRoom();
    if (level == fi.corruptedRoomFloor) corruptedCaveRoom = GenerateCorruptedCaveRoom();
    chestCaveRooms = GeneratChestRooms();
}

CaveRoom* DungeonLayout::GenerateBossRoomCaveRoom()
{
    CaveRoom* result = nullptr;
    int num = 100;
    while (cave->areMinersAlive && num > 0) {
        cave->IterateMinners();
        num--;
    }
    for (int i = 0; i < cave->rooms->Count(); i++) {
        if (cs::Is<BossCaveRoom>((*cave->rooms)[i])) {
            result = (*cave->rooms)[i];
            break;
        }
    }
    return result;
}

CaveRoom* DungeonLayout::GenerateNextLevelCaveRoom()
{
    SingleCaveRoom* singleCaveRoom = cs::gcnew<SingleCaveRoom>();
    CaveRoomUnit* caveRoomUnit = nullptr;
    Vector2 vector = Vector2::zero();
    for (int num = cave->rooms->Count() - 1; num >= 0; num--) {
        CaveRoom* caveRoom = (*cave->rooms)[num];
        for (int i = 0; i < caveRoom->units->Count(); i++) {
            CaveRoomUnit* u = (*caveRoom->units)[i];
            Vector2 vector2 = u->localPos + caveRoom->position;
            if (u->connectedNorth == nullptr && !cave->usedRoomUnits->ContainsKey(vector2 + Vector2::up())) {
                caveRoomUnit = u;
                singleCaveRoom->position = caveRoom->position + u->localPos + Vector2::up();
                vector = Vector2::up();
                break;
            }
            if (u->connectedSouth == nullptr && !cave->usedRoomUnits->ContainsKey(vector2 - Vector2::up())) {
                caveRoomUnit = u;
                singleCaveRoom->position = caveRoom->position + u->localPos - Vector2::up();
                vector = -Vector2::up();
                break;
            }
            if (u->connectedWest == nullptr && !cave->usedRoomUnits->ContainsKey(vector2 - Vector2::right())) {
                caveRoomUnit = u;
                singleCaveRoom->position = caveRoom->position + u->localPos - Vector2::right();
                vector = -Vector2::right();
                break;
            }
            if (u->connectedEast == nullptr && !cave->usedRoomUnits->ContainsKey(vector2 + Vector2::right())) {
                caveRoomUnit = u;
                singleCaveRoom->position = caveRoom->position + u->localPos + Vector2::right();
                vector = Vector2::right();
                break;
            }
        }
        // sic: a candidate at exactly (0,0) is treated as "not found"
        if (singleCaveRoom->position != Vector2::zero()) break;
    }
    if (singleCaveRoom->position != Vector2::zero()) {
        caveRoomUnit->Connect(vector, singleCaveRoom);
        (*singleCaveRoom->units)[0]->Connect(-vector, caveRoomUnit->room);
        singleCaveRoom->pathFromInit = cs::List<Vector2>::CopyOf(caveRoomUnit->room->pathFromInit);
        singleCaveRoom->pathFromInit->Add(caveRoomUnit->room->position);
        cave->AddRoom(singleCaveRoom);
        usedSpecialCaveRooms->Add(singleCaveRoom);
        return singleCaveRoom;
    }
    return nullptr;
}

CaveRoom* DungeonLayout::GenerateCaveOnsen()
{
    CaveRoom* caveRoom = nullptr;
    cs::List<Vector2>* pathFromInit = cs::NullCheck(bossCaveRoom)->pathFromInit;
    if (pathFromInit->Count() > 0) {
        Vector2 key = (*pathFromInit)[pathFromInit->Count() / 2];
        caveRoom = cave->usedRoomUnits->Get(key)->room;
    }
    usedSpecialCaveRooms->Add(caveRoom);
    return caveRoom;
}

CaveRoom* DungeonLayout::PickByConnections(bool most, bool skipInit)
{
    int best = most ? -1 : INT_MAX;
    CaveRoom* pick = nullptr;
    for (int i = 0; i < cave->rooms->Count(); i++) {
        CaveRoom* r = (*cave->rooms)[i];
        int n = r->GetConnectedCount();
        bool better = most ? n > best : n < best;
        if ((!skipInit || r != initCaveRoom) && usedSpecialCaveRooms->IndexOf(r) == -1 && better) {
            pick = r;
            best = n;
        }
    }
    usedSpecialCaveRooms->Add(pick);
    return pick;
}

CaveRoom* DungeonLayout::GenerateHomeChestCaveRoom() { return PickByConnections(true, true); }
CaveRoom* DungeonLayout::GenerateWavesCaveRoom() { return PickByConnections(false, true); }
CaveRoom* DungeonLayout::GenerateAbandonedItemsCaveRoom() { return PickByConnections(false, true); }
CaveRoom* DungeonLayout::GenerateWandererCaveRoom() { return PickByConnections(false, true); }
CaveRoom* DungeonLayout::GenerateCrazyPeteCaveRoom() { return PickByConnections(false, false); }

cs::List<CaveRoom*>* DungeonLayout::GenerateTimedChestCaveRooms()
{
    auto* list = cs::gcnew<cs::List<CaveRoom*>>();
    // With the shipped constants (min 0, max 1) this is always 0: Range's max is exclusive.
    int num = Random::Range(Constants::GetInt("minTimedChestsPerFloor"), Constants::GetInt("maxTimedChestsPerFloor"));
    for (int i = 1; i <= num; i++) {
        for (int j = 0; j < cave->rooms->Count(); j++) {
            if (IsTimedChestRoom((*cave->rooms)[j])) {
                list->Add((*cave->rooms)[j]);
                break;
            }
        }
    }
    usedSpecialCaveRooms->AddRange(list);
    return list;
}

bool DungeonLayout::IsTimedChestRoom(CaveRoom* room)
{
    int num = 0;
    bool flag = false;
    for (int i = 0; i < room->units->Count(); i++) {
        CaveRoomUnit* u = (*room->units)[i];
        int n = (u->connectedEast != nullptr) + (u->connectedNorth != nullptr) + (u->connectedSouth != nullptr) + (u->connectedWest != nullptr);
        num += n;
    }
    if (num == 2) {
        for (int j = 0; j < room->units->Count(); j++) {
            CaveRoomUnit* u = (*room->units)[j];
            int n = (u->connectedEast != nullptr) + (u->connectedNorth != nullptr) + (u->connectedSouth != nullptr) + (u->connectedWest != nullptr);
            if (n == 2 && (u->connectedEast == nullptr || u->connectedWest == nullptr) && (u->connectedNorth == nullptr || u->connectedSouth == nullptr))
                flag = true;
        }
    }
    if ((num > 2) | flag) return usedSpecialCaveRooms->IndexOf(room) == -1;
    return false;
}

CaveRoom* DungeonLayout::GenerateCorruptedCaveRoom()
{
    int num = INT_MIN;
    CaveRoom* caveRoom = nullptr;
    for (int i = 0; i < cave->rooms->Count(); i++) {
        CaveRoom* r = (*cave->rooms)[i];
        if (r != initCaveRoom && usedSpecialCaveRooms->IndexOf(r) == -1) {
            int d = TakeMinDistanceTospecialRooms(r);
            if (d > num) {
                num = d;
                caveRoom = r;
            }
        }
    }
    usedSpecialCaveRooms->Add(caveRoom);
    return caveRoom;
}

cs::List<CaveRoom*>* DungeonLayout::GeneratChestRooms()
{
    auto* list = cs::gcnew<cs::List<CaveRoom*>>();
    int num, num2;
    if (level == 0) {
        num = Constants::GetInt("minChestsFloor1");
        num2 = Constants::GetInt("maxChestsFloor1");
    } else if (level == 1) {
        num = Constants::GetInt("minChestsFloor2");
        num2 = Constants::GetInt("maxChestsFloor2");
    } else {
        num = Constants::GetInt("minChestsFloor3");
        num2 = Constants::GetInt("maxChestsFloor3");
    }
    int num3 = Random::Range(num, num2 + 1);
    if (num3 > 0) {
        for (int i = 0; i < cave->rooms->Count(); i++) {
            CaveRoom* r = (*cave->rooms)[i];
            if (r != initCaveRoom && usedSpecialCaveRooms->IndexOf(r) == -1 && r != corruptedCaveRoom) list->Add(r);
        }
        list->Sort([](CaveRoom* x, CaveRoom* y) { return cs::CompareTo(y->GetConnectedCount(), x->GetConnectedCount()); });
        int num4 = std::min(list->Count() - 1, num3);
        if (list->Count() - num4 > 0) list->RemoveRange(num4, list->Count() - num4);
    }
    return list;
}

int DungeonLayout::TakeMinDistanceTospecialRooms(CaveRoom* room)
{
    int num = INT_MAX;
    auto consider = [&](CaveRoom* other) {
        int d = DistanceTo(room, other);
        if (d < num) num = d;
    };
    consider(initCaveRoom);
    consider(bossCaveRoom);
    consider(onsenCaveRoom);
    consider(homeChestCaveRoom);
    consider(wavesCaveRoom);
    consider(crazyPeteCaveRoom);
    for (int i = 0; i < timedChestCaveRooms->Count(); i++) consider((*timedChestCaveRooms)[i]);
    return num;
}

int DungeonLayout::DistanceTo(CaveRoom* room1, CaveRoom* room2)
{
    if (room2 == nullptr) return INT_MAX;
    return std::abs(static_cast<int>(room1->position.x - room2->position.x)) + std::abs(static_cast<int>(room1->position.y - room2->position.y));
}

} // namespace game
