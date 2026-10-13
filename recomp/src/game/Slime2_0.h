#pragma once
// Slime2_0Behaviour.cs: the Golem dungeon's big slime (prefab enemies_golem/Slime, controller
// Slime2_0), an EnemyBehaviour (game/Enemy.h) that dashes at the hero and swallows him: he's
// trapped inside until he mashes the stick or the slime spits him out.
//
// While it targets him, every frame (ChooseAndApplyAction):
//   - not holding him (enemyState not Attacking):
//       - he's free (HeroTrap::isTrapped false), it isn't dashing or biting, and he's within
//         dashRadius (100; Vector3.Distance of the transforms, the root rounded to a float): the
//         dash: isDashing, EnemyDashTowardsThePlayer(1500) (an Impulse towards him: 250 a second
//         for its mass 6), Invoke("EnableDash", 1.5);
//       - else MoveEnemy (EnemyBehaviour's chase through the PolyNav map);
//   - holding him (Attacking): unless he's dead, MoveAutomaticallyToPoint(its "WillAbsorbedPoint"
//     child, 0 s, no animation) every frame; his look from the stick (Value.normalized: the
//     larger axis, ties to x; x > 0 Right else Left, y > 0 Back else Front); while not releasing,
//     a change of the stick's x or y sign since last frame (Mathf.Sign: 0 counts as +) counts a
//     press, and the fourth (buttonCountThreshold) releases him (ReleasePlayerInmediatelly: the
//     Animator's "Release") and starts the count over.
// Its attack trigger (the "AttackCollider" child, 15 x 15, on in every state) touching the hero
// (AttackHit, OnTriggerEnter2D; whatever else it touches is only remembered: lastAttackHitGO;
// "Hole" would stop its body, but EnemyAttack doesn't meet SunkenObstacle):
//   - biting (isAttacking): EnemyBehaviour.AttackHit (DealDamageToPlayer);
//   - else if he's trapped (by another), or Random.value < 0.3 (drawn only when he's free): the
//     bite (PlayMeleeAnimation) if he's alive, it's alive and realtimeSinceStartup is more than
//     timeBetweenAttacks (1.5) past the last bite: movement forbidden, the body stopped,
//     isAttacking, timeLastAttack, the "MeleeAttack" trigger (its clip grows the trigger to 36 x 36
//     from 0.5 s to 0.833, FinishMeleeAttack at 1.25: movement back);
//   - else TryAbsorbPlayer: if he's alive, realtimeSinceStartup is more than
//     timeBetweenAbsortions (2.3) past the last spit, it's alive, he's free and not rolling: he's
//     trapped (isTrapped, his movement and attack forbidden, ChangeToDungeonMode, his weapon's
//     Animator reset, his art off and on), the "Absorb" trigger, Attacking, its object on
//     TransparentFX (its body meets almost nothing), its sprite drawn over his (overOffset 10),
//     its body stopped, the presses counted from 0.
// The Absorb clip (2.917 s) goes to Expell at its end, or at once on "Release"; Expell's events:
// PlaySlimePop (0.333), ReleasePlayer (0.417). ReleasePlayer, if it holds him: Pursuing, his
// movement and attack back, his ability trail off, isTrapped off, its sprite's own order back, its
// object on GroundEnemy, then where he lands: GetRandomCircleBorderPoint(40) (Random.Range(0, 360)
// as an int, taken as radians: the game's), redrawn while a Physics2D.Raycast from the slime
// along it (40, LevelBoundary | EnemyBoundary | RisenObstacle) hits something, at most 100 times
// (then zero); the slime's position plus it, clamped 10 inside the room's bounds;
// MoveAutomaticallyToPoint(it, 0.1); effectOnAttack on him (none on the prefab);
// timeLastExpell; isDashing with EnableDash invoked anew 1.5 s on.
// Its sounds (SoundsSlime, the same object: the clips' PlaySlimeSwallow, PlaySlimeAttack,
// PlaySlimePop) and its hit (EnemyWasHit: golem_dungeon_slime_hit when the health drops) are
// MasterAudio.PlaySound's draws.
// OnDisable (switched off, destroyed while on, and OnDeath's `enabled = false`) and OnDeath let
// the hero go without the animation (ReleaseWithoutAnimation: if he's in its room, his movement
// and attack back, isTrapped off, his trail off; whichever slime holds him: the game's), and take
// it off HeroMerchant.OnDie (Init put it on). Its death counts in HeroStatistics by culture.
//
// realtimeSinceStartup isn't the game's clock: the driver gives it (realtimeSinceStartup; a
// replay feeds the values the game read, in order). Unset: the scene's Time.time.
//
// Mono's float arithmetic (unity/Mathf.h): the cooldowns' `realtime - last > wait` and the
// distances in doubles; Vector3/Vector2 operators round each component.

