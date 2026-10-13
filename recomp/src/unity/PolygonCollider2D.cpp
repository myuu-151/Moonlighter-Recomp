#include "unity/PolygonCollider2D.h"

#include <tesselator.h>

namespace unity {

std::vector<std::vector<Vector2>> PolygonColliderShapes(const std::vector<std::vector<Vector2>>& paths)
{
    constexpr int kMaxVertices = 8; // b2_maxPolygonVertices
    std::vector<std::vector<Vector2>> shapes;
    TESStesselator* tess = tessNewTess(nullptr);
    if (!tess) return shapes;
    for (const std::vector<Vector2>& path : paths) {
        if (path.size() < 3) continue;
        std::vector<TESSreal> xy;
        xy.reserve(path.size() * 2);
        for (const Vector2& p : path) {
            xy.push_back(p.x);
            xy.push_back(p.y);
        }
        tessAddContour(tess, 2, xy.data(), static_cast<int>(sizeof(TESSreal) * 2), static_cast<int>(path.size()));
    }
    if (tessTesselate(tess, TESS_WINDING_ODD, TESS_POLYGONS, kMaxVertices, 2, nullptr)) {
        const TESSreal* verts = tessGetVertices(tess);
        const TESSindex* elems = tessGetElements(tess);
        const int count = tessGetElementCount(tess);
        for (int i = 0; i < count; i++) {
            const TESSindex* poly = &elems[i * kMaxVertices];
            std::vector<Vector2> shape;
            for (int k = 0; k < kMaxVertices && poly[k] != TESS_UNDEF; k++)
                shape.push_back({verts[poly[k] * 2], verts[poly[k] * 2 + 1]});
            if (shape.size() >= 3) shapes.push_back(shape);
        }
    }
    tessDeleteTess(tess);
    return shapes;
}

void SetPolygonColliderShapes(Collider2D& collider, const std::vector<std::vector<Vector2>>& localPaths)
{
    collider.shapes.clear();
    for (std::vector<Vector2>& shape : PolygonColliderShapes(localPaths)) {
        for (Vector2& v : shape)
            v = {collider.position.x + collider.scale.x * (v.x + collider.offset.x), collider.position.y + collider.scale.y * (v.y + collider.offset.y)};
        collider.shapes.push_back(shape);
    }
}

} // namespace unity
