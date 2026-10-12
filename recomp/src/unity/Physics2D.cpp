#include "unity/Physics2D.h"

#include <algorithm>
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

namespace {

// b2PolygonShape::RayCast of a box [mn, mx] (b2PolygonShape::SetAsBox's vertices and normals,
// in its order: (0, -1) at the bottom, (1, 0) right, (0, 1) top, (-1, 0) left) from p1 by d
// (p2 - p1), maxFraction 1: whether it enters, at which fraction, through which edge.
bool RayBox(Vector2 p1, Vector2 d, Vector2 mn, Vector2 mx, float& fraction, Vector2& normal)
{
    const Vector2 vertices[4] = {{mn.x, mn.y}, {mx.x, mn.y}, {mx.x, mx.y}, {mn.x, mx.y}};
    const Vector2 normals[4] = {{0.0f, -1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}};
    float lower = 0.0f, upper = 1.0f;
    int index = -1;
    for (int i = 0; i < 4; i++) {
        // b2Dot(normals[i], vertices[i] - p1), b2Dot(normals[i], d): floats, as Box2D has them.
        const float ex = vertices[i].x - p1.x, ey = vertices[i].y - p1.y;
        const float numerator = normals[i].x * ex + normals[i].y * ey;
        const float denominator = normals[i].x * d.x + normals[i].y * d.y;
        if (denominator == 0.0f) {
            if (numerator < 0.0f) return false;
        } else if (denominator < 0.0f && numerator < lower * denominator) {
            lower = numerator / denominator;
            index = i;
        } else if (denominator > 0.0f && numerator < upper * denominator) {
            upper = numerator / denominator;
        }
        if (upper < lower) return false;
    }
    if (index < 0) return false;
    fraction = lower;
    normal = normals[index];
    return true;
}

// b2PolygonShape::TestPoint of a box: outside only past an edge.
bool BoxHolds(Vector2 p, Vector2 mn, Vector2 mx) { return p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y; }

} // namespace

RaycastHit2D Physics2D::Raycast(Vector2 origin, Vector2 direction, float distance, int layerMask) const
{
    RaycastHit2D best;
    const float len = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (!(len > 1e-05f)) return best; // Vector2.normalized: zero below kEpsilon
    const Vector2 d{direction.x / len, direction.y / len};
    const Vector2 p2{origin.x + d.x * distance, origin.y + d.y * distance};
    const Vector2 delta{p2.x - origin.x, p2.y - origin.y};
    float bestFraction = std::numeric_limits<float>::infinity();
    for (const Collider2D& c : colliders_) {
        if (!InMask(c, layerMask) || (c.trigger && !queriesHitTriggers)) continue;
        const size_t boxes = c.parts.empty() ? 1 : c.parts.size();
        for (size_t b = 0; b < boxes; b++) {
            const Vector2 mn = c.parts.empty() ? c.Min() : c.parts[b].first, mx = c.parts.empty() ? c.Max() : c.parts[b].second;
            float fraction;
            Vector2 normal;
            if (queriesStartInColliders && BoxHolds(origin, mn, mx)) {
                fraction = 0.0f;
                normal = -d;
            } else if (!RayBox(origin, delta, mn, mx, fraction, normal)) {
                continue;
            }
            if (!(fraction < bestFraction)) continue;
            bestFraction = fraction;
            best.collider = &c;
            best.fraction = fraction;
            best.distance = fraction * distance;
            best.normal = normal;
            // b2World::RayCast's point: (1 - fraction) * p1 + fraction * p2.
            best.point = fraction == 0.0f ? origin : Vector2{(1.0f - fraction) * origin.x + fraction * p2.x, (1.0f - fraction) * origin.y + fraction * p2.y};
            best.centroid = best.point;
        }
    }
    return best;
}

std::vector<const Collider2D*> Physics2D::OverlapBoxAll(Vector2 point, Vector2 size, float, int layerMask) const
{
    Vector2 mn{point.x - std::fabs(size.x) / 2.0f, point.y - std::fabs(size.y) / 2.0f};
    Vector2 mx{point.x + std::fabs(size.x) / 2.0f, point.y + std::fabs(size.y) / 2.0f};
    // (Unity's order is its broad phase's, see the header: the most overlap first stands in.)
    std::vector<std::pair<float, const Collider2D*>> hits;
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
        if (touches) hits.emplace_back(area, &c);
    }
    std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<const Collider2D*> out;
    out.reserve(hits.size());
    for (const auto& h : hits) out.push_back(h.second);
    return out;
}

const Collider2D* Physics2D::OverlapBox(Vector2 point, Vector2 size, float angle, int layerMask) const
{
    std::vector<const Collider2D*> all = OverlapBoxAll(point, size, angle, layerMask);
    return all.empty() ? nullptr : all.front();
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
