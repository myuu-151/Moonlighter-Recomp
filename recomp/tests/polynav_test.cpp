// PolyNav (game/PolyNav, game/PolyNavAgent, game/HoleOutlines, game/RoomPolyNav):
//  1. the geometry and A* on hand-made maps, against values worked out from PolyNav2D.cs,
//     AStar.cs and Heap.cs by hand (node order, links, the heap's tie-breaking, the frame the
//     path arrives on);
//  2. GenerateCollidersFromGrid's outlines on small grids;
//  3. an agent walking around an obstacle (arrives, never inside it), Stop, an invalid goal;
//  4. with the game's data (ML_DATA, default data/extracted): the DungeonRoom prefab with a
//     wall of holes, built into a map, a path through the gap;
//  5. with the oracle's recording (data/oracle/polynav.jsonl, `-oracle-polynav`; skipped if
//     there's none), every recorded room, exactly:
//     - inflate: PolyNav2D.InflatePolygon of each obstacle's `points`;
//     - transform: the inflated points through the obstacle's recorded transform chain, against
//       the map's obstacle polygons;
//     - graph: nodes and links made from the recorded polygons;
//     - probes: PointIsValid, GetCloserEdgePoint, CheckLOS over the room;
//     - paths: FindPath between fixed points (and whether it answered at once);
//     - holes: GenerateCollidersFromGrid from the room's obstaclesGrid against its colliders;
//     - builder: the map built from the room's hierarchy (its hole colliders left out, traced
//       from the grid instead) against the game's: obstacles in order, master, polygons, nodes.
//
// Usage: polynav_test [polynav.jsonl]   (run from the repository's root)

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "cs/Object.h"
#include "game/Constants.h"
#include "game/HoleOutlines.h"
#include "game/PolyNav.h"
#include "game/PolyNavAgent.h"
#include "game/Prefabs.h"
#include "game/RoomPolyNav.h"
#include "unity/Mathf.h"
#include "util/Json.h"

using game::PolyNav2D;
using game::PolyNavAgent;
using game::PolyNavObstacle;
using json::Value;
using unity::Vector2;
using Poly = std::vector<Vector2>;

