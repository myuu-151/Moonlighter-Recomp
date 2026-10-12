#include "unity/Physics2D.h"

#include <cmath>
#include <limits>

namespace unity {

Vector2 Collider2D::Min() const
{
    if (hasWorldBox) return worldMin;
    float w = std::fabs(size.x * scale.x), h = std::fabs(size.y * scale.y);
    return {position.x + offset.x * scale.x - w / 2.0f, position.y + offset.y * scale.y - h / 2.0f};
}

Vector2 Collider2D::Max() const
{
    if (hasWorldBox) return worldMax;
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

namespace {

// A box (angle 0) of half size `half` swept from `origin` along unit `d` up to `distance`
// against the box [mn, mx], stopping kCastReach short of its core: the distance to first
// contact, and the axis that stopped it (0 x, 1 y, -1: overlapping at the start). False if it
// doesn't hit.
bool Sweep(Vector2 origin, Vector2 half, Vector2 d, float distance, Vector2 mn, Vector2 mx, float& t, int& axis)
{
    const float reach = Physics2D::kCastReach;
    Vector2 emn{mn.x - half.x - reach, mn.y - half.y - reach}, emx{mx.x + half.x + reach, mx.y + half.y + reach};
    float t0 = 0.0f, t1 = distance;
    axis = -1;
    if (origin.x > emn.x && origin.x < emx.x && origin.y > emn.y && origin.y < emx.y) {
        t = 0.0f;
        return true;
    }
    for (int k = 0; k < 2; k++) {
        float o = k == 0 ? origin.x : origin.y, v = k == 0 ? d.x : d.y;
        float lo = k == 0 ? emn.x : emn.y, hi = k == 0 ? emx.x : emx.y;
        if (v == 0.0f) {
            if (o <= lo || o >= hi) return false;
            continue;
        }
        float a = (lo - o) / v, b = (hi - o) / v;
        if (a > b) std::swap(a, b);
        if (a > t0) {
            t0 = a;
            axis = k;
        }
        if (b < t1) t1 = b;
        if (t0 > t1) return false;
    }
    t = t0;
    return true;
}

float OverlapArea(Vector2 amn, Vector2 amx, Vector2 bmn, Vector2 bmx)
{
    return std::max(0.0f, std::fmin(amx.x, bmx.x) - std::fmax(amn.x, bmn.x)) * std::max(0.0f, std::fmin(amx.y, bmx.y) - std::fmax(amn.y, bmn.y));
}

} // namespace

RaycastHit2D Physics2D::BoxCast(Vector2 origin, Vector2 size, float, Vector2 direction, float distance, int layerMask) const
{
    RaycastHit2D best;
    float len = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    Vector2 d = len > 0.0f ? Vector2{direction.x / len, direction.y / len} : Vector2{};
    Vector2 half{std::fabs(size.x) / 2.0f, std::fabs(size.y) / 2.0f};
    float bestT = std::numeric_limits<float>::infinity();
    for (const Collider2D& c : colliders_) {
        if (!InMask(c, layerMask)) continue;
        const size_t boxes = c.parts.empty() ? 1 : c.parts.size();
        for (size_t b = 0; b < boxes; b++) {
            Vector2 mn = c.parts.empty() ? c.Min() : c.parts[b].first, mx = c.parts.empty() ? c.Max() : c.parts[b].second;
            float t0;
            int axis;
            if (!Sweep(origin, half, d, distance, mn, mx, t0, axis) || !(t0 < bestT)) continue;
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
        if (!InMask(c, layerMask)) continue;
        bool touches = false;
        float area = 0.0f;
        const size_t boxes = c.parts.empty() ? 1 : c.parts.size();
        for (size_t b = 0; b < boxes; b++) {
            Vector2 cmn = c.parts.empty() ? c.Min() : c.parts[b].first, cmx = c.parts.empty() ? c.Max() : c.parts[b].second;
            if (Gap(mn, mx, cmn, cmx) >= 2.0f * kSkin) continue;
            touches = true;
            area += OverlapArea(mn, mx, cmn, cmx);
        }
        if (touches && area > bestArea) {
            bestArea = area;
            best = &c;
        }
    }
    return best;
}

bool Physics2D::IsTouching(const Collider2D& a, const Collider2D& b)
{
    const size_t na = a.parts.empty() ? 1 : a.parts.size(), nb = b.parts.empty() ? 1 : b.parts.size();
    for (size_t i = 0; i < na; i++)
        for (size_t j = 0; j < nb; j++) {
            Vector2 amn = a.parts.empty() ? a.Min() : a.parts[i].first, amx = a.parts.empty() ? a.Max() : a.parts[i].second;
            Vector2 bmn = b.parts.empty() ? b.Min() : b.parts[j].first, bmx = b.parts.empty() ? b.Max() : b.parts[j].second;
            if (Gap(amn, amx, bmn, bmx) < 2.0f * kSkin) return true;
        }
    return false;
}

} // namespace unity
