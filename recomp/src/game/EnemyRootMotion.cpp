// EnemyPhysics::ApplyRootMotion (game/Enemy.h): an Animator with applyRootMotion on the object of
// a Dynamic Rigidbody2D (the BrokenGolemTurret's, on its root; animators.py's "applyRootMotion").
//
// Its clips have no root curves, so the root motion moves nothing; but the body never sleeps in
// the game while its Animator runs (enemy_BrokenGolemTurret.jsonl: both turrets awake at every
// fixed step's start and every frame's end of 968 frames, where a body at rest sleeps after
// 0.5 s: the GolemSoldier's, the FlyingKamikazeGolem's), and its transform isn't touched (the
// oracle's "tc", transform.hasChanged from the turret's Update to the frame's end, never set).
// So Unity applies the root motion through the body, which starts its rest over every Animator
// pass: woken, its time at rest back to zero (PhysicsWorld::RestartRest, as a synced transform
// does), its velocity kept. Rigidbody2D.velocity = zero alone wouldn't do it (Box2D's
// SetLinearVelocity wakes only for a non-zero one: the FlyingKamikazeGolem's PrepareChargeAttack
// leaves it asleep, frame 56 of its recording).
//
// Open: whether the root motion also sets the body's velocity (to deltaPosition / deltaTime, zero
// here) or moves it (MovePosition): a turret never moves in the recording. The oracle's
// "rootMotionProbe" (TurretRootMotionProbe.cs, after the recording) sets a turret's velocity in
// Update and records it at the frame's end and the body after the next fixed step;
// tests/turret_replay.h (AfterFrames) checks it against this model where the recording has it.

#include <Box2D/Box2D.h>

#include "game/Enemy.h"

namespace game {

void EnemyPhysics::ApplyRootMotion(Enemy& e)
{
    if (!e.body || e.destroyed || !e.active) return;
    if (e.body->bodyType() != unity::RigidbodyType2D::Dynamic || !e.body->Body()->IsActive()) return;
    world_.RestartRest(*e.body);
}

} // namespace game
