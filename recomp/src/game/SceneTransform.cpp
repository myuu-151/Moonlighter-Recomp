#include "game/SceneTransform.h"

#include <cmath>
#include <cstdlib>
#include <map>
#include <string>

namespace game {

Affine Affine::Then(const Affine& local) const
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

Affine Affine::Local(const SceneRow& r)
{
    float cs, sn;
    float quarter = r.rotation / 90.0f;
    if (quarter == std::floor(quarter)) {
        int q = ((static_cast<int>(quarter) % 4) + 4) % 4;
        static const float kCos[4] = {1.0f, 0.0f, -1.0f, 0.0f}, kSin[4] = {0.0f, 1.0f, 0.0f, -1.0f};
        cs = kCos[q];
        sn = kSin[q];
    } else {
        float rad = r.rotation * 3.14159265358979f / 180.0f;
        cs = std::cos(rad);
        sn = std::sin(rad);
    }
    Affine m;
    m.a = cs * r.sx;
    m.b = sn * r.sx;
    m.c = -sn * r.sy;
    m.d = cs * r.sy;
    m.tx = r.x;
    m.ty = r.y;
    return m;
}

namespace {

// "Details/WallDetails/DetailSpawn@3" -> ("Details/WallDetails/DetailSpawn", 3); no '@': -1.
std::pair<std::string, int> Indexed(const std::string& path)
{
    size_t at = path.rfind('@');
    if (at == std::string::npos || path.find('/', at) != std::string::npos) return {path, -1};
    return {path.substr(0, at), std::atoi(path.c_str() + at + 1)};
}

} // namespace

SceneWorld ResolveScene(const std::vector<SceneRow>& rows, unity::Vector2 roomPosition)
{
    SceneWorld out;
    out.parent.assign(rows.size(), -1);
    std::map<std::string, std::vector<int>> seen; // path -> rows with it, in order
    for (size_t i = 0; i < rows.size(); i++) {
        const std::string& path = rows[i].path;
        size_t slash = path.rfind('/');
        if (slash != std::string::npos) {
            auto [p, n] = Indexed(path.substr(0, slash));
            auto it = seen.find(p);
            if (it != seen.end() && !it->second.empty())
                out.parent[i] = n >= 0 ? (n < static_cast<int>(it->second.size()) ? it->second[static_cast<size_t>(n)] : -1) : it->second.back();
        }
        seen[Indexed(path).first].push_back(static_cast<int>(i));
    }
    Affine room;
    room.tx = roomPosition.x;
    room.ty = roomPosition.y;
    out.world.resize(rows.size());
    out.active.resize(rows.size());
    for (size_t i = 0; i < rows.size(); i++) {
        const int p = out.parent[i];
        out.world[i] = (p >= 0 ? out.world[static_cast<size_t>(p)] : room).Then(Affine::Local(rows[i]));
        out.active[i] = rows[i].active && (p < 0 || out.active[static_cast<size_t>(p)]);
    }
    return out;
}

} // namespace game
