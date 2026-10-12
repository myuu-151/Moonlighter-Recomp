#pragma once
// An enemy's colliders past the solid root box and the attack trigger (game/Enemy.h):
//   - a root BoxCollider2D that's a trigger and that clips drive (offset and size: the
//     HermitSlime's grows as it comes out of its shell). Unity remakes the shape as the
//     Animator writes it; the port remakes the fixture when the box changed (the body's rest
//     restarts, as with the attack trigger: enemy_GolemSoldier.jsonl). A trigger has no mass:
//     the body keeps the mass set by hand at its origin.
//   - child hurtboxes: EnemyDamageReceiver (game/Enemy.h), trigger boxes on the root's body.
//
// The weapon's main attack on an enemy (Weapon.OnMainAttackHit, Weapon.cs:278-354), by the tag
// of what its hitbox touched:
//   - HardEnemy / SoftEnemy (the enemy's own collider): Weapon.PushEnemyBackwards (under 48 of
//     weight), the hit's visual effect, the combat effects' Random.Range(0, 1) draws (when the
//     weapon has them), then Enemy.DealDamageToEnemy;
//   - EnemyDamageReceiver: EnemyDamageReceiver.DealDamageToEnemy on the enemy it advises: no
//     push, no visual effect, no combat effects, no pad rumble.
// Both then shake the camera (DoWeaponMainAttackScreenShake, or the last combo hit's) and put
// on the hit-stop (GameManager.SetGameSpeedForTime(hitToEnemyEffectTimeScale,
// hitToEnemyEffectDuration)): the hero's side, the caller's.

#include "game/Enemy.h"
#include "unity/Vector2.h"

namespace game {

// What WeaponMainAttackHit did.
struct WeaponHitResult {
    Enemy* enemy = nullptr; // whose health it went to (none: nobody's)
    bool pushed = false;    // Weapon.PushEnemyBackwards ran (the push itself needs weight under 48)
    float damage = 0.0f;    // what DealDamageToEnemy dealt
};

// The main attack on an enemy's own collider: the push (from the hero at `heroPosition` with
// `pushForce`), then DealDamageToEnemy(strength).
WeaponHitResult WeaponMainAttackHit(Enemy& enemy, float strength, unity::Vector2 heroPosition, float pushForce);
// The main attack on a hurtbox: DealDamageToEnemy(strength) through it, no push.
WeaponHitResult WeaponMainAttackHit(const EnemyDamageReceiver& receiver, float strength);

} // namespace game
