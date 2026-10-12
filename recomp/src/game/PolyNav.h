#pragma once
// PolyNav (the asset the enemies walk with), translated literally: PolyNav2D.cs (the map: a
// master polygon less the inflated obstacle polygons, its nodes and their line-of-sight
// links, FindPath), AStar.cs, Heap.cs and PolyNavObstacle.points. The agent that follows the
// paths is game/PolyNavAgent.h; a room's map is built by game/RoomPolyNav.h.
//
// FindPath's A* is a coroutine: it runs to the end when the request starts (the game's
// "more than 30 ms: yield" never happens on its maps; not modelled) and then yields one
// frame before the callback. Here a request that starts in frame N is delivered by
// ResumeCoroutines() in frame N+1. Per frame, in the game's order:
//     nav.BeginFrame();              // the frame starts
//     ... Update()s (agents' owners call SetDestination) ...
//     nav.ResumeCoroutines();        // coroutines waiting on `yield return null`
//     ... LateUpdate()s: nav.LateUpdate() (regenerates a flagged map), agents' LateUpdate ...
// A FindPath whose ends see each other answers at once, inside the call.

#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "unity/Vector2.h"

namespace game {

// Transform.TransformPoint for 2D objects: the point through each transform from the
// object's own up to its root, each level as Unity computes it (checked bit for bit against
// the game on every obstacle of the recorded rooms, the turned and scaled doors included):
// the point rotated by the local rotation (a rotation about z: quaternion (0, 0, qz, qw), with
// Quaternion * Vector3's formula, in float), then fma(rotated, local scale, local position)
// per axis, one rounding (as a fused multiply-add: scaling first, or rounding the product,
// is off by an ulp on LevelDoorLeft's 1.0000005 scale).
struct NavTransformLevel {
    unity::Vector2 position;
    float qz = 0.0f, qw = 1.0f;
    unity::Vector2 scale{1.0f, 1.0f};
    // A local transform with its stored quaternion.
    static NavTransformLevel FromQuaternion(float x, float y, float sx, float sy, float qz, float qw);
    // ... with only the rotation in degrees (2 atan2(z, w), as tools/extract/prefabs.py writes
    // it): the quaternion back from it; quarter turns get the exact values the prefabs store
    // (0.70710683 = 0x3F3504F4 for sqrt(1/2), 0 and 1), others the nearest float.
    static NavTransformLevel From(float x, float y, float sx, float sy, float degrees);
};

struct NavTransform {
    std::vector<NavTransformLevel> levels; // the object's own first, then its parents' up to the root
    unity::Vector2 TransformPoint(unity::Vector2 p) const;
    static NavTransform Translation(unity::Vector2 position);
};

// A PolyNavObstacle: its `points` (local to its object) and what CreatePolyMap reads.
struct PolyNavObstacle {
    enum class ShapeType { Polygon, Box };
    std::string path; // its object (for reports)
    bool invertPolygon = false;
    ShapeType shapeType = ShapeType::Polygon;
    float extraOffset = 0.0f;
    std::vector<unity::Vector2> points; // PolyNavObstacle.points (null in C#: empty)
    NavTransform transform;

    // PolyNavObstacle.points for a BoxCollider2D: its corners from the top left, clockwise.
    static std::vector<unity::Vector2> BoxPoints(unity::Vector2 offset, unity::Vector2 size);
    // ... for a PolygonCollider2D: `points` (its first path), reversed with invertPolygon.
    static std::vector<unity::Vector2> PolygonPoints(const std::vector<unity::Vector2>& path0, bool invertPolygon);
};

class PolyNav2D {
public:
    struct Polygon {
        std::vector<unity::Vector2> points;
    };
    struct PolyMap {
        std::vector<Polygon> masterPolygons, obstaclePolygons, allPolygons;
        PolyMap() = default;
        PolyMap(std::vector<Polygon> masterPolys, std::vector<Polygon> obstaclePolys);
    };
    struct PathNode {
        unity::Vector2 pos;
        std::vector<int> links;
        float gCost = 1.0f;
        float hCost = 0.0f;
        PathNode* parent = nullptr;
        int heapIndex = 0;
        explicit PathNode(unity::Vector2 p) : pos(p) {}
        float fCost() const { return gCost + hCost; }
        int CompareTo(const PathNode& other) const;
    };
    // The callback's argument: the path, or nullptr where C# passes null (no path).
    using PathCallback = std::function<void(const std::vector<unity::Vector2>* path)>;

