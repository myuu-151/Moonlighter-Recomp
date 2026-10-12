#include "game/PolyNav.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

#include "unity/Mathf.h"

namespace game {

using unity::Vector2;
using unity::mono::D; // (Mono's float arithmetic: unity/Mathf.h)
using unity::mono::F;
namespace Mathf = unity::Mathf;

PolyNav2D* PolyNav2D::current = nullptr;

// ---------------------------------------------------------------------------------------
// Transforms and obstacles

NavTransformLevel NavTransformLevel::FromQuaternion(float x, float y, float sx, float sy, float qz, float qw)
{
    NavTransformLevel l;
    l.position = {x, y};
    l.scale = {sx, sy};
    l.qz = qz;
    l.qw = qw;
    return l;
}

namespace {

// A quaternion component from its exact value: sqrt(1/2), 0 and 1 as the prefabs store them.
float StoredComponent(double v)
{
    const double half = std::sqrt(0.5);
    const float storedHalf = 0.707106829f; // 0x3F3504F4
    if (std::fabs(std::fabs(v) - half) < 1e-7) return v < 0 ? -storedHalf : storedHalf;
    if (std::fabs(v) < 1e-7) return 0.0f;
    if (std::fabs(std::fabs(v) - 1.0) < 1e-7) return v < 0 ? -1.0f : 1.0f;
    return static_cast<float>(v);
}

} // namespace

NavTransformLevel NavTransformLevel::From(float x, float y, float sx, float sy, float degrees)
{
    if (degrees == 0.0f) return FromQuaternion(x, y, sx, sy, 0.0f, 1.0f);
    const double half = static_cast<double>(degrees) * 3.14159265358979323846 / 360.0;
    return FromQuaternion(x, y, sx, sy, StoredComponent(std::sin(half)), StoredComponent(std::cos(half)));
}

Vector2 NavTransform::TransformPoint(Vector2 p) const
{
    for (const NavTransformLevel& l : levels) {
        float x = p.x, y = p.y, z = 0.0f;
        // Quaternion * Vector3 with the rotation (0, 0, qz, qw).
        const float qx = 0.0f, qy = 0.0f, qz = l.qz, qw = l.qw;
        float num = qx * 2.0f, num2 = qy * 2.0f, num3 = qz * 2.0f;
        float num4 = qx * num, num5 = qy * num2, num6 = qz * num3;
        float num7 = qx * num2, num8 = qx * num3, num9 = qy * num3;
        float num10 = qw * num, num11 = qw * num2, num12 = qw * num3;
        float rx = (1.0f - (num5 + num6)) * x + (num7 - num12) * y + (num8 + num11) * z;
        float ry = (num7 + num12) * x + (1.0f - (num4 + num6)) * y + (num9 - num10) * z;
        // Scale and translate, fused.
        p = {std::fma(rx, l.scale.x, l.position.x), std::fma(ry, l.scale.y, l.position.y)};
    }
    return p;
}

NavTransform NavTransform::Translation(Vector2 position)
{
    NavTransform t;
    NavTransformLevel l;
    l.position = position;
    t.levels.push_back(l);
    return t;
}

std::vector<Vector2> PolyNavObstacle::BoxPoints(Vector2 offset, Vector2 size)
{
    Vector2 vector = offset + unity::Div(Vector2{0.0f - size.x, size.y}, 2.0f);
    Vector2 vector2 = offset + unity::Div(Vector2{size.x, size.y}, 2.0f);
    Vector2 vector3 = offset + unity::Div(Vector2{size.x, 0.0f - size.y}, 2.0f);
    Vector2 vector4 = offset + unity::Div(Vector2{0.0f - size.x, 0.0f - size.y}, 2.0f);
    return {vector, vector2, vector3, vector4};
}

std::vector<Vector2> PolyNavObstacle::PolygonPoints(const std::vector<Vector2>& path0, bool invertPolygon)
{
    std::vector<Vector2> array = path0;
    if (invertPolygon) std::reverse(array.begin(), array.end());
    return array;
}

// ---------------------------------------------------------------------------------------
// PathNode, Heap

namespace {

// float.CompareTo.
int CompareFloat(float a, float b)
{
    if (a < b) return -1;
    if (a > b) return 1;
    if (a == b) return 0;
    if (std::isnan(a)) return std::isnan(b) ? 0 : -1;
    return 1;
}

using PathNode = PolyNav2D::PathNode;

// Heap<PathNode>. `fault` stands for the exceptions C# would throw (an index past the array).
class Heap {
public:
    explicit Heap(int maxHeapSize) : items_(static_cast<size_t>(maxHeapSize), nullptr) {}

