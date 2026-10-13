#pragma once
// For the oracle tests built against the translated game code (ML_TRANSLATED=ON): the hand
// port's DungeonLayout, driving the translated DungeonGenerator (recomp/gen/cpp).
//
// Generate runs the layout part of DungeonGenerator.GenerateDungeonCoroutine (from
// `lastDoorDirectionIndex = ...` through `chestCaveRooms = GeneratChestRooms()`, what runs in
// one frame) the way the coroutine does, on a translated DungeonGenerator: every method it
// calls is the game's own code, translated. The coroutine itself is not translated yet: around
// that part it builds the dungeon's scene (GameObjects, DungeonManager), which needs the Unity
// layer (progress/unity_api_gaps.tsv).

#include "game/Cave.h"

namespace translated {

using game::CaveGenerator;
using game::CaveRoom;

// DungeonGenerator.FloorGenerationSaveInfo's fields as a value (the hand port's shape).
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

    // The translated FloorGenerationSaveInfo.ResetValues, on a translated object.
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
    cs::List<CaveRoom*>* usedSpecialCaveRooms = nullptr;

    // `specialRoomAvailable` stands for roomDatabase.GetSpecialDungeonRoom(culture) != null.
    void Generate(const FloorGenerationSaveInfo& floorInfo, bool specialRoomAvailable);

    static int GetComplementaryDoorIndex(int index);
};

} // namespace translated

namespace game {
using FloorGenerationSaveInfo = ::translated::FloorGenerationSaveInfo;
using DungeonLayout = ::translated::DungeonLayout;
} // namespace game
