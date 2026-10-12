#pragma once
// HeroMerchantController: the hero's walking and rolling, from the decompiled class.
//
// Update moves him by last frame's velocity (velX, velY, clamped to 0.75, times the move
// speed), then UpdateMoveAnimations takes the stick, box-casts each axis against the room's
// walls (with the corner slide), and sets the Animator's Walk and direction bools; the
// Animator runs after the scripts, as Unity's does. Pushing into a door for
// maxTimePushingDoors enters it (doorEntered, for one frame: the game's
// DungeonDoor.OnPlayerEnter is the caller's).
//
// The roll (LeftTrigger: the equipped ability, a Dash): UseAbility sets the direction, Dash
// fixes the velocity to the eight-way stick, boosts the speed (a SpeedEffect: speedFactor
// times 1.5 kBaseSpeed / speed, times 1 / that when it's destroyed: right after the scripts'
// Update if it's destroyed there, after LateUpdate if an animation event does it), turns move
// input off and makes him intangible; the Ability<Dir> states run the Animator at 1.25 and
// their exit gives control back. While intangible the casts use the god-mode layers.
//
// Each frame starts with the physics steps (every 0.01 s): his Dynamic Rigidbody2D round-trips
// his position through its centre of mass. Checked against the game frame by frame, walking
// and rolling (player_walk_test). With a physicsWorld (opt-in) the steps are Box2D's, his
// body's contacts pushing him out of what his casts let him into (rocks' corners); his layer
// follows SetIntangibleMode / SetDefaultMode.
//
// Holes: pushing into one for maxTimePushingHoles (or standing over one, checked every
// checkForHolesTimerStep) plays the fall: DOTween moves him a tile on (DelayedCall 0.06, DOMove
// 0.54), and when the art's spin ends (0.8 s) he's back at the door he came in by (respawn),
// or where he last stood clear of holes in the floor's first room.
//
// With a PhysicsWorld (physicsWorld), his Dynamic Rigidbody2D's contacts push him out of
// rocks and holes as Box2D does; without one, only the round trip through its centre of mass.
//
// The main attack (Action1): the equipped weapon's type's trigger (SpearMainAttack,
// GlovesMainAttack, ...) starts <Type>Main{First,Second,Third}<Dir> by ComboCounter; entering one
// disables move input, lunges if the stick points his way (velocity 1 for 0.1 s), triggers the
// weapon's Animator (game/Weapon: its hitbox and sprite) and counts the combo; leaving gives
// input back; Idle and Walk reset the combo. A press early in a swing (the type's
// attack-to-attack window) is thrown away, and so is a roll early in one (attackToRoll).
//
// Damage (HeroMerchant.DealDamageToPlayer): defense takes its share, the health drops (its
// Stat reporting first: the Animator's Damaged trigger, the health bar's damaged effect), and
// he's invincible for 1 s (scaled), blinks (the art off at 0.2 s, then every 0.1 s, on again at
// 1 s) and is intangible until the damaged effect ends (1.04 s unscaled: SetDefaultMode). A fall
// costs willFallDamage of his max health, dealt as he's put back.
//
// Not yet: secondary attacks, the bow's arrows, trick weapons, abilities other than the roll,
// knockback, amulets, the camera's shake (it draws Random.insideUnitSphere per
// hit: the RNG's sync with enemies will need it).
//
// Death (health at its minimum: HeroMerchantStats.Die): he stops, his weapon resets, the
// Animator plays Death and the game pauses; only his Animator runs on (its clip's last event,
// OnWillDeathAnimationEnded, ends it: deathAnimationEnded). Not yet: dropping the bag's items,
// the return to town.

#include <functional>
#include <string>
#include <vector>

#include "unity/Animator.h"
#include "unity/Physics2D.h"
#include "unity/Tween.h"
#include "unity/PhysicsWorld.h"
#include "unity/Vector2.h"
#include "game/HeroMerchantStats.h"
#include "game/Weapon.h"

namespace game {

struct Prefab;

class HeroMerchantController {
public:
    enum Direction { Left = 0, Right = 1, Back = 2, Front = 3 };
    enum EightDirection { E_Left, E_Right, E_Back, E_Front, E_LeftUp, E_LeftDown, E_RightUp, E_RightDown };

