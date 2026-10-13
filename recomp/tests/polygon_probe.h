#pragma once
// The oracle's polygon probe (oracle/MoonlighterOracle/PolygonProbe.cs) against the port's cut of
// a PolygonCollider2D (unity/PolygonCollider2D.h), for the replays that build the room's holes
// (enemy_replay.h, tangle_test.cpp):
//   - "shapeCount" (Collider2D.shapeCount) against the number of pieces the port makes: fails if
//     they differ (the pieces are the collider's fixtures, whose contacts the replays compare);
//   - "rays": each recorded ray (from inside the collider, queriesStartInColliders off) through
//     the port's pieces as Box2D casts it (b2PolygonShape::RayCast, a piece holding the origin
//     skipped): the same hits in the same order, the normals' bits equal and the fractions
//     within 1e-5. Informs without failing: whether Unity drops the hits of only the piece the ray
//     starts in (then they show the cuts) is the probe's own assumption.
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
    int same = 0, differ = 0;
    std::string first;
    for (const json::Value& ray : rays->arr) {
        if (ray.size() < 5) continue;
        const b2Vec2 o(PolygonProbeBits(ray[0]), PolygonProbeBits(ray[1])), d(PolygonProbeBits(ray[2]), PolygonProbeBits(ray[3]));
        const float reach = PolygonProbeBits(ray[4]);
        b2RayCastInput in;
        in.p1 = o;
        in.p2 = b2Vec2(o.x + reach * d.x, o.y + reach * d.y);
        in.maxFraction = 1.0f;
        std::vector<std::pair<float, b2Vec2>> port;
        for (const b2PolygonShape& p : pieces) {
            if (p.TestPoint(identity, o)) continue;
            b2RayCastOutput out;
            if (p.RayCast(&out, in, identity, 0)) port.emplace_back(out.fraction, out.normal);
        }
        std::sort(port.begin(), port.end(), [](const std::pair<float, b2Vec2>& a, const std::pair<float, b2Vec2>& b) { return a.first < b.first; });
        bool ok = port.size() + 5 == ray.size();
        for (size_t k = 0; ok && k < port.size(); k++) {
            const json::Value& h = ray[k + 5];
            ok = std::fabs(port[k].first - PolygonProbeBits(h[0])) <= 1e-5f && port[k].second.x == PolygonProbeBits(h[3]) &&
                 port[k].second.y == PolygonProbeBits(h[4]);
        }
        (ok ? same : differ)++;
        if (!ok && first.empty())
            first = "from (" + std::to_string(o.x) + ", " + std::to_string(o.y) + ") along (" + std::to_string(d.x) + ", " + std::to_string(d.y) +
                    "): port " + std::to_string(port.size()) + " hits, game " + std::to_string(ray.size() - 5);
    }
    t.Info("PolygonCollider2D cuts (rays from inside meet the same pieces)", differ == 0,
           c.path + ": " + std::to_string(same) + " rays alike, " + std::to_string(differ) + " not" + (first.empty() ? "" : "; first " + first));
}

} // namespace