#include <functional>
#include <memory>
#include <string>

#include "game/Enemy.h"
#include "game/HeroTrap.h"
#include "game/Prefabs.h"
#include "unity/Rect.h"
#include "unity/Vector2.h"

namespace game {

class Slime2_0Behaviour : public EnemyBehaviour {
public:
    explicit Slime2_0Behaviour(Enemy& enemy) : EnemyBehaviour(enemy) {}

    // Serialized.
    CommonEnemyCulture slimeCulture = CommonEnemyCulture::Golem;

    // Private in C# (public here for replays and checks).
    bool hasWillAbsorbedPoint = true;       // transform.Find("WillAbsorbedPoint") found it
    unity::Vector2 willAbsorbedPointLocal{0.0f, 5.0f}; // its local position (the prefab's)
    float dashRadius = 100.0f, dashForce = 1500.0f, attackRadius = 30.0f; // (attackRadius unused)
    bool hasHeroMerchantTrapped = false, isDashing = false, isAttacking = false;
    float dashDisabledTime = 1.5f, attackDisabledTime = 1.0f, absorbTimeThreshold = 0.8f; // (the last two unused)
    float lastButtonPress = 0.0f; // (unused)
    bool isReleasing = false;
    int buttonCount = 0, buttonCountThreshold = 4;
    float timeBetweenAbsortions = 2.3f, timeLastExpell = 0.0f;
    float timeBetweenAttacks = 1.5f, timeLastAttack = 0.0f;
    std::string lastAttackHitTag; // lastAttackHitGO, by its tag ("": what the port can't name)
    bool hasLastAttackHit = false;
    // Its sprite's SpriteDepthOrderRender (sorting only): over the hero's while it holds him, and
    // overOffset.
    bool depthOverHero = false;
    int depthOverOffset = 10;

    // Time.realtimeSinceStartup as each read takes it (`method`: the reading method's name:
    // "AttackHit", "PlayMeleeAnimation", "TryAbsorbPlayer", "ReleasePlayer"). Unset: the scene's
    // Time.time.
    std::function<float(const char* method)> realtimeSinceStartup;
    // HeroStatistics.Register<Culture>SlimeDeath.
    std::function<void(CommonEnemyCulture)> registerDeath;

    void Init() override;
    void ChooseAndApplyAction(float deltaTime) override;
    void EnableDash();
    // AttackHit(other): what its attack trigger touched; the port names the hero ("Player") and
    // nothing else ("": EnemyAttack meets no "Hole").
    void AttackHit(bool otherIsPlayer) override;
    void AttackHitTag(const std::string& tag);
    void PlayMeleeAnimation();
    void FinishMeleeAttack();
    void EnemyWasHit(float newHealthValue) override;
    void TryAbsorbPlayer();
    void ReleasePlayerInmediatelly();
    void ReleasePlayer();
    void ReleaseWithoutAnimation();
    void OnDisable() override;
    bool OnAnimationEvent(const unity::AnimationEvent& e) override;
    // The WillAbsorbedPoint child's world position (the root's plus its local position).
    unity::Vector2 WillAbsorbedPoint() const;
    // DungeonRoom.GetRoomBounds() of the scene's room.
    unity::Rect RoomBounds() const;

    // For checks and replays (not the game's): each of these as it starts (after = false) and
    // ends (after = true), whether or not it does anything; and what the last of them drew.
    enum class Event { Hit, Melee, Absorb, Trapped, ReleaseNow, Release, EnableDash, ReleaseWithoutAnimation };
    std::function<void(Slime2_0Behaviour&, Event, bool after)> onEvent;
    int lastReleaseRedraws = -1;  // ReleasePlayer's redraws (num2), -1: none yet
    unity::Vector2 lastReleasePoint; // where it sent him
    int lastBorderAngle = 0;      // GetRandomCircleBorderPoint's last Random.Range(0, 360)

protected:
    void OnDeath() override;

private:
    HeroTrap* Trap() const { return enemy.scene.hero ? enemy.scene.hero->trap : nullptr; }
    bool HeroDead() const { return enemy.scene.hero && enemy.scene.hero->isDead; }
    float Realtime(const char* method);
    unity::Vector2 GetRandomCircleBorderPoint(float radius);
    void SetLayer(int layer);
    void StopBody();
    void Notify(Event e, bool after)
    {
        if (onEvent) onEvent(*this, e, after);
    }
};

// The behaviour of a prefab whose root has one (MakeEnemy's), with its serialized fields
// (prefabs.json's Slime2_0Behaviour row) and the WillAbsorbedPoint child's position.
std::unique_ptr<Slime2_0Behaviour> MakeSlime2_0Behaviour(Enemy& enemy, const Behaviour& fields, const Prefab& prefab);

} // namespace game
