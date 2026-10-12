#pragma once
// The data-logic parts of filling a dungeon room (DungeonRoom.cs):
//  - LoadGridFromInfo: pick the enemy pack and resolve random tiles into obstaclesGrid;
//  - the champion decision at the start of SpawnEnemies;
//  - GetChestNameByFloor / GetWandererChestNameByFloor: chest tier and mimic.
// What these spawn (prefab instantiation, enemy Init, Chest.Awake) is engine-side and
// handled separately; the oracle traces the RNG state around each of those.

#include <string>
#include <utility>
#include <vector>

#include "game/RoomDatabase.h"
#include "game/RoomDetails.h"

namespace game {

class RoomFill : public cs::Object {
public:
    // The DungeonRoom prefab serializes a zeroed X*Y obstaclesGrid (LoadGridFromInfo makes
    // X + X*Y). Rooms whose info has no obstacle packs (Camp, Healing, ...) keep it, since
    // LoadGridFromInfo returns early without drawing.
    explicit RoomFill(DungeonRoomInfo* info, int slicesX = 20, int slicesY = 11)
        : roomInfo(info), gridSlicesX(slicesX), gridSlicesY(slicesY), obstaclesGrid(slicesX * slicesY, 0) {}

    DungeonRoomInfo* roomInfo;
    int gridSlicesX = 20;
    int gridSlicesY = 11;
    int currentSpawnPack = 0;
    std::vector<int> obstaclesGrid; // empty = null in C#
    bool canContainMimics = true;
    int roomEnemies = 0; // roomEnemies.Count after RoomPlaced: the pack's enemies plus mimics
    std::vector<std::pair<int, std::string>> chests;  // SpawnChests: (tile, chest or mimic prefab name)
    std::vector<std::pair<int, std::string>> enemies; // SpawnEnemies: (tile, enemy prefab name) of those spawned
    // AddLightShadow and SetBackgroundImages: the sprites' names ("" = none), the shadow's flips.
    struct Art {
        bool set = false;
        std::string light, shadow, background, walls;
        int shadowFlipX = 1, shadowFlipY = 1;
    } art;
    // SpawnDetails: what it placed, from which register, and the room prefab's wall spawners.
    DetailsResult details;
    const DetailsData* detailsData = nullptr;
    int detailSpawners = -1;

    void LoadGridFromInfo();

    // SpawnEnemies up to the first enemy spawn: returns the index of the champion within
    // the current pack (-1 for none) and applies the pack fallback. `afternoon` selects
    // championProbAtNight (TimeManagerV2.CurrrentDayTime == Afternoon).
    int ChooseChampion(bool afternoon);

    // `morning` selects the day chest table; `intermediate` is the DLC intermediate dungeon.
    std::string GetChestNameByFloor(int dungeonLevel, bool morning, bool intermediate);
    std::string GetWandererChestNameByFloor(int dungeonLevel, bool intermediate);

    static const char* CultureName(Culture c);
};

} // namespace game
