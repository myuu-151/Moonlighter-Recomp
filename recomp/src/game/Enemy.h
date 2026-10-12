#pragma once
// Enemy.cs, EnemyBehaviour.cs and EnemyAttackCollisionDetection.cs: what every common enemy
// is and does (BabySlimeBehaviour, game/BabySlime.h, is the first one ported on top).
//
// Enemy is the object: the Enemy component's fields, its stats (game/EnemyStats.h), its
// transform (kept by its PolyNavAgent: the local position under its parents, the spawn point
// and the room's enemy container), its Rigidbody2D and colliders, its Animator and the
// SpriteRenderer's sprite and material (_FlashAmount, _Color: the hit flash and the death's
// fade). EnemyBehaviour is the component that thinks: targeting the hero, moving through the
// PolyNav map, attacking, being hit, dying.
//
// A frame of a room's enemies, in the game's order (the driver calls each part):
//   - each fixed step (EnemyPhysics::Step): the transforms the scripts moved go to their
//     bodies, the world steps, the awake bodies' positions come back to the transforms, then
//     the attack triggers that began touching report (EnemyAttackCollisionDetection.
//     OnTriggerEnter2D -> AttackHit -> the hero's DealDamageToPlayer); a weapon hit lands here
//     too (Enemy::DealDamageToEnemy, Enemy::PushBackwards);
//   - DOTween's update (scene.tweens), then each behaviour's Update();
//   - the delayed calls (scene.calls: Invoke, WaitForSeconds, Destroy(obj, t)) and the PolyNav
//     coroutines (PolyNav2D::ResumeCoroutines: paths arrive);
//   - each Animator's Update (Enemy::UpdateAnimator);
//   - PolyNav2D::LateUpdate and each agent's LateUpdate (the move).
// Time.time / Time.fixedTime: scene.calls->now, which the driver keeps on the clock of the
// phase it's in (Invoke made inside a fixed step counts from the fixed clock).

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "game/EnemyStats.h"
#include "game/PolyNavAgent.h"
#include "unity/Animator.h"
#include "unity/DelayedCalls.h"
#include "unity/Physics2D.h"
#include "unity/PhysicsWorld.h"
#include "unity/Tween.h"
#include "unity/Vector2.h"

class b2Fixture;

namespace game {

class Enemy;
class EnemyBehaviour;
class EnemyPhysics;

enum class EnemyDamageReceivedType { Normal, Effect, Familiar, Trick };

// The layers enemies use (the project's TagManager).
constexpr int kLayerTransparentFX = 1, kLayerGroundEnemy = 12, kLayerEnemyAttack = 23;

// What an enemy sees of the hero (HeroMerchant.Instance) and does to him. The hero's own
// lane fills it; the enemies only read and call.
struct EnemyHero {
    unity::Vector2 position;  // his transform's, where the enemies' scripts read it
    bool isDead = false;      // heroMerchantStats.isDead
    // HeroMerchantController.CheckIfPlayerIsShieldProtected(enemy's object); none: false.
    std::function<bool(const Enemy&)> isShieldProtected;
    // HeroMerchant.DealDamageToPlayer(attackStrength, damageSource (Enemy.name), DamageType.Normal, enemy).
    std::function<void(float attackStrength, const std::string& damageSource, Enemy& enemy)> dealDamage;
    // HeroMerchantController.AddForce(force) (an Impulse on his body: EnemyBehaviour.PushPlayerBackwards).
    std::function<void(unity::Vector2 force)> addForce;
};

// The scene an enemy lives in: the engine's schedulers, physics, the hero and the room.
struct EnemyScene {
    unity::DelayedCalls* calls = nullptr;       // Invoke, coroutines' WaitForSeconds, Destroy(obj, t)
    unity::Tweens* tweens = nullptr;            // DOTween (the death's fade, the fall into a hole)
    const unity::Physics2D* queries = nullptr;  // Physics2D.OverlapBox (falling into holes); none: no holes
    EnemyHero* hero = nullptr;
    bool bossRoom = false;                      // enemyRoom is a DungeonBossRoom (no dead sprite)
    // DungeonRoom.EnemyDefeated(enemy) (the room's bookkeeping; the enemy's reparenting is its own).
    std::function<void(Enemy&)> onEnemyDefeated;
    // DungeonRoom.SpawnEnemyDeadSprite(sprite, position).
    std::function<void(const std::string& sprite, unity::Vector2 position)> spawnEnemyDeadSprite;
    // Enemy.DropItemsOnDeath (the item drops and the familiar egg; their Random draws are theirs).
    std::function<void(Enemy&)> dropItemsOnDeath;
    // EnemyBehaviour.SpawnDeathParticles (the sound and the particles).
    std::function<void(Enemy&)> spawnDeathParticles;
    // Enemy.OnEnemyRecivesDamage(enemy, totalDamage, type).
    std::function<void(Enemy&, float, EnemyDamageReceivedType)> onEnemyReceivesDamage;
};

class Enemy {
public:
    Enemy(EnemyScene& scene, const unity::AnimatorController* controller);
    virtual ~Enemy();
    Enemy(const Enemy&) = delete;
    Enemy& operator=(const Enemy&) = delete;

