#pragma once
// GolemButlerBehaviour.cs and GolemButlerProjectile.cs: the Golem dungeon's butler, an
// EnemyBehaviour (game/Enemy.h) behind a shield that turns to face the hero, and shoots at him
// from afar. It has no Random of its own (its sounds draw: MasterAudio).
//
// Start (the frame after it's made, before targeting; MonoBehaviour.Start: the driver calls it):
// ForbidEnemyMovement (canMove off: only a hit's damaged effect turns it back on, through its
// coroutine's AllowEnemyMovement), LookToPosition (DistanceX / DistanceY), and
// InvokeRepeating("LookToPlayer", 0.1, 2.5): a method the class doesn't have (Unity logs it
// each time and calls nothing): not ported.
// Init: EnemyBehaviour's, the pool of its shots (game/PoolManager.h)
// `enemy.name + "_" + projectilePrefab.name` filled with 2 more, previousEnemyHealth.
//
// While it targets the hero, every frame (ChooseAndApplyAction), unless Stunned:
//   - within 150 of him (Vector2.Distance between the transforms): if he's in its field of
//     vision (IsPlayerInFieldOfVision) it moves (MoveEnemy, EnemyBehaviour's: with canMove off,
//     only ResetTrigger("Attacking")) and stops waiting; else, unless it's waiting already, it
//     waits (isWaiting, and LookToPosition re-Invoked 2.5 s on) and stops (StopEnemy);
//   - farther: TryShootAttack: if a shot is ready (shotEnabled), it isn't any more,
//     LookToPosition, the "ShootAttack" trigger, Invoke("RecoverShot", 1.6);
//   - then invincible unless he's behind it (IsPlayerInBack), every frame.
// Stunned (DisarmEnemy, which nothing calls): within 80 of him it rearms (RearmEnemy).
//
// The field of vision and the back are where its Animator's state faces: WalkUp up, WalkDown
// down, WalkRight right, WalkLeft left (GetCurrentAnimatorStateInfo(0).IsName; the
// "Attack*" and "AttackDefense*" names it also tries are no state of the controller); in any
// other state (ShootAttack*, NotDamaged*, Reactivation*, ...) it faces nowhere: Vector3.Angle
// with a zero vector is 0, so he's in its field of vision and behind it. In its field of vision:
// |Angle(facing, hero - it)| < 60; behind it: Angle(facing, it - hero) < 55.
//
// The Animator (animators/GolemButler.json): four idle walks (0.8 s) whose "ShootAttack"
// trigger takes them to the shot of their direction (0.75 s; but WalkDown's transition wants
// "Attacking", which nothing sets: facing down, the trigger waits, set, until a turn to another
// side takes it), whose clip fires ShootAttack at 0.333 and PlayShootSound at 0.5 and goes back
// to the walk. "Damaged" and "Deactivation" are never set, "NotDamagedWhenHit" only by a hit
// that leaves the health as it was (no damage while not invincible).
//
// ShootAttack (the clip event): a shot from the pool, parented to the room at
// GetProyectileSpawnPosition() (EnemyBehaviour's: the spawn position the angle of (distanceX,
// distanceY) picks, plus transform.position), aimed at the hero (his position minus that,
// a Vector3: his z, taken as 0), its owner the butler; its StartMove.
// The shot (GolemButlerProjectile): StartMove makes its box solid, DOMoves it by
// direction.normalized * 550 over 2 s, linear (scaled time; DOMove's Vector3 operators, each
// rounded), turns its PixelRotation to (int)GetAngleFromVector(right, direction) + 90;
// OnComplete DestroyNow. OnCollisionEnter2D with a LevelBoundary, Rock, Player or Shield: the
// owner's AttackHit (its GetComponent on a destroyed owner throws: then nothing more), the
// "Crash" trigger, the tween paused, the box a trigger, Invoke("DestroyNow", 0.3). DestroyNow:
// the tween killed, back to the pool. The butler doesn't keep its shots: they fly on when it
// dies or the hero leaves.
//
// AttackHit (its AttackCollider's trigger and its shots): EnemyBehaviour's (the damage on the
// hero), then the impact sound unless it's the hero. A hit that hurts it moves it (EnemyWasHit:
// MoveEnemy, by Time.deltaTime: the fixed step's in a physics callback). Its death counts in
// HeroStatistics (RegisterGolemButlerDeath: registerDeath).

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "game/Enemy.h"
#include "game/EnemyProjectile.h"
#include "game/Prefabs.h"
#include "unity/Vector2.h"

namespace game {

class GolemButlerProjectile;

class GolemButlerBehaviour : public EnemyBehaviour {
public:
    explicit GolemButlerBehaviour(Enemy& enemy);
    ~GolemButlerBehaviour() override;

    // Serialized.
    std::string projectilePrefab = "GolemButlerProjectile"; // its name
    std::vector<unity::Vector2> spawnProyectilePositions;   // EnemyBehaviour's

