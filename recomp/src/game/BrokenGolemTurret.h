#pragma once
// BrokenGolemTurretBehaviour.cs and BrokenGolemTurretProjectile.cs: the Golem dungeon's broken
// turret, an EnemyBehaviour (game/Enemy.h) that stands still and shoots along the four axes.
//
// Init: EnemyBehaviour's, then the pool of its shots (game/PoolManager.h)
// `enemy.name + "_" + projectilePrefab.name` filled with 6 more, and its push on the hero 50.
//
// While it targets the hero, every frame (ChooseAndApplyAction): with patrolLapse used up, it
// looks (LookToPosition, unless it's Attacking), attacks (AttackPlayer: Attacking, and
// Invoke("ShootAnimation", shootDelay), the prefab's 0) and draws the next lapse,
// Random.Range(0.001, 0.1); else the lapse runs down by deltaTime.
//
// LookToPosition: the axis towards the hero's feet (his transform + playerOffset, the larger of
// |dx| and |dy|, NormalizeOneDimensional) first, then on round right, up, left, down: a
// Physics2D.Raycast from it along each (2.4 tiles, against RisenObstacle and LevelBoundary)
// until one meets nothing (canShoot); none free: canShoot off, the first axis kept. The axis is
// its direction (enemyDirection) and the Animator's Direction* bools.
//
// The Animator (animators/BrokenGolemTurret.json, an override of GolemTurret, applyRootMotion
// on: its clips move nothing): "Shoot" takes an idle state (Walk*, a one-frame look) to the
// shot of its direction, whose clip fires SetAttackState at 0, ShootProjectile and
// PlayShootSound at 0.583, 1.167 and 1.75, and QuitAttackState at 2.25 (2.167 right); the
// clip's end goes back to the idle, and "Shoot" with another direction's bool switches shots.
// ShootProjectile (if canShoot): a shot from the pool, parented to the room at
// GetProyectileSpawnPosition() (its transform: the prefab lists no spawn positions), scaled as
// the turret, its direction the turret's; its StartMove.
//
// The shot (BrokenGolemTurretProjectile): StartMove makes its box solid, turns its art and
// DOMoves it 500 along x (550 along y) over 3 s, linear (scaled time); OnComplete DestroyNow.
// OnCollisionEnter2D with a LevelBoundary, Rock, Player or Shield: the owner's AttackHit (the
// impact sound unless the hero; then for the hero or a shield the push (PushPlayerBackwards)
// and for the hero the damage if the turret is alive) and ProjectileDestroyed, the "Crash"
// trigger, the tween paused, the box a trigger, Invoke("DestroyNow", 0.3). DestroyNow:
// ProjectileDestroyed, the tween killed, back to the pool.
//
// Its death counts in HeroStatistics (RegisterGolemTurretDeath: registerDeath); destroyed or
// left behind (OnPlayerLeaveRoom), its shots in flight go back to the pool (DestroyProjectiles,
// which skips every other one as the list shrinks under it, as the game's does).

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "game/Enemy.h"
#include "game/EnemyProjectile.h"
#include "game/Prefabs.h"
#include "unity/Physics2D.h"
#include "unity/Vector2.h"

namespace game {

class BrokenGolemTurretProjectile;

// HeroMerchantController.Direction.
enum class HeroDirection { Left = 0, Right = 1, Back = 2, Front = 3 };

class BrokenGolemTurretBehaviour : public EnemyBehaviour {
public:
    explicit BrokenGolemTurretBehaviour(Enemy& enemy);
    ~BrokenGolemTurretBehaviour() override;

    // Serialized.
    std::string projectilePrefab = "BrokenGolemTurretProjectile"; // its name
    float shootDelay = 0.0f;
    HeroDirection enemyDirection = HeroDirection::Left;
    std::vector<unity::Vector2> spawnProyectilePositions; // EnemyBehaviour's
    // HeroStatistics.RegisterGolemTurretDeath.
    std::function<void()> registerDeath;