    EnemyScene& scene;
    std::string name; // Enemy.name: the enemy's key ("BabySlime"), the damage source
    EnemyStats enemyStats;
    std::unique_ptr<EnemyBehaviour> enemyBehaviour;
    bool hasNavAgent = true; // (the transform is the agent's either way)
    PolyNavAgent navAgent;
    std::unique_ptr<unity::Animator> enemyAnimator; // nullptr: none
    bool animatorEnabled = true;
    EnemyDamageReceivedType LastDamageReceived = EnemyDamageReceivedType::Normal;
    std::string enemyDeadSprite;    // EnemyDeadSprite (""; null)
    bool hasEnemyRoom = true;       // enemyRoom set
    bool inRoomEnemies = true;      // in its room's roomEnemies (EnemyDefeated reparents it)
    bool isBreakableObject = false; // a BreakableObject (Die skips the kill statistics)

    // The object.
    bool active = true;     // gameObject.activeSelf
    bool destroyed = false; // Object.Destroy has run
    int layer = kLayerGroundEnemy;
    std::string tag = "SoftEnemy";
    // SpriteRenderer: the sprite shown (the prefab's, then the Animator's), its material.
    std::string sprite;
    float flashAmount = 0.0f;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    // Transform scale (DOScale when falling into a hole) and the sprite's rotation about z.
    float scale = 1.0f, spriteRotation = 0.0f;

    // The colliders: the root's BoxCollider2D, and the "AttackCollider" child's trigger
    // (EnemyAttackCollisionDetection; layer EnemyAttack). Local to the root.
    unity::Vector2 colliderOffset, colliderSize{1.0f, 1.0f};
    bool hasAttackCollider = false;
    unity::Vector2 attackOffset, attackSize{1.0f, 1.0f};
    int attackLayer = kLayerEnemyAttack;
    bool hasRigidbody = true;
    unity::Rigidbody2DDef rigidbody;
    unity::Rigidbody2D* body = nullptr; // once EnemyPhysics::Add made it
    b2Fixture* attackFixture = nullptr;
    EnemyPhysics* physics = nullptr;

    // transform.position (through the parents) and its setter.
    unity::Vector2 position() const { return navAgent.position(); }
    void setPosition(unity::Vector2 p) { navAgent.setPosition(p); }

    // Instantiate under the spawn point (local position zero) and its parents (nearest first:
    // the "EnemySpawn" object's local position, the container's, the room's): Awake (the
    // agent's prime goal), OnEnable.
    void Instantiate(const std::vector<unity::Vector2>& parents);
    // Enemy.Init(name): the stats, the behaviour's Init.
    void Init(const std::string& enemyName, const EnemyStatModificator& stats, const EnemyStatsContext& context);
    // gameObject.SetActive: the agent leaves or joins PolyNavAgent.allAgents, the body its
    // world, the Animator starts over in its default state.
    void SetActive(bool value);