    bool fault = false;
    int Count() const { return currentItemCount_; }

    void Add(PathNode* item)
    {
        if (currentItemCount_ >= static_cast<int>(items_.size())) {
            fault = true;
            return;
        }
        item->heapIndex = currentItemCount_;
        items_[static_cast<size_t>(currentItemCount_)] = item;
        SortUp(item);
        currentItemCount_++;
    }

    PathNode* RemoveFirst()
    {
        PathNode* result = items_[0];
        currentItemCount_--;
        items_[0] = items_[static_cast<size_t>(currentItemCount_)];
        items_[0]->heapIndex = 0;
        SortDown(items_[0]);
        return result;
    }

    bool Contains(PathNode* item)
    {
        if (item->heapIndex < 0 || item->heapIndex >= static_cast<int>(items_.size())) {
            fault = true;
            return false;
        }
        return items_[static_cast<size_t>(item->heapIndex)] == item;
    }

    void UpdateItem(PathNode* item) { SortUp(item); }

private:
    void SortDown(PathNode* item)
    {
        while (true) {
            int num = item->heapIndex * 2 + 1;
            int num2 = item->heapIndex * 2 + 2;
            int num3 = 0;
            if (num >= currentItemCount_) break;
            num3 = num;
            if (num2 < currentItemCount_) {
                if (items_[static_cast<size_t>(num)]->CompareTo(*items_[static_cast<size_t>(num2)]) < 0) num3 = num2;
            }
            PathNode* other2 = items_[static_cast<size_t>(num3)];
            if (item->CompareTo(*other2) < 0) {
                Swap(item, other2);
                continue;
            }
            break;
        }
    }

    void SortUp(PathNode* item)
    {
        int num = (item->heapIndex - 1) / 2;
        while (true) {
            PathNode* val = items_[static_cast<size_t>(num)];
            if (item->CompareTo(*val) > 0) {
                Swap(item, val);
                num = (item->heapIndex - 1) / 2;
                continue;
            }
            break;
        }
    }

    void Swap(PathNode* itemA, PathNode* itemB)
    {
        items_[static_cast<size_t>(itemA->heapIndex)] = itemB;
        items_[static_cast<size_t>(itemB->heapIndex)] = itemA;
        int heapIndex = itemA->heapIndex;
        int heapIndex2 = itemB->heapIndex;
        itemA->heapIndex = heapIndex2;
        itemB->heapIndex = heapIndex;
    }

