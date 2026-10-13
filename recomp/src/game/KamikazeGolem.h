#pragma once
// GolemKamikazeBehaviour.cs (with SoundsKamikaze.cs): the Golem dungeon's kamikaze, a small
// golem that hops at the hero and blows up. An EnemyBehaviour (game/Enemy.h) on the
// KamikazeGolem prefab (enemies_golem; its controller GolemKamikaze).
//
// When its room is entered (EnemyRoom_OnRoomEnter, while its room's enemies are still switched
// off): back at its spawn point with full health (the base's), not invincible, full health
// again, free to move, PursuingPlayer, on GroundEnemy, and InvokeRepeating("MoveEnemy", 0.65,
// 0.65): every 0.65 s from then (in the frame's or a fixed step's pass, as Invoke runs), even
// once it's dead (Invoke runs on a disabled behaviour), until it's destroyed, SelfDestroy runs
// or the room is left (OnExitRoom cancels it; OnDeath leaves that subscription, not the invoke):
//   - MoveEnemy: Vector2.Distance to the hero; if it can move, the Animator's "Attacking" is
//     reset and, farther than 80 (distanceToPlayerToStartChasing), it wanders (MoveRandomly:
//     Random.insideUnitCircle, then Random.Range(60, 90), an int: transform.position + (c.x, c.y,
//     z) * that, a PolyNav destination if PolyNav2D.current.PointIsValid takes the point).
// While it targets the hero, every frame (ChooseAndApplyAction):
//   - within the agent's stoppingDistance + 20 of him (the prefab's 0 + 20; the sum in double),
//     unless it's attacking already: a dash at him (EnemyDashTowardsThePlayer(dashForce 1000):
//     an Impulse on its body) and AttackPlayer (Attacking, the "Attacking" trigger: the
//     Animator's any-state transition to Explosion);
//   - then, if it can move: within 80 it jumps at him (JumpTowardsPlayer: canMove off, the agent
//     stopped, the end transform.position + (his - its) + (Vector3)Random.insideUnitCircle * 10,
//     the "Jump" trigger, invincible, on FlyingEnemy; EnemyBehaviour.MakeJump(end, 0, 0.3) with
//     the default Linear x ease (game/EnemyJump.h: DOMoveY to the middle height in 0.15 s
//     OutCubic, then to the end in 0.15 s InCubic; DOMoveX to the end in 0.3 s) whose callback
//     puts it back on GroundEnemy); else it faces him (LookToPosition).
//
// Its Animator (animators/KamikazeGolem.json): Movement (the walk loop) is the default state;
// Idle (asleep, the prefab's sprite) goes to Awake (AllowEnemyMovement at 0.417) on "Awake" and
// on to Movement, but nothing leads to Idle. HeroMerchant.EnteredInCombat sets "Awake" (when he
// enters a room with enemies, while they're switched off: an inactive object's Animator drops
// it). "Jump" from Idle or Movement to Jump (1.667 s: PlayKamikazeJumpSound at 0.083, the
// AttackCollider's box on from 0.417 to 1.0, PlayKamikazeExplosionSound and StopOnExplode at
// 0.583, SelfDestroy at 1.5: no CreateExplosionToDamageEnemies); "Attacking" from any state
// (itself included) to Explosion (1.083 s: ForbidEnemyMovement at 0; PlayKamikazeExplosionSound,
// StopOnExplode and VibrateOnExplosion at 0.167 (the box on from 0.167 to 0.417, growing);
// CreateExplosionToDamageEnemies at 0.25; SelfDestroy at 0.917). The sounds are its
// SoundsKamikaze component's (MasterAudio.PlaySound: its Random draws).
//   - StopOnExplode: this.DOKill() (no tween has the behaviour as target), transform.DOKill()
//     (the jump's moves, the rise's ContinueJump with them: it stops where it is, on
//     FlyingEnemy, its end never comes), its body's velocity zero, ForbidEnemyMovement, the
//     agent stopped (again);
//   - CreateExplosionToDamageEnemies: Physics2D.OverlapCircleAll(transform.position, tileSize *
//     3.5) (unity/Physics2DOverlap.h), and each collider whose object has an EnemyBehaviour (an
//     enemy's root: itself too, invincible or not) takes Enemy.DealDamageToEnemy(strength), in
//     the order the query returned them;
//   - SelfDestroy: MoveEnemy cancelled, off the room's OnRoomExit, OnDeath -= DropItemsOnDeath
//     (never subscribed: nothing), off the room's OnRoomEnter, not invincible, EnemyStats.Die()
//     (the achievements' counts and the room's EnemyDefeated: its health is untouched, so it's
//     alive and its OnDestroy leaves no dead sprite and no drops), Object.Destroy(gameObject)
//     (at the frame's end: the engine's delayed pass after the LateUpdates).
// Its AttackCollider child has a Kinematic Rigidbody2D of its own (game/Enemy.h:
// attackHasRigidbody). What its box enters (OnTriggerEnter2D -> AttackHit(other)): the hero
// ("Player", not shield-protected): DealDamageToPlayer if it's alive (not the base's isDead
// check), then PushPlayerBackwards(attackPushBackwardsForce, 50); an object tagged "Enemy":
// its Enemy.DealDamageToEnemy(strength); else one tagged "EnemyDamageReceiver": its
// EnemyDamageReceiver.DealDamageToEnemy(strength). Its own root (HardEnemy) is touched too:
// nothing.
//
// Its death (OnDeath, not SelfDestroy) counts in HeroStatistics (RegisterGolemKamikazeDeath:
// registerDeath) and leaves the room's OnRoomExit. MoveTowardsPlayer is dead code.

