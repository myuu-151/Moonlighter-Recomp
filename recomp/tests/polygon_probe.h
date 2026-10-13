#pragma once
// The oracle's polygon probe (oracle/MoonlighterOracle/PolygonProbe.cs) against the port's cut of
// a PolygonCollider2D (unity/PolygonCollider2D.h), for the replays that build the room's holes
// (enemy_replay.h, tangle_test.cpp):
//   - "shapeCount" (Collider2D.shapeCount) against the number of pieces the port makes: fails if
//     they differ (the pieces are the collider's fixtures, whose contacts the replays compare);
//   - "rays": each recorded ray (from inside the collider, queriesStartInColliders off) cast as
//     Unity casts it: PhysicsQuery2D::RaycastAll normalizes the direction and ends the ray at
//     direction * distance + origin; b2PolygonShape::RayCast on each piece (one the ray starts in
//     gives no hit); Raycast2DQuery::ReportFixture keeps one hit per collider, a later fixture's
//     replacing it only if its fraction is smaller (UnityPlayer.dll 2019.2, read with its public
//     symbols: recomp/third_party/box2d/README.md). So a ray reports the nearest piece it enters
//     other than its own, where the cut puts that piece's face: the fraction, point and normal must
//     be the game's bit for bit. Where two pieces meet the ray at the same fraction (at a vertex
//     they share) Unity keeps the one its broad-phase tree reports first, which these pieces alone
//     don't give (the tree holds every fixture of the floor): the game's hit must be one of them.
// Recordings from before the probe have neither, and nothing is checked.

#include <Box2D/Box2D.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "unity/Physics2D.h"
#include "util/Json.h"

namespace {

inline float PolygonProbeBits(const json::Value& v)
{
    int32_t i = static_cast<int32_t>(v.AsInt());
    float f;
    std::memcpy(&f, &i, 4);
    return f;
}

template <class Checks>
void CheckPolygonProbe(Checks& t, const json::Value& line, const unity::Collider2D& c)
{
    const json::Value* count = line.Find("shapeCount");
    if (!count) return;
    t.Add("PolygonCollider2D pieces (shapeCount)", static_cast<long long>(c.shapes.size()) == count->AsInt(),
          c.path + ": port " + std::to_string(c.shapes.size()) + ", game " + std::to_string(count->AsInt()));
    const json::Value* rays = line.Find("rays");
    if (!rays) return;
    std::vector<b2PolygonShape> pieces;
    for (const std::vector<unity::Vector2>& s : c.shapes) {
        b2Vec2 v[b2_maxPolygonVertices];
        const int n = static_cast<int>(std::min<size_t>(s.size(), b2_maxPolygonVertices));
        for (int k = 0; k < n; k++) v[k] = b2Vec2(s[static_cast<size_t>(k)].x, s[static_cast<size_t>(k)].y);
        pieces.emplace_back();
        pieces.back().Set(v, n);
    }
    b2Transform identity;
    identity.SetIdentity();
    int same = 0, differ = 0, tied = 0;
    std::string first;
    for (const json::Value& ray : rays->arr) {
        if (ray.size() < 5) continue;
        const b2Vec2 o(PolygonProbeBits(ray[0]), PolygonProbeBits(ray[1])), d(PolygonProbeBits(ray[2]), PolygonProbeBits(ray[3]));
        const float reach = PolygonProbeBits(ray[4]);
        b2RayCastInput in;
        in.p1 = o;
        const float len2 = d.x * d.x + d.y * d.y, inv = len2 != 0.0f ? 1.0f / std::sqrt(len2) : len2;
        in.p2 = b2Vec2(d.x * inv * reach + o.x, d.y * inv * reach + o.y);
        in.maxFraction = 1.0f;
        // The pieces' hits at the smallest fraction.
        std::vector<b2RayCastOutput> nearest;
        for (const b2PolygonShape& p : pieces) {
            b2RayCastOutput out;
            if (!p.RayCast(&out, in, identity, 0)) continue;
            if (!nearest.empty() && out.fraction > nearest[0].fraction) continue;
            if (!nearest.empty() && out.fraction < nearest[0].fraction) nearest.clear();
            nearest.push_back(out);
        }
        bool ok = false;
        if (ray.size() == 6) {
            const json::Value& h = ray[5];
            for (const b2RayCastOutput& out : nearest) {
                const b2Vec2 point = (1.0f - out.fraction) * in.p1 + out.fraction * in.p2; // (b2WorldRayCastWrapper)
                ok = ok || (out.fraction == PolygonProbeBits(h[0]) && point.x == PolygonProbeBits(h[1]) && point.y == PolygonProbeBits(h[2]) &&
                            out.normal.x == PolygonProbeBits(h[3]) && out.normal.y == PolygonProbeBits(h[4]));
            }
        }
        (ok ? same : differ)++;
        tied += nearest.size() > 1;
        if (!ok && first.empty())
            first = "from (" + std::to_string(o.x) + ", " + std::to_string(o.y) + ") along (" + std::to_string(d.x) + ", " + std::to_string(d.y) +
                    "): the port's nearest pieces " + std::to_string(nearest.size()) + ", the game's hits " + std::to_string(ray.size() - 5);
    }
    t.Add("PolygonCollider2D cuts (the nearest piece rays from inside meet, bit for bit)", differ == 0,
          c.path + ": " + std::to_string(same) + " rays alike (" + std::to_string(tied) + " at a vertex two pieces share), " + std::to_string(differ) +
              " not" + (first.empty() ? "" : "; first " + first));
}

} // namespace
