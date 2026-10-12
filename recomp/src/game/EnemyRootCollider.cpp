// The root box's Collider2D.enabled (game/Enemy.h: Enemy::colliderEnabled), switched by a script
// (FlyingKamikazeGolemBehaviour: off in its prefab, on for a charge, off once it's back to rest).
// Unity destroys a disabled collider's shapes and makes them anew when it's enabled again: off,
// the fixture goes (the body keeps flying, through everything); on, a new one as
// PhysicsWorld::AddBody made it (b2PolygonShape::Set of the box's corners, the default
// material's friction 0.4 and bounciness 0, a trigger as a sensor) and the body's rest starts
// over (as a remade attack trigger's: EnemyPhysics::SyncAttackCollider). Box2D recomputes the
// mass from the densities either way; the mass Unity keeps (set by hand, useAutoMass off) goes
// back after.

#include <Box2D/Box2D.h>

#include "game/Enemy.h"

namespace game {

void EnemyPhysics::SetRootColliderEnabled(Enemy& e, bool enabled)
{
    if (e.colliderEnabled == enabled) return;
    e.colliderEnabled = enabled;
    if (!e.body) return;
    b2Body* b = e.body->Body();
    b2MassData mass;
    b->GetMassData(&mass);
    if (!enabled) {
        if (!e.rootFixture) return;
        e.rootUserData = e.rootFixture->GetUserData();
        b->DestroyFixture(e.rootFixture);
        e.rootFixture = nullptr;
        e.rootBuiltOffset_ = e.colliderOffset;
        e.rootBuiltSize_ = e.colliderSize;
    } else {
        if (e.rootFixture || !e.rootUserData) return;
        const float hx = e.colliderSize.x / 2.0f, hy = e.colliderSize.y / 2.0f;
        const float x0 = e.colliderOffset.x - hx, y0 = e.colliderOffset.y - hy, x1 = e.colliderOffset.x + hx, y1 = e.colliderOffset.y + hy;
        const b2Vec2 corners[4] = {b2Vec2(x0, y0), b2Vec2(x1, y0), b2Vec2(x1, y1), b2Vec2(x0, y1)};
        b2PolygonShape shape;
        shape.Set(corners, 4);
        b2FixtureDef fd;
        fd.shape = &shape;
        fd.density = e.colliderTrigger ? 0.0f : 1.0f;
        fd.friction = 0.4f;
        fd.restitution = 0.0f;
        fd.isSensor = e.colliderTrigger;
        fd.userData = e.rootUserData;
        e.rootFixture = b->CreateFixture(&fd);
        e.rootBuiltOffset_ = e.colliderOffset;
        e.rootBuiltSize_ = e.colliderSize;
        world_.RestartRest(*e.body);
    }
    if (b->GetType() == b2_dynamicBody) b->SetMassData(&mass);
}

} // namespace game
