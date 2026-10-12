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
// Not yet: attacks, abilities other than the roll, holes and falling, knockback.

#include <string>
#include <vector>

#include "unity/Animator.h"
#include "unity/Physics2D.h"
#include "unity/PhysicsWorld.h"
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
    const unity::Collider2D* doorEntered = nullptr; // this frame's
    unity::Animator animator;
    int layer = unity::kLayerDefault; // gameObject.layer

    // Opt-in: the world his Rigidbody2D steps in (the room's colliders added to it), whose
    // contacts push him; his body is made there at the first step. nullptr: the steps only
    // round-trip his position through the centre of mass, as Box2D does without contacts.
    unity::PhysicsWorld* physicsWorld = nullptr;
    unity::Rigidbody2D* Body() const { return body_; }

    void SetDirection(Direction direction);
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
    unity::Rigidbody2D* body_ = nullptr;       // in physicsWorld
    struct SpeedEffect {
        int id;
        float multiplier;
    };
    std::vector<SpeedEffect> speedEffects_;
    std::vector<float> restoreAtFrameEnd_;
    int nextEffectId_ = 1, dashEffect_ = 0; // Dash's static speedEffect
};

} // namespace game
