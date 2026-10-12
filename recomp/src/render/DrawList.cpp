#include "render/DrawList.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "game/Prefabs.h"

namespace render {

namespace {

// A 2D affine transform: (x, y) -> (a x + c y + tx, b x + d y + ty).
struct Affine {
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
    Affine Then(const Affine& local) const // this * local
    {
        Affine r;
        r.a = a * local.a + c * local.b;
        r.b = b * local.a + d * local.b;
        r.c = a * local.c + c * local.d;
        r.d = b * local.c + d * local.d;
        r.tx = a * local.tx + c * local.ty + tx;
        r.ty = b * local.tx + d * local.ty + ty;
        return r;
    }
    void Apply(float x, float y, float& ox, float& oy) const
    {
        ox = a * x + c * y + tx;
        oy = b * x + d * y + ty;
    }
};

Affine Local(const game::SceneRow& r)
{
    float rad = r.rotation * 3.14159265358979f / 180.0f;
    float cs = std::cos(rad), sn = std::sin(rad);
    Affine m;
    m.a = cs * r.sx;
    m.b = sn * r.sx;
    m.c = -sn * r.sy;
    m.d = cs * r.sy;
    m.tx = r.x;
    m.ty = r.y;
    return m;
}

const game::Behaviour* DepthOrder(const game::SceneRow& r)
{
    for (const game::Behaviour& b : r.behaviours)
        if (b.cls == "SpriteDepthOrderRender") return &b;
    return nullptr;
}

// "Details/WallDetails/DetailSpawn@3" -> ("Details/WallDetails/DetailSpawn", 3); no '@': -1.
std::pair<std::string, int> Indexed(const std::string& path)
{
    size_t at = path.rfind('@');
    if (at == std::string::npos || path.find('/', at) != std::string::npos) return {path, -1};
    return {path.substr(0, at), std::atoi(path.c_str() + at + 1)};
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

    // Each row's parent: the latest row before it with its parent's path ("@n": the n-th row
    // with that path).
    std::vector<int> parent(rows.size(), -1);
    std::map<std::string, std::vector<int>> seen; // path -> rows with it, in order
    for (size_t i = 0; i < rows.size(); i++) {
        const std::string& path = rows[i].path;
        size_t slash = path.rfind('/');
        if (slash != std::string::npos) {
            auto [p, n] = Indexed(path.substr(0, slash));
            auto it = seen.find(p);
            if (it != seen.end() && !it->second.empty())
                parent[i] = n >= 0 ? (n < static_cast<int>(it->second.size()) ? it->second[static_cast<size_t>(n)] : -1) : it->second.back();
        }
        seen[Indexed(path).first].push_back(static_cast<int>(i));
    }

    Affine room;
    room.tx = roomPosition.x;
    room.ty = roomPosition.y;
    std::vector<Affine> world(rows.size());
    std::vector<char> active(rows.size());
    std::vector<int> order(rows.size());
    std::vector<DrawQuad> out;
    for (size_t i = 0; i < rows.size(); i++) {
        const game::SceneRow& r = rows[i];
        const int p = parent[i];
        world[i] = (p >= 0 ? world[static_cast<size_t>(p)] : room).Then(Local(r));
        active[i] = r.active && (p < 0 || active[static_cast<size_t>(p)]);
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
        if (!active[i] || !r.rendererEnabled || r.sprite.empty()) continue;
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
    std::stable_sort(out.begin(), out.end(), [](const DrawQuad& a, const DrawQuad& b) {
        if (a.layer != b.layer) return a.layer < b.layer;
        if (a.order != b.order) return a.order < b.order;
        return a.sortY > b.sortY;
    });
    return out;
}

} // namespace render
