#pragma once
// Enemy.cs, EnemyBehaviour.cs and EnemyAttackCollisionDetection.cs: what every common enemy
// is and does (BabySlimeBehaviour, game/BabySlime.h, and GolemSoldierBehaviour,
// game/GolemSoldier.h, are ported on top; game/EnemyFactory.h builds one from its prefab).
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
//   - each Animator's Update (Enemy::UpdateAnimator): its clip events go to the behaviour
//     (EnemyBehaviour::OnAnimationEvent, by the event's function name, as Unity's SendMessage),
//     and the curves it drives onto the object (the "AttackCollider" child's box and transform,
//     the root's box: game/EnemyColliders.h);
//   - PolyNav2D::LateUpdate and each agent's LateUpdate (the move).
// The clocks: scene.calls (the frame's Time.time, the fixed step's, and which one an Invoke made
// now counts from: unity/DelayedCalls.h); the driver also runs its pass after each fixed step.
// DOTween: scene.tweens, updated with deltaTime and DOTween's own unscaled delta (the death's
// fade ignores the time scale).

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "game/EnemyStats.h"
#include "game/HeroDamage.h"
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
class EnemyProjectiles;
class MasterAudio;
struct HeroTrap; // (game/HeroTrap.h: the Slime's)

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
    // HeroMerchantController.VibrateForTime(intensity, duration) (the pad's rumble).
    std::function<void(float intensity, float duration)> vibrate;
    // HeroMerchant.DealDamageToPlayer(attackStrength, damageSource (Enemy.name), DamageType.Normal, enemy).
    std::function<void(float attackStrength, const std::string& damageSource, Enemy& enemy)> dealDamage;
    // HeroMerchantController.AddForce(force) (an Impulse on his body: EnemyBehaviour.PushPlayerBackwards).
    std::function<void(unity::Vector2 force)> addForce;
    // DealDamageToPlayer with its DamageType (ItemDrain: the HermitSlime's; game/HeroDamage.h's
    // DealDamageToPlayer calls this, else dealDamage).
    std::function<void(float attackStrength, const std::string& damageSource, HeroDamageType type, Enemy& enemy)> dealTypedDamage;
    // The Slime's trap: his controller's isTrapped, his stick, the calls it makes on him
    // (game/HeroTrap.h); none: no slime traps him.
    HeroTrap* trap = nullptr;
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
    // MasterAudio (game/MasterAudio.h): PlaySound's Random draws (none: no draws).
    MasterAudio* audio = nullptr;
    // MasterAudio.PlaySound(name) for presentation (the draws are `audio`'s).
    std::function<void(Enemy&, const std::string& sound)> playSound;
    // CameraManager.Rumble(duration, strength, direction, vibration): the camera's shake. The
    // Random draws its DOShakePosition makes are the caller's (unity::ShakeDraws).
    std::function<void(float duration, float strength, unity::Vector2 direction, int vibration)> rumble;
    // Each clip event the Animator fires, before (after = false) and after the behaviour's
    // method has run (for checks; the behaviour gets it either way).
    std::function<void(Enemy&, const unity::AnimationEvent&, bool after)> onAnimationEvent;
    // The room's enemy projectiles and DungeonManager's PoolManager (game/EnemyProjectile.h:
    // the turrets' shots); none: the enemies that shoot don't.
    EnemyProjectiles* projectiles = nullptr;
    // The DungeonRoom: transform.position, roomFloorWidth / roomFloorHeight, roomCenterYOffset
    // (where a patrol picks its tiles: HermitSlimeBehaviourNEW.ChooseNewDestination).
    unity::Vector2 roomPosition;
    int roomFloorWidth = 484, roomFloorHeight = 268, roomCenterYOffset = 0;
};

