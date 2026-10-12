#include "game/GolemButler.h"

#include <cmath>

#include <Box2D/Box2D.h>

#include "game/MasterAudio.h"
#include "unity/Mathf.h"

namespace game {

using unity::Vector2;
namespace Mathf = unity::Mathf;
using unity::mono::D;
using unity::mono::F;

namespace {

// DOMove's reach and time (StartMove).
constexpr float kMoveReach = 550.0f, kMoveTime = 2.0f;
// IsPlayerInBack's angle (a literal, not a field).
constexpr float kBackAngle = 55.0f;

} // namespace

float GolemButlerAngle(Vector2 from, Vector2 to)
{
    // (float)Math.Sqrt(from.sqrMagnitude * to.sqrMagnitude); Mathf.Clamp(Dot / it, -1, 1);
    // (float)Math.Acos(dot) * Mathf.Rad2Deg.
    const float fromSqr = F(D(from.x) * D(from.x) + D(from.y) * D(from.y));
    const float toSqr = F(D(to.x) * D(to.x) + D(to.y) * D(to.y));
    const float denominator = F(std::sqrt(D(fromSqr) * D(toSqr)));
    if (denominator < 1e-15f) return 0.0f;
    const float dot = F(D(from.x) * D(to.x) + D(from.y) * D(to.y));
    const float clamped = Mathf::Clamp(F(D(dot) / D(denominator)), -1.0f, 1.0f);
    return F(D(F(std::acos(D(clamped)))) * D(57.29578f));
}

float GolemButlerAngleFromVector(Vector2 me, Vector2 other)
{
    // Vector2.Angle(other, me); 360 - it where Vector3.Cross(other, me).z > 0.
    float num = GolemButlerAngle(other, me);
    const float z = F(D(other.x) * D(me.y) - D(other.y) * D(me.x));
    if (z > 0.0f) num = 360.0f - num;
    return num;
}

// ---- GolemButlerBehaviour ---------------------------------------------------------------

GolemButlerBehaviour::GolemButlerBehaviour(Enemy& e) : EnemyBehaviour(e) {}

GolemButlerBehaviour::~GolemButlerBehaviour()
{
    // (The C++ objects outlive nothing they point at: the shots forget their owner, which the
    // game's find destroyed.)
    if (EnemyProjectiles* sys = enemy.scene.projectiles)
        for (EnemyProjectile* p : sys->All())
            if (auto* shot = dynamic_cast<GolemButlerProjectile*>(p))
                if (shot->projectileOwner == &enemy) shot->projectileOwner = nullptr;
}

void GolemButlerBehaviour::Start()
{
    ForbidEnemyMovement();
    LookToPosition();
    // InvokeRepeating("LookToPlayer", 0.1f, 2.5f): no such method (nothing runs).
}

void GolemButlerBehaviour::Init()
{
    EnemyBehaviour::Init();
    if (EnemyProjectiles* sys = enemy.scene.projectiles)
        sys->poolManager.CreateOrFillGameObjectPool(ProjectilePooledName(), sys->Model(projectilePrefab), 2);
    previousEnemyHealth = enemy.enemyStats.CurrentHealth();
}

void GolemButlerBehaviour::Update(float deltaTime)
{
    lastDeltaTime_ = deltaTime;
    EnemyBehaviour::Update(deltaTime);
}

void GolemButlerBehaviour::ChooseAndApplyAction(float deltaTime)
{
    if (enemyState != EnemyState::Stunned) {
        // Vector2.Distance(transform.position, heroMerchantTransform.position).
        const Vector2 p = enemy.position(), h = HeroPosition();
        if (unity::Distance(p, h) < rangeAttackMinDistance) {
            if (IsPlayerInFieldOfVision()) {
                MoveEnemy(deltaTime);
                if (isWaiting) isWaiting = false;
            } else {
                if (!isWaiting) {
                    isWaiting = true;
                    unity::DelayedCalls& calls = *enemy.scene.calls;
                    calls.CancelInvoke(this, "LookToPosition");
                    calls.Invoke(this, "LookToPosition", waitWhileWillIsNearTime, [this] { LookToPosition(); });
                }
                StopEnemy();
            }
        } else {
            TryShootAttack();
        }
        enemy.enemyStats.SetInvencible(!IsPlayerInBack());
    } else {
        // Vector3.Distance(transform.position, heroMerchantTransform.position) (z 0: Vector2's).
        if (unity::Distance(enemy.position(), HeroPosition()) < distanceToRearm) RearmEnemy();
    }
}

void GolemButlerBehaviour::LookToPosition()
{
    if (beforeLook) beforeLook(*this);
    EnemyBehaviour::LookToPosition();
}

void GolemButlerBehaviour::TryShootAttack()
{
    if (!shotEnabled) return;
    shotEnabled = false;
    LookToPosition();
    SetAnimatorTrigger("ShootAttack", true);
    enemy.scene.calls->Invoke(this, "RecoverShot", shootAttackRecoverTime, [this] { RecoverShot(); });
}

Vector2 GolemButlerBehaviour::GetProyectileSpawnPosition() const
{
    const Vector2 p = enemy.position();
    if (spawnProyectilePositions.empty()) return p;
    const float count = static_cast<float>(spawnProyectilePositions.size());
    // (int)Mathf.Round(angle / 360f * count): the quotient and product in double, a float argument.
    const float angle = GolemButlerAngleFromVector(Vector2::right(), {distanceX, distanceY});
    int num = static_cast<int>(F(std::nearbyint(D(F(D(angle) / 360.0 * D(count))))));
    if (num >= static_cast<int>(spawnProyectilePositions.size())) num = 0;
    return p + spawnProyectilePositions[static_cast<size_t>(num)];
}

void GolemButlerBehaviour::ShootAttack()
{
    if (beforeShootAttack) beforeShootAttack(*this);
    EnemyProjectiles* sys = enemy.scene.projectiles;
    if (!sys) return;
    auto* shot = dynamic_cast<GolemButlerProjectile*>(sys->poolManager.GetObject(ProjectilePooledName()));
    if (!shot) return;
    shot->SetParentActive(sys->roomParents); // transform.SetParent(enemy.enemyRoom.transform)
    shot->setPosition(GetProyectileSpawnPosition()); // (rotation identity)
    shot->onShouldBeDestroyed = [sys](EnemyProjectile& p) { sys->poolManager.ReturnObject(&p); };
    // heroMerchantTransform.position - (Vector3)GetProyectileSpawnPosition().
    const Vector2 h = HeroPosition();
    const Vector2 spawn = GetProyectileSpawnPosition();
    shot->projectileDirection = h - spawn;
    shot->projectileOwner = &enemy;
    shot->StartMove();
    if (onShot) onShot(*this, shot);
}

void GolemButlerBehaviour::RecoverShot()
{
    enemy.scene.calls->CancelInvoke(this, "RecoverShot");
    shotEnabled = true;
}

void GolemButlerBehaviour::EnemyWasHit(float newHealthValue)
{
    EnemyBehaviour::EnemyWasHit(newHealthValue);
    // Time.deltaTime: the fixed step's inside a physics callback (a weapon's hit), else the frame's.
    const unity::DelayedCalls* calls = enemy.scene.calls;
    MoveEnemy(calls && calls->inFixedStep ? fixedDeltaTime : lastDeltaTime_);
}

void GolemButlerBehaviour::HealthNotChangedWhenHit(float newHealth)
{
    EnemyBehaviour::HealthNotChangedWhenHit(newHealth);
    PlayNoDamagedSound();
    PushPlayerBackwards(attackPushBackwardsForce);
}

void GolemButlerBehaviour::PlaySound(const char* sound)
{
    if (enemy.scene.audio) enemy.scene.audio->PlaySound(sound);
    if (enemy.scene.playSound) enemy.scene.playSound(enemy, sound);
}

void GolemButlerBehaviour::PlayNoDamagedSound() { PlaySound("golem_dungeon_butler_shield_hit"); }
void GolemButlerBehaviour::PlayShootSound() { PlaySound("golem_dungeon_golembutler_shot"); }
void GolemButlerBehaviour::PlayShotImpactSound() { PlaySound("golem_dungeon_golembutler_shot_impact"); }

Vector2 GolemButlerBehaviour::Facing() const
{
    // The later tests win (as the C#'s ifs in a row), though a state matches one at most.
    Vector2 vector = Vector2::zero();
    if (!enemy.enemyAnimator) return vector;
    const unity::AnimatorStateInfo info = enemy.enemyAnimator->GetCurrentAnimatorStateInfo();
    auto is = [&](const char* a, const char* b, const char* c) { return info.IsName(a) || info.IsName(b) || info.IsName(c); };
    if (is("WalkUp", "AttackUp", "AttackDefenseUp")) vector = Vector2::up();
    if (is("WalkDown", "AttackDown", "AttackDefenseDown")) vector = Vector2{-0.0f, -1.0f}; // -1f * Vector3.up
    if (is("WalkRight", "AttackRight", "AttackDefenseRight")) vector = Vector2::right();
    if (is("WalkLeft", "AttackLeft", "AttackDefenseLeft")) vector = Vector2{-1.0f, -0.0f}; // -1f * Vector3.right
    return vector;
}

bool GolemButlerBehaviour::IsPlayerInFieldOfVision() const
{
    // Mathf.Abs(Vector3.Angle(vector, heroMerchantTransform.position - transform.position)) < fieldOfVisionAngle.
    const Vector2 h = HeroPosition(), p = enemy.position();
    const bool in = Mathf::Abs(GolemButlerAngle(Facing(), h - p)) < fieldOfVisionAngle;
    visionChecks.push_back(in);
    return in;
}

bool GolemButlerBehaviour::IsPlayerInBack() const
{
    // Vector3.Angle(vector, transform.position - heroMerchantTransform.position) < 55f.
    const Vector2 h = HeroPosition(), p = enemy.position();
    const bool back = GolemButlerAngle(Facing(), p - h) < kBackAngle;
    backChecks.push_back(back);
    return back;
}

void GolemButlerBehaviour::SetEnemyMass(float newMass)
{
    // Rigidbody2D.mass (the centre of mass stays, as Enemy::Init's), PolyNavAgent.mass.
    enemy.rigidbody.mass = newMass;
    if (unity::Rigidbody2D* rb = enemy.body)
        if (rb->bodyType() == unity::RigidbodyType2D::Dynamic) {
            b2MassData md;
            rb->Body()->GetMassData(&md);
            const float before = md.mass;
            md.I = before > 0.0f ? (md.I - before * b2Dot(md.center, md.center)) * (newMass / before) + newMass * b2Dot(md.center, md.center) : md.I;
            md.mass = newMass;
            rb->Body()->SetMassData(&md);
        }
    enemy.navAgent.mass = newMass;
}

void GolemButlerBehaviour::DisarmEnemy()
{
    enemy.enemyStats.SetInvencible(true);
    enemyState = EnemyState::Stunned;
    SetAnimatorTrigger("Deactivation", true);
    SetEnemyMass(100.0f);
    ForbidEnemyMovement();
}

void GolemButlerBehaviour::RearmEnemy()
{
    enemy.enemyStats.SetInvencible(false);
    enemyState = EnemyState::PursuingPlayer;
    SetEnemyMass(enemy.enemyStats.weight());
    SetAnimatorTrigger("Reactivation", true);
}

void GolemButlerBehaviour::AttackHit(bool otherIsPlayer)
{
    EnemyBehaviour::AttackHit(otherIsPlayer);
    if (!otherIsPlayer) PlayShotImpactSound();
}

void GolemButlerBehaviour::OnDeath()
{
    EnemyBehaviour::OnDeath();
    if (registerDeath) registerDeath();
}

bool GolemButlerBehaviour::OnAnimationEvent(const unity::AnimationEvent& e)
{
    const std::string& f = e.function;
    if (f == "ShootAttack") ShootAttack();
    else if (f == "PlayShootSound") PlayShootSound();
    else if (f == "PlayShotImpactSound") PlayShotImpactSound();
    else if (f == "PlayNoDamagedSound") PlayNoDamagedSound();
    else if (f == "PlayDamagedSound") PlayDamagedSound();
    else if (f == "RecoverShot") RecoverShot();
    else if (f == "TryShootAttack") TryShootAttack();
    else if (f == "DisarmEnemy") DisarmEnemy();
    else if (f == "RearmEnemy") RearmEnemy();
    else return EnemyBehaviour::OnAnimationEvent(e);
    return true;
}

std::unique_ptr<GolemButlerBehaviour> MakeGolemButlerBehaviour(Enemy& enemy, const Behaviour& fields)
{
    auto butler = std::make_unique<GolemButlerBehaviour>(enemy);
    if (const auto it = fields.raw.find("projectilePrefab"); it != fields.raw.end() && it->second.IsString())
        butler->projectilePrefab = it->second.s;
    if (const auto it = fields.raw.find("spawnProyectilePositions"); it != fields.raw.end())
        for (const json::Value& v : it->second.arr)
            if (v.IsArray() && v.size() == 2)
                butler->spawnProyectilePositions.push_back({static_cast<float>(v[0].AsDouble()), static_cast<float>(v[1].AsDouble())});
    return butler;
}

// ---- GolemButlerProjectile --------------------------------------------------------------

GolemButlerProjectile::GolemButlerProjectile(EnemyProjectiles& sys, const Prefab& prefab, const unity::AnimatorController* controller)
    : EnemyProjectile(sys, prefab, controller)
{
}

GolemButlerBehaviour* GolemButlerProjectile::Owner() const
{
    if (!projectileOwner || projectileOwner->destroyed) return nullptr;
    return dynamic_cast<GolemButlerBehaviour*>(projectileOwner->enemyBehaviour.get());
}

void GolemButlerProjectile::StartMove()
{
    SetTrigger(false);
    crashed = false;
    unity::Tweens* tweens = scene().tweens;
    // transform.DOMove(transform.position + projectileDirection.normalized * 550f, 2f): the
    // Vector3 operators, each rounded; from the position when it starts; linear, scaled time.
    const Vector2 end = position() + unity::Normalized(projectileDirection) * kMoveReach;
    GolemButlerProjectile* self = this;
    flyingMove = tweens ? tweens->Move([self] { return self->position(); }, [self](Vector2 p) { self->setPosition(p); }, end, kMoveTime,
                                       unity::Ease::Linear, 0.0f, false)
                        : 0;
    // rotation.Angle = (int)Extensions.GetAngleFromVector(Vector3.right, projectileDirection) + 90.
    rotationAngle = static_cast<int>(GolemButlerAngleFromVector(Vector2::right(), projectileDirection)) + 90;
    artRotation = static_cast<float>(rotationAngle);
    if (tweens && flyingMove) tweens->OnComplete(flyingMove, [this] { DestroyNow(); });
}

void GolemButlerProjectile::OnCollisionEnter2D(const Collision2D& other)
{
    if (!other.CompareTag("LevelBoundary") && !other.CompareTag("Rock") && !other.CompareTag("Player") && !other.CompareTag("Shield")) return;
    // projectileOwner.GetComponent<GolemButlerBehaviour>().AttackHit(other.gameObject): on a
    // destroyed owner the GetComponent throws, and the rest doesn't run (the shot flies on).
    GolemButlerBehaviour* owner = Owner();
    if (!owner) return;
    owner->AttackHit(other.CompareTag("Player"));
    crashed = true;
    if (animator && animator->Controller().Param("Crash") >= 0) animator->SetTrigger("Crash");
    // flyingMove.Pause(): it never moves again (DestroyNow kills it).
    if (scene().tweens) scene().tweens->Kill(flyingMove);
    flyingMove = 0;
    SetTrigger(true);
    if (scene().calls) scene().calls->Invoke(this, "DestroyNow", timeToDestroyAfterCrash, [this] { DestroyNow(); });
}

void GolemButlerProjectile::DestroyNow()
{
    if (scene().tweens) scene().tweens->Kill(flyingMove);
    flyingMove = 0;
    if (onDestroyNow) onDestroyNow(*this);
    if (onShouldBeDestroyed) onShouldBeDestroyed(*this);
    else system.Deactivate(*this); // (Object.Destroy: gone from the scene)
}

std::unique_ptr<EnemyProjectile> MakeGolemButlerProjectile(EnemyProjectiles& system, const Prefab& prefab,
                                                          const unity::AnimatorController* controller, const Behaviour& fields)
{
    auto shot = std::make_unique<GolemButlerProjectile>(system, prefab, controller);
    shot->timeToDestroyAfterCrash = static_cast<float>(fields.Field("timeToDestroyAfterCrash", 0.3));
    return shot;
}

} // namespace game
