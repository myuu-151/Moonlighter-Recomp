#include "game/RoomPhysics.h"

#include <algorithm>

#include "game/SceneTransform.h"

namespace game {

int AddRoomColliders(const std::vector<SceneRow>& rows, unity::Vector2 roomPosition, unity::Physics2D& physics, const std::string& pathPrefix)
{
    const SceneWorld scene = ResolveScene(rows, roomPosition);
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