    // Private in C# (public here for replays and checks).
    float previousEnemyHealth = 0.0f;
    float fieldOfVisionAngle = 60.0f;
    float distanceToRearm = 80.0f;
    bool isWaiting = false;
    float waitWhileWillIsNearTime = 2.5f;
    bool shotEnabled = true;
    float rangeAttackMinDistance = 150.0f;
    float shootAttackRecoverTime = 1.6f;

    // Time.fixedDeltaTime (what Time.deltaTime reads inside a physics callback: a weapon hit's
    // EnemyWasHit -> MoveEnemy).
    float fixedDeltaTime = 0.01f;
    // HeroStatistics.RegisterGolemButlerDeath.
    std::function<void()> registerDeath;

    std::string ProjectilePooledName() const { return enemy.name + "_" + projectilePrefab; }

    // MonoBehaviour.Start.
    void Start();
    void Init() override;
    void Update(float deltaTime) override;
    void ChooseAndApplyAction(float deltaTime) override;
    // EnemyBehaviour's (DistanceX / DistanceY), after the beforeLook hook.
    void LookToPosition() override;
    void TryShootAttack();
    void ShootAttack();
    void RecoverShot();
    void EnemyWasHit(float newHealthValue) override;
    void HealthNotChangedWhenHit(float newHealth) override;
    void PlayDamagedSound() {}
    void PlayNoDamagedSound();
    void PlayShootSound();
    void PlayShotImpactSound();
    bool IsPlayerInFieldOfVision() const;
    bool IsPlayerInBack() const;
    void DisarmEnemy();
    void RearmEnemy();
    void PlayDeathSound() override {}
    // AttackHit(other): EnemyBehaviour's, then the impact sound unless `otherIsPlayer`.
    void AttackHit(bool otherIsPlayer) override;
    // EnemyBehaviour.SetEnemyMass: the Rigidbody2D's mass and the agent's.
    void SetEnemyMass(float newMass);
    // EnemyBehaviour.GetProyectileSpawnPosition().
    unity::Vector2 GetProyectileSpawnPosition() const;
    bool OnAnimationEvent(const unity::AnimationEvent& e) override;

    // The way its Animator's state faces (up, down, right, left; zero for none), as both checks
    // pick it.
    unity::Vector2 Facing() const;

    // For checks (not the game's): called as each LookToPosition and each ShootAttack starts (a
    // replay puts the hero where the game had him then), and with each shot taken; each vision
    // check's and back check's result, in order.
    std::function<void(GolemButlerBehaviour&)> beforeLook, beforeShootAttack;
    std::function<void(GolemButlerBehaviour&, GolemButlerProjectile*)> onShot;
    mutable std::vector<bool> visionChecks, backChecks;

protected:
    void OnDeath() override;

private:
    void PlaySound(const char* sound);
    float lastDeltaTime_ = 0.0f;
};

class GolemButlerProjectile : public EnemyProjectile {
public:
    GolemButlerProjectile(EnemyProjectiles& system, const Prefab& prefab, const unity::AnimatorController* controller);

    unity::Vector2 projectileDirection; // a Vector3 with the hero's z (taken as 0)
    float timeToDestroyAfterCrash = 0.3f; // serialized
    Enemy* projectileOwner = nullptr;
    int flyingMove = 0;                   // the DOMove (Tweens handle; 0: none)
    // rotation.Angle (PixelRotation: presentation): (int)GetAngleFromVector(right, direction) + 90.
    int rotationAngle = 0;
    // onShouldBeDestroyed (the pool's ReturnObject); none: Object.Destroy (not ported).
    std::function<void(EnemyProjectile&)> onShouldBeDestroyed;

    void StartMove();
    void OnCollisionEnter2D(const Collision2D& other) override;
    void DestroyNow();

    // For checks: each DestroyNow, and each crash.
    std::function<void(GolemButlerProjectile&)> onDestroyNow;
    bool crashed = false;

private:
    GolemButlerBehaviour* Owner() const;
};

// Vector2.Angle(from, to) as Unity 2019.2's managed code has it (and Vector3.Angle of z = 0
// vectors: the same expressions).
float GolemButlerAngle(unity::Vector2 from, unity::Vector2 to);
// Extensions.GetAngleFromVector(me, other).
float GolemButlerAngleFromVector(unity::Vector2 me, unity::Vector2 other);

// The behaviour of a prefab whose root has one (MakeEnemy's), with its serialized fields
// (prefabs.json's GolemButlerBehaviour row).
std::unique_ptr<GolemButlerBehaviour> MakeGolemButlerBehaviour(Enemy& enemy, const Behaviour& fields);
std::unique_ptr<EnemyProjectile> MakeGolemButlerProjectile(EnemyProjectiles& system, const Prefab& prefab,
                                                          const unity::AnimatorController* controller, const Behaviour& fields);

} // namespace game
