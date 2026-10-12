#include "game/RoomPolyNav.h"

#include <functional>

#include "game/Constants.h"
#include "game/HoleOutlines.h"
#include "game/SceneTransform.h"

namespace game {

using unity::Vector2;

namespace {

const char kHoles[] = "DungeonLevelRoomHoles";

bool StartsWith(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

// The local transforms from row `i` up to the room (then the room itself).
NavTransform Chain(const std::vector<SceneRow>& rows, const SceneWorld& scene, int i, Vector2 roomPosition)
{
    NavTransform t;
    for (int k = i; k >= 0; k = scene.parent[static_cast<size_t>(k)]) {
        const SceneRow& r = rows[static_cast<size_t>(k)];
        t.levels.push_back(NavTransformLevel::From(r.x, r.y, r.sx, r.sy, r.rotation));
    }
    NavTransformLevel room;
    room.position = roomPosition;
    t.levels.push_back(room);
    return t;
}

} // namespace

std::vector<PolyNavObstacle> RoomNavObstacles(const std::vector<SceneRow>& rows, const std::vector<int>& obstaclesGrid, int gridSlicesX,
                                              int gridSlicesY, Vector2 roomPosition, float tileSize)
{
    const SceneWorld scene = ResolveScene(rows, roomPosition);
    // The hierarchy: children in row order, walked depth first (the order SetActive enables
    // a hierarchy's behaviours in).
    std::vector<std::vector<int>> children(rows.size());
    std::vector<int> roots;
    for (size_t i = 0; i < rows.size(); i++) {
        int p = scene.parent[i];
        (p >= 0 ? children[static_cast<size_t>(p)] : roots).push_back(static_cast<int>(i));
    }
    bool rowsHaveHoles = false;
    for (const SceneRow& r : rows)
        if (StartsWith(r.path, std::string(kHoles) + "/")) rowsHaveHoles = true;

    std::vector<PolyNavObstacle> out;
    std::function<void(int)> visit = [&](int i) {
        const SceneRow& r = rows[static_cast<size_t>(i)];
        if (scene.active[static_cast<size_t>(i)]) {
            for (const Behaviour& b : r.behaviours) {
                if (b.cls != "PolyNavObstacle" || b.Field("m_Enabled", 1.0) == 0.0) continue;
                PolyNavObstacle o;
                o.path = r.path;
                o.invertPolygon = b.Field("invertPolygon") != 0.0;
                o.shapeType = b.Field("shapeType") != 0.0 ? PolyNavObstacle::ShapeType::Box : PolyNavObstacle::ShapeType::Polygon;
                o.extraOffset = static_cast<float>(b.Field("extraOffset"));
                if (!r.colliders.empty()) { // GetComponent<Collider2D>(): the first
                    const ColliderDef& c = r.colliders[0];
                    if (c.type == "BoxCollider2D") {
                        o.points = PolyNavObstacle::BoxPoints({c.ox, c.oy}, {c.sx, c.sy});
                    } else if (c.type == "PolygonCollider2D" && !c.paths.empty()) {
                        std::vector<Vector2> path0;
                        for (const auto& p : c.paths[0]) path0.push_back({p.first, p.second});
                        o.points = PolyNavObstacle::PolygonPoints(path0, o.invertPolygon);
                    }
                }
                o.transform = Chain(rows, scene, i, roomPosition);
                out.push_back(std::move(o));
                break; // one PolyNavObstacle per object
            }
            if (r.path == kHoles && !rowsHaveHoles && static_cast<int>(obstaclesGrid.size()) >= gridSlicesX * gridSlicesY) {
                // GenerateHolesPrefab: a PolyNavObstacle (invertPolygon) on each collider's object.
                for (const GridCollider& c : GenerateCollidersFromGrid(obstaclesGrid, gridSlicesX, gridSlicesY, {2}, tileSize)) {
                    PolyNavObstacle o;
                    o.path = std::string(kHoles) + "/" + c.name;
                    o.invertPolygon = true;
                    o.shapeType = PolyNavObstacle::ShapeType::Polygon;
                    o.points = PolyNavObstacle::PolygonPoints(c.paths[0], true);
                    o.transform = Chain(rows, scene, i, roomPosition);
                    o.transform.levels.insert(o.transform.levels.begin(), NavTransformLevel{}); // the collider's object, at 0
                    out.push_back(std::move(o));
                }
            }
        }
        for (int c : children[static_cast<size_t>(i)]) visit(c);
    };
    for (int r : roots) visit(r);
    return out;
}

std::unique_ptr<RoomPolyNav> BuildRoomPolyNav(const std::vector<SceneRow>& rows, const std::vector<int>& obstaclesGrid, int gridSlicesX,
                                              int gridSlicesY, Vector2 roomPosition, float tileSize, float inflateRadius)
{
    auto out = std::make_unique<RoomPolyNav>();
    out->obstacles = RoomNavObstacles(rows, obstaclesGrid, gridSlicesX, gridSlicesY, roomPosition, tileSize);
    PolyNav2D& nav = out->nav;
    nav.inflateRadius = inflateRadius;
    // The cameraObjective's world position.
    Vector2 center = roomPosition;
    for (const SceneRow& r : rows)
        if (r.path == "CameraObjective") center = {r.x + roomPosition.x, r.y + roomPosition.y};
    nav.position = center;
    nav.boxSize = {static_cast<float>(kRoomFloorWidth), static_cast<float>(kRoomFloorHeight)};
    nav.boxOffset = {0.0f, static_cast<float>(kRoomCenterYOffset)};
    for (const PolyNavObstacle& o : out->obstacles) nav.AddObstacle(&o);
    nav.GenerateMap(true);
    nav.regenerateFlag = false;
    return out;
}

std::unique_ptr<RoomPolyNav> BuildRoomPolyNav(const RoomFill& room, Vector2 roomPosition)
{
    return BuildRoomPolyNav(RoomSceneTree(room, roomPosition), room.obstaclesGrid, room.gridSlicesX, room.gridSlicesY, roomPosition,
                            Constants::GetFloat("tileSize"));
}

} // namespace game
