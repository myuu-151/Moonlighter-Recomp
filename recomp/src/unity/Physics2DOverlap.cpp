#include "unity/Physics2DOverlap.h"

#include <algorithm>

#include <Box2D/Box2D.h>

#include "unity/PhysicsWorld.h"

namespace unity {

namespace {

// b2World::QueryAABB's callback: every fixture whose proxy's fat box meets the query's, in the
// tree's order (a fixture with several children can come more than once).
class Collect : public b2QueryCallback {
public:
    std::vector<b2Fixture*> fixtures;
    bool ReportFixture(b2Fixture* fixture) override
    {
        fixtures.push_back(fixture);
        return true;
    }
};

} // namespace

std::vector<const b2Fixture*> OverlapCircleAll(PhysicsWorld& world, Vector2 point, float radius, int layerMask, bool queriesHitTriggers)
{
    b2CircleShape circle;
    circle.m_p.SetZero();
    circle.m_radius = radius;
    b2Transform circleXf;
    circleXf.Set(b2Vec2(point.x, point.y), 0.0f);
    b2AABB box;
    circle.ComputeAABB(&box, circleXf, 0);
    Collect collect;
    world.World().QueryAABB(&collect, box);

    std::vector<const b2Fixture*> hits;
    std::vector<const void*> seen; // the colliders already in (a compound one's fixtures are one)
    for (b2Fixture* f : collect.fixtures) {
        const int layer = f->GetUserData() ? *static_cast<const int*>(f->GetUserData()) : 0;
        if (!(layerMask >> layer & 1)) continue;
        if (f->IsSensor() && !queriesHitTriggers) continue;
        const Collider2D* collider = world.ColliderOf(f);
        const void* key = collider ? static_cast<const void*>(collider) : static_cast<const void*>(f);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        const b2Shape* shape = f->GetShape();
        const b2Transform& xf = f->GetBody()->GetTransform();
        bool overlaps = false;
        for (int32 child = 0; child < shape->GetChildCount() && !overlaps; child++) overlaps = b2TestOverlap(&circle, 0, shape, child, circleXf, xf);
        if (!overlaps) continue;
        seen.push_back(key);
        hits.push_back(f);
    }
    return hits;
}

} // namespace unity