namespace {

int checks = 0, failures = 0;

void Check(bool ok, const std::string& what)
{
    checks++;
    if (!ok) {
        failures++;
        std::printf("FAIL %s\n", what.c_str());
    }
}

std::string ReadFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool Same(Vector2 a, Vector2 b) { return a.x == b.x && a.y == b.y; }

bool SamePoly(const Poly& a, const Poly& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (!Same(a[i], b[i])) return false;
    return true;
}

std::string Str(Vector2 v)
{
    char buf[96];
    std::snprintf(buf, sizeof buf, "(%.9g, %.9g)", v.x, v.y);
    return buf;
}

std::string Str(const Poly& p)
{
    std::string s = "[";
    for (size_t i = 0; i < p.size(); i++) s += (i ? " " : "") + Str(p[i]);
    return s + "]";
}

// ---------------------------------------------------------------------------------------
// 1. Geometry and A*

void GeometryTests()
{
    // SegmentsCross: a proper crossing; touching at an end (num2 or num3 zero); parallel.
    Check(PolyNav2D::SegmentsCross({0, 0}, {2, 2}, {0, 2}, {2, 0}), "SegmentsCross: an X");
    Check(!PolyNav2D::SegmentsCross({0, 0}, {2, 2}, {2, 2}, {3, 0}), "SegmentsCross: meeting at an end");
    Check(!PolyNav2D::SegmentsCross({0, 0}, {2, 0}, {0, 1}, {2, 1}), "SegmentsCross: parallel");
    Check(!PolyNav2D::SegmentsCross({0, 0}, {1, 1}, {0, 2}, {2, 0}), "SegmentsCross: stopping short of the other");

    // A box's points go clockwise from the top left: convex; the master's go the other way.
    Poly box = PolyNavObstacle::BoxPoints({0, 0}, {24, 24});
    Check(SamePoly(box, {{-12, 12}, {12, 12}, {12, -12}, {-12, -12}}), "BoxPoints " + Str(box));
    for (int i = 0; i < 4; i++) Check(!PolyNav2D::PointIsConcave(box, i), "a box corner is convex");
    Poly ccw(box.rbegin(), box.rend());
    for (int i = 0; i < 4; i++) Check(PolyNav2D::PointIsConcave(ccw, i), "a counter-clockwise corner is 'concave'");

    // Inflating: convex corners outwards by dist along both edges, concave ones inwards.
    Poly in = PolyNav2D::InflatePolygon(box, 8.0f);
    Check(SamePoly(in, {{-20, 20}, {20, 20}, {20, -20}, {-20, -20}}), "InflatePolygon of a box " + Str(in));
    Poly shrunk = PolyNav2D::InflatePolygon(ccw, 8.0f);
    Check(SamePoly(shrunk, {{-4, -4}, {4, -4}, {4, 4}, {-4, 4}}), "InflatePolygon of a counter-clockwise box " + Str(shrunk));

    // PointInsidePolygon: a ray from left of min(0, every x).
    Check(PolyNav2D::PointInsidePolygon(box, {0, 0}), "inside the box");
    Check(!PolyNav2D::PointInsidePolygon(box, {13, 0}), "right of the box");
    Check(!PolyNav2D::PointInsidePolygon(box, {-13, 5}), "left of the box");
    Poly far = PolyNavObstacle::BoxPoints({100, 0}, {10, 10});
    Check(PolyNav2D::PointInsidePolygon(far, {100, 1}), "inside a box right of x = 0");
}

// A room-sized map: master 484 x 268 at (640, 360), inflate 8, one 24 x 24 box in the middle.
struct TestMap {
    PolyNavObstacle box;
    PolyNav2D nav;
    TestMap()
    {
        box.path = "box";
        box.shapeType = PolyNavObstacle::ShapeType::Box;
        box.points = PolyNavObstacle::BoxPoints({0, 0}, {24, 24});
        box.transform = game::NavTransform::Translation({640, 360});
        nav.inflateRadius = 8.0f;
        nav.position = {640, 360};
        nav.boxSize = {484, 268};
        nav.boxOffset = {0, 0};
        nav.AddObstacle(&box);
        nav.GenerateMap(true);
        nav.regenerateFlag = false;
    }
};

void MapTests()
{
    TestMap m;
    PolyNav2D& nav = m.nav;
    Check(nav.map.masterPolygons.size() == 1 && SamePoly(nav.map.masterPolygons[0].points, {{406, 486}, {406, 234}, {874, 234}, {874, 486}}),
          "master: the box shrunk by inflateRadius, TL BL BR TR");
    Check(nav.map.obstaclePolygons.size() == 1 && SamePoly(nav.map.obstaclePolygons[0].points, {{620, 380}, {660, 380}, {660, 340}, {620, 340}}),
          "obstacle: inflated then moved");
    // Nodes: the obstacle's corners pushed out 0.05 more (the master's are all 'concave').
    const float o = 0.0f - 0.05f;
    const Poly nodes{{620.0f + 1.0f * o, 380.0f + -1.0f * o}, {660.0f + -1.0f * o, 380.0f + -1.0f * o}, {660.0f + -1.0f * o, 340.0f + 1.0f * o},
                     {620.0f + 1.0f * o, 340.0f + 1.0f * o}};
    Check(nav.nodesCount() == 4, "4 nodes");
    // Links: the sides, and the diagonals too: they pass through the polygon's corners, which
    // SegmentsCross doesn't count (a crossing must be strictly inside both segments).
    const std::vector<std::vector<int>> links{{1, 2, 3}, {0, 2, 3}, {0, 1, 3}, {0, 1, 2}};
    for (int i = 0; i < nav.nodesCount() && i < 4; i++) {
        Check(Same(nav.Nodes()[i]->pos, nodes[i]), "node " + std::to_string(i) + " " + Str(nav.Nodes()[i]->pos));
        std::string ls;
        for (int l : nav.Nodes()[i]->links) ls += " " + std::to_string(l);
        Check(nav.Nodes()[i]->links == links[i], "node " + std::to_string(i) + "'s links:" + ls);
    }

    Check(!nav.PointIsValid({640, 360}), "inside the obstacle: invalid");
    Check(nav.PointIsValid({500, 300}), "on the floor: valid");
    Check(!nav.PointIsValid({300, 300}), "outside the master: invalid");
    Vector2 closer = nav.GetCloserEdgePoint({640, 365});
    Check(nav.PointIsValid(closer) && std::fabs(closer.x - 640.0f) < 1e-3f && std::fabs(closer.y - 380.01f) < 1e-3f,
          "GetCloserEdgePoint: onto the nearest edge " + Str(closer));

    // In sight: answered inside the call.
    bool got = false;
    Poly path;
    nav.FindPath({540, 300}, {740, 300}, [&](const Poly* p) {
        got = p != nullptr;
        if (p) path = *p;
    });
    Check(got && SamePoly(path, {{540, 300}, {740, 300}}), "FindPath in sight: at once, [start, end]");

    // Behind the obstacle: A* now, the path next frame. Both ways round are as long; the heap
    // has TL first, BL next to it; TR and BR tie on f and h, TR is the root; the end ties BR
    // on f and wins on h: start, TL, TR, end.
    got = false;
    bool called = false;
    nav.BeginFrame();
    nav.FindPath({540, 360}, {740, 360}, [&](const Poly* p) {
        called = true;
        got = p != nullptr;
        if (p) path = *p;
    });
    Check(!called && nav.pendingRequest() && nav.nodesCount() == 6, "FindPath blocked: queued, start and end in the nodes");
    nav.ResumeCoroutines();
    Check(!called, "not in the frame it was asked in");
    nav.BeginFrame();
    nav.ResumeCoroutines();
    Check(called && got && SamePoly(path, {{540, 360}, nodes[0], nodes[1], {740, 360}}), "the next frame: start, TL, TR, end " + Str(path));
    Check(!nav.pendingRequest() && nav.nodesCount() == 4, "the start and end nodes gone again");
    for (int i = 0; i < nav.nodesCount() && i < 4; i++) Check(nav.Nodes()[i]->links == links[i], "node " + std::to_string(i) + "'s links back");

    // Two requests: the second waits for the first, starts when it's done, a frame later.
    int done1 = -1, done2 = -1;
    nav.FindPath({540, 360}, {740, 360}, [&](const Poly*) { done1 = 1; });
    nav.FindPath({740, 360}, {540, 360}, [&](const Poly* p) {
        done2 = 2;
        if (p) path = *p;
    });
    nav.BeginFrame();
    nav.ResumeCoroutines();
    Check(done1 == 1 && done2 == -1, "two requests: the first next frame");
    nav.BeginFrame();
    nav.ResumeCoroutines();
    Check(done2 == 2 && path.size() == 4 && Same(path[0], {740, 360}), "two requests: the second the frame after");

    // From inside the obstacle: the start moves to the closer edge point first.
    called = false;
    nav.FindPath({640, 365}, {740, 360}, [&](const Poly* p) {
        called = true;
        if (p) path = *p;
    });
    nav.BeginFrame();
    nav.ResumeCoroutines();
    Check(called && !path.empty() && Same(path[0], closer), "an invalid start: from GetCloserEdgePoint " + Str(path));

    // The map follows its obstacles (LateUpdate regenerates a flagged map, master kept).
    PolyNavObstacle second = m.box;
    second.transform = game::NavTransform::Translation({740, 400});
    nav.AddObstacle(&second);
    Check(nav.regenerateFlag, "AddObstacle flags the map");
    nav.LateUpdate();
    Check(!nav.regenerateFlag && nav.map.obstaclePolygons.size() == 2 && nav.nodesCount() == 8, "LateUpdate: two obstacles, eight nodes");
    nav.RemoveObstacle(&second);
    nav.LateUpdate();
    Check(nav.map.obstaclePolygons.size() == 1 && nav.nodesCount() == 4, "RemoveObstacle: back to one");
}

// ---------------------------------------------------------------------------------------
// 2. Hole outlines

std::vector<int> Grid(std::initializer_list<std::pair<int, int>> holes)
{
    std::vector<int> g(20 * 11, 0);
    for (auto [i, j] : holes) g[static_cast<size_t>(i + j * 20)] = 2;
    return g;
}

void OutlineTests()
{
    // One tile: from its bottom-right corner counter-clockwise; the PolyNavObstacle reverses it.
    auto c = game::GenerateCollidersFromGrid(Grid({{5, 5}}), 20, 11, {2}, 24.0f);
    Check(c.size() == 1 && c[0].name == "Collider_0" && c[0].paths.size() == 1 &&
              SamePoly(c[0].paths[0], {{-96, -12}, {-96, 12}, {-120, 12}, {-120, -12}}),
          "one hole tile" + (c.empty() ? std::string() : " " + Str(c[0].paths[0])));
    // On the grid's corner: the border vertices pushed out by 10.
    c = game::GenerateCollidersFromGrid(Grid({{0, 0}}), 20, 11, {2}, 24.0f);
    Check(c.size() == 1 && SamePoly(c[0].paths[0], {{-216, -142}, {-216, -108}, {-250, -108}, {-250, -142}}),
          "a corner tile" + (c.empty() ? std::string() : " " + Str(c[0].paths[0])));
    // Two tiles side by side: one outline, the straight run's middle vertices cleaned away.
    c = game::GenerateCollidersFromGrid(Grid({{5, 5}, {6, 5}}), 20, 11, {2}, 24.0f);
    Check(c.size() == 1 && SamePoly(c[0].paths[0], {{-72, -12}, {-72, 12}, {-120, 12}, {-120, -12}}),
          "two tiles" + (c.empty() ? std::string() : " " + Str(c[0].paths[0])));
    // An L: six corners.
    c = game::GenerateCollidersFromGrid(Grid({{5, 5}, {6, 5}, {5, 6}}), 20, 11, {2}, 24.0f);
    Check(c.size() == 1 && c[0].paths[0].size() == 6, "an L: six corners" + (c.empty() ? std::string() : " " + Str(c[0].paths[0])));
    // Two regions: two colliders, numbered in order.
    c = game::GenerateCollidersFromGrid(Grid({{2, 2}, {12, 7}}), 20, 11, {2}, 24.0f);
    Check(c.size() == 2 && c[0].name == "Collider_0" && c[1].name == "Collider_1", "two regions, two colliders");
    // A ring: its inner outline joins the outer collider as a second path (PolygonInside).
    c = game::GenerateCollidersFromGrid(Grid({{4, 4}, {5, 4}, {6, 4}, {4, 5}, {6, 5}, {4, 6}, {5, 6}, {6, 6}}), 20, 11, {2}, 24.0f);
    std::string ring;
    for (auto& col : c) {
        ring += " " + col.name + ":";
        for (auto& p : col.paths) ring += " " + Str(p);
    }
    // The inner outline's start tile comes first in the scan (column 4, the corner tile below
    // the gap), so Collider_0's first path - what PolyNav reads - is the inner one.
    Check(c.size() == 1 && c[0].paths.size() == 2 && c[0].paths[0].size() == 4 && c[0].paths[1].size() == 4, "a ring: one collider, two paths" + ring);
    if (c.size() == 1 && c[0].paths.size() == 2) {
        float w0 = 0, w1 = 0;
        for (auto& p : c[0].paths[0]) w0 = std::fmax(w0, std::fabs(p.y));
        for (auto& p : c[0].paths[1]) w1 = std::fmax(w1, std::fabs(p.y));
        Check(w0 == 12.0f && w1 == 36.0f, "a ring: the inner outline first, the outer second" + ring);
    }
}

// ---------------------------------------------------------------------------------------
// 3. The agent

void AgentTests()
{
    TestMap m;
    PolyNav2D::current = &m.nav;
    PolyNavAgent a;
    a.maxSpeed = 50.0f;
    a.mass = 2.0f;
    a.stoppingDistance = 1.0f;
    a.slowingDistance = 20.0f;
    a.accelerationRate = 200.0f;
    a.decelerationRate = 1.0f;
    a.avoidRadius = 11.0f;
    a.lookAheadDistance = 5.0f;
    a.repath = false;
    a.restrict = false;
    a.parents = {{-100, 0}, {640, 360}}; // an EnemySpawn in the room
    a.setPosition({540, 360});
    Check(Same(a.localPosition, {0, 0}) && Same(a.position(), {540, 360}), "position through the parents");
    a.Awake();
    a.OnEnable();
    bool reached = false, started = false;
    a.OnNavigationStarted = [&] { started = true; };
    a.OnDestinationReached = [&] { reached = true; };
    Check(a.SetDestination({740, 360}) && a.pathPending() && !a.hasPath(), "SetDestination: a request out");
    const float dt = 1.0f / 60.0f;
    bool inside = false;
    int frame = 0;
    for (; frame < 1200 && !reached; frame++) {
        m.nav.BeginFrame();
        m.nav.ResumeCoroutines();
        if (frame == 0) Check(started && a.hasPath() && a.activePath().size() == 3, "the path on the next frame, its start dropped");
        m.nav.LateUpdate();
        a.LateUpdate(dt);
        Vector2 p = a.position();
        if (p.x > 628 && p.x < 652 && p.y > 348 && p.y < 372) inside = true;
    }
    Check(reached && !inside, "around the obstacle to the goal in " + std::to_string(frame) + " frames, never inside");
    Check(Same(a.primeGoal(), a.position()) && !a.hasPath() && Same(a.velocity, {0, 0}), "arrived: stopped");

    // A goal inside the obstacle: the closer edge point instead (closerPointOnInvalid).
    a.closerPointOnInvalid = true;
    bool invalid = false;
    a.OnDestinationInvalid = [&] { invalid = true; };
    a.SetDestination({640, 365});
    Check(Same(a.primeGoal(), m.nav.GetCloserEdgePoint({640, 365})) && !invalid, "an invalid goal: the closer edge point");
    a.Stop();
    Check(!a.pathPending() && !a.hasPath(), "Stop: no request, no path");
    m.nav.BeginFrame();
    m.nav.ResumeCoroutines(); // the request's answer arrives and is dropped
    Check(!a.hasPath(), "a path after Stop is dropped");
    a.closerPointOnInvalid = false;
    Check(!a.SetDestination({640, 362}) && invalid, "an invalid goal without closerPointOnInvalid: OnDestinationInvalid");
    a.OnDisable();
    PolyNav2D::current = nullptr;
}

// ---------------------------------------------------------------------------------------
// 4. A real room

game::SceneRow RowFrom(const game::PrefabNode& n)
{
    game::SceneRow r;
    r.path = n.path;
    r.active = n.active;
    r.x = n.x;
    r.y = n.y;
    r.sx = n.sx;
    r.sy = n.sy;
    r.rotation = n.rotation;
    r.behaviours = n.behaviours;
    r.objectLayer = n.objectLayer;
    r.tag = n.tag;
    r.colliders = n.colliders;
    return r;
}

void PrefabRoomTest(float tileSize)
{
    const game::Prefab* room = game::PrefabLibrary::Find("DungeonRoom", "dungeon_common");
    if (!room) {
        std::printf("(no DungeonRoom prefab: skipping the prefab room)\n");
        return;
    }
    std::vector<game::SceneRow> rows;
    for (const game::PrefabNode& n : room->nodes)
        if (!n.path.empty()) rows.push_back(RowFrom(n));
    game::SceneRow holes;
    holes.path = "DungeonLevelRoomHoles";
    rows.push_back(holes);
    // A wall of holes down column 10, open at the top two rows.
    std::vector<int> grid(20 * 11, 0);
    for (int j = 0; j < 9; j++) grid[static_cast<size_t>(10 + j * 20)] = 2;
    const Vector2 at{1280, 720};
    auto nav = game::BuildRoomPolyNav(rows, grid, 20, 11, at, tileSize);
    int boundaries = 0, holeObstacles = 0;
    for (const PolyNavObstacle& o : nav->obstacles) {
        if (o.path.rfind("DungeonLevelBoundaries/", 0) == 0 || o.path.rfind("DungeonLevelDoors/", 0) == 0) boundaries++;
        if (o.path.rfind("DungeonLevelRoomHoles/", 0) == 0) holeObstacles++;
    }
    Check(boundaries == 16 && holeObstacles == 1 && nav->obstacles.size() == 17,
          "the room's obstacles: 12 boundaries, 4 door boundaries ... (" + std::to_string(boundaries) + " + " + std::to_string(holeObstacles) + ")");
    Check(SamePoly(nav->nav.map.masterPolygons[0].points, {{1046, 846}, {1046, 594}, {1514, 594}, {1514, 846}}), "the room's master");
    // Across the wall: through the gap at the top.
    PolyNav2D& n = nav->nav;
    bool got = false;
    Poly path;
    n.FindPath({1130, 720}, {1430, 720}, [&](const Poly* p) {
        got = p != nullptr;
        if (p) path = *p;
    });
    n.BeginFrame();
    n.ResumeCoroutines();
    bool overTheWall = false, inSight = got;
    for (size_t i = 0; i + 1 < path.size(); i++) {
        if (!n.CheckLOS(path[i], path[i + 1])) inSight = false;
        if (path[i].y > 720 + 84) overTheWall = true;
    }
    Check(got && path.size() >= 4 && inSight && overTheWall, "a path round the wall of holes " + Str(path));
}

// ---------------------------------------------------------------------------------------
// 5. The oracle's recording

float Bits(const Value& v)
{
    int32_t i = static_cast<int32_t>(v.AsInt());
    float f;
    std::memcpy(&f, &i, 4);
    return f;
}

Vector2 V2(const Value& v) { return {Bits(v[0]), Bits(v[1])}; }

Poly Flat(const Value& v)
{
    Poly p;
    for (size_t i = 0; i + 1 < v.size(); i += 2) p.push_back({Bits(v[i]), Bits(v[i + 1])});
    return p;
}

std::vector<Poly> Flats(const Value& v)
{
    std::vector<Poly> out;
    for (const Value& p : v.arr) out.push_back(Flat(p));
    return out;
}

// Per-category counts over the whole recording.
struct Tally {
    std::map<std::string, std::pair<int, int>> byKind; // kind -> (checked, failed)
    void Add(const std::string& kind, bool ok, const std::string& what)
    {
        auto& t = byKind[kind];
        t.first++;
        checks++;
        if (!ok) {
            t.second++;
            failures++;
            if (t.second <= 5) std::printf("FAIL [%s] %s\n", kind.c_str(), what.c_str());
        }
    }
};

std::string PolyDiff(const Poly& a, const Poly& b)
{
    if (a.size() != b.size()) return "sizes " + std::to_string(a.size()) + " vs " + std::to_string(b.size());
    if (a.empty()) return "(empty)";
    float worst = 0.0f;
    size_t at = 0;
    for (size_t i = 0; i < a.size(); i++) {
        float d = std::fmax(std::fabs(a[i].x - b[i].x), std::fabs(a[i].y - b[i].y));
        if (d > worst) {
            worst = d;
            at = i;
        }
    }
    char buf[256];
    std::snprintf(buf, sizeof buf, "point %zu: %s vs %s (off by %g)", at, Str(a[at]).c_str(), Str(b[at]).c_str(), worst);
    return buf;
}

void CompareGraph(Tally& t, const std::string& kind, const std::string& room, const PolyNav2D& nav, const Value& nodes)
{
    bool sameCount = nav.nodesCount() == static_cast<int>(nodes.size());
    t.Add(kind, sameCount,
          room + ": " + std::to_string(nav.nodesCount()) + " nodes, the game " + std::to_string(nodes.size()));
    if (!sameCount) return;
    for (size_t i = 0; i < nodes.size(); i++) {
        const auto& n = *nav.Nodes()[i];
        std::vector<int> links;
        for (const Value& l : nodes[i][2].arr) links.push_back(static_cast<int>(l.AsInt()));
        Vector2 pos{Bits(nodes[i][0]), Bits(nodes[i][1])};
        t.Add(kind, Same(n.pos, pos) && n.links == links,
              room + ": node " + std::to_string(i) + " " + Str(n.pos) + " vs " + Str(pos) + (n.links == links ? "" : ", links differ"));
    }
}

void CompareMaps(Tally& t, const std::string& kind, const std::string& room, const PolyNav2D::PolyMap& map, const std::vector<Poly>& master,
                 const std::vector<Poly>& polys)
{
    t.Add(kind, map.masterPolygons.size() == master.size(), room + ": master polygons");
    for (size_t i = 0; i < master.size() && i < map.masterPolygons.size(); i++)
        t.Add(kind, SamePoly(map.masterPolygons[i].points, master[i]), room + ": master " + PolyDiff(map.masterPolygons[i].points, master[i]));
    t.Add(kind, map.obstaclePolygons.size() == polys.size(),
          room + ": " + std::to_string(map.obstaclePolygons.size()) + " obstacle polygons, the game " + std::to_string(polys.size()));
    for (size_t i = 0; i < polys.size() && i < map.obstaclePolygons.size(); i++)
        t.Add(kind, SamePoly(map.obstaclePolygons[i].points, polys[i]),
              room + ": obstacle " + std::to_string(i) + " " + PolyDiff(map.obstaclePolygons[i].points, polys[i]));
}

game::SceneRow RecordedRow(const Value& r)
{
    game::SceneRow row;
    row.path = r[0].s;
    row.active = r[1].b;
    row.x = Bits(r[2]);
    row.y = Bits(r[3]);
    row.sx = Bits(r[4]);
    row.sy = Bits(r[5]);
    row.rotation = Bits(r[6]);
    if (r[9].IsArray()) {
        game::Behaviour b;
        b.cls = "PolyNavObstacle";
        b.fields["invertPolygon"] = r[9][0].b ? 1.0 : 0.0;
        b.fields["shapeType"] = static_cast<double>(r[9][1].AsInt());
        b.fields["extraOffset"] = static_cast<double>(Bits(r[9][2]));
        b.fields["m_Enabled"] = r[9][3].b ? 1.0 : 0.0;
        row.behaviours.push_back(b);
    }
    if (r[10].IsArray()) {
        const Value& c = r[10];
        game::ColliderDef d;
        d.type = c[0].s;
        d.enabled = c[1].b;
        d.trigger = c[2].b;
        d.ox = Bits(c[3]);
        d.oy = Bits(c[4]);
        if (d.type == "BoxCollider2D") {
            d.sx = Bits(c[5]);
            d.sy = Bits(c[6]);
        } else if (d.type == "CircleCollider2D") {
            d.radius = Bits(c[5]);
        } else if (d.type == "PolygonCollider2D") {
            for (const Value& path : c[5].arr) {
                std::vector<std::pair<float, float>> pts;
                for (Vector2 p : Flat(path)) pts.push_back({p.x, p.y});
                d.paths.push_back(pts);
            }
        }
        row.colliders.push_back(d);
    }
    return row;
}

void OracleRoom(Tally& t, const Value& rec, float tileSize)
{
    const std::string room = rec["room"].s + " (" + rec["roomInfo"].s + ")";
    const Vector2 roomPos = V2(rec["roomPos"]);
    const Value& navRec = rec["nav"];
    const float inflate = Bits(navRec["inflate"]);
    const Vector2 center = V2(navRec["pos"]);
    const std::vector<Poly> master = Flats(rec["master"]), polys = Flats(rec["polys"]);
    const Value& obstacles = rec["obstacles"];
    if (navRec["pending"].b || navRec["regen"].b) std::printf("note: %s was recorded with a request pending or a map flagged\n", room.c_str());

    // Inflate and transform each recorded obstacle.
    for (size_t k = 0; k < obstacles.size(); k++) {
        const Value& o = obstacles[k];
        if (o.IsNull()) continue;
        const std::string what = room + ": " + o["path"].s;
        Poly points = Flat(o["points"]);
        Poly inflated = PolyNav2D::InflatePolygon(points, unity::Mathf::Max(0.01f, inflate + Bits(o["extra"])));
        Poly recorded = o["inflated"].IsArray() ? Flat(o["inflated"]) : Poly{};
        t.Add("inflate", SamePoly(inflated, recorded), what + " " + PolyDiff(inflated, recorded));
        game::NavTransform tr;
        bool flat = true;
        for (const Value& l : o["chain"].arr) {
            game::NavTransformLevel lv;
            lv.position = {Bits(l[0]), Bits(l[1])};
            lv.scale = {Bits(l[2]), Bits(l[3])};
            if (Bits(l[4]) != 0.0f || Bits(l[5]) != 0.0f) flat = false;
            lv.qz = Bits(l[6]);
            lv.qw = Bits(l[7]);
            tr.levels.push_back(lv);
        }
        if (!flat) std::printf("note: %s is rotated about x or y\n", what.c_str());
        Poly world;
        for (Vector2 p : recorded) world.push_back(tr.TransformPoint(p));
        if (k < polys.size()) t.Add("transform", SamePoly(world, polys[k]), what + " " + PolyDiff(world, polys[k]));
    }

    // The graph from the game's polygons.
    PolyNav2D nav;
    nav.inflateRadius = inflate;
    std::vector<PolyNav2D::Polygon> mp, op;
    for (const Poly& p : master) mp.push_back({p});
    for (const Poly& p : polys) op.push_back({p});
    nav.map = PolyNav2D::PolyMap(mp, op);
    nav.GenerateNodes();
    CompareGraph(t, "graph", room, nav, rec["nodes"]);

    // Probes.
    for (const Value& p : rec["probes"].arr) {
        Vector2 at{Bits(p[0]), Bits(p[1])};
        Vector2 closer = nav.GetCloserEdgePoint(at);
        t.Add("probes", nav.PointIsValid(at) == p[2].b, room + ": PointIsValid" + Str(at));
        t.Add("probes", Same(closer, {Bits(p[3]), Bits(p[4])}), room + ": GetCloserEdgePoint" + Str(at) + " " + Str(closer) + " vs " + Str({Bits(p[3]), Bits(p[4])}));
        t.Add("probes", nav.CheckLOS(center, at) == p[5].b, room + ": CheckLOS to " + Str(at));
    }

    // Paths, in the recorded order on one map (the nodes keep their costs between searches, as
    // the game's do).
    for (const Value& p : rec["paths"].arr) {
        Vector2 s = V2(p["start"]), e = V2(p["end"]);
        bool called = false, got = false;
        Poly path;
        nav.FindPath(s, e, [&](const Poly* r) {
            called = true;
            got = r != nullptr;
            if (r) path = *r;
        });
        int frames = 0;
        while (!called && frames < 10) {
            nav.BeginFrame();
            nav.ResumeCoroutines();
            frames++;
        }
        const int gameFrames = static_cast<int>(p["frames"].AsInt());
        const bool gameGot = p["path"].IsArray();
        Poly gamePath = gameGot ? Flat(p["path"]) : Poly{};
        t.Add("paths", called == p["got"].b && got == gameGot && SamePoly(path, gamePath),
              room + ": FindPath " + Str(s) + " -> " + Str(e) + ": " + Str(path) + " vs " + Str(gamePath));
        t.Add("path frames", (frames == 0) == (gameFrames == 0),
              room + ": FindPath " + Str(s) + " answered after " + std::to_string(frames) + " frames, the game " + std::to_string(gameFrames));
    }

    // The holes from the grid.
    std::vector<int> grid;
    for (const Value& v : rec["grid"].arr) grid.push_back(static_cast<int>(v.AsInt()));
    const int gx = static_cast<int>(rec["gridX"].AsInt());
    const int gy = gx > 0 ? static_cast<int>(grid.size()) / gx : 0;
    std::vector<game::SceneRow> rowsNoHoles;
    std::vector<std::pair<std::string, std::vector<Poly>>> gameHoles;
    for (const Value& r : rec["rows"].arr) {
        game::SceneRow row = RecordedRow(r);
        if (row.path.rfind("DungeonLevelRoomHoles/", 0) == 0) {
            std::vector<Poly> paths;
            if (!row.colliders.empty())
                for (const auto& path : row.colliders[0].paths) {
                    Poly p;
                    for (auto [x, y] : path) p.push_back({x, y});
                    paths.push_back(p);
                }
            gameHoles.push_back({row.path.substr(std::strlen("DungeonLevelRoomHoles/")), paths});
        } else {
            rowsNoHoles.push_back(row);
        }
    }
    if (gx > 0) {
        auto holes = game::GenerateCollidersFromGrid(grid, gx, gy, {2}, tileSize);
        t.Add("holes", holes.size() == gameHoles.size(),
              room + ": " + std::to_string(holes.size()) + " hole colliders, the game " + std::to_string(gameHoles.size()));
        for (size_t i = 0; i < holes.size() && i < gameHoles.size(); i++) {
            bool same = holes[i].name == gameHoles[i].first && holes[i].paths.size() == gameHoles[i].second.size();
            for (size_t k = 0; same && k < holes[i].paths.size(); k++) same = SamePoly(holes[i].paths[k], gameHoles[i].second[k]);
            t.Add("holes", same,
                  room + ": " + holes[i].name + " " + (holes[i].paths.empty() ? "" : Str(holes[i].paths[0])) + " vs " + gameHoles[i].first + " " +
                      (gameHoles[i].second.empty() ? "" : Str(gameHoles[i].second[0])));
        }
    }

    // The whole map from the room's hierarchy.
    auto built = game::BuildRoomPolyNav(rowsNoHoles, grid, gx, gy, roomPos, tileSize, inflate);
    t.Add("builder", Same(built->nav.position, center), room + ": the map's centre " + Str(built->nav.position) + " vs " + Str(center));
    std::vector<std::string> gamePaths;
    for (const Value& o : obstacles.arr)
        if (!o.IsNull()) gamePaths.push_back(o["inRoom"].b ? o["path"].s : "(outside) " + o["path"].s);
    bool sameOrder = built->obstacles.size() == gamePaths.size();
    for (size_t i = 0; sameOrder && i < gamePaths.size(); i++) sameOrder = built->obstacles[i].path == gamePaths[i];
    std::string order;
    if (!sameOrder) {
        for (size_t i = 0; i < std::max(built->obstacles.size(), gamePaths.size()); i++)
            order += "\n    " + (i < built->obstacles.size() ? built->obstacles[i].path : std::string("-")) + "  |  " +
                     (i < gamePaths.size() ? gamePaths[i] : std::string("-"));
    }
    t.Add("builder", sameOrder, room + ": the obstacles in order (port | game)" + order);
    if (sameOrder)
        for (size_t i = 0; i < gamePaths.size(); i++) {
            Poly recorded = Flat(obstacles[i]["points"]);
            t.Add("builder", SamePoly(built->obstacles[i].points, recorded),
                  room + ": " + gamePaths[i] + " points " + PolyDiff(built->obstacles[i].points, recorded));
        }
    CompareMaps(t, "builder", room, built->nav.map, master, polys);
    CompareGraph(t, "builder graph", room, built->nav, rec["nodes"]);
}

void OracleTests(const std::string& file, float tileSize)
{
    std::ifstream in(file);
    if (!in) {
        std::printf("(no %s: skipping the oracle comparison; record it with -oracle-polynav)\n", file.c_str());
        return;
    }
    Tally t;
    int rooms = 0;
    for (std::string line; std::getline(in, line);) {
        if (line.empty()) continue;
        Value rec = json::Parse(line);
        if (rec["ev"].s == "polynavStart") {
            std::printf("oracle: session %s floor %lld culture %lld\n", rec["session"].s.c_str(), static_cast<long long>(rec["floor"].AsInt()),
                        static_cast<long long>(rec["culture"].AsInt()));
            continue;
        }
        if (rec["ev"].s != "polynavRoom") continue;
        OracleRoom(t, rec, tileSize);
        rooms++;
    }
    std::printf("oracle: %d rooms\n", rooms);
    for (const auto& [kind, n] : t.byKind) std::printf("  %-14s %6d/%d\n", kind.c_str(), n.first - n.second, n.first);
}

} // namespace

int main(int argc, char** argv)
{
    cs::Heap heap;
    cs::SetCurrentHeap(&heap);
    const std::string file = argc > 1 ? argv[1] : "data/oracle/polynav.jsonl";
    const std::string data = std::getenv("ML_DATA") ? std::getenv("ML_DATA") : "data/extracted";

    GeometryTests();
    MapTests();
    OutlineTests();
    AgentTests();
    std::printf("hand-made: %d/%d\n", checks - failures, checks);

    float tileSize = 24.0f;
    if (game::Constants::LoadFromFile(data + "/constants.json")) tileSize = game::Constants::GetFloat("tileSize");
    const std::string prefabs = ReadFile(data + "/prefabs.json");
    if (!prefabs.empty()) {
        game::PrefabLibrary::Load(json::Parse(prefabs));
        PrefabRoomTest(tileSize);
    } else {
        std::printf("(no %s/prefabs.json: skipping the prefab room)\n", data.c_str());
    }
    OracleTests(file, tileSize);

    std::printf("%s: %d/%d checks\n", failures ? "FAILED" : "ok", checks - failures, checks);
    return failures ? 1 : 0;
}
