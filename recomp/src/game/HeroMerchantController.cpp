#include "game/HeroMerchantController.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "game/Constants.h"
#include "game/Prefabs.h"
#include "game/Weapon.h"

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

// The states SMAnimatorHandler's registrations name (HeroMerchantController.Start).
bool IsStateIn(int32_t hash, const char* const* names, size_t count)
{
    for (size_t i = 0; i < count; i++)
        if (unity::StringToHash(names[i]) == hash) return true;
    return false;
}

// The main attacks' states: <Type>Main{First,Second,Third}<Dir> (the gloves' <...>Punch<Dir>)
// and the bow's BowAttackAnimation<Dir>.
bool IsMainAttackState(int32_t hash)
{
    static const std::vector<int32_t> kHashes = [] {
        std::vector<int32_t> h;
        static const char* const kDirs[4] = {"Down", "Right", "Left", "Up"};
        static const char* const kHits[3] = {"First", "Second", "Third"};
        for (std::string type : {"ShortSword", "BigSword", "Spear", "Gloves"})
            for (const char* hit : kHits)
                for (const char* dir : kDirs) h.push_back(unity::StringToHash(type + "Main" + hit + (type == "Gloves" ? "Punch" : "") + dir));
        for (const char* dir : kDirs) h.push_back(unity::StringToHash(std::string("BowAttackAnimation") + dir));
        return h;
    }();
    return std::find(kHashes.begin(), kHashes.end(), hash) != kHashes.end();
}

// By WeaponType: the main attack's trigger and the secondary attack's bool.
const char* const kMainAttackTrigger[6] = {"", "ShortSwordMainAttack", "BigSwordMainAttack", "SpearMainAttack", "GlovesMainAttack", "BowMainAttack"};
const char* const kSecondaryAttackBool[6] = {"", "ShortSwordSecondaryAttack", "BigSwordSecondaryAttack", "SpearSecondaryAttack",
                                             "GlovesSecondaryAttack", "BowSecondaryAttack"};

