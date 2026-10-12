#include "game/HeroMerchantController.h"

#include <algorithm>
#include <cmath>

#include "game/Constants.h"
#include "game/Prefabs.h"

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

bool IsRollState(int32_t hash)
{
    static const int32_t kRolls[4] = {unity::StringToHash("AbilityRight"), unity::StringToHash("AbilityLeft"), unity::StringToHash("AbilityUp"),
                                      unity::StringToHash("AbilityDown")};
    for (int32_t h : kRolls)
        if (h == hash) return true;
    return false;
}

} // namespace

HeroMerchantController::Settings HeroMerchantController::SettingsFrom(const Prefab& hero)
{
    Settings s;
    s.lowerDeadZone = Constants::GetFloat("lowerDeadZone");
    s.kBaseSpeed = Constants::GetFloat("kBaseSpeed");
    s.merchantColliderXOffset = Constants::GetFloat("merchantColliderXOffset");
    s.merchantColliderYOffset = Constants::GetFloat("merchantColliderYOffset");
    s.tileSize = Constants::GetFloat("tileSize");
    const PrefabNode& root = hero.nodes[0];
    for (const ColliderDef& c : root.colliders)
        if (c.type == "BoxCollider2D") {
            s.colliderSize = {c.sx, c.sy};
            s.colliderOffset = {c.ox, c.oy};
        }
    for (const Behaviour& b : root.behaviours)
        if (b.cls == "HeroMerchantController") {
            s.collisionLayers = static_cast<int>(b.Field("collisionDetectionLayers"));
            s.slideLayers = static_cast<int>(b.Field("slideCollisionDetectionLayers"));
            s.godModeLayers = static_cast<int>(b.Field("godModeCollisionDetectionLayers"));
        }
    if (root.hasRigidbody) {
        s.body = root.rigidbody;
        s.body.hasCenterOfMass = true; // the collider's centre (useAutoMass off keeps it)
        s.body.centerOfMass = s.colliderOffset;
    }
    return s;
}

float HeroMerchantController::DepthOffset(const Prefab& hero)
{
    for (const PrefabNode& n : hero.nodes)
        if (n.path == "HeroMerchantArt/Will")
            for (const Behaviour& b : n.behaviours)
                if (b.cls == "SpriteDepthOrderRender") return static_cast<float>(b.Field("offset"));
    return 0.0f;
}

HeroMerchantController::HeroMerchantController(const unity::AnimatorController& controller, const Settings& settings,
                                               const unity::Physics2D& physics)
    : animator(controller), s_(settings), physics_(physics)
{
    // SMAnimatorHandler's registrations (Init): the roll states.
    animator.onStateEnter = [this](int32_t hash) {
        if (IsRollState(hash)) animator.speed = 1.25f; // AbilityUseStarted
    };
    animator.onStateExit = [this](int32_t hash) {
        if (!IsRollState(hash)) return;
        // AbilityUseEnded
        animator.speed = 1.0f;
        SetDirection(inputDirection);
        EnableMoveInput();
        velX = velY = 0.0f; // StopMovingVerticalyAndHorizontaly
        SetDefaultMode();
    };
    // The clips' events (on the hero's GameObject: the controller's methods).
    animator.onEvent = [this](const unity::AnimationEvent& e) {
        if (e.function == "AbilityUseStart") {
            // Ability.SendAbilityUseStart: in use, and every component's AbilityUseStart.
            abilityInUse = true;
            DashAbilityUseStart();
        } else if (e.function == "AbilityUseEnd") {
            abilityInUse = false;
            DestroySpeedEffect(dashEffect_); // Dash.AbilityUseEnd
            UpdateAnimationHeroDirection(stick_);
        }
    };
}

bool HeroMerchantController::IsTag(const char* tag) const
{
    return animator.GetCurrentAnimatorStateInfo().IsTag(tag);
}

