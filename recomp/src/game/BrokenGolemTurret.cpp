#include "game/BrokenGolemTurret.h"

#include <algorithm>
#include <cmath>

#include "game/Constants.h"
#include "game/MasterAudio.h"
#include "unity/Mathf.h"
#include "unity/Random.h"

namespace game {

using unity::Vector2;
namespace Mathf = unity::Mathf;
using unity::mono::D;
using unity::mono::F;

namespace {

// LayerMask.NameToLayer (the project's TagManager).
constexpr int kLayerRisenObstacle = 8, kLayerLevelBoundary = 11;
// DOMove's reach and time (StartMove).
constexpr float kMoveX = 500.0f, kMoveY = 550.0f, kMoveTime = 3.0f;

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

} // namespace

// ---- BrokenGolemTurretBehaviour -------------------------------------------------------

const Vector2 BrokenGolemTurretBehaviour::kCheckDirections[4] = {{1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}, {0.0f, -1.0f}};

BrokenGolemTurretBehaviour::BrokenGolemTurretBehaviour(Enemy& e) : EnemyBehaviour(e) {}

BrokenGolemTurretBehaviour::~BrokenGolemTurretBehaviour()
{
    // (The C++ objects outlive nothing they point at: the shots forget their owner.)
    if (EnemyProjectiles* sys = enemy.scene.projectiles)
        for (EnemyProjectile* p : sys->All())
            if (auto* shot = dynamic_cast<BrokenGolemTurretProjectile*>(p))
                if (shot->projectileOwner == &enemy) shot->projectileOwner = nullptr;
}

int BrokenGolemTurretBehaviour::LookLayerMask() { return (1 << kLayerRisenObstacle) | (1 << kLayerLevelBoundary); }

void BrokenGolemTurretBehaviour::Init()
{
    EnemyBehaviour::Init();
    if (EnemyProjectiles* sys = enemy.scene.projectiles)
        sys->poolManager.CreateOrFillGameObjectPool(ProjectilePooledName(), sys->Model(projectilePrefab), 6);
    attackPushBackwardsForce = 50.0f;
}

void BrokenGolemTurretBehaviour::ChooseAndApplyAction(float deltaTime)
{
    if (patrolLapse <= 0.0f) {
        if (enemyState != EnemyState::Attacking) LookToPosition();
        AttackPlayer();
        patrolLapse = unity::Random::Range(0.001f, 0.1f);
    } else {
        patrolLapse -= deltaTime;
    }
}

void BrokenGolemTurretBehaviour::LookToPosition()
{
    if (beforeLook) beforeLook(*this);
    const int layerMask = LookLayerMask();
    // Extensions.NormalizeOneDimensional(heroMerchantTransform.position - transform.position +
    // playerOffset): Vector3 operators, each rounded.
    const Vector2 h = HeroPosition(), p = enemy.position();
    const Vector2 me = (h - p) + playerOffset;
    const Vector2 item = Mathf::Max(Mathf::Abs(me.x), Mathf::Abs(me.y)) != Mathf::Abs(me.x) ? Vector2::up() * Mathf::Sign(me.y)
                                                                                         : Vector2::right() * Mathf::Sign(me.x);
    canShoot = false;
    int num = -1; // checkDirections.IndexOf(item)
    for (int k = 0; k < 4 && num < 0; k++)
        if (kCheckDirections[k].x == item.x && kCheckDirections[k].y == item.y) num = k;
    // (IndexOf can't miss: a NormalizeOneDimensional is one of the four, -0 equal to 0.)
    if (num < 0) num = 0;
    // Constants.GetFloat("tileSize") * 2.4f, a float argument.
    const float distance = F(D(Constants::GetFloat("tileSize")) * D(2.4f));
    for (int i = 0; i < 4; i++) {
        Ray ray;
        ray.origin = p;
        ray.direction = kCheckDirections[num];
        ray.distance = distance;
        ray.layerMask = layerMask;
        if (enemy.scene.queries) ray.hit = enemy.scene.queries->Raycast(p, kCheckDirections[num], distance, layerMask);
        rays.push_back(ray);
        if (ray.hit.collider == nullptr) {
            canShoot = true;
            break;
        }
        num = (num + 1) % 4;
    }
    const Vector2 dir = kCheckDirections[num];
    const bool right = Mathf::Abs(dir.x) >= Mathf::Abs(dir.y) && dir.x >= 0.0f;
    const bool left = Mathf::Abs(dir.x) >= Mathf::Abs(dir.y) && !(dir.x >= 0.0f);
    const bool up = !(Mathf::Abs(dir.x) >= Mathf::Abs(dir.y)) && dir.y >= 0.0f;
    const bool down = !(Mathf::Abs(dir.x) >= Mathf::Abs(dir.y)) && !(dir.y >= 0.0f);
    enemyDirection = right ? HeroDirection::Right : left ? HeroDirection::Left : up ? HeroDirection::Back : HeroDirection::Front;
    if (unity::Animator* a = enemy.enemyAnimator.get()) {
        a->SetBool("DirectionRight", right);
        a->SetBool("DirectionLeft", left);
        a->SetBool("DirectionUp", up);
        a->SetBool("DirectionDown", down);
    }
}

void BrokenGolemTurretBehaviour::AttackPlayer()
{
    enemyState = EnemyState::Attacking;
    if (enemy.scene.calls) enemy.scene.calls->Invoke(this, "ShootAnimation", shootDelay, [this] { ShootAnimation(); });
}

void BrokenGolemTurretBehaviour::AttackHitObject(const std::string& otherTag)
{
    const bool player = otherTag == "Player";
    if (!player) PlayShotImpactSound();
    if (!player && otherTag != "Shield") return;
    PushPlayerBackwards();
    if (player && enemy.enemyStats.isAlive()) {
        EnemyHero* hero = enemy.scene.hero;
        if (hero && hero->dealDamage) hero->dealDamage(enemy.enemyStats.strength(), enemy.name, enemy);
    }
}

void BrokenGolemTurretBehaviour::SetAttackState() { enemyState = EnemyState::Attacking; }

void BrokenGolemTurretBehaviour::QuitAttackState()
{
    enemyState = EnemyState::Idle;
    LookToPosition();
}

void BrokenGolemTurretBehaviour::ShootAnimation()
{
    if (onShootAnimation) onShootAnimation(*this);
    SetAnimatorTrigger("Shoot", true);
}

Vector2 BrokenGolemTurretBehaviour::GetProyectileSpawnPosition() const
{
    const Vector2 p = enemy.position();
    if (spawnProyectilePositions.empty()) return p;
    const float count = static_cast<float>(spawnProyectilePositions.size());
    // (int)Mathf.Round(angle / 360f * count): the quotient and product in double, a float argument.
    const float angle = GetAngleFromVector(Vector2::right(), {distanceX, distanceY});
    int num = static_cast<int>(F(std::nearbyint(D(F(D(angle) / 360.0 * D(count))))));
    if (num >= static_cast<int>(spawnProyectilePositions.size())) num = 0;
    return p + spawnProyectilePositions[static_cast<size_t>(num)];
}

void BrokenGolemTurretBehaviour::ShootProjectile()
{
    if (!canShoot) return;
    EnemyProjectiles* sys = enemy.scene.projectiles;
    if (!sys) return;
    auto* shot = dynamic_cast<BrokenGolemTurretProjectile*>(sys->poolManager.GetObject(ProjectilePooledName()));
    if (!shot) return;
    shot->SetParentActive(sys->roomParents); // transform.SetParent(enemy.enemyRoom.transform)
    shot->setPosition(GetProyectileSpawnPosition());
    shot->scale = enemy.scale; // localScale = transform.localScale (rotation identity)
    shot->onShouldBeDestroyed = [sys](EnemyProjectile& p) { sys->poolManager.ReturnObject(&p); };
    shot->projectileDirection = enemyDirection;
    shot->projectileOwner = &enemy;
    shot->StartMove();
    projectilesShot.push_back(shot);
    if (onShot) onShot(*this, shot);
}

void BrokenGolemTurretBehaviour::ProjectileDestroyed(BrokenGolemTurretProjectile* projectile)
{
    const auto it = std::find(projectilesShot.begin(), projectilesShot.end(), projectile);
    if (it != projectilesShot.end()) projectilesShot.erase(it);
}

void BrokenGolemTurretBehaviour::PlaySound(const char* sound)
{
    if (enemy.scene.audio) enemy.scene.audio->PlaySound(sound);
    if (enemy.scene.playSound) enemy.scene.playSound(enemy, sound);
}

void BrokenGolemTurretBehaviour::PlayChargeSound() { PlaySound("golem_dungeon_turret_shot_charge"); }
void BrokenGolemTurretBehaviour::PlayShootSound() { PlaySound("golem_dungeon_turret_shot"); }
void BrokenGolemTurretBehaviour::PlayShotImpactSound() { PlaySound("golem_dungeon_turret_shot_impact"); }

void BrokenGolemTurretBehaviour::OnEnemyDestroyed()
{
    EnemyBehaviour::OnEnemyDestroyed();
    DestroyProjectiles();
}

void BrokenGolemTurretBehaviour::OnPlayerLeaveRoom()
{
    EnemyBehaviour::OnPlayerLeaveRoom();
    DestroyProjectiles();
}

void BrokenGolemTurretBehaviour::DestroyProjectiles()
{
    // By index while DestroyNow takes each out of the list: every other one is skipped.
    for (size_t i = 0; i < projectilesShot.size(); i++)
        if (projectilesShot[i]) projectilesShot[i]->DestroyNow();
}

void BrokenGolemTurretBehaviour::OnDeath()
{
    EnemyBehaviour::OnDeath();
    if (registerDeath) registerDeath();
}

bool BrokenGolemTurretBehaviour::OnAnimationEvent(const unity::AnimationEvent& e)
{
    const std::string& f = e.function;
    if (f == "SetAttackState") SetAttackState();
    else if (f == "QuitAttackState") QuitAttackState();
    else if (f == "ShootProjectile") ShootProjectile();
    else if (f == "PlayShootSound") PlayShootSound();
    else if (f == "PlayChargeSound") PlayChargeSound();
    else if (f == "PlayShotImpactSound") PlayShotImpactSound();
    else if (f == "ShootAnimation") ShootAnimation();
    else return EnemyBehaviour::OnAnimationEvent(e);
    return true;
}

std::unique_ptr<BrokenGolemTurretBehaviour> MakeBrokenGolemTurretBehaviour(Enemy& enemy, const Behaviour& fields)
{
    auto turret = std::make_unique<BrokenGolemTurretBehaviour>(enemy);
    if (const auto it = fields.raw.find("projectilePrefab"); it != fields.raw.end() && it->second.IsString())
        turret->projectilePrefab = it->second.s;
    turret->shootDelay = static_cast<float>(fields.Field("shootDelay", 0.0));
    turret->enemyDirection = static_cast<HeroDirection>(static_cast<int>(fields.Field("enemyDirection", 0.0)));
    if (const auto it = fields.raw.find("spawnProyectilePositions"); it != fields.raw.end())
        for (const json::Value& v : it->second.arr)
            if (v.IsArray() && v.size() == 2)
                turret->spawnProyectilePositions.push_back({static_cast<float>(v[0].AsDouble()), static_cast<float>(v[1].AsDouble())});
    return turret;
}

// ---- BrokenGolemTurretProjectile ------------------------------------------------------

BrokenGolemTurretProjectile::BrokenGolemTurretProjectile(EnemyProjectiles& sys, const Prefab& prefab, const unity::AnimatorController* controller)
    : EnemyProjectile(sys, prefab, controller)
{
}

BrokenGolemTurretBehaviour* BrokenGolemTurretProjectile::Owner() const
{
    // (bool)projectileOwner && its BrokenGolemTurretBehaviour.
    if (!projectileOwner || projectileOwner->destroyed) return nullptr;
    return dynamic_cast<BrokenGolemTurretBehaviour*>(projectileOwner->enemyBehaviour.get());
}

void BrokenGolemTurretProjectile::StartMove()
{
    SetTrigger(false);
    crashed = false;
    unity::Tweens* tweens = scene().tweens;
    // projectileArt.transform.rotation = identity, then Rotate(forward * angle): its euler z.
    artRotation = 0.0f;
    const Vector2 at = position();
    // DOMoveX / DOMoveY (Vector3Plugin with an axis constraint): that component start +
    // change * ease (one expression, rounded once), the other as transform.position reads it
    // then, the whole position set; linear, on the scaled clock.
    auto moveAxis = [this, tweens](bool x, float end) -> int {
        if (!tweens) return 0;
        BrokenGolemTurretProjectile* self = this;
        return tweens->To([self, x] { const Vector2 p = self->position(); return Vector2{x ? p.x : p.y, 0.0f}; },
                          [self, x](Vector2 v) {
                              Vector2 p = self->position();
                              (x ? p.x : p.y) = v.x;
                              self->setPosition(p);
                          },
                          Vector2{end, 0.0f}, kMoveTime, unity::Ease::Linear, 0.0f, false);
    };
    switch (projectileDirection) {
    case HeroDirection::Right:
        flyingMove = moveAxis(true, at.x + kMoveX);
        artRotation = 90.0f;
        break;
    case HeroDirection::Left:
        flyingMove = moveAxis(true, at.x + -kMoveX);
        artRotation = 270.0f;
        break;
    case HeroDirection::Front:
        flyingMove = moveAxis(false, at.y + -kMoveY);
        artRotation = 0.0f;
        break;
    case HeroDirection::Back:
        flyingMove = moveAxis(false, at.y + kMoveY);
        artRotation = 180.0f;
        break;
    default: {
        BrokenGolemTurretProjectile* self = this;
        flyingMove = tweens ? tweens->Move([self] { return self->position(); }, [self](Vector2 p) { self->setPosition(p); }, at, kMoveTime,
                                           unity::Ease::Linear, 0.0f, false)
                            : 0;
        break;
    }
    }
    if (tweens && flyingMove) tweens->OnComplete(flyingMove, [this] { DestroyNow(); });
}

void BrokenGolemTurretProjectile::OnCollisionEnter2D(const Collision2D& other)
{
    if (!other.CompareTag("LevelBoundary") && !other.CompareTag("Rock") && !other.CompareTag("Player") && !other.CompareTag("Shield")) return;
    if (BrokenGolemTurretBehaviour* owner = Owner()) {
        owner->AttackHitObject(other.tag);
        owner->ProjectileDestroyed(this);
    }
    crashed = true;
    if (animator && animator->Controller().Param("Crash") >= 0) animator->SetTrigger("Crash");
    // flyingMove.Pause(): it never moves again (DestroyNow kills it).
    if (scene().tweens) scene().tweens->Kill(flyingMove);
    flyingMove = 0;
    SetTrigger(true);
    if (scene().calls) scene().calls->Invoke(this, "DestroyNow", timeToDestroyAfterCrash, [this] { DestroyNow(); });
}

void BrokenGolemTurretProjectile::DestroyNow()
{
    if (BrokenGolemTurretBehaviour* owner = Owner()) owner->ProjectileDestroyed(this);
    if (scene().tweens) scene().tweens->Kill(flyingMove);
    flyingMove = 0;
    if (onDestroyNow) onDestroyNow(*this);
    if (onShouldBeDestroyed) onShouldBeDestroyed(*this);
    else system.Deactivate(*this); // (Object.Destroy: gone from the scene)
}

std::unique_ptr<EnemyProjectile> MakeBrokenGolemTurretProjectile(EnemyProjectiles& system, const Prefab& prefab,
                                                                const unity::AnimatorController* controller, const Behaviour& fields)
{
    auto shot = std::make_unique<BrokenGolemTurretProjectile>(system, prefab, controller);
    shot->timeToDestroyAfterCrash = static_cast<float>(fields.Field("timeToDestroyAfterCrash", 0.3));
    return shot;
}

} // namespace game