    struct Settings {
        float lowerDeadZone = 0.4f, kBaseSpeed = 185.0f;
        float merchantColliderXOffset = 1.0f, merchantColliderYOffset = -3.5f, tileSize = 24.0f;
        unity::Vector2 colliderSize{13.0f, 13.0f}, colliderOffset{1.0f, -3.5f};
        int collisionLayers = 0, slideLayers = 0, godModeLayers = 0;
        // Dash: RisenObstacle | LevelBoundary | PlayerSpecificShopCollider.
        int rollLayers = (1 << 8) | (1 << 11) | (1 << 17);
        float abilityCooldown = 0.0f; // the equipped ability item's cooldown
        // <type>AttackToAttackNormalizedTimeWindow, by WeaponType (None unused).
        float attackToAttackWindow[6] = {0.55f, 0.55f, 0.55f, 0.65f, 0.55f, 0.55f};
        float mainAttackToSecondaryWindow = 0.55f, attackToRollWindow = 0.55f, moveTimeWhileAttacking = 0.35f;
        int holesMask = 1 << 14;      // SunkenObstacle
        // His Rigidbody2D (hero_merchant's): Dynamic, mass 1 by hand, drag 5, no gravity,
        // rotation frozen, continuous; the centre of mass is his BoxCollider2D's centre.
        unity::Rigidbody2DDef body = HeroBody();
        static unity::Rigidbody2DDef HeroBody()
        {
            unity::Rigidbody2DDef b;
            b.linearDrag = 5.0f;
            b.gravityScale = 0.0f;
            b.constraints = unity::Rigidbody2DDef::FreezeRotation;
            b.collisionDetection = unity::Rigidbody2DDef::Continuous;
            return b;
        }
    };

    // What the pad gives this frame (InControl's actions).
    struct Input {
        unity::Vector2 stick; // LeftStick.X, .Y
        bool roll = false;    // LeftTrigger.WasPressed
        bool attack = false;  // Action1.WasPressed
        float unscaledDeltaTime = -1.0f; // Time.unscaledDeltaTime (the weapon's Animator's, DOTween's unscaled); < 0: deltaTime
    };
    enum class DamageType { Normal, ItemDrain, Self }; // HeroMerchant.DamageType

    // From his prefab (hero_merchant's HeroMerchant: its collider and the controller's layer
    // masks) and the constants table.
    static Settings SettingsFrom(const Prefab& hero);
    // His SpriteDepthOrderRender's offset (HeroMerchantArt/Will).
    static float DepthOffset(const Prefab& hero);

    // Holds `this` in the Animator's callbacks: don't copy or move it.
    HeroMerchantController(const unity::AnimatorController& controller, const Settings& settings, const unity::Physics2D& physics);
    HeroMerchantController(const HeroMerchantController&) = delete;
    HeroMerchantController& operator=(const HeroMerchantController&) = delete;

    // One frame, then the Animator, then what Unity does at the end of a frame.
    void Update(float deltaTime, const Input& input);
    void Update(float deltaTime, unity::Vector2 stick) { Update(deltaTime, Input{stick}); }

    unity::Vector2 position;
    float velX = 0.0f, velY = 0.0f;
    float currentPlayerMoveSpeed = 185.0f, speedFactor = 1.0f;
    bool canMove = true, isMoveInputEnabled = true, isHorizontalMoveInputEnabled = true, isVerticalMoveInputEnabled = true;
    bool isIntangible = false, isHolesIntangible = false, playingDamagedEffect = false, isFalling = false;
    bool abilityAvailable = true, abilityInUse = false;
    Direction heroDirection = Front, inputDirection = Front;
    float timePushingDoors = 0.0f, maxTimePushingDoors = 0.15f;
    float timePushingHoles = 0.0f, maxTimePushingHoles = 0.6f;
    float checkForHolesTimer = 0.0f, checkForHolesTimerStep = 0.2f;
    unity::Vector2 lastHolesFreePosition;
    // Where a fall puts him back: the room's (DungeonManager.TeleportHeroToLastDoorUsed: the
    // door he came in by), or in the floor's first room his last spot clear of holes.
    bool roomIsInit = false;
    unity::Vector2 respawn;
    unity::Tweens tweens; // DOTween's, for him
    Weapon* weapon = nullptr; // the equipped one, nullptr bare-handed
    int comboCounter = 0, maxComboCounter = 3;
    bool canAttack = true, attackInputEnabled = true;
    const unity::Collider2D* doorEntered = nullptr; // this frame's
    // DungeonDoor.isOpened for a door's collider (the room's door state is the caller's);
    // none: every door is open.
    std::function<bool(const unity::Collider2D&)> isDoorOpened;
    HeroMerchantStats stats;   // HeroMerchant's heroMerchantStats (Init'd from the constants)
    bool artVisible = true;    // heroMerchantArt.activeSelf (the invincibility blink)
    float flashAmount = 0.0f;  // the art's _FlashAmount (the damaged effect)
    bool flashTinted = false;  // its _Color: the health bar's colour (true) or white
    bool deathAnimationEnded = false; // OnWillDeathAnimationEnded
    unity::Animator animator;
    int layer = unity::kLayerDefault; // gameObject.layer