// A child hurtbox: a trigger BoxCollider2D with an EnemyDamageReceiver, tagged
// "EnemyDamageReceiver" (the HermitSlime's "Collidere"). The weapon's hits on it go to the
// enemy it advises without the push (game/EnemyColliders.h: WeaponMainAttackHit). Its box is a
// sensor on the root's body (the child has no Rigidbody2D), at its local position plus offset.
struct EnemyDamageReceiver {
    std::string path;        // below the root ("Collidere")
    Enemy* advise = nullptr; // gameObjectToAdvise's enemy (none: the hits go nowhere)
    bool active = true, enabled = true, trigger = true;
    unity::Vector2 position, offset, size{1.0f, 1.0f}; // the child's local position, the box
    int layer = kLayerGroundEnemy;
    std::string tag = "EnemyDamageReceiver";
    b2Fixture* fixture = nullptr; // once EnemyPhysics::Add made it (none while off)
    // EnemyDamageReceiver.DealDamageToEnemy(damage, type): Enemy.DealDamageToEnemy on the advised
    // enemy (its damageReceiver only places the floating number). Returns the damage dealt.
    float DealDamageToEnemy(float damage, EnemyDamageReceivedType type = EnemyDamageReceivedType::Normal) const;
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
    // The attack trigger: the child's BoxCollider2D (enabled, offset, size) and the child
    // itself (active, local position), all of which an attack clip can animate (the
    // GolemSoldier's swing turns the box on and moves and grows it). A box that's off (or on
    // an inactive child) has no fixture; one that changes gets a new fixture as the Animator
    // writes it (EnemyPhysics::SyncAttackCollider), which restarts the body's rest
    // (enemy_GolemSoldier.jsonl: a sleeping soldier wakes the frame its box comes on, and one
    // still swinging sleeps 0.5 s after the box last changed, not after it went off). Turned on
    // again, it enters what it touches again; remade with a new shape, it doesn't (only the
    // first touch of a growing box reports: enemy_GolemSoldier.jsonl).
    bool hasAttackCollider = false;
    bool attackEnabled = true, attackActive = true;
    unity::Vector2 attackOffset, attackSize{1.0f, 1.0f};
    unity::Vector2 attackPosition; // the child's local position
    int attackLayer = kLayerEnemyAttack;
    bool hasRigidbody = true;
    unity::Rigidbody2DDef rigidbody;
    unity::Rigidbody2D* body = nullptr; // once EnemyPhysics::Add made it
    b2Fixture* attackFixture = nullptr;
    EnemyPhysics* physics = nullptr;
    // The root box as a trigger, its offset and size driven by the clips, and the child
    // hurtboxes (game/EnemyColliders.cpp; the HermitSlime's). A trigger root is a sensor with no
    // mass: the body's centre of mass is its origin. A clip that changes the box remakes its
    // fixture as the Animator writes it (the body's rest restarts, as the attack trigger's).
    bool colliderTrigger = false;
    std::vector<EnemyDamageReceiver> damageReceivers;
    b2Fixture* rootFixture = nullptr; // the root box's (EnemyPhysics::Add)
    // The root box's Collider2D.enabled (the FlyingKamikazeGolem's is off in its prefab and its
    // script switches it): off, the box has no fixture (EnemyPhysics::SetRootColliderEnabled,
    // game/EnemyRootCollider.cpp); rootUserData keeps its layer slot meanwhile.
    bool colliderEnabled = true;
    void* rootUserData = nullptr;
    // MasterAudio's EventSounds on its objects (the FlyingKamikazeGolem's "SoundsFlyingKamikaze"):
    // what their OnEnable plays (useEnableSound's PlaySound events), each a MasterAudio.PlaySound
    // with its Random draws, at Instantiate (if active) and SetActive(true), after the root's
    // own components. game/EnemyEventSounds.cpp.
    std::vector<std::string> enableSounds;
    void PlayEnableSounds();

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

    // The Animator's pass for the frame (if it's on): its events to the behaviour, then what
    // it drove onto the object (the attack trigger) and the sprite it shows.
    void UpdateAnimator(float deltaTime);
    // A new Animator in its default state (gameObject.SetActive(true) rebinds it): the
    // animated properties' defaults are the object's values now (call it once the prefab's are
    // in: MakeEnemy does), its events go to the behaviour.
    void ResetAnimator();

    // Object.Destroy (now): OnDestroy (the dead sprite and the drops if it died), the
    // behaviour's OnEnemyDestroyed; the body and agent leave; its calls are dropped.
    void Destroy();
    // DungeonRoom.EnemyDefeated's transform.SetParent(parent.parent): out of its spawn point,
    // keeping its world position.
    void ReparentToContainer();

