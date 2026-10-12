#pragma once
// HermitSlimeBehaviourNEW.cs: the Golem dungeon's hermit slime, a slime under a golem's
// gauntlet. An EnemyBehaviour (game/Enemy.h) that patrols the room at random and slams the
// gauntlet down when the hero comes within 90 of it. While it targets the hero, every frame
// (ChooseAndApplyAction), unless it's attacking:
//   - within 90 of him (Vector2.Distance between the transforms): it faces him
//     (LookToPosition), stops (StopEnemy: canMove off, the agent stopped), resets its patrol
//     (patrolLapse 0) and attacks (AttackPlayer: Attacking, the Animator's trigger);
//   - else, if it can move: MoveEnemy, the patrol: while patrolLapse is above 0 it counts down
//     by deltaTime; then patrolLapse = Random.Range(2, 6) (an int: 2 to 5 s) and a new
//     destination (ChooseNewDestination: Random.Range(0, roomFloorWidth), then
//     Random.Range(0, roomFloorHeight), from the room floor's bottom-left corner); then it
//     faces where it's going (LookToDestination: DistanceX or DistanceY, the larger).
//
// Its Animator (animators/HermitSlime.json): four walk loops (1.167 s; each fires SetInvencible
// at its start: in its shell it takes no damage), and four attack states the trigger takes
// them to (4.333 s). An attack fires SetInvencible (0), LockLookDirection (0.417: resets the
// trigger), SetVencible, DoScreenShakeFromEnemyAnimation, PlayCrashSound and
// SpawnAttackParticles (0.583: the slam; AttackDown has the shake before SetVencible),
// SetInvencible (3.917), EndAttack (4.25: Idle, canMove back, "DoAttack" set false: no such
// parameter); at its end it goes back to the walk of its direction. Its curves drive the
// AttackCollider (on from 0.583 to 0.75, 32 x 64 or 64 x 32, moved where the gauntlet lands)
// and the root's box (a trigger: it grows as the slime comes out, up to 65, then shrinks back
// to 15 x 15 by 3.4 s). The four HermitSlimeAttackState StateMachineBehaviours set
// "AttackCycleOffset", a parameter the controller doesn't have: nothing.
//
// Its root box is a trigger, so nothing pushes it about; the weapon hits it there (tag
// SoftEnemy) or through its "Collidere" child (an EnemyDamageReceiver: game/EnemyColliders.h);
// with a weight of 48, Weapon.PushEnemyBackwards leaves it alone either way. Its attack
// trigger's hit on the hero is DealDamageToPlayer with DamageType.ItemDrain (he drops three
// random items: game/HeroDamage.h).
//
// Its death counts in HeroStatistics (RegisterGolemHermitDeath: registerDeath).

#include <functional>
#include <memory>

#include "game/Enemy.h"
#include "game/Prefabs.h"
#include "unity/Vector2.h"

namespace game {

class HermitSlimeBehaviourNEW : public EnemyBehaviour {
public:
    explicit HermitSlimeBehaviourNEW(Enemy& enemy) : EnemyBehaviour(enemy) {}

    // Serialized.
    bool iDirectionLocked = false;
    bool hasAttackParticles = true; // attackParticles set (its ParticleSystem)

    // Private in C# (public here for replays and checks).
    unity::Vector2 destinyPosition; // a Vector3 with z 0
    float patrolLapse = -1.0f;
    unity::Vector2 roomGridOrigin, roomGridSize;
    unity::Vector2 tileSize{24.0f, 24.0f}; // (unused)
    float attackDistance = 90.0f;

    // HeroStatistics.RegisterGolemHermitDeath.
    std::function<void()> registerDeath;
    // attackParticles.Play() (presentation; its particles don't draw from UnityEngine.Random).
    std::function<void(Enemy&)> playAttackParticles;
    // What the last ChooseNewDestination did (for checks; not the game's).
    int destinations = 0;

    void EnemyRoom_OnRoomEnter() override;
    void ChooseAndApplyAction(float deltaTime) override;
    void MoveEnemy(float deltaTime) override;
    void StopEnemy() override;
    void LockLookDirection();
    void UnLockLookDirection() { iDirectionLocked = false; }
    void EndAttack();
    // AttackHit(other): on the hero (not shield-protected, not dead), DealDamageToPlayer with
    // ItemDrain if it's alive (effectOnAttack: none on the prefab).
    void AttackHit(bool otherIsPlayer) override;
    // AttackHit(other) on something tagged "Shield" (the hero's shield): PushPlayerBackwards(
    // attackPushBackwardsForce). The port's attack trigger reports the hero or not, so nothing
    // calls this yet.
    void AttackHitShield();
    void SpawnAttackParticles();
    // MasterAudio.PlaySound("golem_dungeon_golem_crash") and the pad's rumble
    // (defaultStrongEnemyVibrationIntensity, defaultStrongEnemyVibrationDuration).
    void PlayCrashSound();
    bool OnAnimationEvent(const unity::AnimationEvent& e) override;

protected:
    void OnDeath() override;

private:
    void ChooseNewDestination();
    void LookToDestination();
};

// The HermitSlimeBehaviourNEW of a prefab whose root has one (MakeEnemy's), with its
// serialized fields (`fields`: prefabs.json's HermitSlimeBehaviourNEW row).
std::unique_ptr<HermitSlimeBehaviourNEW> MakeHermitSlimeBehaviour(Enemy& enemy, const Behaviour& fields);

} // namespace game
