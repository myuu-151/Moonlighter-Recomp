#include "render/DrawList.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "game/Prefabs.h"
#include "game/SceneTransform.h"

namespace render {

namespace {

const game::Behaviour* DepthOrder(const game::SceneRow& r)
{
    for (const game::Behaviour& b : r.behaviours)
        if (b.cls == "SpriteDepthOrderRender") return &b;
    return nullptr;
}

} // namespace

std::vector<DrawQuad> BuildDrawList(const std::vector<game::SceneRow>& rows, unity::Vector2 roomPosition, const SpriteBank& bank,
                                    std::vector<std::string>* missing)
{
    const std::vector<std::string>& layers = game::PrefabLibrary::SortingLayers();
    auto layerIndex = [&](const std::string& name) {
        for (size_t i = 0; i < layers.size(); i++)
            if (layers[i] == name) return static_cast<int>(i);
        return 0;
    };

    const game::SceneWorld scene = game::ResolveScene(rows, roomPosition);
    const std::vector<game::Affine>& world = scene.world;
    std::vector<int> order(rows.size());
    std::vector<DrawQuad> out;
    for (size_t i = 0; i < rows.size(); i++) {
        const game::SceneRow& r = rows[i];
        const int p = scene.parent[i];
        order[i] = r.order;
        if (const game::Behaviour* d = DepthOrder(r)) {
            if (d->Field("forceOrderValue") != 0.0) {
                order[i] = static_cast<int>(d->Field("forcedOrderValue"));
            } else if (d->Field("over") != 0.0 && p >= 0) {
                // SetOver: one more than the renderer it's set over (taken as its parent's).
                order[i] = order[static_cast<size_t>(p)] + 1 + static_cast<int>(d->Field("overOffset"));
            } else {
                float y = world[i].ty + static_cast<float>(d->Field("offset"));
                order[i] = 15000 + static_cast<int>(y * 10.0f) * -1;
            }
        }
        if (!scene.active[i] || !r.rendererEnabled || r.sprite.empty()) continue;
        const Sprite* s = bank.Find(r.sprite);
        if (!s) {
            if (missing) missing->push_back(r.sprite);
            continue;
        }
        DrawQuad q;
        q.sprite = s;
        float x0 = s->left, y0 = s->bottom;
        float x1 = x0 + static_cast<float>(s->width) * s->unitsPerPixel, y1 = y0 + static_cast<float>(s->height) * s->unitsPerPixel;
        if (r.flipX) {
            x0 = -x0;
            x1 = -x1;
        }
        if (r.flipY) {
            y0 = -y0;
            y1 = -y1;
        }
        world[i].Apply(x0, y0, q.x[0], q.y[0]);
        world[i].Apply(x1, y0, q.x[1], q.y[1]);
        world[i].Apply(x1, y1, q.x[2], q.y[2]);
        world[i].Apply(x0, y1, q.x[3], q.y[3]);
        for (int c = 0; c < 4; c++) q.color[c] = r.color[c];
        q.layer = layerIndex(r.layer);
        q.order = order[i];
        q.sortY = world[i].ty;
        q.sequence = static_cast<int>(i);
        q.path = r.path;
        out.push_back(q);
    }
    SortDrawList(out);
    return out;
}

void SortDrawList(std::vector<DrawQuad>& quads)
{
    std::stable_sort(quads.begin(), quads.end(), [](const DrawQuad& a, const DrawQuad& b) {
        if (a.layer != b.layer) return a.layer < b.layer;
        if (a.order != b.order) return a.order < b.order;
        return a.sortY > b.sortY;
    });
}

} // namespace render
