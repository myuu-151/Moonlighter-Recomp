#pragma once
// DungeonRoom.SpawnDetails and its helpers (DungeonRoom.cs): wall details, the two floor
// texture layers (sampled from probability images, cached across rooms), middle-zone
// sprites, particles, breakables (with overlap rejection) and creatures.
// Sprites/prefabs are identified by index; what matters for exactness is the sequence of
// draws, which depends on list sizes, probability-image pixels and sprite texture sizes.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <map>

#include "game/RoomDatabase.h"
#include "util/Json.h"
#include "unity/Rect.h"

namespace game {

// DetailsPrefabRegister data for one culture/level.
struct DetailsData {
    struct Size {
        int w = 0, h = 0;
        bool null = false; // a null entry in the sprite list
    };
    struct ProbImage {
        int w = 0, h = 0;
        std::vector<uint8_t> red; // GetPixels32 order: red[x + y * w]
        bool null = false;
    };
    int breakables = 0, particles = 0, creatures = 0, walls = 0;
    // Prefab names (empty when the data came from a trace that only recorded counts).
    std::vector<std::string> breakableNames, particleNames, creatureNames, wallNames;
    std::vector<Size> lower, lower2, middle;
    std::vector<ProbImage> lowerProb, lower2Prob;
};

// DungeonRoom's static caches of generated floor textures (reset when the floor's
// DungeonManager is destroyed).
struct DetailsCache {
    int lowerDetailsIndex = 0;
    int lower2DetailsIndex = 0;
    static constexpr int kLength = 3; // CACHE_DETAILS_LENGTH
};

struct DetailsRoom {
    Culture culture = Culture::Golem;      // roomInfo.culture
    Culture floorCulture = Culture::Golem; // DungeonManager.culture
    int spawners = -1;                     // detailSpawners.childCount (-1: no spawners object)
    int roomFloorWidth = 484, roomFloorHeight = 268;
    std::vector<unity::Rect> zones; // roomInfo.detailGenerationInfo.middleDetailZones
    float wallProb = 0.75f, middleProb = 0.0f, particlesProb = 1.0f, breakablesProb = 0.2f, creaturesProb = 0.2f;
    int middlePerZone = 1, breakablesPerZone = 5, creaturesPerZone = 1;
};

struct DetailsResult {
    int walls = 0, lowerHits = 0, lower2Hits = 0, middle = 0, breakables = 0, creatures = 0;
    bool particles = false;
    // What was placed: list indices, and positions relative to the room.
    struct Placed {
        int index = -1;
        float x = 0.0f, y = 0.0f;
    };
    std::vector<std::pair<int, int>> wallPicks; // (spawner child index, wall prefab)
    int particlePick = -1;
    std::vector<Placed> breakablePicks, creaturePicks;
};

// SpawnDetails() for one room.
DetailsResult SpawnDetails(const DetailsRoom& room, const DetailsData& data, DetailsCache& cache);

// DetailsPrefabRegister data extracted from the bundles (data/extracted/details.json):
// (culture, level) -> data.
std::map<std::pair<int, int>, DetailsData> LoadDetailsData(const json::Value& extracted);

// DungeonRoomGenerator.CreateMiddleDetailZones: greedy rectangles over free tiles
// (values 1..10 block). Uses the generator's own 20x11 slice fields for indexing/centring.
std::vector<unity::Rect> CreateMiddleDetailZones(const std::vector<int>& grid, int roomSlicesX, int roomSlicesY, float tileSize,
                                                 int generatorSlicesX = 20, int generatorSlicesY = 11);

} // namespace game