    // Opt-in: the world his Rigidbody2D steps in (the room's colliders added to it), whose
    // contacts push him; his body is made there at the first step. nullptr: the steps only
    // round-trip his position through the centre of mass, as Box2D does without contacts.
    unity::PhysicsWorld* physicsWorld = nullptr;
    unity::Rigidbody2D* Body() const { return body_; }

    void SetDirection(Direction direction);
    // DungeonLoadingPanel.Disable on a new floor: his input direction set, then UseAbility (he
    // rolls out of the door he came in by).
    void RollInto(Direction direction);
    // HeroMerchant.DealDamageToPlayer (no shield, absorption or amulets yet).
    void DealDamageToPlayer(float attackStrength, const char* damageSource, DamageType type = DamageType::Normal);
    void TeleportTo(unity::Vector2 to);
    bool CheckForHoles();
    void PlayFallAnimation();
    bool IsPlayingRollAnimation() const { return IsTag("Ability"); }

private:
    void UpdateMoveAnimations(unity::Vector2 stick, float deltaTime);
    Direction ParseInputToDirection(unity::Vector2 stick, float x, float y) const;
    EightDirection ParseInputToEightDirection(unity::Vector2 stick, float x, float y) const;
    void UpdateDirection(float x, float y);
    void UpdateAnimationHeroDirection(unity::Vector2 stick);
    bool IsTag(const char* tag) const;

    void UseAbility(unity::Vector2 stick);
    void MainAttackUseStart();
    void MainAttackUseEnd();
    void IdleStateStart();
    void ResetCurrentWeaponAnimator();
    void MoveWithAttackIfInputDirection(Direction direction, float time);
    bool IsPlayingMovingMainAttackAnimation() const;
    bool IsPlayingMainAttackAnimation() const;
    bool IsPlayingMainAttackAnimationToSecondaryAttackTimeWindow() const;
    bool CanAttackAgain() const;
    bool CanRollFromAttack() const;
    void DashAbilityUseStart();
    void OnHealthChanged(float newHealth);
    void Die();
    void PlayDamagedEffect();
    void SetPlayingDamagedEffectToFalse();
    void StartPlayingInvencibilityEffect(float time, float delay);
    void HidePlayerArtFromInvincibleEffect();
    void ShowPlayerArtFromInvincibleEffect();
    void StopPlayingInvencibilityEffect();
    void EnableHeroMerchantArt();
    void DisableHeroMerchantArt();
    void KillInvencibleEffectTweens();
    void KillDamagedEffectTweens();
    void SetIntangibleMode(int isHolesEnabled = 0);
    void SetDefaultMode();
    void EnableMoveInput();
    // SpeedEffects on him: speedFactor is multiplied as one starts and multiplied by
    // 1 / multiplier when Unity destroys it (restoreAtFrameEnd_, see Update).
    int AddSpeedEffectModifier(float multiplier);
    void ResetSpeedFactor();
    void DestroySpeedEffect(int id);

    Settings s_;
    const unity::Physics2D& physics_;
    unity::Vector2 stick_;
    float now_ = 0.0f, abilityReadyAt_ = 0.0f; // Time.time; Invoke("MakeAbilityAvailable")
    static constexpr double kFixedStep = 0.01; // Time.fixedDeltaTime
    double fixedTime_ = 0.0;                   // game time not yet stepped by physics
    unity::Rigidbody2D* body_ = nullptr;       // in physicsWorld
    struct SpeedEffect {
        int id;
        float multiplier;
    };
    std::vector<SpeedEffect> speedEffects_;
    std::vector<float> restoreAtFrameEnd_;
    int nextEffectId_ = 1, dashEffect_ = 0; // Dash's static speedEffect
    int moveWillWhileAttackingTween_ = 0;
    // The damaged effect's and the blink's tweens (killed as the game kills them).
    int tweenColorWhite_ = 0, tweenColorRed_ = 0, tweenColorDefault_ = 0, tweenSetPlayerDamagedEffectToFalse_ = 0;
    int tweenDelayedHide_ = 0, tweenHidePlayerFromInvincible_ = 0, tweenShowPlayerFromInvincible_ = 0;
    float previousHealth_ = 0.0f;
    float willFallDamage_ = 0.05f, willInvincibleBlinkDuration_ = 0.1f, willDamagedRedTime_ = 0.08f, willDamagedWhiteTime_ = 0.08f;
};

} // namespace game