    std::vector<PathNode*> items_;
    int currentItemCount_ = 0;
};

float GetDistance(const PathNode* a, const PathNode* b) { return unity::Magnitude(a->pos - b->pos); }

} // namespace

int PolyNav2D::PathNode::CompareTo(const PathNode& other) const
{
    int num = CompareFloat(fCost(), other.fCost());
    if (num == 0) num = CompareFloat(hCost, other.hCost);
    return -num;
}

PolyNav2D::PolyMap::PolyMap(std::vector<Polygon> masterPolys, std::vector<Polygon> obstaclePolys)
    : masterPolygons(std::move(masterPolys)), obstaclePolygons(std::move(obstaclePolys))
{
    allPolygons = masterPolygons;
    allPolygons.insert(allPolygons.end(), obstaclePolygons.begin(), obstaclePolygons.end());
}

// ---------------------------------------------------------------------------------------
// PolyNav2D

void PolyNav2D::AddObstacle(const PolyNavObstacle* navObstacle)
{
    if (std::find(navObstacles.begin(), navObstacles.end(), navObstacle) == navObstacles.end()) {
        navObstacles.push_back(navObstacle);
        regenerateFlag = true;
    }
}

void PolyNav2D::RemoveObstacle(const PolyNavObstacle* navObstacle)
{
    auto it = std::find(navObstacles.begin(), navObstacles.end(), navObstacle);
    if (it != navObstacles.end()) {
        navObstacles.erase(it);
        regenerateFlag = true;
    }
}

void PolyNav2D::LateUpdate()
{
    if (generateOnUpdate && regenerateFlag) {
        regenerateFlag = false;
        GenerateMap(false);
    }
}

void PolyNav2D::FindPath(Vector2 start, Vector2 end, PathCallback callback)
{
    if (CheckLOS(start, end)) {
        std::vector<Vector2> path{start, end};
        callback(&path);
    } else {
        pathRequests_.push(PathRequest{start, end, std::move(callback)});
        TryNextFindPath();
    }
}

int PolyNav2D::IndexOf(const PathNode* node) const
{
    for (size_t i = 0; i < nodes_.size(); i++)
        if (nodes_[i].get() == node) return static_cast<int>(i);
    return -1;
}

void PolyNav2D::TryNextFindPath()
{
    if (!isProcessingPath_ && !pathRequests_.empty()) {
        isProcessingPath_ = true;
        currentRequest_ = std::move(pathRequests_.front());
        pathRequests_.pop();
        if (!PointIsValid(currentRequest_.start)) currentRequest_.start = GetCloserEdgePoint(currentRequest_.start);
        startNode_ = std::make_shared<PathNode>(currentRequest_.start);
        endNode_ = std::make_shared<PathNode>(currentRequest_.end);
        nodes_.push_back(startNode_);
        LinkStart(*startNode_, nodes_);
        nodes_.push_back(endNode_);
        LinkEnd(*endNode_, nodes_);
        // StartCoroutine(AStar.CalculatePath(...)): runs to its `yield return null` now.
        bool success = false;
        if (!CalculatePath(success)) {
            stuck_ = true; // the coroutine threw: no callback, isProcessingPath stays set
            resultReady_ = false;
            return;
        }
        resultFound_ = success;
        resultPath_.clear();
        if (success) {
            // RetracePath (after the yield in C#; nothing between changes the parents).
            for (PathNode* n = endNode_.get(); n != startNode_.get(); n = n->parent) resultPath_.push_back(n->pos);
            resultPath_.push_back(startNode_->pos);
            std::reverse(resultPath_.begin(), resultPath_.end());
        }
        resultReady_ = true;
        requestFrame_ = frame_;
    }
}

bool PolyNav2D::CalculatePath(bool& success)
{
    std::vector<std::shared_ptr<PathNode>>& allNodes = nodes_;
    PathNode* startNode = startNode_.get();
    PathNode* endNode = endNode_.get();
    Heap openList(static_cast<int>(allNodes.size()));
    std::unordered_set<PathNode*> closedList;
    success = false;
    openList.Add(startNode);
    if (openList.fault) return false;
    while (openList.Count() > 0) {
        PathNode* pathNode = openList.RemoveFirst();
        closedList.insert(pathNode);
        if (pathNode == endNode) {
            success = true;
            break;
        }
        const std::vector<int>& links = pathNode->links;
        for (size_t i = 0; i < links.size(); i++) {
            if (links[i] < 0 || links[i] >= static_cast<int>(allNodes.size())) return false;
            PathNode* pathNode2 = allNodes[static_cast<size_t>(links[i])].get();
            if (closedList.count(pathNode2)) continue;
            float num = pathNode->gCost + GetDistance(pathNode, pathNode2);
            bool open = num < pathNode2->gCost;
            if (!open) {
                open = !openList.Contains(pathNode2);
                if (openList.fault) return false;
            }
            if (open) {
                pathNode2->gCost = num;
                pathNode2->hCost = GetDistance(pathNode2, endNode);
                pathNode2->parent = pathNode;
                bool contains = openList.Contains(pathNode2);
                if (openList.fault) return false;
                if (!contains) {
                    openList.Add(pathNode2);
                    if (openList.fault) return false;
                    openList.UpdateItem(pathNode2);
                }
            }
        }
    }
    return true;
}

void PolyNav2D::ResumeCoroutines()
{
    if (!isProcessingPath_ || !resultReady_ || requestFrame_ >= frame_) return;
    resultReady_ = false;
    RequestDone();
}

void PolyNav2D::RequestDone()
{
    // The path the coroutine hands over (a copy: a callback may start the next request).
    const bool found = resultFound_;
    const std::vector<Vector2> path = resultPath_;
    // try { ... } catch { }: an index past the list ends the block there.
    bool ok = true;
    for (size_t i = 0; i < endNode_->links.size(); i++) {
        int link = endNode_->links[i];
        if (link < 0 || link >= static_cast<int>(nodes_.size())) {
            ok = false;
            break;
        }
        std::vector<int>& l = nodes_[static_cast<size_t>(link)]->links;
        auto it = std::find(l.begin(), l.end(), IndexOf(endNode_.get()));
        if (it != l.end()) l.erase(it);
    }
    if (ok) {
        int e = IndexOf(endNode_.get());
        if (e >= 0) nodes_.erase(nodes_.begin() + e);
        int s = IndexOf(startNode_.get());
        if (s >= 0) nodes_.erase(nodes_.begin() + s);
    }
    isProcessingPath_ = false;
    PathCallback callback = currentRequest_.callback;
    callback(found ? &path : nullptr);
    TryNextFindPath();
}

void PolyNav2D::GenerateMap(bool generateMaster)
{
    CreatePolyMap(generateMaster);
    CreateNodes();
    LinkNodes(nodes_);
}

void PolyNav2D::GenerateNodes()
{
    CreateNodes();
    LinkNodes(nodes_);
}

void PolyNav2D::CreatePolyMap(bool generateMaster)
{
    std::vector<Polygon> list;
    std::vector<Polygon> list2;
    navObstacles.erase(std::remove(navObstacles.begin(), navObstacles.end(), nullptr), navObstacles.end());
    for (const PolyNavObstacle* polyNavObstacle : navObstacles) {
        std::vector<Vector2> points = InflatePolygon(polyNavObstacle->points, Mathf::Max(0.01f, inflateRadius + polyNavObstacle->extraOffset));
        for (Vector2& p : points) p = polyNavObstacle->transform.TransformPoint(p);
        list2.push_back(Polygon{points});
    }
    if (generateMaster) {
        // The master collider is a BoxCollider2D.
        Vector2 vector = boxOffset + unity::Div(Vector2{0.0f - boxSize.x, boxSize.y}, 2.0f);
        Vector2 vector2 = boxOffset + unity::Div(Vector2{boxSize.x, boxSize.y}, 2.0f);
        Vector2 vector3 = boxOffset + unity::Div(Vector2{boxSize.x, 0.0f - boxSize.y}, 2.0f);
        Vector2 vector4 = boxOffset + unity::Div(Vector2{0.0f - boxSize.x, 0.0f - boxSize.y}, 2.0f);
        std::vector<Vector2> corners{vector, vector4, vector3, vector2};
        const NavTransform t = NavTransform::Translation(position);
        for (Vector2& p : corners) p = t.TransformPoint(p);
        list.push_back(Polygon{InflatePolygon(corners, Mathf::Max(0.01f, inflateRadius))});
    } else {
        list = map.masterPolygons;
    }
    map = PolyMap(std::move(list), std::move(list2));
}

void PolyNav2D::CreateNodes()
{
    nodes_.clear();
    for (const Polygon& polygon : map.allPolygons) {
        std::vector<Vector2> array = InflatePolygon(polygon.points, 0.05f);
        for (size_t j = 0; j < array.size(); j++)
            if (!PointIsConcave(array, static_cast<int>(j)) && PointIsValid(array[j])) nodes_.push_back(std::make_shared<PathNode>(array[j]));
    }
}

void PolyNav2D::LinkNodes(std::vector<std::shared_ptr<PathNode>>& nodeList)
{
    for (size_t i = 0; i < nodeList.size(); i++) {
        nodeList[i]->links.clear();
        for (size_t j = 0; j < nodeList.size(); j++) {
            if (nodeList[i] != nodeList[j] && j <= i && CheckLOS(nodeList[i]->pos, nodeList[j]->pos)) {
                nodeList[i]->links.push_back(static_cast<int>(j));
                nodeList[j]->links.push_back(static_cast<int>(i));
            }
        }
    }
}

void PolyNav2D::LinkStart(PathNode& start, std::vector<std::shared_ptr<PathNode>>& toNodes)
{
    for (size_t i = 0; i < toNodes.size(); i++)
        if (CheckLOS(start.pos, toNodes[i]->pos)) start.links.push_back(static_cast<int>(i));
}

void PolyNav2D::LinkEnd(PathNode& end, std::vector<std::shared_ptr<PathNode>>& toNodes)
{
    for (size_t i = 0; i < toNodes.size(); i++) {
        if (CheckLOS(end.pos, toNodes[i]->pos)) {
            end.links.push_back(static_cast<int>(i));
            toNodes[i]->links.push_back(IndexOf(&end));
        }
    }
}

bool PolyNav2D::CheckLOS(Vector2 posA, Vector2 posB) const
{
    if (unity::SqrMagnitude(posA - posB) < Mathf::Epsilon) return true;
    for (const Polygon& polygon : map.allPolygons) {
        const size_t n = polygon.points.size();
        for (size_t j = 0; j < n; j++)
            if (SegmentsCross(posA, posB, polygon.points[j], polygon.points[(j + 1) % n])) return false;
    }
    return true;
}

bool PolyNav2D::PointIsValid(Vector2 point) const
{
    for (size_t i = 0; i < map.allPolygons.size(); i++) {
        bool inside = PointInsidePolygon(map.allPolygons[i].points, point);
        if (i == 0 ? !inside : inside) return false;
    }
    return true;
}

std::vector<Vector2> PolyNav2D::InflatePolygon(const std::vector<Vector2>& points, float dist)
{
    const int n = static_cast<int>(points.size());
    std::vector<Vector2> array(points.size());
    for (int i = 0; i < n; i++) {
        Vector2 vector = points[static_cast<size_t>(i == 0 ? n - 1 : i - 1)];
        Vector2 vector2 = points[static_cast<size_t>(i)];
        Vector2 vector3 = points[static_cast<size_t>((i + 1) % n)];
        Vector2 normalized = unity::Normalized(vector - vector2);
        Vector2 normalized2 = unity::Normalized(vector3 - vector2);
        Vector2 vector4 = normalized + normalized2;
        vector4 = vector4 * (!PointIsConcave(points, i) ? (0.0f - dist) : dist);
        array[static_cast<size_t>(i)] = points[static_cast<size_t>(i)] + vector4;
    }
    return array;
}

bool PolyNav2D::PointIsConcave(const std::vector<Vector2>& points, int point)
{
    const int n = static_cast<int>(points.size());
    Vector2 vector = points[static_cast<size_t>(point)];
    Vector2 vector2 = points[static_cast<size_t>((point + 1) % n)];
    Vector2 vector3 = points[static_cast<size_t>(point == 0 ? n - 1 : point - 1)];
    Vector2 vector4{vector.x - vector3.x, vector.y - vector3.y};
    Vector2 vector5{vector2.x - vector.x, vector2.y - vector.y};
    return D(vector4.x) * D(vector5.y) - D(vector4.y) * D(vector5.x) > 0.0;
}

bool PolyNav2D::SegmentsCross(Vector2 a, Vector2 b, Vector2 c, Vector2 d)
{
    float num = F((D(b.x) - D(a.x)) * (D(d.y) - D(c.y)) - (D(b.y) - D(a.y)) * (D(d.x) - D(c.x)));
    if (num == 0.0f) return false;
    float num2 = F((D(a.y) - D(c.y)) * (D(d.x) - D(c.x)) - (D(a.x) - D(c.x)) * (D(d.y) - D(c.y)));
    float num3 = F((D(a.y) - D(c.y)) * (D(b.x) - D(a.x)) - (D(a.x) - D(c.x)) * (D(b.y) - D(a.y)));
    if (num2 == 0.0f || num3 == 0.0f) return false;
    float num4 = num2 / num;
    float num5 = num3 / num;
    if (num4 > 0.0f && num4 < 1.0f) {
        if (num5 > 0.0f) return num5 < 1.0f;
        return false;
    }
    return false;
}

bool PolyNav2D::PointInsidePolygon(const std::vector<Vector2>& polyPoints, Vector2 point)
{
    float num = 0.0f;
    for (const Vector2& p : polyPoints) num = Mathf::Min(num, p.x);
    Vector2 a{num - 0.1f, point.y};
    int num2 = 0;
    const size_t n = polyPoints.size();
    for (size_t j = 0; j < n; j++)
        if (SegmentsCross(a, point, polyPoints[j], polyPoints[(j + 1) % n])) num2++;
    return (num2 & 1) == 1;
}

Vector2 PolyNav2D::GetCloserEdgePoint(Vector2 point) const
{
    std::vector<Vector2> list;
    Vector2 item = Vector2::zero();
    float num = std::numeric_limits<float>::infinity();
    for (const Polygon& polygon : map.allPolygons) {
        std::vector<Vector2> array = InflatePolygon(polygon.points, 0.01f);
        const size_t n = array.size();
        for (size_t j = 0; j < n; j++) {
            Vector2 vector = array[j];
            Vector2 vector2 = array[(j + 1) % n];
            Vector2 c = polygon.points[j];
            Vector2 d = polygon.points[(j + 1) % polygon.points.size()];
            Vector2 vector3 = unity::Project3(point - vector, vector2 - vector) + vector;
            if (SegmentsCross(point, vector3, c, d) && PointIsValid(vector3)) list.push_back(vector3);
            float sqrMagnitude = unity::SqrMagnitude(point - array[j]);
            if (sqrMagnitude < num && PointIsValid(array[j])) {
                num = sqrMagnitude;
                item = array[j];
            }
        }
    }
    list.push_back(item);
    float num2 = std::numeric_limits<float>::infinity();
    size_t index = 0;
    for (size_t k = 0; k < list.size(); k++) {
        float sqrMagnitude2 = unity::SqrMagnitude(point - list[k]);
        if (sqrMagnitude2 < num2) {
            num2 = sqrMagnitude2;
            index = k;
        }
    }
    return list[index];
}

} // namespace game
