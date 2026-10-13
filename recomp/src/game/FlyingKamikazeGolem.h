#pragma once
// FlyingKamikazeGolemBehaviour.cs: the Golem dungeon's flying kamikaze, an EnemyBehaviour
// (game/Enemy.h) that flies by setting its Rigidbody2D's velocity and charges at the hero.
//
// Its root box (10 x 10, FlyingEnemy, HardEnemy) is off in the prefab and its "AttackCollider"
// child (a 15 x 15 trigger on EnemyAttack) inactive: the charge switches both on.
//
// While it targets the hero, every frame (ChooseAndApplyAction, if canMove):
//   - not charging: PrepareChargeAttack: the body stopped, charging, the Animator's "Attack"
//     trigger (Idle -> Attack, whose clip fires MakeChargeAttack at 0.333 s and goes back to
//     Idle at its end);
//   - charging and not on its way back: MoveEnemy: the body's velocity is desiredDirection, which
//     then turns 2% of the way to speed * 3 towards the hero clamped into the room's bounds
//     (before the first charge the bounds are an empty Rect at the origin: towards (0, 0));
//     `angle` runs on by 3 deltaTime;
//   - with bounds set and its transform outside them (Rect.Contains): stopped (velocity and
//     desiredDirection zero), `angle` the hero's from the right (Extensions.GetAngleFromVector),
//     the transform clamped 2 inside the bounds.
// MakeChargeAttack (the clip's event): MasterAudio.PlaySound("golem_dungeon_flyingkamikazegolem_attack")
// (its draws), the bounds from the room the first time (DungeonRoom.GetRoomBounds), the attack
// child active and the root box enabled, then the charge's aim: the hero's direction
// (Vector3.normalized) turned by Random.Range(-20, 21) degrees, with the game's bug: the new y
// is computed from the already turned x; 150 along it, clamped into the bounds (z dropped);
// the look (the larger axis' sign: enemyDirection, and SetBool on Direction* parameters the
// controller doesn't have: Unity's warnings, no-ops), the trails (particles), desiredDirection
// 300 towards that point, Invoke("EndCharge", 0.5).
// EndCharge: on its way back, the trails off, the attack child inactive, and DOTween.To on the
// body's velocity to zero over 1.5 s, OutQuint, scaled time (Vector2Plugin: each component
// `start + change * ease` rounded once); its OnComplete: the root box disabled, desiredDirection
// zero, not on its way back, Invoke("EnableCharge", Random.Range(2, 6)). EnableCharge: not
// charging (the next frame prepares the next charge).
// Its death counts in HeroStatistics (RegisterFlyingKamikazeDeath: registerDeath).
//
// Mono's float arithmetic as the IL has it (unity/Mathf.h): the Vector2/Vector3 operators round
// each component; the aim's turned components and `angle` are one expression each. The
// transforms' z is taken as 0 (the oracle's "fly" event records the hero's and the golem's).
//
// Its charge (300 a second) is the first body past Physics2DSettings.maxTranslationSpeed (100)
// read per second: the port's Box2D takes that cap per step (b2_maxTranslation 100, b2Settings.h,
// where the reasons are), so the charge isn't held back; if enemy_FlyingKamikazeGolem.jsonl shows
// it covering 1 a 0.01 s step, Unity's cap is per second (b2_maxTranslation 1).
// Its "SoundsFlyingKamikaze" child's EventSounds plays golem_dungeon_flyingrepair_loop each time
// the golem is switched on (Enemy::enableSounds, game/EnemyEventSounds.cpp: its draws).

#include <functional>
#include <memory>
#include <string>

#include "game/Enemy.h"
#include "game/Prefabs.h"
#include "unity/Rect.h"
#include "unity/Vector2.h"

namespace game {

class FlyingKamikazeGolemBehaviour : public EnemyBehaviour {
public:
    // HeroMerchantController.Direction.
    enum class Direction { Left = 0, Right = 1, Back = 2, Front = 3 };

    explicit FlyingKamikazeGolemBehaviour(Enemy& enemy);
    ~FlyingKamikazeGolemBehaviour() override;

    // Serialized.
    Direction enemyDirection = Direction::Left;
    int trails = 0; // Trails (ParticleSystems: presentation)
    // HeroStatistics.RegisterFlyingKamikazeDeath.
    std::function<void()> registerDeath;

    // State (private in C#).
    bool isCharging = false;
    float chargeDistance = 150.0f;
    unity::Vector2 chargePosition; // (a Vector3: z 0 once clamped)
    unity::Rect bounds;
    float boundsBorder = 2.0f;
    float positionPercent = 0.98f;
    float angle = 0.0f;
    float pursueaArea = 80.0f; // (unused by the game)
    unity::Vector2 desiredDirection;
    bool isReturning = false;
    bool trailsPlaying = false;
    int returnTween = 0; // EndCharge's DOTween.To (Tweens handle; 0: none)

    void Init() override;
    void EnemyRoom_OnRoomEnter() override;
    void ChooseAndApplyAction(float deltaTime) override;
    void MoveEnemy(float deltaTime) override;
    void PrepareChargeAttack();
    void MakeChargeAttack();
    void EndCharge();
    void EnableCharge();
    using EnemyBehaviour::LookToPosition;
    void LookToPosition(unity::Vector2 position);
    void OnEnemyDestroyed() override;
    bool OnAnimationEvent(const unity::AnimationEvent& e) override;

    // DungeonRoom.GetRoomBounds() of the scene's room.
    unity::Rect RoomBounds() const;
    // Rect.Contains(point).
    static bool Contains(const unity::Rect& r, unity::Vector2 point);

    // For checks and replays (not the game's): each of these as it starts (after = false: a
    // replay puts the hero where the game had him for a charge, the Random state the game had)
    // and ends (after = true); ReturnDone is the return tween's OnComplete. The charge's draw
    // and EnableCharge's delay as last made.
    enum class Event { Prepare, Charge, EndCharge, ReturnDone, EnableCharge };
    std::function<void(FlyingKamikazeGolemBehaviour&, Event, bool after)> onEvent;
    int lastTurn = 0;             // Random.Range(-20, 21)
    float lastEnableDelay = 0.0f; // Random.Range(2, 6)

protected:
    void OnDeath() override;

private:
    void Notify(Event e, bool after)
    {
        if (onEvent) onEvent(*this, e, after);
    }
    void SetRootCollider(bool enabled);
    void SetAttackActive(bool active);
    void SetBoolIfAny(const char* name, bool value);
};

// The behaviour of a prefab whose root has one (MakeEnemy's), with its serialized fields
// (prefabs.json's FlyingKamikazeGolemBehaviour row).
std::unique_ptr<FlyingKamikazeGolemBehaviour> MakeFlyingKamikazeGolemBehaviour(Enemy& enemy, const Behaviour& fields);

} // namespace game