    bool generateOnUpdate = true;
    float inflateRadius = 0.1f;
    bool regenerateFlag = false;
    PolyMap map;

    // The master collider: a BoxCollider2D on the PolyNav2D's object (at `position`).
    unity::Vector2 position;
    unity::Vector2 boxSize{1.0f, 1.0f}, boxOffset;

    // PolyNav2D.current (agents look their map up here).
    static PolyNav2D* current;

    // The obstacles are the caller's; they must outlive their place in the list.
    std::vector<const PolyNavObstacle*> navObstacles;
    void AddObstacle(const PolyNavObstacle* navObstacle);
    void RemoveObstacle(const PolyNavObstacle* navObstacle);

    void LateUpdate();
    void GenerateMap(bool generateMaster);
    // CreateNodes + LinkNodes on `map` as it is (for a map set from outside).
    void GenerateNodes();

    void FindPath(unity::Vector2 start, unity::Vector2 end, PathCallback callback);
    void BeginFrame() { frame_++; }
    void ResumeCoroutines();

    bool CheckLOS(unity::Vector2 posA, unity::Vector2 posB) const;
    bool PointIsValid(unity::Vector2 point) const;
    unity::Vector2 GetCloserEdgePoint(unity::Vector2 point) const;

    static std::vector<unity::Vector2> InflatePolygon(const std::vector<unity::Vector2>& points, float dist);
    static bool PointIsConcave(const std::vector<unity::Vector2>& points, int point);
    static bool SegmentsCross(unity::Vector2 a, unity::Vector2 b, unity::Vector2 c, unity::Vector2 d);
    static bool PointInsidePolygon(const std::vector<unity::Vector2>& polyPoints, unity::Vector2 point);

    int nodesCount() const { return static_cast<int>(nodes_.size()); }
    bool pendingRequest() const { return isProcessingPath_; }
    const std::vector<std::shared_ptr<PathNode>>& Nodes() const { return nodes_; }
    // True once a request's A* threw (a stale heap index past the heap: the game's coroutine
    // dies there and the PolyNav2D never takes another request).
    bool Stuck() const { return stuck_; }

private:
    struct PathRequest {
        unity::Vector2 start, end;
        PathCallback callback;
    };

    void TryNextFindPath();
    void RequestDone();
    void CreatePolyMap(bool generateMaster);
    void CreateNodes();
    void LinkNodes(std::vector<std::shared_ptr<PathNode>>& nodeList);
    void LinkStart(PathNode& start, std::vector<std::shared_ptr<PathNode>>& toNodes);
    void LinkEnd(PathNode& end, std::vector<std::shared_ptr<PathNode>>& toNodes);
    int IndexOf(const PathNode* node) const;
    // AStar.CalculatePath up to its yield: false if it threw.
    bool CalculatePath(bool& success);

    std::vector<std::shared_ptr<PathNode>> nodes_;
    std::queue<PathRequest> pathRequests_;
    PathRequest currentRequest_;
    bool isProcessingPath_ = false;
    std::shared_ptr<PathNode> startNode_, endNode_;
    // The running request's result (RetracePath, or none), its callback due after frame `requestFrame_`.
    bool resultReady_ = false;
    bool resultFound_ = false;
    std::vector<unity::Vector2> resultPath_;
    long long frame_ = 0, requestFrame_ = 0;
    bool stuck_ = false;
};

} // namespace game