void HeroMerchantController::Update(float deltaTime, const Input& input)
{
    // Last frame's end (after LateUpdate): SpeedEffects destroyed then undo their multiplier.
    for (float m : restoreAtFrameEnd_) speedFactor *= 1.0f / m;
    restoreAtFrameEnd_.clear();

    // The physics steps before this frame's scripts (every 0.01 s of game time): his Dynamic
    // Rigidbody2D takes the transform as its centre of mass (position + the collider's centre)
    // and writes back the centre minus it, which rounds where the sum changes exponent; in a
    // physics world, its contacts move the centre too.
    fixedTime_ += deltaTime;
    while (fixedTime_ >= kFixedStep) {
        fixedTime_ -= kFixedStep;
        if (physicsWorld) {
            if (!body_) body_ = &physicsWorld->AddBody(s_.body, position, layer, s_.colliderOffset, s_.colliderSize);
            physicsWorld->SetLayer(*body_, layer);
            position = physicsWorld->Simulate(*body_, position, static_cast<float>(kFixedStep));
        } else {
            const Vector2 c = s_.colliderOffset;
            position = Vector2{(position.x + c.x) - c.x, (position.y + c.y) - c.y};
        }
    }

    stick_ = input.stick;
    const Vector2 stick = input.stick;
    now_ += deltaTime;
    if (!abilityAvailable && now_ >= abilityReadyAt_) abilityAvailable = true; // MakeAbilityAvailable

    // Update: the input direction's bools.
    static const char* const kInput[4] = {"LeftInput", "RightInput", "UpInput", "DownInput"};
    inputDirection = ParseInputToDirection(stick, stick.x, stick.y);
    for (int d = 0; d < 4; d++) animator.SetBool(kInput[d], d == static_cast<int>(inputDirection));

    // Last frame's velocity moves him.
    if (std::fabs(velX) >= s_.lowerDeadZone || std::fabs(velY) >= s_.lowerDeadZone || IsPlayingRollAnimation()) {
        Vector2 move = ClampMagnitude(Vector2{velX, velY}, 0.75f) * currentPlayerMoveSpeed * speedFactor * deltaTime;
        position = position + move;
    }
    checkForHolesTimer += deltaTime;
    if (checkForHolesTimer > checkForHolesTimerStep) {
        CheckForHoles();
        checkForHolesTimer = 0.0f;
    }

    if ((IsTag("IdleShop") || IsTag("WalkShop") || IsTag("Idle") || IsTag("Walk") || IsTag("SwordAndShieldSecondaryAttack") ||
         IsTag("GlovesSecondaryAttack") || IsTag("SpearSecondaryAttack") || IsTag("BowSecondaryAttack") || IsTag("Ability") ||
         IsTag("WalkGrass")) &&
        !animator.IsInTransition())
        UpdateMoveAnimations(stick, deltaTime);

    // The roll (LeftTrigger): the equipped ability, unless one's already playing.
    if (canMove && input.roll && !isFalling && abilityAvailable && !IsTag("Ability")) UseAbility(stick);

    // Objects destroyed during Update go before the animation pass; those the Animator's events
    // destroy wait for the end of the frame (after LateUpdate).
    for (float m : restoreAtFrameEnd_) speedFactor *= 1.0f / m;
    restoreAtFrameEnd_.clear();
    tweens.Update(deltaTime); // DOTween's own Update
    animator.Update(deltaTime);
}

void HeroMerchantController::UpdateMoveAnimations(Vector2 stick, float deltaTime)
{
    doorEntered = nullptr;
    if (canMove) {
        const Collider2D* door = nullptr;
        bool hole = false;
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

        if (!isIntangible || isHolesIntangible || playingDamagedEffect) {
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
                if (hit.collider->tag == "Hole" || hit.collider->tag == "NextLevelPortal") hole = true;
                if (hit.collider->tag == "DungeonLevelDoor") door = hit.collider;
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
                if (hit.collider->tag == "Hole" || hit.collider->tag == "NextLevelPortal") hole = true;
                if (hit.collider->tag == "DungeonLevelDoor") door = hit.collider;
            }
        } else {
            // Intangible (rolling): only rocks and walls stop him, and take the speed boost.
            unity::RaycastHit2D hit = physics_.BoxCast(s_.colliderOffset + position, size, 0.0f, Vector2{velX, 0.0f}, 4.0f, s_.godModeLayers);
            if (hit && Sign((hit.point - position).x) == Sign(velX)) {
                ResetSpeedFactor();
                velX = 0.0f;
                if (hit.collider->tag == "Hole" || hit.collider->tag == "NextLevelPortal") hole = true;
                if (hit.collider->tag == "DungeonLevelDoor") door = hit.collider;
            }
            hit = physics_.BoxCast(s_.colliderOffset + position, size, 0.0f, Vector2{0.0f, velY}, 4.0f, s_.godModeLayers);
            if (hit && Sign((hit.point - position).y) == Sign(velY)) {
                ResetSpeedFactor();
                velY = 0.0f;
                if (hit.collider->tag == "Hole" || hit.collider->tag == "NextLevelPortal") hole = true;
                if (hit.collider->tag == "DungeonLevelDoor") door = hit.collider;
            }
        }
        timePushingHoles = canMove && hole ? timePushingHoles + deltaTime : 0.0f;
        if (timePushingHoles > maxTimePushingHoles || (isHolesIntangible && hole)) PlayFallAnimation();
        timePushingDoors = door ? timePushingDoors + deltaTime : 0.0f;
        if (timePushingDoors > maxTimePushingDoors && door) {
            doorEntered = door;
            timePushingDoors = 0.0f;
        }
    }
    if (IsTag("Ability") || IsTag("SwordAndShieldMainAttack") || IsTag("BigSwordMainAttack") || IsTag("BigSwordSecondaryAttack") ||
        IsTag("SpearMainAttack") || IsTag("GlovesMainAttack") || IsTag("BowMainAttack") || animator.IsInTransition())
        return;
    bool moving = canMove && (std::fabs(stick.x) >= s_.lowerDeadZone || std::fabs(stick.y) >= s_.lowerDeadZone);
    animator.SetBool("Walk", moving);
    if (!IsTag("SwordAndShieldSecondaryAttack")) UpdateAnimationHeroDirection(stick);
}

