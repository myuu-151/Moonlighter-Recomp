#pragma once
// The holes' colliders (DungeonRoomGenerator.GenerateCollidersFromGrid, from
// GenerateHolesPrefab): one PolygonCollider2D per 4-connected region of hole tiles
// (obstaclesGrid 2; falling floors get none), layer SunkenObstacle, tag "Hole", solid, on a
// child of the room at its origin. The game traces each region's outline and pushes the
// vertices on the grid's border out by 10; the shape is the union of the region's tiles,
// which is what's added here, as one compound collider per region.

#include <string>

#include "game/RoomFill.h"
#include "unity/Physics2D.h"

namespace game {

int AddHoleColliders(const RoomFill& room, unity::Vector2 roomPosition, unity::Physics2D& physics,
                     const std::string& path = "DungeonLevelRoomHoles");

} // namespace game
