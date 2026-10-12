#include "game/HoleColliders.h"

#include <algorithm>
#include <vector>

namespace game {

namespace {

constexpr int kHole = 2;           // obstaclesGrid
constexpr int kSunkenObstacle = 14; // layer
constexpr float kTile = 24.0f;
constexpr float kBorder = 10.0f;    // what the outline gains at the grid's edges

} // namespace

int AddHoleColliders(const RoomFill& room, unity::Vector2 roomPosition, unity::Physics2D& physics, const std::string& path)
{
    const int w = room.gridSlicesX, h = room.gridSlicesY;
    const std::vector<int>& grid = room.obstaclesGrid;
    if (grid.size() < static_cast<size_t>(w * h)) return 0;
    // The generator converts with its own 480 x 264 grid (20 x 11 tiles of 24).
    const float halfX = 480.0f / 2.0f, halfY = 264.0f / 2.0f;
    std::vector<char> seen(grid.size(), 0);
    int added = 0;
    for (int start = 0; start < w * h; start++) {
        if (grid[static_cast<size_t>(start)] != kHole || seen[static_cast<size_t>(start)]) continue;
        unity::Collider2D c;
        c.path = path + "/Collider_" + std::to_string(added);
        c.tag = "Hole";
        c.layer = kSunkenObstacle;
        c.position = roomPosition; // the colliders' objects sit at the room's origin
        c.hasWorldBox = true;
        c.worldMin = {1e30f, 1e30f};
        c.worldMax = {-1e30f, -1e30f};
        std::vector<int> stack{start};
        seen[static_cast<size_t>(start)] = 1;
        while (!stack.empty()) {
            int t = stack.back();
            stack.pop_back();
            int i = t % w, j = t / w;
            float x0 = static_cast<float>(i) * kTile - halfX, x1 = x0 + kTile;
            float y0 = static_cast<float>(j) * kTile - halfY, y1 = y0 + kTile;
            if (i == 0) x0 -= kBorder;
            if (i == w - 1) x1 += kBorder;
            if (j == 0) y0 -= kBorder;
            if (j == h - 1) y1 += kBorder;
            unity::Vector2 mn{roomPosition.x + x0, roomPosition.y + y0}, mx{roomPosition.x + x1, roomPosition.y + y1};
            c.parts.emplace_back(mn, mx);
            c.worldMin = {std::min(c.worldMin.x, mn.x), std::min(c.worldMin.y, mn.y)};
            c.worldMax = {std::max(c.worldMax.x, mx.x), std::max(c.worldMax.y, mx.y)};
            const int next[4][2] = {{i - 1, j}, {i + 1, j}, {i, j - 1}, {i, j + 1}};
            for (const auto& n : next) {
                if (n[0] < 0 || n[0] >= w || n[1] < 0 || n[1] >= h) continue;
                int k = n[0] + n[1] * w;
                if (grid[static_cast<size_t>(k)] != kHole || seen[static_cast<size_t>(k)]) continue;
                seen[static_cast<size_t>(k)] = 1;
                stack.push_back(k);
            }
        }
        physics.Add(c);
        added++;
    }
    return added;
}

} // namespace game
