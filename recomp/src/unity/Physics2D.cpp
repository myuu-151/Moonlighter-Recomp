#include "unity/Physics2D.h"

#include <cmath>
#include <limits>

namespace unity {

Vector2 Collider2D::Min() const
{
    float w = std::fabs(size.x * scale.x), h = std::fabs(size.y * scale.y);
    return {position.x + offset.x * scale.x - w / 2.0f, position.y + offset.y * scale.y - h / 2.0f};
}

Vector2 Collider2D::Max() const
{
    float w = std::fabs(size.x * scale.x), h = std::fabs(size.y * scale.y);
    return {position.x + offset.x * scale.x + w / 2.0f, position.y + offset.y * scale.y + h / 2.0f};
}

Collider2D& Physics2D::Add(const Collider2D& c)
{
    colliders_.push_back(c);
    return colliders_.back();
}

namespace {

bool InMask(const Collider2D& c, int mask)
{
    return c.enabled && (mask & (1 << c.layer)) != 0;
}

// Gap between two boxes' cores (negative when they overlap).
float Gap(Vector2 amin, Vector2 amax, Vector2 bmin, Vector2 bmax)
{
    float dx = std::max(bmin.x - amax.x, amin.x - bmax.x);
    float dy = std::max(bmin.y - amax.y, amin.y - bmax.y);
    if (dx > 0.0f && dy > 0.0f) return std::sqrt(dx * dx + dy * dy);
    return std::max(dx, dy);
}

} // namespace

RaycastHit2D Physics2D::BoxCast(Vector2 origin, Vector2 size, float, Vector2 direction, float distance, int layerMask) const
{
    RaycastHit2D best;
    float len = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    Vector2 d = len > 0.0f ? Vector2{direction.x / len, direction.y / len} : Vector2{};
    Vector2 half{std::fabs(size.x) / 2.0f, std::fabs(size.y) / 2.0f};
    float reach = 2.0f * kSkin; // both shapes' skins
    float bestT = std::numeric_limits<float>::infinity();
    for (const Collider2D& c : colliders_) {
        if (!InMask(c, layerMask)) continue;
        Vector2 mn = c.Min(), mx = c.Max();
        // The target grown by the caster's half size and the skins: a ray from the origin.
        Vector2 emn{mn.x - half.x - reach, mn.y - half.y - reach}, emx{mx.x + half.x + reach, mx.y + half.y + reach};
        float t0 = 0.0f, t1 = distance;
        int axis = -1;
        bool inside = origin.x > emn.x && origin.x < emx.x && origin.y > emn.y && origin.y < emx.y;
        if (!inside) {
            bool miss = false;
            for (int k = 0; k < 2 && !miss; k++) {
                float o = k == 0 ? origin.x : origin.y, v = k == 0 ? d.x : d.y;
                float lo = k == 0 ? emn.x : emn.y, hi = k == 0 ? emx.x : emx.y;
                if (v == 0.0f) {
                    if (o <= lo || o >= hi) miss = true;
                    continue;
                }
                float a = (lo - o) / v, b = (hi - o) / v;
                if (a > b) std::swap(a, b);
                if (a > t0) { t0 = a; axis = k; }
                if (b < t1) t1 = b;
                if (t0 > t1) miss = true;
            }
            if (miss) continue;
        } else {
            t0 = 0.0f;
        }
        if (t0 < bestT) {
            bestT = t0;
            best.collider = &c;
            best.distance = t0;
            best.fraction = distance > 0.0f ? t0 / distance : 0.0f;
            Vector2 at = origin + d * t0;
            best.centroid = at;
            // The contact on the target's surface.
            float px = std::fmin(std::fmax(at.x, mn.x), mx.x), py = std::fmin(std::fmax(at.y, mn.y), mx.y);
            if (axis == 0) px = d.x > 0.0f ? mn.x : mx.x;
            if (axis == 1) py = d.y > 0.0f ? mn.y : mx.y;
            if (axis == 0) py = std::fmin(std::fmax(at.y, mn.y), mx.y);
            if (axis == 1) px = std::fmin(std::fmax(at.x, mn.x), mx.x);
            best.point = {px, py};
            best.normal = axis == 0 ? Vector2{d.x > 0.0f ? -1.0f : 1.0f, 0.0f} : axis == 1 ? Vector2{0.0f, d.y > 0.0f ? -1.0f : 1.0f} : Vector2{};
        }
    }
    return best;
}

const Collider2D* Physics2D::OverlapBox(Vector2 point, Vector2 size, float, int layerMask) const
{
    Vector2 mn{point.x - std::fabs(size.x) / 2.0f, point.y - std::fabs(size.y) / 2.0f};
    Vector2 mx{point.x + std::fabs(size.x) / 2.0f, point.y + std::fabs(size.y) / 2.0f};
    // Unity returns one of the overlapping colliders, whichever Box2D's tree visits first. The
    // one overlapping the box the most is what the game's walks show (player_walk_test).
    const Collider2D* best = nullptr;
    float bestArea = -1.0f;
    for (const Collider2D& c : colliders_) {
        if (!InMask(c, layerMask) || Gap(mn, mx, c.Min(), c.Max()) >= 2.0f * kSkin) continue;
        Vector2 cmn = c.Min(), cmx = c.Max();
        float area = std::max(0.0f, std::fmin(mx.x, cmx.x) - std::fmax(mn.x, cmn.x)) * std::max(0.0f, std::fmin(mx.y, cmx.y) - std::fmax(mn.y, cmn.y));
        if (area > bestArea) {
            bestArea = area;
            best = &c;
        }
    }
    return best;
}

bool Physics2D::IsTouching(const Collider2D& a, const Collider2D& b)
{
    return Gap(a.Min(), a.Max(), b.Min(), b.Max()) < 2.0f * kSkin;
}

} // namespace unity
