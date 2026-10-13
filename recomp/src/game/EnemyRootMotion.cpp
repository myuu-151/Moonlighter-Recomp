// EnemyPhysics::ApplyRootMotion (game/Enemy.h): an Animator with applyRootMotion on the object of
// a Dynamic Rigidbody2D (the BrokenGolemTurret's, on its root; animators.py's "applyRootMotion"),
// update mode Normal: once per Animator pass, Unity hands the clip's root motion to the body as
// its velocity (deltaPosition / deltaTime), and the body's rest starts over.
//
// The velocity: the oracle's "rootMotionProbe" (TurretRootMotionProbe.cs, after the recording:
// a turret's velocity set to (30, 0) after the frame's Updates, read at the frame's end and after
// the next fixed step). enemy_BrokenGolemTurret.jsonl of enemyfix-turret-2 (Golem_Enemies_1,
// updateMode 0): (0, 0) at the frame's end, the body awake, not moved by the next step. The
// turret's clips have no root curves, so deltaPosition is zero: the pass zeroes the velocity
// whatever set it (not kept, nor moved to by MovePosition, which would still show (30, 0) at the
// frame's end). The angular velocity isn't probed (the turret's rotation is frozen).
//
// The rest: the body never sleeps while its Animator runs (both recordings: the turrets awake at
// every fixed step's start and every frame's end, where a body at rest sleeps after 0.5 s: the
// GolemSoldier's, the FlyingKamikazeGolem's), and its transform isn't touched (the oracle's
// "tc", transform.hasChanged from the turret's Update to the frame's end, never set): woken, its
// time at rest back to zero (PhysicsWorld::RestartRest, as a synced transform does).
// Rigidbody2D.velocity = zero alone wouldn't do it (Box2D's SetLinearVelocity wakes only for a
// non-zero one: the FlyingKamikazeGolem's PrepareChargeAttack leaves it asleep, frame 56 of its
// recording).

#include <Box2D/Box2D.h>

#include "game/Enemy.h"

namespace game {

void EnemyPhysics::ApplyRootMotion(Enemy& e)
{
    if (!e.body || e.destroyed || !e.active) return;
    if (e.body->bodyType() != unity::RigidbodyType2D::Dynamic || !e.body->Body()->IsActive()) return;
    e.body->SetVelocity({0.0f, 0.0f}); // (deltaPosition / deltaTime: no root curves)
    world_.RestartRest(*e.body);
}

} // namespace game
