#pragma once
// A dungeon room's PolyNav map as the game has it once the hero came in through a door:
// DungeonManager.ChangeRoomTo puts the Dungeon scene's PolyNav2D (inflateRadius 8) at the
// room's cameraObjective with its BoxCollider2D (offset (0, roomCenterYOffset) from
// PreparePolyNavForRoom) sized roomFloorWidth x roomFloorHeight (484 x 268); EnterRoom
// activates the room, so every enabled PolyNavObstacle on an active object of it joins
// navObstacles, in hierarchy order; half a second later the room left behind is disabled
// (its obstacles leave) and DisableRoom regenerates the map with the master. That's the only
// regeneration: the scene's PolyNav2D doesn't regenerate in LateUpdate (regenerateFlag stays
// set; generateOnUpdate off here), so until the room left behind is disabled the enemies' map
// is still that room's.
//
// An obstacle's shape is its object's first Collider2D (PolyNavObstacle.points): a box, or a
// polygon's first path reversed with invertPolygon. The holes' obstacles are
// GenerateHolesPrefab's: one per PolygonCollider2D GenerateCollidersFromGrid makes
// (game/HoleOutlines), on "DungeonLevelRoomHoles/Collider_<n>", invertPolygon set.

#include <memory>
#include <string>
#include <vector>

#include "game/PolyNav.h"
#include "game/RoomFill.h"
#include "game/RoomScene.h"

namespace game {

constexpr float kDungeonPolyNavInflateRadius = 8.0f; // the Dungeon scene's @PolyNav2D

// The room's obstacles in navObstacles' order. `rows` is the room's scene tree
// (RoomSceneTree, or rows as the game's hierarchy holds them); the holes are traced from the
// grid and put where the "DungeonLevelRoomHoles" row is, unless the rows already hold its
// colliders ("DungeonLevelRoomHoles/..."). `tileSize`: Constants' "tileSize".
std::vector<PolyNavObstacle> RoomNavObstacles(const std::vector<SceneRow>& rows, const std::vector<int>& obstaclesGrid, int gridSlicesX,
                                              int gridSlicesY, unity::Vector2 roomPosition, float tileSize);

struct RoomPolyNav {
    std::vector<PolyNavObstacle> obstacles; // what nav.navObstacles points to
    PolyNav2D nav;
    RoomPolyNav() = default;
    RoomPolyNav(const RoomPolyNav&) = delete;
    RoomPolyNav& operator=(const RoomPolyNav&) = delete;
};

// The map after the room was entered (GenerateMap(true) over the room's obstacles).
std::unique_ptr<RoomPolyNav> BuildRoomPolyNav(const std::vector<SceneRow>& rows, const std::vector<int>& obstaclesGrid, int gridSlicesX,
                                              int gridSlicesY, unity::Vector2 roomPosition, float tileSize,
                                              float inflateRadius = kDungeonPolyNavInflateRadius);
// The same for a generated room: RoomSceneTree(room, roomPosition) and its grid.
std::unique_ptr<RoomPolyNav> BuildRoomPolyNav(const RoomFill& room, unity::Vector2 roomPosition);

} // namespace game
