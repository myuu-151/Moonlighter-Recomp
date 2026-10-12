#include "game/HeroMerchantController.h"

#include <cmath>

namespace game {

using unity::Collider2D;
using unity::Vector2;

namespace {

float Sign(float f) { return f >= 0.0f ? 1.0f : -1.0f; } // Mathf.Sign

float Distance(Vector2 a, Vector2 b)
{
    float dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

// Vector3.ClampMagnitude.
Vector2 ClampMagnitude(Vector2 v, float max)
{
    float sq = v.x * v.x + v.y * v.y;
    if (sq > max * max) {
        float m = std::sqrt(sq);
        if (m > 1e-05f) v = Vector2{v.x / m, v.y / m};
        else v = Vector2{};
        return v * max;
    }
    return v;
}

} // namespace

HeroMerchantController::HeroMerchantController(const unity::AnimatorController& controller, const Settings& settings,
                                               const unity::Physics2D& physics)
    : animator(controller), s_(settings), physics_(physics)
{
}

bool HeroMerchantController::IsTag(const char* tag) const
{
    return animator.GetCurrentAnimatorStateInfo().IsTag(tag);
}

void HeroMerchantController::Update(float deltaTime, Vector2 stick)
{
    // Update: the input direction's bools.
    static const char* const kInput[4] = {"LeftInput", "RightInput", "UpInput", "DownInput"};
    Direction input = ParseInputToDirection(stick, stick.x, stick.y);
    for (int d = 0; d < 4; d++) animator.SetBool(kInput[d], d == static_cast<int>(input));

    // Last frame's velocity moves him.
    if (std::fabs(velX) >= s_.lowerDeadZone || std::fabs(velY) >= s_.lowerDeadZone) {
        Vector2 move = ClampMagnitude(Vector2{velX, velY}, 0.75f) * currentPlayerMoveSpeed * speedFactor * deltaTime;
        position = position + move;
    }

    if ((IsTag("IdleShop") || IsTag("WalkShop") || IsTag("Idle") || IsTag("Walk") || IsTag("SwordAndShieldSecondaryAttack") ||
         IsTag("GlovesSecondaryAttack") || IsTag("SpearSecondaryAttack") || IsTag("BowSecondaryAttack") || IsTag("Ability") ||
         IsTag("WalkGrass")) &&
        !animator.IsInTransition())
        UpdateMoveAnimations(stick);

    animator.Update(deltaTime);
}

void HeroMerchantController::UpdateMoveAnimations(Vector2 stick)
{
    if (canMove) {
        if (isMoveInputEnabled) {
            if (isHorizontalMoveInputEnabled) velX = stick.x;
            if (isVerticalMoveInputEnabled) velY = stick.y;
        }
        const float xo = s_.merchantColliderXOffset, yo = s_.merchantColliderYOffset, tile = s_.tileSize;
        const Vector2 size = s_.colliderSize;
        auto overlap = [&](Vector2 at, Vector2 box) { return physics_.OverlapBox(position + at, box, 0.0f, s_.slideLayers); };
        // Of two separate colliders far apart, keep the nearer.
        auto nearer = [&](const Collider2D*& a, const Collider2D*& b, bool touchCheck) {
            if (a && b && a != b && (!touchCheck || !unity::Physics2D::IsTouching(*b, *a)) && Distance(a->position, b->position) >= tile * 2.5f) {
                if (Distance(position, a->position) < Distance(position, b->position)) b = nullptr;
                else a = nullptr;
            }
        };

        unity::RaycastHit2D hit = physics_.BoxCast(s_.colliderOffset + position, size, 0.0f, Vector2{velX, 0.0f}, 4.0f, s_.collisionLayers);
        if (hit && Sign((hit.point - position).x) == Sign(velX)) {
            if (velY == 0.0f && std::fabs(velX) > 0.5f && hit.collider->tag != "DungeonLevelDoor") {
                const Collider2D* c1 = overlap(Vector2{xo + velX * 14.0f, yo + (0.0f - tile) / 2.0f}, Vector2{size.x, size.y / 2.0f});
                const Collider2D* c2 = overlap(Vector2{xo + velX * 14.0f, yo + tile / 2.0f}, Vector2{size.x, size.y / 2.0f});
                nearer(c1, c2, true);
                const Collider2D* c3 = overlap(Vector2{xo + (0.0f - tile) / 2.0f, yo + velY * 14.0f}, Vector2{size.x / 2.0f, size.y});
                const Collider2D* c4 = overlap(Vector2{xo + tile / 2.0f, yo + velY * 14.0f}, Vector2{size.x / 2.0f, size.y});
                nearer(c3, c4, true);
                if (!c1 && (!c3 || !c4)) velY += -0.5f;
                if (!c2 && (!c3 || !c4)) velY += 0.5f;
            }
            velX = 0.0f;
        }
        hit = physics_.BoxCast(s_.colliderOffset + position, size, 0.0f, Vector2{0.0f, velY}, 4.0f, s_.collisionLayers);
        if (hit && Sign((hit.point - position).y) == Sign(velY)) {
            if (velX == 0.0f && std::fabs(velY) > 0.5f && hit.collider->tag != "DungeonLevelDoor") {
                const Collider2D* c5 = overlap(Vector2{xo + (0.0f - tile) / 2.0f, yo + velY * 14.0f}, Vector2{size.x / 2.0f, size.y});
                const Collider2D* c6 = overlap(Vector2{xo + tile / 2.0f, yo + velY * 14.0f}, Vector2{size.x / 2.0f, size.y});
                nearer(c5, c6, false);
                const Collider2D* c7 = overlap(Vector2{xo + velX * 14.0f, yo + (0.0f - tile) / 2.0f}, Vector2{size.x, size.y / 2.0f});
                const Collider2D* c8 = overlap(Vector2{xo + velX * 14.0f, yo + tile / 2.0f}, Vector2{size.x, size.y / 2.0f});
                nearer(c7, c8, false);
                if (!c6 && (!c7 || !c8)) velX += 0.5f;
                if (!c5 && (!c7 || !c8)) velX += -0.5f;
            }
            velY = 0.0f;
        }
    }
    if (IsTag("Ability") || IsTag("SwordAndShieldMainAttack") || IsTag("BigSwordMainAttack") || IsTag("BigSwordSecondaryAttack") ||
        IsTag("SpearMainAttack") || IsTag("GlovesMainAttack") || IsTag("BowMainAttack") || animator.IsInTransition())
        return;
    bool moving = canMove && (std::fabs(stick.x) >= s_.lowerDeadZone || std::fabs(stick.y) >= s_.lowerDeadZone);
    animator.SetBool("Walk", moving);
    if (!IsTag("SwordAndShieldSecondaryAttack")) {
        // UpdateAnimationHeroDirection
        if (!((std::fabs(stick.x) < s_.lowerDeadZone && std::fabs(stick.y) < s_.lowerDeadZone) || !canMove || !isMoveInputEnabled))
            UpdateDirection(stick.x, stick.y);
    }
}

HeroMerchantController::Direction HeroMerchantController::ParseInputToDirection(Vector2 stick, float x, float y) const
{
    if (std::fabs(stick.x) < s_.lowerDeadZone && std::fabs(stick.y) < s_.lowerDeadZone) return heroDirection;
    if (x == 0.0f && y == 0.0f) return heroDirection;
    if (std::fabs(x) == std::fabs(y)) return heroDirection;
    if (x < 0.0f) {
        if (y < 0.0f) return std::fabs(x) > std::fabs(y) ? Left : Front;
        return std::fabs(x) > std::fabs(y) ? Left : Back;
    }
    if (y < 0.0f) return std::fabs(x) > std::fabs(y) ? Right : Front;
    return std::fabs(x) > std::fabs(y) ? Right : Back;
}

void HeroMerchantController::UpdateDirection(float x, float y)
{
    if (x == 0.0f && y == 0.0f) return;
    if (x < 0.0f) {
        if (y < 0.0f) SetDirection(std::fabs(x) > std::fabs(y) ? Left : Front);
        else SetDirection(std::fabs(x) > std::fabs(y) ? Left : Back);
        return;
    }
    if (y < 0.0f) SetDirection(std::fabs(x) > std::fabs(y) ? Right : Front);
    else SetDirection(std::fabs(x) > std::fabs(y) ? Right : Back);
}

void HeroMerchantController::SetDirection(Direction direction)
{
    heroDirection = direction;
    animator.SetBool("UpDirection", direction == Back);
    animator.SetBool("DownDirection", direction == Front);
    animator.SetBool("LeftDirection", direction == Left);
    animator.SetBool("RightDirection", direction == Right);
}

} // namespace game