bool IsIdleOrWalkState(int32_t hash)
{
    static const char* const kNames[8] = {"IdleDown", "IdleUp", "IdleRight", "IdleLeft", "WalkDown", "WalkUp", "WalkLeft", "WalkRight"};
    return IsStateIn(hash, kNames, 8);
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
    static const char* const kWindows[6] = {nullptr, "shortSword", "bigSword", "spear", "gloves", "bow"};
    for (int i = 1; i < 6; i++) s.attackToAttackWindow[i] = Constants::GetFloat(std::string(kWindows[i]) + "AttackToAttackNormalizedTimeWindow");
    s.mainAttackToSecondaryWindow = Constants::GetFloat("mainAttackToSecondaryAttackNormalizedTimeWindow");
    s.attackToRollWindow = Constants::GetFloat("attackToRollNormalizedTimeWindow");
    s.moveTimeWhileAttacking = Constants::GetFloat("moveTimeWhileAttacking");
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
    // HeroMerchantStats.Init, and its health's subscribers in the order they subscribe: the
    // stats' HealthValueChanged, the controller's OnHealthChanged, the health bar's PlayAnimation.
    stats.Init();
    previousHealth_ = stats.currentHealth.value;
    stats.currentHealth.onStateUpdated = [this](float v) {
        if (!stats.isDead && v <= stats.currentHealth.minValue) Die(); // HealthValueChanged
        OnHealthChanged(v);
        if (v - stats.currentHealth.value < 0.0f) PlayDamagedEffect(); // WillHealthBar.PlayAnimation
    };
    // The health bar's other two: the absorption slider going down, each shield image removed.
    stats.absorption.onStateUpdated = [this](float v) {
        if (v < stats.absorption.value) PlayDamagedEffect();
    };
    stats.shield.onStateUpdated = [this](float v) {
        for (int n = static_cast<int>(stats.shield.value); n > static_cast<int>(v); n--) PlayDamagedEffect();
    };
    if (Constants::GetFloat("willFallDamage") > 0.0f) { // (the table loaded)
        willFallDamage_ = Constants::GetFloat("willFallDamage");
        willInvincibleBlinkDuration_ = Constants::GetFloat("willInvincibleBlinkDuration");
        willDamagedRedTime_ = Constants::GetFloat("willDamagedRedTime");
        willDamagedWhiteTime_ = Constants::GetFloat("willDamagedWhiteTime");
    }
    // SMAnimatorHandler's registrations (Start): the roll's, the main attacks', Idle's and Walk's.
    animator.onStateEnter = [this](int32_t hash) {
        if (IsRollState(hash)) animator.speed = 1.25f; // AbilityUseStarted
        if (IsMainAttackState(hash)) MainAttackUseStart();
        if (IsIdleOrWalkState(hash)) IdleStateStart(); // (WalkStateStart is IdleStateStart)
    };
    animator.onStateExit = [this](int32_t hash) {
        if (IsMainAttackState(hash)) MainAttackUseEnd();
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
        } else if (e.function == "OnWillDeathAnimationEnded") {
            deathAnimationEnded = true;
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

    // Dead, the game is paused: only his Animator runs (the death's clip).
    if (stats.isDead) {
        animator.Update(input.unscaledDeltaTime < 0.0f ? deltaTime : input.unscaledDeltaTime);
        return;
    }

    // DOTween's update runs before his (a tween he starts this frame first moves the next).
    tweens.Update(deltaTime, input.unscaledDeltaTime < 0.0f ? deltaTime : input.unscaledDeltaTime);

    stick_ = input.stick;
    const Vector2 stick = input.stick;
    now_ += deltaTime;
    if (!abilityAvailable && now_ >= abilityReadyAt_) abilityAvailable = true; // MakeAbilityAvailable

    // Update: the input direction's bools.
    static const char* const kInput[4] = {"LeftInput", "RightInput", "UpInput", "DownInput"};
    inputDirection = ParseInputToDirection(stick, stick.x, stick.y);
    for (int d = 0; d < 4; d++) animator.SetBool(kInput[d], d == static_cast<int>(inputDirection));

    // Last frame's velocity moves him.
    if (std::fabs(velX) >= s_.lowerDeadZone || std::fabs(velY) >= s_.lowerDeadZone || IsPlayingMovingMainAttackAnimation() ||
        IsPlayingRollAnimation()) {
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

    // The main attack (Action1): the weapon type's trigger, unless a swing is still early on or
    // a secondary attack or a roll is playing; bare-handed, the gloves'.
    if (input.attack && canMove && canAttack && attackInputEnabled && !isFalling) {
        if (!weapon) {
            animator.SetTrigger("GlovesMainAttack");
            isMoveInputEnabled = false; // DisableMoveInput
            velX = velY = 0.0f;
        } else if (CanAttackAgain() && weapon->type != WeaponType::None) {
            animator.SetTrigger(kMainAttackTrigger[static_cast<int>(weapon->type)]);
        }
    }

    // The secondary attack's part (Action3 isn't ported: never pressed): late in a swing the
    // weapon's Animator is told it may follow with one; the type's bool stays off.
    if (canMove && canAttack && attackInputEnabled && !IsPlayingRollAnimation() && !isFalling && weapon) {
        const bool main = IsPlayingMainAttackAnimation(), late = IsPlayingMainAttackAnimationToSecondaryAttackTimeWindow();
        if ((main && late) || !main) {
            if (late) weapon->animator.SetTrigger("mainAttackToSecondaryAttack");
            if (weapon->type != WeaponType::None) animator.SetBool(kSecondaryAttackBool[static_cast<int>(weapon->type)], false);
        }
    }

    // The roll (LeftTrigger): the equipped ability, unless one's already playing or a swing is
    // early on.
    if (canMove && canAttack && input.roll && !isFalling && abilityAvailable && CanRollFromAttack()) UseAbility(stick);

    // Objects destroyed during Update go before the animation pass; those the Animator's events
    // destroy wait for the end of the frame (after LateUpdate).
    for (float m : restoreAtFrameEnd_) speedFactor *= 1.0f / m;
    restoreAtFrameEnd_.clear();
    // The weapon's Animator (a child's) runs before his: what his state callbacks set on it
    // takes effect the next frame.
    if (weapon) weapon->Update(input.unscaledDeltaTime < 0.0f ? deltaTime : input.unscaledDeltaTime);
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
        // Only an open door takes him (and resets the timer): pushing a closed one keeps it
        // running, so he goes through the frame it opens.
        if (timePushingDoors > maxTimePushingDoors && door && (!isDoorOpened || isDoorOpened(*door))) {
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
    // A roll ends the damaged effect (and with it the hit's intangibility).
    if (playingDamagedEffect) {
        KillDamagedEffectTweens();
        SetPlayingDamagedEffectToFalse();
        flashAmount = 0.0f; // SetPlayerShaderToDefault
        flashTinted = false;
        EnableHeroMerchantArt();
    }
    if (weapon) weapon->animator.SetTrigger("ResetWeapon");
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

bool HeroMerchantController::IsPlayingMovingMainAttackAnimation() const
{
    return IsTag("SwordAndShieldMainAttack") || IsTag("BigSwordMainAttack") || IsTag("SpearMainAttack") || IsTag("GlovesMainAttack");
}

bool HeroMerchantController::IsPlayingMainAttackAnimation() const
{
    return IsPlayingMovingMainAttackAnimation() || IsTag("BowMainAttack");
}

bool HeroMerchantController::IsPlayingMainAttackAnimationToSecondaryAttackTimeWindow() const
{
    return IsPlayingMainAttackAnimation() && animator.GetCurrentAnimatorStateInfo().normalizedTime >= s_.mainAttackToSecondaryWindow;
}

// Update's Action1 test: not early in a swing (each type's window), not in a secondary attack,
// a bow shot or a roll.
bool HeroMerchantController::CanAttackAgain() const
{
    const float t = animator.GetCurrentAnimatorStateInfo().normalizedTime;
    using T = WeaponType;
    auto early = [&](const char* tag, T type) { return IsTag(tag) && t <= s_.attackToAttackWindow[static_cast<int>(type)]; };
    return !early("SwordAndShieldMainAttack", T::ShortSword) && !IsTag("SwordAndShieldSecondaryAttack") &&
           !early("BigSwordMainAttack", T::BigSword) && !IsTag("BigSwordSecondaryAttack") && !early("SpearMainAttack", T::Spear) &&
           !IsTag("SpearSecondaryAttack") && !early("GlovesMainAttack", T::Gloves) && !IsTag("GlovesSecondaryAttack") &&
           !IsTag("BowMainAttack") && !IsPlayingRollAnimation() && !IsTag("BowSecondaryAttack");
}

// Update's LeftTrigger test: not early in a swing (attackToRoll), in a secondary attack or a
// roll. The game's tags as written: "SwordAndShieldSecondaryMainAttack" and "GlovesMainUp" are
// no state's, so the shield's secondary and the gloves' punches don't stop a roll.
bool HeroMerchantController::CanRollFromAttack() const
{
    const float t = animator.GetCurrentAnimatorStateInfo().normalizedTime;
    auto early = [&](const char* tag) { return IsTag(tag) && t < s_.attackToRollWindow; };
    return !early("SwordAndShieldMainAttack") && !IsTag("SwordAndShieldSecondaryMainAttack") && !early("BigSwordMainAttack") &&
           !IsTag("BigSwordSecondaryAttack") && !early("SpearMainAttack") && !IsTag("SpearSecondaryAttack") && !early("GlovesMainUp") &&
           !IsTag("GlovesSecondaryAttack") && !early("BowMainAttack") && !IsTag("BowSecondaryAttack") && !IsTag("Ability");
}

// Move<Dir>WithAttackIfInputDirection: a step his way if the stick points that way (velocity 1
// for 0.1 s), and the axis stopped after `time` (both DOVirtual.DelayedCalls).
void HeroMerchantController::MoveWithAttackIfInputDirection(Direction direction, float time)
{
    if (moveWillWhileAttackingTween_ && tweens.IsPlaying(moveWillWhileAttackingTween_)) tweens.Complete(moveWillWhileAttackingTween_);
    const bool horizontal = direction == Left || direction == Right;
    auto stop = [this, horizontal] {
        if (IsPlayingRollAnimation()) return; // StopMoving*: not during a roll
        if (horizontal) velX = 0.0f;
        else velY = 0.0f;
    };
    // Mathf.Approximately(v, 0): only (sub)normal zeros.
    if (inputDirection == direction && (stick_.x != 0.0f || stick_.y != 0.0f)) {
        if (direction == Left) velX = -1.0f;
        if (direction == Right) velX = 1.0f;
        if (direction == Back) velY = 1.0f;
        if (direction == Front) velY = -1.0f;
        tweens.DelayedCall(0.1f, stop);
    }
    moveWillWhileAttackingTween_ = tweens.DelayedCall(time, stop);
}

void HeroMerchantController::MainAttackUseStart()
{
    ResetSpeedFactor();
    velX = velY = 0.0f; // DisableMoveInput
    isMoveInputEnabled = false;
    MoveWithAttackIfInputDirection(heroDirection, s_.moveTimeWhileAttacking);
    static const char* const kTriggers[4] = {"MainAttackLeft", "MainAttackRight", "MainAttackUp", "MainAttackDown"}; // by Direction
    if (weapon)
        for (const char* t : kTriggers) weapon->animator.ResetTrigger(t);
    animator.speed = 1.0f;
    if (weapon) {
        weapon->animator.SetTrigger(kTriggers[heroDirection]);
        weapon->animator.SetInteger("ComboCounter", comboCounter);
    }
    comboCounter = std::min(comboCounter + 1, maxComboCounter); // IncreaseComboCounter (the property caps it)
    animator.SetInteger("ComboCounter", comboCounter);
}

void HeroMerchantController::MainAttackUseEnd()
{
    if (!IsPlayingRollAnimation() && !IsTag("SpearSecondaryAttack")) EnableMoveInput();
    SetDirection(inputDirection);
    for (const char* t : {"ShortSwordMainAttack", "BigSwordMainAttack", "SpearMainAttack", "GlovesMainAttack", "BowMainAttack"})
        animator.ResetTrigger(t);
    if (weapon)
        for (const char* t : {"MainAttackUp", "MainAttackDown", "MainAttackLeft", "MainAttackRight"}) weapon->animator.ResetTrigger(t);
}

// ResetCurrentWeaponAnimator: the weapon's direction triggers off, its secondary bools false.
void HeroMerchantController::ResetCurrentWeaponAnimator()
{
    if (!weapon) return;
    for (const char* dir : {"Down", "Left", "Up", "Right"}) {
        weapon->animator.ResetTrigger(std::string("MainAttack") + dir);
        weapon->animator.SetBool(std::string("SecondaryAttack") + dir, false);
    }
}

void HeroMerchantController::IdleStateStart()
{
    comboCounter = 0; // ResetComboCounter
    animator.SetInteger("ComboCounter", comboCounter);
    if (weapon) weapon->animator.SetInteger("ComboCounter", comboCounter);
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
    stats.SetInvencible(tweens);
    ResetCurrentWeaponAnimator();
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
        stats.SetVencible(tweens);
        animator.SetTrigger("EndFall");
        isFalling = false;
        EnableMoveInput();
        canMove = true; // AllowMovement
        TeleportTo(roomIsInit ? lastHolesFreePosition : respawn);
        // The fall's cost (no BlackCatAmulet; no ArmorAmulet's extra).
        const int damage = static_cast<int>(std::max(1.0f, stats.currentHealth.maxValue * willFallDamage_));
        DealDamageToPlayer(static_cast<float>(damage), "Fall");
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
    if (!CheckForHoles()) { // over a hole still: he falls again
        layer = unity::kLayerDefault;
        stats.currentHealth.variableStat = true;
    }
}

void HeroMerchantController::DealDamageToPlayer(float attackStrength, const char* damageSource, DamageType type)
{
    (void)damageSource;
    if (stats.isDead) return;
    const float totalDamage = type != DamageType::Self ? CalcHitDamage(attackStrength, stats.defense.value) : attackStrength;
    const float num2 = stats.currentHealth.value + stats.absorption.value;
    float num3 = num2 - (num2 - totalDamage);
    num3 = num3 < 0.0f ? 0.0f : num3 > num2 ? num2 : num3;
    auto hit = [&] {
        if (type == DamageType::Self) return;
        stats.SetInvencible(tweens, 1.0f);
        StartPlayingInvencibilityEffect(1.0f, 0.2f);
        SetIntangibleMode();
    };
    // A shield takes the whole hit; absorption takes what it can; the health the rest.
    if (stats.shield.value > 0.0f) {
        stats.shield.Set(stats.shield.value - 1.0f);
        num3 = 0.0f;
        hit();
    }
    if (num3 > 0.0f && stats.absorption.value > 0.0f) {
        const float rest = std::max(0.0f, num3 - stats.absorption.value);
        stats.absorption.Set(stats.absorption.value - num3);
        num3 = rest;
        hit();
    }
    if (num3 > 0.0f) {
        stats.currentHealth.Set(stats.currentHealth.value - num3);
        hit();
    }
}

// HeroMerchantStats.Die and PlayDeathAnimation: stopped, the weapon reset, Death, invincible.
void HeroMerchantController::Die()
{
    stats.isDead = true;
    velX = velY = 0.0f; // StopMovement
    canMove = false;
    isMoveInputEnabled = false;
    ResetCurrentWeaponAnimator();
    EnableHeroMerchantArt();
    animator.SetTrigger("Death");
    stats.SetInvencible(tweens);
    ResetSpeedFactor();
}

// Stat's subscriber: a drop sets the Animator's Damaged trigger (no transition reads it).
void HeroMerchantController::OnHealthChanged(float newHealth)
{
    if (previousHealth_ > newHealth) animator.SetTrigger("Damaged");
    previousHealth_ = newHealth;
}

void HeroMerchantController::KillDamagedEffectTweens()
{
    for (int* t : {&tweenColorWhite_, &tweenColorRed_, &tweenColorDefault_, &tweenHidePlayerFromInvincible_, &tweenSetPlayerDamagedEffectToFalse_}) {
        tweens.Kill(*t);
        *t = 0;
    }
}

// The health bar's colour flash, unscaled (DOVirtual's default): red, white, red, then the
// default; the effect (and the hit's intangibility) ends 10 reds later.
void HeroMerchantController::PlayDamagedEffect()
{
    playingDamagedEffect = true;
    KillDamagedEffectTweens();
    flashAmount = 1.0f;
    flashTinted = true;
    float t = willDamagedRedTime_;
    tweenColorWhite_ = tweens.DelayedCall(t, [this] { flashTinted = false; });
    t += willDamagedWhiteTime_;
    tweenColorRed_ = tweens.DelayedCall(t, [this] { flashTinted = true; });
    t += willDamagedRedTime_;
    tweenColorDefault_ = tweens.DelayedCall(t, [this] { flashAmount = 0.0f; flashTinted = false; });
    t += 10.0f * willDamagedRedTime_;
    tweenSetPlayerDamagedEffectToFalse_ = tweens.DelayedCall(t, [this] { SetPlayingDamagedEffectToFalse(); });
}

void HeroMerchantController::SetPlayingDamagedEffectToFalse()
{
    playingDamagedEffect = false;
    if (!abilityInUse && stats.currentHealth.value > 0.0f) SetDefaultMode();
}

// The blink (scaled): the art off after `delay`, back on and off every blink duration, on for
// good at `time`.
void HeroMerchantController::StartPlayingInvencibilityEffect(float time, float delay)
{
    tweenDelayedHide_ = tweens.DelayedCall(delay, [this] { HidePlayerArtFromInvincibleEffect(); }, false);
    tweens.DelayedCall(time, [this] { StopPlayingInvencibilityEffect(); }, false);
}

void HeroMerchantController::HidePlayerArtFromInvincibleEffect()
{
    if (stats.isDead) return;
    DisableHeroMerchantArt();
    tweenHidePlayerFromInvincible_ = tweens.DelayedCall(willInvincibleBlinkDuration_, [this] { ShowPlayerArtFromInvincibleEffect(); }, false);
}

void HeroMerchantController::ShowPlayerArtFromInvincibleEffect()
{
    if (stats.isDead) return;
    EnableHeroMerchantArt();
    tweenShowPlayerFromInvincible_ = tweens.DelayedCall(willInvincibleBlinkDuration_, [this] { HidePlayerArtFromInvincibleEffect(); }, false);
}

void HeroMerchantController::StopPlayingInvencibilityEffect()
{
    if (stats.isDead) return;
    EnableHeroMerchantArt();
    flashTinted = false; // SetPlayerShaderColorToWhite
    flashAmount = 0.0f;
}

// (Showing or hiding the art kills the blink's tweens: the caller schedules the next.)
void HeroMerchantController::EnableHeroMerchantArt()
{
    artVisible = true;
    KillInvencibleEffectTweens();
}

void HeroMerchantController::DisableHeroMerchantArt()
{
    artVisible = false;
    KillInvencibleEffectTweens();
}

void HeroMerchantController::KillInvencibleEffectTweens()
{
    for (int* t : {&tweenDelayedHide_, &tweenHidePlayerFromInvincible_, &tweenShowPlayerFromInvincible_}) {
        tweens.Kill(*t);
        *t = 0;
    }
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
