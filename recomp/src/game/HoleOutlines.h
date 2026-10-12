#pragma once
// DungeonRoomGenerator.GenerateCollidersFromGrid, literally: the outlines of the grid's
// regions of `check` tiles (holes: obstaclesGrid 2) traced corner by corner from each
// region's start tiles, CleanVertexes, CleanRepeatedPaths, then in room units (tileSize per
// tile from the generator's 480 x 264 grid, vertices on the grid's border pushed out by 10)
// one PolygonCollider2D per outline, an outline inside an earlier one (or holding it)
// becoming that collider's next path instead (PolygonInside, IsPointInPolygon,
// InnerPerimeterFromCurrentPerimeter). The vertex order is the game's: PolyNav reads it
// (PolygonCollider2D.points is the first path), and game/HoleColliders uses the shapes.

#include <string>
#include <vector>

#include "unity/Vector2.h"

namespace game {

struct GridCollider {
    std::string name; // the object's: "Collider_<n>" (numbers of merged outlines are skipped)
    std::vector<std::vector<unity::Vector2>> paths; // local to the room
};

// `grid` is gridSlicesX x gridSlicesY (index i + j * gridSlicesX).
std::vector<GridCollider> GenerateCollidersFromGrid(const std::vector<int>& grid, int gridSlicesX, int gridSlicesY, const std::vector<int>& check,
                                                    float tileSize, int colliderShrinkAmount = 0);

// The traced outlines in grid units (tile corners at +-0.5), after CleanRepeatedPaths.
std::vector<std::vector<unity::Vector2>> TraceGridOutlines(const std::vector<int>& grid, int gridSlicesX, int gridSlicesY,
                                                           const std::vector<int>& check);

std::vector<unity::Vector2> InnerPerimeterFromCurrentPerimeter(const std::vector<unity::Vector2>& currentPerimeter, float width);
bool IsPointInPolygon(unity::Vector2 p, const std::vector<unity::Vector2>& polygon);

} // namespace game
