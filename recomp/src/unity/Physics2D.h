#pragma once
// Physics2D's queries over a room's colliders, as the game's scripts use them: BoxCast and
// OverlapBox for the hero's movement, OverlapCircle for pickups, Raycast for the enemies'
// looks ahead (TangleBehaviour's weave). The project's settings
// (globalgamemanagers): queries hit triggers, queries starting inside a collider hit it at
// distance 0, transforms aren't synced to physics automatically.
//
// Box colliders, axis-aligned (the rooms' walls, doors and props). Box2D keeps a skin of
// `kSkin` around every polygon; overlaps see it, casts stop kCastReach from the core.

#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "unity/Vector2.h"

namespace unity {

struct Collider2D {
    std::string path, tag;
    int layer = 0;
    bool enabled = true, trigger = false;
    Vector2 position, scale{1.0f, 1.0f}; // the transform's, world
    Vector2 offset, size{1.0f, 1.0f};    // BoxCollider2D's, local
    // The world box when it was placed through a rotated parent (RoomPhysics); else it's
    // the transform's position and scale applied to offset and size.
    bool hasWorldBox = false;
    Vector2 worldMin, worldMax;
    // A compound collider (a PolygonCollider2D made of tiles, the holes'): its world boxes, the
    // world box above being their bounds. Empty: just the box.
    std::vector<std::pair<Vector2, Vector2>> parts;
    // World box, without the skin.
    Vector2 Min() const;
    Vector2 Max() const;
};

struct RaycastHit2D {
    const Collider2D* collider = nullptr;
    Vector2 point, normal, centroid;
    float distance = 0.0f, fraction = 0.0f;
    explicit operator bool() const { return collider != nullptr; }
};

class Physics2D {
public:
    static constexpr float kSkin = 0.01f; // Box2D's polygon radius (the default contact offset)
    // How near a cast comes to a core before it hits: Box2D's time of impact stops at its
    // target separation, max(b2_linearSlop, both skins - 3 b2_linearSlop) = 0.005, not at the
    // skins' 0.02. walk_rocks.jsonl bounds it below 0.0109: a 0.0109 gap beside a rock's corner
    // that Will's horizontal cast slid past (frame 747; 0.02 stopped him there). Within that
    // bound the recordings can't tell values apart.
    static constexpr float kCastReach = 0.005f;

    // Physics2DSettings (physics2d.json): whether queries see triggers, and whether a ray
    // starting inside a collider hits it. Raycast reads them (the other queries assume both on,
    // as the project has them).
    bool queriesHitTriggers = true;
    bool queriesStartInColliders = true;

    Collider2D& Add(const Collider2D& c);
    void Clear() { colliders_.clear(); }
    const std::deque<Collider2D>& Colliders() const { return colliders_; }
    std::deque<Collider2D>& Colliders() { return colliders_; }

    // A box (angle 0) swept from `origin` along `direction` up to `distance`: the nearest hit.
    RaycastHit2D BoxCast(Vector2 origin, Vector2 size, float angle, Vector2 direction, float distance, int layerMask) const;
    // Physics2D.Raycast(origin, direction, distance, layerMask): the nearest collider in the
    // mask (enabled; a trigger only with queriesHitTriggers) that the ray from `origin` along
    // `direction` (normalized; zero: no hit) meets within `distance`. Box2D's
    // b2PolygonShape::RayCast against each box's core, in floats (a ray ignores the skin; an
    // edge it only grazes counts), from p1 = origin to p2 = origin + direction * distance: the
    // hit's fraction along that, point (1 - fraction) p1 + fraction p2, the edge's normal,
    // distance fraction * distance. With queriesStartInColliders, a collider holding the
    // origin (its edges included, b2PolygonShape::TestPoint) is hit at fraction 0: point the
    // origin, normal -direction (Unity's documented normal there).
    RaycastHit2D Raycast(Vector2 origin, Vector2 direction, float distance, int layerMask) const;
    // A collider overlapping the box, or nullptr. When several do, Unity returns the first its
    // query meets: Box2D's broad-phase tree order (sorted by z, all 0 here), which depends on
    // every proxy the world has made and moved (his, his familiar's, the critters') and isn't
    // modelled: the one overlapping the box the most stands in for it (OverlapBoxAll's first).
    // walk.jsonl shows the order isn't geometric: at a corner the game takes one boundary with
    // him at x 2152.58 and 2150.90 (the other overlapping more at 2150.90) and the other from
    // 2149.26 on, standing still.
    const Collider2D* OverlapBox(Vector2 point, Vector2 size, float angle, int layerMask) const;
    // Every collider overlapping the box, OverlapBox's first.
    std::vector<const Collider2D*> OverlapBoxAll(Vector2 point, Vector2 size, float angle, int layerMask) const;
    // Whether two colliders touch (Collider2D.IsTouching: within the skins).
    static bool IsTouching(const Collider2D& a, const Collider2D& b);

private:
    std::deque<Collider2D> colliders_; // stable addresses
};

} // namespace unity
