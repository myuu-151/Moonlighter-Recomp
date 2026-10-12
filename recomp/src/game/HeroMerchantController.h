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
// and rolling (player_walk_test).
//
// Holes: pushing into one for maxTimePushingHoles (or standing over one, checked every
// checkForHolesTimerStep) plays the fall: DOTween moves him a tile on (DelayedCall 0.06, DOMove
// 0.54), and when the art's spin ends (0.8 s) he's back at the door he came in by (respawn),
// or where he last stood clear of holes in the floor's first room.
//
// Not yet: attacks, abilities other than the roll, the fall's damage, the dynamic body's
// contacts (rocks), knockback.

#include <string>
#include <vector>

#include "unity/Animator.h"
#include "unity/Physics2D.h"
#include "unity/Tween.h"
#include "unity/Vector2.h"

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
        int holesMask = 1 << 14;      // SunkenObstacle
    };

    // What the pad gives this frame (InControl's actions).
    struct Input {
        unity::Vector2 stick; // LeftStick.X, .Y
        bool roll = false;    // LeftTrigger.WasPressed
    };

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
    const unity::Collider2D* doorEntered = nullptr; // this frame's
    unity::Animator animator;

    void SetDirection(Direction direction);
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
    void DashAbilityUseStart();
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
    struct SpeedEffect {
        int id;
        float multiplier;
    };
    std::vector<SpeedEffect> speedEffects_;
    std::vector<float> restoreAtFrameEnd_;
    int nextEffectId_ = 1, dashEffect_ = 0; // Dash's static speedEffect
};

} // namespace game