#include <functional>
#include <memory>
#include <vector>

#include "game/Enemy.h"
#include "game/Prefabs.h"
#include "unity/Vector2.h"

class b2Fixture;

namespace game {

class GolemKamikazeBehaviour : public EnemyBehaviour {
public:
    explicit GolemKamikazeBehaviour(Enemy& enemy) : EnemyBehaviour(enemy) {}
    ~GolemKamikazeBehaviour() override;

    // Private in C# (the class's own values; [Header] on a private field serializes nothing).
    float dashForce = 1000.0f;
    float distanceToPlayerToStartChasing = 80.0f;

    // The events it's on: the room's OnRoomExit, HeroMerchant.EnteredInCombat.
    bool subscribedToRoomExit = false;
    bool subscribedToCombat = false;

    // HeroStatistics.RegisterGolemKamikazeDeath.
    std::function<void()> registerDeath;
    // A collider the explosion's OverlapCircleAll found that isn't an enemy root the physics
    // knows (a breakable's BreakableObjectBehaviour would take the damage: not ported).
    std::function<void(const b2Fixture*)> explosionOther;
    // What happened (for checks; not the game's).
    int moveEnemyCalls = 0, randomMoves = 0, jumps = 0, explosions = 0;
    std::vector<Enemy*> lastExplosionHits; // the enemies the last explosion damaged, in order
    unity::Vector2 lastWander;             // MoveRandomly's last point
    bool lastWanderValid = false;          // and whether PolyNav took it
    // Each MoveEnemy (the InvokeRepeating's), before (after = false) and after it runs (for
    // replays: its draws).
    std::function<void(GolemKamikazeBehaviour&, bool after)> onMoveEnemy;

    void Init() override;
    void EnemyRoom_OnRoomEnter() override;
    // The room's OnRoomExit: MoveEnemy cancelled.
    void OnExitRoom();
    // HeroMerchant.EnteredInCombat: the Animator's "Awake" (an active one).
    void HeroMerchantEnteredInCombat();
    void ChooseAndApplyAction(float deltaTime) override;
    // The InvokeRepeating's (deltaTime unused).
    void MoveEnemy(float deltaTime) override;
    void StopOnExplode();
    void AttackHit(bool otherIsPlayer) override;
    void AttackHitOther(const AttackOther& other) override;
    void SelfDestroy();
    // The pad's rumble (defaultStrongEnemyVibrationIntensity, defaultStrongEnemyVibrationDuration).
    void VibrateOnExplosion();
    void CreateExplosionToDamageEnemies();
    // SoundsKamikaze's: MasterAudio.PlaySound("golem_dungeon_kamikaze_jump" / "_explosion").
    void PlayKamikazeJumpSound();
    void PlayKamikazeExplosionSound();
    bool OnAnimationEvent(const unity::AnimationEvent& e) override;
    void OnEnemyDestroyed() override;

protected:
    void OnDeath() override;

private:
    void MoveRandomly();
    void JumpTowardsPlayer();
    void MoveTowardsPlayer();
    bool CheckIfDestinationPointIsReachable(unity::Vector2 point) const;
    void SetLayer(int layer);
    void PlaySound(const char* sound);
};

// The GolemKamikazeBehaviour of a prefab whose root has one (MakeEnemy's), with its serialized
// fields (`fields`: prefabs.json's GolemKamikazeBehaviour row).
std::unique_ptr<GolemKamikazeBehaviour> MakeGolemKamikazeBehaviour(Enemy& enemy, const Behaviour& fields);

} // namespace game