void HeroMerchantController::UpdateAnimationHeroDirection(Vector2 stick)
{
    if ((std::fabs(stick.x) < s_.lowerDeadZone && std::fabs(stick.y) < s_.lowerDeadZone) || !canMove || !isMoveInputEnabled) return;
    UpdateDirection(stick.x, stick.y);
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

HeroMerchantController::EightDirection HeroMerchantController::ParseInputToEightDirection(Vector2 stick, float x, float y) const
{
    if (std::fabs(stick.x) < s_.lowerDeadZone && std::fabs(stick.y) < s_.lowerDeadZone) return static_cast<EightDirection>(heroDirection);
    if (x == 0.0f && y == 0.0f) return static_cast<EightDirection>(heroDirection);
    const bool diagonal = std::fabs(std::fabs(x) - std::fabs(y)) < 0.5f;
    const bool wide = std::fabs(x) > std::fabs(y);
    if (x < 0.0f) {
        if (y < 0.0f) return diagonal ? E_LeftDown : wide ? E_Left : E_Front;
        return diagonal ? E_LeftUp : wide ? E_Left : E_Back;
    }
    if (y < 0.0f) return diagonal ? E_RightDown : wide ? E_Right : E_Front;
    return diagonal ? E_RightUp : wide ? E_Right : E_Back;
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

// UseAbility with the Dash ability (Ability.UseAbility's cooldown, Dash.UseAbility).
void HeroMerchantController::UseAbility(Vector2 stick)
{
    SetDirection(inputDirection);
    abilityAvailable = false;
    abilityReadyAt_ = now_ + s_.abilityCooldown;

    DashAbilityUseStart();
    dashEffect_ = AddSpeedEffectModifier(1.5f * (s_.kBaseSpeed / currentPlayerMoveSpeed));
    // Start/StopMoving*: the stop only acts outside a roll (the Animator isn't in one yet).
    const bool rolling = IsPlayingRollAnimation();
    auto stopX = [&] { if (!rolling) velX = 0.0f; };
    auto stopY = [&] { if (!rolling) velY = 0.0f; };
    switch (ParseInputToEightDirection(stick, stick.x, stick.y)) {
    case E_Front: velY = -1.0f; stopX(); break;
    case E_Back: velY = 1.0f; stopX(); break;
    case E_Left: velX = -1.0f; stopY(); break;
    case E_Right: velX = 1.0f; stopY(); break;
    case E_LeftUp: velX = -1.0f; velY = 1.0f; break;
    case E_LeftDown: velX = -1.0f; velY = -1.0f; break;
    case E_RightUp: velX = 1.0f; velY = 1.0f; break;
    case E_RightDown: velX = 1.0f; velY = -1.0f; break;
    }
    unity::RaycastHit2D hit = physics_.BoxCast(s_.colliderOffset + position, s_.colliderSize, 0.0f, Vector2{velX, 0.0f}, 4.0f, s_.rollLayers);
    if (hit && Sign((hit.point - position).x) == Sign(velX)) {
        ResetSpeedFactor();
        stopX();
    }
    hit = physics_.BoxCast(s_.colliderOffset + position, s_.colliderSize, 0.0f, Vector2{0.0f, velY}, 4.0f, s_.rollLayers);
    if (hit && Sign((hit.point - position).y) == Sign(velY)) {
        ResetSpeedFactor();
        stopY();
    }
    animator.SetTrigger("Ability");
}

// Dash.AbilityUseStart: no move input (keeping the velocity), intangible.
void HeroMerchantController::DashAbilityUseStart()
{
    isMoveInputEnabled = false;
    isHorizontalMoveInputEnabled = false;
    isVerticalMoveInputEnabled = false;
    SetIntangibleMode();
}

void HeroMerchantController::TeleportTo(Vector2 to)
{
    velX = velY = 0.0f;
    position = to;
}

bool HeroMerchantController::CheckForHoles()
{
    const Vector2 v{position.x, position.y + s_.merchantColliderYOffset};
    const Vector2 one{1.0f, 1.0f};
    const float n = 2.5f;
    const Collider2D* a = physics_.OverlapBox(v + Vector2{n, n}, one, 0.0f, s_.holesMask);
    const Collider2D* b = physics_.OverlapBox(v - Vector2{n, n}, one, 0.0f, s_.holesMask);
    const Collider2D* c = physics_.OverlapBox(v + Vector2{-1.0f, 1.0f} * n, one, 0.0f, s_.holesMask);
    const Collider2D* d = physics_.OverlapBox(v + Vector2{1.0f, -1.0f} * n, one, 0.0f, s_.holesMask);
    if (a && b && c && d) {
        if (!isFalling && (!isIntangible || isHolesIntangible)) {
            PlayFallAnimation();
            return true;
        }
    } else {
        lastHolesFreePosition = position;
    }
    return false;
}

void HeroMerchantController::PlayFallAnimation()
{
    if (!canMove) return;
    canMove = false; // ForbidMovement
    velX = velY = 0.0f;
    isMoveInputEnabled = false;
    ResetSpeedFactor();
    animator.SetTrigger("BeginFall");
    isFalling = true;
    timePushingHoles = 0.0f;
    SetIntangibleMode();
    const float time = 0.6f;
    // A tile on the way he faces (the next one if he's less than half into it), as he goes.
    tweens.DelayedCall(time * 0.1f, [this, time] {
        Vector2 v = position;
        const float num = s_.tileSize;
        Vector2 zero;
        if (heroDirection == Left) zero.x = std::fmod(0.0f - v.x, num);
        if (heroDirection == Front) zero.y = std::fmod(0.0f - v.y, num);
        if (heroDirection == Back) zero.y = std::fmod(v.y, num);
        if (heroDirection == Right) zero.x = std::fmod(v.x, num);
        if (std::max(std::fabs(zero.x), std::fabs(zero.y)) < num * 0.5f) {
            // NormalizeOneDimensional
            Vector2 unit = std::max(std::fabs(zero.x), std::fabs(zero.y)) != std::fabs(zero.x) ? Vector2{0.0f, Sign(zero.y)} : Vector2{Sign(zero.x), 0.0f};
            zero = zero + Vector2{num * unit.x, num * unit.y};
        }
        v = v + zero;
        tweens.Move([this] { return position; }, [this](Vector2 p) { position = p; }, v, time * 0.9f);
    });
    // The art's spin (0.2 s on, 0.6 s): its end is the fall's.
    tweens.Timer(time, 0.2f, [this] {
        animator.SetTrigger("EndFall");
        isFalling = false;
        EnableMoveInput();
        canMove = true; // AllowMovement
        TeleportTo(roomIsInit ? lastHolesFreePosition : respawn);
        SetDefaultMode();
    });
}

void HeroMerchantController::SetIntangibleMode(int isHolesEnabled)
{
    isHolesIntangible = isHolesEnabled > 0;
    if (physicsWorld) physicsWorld->IgnoreLayerCollision(unity::kLayerIntangibleHero, unity::kLayerSunkenObstacle, !isHolesIntangible);
    layer = unity::kLayerIntangibleHero;
    isIntangible = true;
}

void HeroMerchantController::SetDefaultMode()
{
    isIntangible = false;
    isHolesIntangible = false;
    if (!CheckForHoles()) layer = unity::kLayerDefault; // over a hole still: he falls again
}

void HeroMerchantController::EnableMoveInput()
{
    isMoveInputEnabled = true;
    isHorizontalMoveInputEnabled = true;
    isVerticalMoveInputEnabled = true;
}

int HeroMerchantController::AddSpeedEffectModifier(float multiplier)
{
    ResetSpeedFactor();
    speedFactor *= multiplier;
    speedEffects_.push_back({nextEffectId_, multiplier});
    return nextEffectId_++;
}

void HeroMerchantController::ResetSpeedFactor()
{
    for (const SpeedEffect& e : speedEffects_) restoreAtFrameEnd_.push_back(e.multiplier);
    speedEffects_.clear();
}

void HeroMerchantController::DestroySpeedEffect(int id)
{
    for (size_t i = 0; i < speedEffects_.size(); i++)
        if (speedEffects_[i].id == id) {
            restoreAtFrameEnd_.push_back(speedEffects_[i].multiplier);
            speedEffects_.erase(speedEffects_.begin() + static_cast<long>(i));
            return;
        }
}

} // namespace game
