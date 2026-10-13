#include "game/RoomPhysics.h"

#include <algorithm>
#include <cmath>

#include "game/SceneTransform.h"

namespace game {

Turn Turn::Then(float localZ, float localW, float sx, float sy) const
{
    // (w1 z2 + z1 w2 + x1 y2 - y1 x2, w1 w2 - x1 x2 - y1 y2 - z1 z2 with x and y 0.)
    Turn t;
    t.z = w * localZ + z * localW;
    t.w = w * localW - z * localZ;
    t.scale = {scale.x * sx, scale.y * sy};
    return t;
}

namespace {

// A row's or prefab node's local turn: its stored quaternion, else its degrees (none: unturned).
void LocalTurn(bool hasQuaternion, float qz, float qw, float degrees, float& z, float& w)
{
    if (hasQuaternion) {
        z = qz;
        w = qw;
    } else if (degrees == 0.0f) {
        z = 0.0f;
        w = 1.0f;
    } else {
        const float half = degrees * 0.5f * 0.0174532924f;
        z = std::sin(half);
        w = std::cos(half);
    }
}

} // namespace

bool DungeonRoomTurn(const std::string& path, Turn& turn)
{
    if (!PrefabLibrary::Loaded()) return false;
    const Prefab* room = PrefabLibrary::Find("DungeonRoom", "dungeon_common");
    if (!room) return false;
    auto node = [&](const std::string& p) -> const PrefabNode* {
        for (const PrefabNode& n : room->nodes)
            if (n.path == p) return &n;
        return nullptr;
    };
    if (!node(path)) return false;
    turn = Turn{};
    for (size_t end = 0; end != std::string::npos;) {
        end = path.find('/', end + 1);
        const PrefabNode* n = node(path.substr(0, end));
        if (!n) return false;
        float z, w;
        LocalTurn(n->hasQuaternion, n->qz, n->qw, n->rotation, z, w);
        turn = turn.Then(z, w, n->sx, n->sy);
    }
    return true;
}

int AddRoomColliders(const std::vector<SceneRow>& rows, unity::Vector2 roomPosition, unity::Physics2D& physics, const std::string& pathPrefix)
{
    const SceneWorld scene = ResolveScene(rows, roomPosition);
    // Each row's world turn, down its parents from the room's unturned root.
    std::vector<Turn> turns(rows.size());
    for (size_t i = 0; i < rows.size(); i++) {
        const int p = scene.parent[i];
        float z, w;
        LocalTurn(rows[i].hasQuaternion, rows[i].qz, rows[i].qw, rows[i].rotation, z, w);
        turns[i] = (p >= 0 ? turns[static_cast<size_t>(p)] : Turn{}).Then(z, w, rows[i].sx, rows[i].sy);
    }
    int added = 0;
    for (size_t i = 0; i < rows.size(); i++) {
        if (!scene.active[i]) continue;
        const SceneRow& r = rows[i];
        const Affine& m = scene.world[i];
        for (const ColliderDef& d : r.colliders) {
            if (d.type != "BoxCollider2D") continue;
            unity::Collider2D c;
            c.path = pathPrefix + r.path;
            c.tag = r.tag;
            c.layer = r.objectLayer;
            c.enabled = d.enabled;
            c.trigger = d.trigger;
            c.position = {m.tx, m.ty};
            c.offset = {d.ox, d.oy};
            c.size = {d.sx, d.sy};
            c.rotZ = turns[i].z;
            c.rotW = turns[i].w;
            c.matrixScale = turns[i].scale;
            // The box's corners through the object's world transform.
            float xs[4], ys[4];
            const float hx = d.sx / 2.0f, hy = d.sy / 2.0f;
            m.Apply(d.ox - hx, d.oy - hy, xs[0], ys[0]);
            m.Apply(d.ox + hx, d.oy - hy, xs[1], ys[1]);
            m.Apply(d.ox + hx, d.oy + hy, xs[2], ys[2]);
            m.Apply(d.ox - hx, d.oy + hy, xs[3], ys[3]);
            c.hasWorldBox = true;
            c.worldMin = {*std::min_element(xs, xs + 4), *std::min_element(ys, ys + 4)};
            c.worldMax = {*std::max_element(xs, xs + 4), *std::max_element(ys, ys + 4)};
            physics.Add(c);
            added++;
        }
    }
    return added;
}

void UpdateDoorColliders(const RoomDoors& doors, unity::Physics2D& physics, const std::string& pathPrefix)
{
    for (unity::Collider2D& c : physics.Colliders())
        for (int i = 0; i < 4; i++) {
            if (c.path == pathPrefix + kDoorPaths[i]) c.enabled = doors.doors[i].colliderEnabled;
            if (c.path == pathPrefix + kDoorBoundaryPaths[i]) {
                c.layer = doors.boundaryLayer[i];
                c.tag = doors.boundaryTag[i];
            }
        }
}

} // namespace game
