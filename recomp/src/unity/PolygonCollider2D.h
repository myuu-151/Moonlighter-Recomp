#pragma once
// A PolygonCollider2D's shapes in Unity's 2D physics world. Box2D only takes convex polygons of
// at most b2_maxPolygonVertices (8) vertices, so Unity cuts the collider's paths into such
// polygons and makes one fixture of each (Collider2D.shapeCount of them).
//
// How Unity cuts them, as the port does it: libtess2 (third_party/libtess2, the SGI GLU
// tessellator refactored by Mikko Mononen) over every path at once, odd winding (a path inside
// another is a hole in it), polygons of up to 8 vertices (tessTesselate with TESS_POLYGONS, a
// computed normal): the tessellator's monotone triangulation, its triangles merged into convex
// polygons while they stay convex and within 8 vertices. Each polygon then goes through
// b2PolygonShape::Set (PhysicsWorld), whose hull drops the collinear vertices a merge leaves.
//
// The evidence: UnityPlayer.dll's PolygonCollider2D::PreparePolygonShapes makes exactly these
// libtess2 calls (third_party/libtess2/README.md). tangle.jsonl's Tangles 2 and 5 are pushed into
// the inside corners of a plus-shaped hole (Golem_Enemies_1, Collider_2) for 130 frames, and match
// bit for bit only when the corner has two pieces touching it, the arm and the column through the
// middle, as libtess2 cuts it (one box per tile puts a third, diagonal piece there). The oracle's
// polygon probe (PolygonProbe.cs; tests/polygon_probe.h) agrees on every recorded hole: the
// number of pieces (Collider2D.shapeCount), and for each ray cast from inside the hole the nearest
// piece it enters, bit for bit (tangle, slime, slime_bis and recq enemyfix-flykami-2's
// enemy_FlyingKamikazeGolem: 12,124 rays).

#include <vector>

#include "unity/Physics2D.h"
#include "unity/Vector2.h"

namespace unity {

// The convex polygons (at most 8 vertices, in libtess2's order) that `paths` (a
// PolygonCollider2D's, in any one frame of reference) are cut into; the result is in the same
// frame. Empty if the paths enclose nothing.
std::vector<std::vector<Vector2>> PolygonColliderShapes(const std::vector<std::vector<Vector2>>& paths);

// `collider`'s shapes (Collider2D::shapes, world vertices) from its PolygonCollider2D paths
// (local: offset, then the transform's scale and position, unrotated).
void SetPolygonColliderShapes(Collider2D& collider, const std::vector<std::vector<Vector2>>& localPaths);

} // namespace unity