    // Enemy.DealDamageToEnemy(attackStrength, damageReceiver, type): the hit (defense,
    // invincibility, the health's setter: flash, death). Returns the damage dealt.
    float DealDamageToEnemy(float attackStrength, EnemyDamageReceivedType type = EnemyDamageReceivedType::Normal);
    virtual float CalcHitDamage(float hitStrength, float targetDefense) const;
    // Weapon.PushEnemyBackwards(enemy, attackPushForce): the knockback of the hero's hits, an
    // Impulse away from him, for enemies under 48 of weight.
    void PushBackwards(unity::Vector2 heroPosition, float attackPushForce);
    // Rigidbody2D.AddForce(force, ForceMode2D.Impulse).
    void AddImpulse(unity::Vector2 force);
    // Rigidbody2D.velocity.
    unity::Vector2 velocity() const;

    // The Animator's pass for the frame (if it's on), then the sprite it shows.
    void UpdateAnimator(float deltaTime);

    // Object.Destroy (now): OnDestroy (the dead sprite and the drops if it died), the
    // behaviour's OnEnemyDestroyed; the body and agent leave; its calls are dropped.
    void Destroy();
    // DungeonRoom.EnemyDefeated's transform.SetParent(parent.parent): out of its spawn point,
    // keeping its world position.
    void ReparentToContainer();

    const unity::AnimatorController* Controller() const { return controller_; }

private:
    friend class EnemyPhysics;
    const unity::AnimatorController* controller_ = nullptr;
    unity::Vector2 synced_; // the transform as the body last had it (what the physics wrote or took)
};

class EnemyBehaviour {
public:
    enum class EnemyState { Idle, PursuingPlayer, Attacking, RunningAway, Stunned };
    enum class CommonEnemyCulture { Golem, Forest, Desert, Tech };

    EnemyBehaviour(Enemy& enemy);
    virtual ~EnemyBehaviour() = default;
    EnemyBehaviour(const EnemyBehaviour&) = delete;
    EnemyBehaviour& operator=(const EnemyBehaviour&) = delete;

    Enemy& enemy;
    bool enabled = true; // MonoBehaviour.enabled (OnDeath turns it off)

    // Serialized fields.
    EnemyState enemyState = EnemyState::Idle;
    float attackPushBackwardsForce = 200.0f;
    bool canMove = true;
    bool fallInHoles = true;
    bool isRigidbodyStatic = false;
    bool pauseAnimationsWhenDamaged = true;
    float stopCheckingHolesDelay = 0.0f;
    float heroMerchantWidth = 16.0f;
    bool hasEffectOnAttack = false; // effectOnAttack (a MoonlighterEffect on the hero: not ported)
    std::string soundWhenHit;

    // State (protected in C#; public here for replays and checks).
    float _lapse = 0.0f;
    bool targetPlayer = false;
    bool playingDamagedEffect = false;
    unity::Vector2 playerOffset; // (0, merchantColliderYOffset), from Constants
    int raycastObstaclesLayer = 0;
    unity::Vector2 checkHolesOnColliderCenter;
    float distanceX = 0.0f, distanceY = 0.0f;
    float checkHoles = 1.0f;
    int deadForce = 100;
    bool subscribedToRoomEnter = false;
    bool hasConfusedEffect = false; // a ConfusedEffect below it (not ported: none)

