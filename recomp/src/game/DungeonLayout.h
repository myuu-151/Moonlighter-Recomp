#pragma once
// The layout half of DungeonGenerator.cs: FloorGenerationSaveInfo, the special-room
// pickers, and GenerateDungeonCoroutine from `lastDoorDirectionIndex = ...` through
// `chestCaveRooms = GeneratChestRooms()` (the part that runs synchronously in one frame).
// Room filling (prefabs, enemies, doors) is not ported yet.

#include "game/Cave.h"

namespace game {

struct FloorGenerationSaveInfo {
    int lastDoorDirectionIndex = 0;
    int wavesRoomFloor = -1;
    int wandererNestRoomFloor = -1;
    int abandonedItemsFloor = -1;
    int dungeonToHomeFloor = -1;
    int corruptedRoomFloor = 0;
    int crazyPeteRoomFloor = -1;
    int specialDungeonRoom = -1;
    int desertHardEnemiesRoom = -1;

    void ResetValues();
};

class DungeonLayout {
public:
    int level = 0;
    int culture = 0;

    CaveGenerator* cave = nullptr;
    int lastDoorDirectionIndex = 0;

    CaveRoom* bossCaveRoom = nullptr;
    CaveRoom* initCaveRoom = nullptr;
    CaveRoom* onsenCaveRoom = nullptr;
    CaveRoom* homeChestCaveRoom = nullptr;
    CaveRoom* wavesCaveRoom = nullptr;
    CaveRoom* wandererNestCaveRoom = nullptr;
    CaveRoom* abandonedItemsCaveRoom = nullptr;
    cs::List<CaveRoom*>* timedChestCaveRooms = nullptr;
    cs::List<CaveRoom*>* chestCaveRooms = nullptr;
    CaveRoom* crazyPeteCaveRoom = nullptr;
    CaveRoom* specialDungeonCaveRoom = nullptr;
    CaveRoom* corruptedCaveRoom = nullptr;
    cs::List<CaveRoom*>* usedSpecialCaveRooms;

    DungeonLayout();

    // `specialRoomAvailable` stands for roomDatabase.GetSpecialDungeonRoom(culture) != null.
    void Generate(const FloorGenerationSaveInfo& floorInfo, bool specialRoomAvailable);

    static int GetComplementaryDoorIndex(int index);

private:
    CaveRoom* GenerateBossRoomCaveRoom();
    CaveRoom* GenerateNextLevelCaveRoom();
    CaveRoom* GenerateCaveOnsen();
    CaveRoom* GenerateHomeChestCaveRoom();
    CaveRoom* GenerateWavesCaveRoom();
    CaveRoom* GenerateAbandonedItemsCaveRoom();
    CaveRoom* GenerateWandererCaveRoom();
    cs::List<CaveRoom*>* GenerateTimedChestCaveRooms();
    bool IsTimedChestRoom(CaveRoom* room);
    CaveRoom* GenerateCrazyPeteCaveRoom();
    CaveRoom* GenerateCorruptedCaveRoom();
    cs::List<CaveRoom*>* GeneratChestRooms();
    int TakeMinDistanceTospecialRooms(CaveRoom* room);
    int DistanceTo(CaveRoom* room1, CaveRoom* room2);

    // Shared body of the "fewest/most connections, not yet used" pickers.
    CaveRoom* PickByConnections(bool most, bool skipInit);
};

} // namespace game
