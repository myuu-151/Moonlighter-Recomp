#pragma once
// Where generation puts objects in a room, as the scene holds them (the oracle's floorEnd
// snapshot): each object's path below the room's root and its position relative to the room.
// Covers what the generator decides (enemy spawns and mimics, chests, rocks); what a prefab
// carries inside itself is the prefab's own data.

#include <string>
#include <vector>

#include "game/RoomFill.h"
#include "unity/Vector2.h"

namespace game {

// The DungeonRoom prefab, shared by every room.
constexpr int kRoomFloorWidth = 484, kRoomFloorHeight = 268, kRoomCenterYOffset = 0;
constexpr float kRoomSizeX = 640.0f, kRoomSizeY = 360.0f; // DungeonRoom.size
// roomDoorsBoundaries' local positions (their order doesn't matter to the generator).
extern const unity::Vector2 kDoorBoundaries[4];
// detailSpawners' children ("Details/WallDetails/DetailSpawn"), in child order.
constexpr int kDetailSpawnerCount = 12;
extern const unity::Vector2 kDetailSpawners[kDetailSpawnerCount];

// DungeonRoom.TranslateTileToPosition (local to the room).
unity::Vector2 TranslateTileToPosition(int tile, int gridSlicesX);

// AddSingleDungeonRoom: the room's position in the floor.
inline unity::Vector2 RoomPosition(int x, int y) { return {kRoomSizeX * static_cast<float>(x), kRoomSizeY * static_cast<float>(y)}; }

struct SceneObject {
    std::string path; // below the room root, e.g. "DungeonLevelEnemies/EnemySpawn/Slime(Clone)"
    float x = 0.0f, y = 0.0f;
    std::string sprite; // its SpriteRenderer's sprite, where that's what generation decides ("" = none)
};

// One object of a room's tree, as the scene holds it: its path below the room's root and its
// local transform and SpriteRenderer (what the oracle's floor snapshot records per object).
struct SceneRow {
    std::string path;
    bool active = true;
    float x = 0.0f, y = 0.0f, sx = 1.0f, sy = 1.0f, rotation = 0.0f;
    std::string sprite, layer; // layer empty: no SpriteRenderer
    int order = 0;
    bool flipX = false, flipY = false, rendererEnabled = false;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

// The whole room: the DungeonRoom prefab (as RoomPlaced leaves it) and everything
// generation put in it, prefabs expanded (PrefabLibrary). Rows in no particular order.
std::vector<SceneRow> RoomSceneTree(const RoomFill& room, unity::Vector2 roomPosition = {});

// The suffix of a culture's dungeon and enemy bundles ("golem", ..., "tesla").
const char* CultureBundle(Culture c);

// Objects RoomPlaced creates for `room` (after PlaceRoom), in creation order. `roomPosition`
// is where the room is in the floor (RoomPosition): details are instantiated at a world
// position and then parented to the room, and that round trip rounds.
std::vector<SceneObject> RoomSceneObjects(const RoomFill& room, unity::Vector2 roomPosition = {});

} // namespace game
