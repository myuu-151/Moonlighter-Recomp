#pragma once
// AddSingleDungeonRoom's random part: DungeonRoom.InitializeRoom (LoadGridFromInfo) and
// RoomPlaced (enemies, chests, generator passes, room prefabs + skeleton, details), in the
// game's order. Everything the engine would answer (what a chest prefab contains, how many
// breakables FindObjectsOfType sees, the room prefab's detail spawners, the culture's detail
// data) comes through RoomEngine, so the same code runs against recorded traces and, later,
// against the real engine layer.

#include <string>
#include <vector>

#include "game/Chest.h"
#include "game/RoomDatabase.h"
#include "game/RoomDetails.h"
#include "game/RoomFill.h"
#include "game/RoomGenerator.h"

namespace game {

// Per-floor state the game keeps on the floor's DungeonManager.
struct FloorRoomState {
    RoomGenerator* generator = nullptr;
    DetailsCache cache;
    int skeletonsDeployed = 0;
    int level = 0;
    Culture culture = Culture::Golem;
    // Cultures whose enemy bundle is loaded (the floor's, plus a corrupted room's once it's
    // placed): an enemy whose prefab isn't in them isn't spawned (PrefabCatalog).
    std::vector<Culture> enemyBundles;
};

class RoomEngine {
public:
    virtual ~RoomEngine() = default;
    // Chest.Awake of the next chest instantiated (in SpawnChests or inside a room prefab).
    virtual ChestConfig NextChest(bool& ngp) = 0;
    // ChestGeneration for a chest with random contents (not ported yet): advance the RNG.
    virtual void RandomChestContents(const ChestConfig& chest) = 0;
    // FindObjectsOfType<BreakableObjectBehaviour>().Length at the skeleton pick, skins count.
    virtual int BreakablesInScene() = 0;
    virtual int SkeletonSkins() = 0;
    // Room prefab inputs for SpawnDetails (spawner count, floor size) and the culture's data.
    virtual DetailsRoom RoomPrefab(Culture culture, Culture floorCulture) = 0;
    virtual const DetailsData* Details(Culture culture, int level) = 0;
    // Prefabs in the room's prefabSpawnPacks that contain a Chest (Chest.Awake on Instantiate).
    virtual int ChestsInPrefabs(DungeonRoomInfo* info) = 0;
};

// Runs InitializeRoom + RoomPlaced for one room. Returns the filled room (grid, pack).
RoomFill* PlaceRoom(DungeonRoomInfo* info, int slicesX, int slicesY, Culture roomCulture, FloorRoomState& floor, RoomEngine& engine);

} // namespace game
