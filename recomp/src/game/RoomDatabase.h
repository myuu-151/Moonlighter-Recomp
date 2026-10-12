#pragma once
// DungeonRoomInfo.cs, PrefabSpawnInfo.cs, DungeonRoomDetailsGenerationInfo.cs and
// DungeonRoomDatabase.cs: the hand-made room designs dungeon floors are assembled from.
//
// Loaded from the JSON the game itself uses: the "rooms" TextAsset for `database`
// (re-read on every floor, which resets weights), and a JsonUtility dump of the
// DungeonRoomDatabase asset for the special rooms (init, healing, camp, ...), which
// persist across floors.

#include <string>

#include "cs/List.h"
#include "cs/Object.h"
#include "unity/Rect.h"
#include "unity/Vector2.h"

namespace json {
struct Value;
}

namespace game {

enum class Culture : int { Merchant = 0, Golem, Forest, Desert, Tech, Unknown, Developer, Wanderer, ICT };

class PrefabSpawnInfo : public cs::Object {
public:
    std::string name;
    int quantity = 1;
};

class DungeonRoomDetailsGenerationInfo : public cs::Object {
public:
    float wallDetailGenerationProb = 0.75f;
    int breakablesPerZone = 5;
    float breakablesProbability = 0.2f;
    int creaturesPerZone = 1;
    float creaturesDetailGenerationProb = 0.2f;
    float particlesDetailGenerationProb = 1.0f;
    int middleDetailsPerZone = 1;
    float middleDetailGenerationProb = 0.0f;
    cs::List<unity::Rect>* middleDetailZones = nullptr;
};

class DungeonRoomInfo : public cs::Object {
public:
    enum class DungeonTile : int {
        None = 1,
        Holes = 2,
        None_Holes = 3,
        Rocks = 4,
        Holes_Rocks = 6,
        FallingFloors = 8,
        Slime = 16,
        Chest = 32,
        Acid = 64,
        Lava = 128,
        Electric = 256,
    };
    enum class Doors : int { North = 1, South = 2, West = 4, East = 8 };

    class TileSpawn : public cs::Object {
    public:
        int index = 0;
        DungeonTile tile = DungeonTile::None;
    };
    class ObstaclePack : public cs::Object {
    public:
        cs::List<TileSpawn*>* spawns = cs::gcnew<cs::List<TileSpawn*>>();
    };
    class EnemySpawn : public cs::Object {
    public:
        int index = 0;
        PrefabSpawnInfo* enemy = nullptr;
    };
    class EnemyPack : public cs::Object {
    public:
        cs::List<EnemySpawn*>* spawns = cs::gcnew<cs::List<EnemySpawn*>>();
    };
    class PrefabSpawn : public cs::Object {
    public:
        unity::Vector2 position;
        PrefabSpawnInfo* prefab = cs::gcnew<PrefabSpawnInfo>();
    };

    Culture culture = Culture::Merchant;
    unity::Vector2Int floors;
    std::string name;
    float weight = 1.0f;
    cs::List<Doors>* availableDoors = nullptr;
    DungeonRoomDetailsGenerationInfo* detailGenerationInfo;
    bool canContainChampion = true;
    bool isTimedRoom = false;
    bool isOnLevel1 = true;
    bool isOnLevel2 = true;
    bool isOnLevel3 = true;
    cs::List<ObstaclePack*>* obstaclesSpawnPacks;
    cs::List<EnemyPack*>* enemiesSpawnPacks;
    cs::List<PrefabSpawn*>* prefabSpawnPacks;

    DungeonRoomInfo();

    // JsonUtility.FromJsonOverwrite semantics: fields present in `v` replace the defaults.
    static DungeonRoomInfo* FromJson(const json::Value& v);
};

class DungeonRoomDatabase : public cs::Object {
public:
    struct RoomDatabase {
        cs::List<DungeonRoomInfo*>* rooms = nullptr;
        cs::List<std::string>* GolemCorruptedRooms = nullptr;
        cs::List<std::string>* ForestCorruptedRooms = nullptr;
        cs::List<std::string>* DesertCorruptedRooms = nullptr;
        cs::List<std::string>* TeslaCorruptedRooms = nullptr;
        std::string GolemMinibossRoomName;
        std::string CorruptedGolemMinibossRoomName;
        std::string ForestMinibossRoomName;
        std::string CorruptedForestMinibossRoomName;
        std::string DesertMinibossRoomName;
        std::string CorruptedDesertMinibossRoomName;
        std::string TeslaMinibossRoomName;
        std::string CorruptedTeslaMinibossRoomName;
    };

    DungeonRoomInfo* campRoomInfo = nullptr;
    DungeonRoomInfo* initRoomInfo = nullptr;
    DungeonRoomInfo* healingRoomInfo = nullptr;
    DungeonRoomInfo* waveRoomInfo = nullptr;
    DungeonRoomInfo* sendToHomeRoomInfo = nullptr;
    DungeonRoomInfo* testRoomInfo = nullptr;
    DungeonRoomInfo* wandererNestRoomInfo = nullptr;
    DungeonRoomInfo* abandonedItemsRoomInfo = nullptr;
    DungeonRoomInfo* specialForestRoomInfo = nullptr;

    cs::List<DungeonRoomInfo*>* GolemCorruptedRooms = nullptr;
    cs::List<DungeonRoomInfo*>* ForestCorruptedRooms = nullptr;
    cs::List<DungeonRoomInfo*>* DesertCorruptedRooms = nullptr;
    cs::List<DungeonRoomInfo*>* TeslaCorruptedRooms = nullptr;

    RoomDatabase database;

    // The special-room fields of the ScriptableObject (oracle's roomdb dump, "special" part).
    void LoadSpecialRooms(const json::Value& assetDump);
    // DungeonRoomDatabase.LoadFromJSON: overwrite `database` from the "rooms" TextAsset.
    void LoadFromJSON(const json::Value& roomsText);

    DungeonRoomInfo* GetSpecialDungeonRoom(Culture) { return nullptr; } // always null in this build
    cs::List<DungeonRoomInfo*>* GetRoomsByCulture(Culture culture, int level);
    cs::List<DungeonRoomInfo*>* GetTimedChestRoomsByCulture(Culture culture, int level);
    cs::List<DungeonRoomInfo*>* GetChestRoomsByCulture(Culture culture, int level);
    cs::List<DungeonRoomInfo*>* GetCorruptedRooms(Culture culture);
    DungeonRoomInfo* FindRoom(Culture culture, const std::string& name);

private:
    void UpdateCorruptedRooms();
};

} // namespace game
