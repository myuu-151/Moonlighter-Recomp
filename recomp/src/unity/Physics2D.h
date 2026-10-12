#pragma once
// Physics2D's queries over a room's colliders, as the game's scripts use them: BoxCast and
// OverlapBox for the hero's movement, OverlapCircle for pickups. The project's settings
// (globalgamemanagers): queries hit triggers, queries starting inside a collider hit it at
// distance 0, transforms aren't synced to physics automatically.
//
// Box colliders, axis-aligned (the rooms' walls, doors and props). Box2D keeps a skin of
// `kSkin` around every polygon; casts and overlaps see it.

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

    Collider2D& Add(const Collider2D& c);
    void Clear() { colliders_.clear(); }
    const std::deque<Collider2D>& Colliders() const { return colliders_; }
    std::deque<Collider2D>& Colliders() { return colliders_; }

    // A box (angle 0) swept from `origin` along `direction` up to `distance`: the nearest hit.
    RaycastHit2D BoxCast(Vector2 origin, Vector2 size, float angle, Vector2 direction, float distance, int layerMask) const;
    // A collider overlapping the box (the one overlapping it most), or nullptr.
    const Collider2D* OverlapBox(Vector2 point, Vector2 size, float angle, int layerMask) const;
    // Whether two colliders touch (Collider2D.IsTouching: within the skins).
    static bool IsTouching(const Collider2D& a, const Collider2D& b);

private:
    std::deque<Collider2D> colliders_; // stable addresses
};

} // namespace unity