    const unity::AnimatorController* Controller() const { return controller_; }

private:
    friend class EnemyPhysics;
    void ApplyAnimatedProperties();
    // The root box's animated properties (game/EnemyColliders.cpp): bound with the Animator's
    // defaults, applied after its pass.
    void BindRootBox();
    void ApplyRootBox();
    int rootProps_[4] = {-1, -1, -1, -1}; // the root's m_Offset.x/y, m_Size.x/y (-1: not animated)
    unity::Vector2 rootBuiltOffset_, rootBuiltSize_; // the root fixture as last built
    const unity::AnimatorController* controller_ = nullptr;
    // The controller's properties for the attack trigger (-1: no clip animates it): m_IsActive,
    // m_Enabled, m_Offset.x/y, m_Size.x/y, m_LocalPosition.x/y.
    int attackProps_[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    // The attack fixture as EnemyPhysics last built it (none: built = false).
    bool attackBuilt_ = false;
    unity::Vector2 attackBuiltCenter_, attackBuiltSize_;
    unity::Vector2 synced_; // the transform as the body last had it (what the physics wrote or took)
    // The transform was set by something that dirties it for the physics even where its world
    // position reads the same (SetParent, localPosition): synced to the body at the next step.
    bool transformDirty_ = false;
    friend class EnemyBehaviour;
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
    // MonoBehaviour.OnDisable: the object switched off (Enemy::SetActive(false)) or destroyed
    // while on, with the behaviour enabled (the Slime's releases the hero).
    virtual void OnDisable() {}
    // CameraManager.Rumble with the enemy attacks' shake (a random direction: Random.insideUnitSphere,
    // then DOShakePosition's draws).
    void DoScreenShakeFromEnemyAnimation();

    // An Animator clip event: the method its function names, as SendMessage finds it on the
    // object (the behaviour's own first, then EnemyBehaviour's). False: no such method here
    // (Unity logs "has no receiver").
    virtual bool OnAnimationEvent(const unity::AnimationEvent& e);

    // EnemyStats.OnDeath's handler: off the room's OnRoomEnter, then OnDeath.
    void EnemyStats_OnDeath();

protected:
    virtual void OnDeath();
    unity::Vector2 HeroPosition() const;
    void SetAnimatorFloat(const char* name, float value);
    void SetAnimatorTrigger(const char* name, bool set);

private:
    void SetEnemyShaderColorToRed();
    void SetEnemyShaderColorToWhite();
    void SetEnemyShaderToDefault();
    void SetPlayingDamagedEffectToFalse(EnemyDamageReceivedType type); // the coroutine past its wait
    unity::DelayedCalls& Calls() const { return *enemy.scene.calls; }
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

    // (Two constructors, not a defaulted Options argument: GCC wants Options' member
    // initializers parsed first.)
    explicit EnemyPhysics(unity::PhysicsWorld& world) : EnemyPhysics(world, Options{}) {}
    EnemyPhysics(unity::PhysicsWorld& world, const Options& options);
    ~EnemyPhysics();
    EnemyPhysics(const EnemyPhysics&) = delete;
    EnemyPhysics& operator=(const EnemyPhysics&) = delete;

    // The enemy's Rigidbody2D (its def, mass from the stats' weight already in it) with its
    // root box on its layer and the attack trigger as a sensor on the same body, at its
    // transform.
    void Add(Enemy& enemy);
    // The hero's body: what touches an attack trigger from it is "the Player".
    void SetHero(const unity::Rigidbody2D* hero) { hero_ = hero; }

    // One fixed step (`dt`: Time.fixedDeltaTime): sync, the world's step, Unity's trigger pass
    // (PhysicsWorld::UpdateContacts: a trigger paired at the step's end starts in this step),
    // write-back, OnTriggerEnter2D. The hero's body is the caller's to sync first.
    void Step(float dt);
    // The step in parts, for a driver that steps the world itself: Sync before b2World::Step,
    // the rest after (UpdateContacts first).
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

    // The attack trigger as the object has it now: its fixture made (the body's rest
    // restarted), remade (what it touched doesn't enter again) or taken away.
    void SyncAttackCollider(Enemy& enemy);
    // The root box as the Animator left it: remade if its offset or size changed (the body's
    // rest restarted); and the hurtboxes' fixtures (Add). game/EnemyColliders.cpp.
    void SyncRootCollider(Enemy& enemy);
    // The hurtbox whose fixture this is (none: not one).
    EnemyDamageReceiver* ReceiverOf(const b2Fixture* fixture);
    // Collider2D.enabled on the root box (enemy.colliderEnabled): off destroys its fixture, on
    // makes it again (the body's rest restarted, as a remade attack trigger's); the mass set by
    // hand stays. game/EnemyRootCollider.cpp.
    void SetRootColliderEnabled(Enemy& enemy, bool enabled);

    // Other bodies of the world stepped with the enemies' (game/EnemyProjectile.h: the enemy
    // projectiles), called in Step: Sync after the enemies' sync, AfterWorldStep right after
    // b2World::Step (before Unity's trigger pass), WriteBack after theirs, Report after
    // ReportTriggers (their OnCollisionEnter2D).
    class Companion {
    public:
        virtual ~Companion() = default;
        virtual void Sync() {}
        virtual void AfterWorldStep() {}
        virtual void WriteBack() {}
        virtual void Report() {}
    };
    void AddCompanion(Companion* companion) { companions_.push_back(companion); }

    // The attack triggers that began touching in the last step, in Box2D's order (for checks).
    struct Hit {
        Enemy* enemy = nullptr;
        bool player = false;
        bool otherTrigger = false; // what it touched is a trigger itself (a breakable's)
    };
    const std::vector<Hit>& LastHits() const { return hits_; }

private:
    class Listener;
    void AddChildColliders(Enemy& enemy); // a trigger root, the hurtboxes (game/EnemyColliders.cpp)
    unity::PhysicsWorld& world_;
    Options options_;
    Listener* listener_ = nullptr;
    std::vector<Enemy*> enemies_;
    const unity::Rigidbody2D* hero_ = nullptr;
    std::vector<Hit> hits_;
    // What remade attack fixtures were touching: no new enter with these in the next step.
    std::vector<std::pair<const b2Fixture*, const b2Fixture*>> carried_;
    std::vector<Companion*> companions_;
};

} // namespace game
