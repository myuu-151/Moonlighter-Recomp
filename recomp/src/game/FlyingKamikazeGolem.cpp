#include "game/FlyingKamikazeGolem.h"

#include <cmath>

#include "game/MasterAudio.h"
#include "unity/Mathf.h"
#include "unity/Random.h"
#include "unity/Tween.h"

namespace game {

using unity::Vector2;
namespace Mathf = unity::Mathf;
using unity::mono::D;
using unity::mono::F;

namespace {

// Vector2.Angle(from, to) as Unity 2019.2's managed code has it.
float Angle(Vector2 from, Vector2 to)
{
    const float fromSqr = F(D(from.x) * D(from.x) + D(from.y) * D(from.y));
    const float toSqr = F(D(to.x) * D(to.x) + D(to.y) * D(to.y));
    const float denominator = F(std::sqrt(D(fromSqr) * D(toSqr)));
    if (denominator < 1e-15f) return 0.0f;
    const float dot = F(D(from.x) * D(to.x) + D(from.y) * D(to.y));
    const float clamped = Mathf::Clamp(F(D(dot) / D(denominator)), -1.0f, 1.0f);
    return F(D(F(std::acos(D(clamped)))) * D(57.29578f));
}

// Extensions.GetAngleFromVector(me, other): Angle(other, me), 360 - it where Cross(other, me).z > 0.
float GetAngleFromVector(Vector2 me, Vector2 other)
{
    float num = Angle(other, me);
    const float z = F(D(other.x) * D(me.y) - D(other.y) * D(me.x));
    if (z > 0.0f) num = 360.0f - num;
    return num;
}

// (float)Math.PI / 180f, as the compiler folded it (the IL's ldc.r4).
constexpr float kDeg2Rad = 0.017453292f;

} // namespace

FlyingKamikazeGolemBehaviour::FlyingKamikazeGolemBehaviour(Enemy& e) : EnemyBehaviour(e) {}

FlyingKamikazeGolemBehaviour::~FlyingKamikazeGolemBehaviour()
{
    if (returnTween && enemy.scene.tweens) enemy.scene.tweens->Kill(returnTween);
}

void FlyingKamikazeGolemBehaviour::Init()
{
    EnemyBehaviour::Init();
    isCharging = false;
}

void FlyingKamikazeGolemBehaviour::EnemyRoom_OnRoomEnter()
{
    if (!subscribedToRoomEnter) return; // (the event's handler)
    EnemyBehaviour::EnemyRoom_OnRoomEnter();
    isCharging = false;
}

bool FlyingKamikazeGolemBehaviour::Contains(const unity::Rect& r, Vector2 point)
{
    // Rect.Contains(Vector3): xMin <= x < xMax (xMax: m_Width + m_XMin, a float), the same in y.
    const float xMax = r.width + r.x, yMax = r.height + r.y;
    return point.x >= r.x && point.x < xMax && point.y >= r.y && point.y < yMax;
}

unity::Rect FlyingKamikazeGolemBehaviour::RoomBounds() const
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

void FlyingKamikazeGolemBehaviour::ChooseAndApplyAction(float deltaTime)
{
    if (!canMove) return;
    if (!isCharging) {
        PrepareChargeAttack();
        return;
    }
    if (!isReturning) MoveEnemy(deltaTime);
    if (bounds.width > 0.0f && !Contains(bounds, enemy.position())) {
        if (enemy.body) enemy.body->SetVelocity(Vector2::zero());
        desiredDirection = Vector2::zero();
        // GetAngleFromVector(Vector3.right, heroMerchantTransform.position - transform.position).
        angle = GetAngleFromVector(Vector2::right(), HeroPosition() - enemy.position());
        const Vector2 p = enemy.position();
        const float xMax = bounds.width + bounds.x, yMax = bounds.height + bounds.y;
        enemy.setPosition({Mathf::Clamp(p.x, bounds.x + boundsBorder, xMax - boundsBorder),
                           Mathf::Clamp(p.y, bounds.y + boundsBorder, yMax - boundsBorder)});
    }
}

void FlyingKamikazeGolemBehaviour::MoveEnemy(float deltaTime)
{
    if (enemy.body) enemy.body->SetVelocity(desiredDirection);
    Vector2 vector = HeroPosition();
    vector = {Mathf::Clamp(vector.x, bounds.x, bounds.x + bounds.width), Mathf::Clamp(vector.y, bounds.y, bounds.y + bounds.height)};
    // desiredDirection * positionPercent + (vector - position).normalized * speed * 3f *
    // (1f - positionPercent): Vector2's operators, left to right.
    const Vector2 toHero = vector - enemy.position();
    desiredDirection = desiredDirection * positionPercent +
                       unity::Normalized(toHero) * enemy.enemyStats.speed() * 3.0f * (1.0f - positionPercent);
    angle = F(D(angle) + D(deltaTime) * 3.0);
}

void FlyingKamikazeGolemBehaviour::PrepareChargeAttack()
{
    Notify(Event::Prepare, false);
    if (enemy.body) enemy.body->SetVelocity(Vector2::zero());
    isCharging = true;
    SetAnimatorTrigger("Attack", true);
    Notify(Event::Prepare, true);
}

void FlyingKamikazeGolemBehaviour::MakeChargeAttack()
{
    Notify(Event::Charge, false);
    const char* const sound = "golem_dungeon_flyingkamikazegolem_attack";
    if (enemy.scene.audio) enemy.scene.audio->PlaySound(sound);
    if (enemy.scene.playSound) enemy.scene.playSound(enemy, sound);
    if (bounds.height == 0.0f) bounds = RoomBounds();
    SetAttackActive(true);
    SetRootCollider(true);
    isCharging = true;
    const int num = 20;
    const Vector2 at = enemy.position();
    chargePosition = at;
    // (heroMerchantTransform.position - transform.position).normalized (a Vector3, z 0).
    Vector2 normalized = unity::Normalized(HeroPosition() - at);
    lastTurn = unity::Random::Range(-num, num + 1);
    const float f = F(D(static_cast<float>(lastTurn)) * D(kDeg2Rad));
    // Mathf.Cos / Mathf.Sin: (float)Math.Cos(f). The y uses the x already turned (the game's).
    const float cosF = F(std::cos(D(f))), sinF = F(std::sin(D(f)));
    normalized.x = F(D(normalized.x) * D(cosF) - D(normalized.y) * D(sinF));
    normalized.y = F(D(normalized.x) * D(sinF) + D(normalized.y) * D(cosF));
    chargePosition = chargePosition + normalized * chargeDistance;
    chargePosition = {Mathf::Clamp(chargePosition.x, bounds.x, bounds.x + bounds.width),
                      Mathf::Clamp(chargePosition.y, bounds.y, bounds.y + bounds.height)};
    LookToPosition(chargePosition - enemy.position());
    trailsPlaying = true; // EnableTrail: each trail's Play (particles)
    desiredDirection = unity::Normalized(chargePosition - enemy.position()) * 300.0f;
    if (enemy.scene.calls) enemy.scene.calls->Invoke(this, "EndCharge", 0.5f, [this] { EndCharge(); });
    Notify(Event::Charge, true);
}

void FlyingKamikazeGolemBehaviour::EndCharge()
{
    Notify(Event::EndCharge, false);
    isReturning = true;
    trailsPlaying = false; // DisableTrail
    SetAttackActive(false);
    unity::Tweens* tweens = enemy.scene.tweens;
    if (!tweens) {
        Notify(Event::EndCharge, true);
        return;
    }
    // DOTween.To(() => _body.velocity, v => _body.velocity = v, Vector2.zero, 1.5f)
    //     .SetEase(Ease.OutQuint).OnComplete(...).SetUpdate(false).
    Enemy* e = &enemy;
    returnTween = tweens->To([e] { return e->body ? e->body->velocity() : Vector2::zero(); },
                             [e](Vector2 v) {
                                 if (e->body) e->body->SetVelocity(v);
                             },
                             Vector2::zero(), 1.5f, unity::Ease::OutQuint, 0.0f, false);
    tweens->OnComplete(returnTween, [this] {
        Notify(Event::ReturnDone, false);
        returnTween = 0;
        SetRootCollider(false);
        desiredDirection = Vector2::zero();
        isReturning = false;
        lastEnableDelay = unity::Random::Range(2.0f, 6.0f);
        if (enemy.scene.calls) enemy.scene.calls->Invoke(this, "EnableCharge", lastEnableDelay, [this] { EnableCharge(); });
        Notify(Event::ReturnDone, true);
    });
    Notify(Event::EndCharge, true);
}

void FlyingKamikazeGolemBehaviour::EnableCharge()
{
    Notify(Event::EnableCharge, false);
    isCharging = false;
    Notify(Event::EnableCharge, true);
}

void FlyingKamikazeGolemBehaviour::LookToPosition(Vector2 position)
{
    const bool flag = Mathf::Abs(position.x) > Mathf::Abs(position.y);
    const float num = Mathf::Sign(flag ? position.x : position.y);
    const bool right = flag && num > 0.0f, left = flag && !(num > 0.0f), up = !flag && num > 0.0f, down = !flag && !(num > 0.0f);
    enemyDirection = right ? Direction::Right : left ? Direction::Left : up ? Direction::Back : Direction::Front;
    // (The controller has none of these: Unity warns, nothing changes.)
    SetBoolIfAny("DirectionRight", right);
    SetBoolIfAny("DirectionLeft", left);
    SetBoolIfAny("DirectionUp", up);
    SetBoolIfAny("DirectionDown", down);
}

void FlyingKamikazeGolemBehaviour::SetBoolIfAny(const char* name, bool value)
{
    if (enemy.enemyAnimator && enemy.enemyAnimator->Controller().Param(name) >= 0) enemy.enemyAnimator->SetBool(name, value);
}

void FlyingKamikazeGolemBehaviour::SetRootCollider(bool enabled)
{
    // _collider.enabled (the root BoxCollider2D: GetComponent<Collider2D>()).
    if (enemy.physics) enemy.physics->SetRootColliderEnabled(enemy, enabled);
    else enemy.colliderEnabled = enabled;
}

void FlyingKamikazeGolemBehaviour::SetAttackActive(bool active)
{
    // Attack.SetActive (the "AttackCollider" child): its trigger's shape made or destroyed now.
    enemy.attackActive = active;
    if (enemy.physics) enemy.physics->SyncAttackCollider(enemy);
}

void FlyingKamikazeGolemBehaviour::OnEnemyDestroyed()
{
    EnemyBehaviour::OnEnemyDestroyed();
    // The return tween's setter on a destroyed body: DOTween's safe mode kills the tween.
    if (returnTween && enemy.scene.tweens) enemy.scene.tweens->Kill(returnTween);
    returnTween = 0;
}

void FlyingKamikazeGolemBehaviour::OnDeath()
{
    if (registerDeath) registerDeath();
    EnemyBehaviour::OnDeath();
}

bool FlyingKamikazeGolemBehaviour::OnAnimationEvent(const unity::AnimationEvent& e)
{
    const std::string& f = e.function;
    if (f == "MakeChargeAttack") MakeChargeAttack();
    else if (f == "EndCharge") EndCharge();
    else if (f == "EnableCharge") EnableCharge();
    else if (f == "PrepareChargeAttack") PrepareChargeAttack();
    else return EnemyBehaviour::OnAnimationEvent(e);
    return true;
}

std::unique_ptr<FlyingKamikazeGolemBehaviour> MakeFlyingKamikazeGolemBehaviour(Enemy& enemy, const Behaviour& fields)
{
    auto golem = std::make_unique<FlyingKamikazeGolemBehaviour>(enemy);
    golem->enemyDirection = static_cast<FlyingKamikazeGolemBehaviour::Direction>(static_cast<int>(fields.Field("enemyDirection", 0.0)));
    golem->trails = static_cast<int>(fields.Field("Trails", 0.0));
    return golem;
}

} // namespace game
