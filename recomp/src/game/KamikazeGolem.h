#pragma once
// GolemKamikazeBehaviour.cs: the Golem dungeon's kamikaze, a small golem that hops at the hero
// and blows up. An EnemyBehaviour (game/Enemy.h) on the KamikazeGolem prefab.
//
// When its room is entered (EnemyRoom_OnRoomEnter): back at its spawn point with full health,
// not invincible, free to move, PursuingPlayer, on GroundEnemy, and InvokeRepeating("MoveEnemy",
// 0.65, 0.65): every 0.65 s from then (in the frame's or a fixed step's pass, as Invoke runs),
// even once it's dead and until it's destroyed or the room is left (OnExitRoom cancels it):
//   - MoveEnemy: if it can move, the Animator's "Attacking" is reset, and farther than 80 from
//     the hero (Vector2.Distance) it wanders (MoveRandomly: Random.insideUnitCircle, then
//     Random.Range(60, 90), an int: that far that way, a PolyNav destination if the point is
//     valid).
// While it targets the hero, every frame (ChooseAndApplyAction):
//   - within the agent's stoppingDistance + 20 of him (0 + 20), unless it's attacking already:
//     a dash at him (EnemyDashTowardsThePlayer(1000): an Impulse on its body) and AttackPlayer
//     (Attacking: the Animator's any-state transition to Explosion);
//   - then, if it can move: within 80 it jumps at him (JumpTowardsPlayer: canMove off, the
//     agent stopped, the end where he stands plus Random.insideUnitCircle * 10, the "Jump"
//     trigger, invincible, on FlyingEnemy; EnemyBehaviour.MakeJump: DOMoveY to the middle height
//     in 0.15 s (OutCubic) then DOMoveY to the end in 0.15 s (InCubic), and DOMoveX to the end
//     in 0.3 s (Linear), on the transform; at the end back on GroundEnemy); else it faces him
//     (LookToPosition).
//
// Its Animator (animators/KamikazeGolem.json, controller GolemKamikaze): Movement (a walk loop)
// is the default state; Idle (asleep, the prefab's sprite) goes through Awake (AllowEnemyMovement
// at 0.417) to Movement on "Awake" (HeroMerchant.EnteredInCombat, which comes while its room's
// enemies are switched off, so it's lost), but nothing leads to Idle; "Jump" from Idle or
// Movement to Jump (1.667 s:
// PlayKamikazeJumpSound at 0.083, the AttackCollider's box on from 0.417 to 1.0 (growing to 40
// x 40, offset y -4), PlayKamikazeExplosionSound and StopOnExplode at 0.583, SelfDestroy at
// 1.5); "Attacking" from any state (itself included) to Explosion (1.083 s: ForbidEnemyMovement
// at 0, PlayKamikazeExplosionSound, StopOnExplode and VibrateOnExplosion at 0.167 (the box on
// until 0.417, growing to 70 x 70), CreateExplosionToDamageEnemies at 0.25, SelfDestroy at
// 0.917). The sounds are its SoundsKamikaze component's (MasterAudio.PlaySound: its draws).
//   - StopOnExplode: the transform's tweens killed (the jump stops where it is, its end never
//     comes: it stays on FlyingEnemy), its body's velocity zero, ForbidEnemyMovement, the agent
//     stopped;
//   - CreateExplosionToDamageEnemies: Physics2D.OverlapCircleAll(its position, tileSize * 3.5)
//     (every collider the physics has there: unity/Physics2DOverlap.h); each one whose object
//     has an EnemyBehaviour (an enemy's root, itself included) takes DealDamageToEnemy(strength);
//   - SelfDestroy: MoveEnemy cancelled, off the room's events, not invincible, EnemyStats.Die()
//     (the room's EnemyDefeated: it doesn't die, it stays alive, so no dead sprite and no drops
//     when it goes), Object.Destroy at the frame's end.
// Its AttackCollider child has a Kinematic Rigidbody2D of its own (game/Enemy.h:
// attackHasRigidbody): what its box touches (OnTriggerEnter2D -> AttackHit): the hero (not
// shield-protected): DealDamageToPlayer if it's alive, and PushPlayerBackwards(50); something
// tagged "Enemy" or "EnemyDamageReceiver": DealDamageToEnemy(strength) on it.
//
// Its death counts in HeroStatistics (RegisterGolemKamikazeDeath: registerDeath).

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

    // Private in C# (the class's own values).
    float dashForce = 1000.0f;
    float distanceToPlayerToStartChasing = 80.0f;
    // EnemyBehaviour.MakeJump's (private there).
    unity::Vector2 jumpEndPosition;
    float jumpTime = 0.0f;
    std::function<void()> jumpCallback;
    // The transform's tweens (DOMoveY, DOMoveX: transform.DOKill kills them; 0: none).
    int jumpTweenY = 0, jumpTweenX = 0;

    // The events it's on: the room's OnRoomExit, HeroMerchant.EnteredInCombat.
    bool subscribedToRoomExit = false;
    bool subscribedToCombat = false;

    // HeroStatistics.RegisterGolemKamikazeDeath.
    std::function<void()> registerDeath;
    // A collider the explosion's OverlapCircleAll found that isn't an enemy root the physics
    // knows (a breakable's: its BreakableObject would take the damage; not ported).
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
    // EnemyBehaviour.MakeJump(endPosition, height, time, callback, Ease.Linear).
    void MakeJump(unity::Vector2 endPosition, float height, float time, std::function<void()> callback);
    bool OnAnimationEvent(const unity::AnimationEvent& e) override;
    void OnEnemyDestroyed() override;

protected:
    void OnDeath() override;
    void ContinueJump();
    void EndJump();

private:
    void MoveRandomly();
    void JumpTowardsPlayer();
    void MoveTowardsPlayer();
    bool CheckIfDestinationPointIsReachable(unity::Vector2 point) const;
    void SetLayer(int layer);
    void PlaySound(const char* sound);
    // DOMoveX (x) / DOMoveY: that component start + change * ease, the other as it reads then.
    int MoveAxis(bool x, float end, float duration, unity::Ease ease);
};

// The GolemKamikazeBehaviour of a prefab whose root has one (MakeEnemy's), with its serialized
// fields (`fields`: prefabs.json's GolemKamikazeBehaviour row).
std::unique_ptr<GolemKamikazeBehaviour> MakeGolemKamikazeBehaviour(Enemy& enemy, const Behaviour& fields);

} // namespace game
