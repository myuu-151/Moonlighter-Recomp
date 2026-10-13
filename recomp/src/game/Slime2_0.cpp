#include "game/Slime2_0.h"

#include <cmath>

#include "game/MasterAudio.h"
#include "unity/DelayedCalls.h"
#include "unity/Mathf.h"
#include "unity/Physics2D.h"
#include "unity/Random.h"

namespace game {

using unity::Vector2;
namespace Mathf = unity::Mathf;
using unity::mono::D;
using unity::mono::F;

namespace {

// LayerMask.NameToLayer of the layers ReleasePlayer's raycast and the trap use.
constexpr int kLayerRisenObstacle = 8, kLayerLevelBoundary = 11, kLayerEnemyBoundary = 21;

// Vector3.Distance(a, b) of two transforms (z 0): the difference a new Vector3 (each component a
// float), Mathf.Sqrt of the sum of squares (a float argument, then (float)Math.Sqrt).
float Distance3(Vector2 a, Vector2 b)
{
    const float x = a.x - b.x, y = a.y - b.y, z = 0.0f - 0.0f;
    const float sum = F(D(x) * D(x) + D(y) * D(y) + D(z) * D(z));
    return F(std::sqrt(D(sum)));
}

} // namespace

void Slime2_0Behaviour::Init()
{
    // willAbsorbedPoint = transform.Find("WillAbsorbedPoint") (MakeSlime2_0Behaviour found it).
    if (HeroTrap* trap = Trap()) trap->SubscribeOnDie(this, [this] { ReleasePlayerInmediatelly(); });
    // body = GetComponent<Rigidbody2D>() (the enemy's).
    EnemyBehaviour::Init();
}

void Slime2_0Behaviour::OnDisable() { ReleaseWithoutAnimation(); }

void Slime2_0Behaviour::OnDeath()
{
    ReleaseWithoutAnimation();
    // EnemyBehaviour.OnDeath ends with `enabled = false`, which runs OnDisable (the object on).
    const bool wasEnabled = enabled;
    EnemyBehaviour::OnDeath();
    if (wasEnabled && !enabled && enemy.active && !enemy.destroyed) OnDisable();
    if (registerDeath) registerDeath(slimeCulture);
}

void Slime2_0Behaviour::ReleaseWithoutAnimation()
{
    Notify(Event::ReleaseWithoutAnimation, false);
    hasHeroMerchantTrapped = false;
    // HeroMerchant.Instance (set) and DungeonManager.Instance.currentDungeonRoom == enemy.enemyRoom.
    HeroTrap* trap = Trap();
    if (trap && (!trap->inEnemyRoom || trap->inEnemyRoom(enemy))) {
        trap->AllowMovement();
        trap->AllowAttack();
        trap->isTrapped = false;
        trap->DisableAbilityTrail();
    }
    if (trap) trap->UnsubscribeOnDie(this);
    Notify(Event::ReleaseWithoutAnimation, true);
}

float Slime2_0Behaviour::Realtime(const char* method)
{
    if (realtimeSinceStartup) return realtimeSinceStartup(method);
    return enemy.scene.calls ? static_cast<float>(enemy.scene.calls->now) : 0.0f;
}

Vector2 Slime2_0Behaviour::WillAbsorbedPoint() const
{
    // willAbsorbedPoint.position: the root's world position through its local one (no rotation,
    // scale 1: each component one float addition).
    const Vector2 p = enemy.position();
    return {p.x + willAbsorbedPointLocal.x, p.y + willAbsorbedPointLocal.y};
}

unity::Rect Slime2_0Behaviour::RoomBounds() const
{
    // DungeonRoom.GetRoomBounds: transform.position + (0, roomCenterYOffset, 0), then
    // new Rect(x - (float)roomFloorWidth * 0.5f, y - (float)roomFloorHeight * 0.5f, width, height).
    const EnemyScene& s = enemy.scene;
    const float px = s.roomPosition.x + 0.0f;
    const float py = s.roomPosition.y + static_cast<float>(s.roomCenterYOffset);
    const float w = static_cast<float>(s.roomFloorWidth), h = static_cast<float>(s.roomFloorHeight);
    unity::Rect r;
    r.x = F(D(px) - D(w) * 0.5);
    r.y = F(D(py) - D(h) * 0.5);
    r.width = w;
    r.height = h;
    return r;
}

void Slime2_0Behaviour::SetLayer(int layer)
{
    // gameObject.layer (the root's: its body; the AttackCollider child keeps EnemyAttack).
    enemy.layer = layer;
    if (enemy.physics) enemy.physics->SetLayer(enemy, layer);
}

void Slime2_0Behaviour::StopBody()
{
    if (enemy.body) enemy.body->SetVelocity(Vector2::zero());
}

void Slime2_0Behaviour::ChooseAndApplyAction(float deltaTime)
{
    HeroTrap* trap = Trap();
    const bool trapped = trap && trap->isTrapped;
    if (enemyState != EnemyState::Attacking) {
        if (!trapped) {
            if (!isDashing && !isAttacking && Distance3(enemy.position(), HeroPosition()) < dashRadius) {
                isDashing = true;
                EnemyDashTowardsThePlayer(dashForce);
                if (enemy.scene.calls) enemy.scene.calls->Invoke(this, "EnableDash", dashDisabledTime, [this] { EnableDash(); });
            } else {
                MoveEnemy(deltaTime);
            }
        } else {
            MoveEnemy(deltaTime);
        }
        return;
    }
    if (HeroDead()) return;
    Notify(Event::Trapped, false);
    if (trap) {
        trap->MoveAutomaticallyToPoint(WillAbsorbedPoint(), 0.0f, false);
        HeroTrap::Direction direction = HeroTrap::Direction::Right;
        const Vector2 normalized = unity::Normalized(trap->stick);
        const float num = Mathf::Abs(normalized.x);
        const float num2 = Mathf::Abs(normalized.y);
        if (num < num2) direction = !(normalized.y > 0.0f) ? HeroTrap::Direction::Front : HeroTrap::Direction::Back;
        else direction = normalized.x > 0.0f ? HeroTrap::Direction::Right : HeroTrap::Direction::Left;
        trap->SetDirection(direction);
        if (!isReleasing && (Mathf::Sign(trap->stick.x) != Mathf::Sign(trap->lastStick.x) || Mathf::Sign(trap->stick.y) != Mathf::Sign(trap->lastStick.y))) {
            buttonCount++;
            if (buttonCount >= buttonCountThreshold) {
                ReleasePlayerInmediatelly();
                buttonCount = 0;
            }
        }
    }
    Notify(Event::Trapped, true);
}

void Slime2_0Behaviour::EnableDash()
{
    Notify(Event::EnableDash, false);
    if (enemy.scene.calls) enemy.scene.calls->CancelInvoke(this, "EnableDash");
    isDashing = false;
    Notify(Event::EnableDash, true);
}

void Slime2_0Behaviour::AttackHit(bool otherIsPlayer) { AttackHitTag(otherIsPlayer ? "Player" : ""); }

void Slime2_0Behaviour::AttackHitTag(const std::string& tag)
{
    Notify(Event::Hit, false);
    lastAttackHitTag = tag;
    hasLastAttackHit = true;
    if (tag == "Hole") {
        StopBody(); // enemy.GetComponent<Rigidbody2D>().velocity = Vector2.zero
    } else if (tag == "Player") {
        HeroTrap* trap = Trap();
        const bool trapped = trap && trap->isTrapped;
        if (isAttacking) {
            EnemyBehaviour::AttackHit(true);
        } else if (trapped || (!trapped && unity::Random::value() < 0.3f)) {
            if (!HeroDead() && D(Realtime("AttackHit")) - D(timeLastAttack) > D(timeBetweenAttacks) && enemy.enemyStats.isAlive())
                PlayMeleeAnimation();
        } else {
            TryAbsorbPlayer();
        }
    }
    Notify(Event::Hit, true);
}

void Slime2_0Behaviour::PlayMeleeAnimation()
{
    Notify(Event::Melee, false);
    ForbidEnemyMovement();
    StopBody();
    isAttacking = true;
    timeLastAttack = Realtime("PlayMeleeAnimation");
    SetAnimatorTrigger("MeleeAttack", true);
    Notify(Event::Melee, true);
}

void Slime2_0Behaviour::FinishMeleeAttack()
{
    isAttacking = false;
    AllowEnemyMovement();
}

void Slime2_0Behaviour::EnemyWasHit(float newHealthValue)
{
    EnemyBehaviour::EnemyWasHit(newHealthValue);
    // (Inside CurrentHealth's setter: the health is still the old one.)
    if (newHealthValue < enemy.enemyStats.CurrentHealth()) {
        static const char* const kSound = "golem_dungeon_slime_hit";
        if (enemy.scene.audio) enemy.scene.audio->PlaySound(kSound);
        if (enemy.scene.playSound) enemy.scene.playSound(enemy, kSound);
    }
}

void Slime2_0Behaviour::TryAbsorbPlayer()
{
    Notify(Event::Absorb, false);
    HeroTrap* trap = Trap();
    if (!HeroDead() && D(Realtime("TryAbsorbPlayer")) - D(timeLastExpell) > D(timeBetweenAbsortions) && enemy.enemyStats.isAlive() &&
        !(trap && trap->isTrapped) && !(trap && trap->abilityInUse)) {
        isDashing = true;
        if (trap) trap->isTrapped = true;
        SetAnimatorTrigger("Absorb", true);
        hasHeroMerchantTrapped = true;
        enemyState = EnemyState::Attacking;
        if (trap) {
            trap->ForbidMovement();
            trap->ForbidAttack();
            trap->ChangeToDungeonMode();
            trap->ResetCurrentWeaponAnimator();
            trap->DisableHeroMerchantArt();
            trap->EnableHeroMerchantArt();
        }
        SetLayer(kLayerTransparentFX);
        // GetComponentInChildren<SpriteDepthOrderRender>().SetOver(the hero's); overOffset = 10.
        depthOverHero = true;
        depthOverOffset = 10;
        StopBody();
        buttonCount = 0;
        isReleasing = false;
    }
    Notify(Event::Absorb, true);
}

void Slime2_0Behaviour::ReleasePlayerInmediatelly()
{
    Notify(Event::ReleaseNow, false);
    if (hasHeroMerchantTrapped) {
        isReleasing = true;
        SetAnimatorTrigger("Release", true);
    }
    Notify(Event::ReleaseNow, true);
}

Vector2 Slime2_0Behaviour::GetRandomCircleBorderPoint(float radius)
{
    // float f = Random.Range(0, 360) (the int overload); new Vector3(Mathf.Cos(f), Mathf.Sin(f), 0)
    // * radius: f in radians (the game's), each component rounded.
    lastBorderAngle = unity::Random::Range(0, 360);
    const float f = static_cast<float>(lastBorderAngle);
    const float c = F(std::cos(D(f))), s = F(std::sin(D(f)));
    return {c * radius, s * radius};
}

void Slime2_0Behaviour::ReleasePlayer()
{
    Notify(Event::Release, false);
    if (!hasHeroMerchantTrapped) {
        Notify(Event::Release, true);
        return;
    }
    HeroTrap* trap = Trap();
    hasHeroMerchantTrapped = false;
    enemyState = EnemyState::PursuingPlayer;
    if (trap) {
        trap->AllowMovement();
        trap->AllowAttack();
        trap->DisableAbilityTrail();
        trap->isTrapped = false;
    }
    depthOverHero = false; // SetOver(null)
    SetLayer(kLayerGroundEnemy);
    const float time = 0.1f;
    const float num = 40.0f;
    Vector2 vector = GetRandomCircleBorderPoint(num);
    const int layerMask = (1 << kLayerLevelBoundary) | (1 << kLayerEnemyBoundary) | (1 << kLayerRisenObstacle);
    int num2 = 0;
    const unity::Physics2D* queries = enemy.scene.queries;
    auto hits = [&]() { return queries && queries->Raycast(enemy.position(), vector, num, layerMask).collider != nullptr; };
    while (hits() && num2 < 100) {
        num2++;
        vector = GetRandomCircleBorderPoint(num);
    }
    if (num2 >= 100) vector = Vector2::zero();
    vector = enemy.position() + vector;
    if (enemy.hasEnemyRoom) {
        const unity::Rect roomBounds = RoomBounds();
        const float num3 = 10.0f;
        const float xMax = roomBounds.width + roomBounds.x, yMax = roomBounds.height + roomBounds.y;
        vector.x = Mathf::Clamp(vector.x, roomBounds.x + num3, xMax - num3);
        vector.y = Mathf::Clamp(vector.y, roomBounds.y + num3, yMax - num3);
    }
    lastReleaseRedraws = num2;
    lastReleasePoint = vector;
    if (trap) trap->MoveAutomaticallyToPoint(vector, time, true);
    if (!HeroDead() && hasEffectOnAttack && trap && trap->hasHealthStat && trap->onCreateEffect) trap->onCreateEffect(enemy);
    timeLastExpell = Realtime("ReleasePlayer");
    isDashing = true;
    if (unity::DelayedCalls* calls = enemy.scene.calls) {
        calls->CancelInvoke(this, "EnableDash");
        calls->Invoke(this, "EnableDash", dashDisabledTime, [this] { EnableDash(); });
    }
    Notify(Event::Release, true);
}

bool Slime2_0Behaviour::OnAnimationEvent(const unity::AnimationEvent& e)
{
    const std::string& f = e.function;
    if (f == "ReleasePlayer") ReleasePlayer();
    else if (f == "FinishMeleeAttack") FinishMeleeAttack();
    else if (f == "EnableDash") EnableDash();
    else if (f == "ReleasePlayerInmediatelly") ReleasePlayerInmediatelly();
    else if (f == "PlaySlimeSwallow" || f == "PlaySlimeAttack" || f == "PlaySlimePop") {
        // SoundsSlime (the same object): MasterAudio.PlaySound's draws.
        const char* sound = f == "PlaySlimeSwallow" ? "golem_dungeon_slime_swallow" : f == "PlaySlimeAttack" ? "golem_dungeon_slime_attack"
                                                                                                            : "golem_dungeon_slime_pop";
        if (enemy.scene.audio) enemy.scene.audio->PlaySound(sound);
        if (enemy.scene.playSound) enemy.scene.playSound(enemy, sound);
    } else {
        return EnemyBehaviour::OnAnimationEvent(e);
    }
    return true;
}

std::unique_ptr<Slime2_0Behaviour> MakeSlime2_0Behaviour(Enemy& enemy, const Behaviour& fields, const Prefab& prefab)
{
    auto slime = std::make_unique<Slime2_0Behaviour>(enemy);
    slime->slimeCulture = static_cast<EnemyBehaviour::CommonEnemyCulture>(static_cast<int>(fields.Field("slimeCulture", 0.0)));
    slime->hasEffectOnAttack = fields.Field("effectOnAttack", 0.0) != 0.0;
    // (EnemyBehaviour's serialized soundWhenHit: Enemy.DealDamageToEnemy's sound, presentation.)
    slime->hasWillAbsorbedPoint = false;
    for (const PrefabNode& n : prefab.nodes)
        if (n.path == "WillAbsorbedPoint") {
            slime->hasWillAbsorbedPoint = true;
            slime->willAbsorbedPointLocal = {n.x, n.y};
        }
    return slime;
}

} // namespace game
