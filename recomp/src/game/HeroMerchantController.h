#pragma once
// HeroMerchantController: the hero's walking, from the decompiled class. Update moves him
// by last frame's velocity (velX, velY, clamped to 0.75, times the move speed), then
// UpdateMoveAnimations takes the stick, box-casts each axis against the room's walls (with
// the corner slide), and sets the Animator's Walk and direction bools; the Animator runs
// after the scripts, as Unity's does.
//
// Not yet: attacks, rolls, abilities, holes and falling, doors, the dynamic body's
// contacts (rocks), knockback.

#include <string>

#include "unity/Animator.h"
#include "unity/Physics2D.h"
#include "unity/Vector2.h"

namespace game {

class HeroMerchantController {
public:
    enum Direction { Left = 0, Right = 1, Back = 2, Front = 3 };

    struct Settings {
        float lowerDeadZone = 0.4f;
        float merchantColliderXOffset = 1.0f, merchantColliderYOffset = -3.5f, tileSize = 24.0f;
        unity::Vector2 colliderSize{13.0f, 13.0f}, colliderOffset{1.0f, -3.5f};
        int collisionLayers = 0, slideLayers = 0;
    };

    HeroMerchantController(const unity::AnimatorController& controller, const Settings& settings, const unity::Physics2D& physics);

    // One frame: the stick as InControl reports it (LeftStick.X, .Y), then the Animator.
    void Update(float deltaTime, unity::Vector2 stick);

    unity::Vector2 position;
    float velX = 0.0f, velY = 0.0f;
    float currentPlayerMoveSpeed = 185.0f, speedFactor = 1.0f;
    bool canMove = true, isMoveInputEnabled = true, isHorizontalMoveInputEnabled = true, isVerticalMoveInputEnabled = true;
    Direction heroDirection = Front;
    unity::Animator animator;

    void SetDirection(Direction direction);

private:
    void UpdateMoveAnimations(unity::Vector2 stick);
    Direction ParseInputToDirection(unity::Vector2 stick, float x, float y) const;
    void UpdateDirection(float x, float y);
    bool IsTag(const char* tag) const;

    Settings s_;
    const unity::Physics2D& physics_;
};

} // namespace game