    // State (private in C#).
    float attackThreshold = 5.0f;
    float patrolLapse = 0.0f;
    bool canShoot = false;
    std::vector<BrokenGolemTurretProjectile*> projectilesShot; // _projectilesShot
    static const unity::Vector2 kCheckDirections[4];           // right, up, left, down

    std::string ProjectilePooledName() const { return enemy.name + "_" + projectilePrefab; }

    void Init() override;
    void ChooseAndApplyAction(float deltaTime) override;
    void LookToPosition() override;
    void AttackPlayer() override;
    // AttackHit(other): what a shot hit, by its tag ("Player", "Shield", ...).
    void AttackHitObject(const std::string& otherTag);
    void AttackHit(bool otherIsPlayer) override { AttackHitObject(otherIsPlayer ? "Player" : ""); }
    void SetAttackState();
    void QuitAttackState();
    void ShootAnimation();
    void ShootProjectile();
    void ProjectileDestroyed(BrokenGolemTurretProjectile* projectile);
    void PlayChargeSound();
    void PlayShootSound();
    void PlayShotImpactSound();
    void PlayDeathSound() override {}
    void OnEnemyDestroyed() override;
    void OnPlayerLeaveRoom() override;
    void DestroyProjectiles();
    // EnemyBehaviour.GetProyectileSpawnPosition(): transform.position plus the spawn position
    // the angle of (distanceX, distanceY) picks (none listed: transform.position).
    unity::Vector2 GetProyectileSpawnPosition() const;
    bool OnAnimationEvent(const unity::AnimationEvent& e) override;

    // What the port did, for checks (not the game's): each raycast of the looks, in order, and
    // the shots taken (the pool's objects).
    struct Ray {
        unity::Vector2 origin, direction;
        float distance = 0.0f;
        int layerMask = 0;
        unity::RaycastHit2D hit;
    };
    std::vector<Ray> rays;
    // Called as a look starts (a replay puts the hero where the game had him then), as
    // ShootAnimation runs, as a shot is taken.
    std::function<void(BrokenGolemTurretBehaviour&)> beforeLook;
    std::function<void(BrokenGolemTurretBehaviour&)> onShootAnimation;
    std::function<void(BrokenGolemTurretBehaviour&, BrokenGolemTurretProjectile*)> onShot;

    // LayerMask: RisenObstacle | LevelBoundary.
    static int LookLayerMask();

protected:
    void OnDeath() override;

private:
    void PlaySound(const char* sound);
};

class BrokenGolemTurretProjectile : public EnemyProjectile {
public:
    BrokenGolemTurretProjectile(EnemyProjectiles& system, const Prefab& prefab, const unity::AnimatorController* controller);

    HeroDirection projectileDirection = HeroDirection::Left;
    float timeToDestroyAfterCrash = 0.3f; // serialized
    Enemy* projectileOwner = nullptr;
    int flyingMove = 0;                   // the DOMove (Tweens handle; 0: none)
    // onShouldBeDestroyed (the pool's ReturnObject); none: Object.Destroy (not ported).
    std::function<void(EnemyProjectile&)> onShouldBeDestroyed;

    void StartMove();
    void OnCollisionEnter2D(const Collision2D& other) override;
    void DestroyNow();

    // For checks: each DestroyNow, and each crash.
    std::function<void(BrokenGolemTurretProjectile&)> onDestroyNow;
    bool crashed = false;

private:
    BrokenGolemTurretBehaviour* Owner() const;
};

// The behaviour of a prefab whose root has one (MakeEnemy's), with its serialized fields
// (prefabs.json's BrokenGolemTurretBehaviour row).
std::unique_ptr<BrokenGolemTurretBehaviour> MakeBrokenGolemTurretBehaviour(Enemy& enemy, const Behaviour& fields);
std::unique_ptr<EnemyProjectile> MakeBrokenGolemTurretProjectile(EnemyProjectiles& system, const Prefab& prefab,
                                                                const unity::AnimatorController* controller, const Behaviour& fields);

} // namespace game