    virtual void Init();
    // MonoBehaviour.Update.
    virtual void Update(float deltaTime);
    virtual void EnemyRoom_OnRoomEnter();
    virtual void StartTargetingPlayerAfterTime(float time);
    virtual void CancelPlayerTargetingAfterTime();
    virtual void StartTargetingPlayer();
    virtual void StopTargetingPlayer();
    virtual void ChooseAndApplyAction(float deltaTime);
    virtual void MoveEnemy(float deltaTime);
    void SetPlayerDestination();
    virtual void StopEnemy();
    virtual void AttackPlayer();
    virtual void LookToPosition();
    // AttackHit(other): what touched the attack trigger; `otherIsPlayer`: it's the hero
    // (tag "Player").
    virtual void AttackHit(bool otherIsPlayer);
    void PushPlayerBackwards();
    void PushPlayerBackwards(float attackPushForce);
    virtual void EnemyDashTowardsThePlayer(float dashPushForce);
    virtual void ShieldPushEnemy(unity::Vector2 direction, float dashPushForce);
    void ForbidEnemyMovement();
    void AllowEnemyMovement();
    virtual void EnemyWasHit(float newHealthValue);
    void PlayDamagedEffect();
    void SetPlayingDamagedEffectToTrue();
    bool IsEnemyPlayingDamagedEffect() const { return playingDamagedEffect; }
    void SetEnemyShaderFlashAmount(float value);
    void SetEnemyShaderColor(const float color[4]);
    virtual void SpawnDeathParticles();
    virtual void HealthDecreasedWhenHit(float newValue);
    virtual void HealthNotChangedWhenHit(float newHealth);
    virtual void SetInvencible();
    virtual void SetVencible();
    virtual void PlayDeathSound() {}
    virtual void OnEnemyDestroyed();
    virtual void OnPlayerLeaveRoom();
    virtual bool IsBoss() const { return false; }

    // EnemyStats.OnDeath's handler: off the room's OnRoomEnter, then OnDeath.
    void EnemyStats_OnDeath();

protected:
    virtual void OnDeath();

private:
    void SetEnemyShaderColorToRed();
    void SetEnemyShaderColorToWhite();
    void SetEnemyShaderToDefault();
    void SetPlayingDamagedEffectToFalse(EnemyDamageReceivedType type); // the coroutine past its wait
    unity::DelayedCalls& Calls() const { return *enemy.scene.calls; }
    unity::Vector2 HeroPosition() const;
    void SetAnimatorFloat(const char* name, float value);
    void SetAnimatorTrigger(const char* name, bool set);
};

// The enemies' bodies in a unity::PhysicsWorld, and their attack triggers. The world's
// contact listener is this one's (the hero's port doesn't use it).
class EnemyPhysics {
public:
    struct Options {
        // After a step, an awake body's position goes back to its transform even when the step
        // didn't move it (the transform's local position is then the world position through
        // its parents again, which can round differently from what the script set).
        bool writeBackUnmoved = true;
    };

    explicit EnemyPhysics(unity::PhysicsWorld& world, const Options& options = Options());
    ~EnemyPhysics();
    EnemyPhysics(const EnemyPhysics&) = delete;
    EnemyPhysics& operator=(const EnemyPhysics&) = delete;

    // The enemy's Rigidbody2D (its def, mass from the stats' weight already in it) with its
    // root box on its layer and the attack trigger as a sensor on the same body, at its
    // transform.
    void Add(Enemy& enemy);
    // The hero's body: what touches an attack trigger from it is "the Player".
    void SetHero(const unity::Rigidbody2D* hero) { hero_ = hero; }

    // One fixed step (`dt`: Time.fixedDeltaTime). The hero's body is the caller's to sync first.
    void Step(float dt);
    // The step in parts, for a driver that steps the world itself: Sync before b2World::Step,
    // the rest after.
    void Sync();
    void WriteBack();
    void ReportTriggers();

    // The body somewhere its transform isn't (a replay starting from a recorded body): there,
    // without the transform counting as moved.
    void PlaceBody(Enemy& enemy, unity::Vector2 bodyPosition);
    // gameObject.layer changed (OnDeath's TransparentFX).
    void SetLayer(Enemy& enemy, int layer);
    // The body leaves (Destroy, SetActive(false)) or comes back.
    void SetBodyActive(Enemy& enemy, bool active);

    // The attack triggers that began touching in the last step, in Box2D's order (for checks).
    struct Hit {
        Enemy* enemy = nullptr;
        bool player = false;
    };
    const std::vector<Hit>& LastHits() const { return hits_; }

private:
    class Listener;
    unity::PhysicsWorld& world_;
    Options options_;
    Listener* listener_ = nullptr;
    std::vector<Enemy*> enemies_;
    const unity::Rigidbody2D* hero_ = nullptr;
    std::vector<Hit> hits_;
};

} // namespace game
