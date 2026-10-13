# libtess2

How Unity 2019.2 cuts a PolygonCollider2D into Box2D's convex polygons
(`recomp/src/unity/PolygonCollider2D.*`): the SGI GLU tessellator as refactored by Mikko
Mononen, polygons of up to 8 vertices. UnityPlayer.dll, read with its public symbols
(`recomp/third_party/box2d/README.md`, "Reading Unity's fork"), links libtess2 (`tessNewTess`,
`tessAddContour`, `tessTesselate`, ...), and `PolygonCollider2D::PreparePolygonShapes` calls
`tessAddContour(tess, 2, points, 8, count)` for each path and then `tessTesselate(tess,
TESS_WINDING_ODD, TESS_POLYGONS, 8, 2, NULL)`, as the port does. Its libtess2 has the
constrained Delaunay pass (`tessMeshRefineDelaunay`), which `tessTesselate` runs only when a flag
of the tessellator is set: `tessNewTess` zeroes it and `PreparePolygonShapes` doesn't set it.
Which version its merge of triangles into polygons is isn't read; the oracle's polygon probe
(`recomp/tests/polygon_probe.h`) finds the pieces of this one on every recorded hole (the count,
and the nearest piece rays from inside meet, bit for bit).

- Source: https://github.com/memononen/libtess2, commit
  `37d6fb0955f7a8b457529f5d2c2ad8eaddc78cbf` (2015-05-06, "Fixed dead store analyzer
  warnings"): the last version before the 2018 changes (a rewritten
  `tessMeshMergeConvexFaces`; the constrained Delaunay pass, which may be what Unity 2021.2
  exposes as `PolygonCollider2D.useDelaunayMesh`). On every PolygonCollider2D in the oracle's
  recordings it gives exactly what the 2013 initial commit gives; the 2025 master gives the
  same pieces with other starting vertices and, for one collider, other pieces.
- Taken: `Include/tesselator.h`, `Source/*`. Not changed.
- Licence: SGI Free Software License B 2.0 (`LICENSE.txt`).

Built into `mlcore` as C (`recomp/CMakeLists.txt`, warnings off) with the game code's float
flags (no FMA contraction). Its arithmetic is float; for the rooms' hole outlines (integer
coordinates) the output vertices are the input's.
