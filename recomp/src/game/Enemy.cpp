#include "game/Enemy.h"

#include <Box2D/Box2D.h>

#include "game/Constants.h"
#include "unity/Mathf.h"
#include "unity/Random.h"

namespace game {

using unity::Vector2;
namespace Mathf = unity::Mathf;

namespace {

// LayerMask.NameToLayer of the layers EnemyBehaviour.Init names.
constexpr int kLayerRisenObstacle = 8, kLayerLevelBoundary = 11, kLayerSunkenObstacle = 14, kLayerEnemyBoundary = 21;
// Weapon.PushEnemyBackwards leaves the heavy ones alone.
constexpr float kPushMaxWeight = 48.0f;

const float kRed[4] = {1.0f, 0.0f, 0.0f, 1.0f};
const float kWhite[4] = {1.0f, 1.0f, 1.0f, 1.0f};

} // namespace

// ---- Enemy ------------------------------------------------------------------------------

Enemy::Enemy(EnemyScene& sceneIn, const unity::AnimatorController* controller) : scene(sceneIn), controller_(controller)
{
    if (controller_) enemyAnimator = std::make_unique<unity::Animator>(*controller_);
    // EnemyStats.Die -> MarkEnemyAsDefeated -> DungeonRoom.EnemyDefeated: out of roomEnemies,
    // the room's events, and (if it was there) out of its spawn point.
    enemyStats.OnDie = [this] {
        if (!hasEnemyRoom || isBreakableObject) return;
        const bool listed = inRoomEnemies;
        inRoomEnemies = false;
        if (scene.onEnemyDefeated) scene.onEnemyDefeated(*this);
        if (listed) ReparentToContainer();
    };
}

Enemy::~Enemy() = default;

void Enemy::Instantiate(const std::vector<Vector2>& parents)
{
    navAgent.parents = parents;
    navAgent.localPosition = Vector2::zero();
    navAgent.Awake();
    if (active) navAgent.OnEnable();
    synced_ = position();
}

void Enemy::Init(const std::string& enemyName, const EnemyStatModificator& stats, const EnemyStatsContext& context)
{
    name = enemyName;
    float mass = rigidbody.mass;
    enemyStats.Init(stats, context, hasRigidbody ? &mass : nullptr);
    rigidbody.mass = mass;
    if (body && body->bodyType() == unity::RigidbodyType2D::Dynamic) {
        // Rigidbody2D.mass = weight (the centre of mass stays).
        b2MassData md;
        body->Body()->GetMassData(&md);
        const float before = md.mass;
        md.I = before > 0.0f ? (md.I - before * b2Dot(md.center, md.center)) * (mass / before) + mass * b2Dot(md.center, md.center) : md.I;
        md.mass = mass;
        body->Body()->SetMassData(&md);
    }
    if (enemyBehaviour) enemyBehaviour->Init();
}

void Enemy::SetActive(bool value)
{
    if (active == value || destroyed) return;
    active = value;
    if (!value) {
        navAgent.OnDisable();
        if (physics) physics->SetBodyActive(*this, false);
        if (scene.calls) scene.calls->StopCoroutines(enemyBehaviour.get());
    } else {
        navAgent.OnEnable();
        if (physics) physics->SetBodyActive(*this, true);
        // The Animator starts over: its default state, its parameters' defaults.
        if (controller_) enemyAnimator = std::make_unique<unity::Animator>(*controller_);
        animatorEnabled = true;
    }
}

float Enemy::CalcHitDamage(float hitStrength, float targetDefense) const
{
    const float num = static_cast<float>(Mathf::RoundToInt(hitStrength * (targetDefense / 100.0f)));
    const float damage = hitStrength - num;
    return damage < 0.0f ? 0.0f : damage; // Mathf.Clamp(damage, 0, +inf)
}

float Enemy::DealDamageToEnemy(float attackStrength, EnemyDamageReceivedType type)
{
    // enemyBehaviour.isActiveAndEnabled
    if (!enemyBehaviour || !enemyBehaviour->enabled || !active || destroyed) return 0.0f;
    const float hitStrength = attackStrength;
    const float otherDefense = enemyStats.defense();
    LastDamageReceived = type;
    const float totalDamage = CalcHitDamage(hitStrength, otherDefense);
    const float beforeAttackHealth = enemyStats.CurrentHealth();
    if (!(beforeAttackHealth > 0.0f)) return 0.0f;
    const bool flag = enemyBehaviour->IsEnemyPlayingDamagedEffect();
    bool flag2 = enemyStats.Invencible();
    // (The amulets' IModifyPlayerDamage effects would add to the damage here: not ported.)
    if (type == EnemyDamageReceivedType::Effect || (type == EnemyDamageReceivedType::Trick && flag)) enemyStats.SetInvencible(false);
    enemyStats.SetCurrentHealth(enemyStats.CurrentHealth() - totalDamage);
    if (scene.onEnemyReceivesDamage) scene.onEnemyReceivesDamage(*this, totalDamage, type);
    if (type == EnemyDamageReceivedType::Effect || (type == EnemyDamageReceivedType::Trick && flag && flag2)) {
        enemyStats.SetInvencible(flag2);
        flag2 = false;
    }
    // (The hit sound and the floating damage number follow: presentation.)
    (void)flag2;
    return totalDamage;
}

void Enemy::AddImpulse(Vector2 force)
{
    if (!body || destroyed || !active) return;
    b2Body* b = body->Body();
    b->ApplyLinearImpulse(b2Vec2(force.x, force.y), b->GetWorldCenter(), true);
}

Vector2 Enemy::velocity() const { return body ? body->velocity() : Vector2::zero(); }

void Enemy::PushBackwards(Vector2 heroPosition, float attackPushForce)
{
    if (!enemyBehaviour || enemyBehaviour->IsBoss() || !(enemyStats.weight() < kPushMaxWeight)) return;
    // (Vector2)(enemy.transform.position - hero.transform.position), normalized.
    const Vector2 p = position();
    const Vector2 vector = unity::Normalized(Vector2{p.x - heroPosition.x, p.y - heroPosition.y});
    AddImpulse(vector * attackPushForce);
}

void Enemy::UpdateAnimator(float deltaTime)
{
    if (!enemyAnimator || !animatorEnabled || !active || destroyed) return;
    enemyAnimator->Update(deltaTime);
    if (const std::string* s = enemyAnimator->Sprite()) sprite = *s;
}

void Enemy::Destroy()
{
    if (destroyed) return;
    // OnDisable first (the agent leaves allAgents, the body its world), then OnDestroy.
    navAgent.OnDisable();
    if (physics) physics->SetBodyActive(*this, false);
    if (enemyBehaviour && !enemyBehaviour->IsBoss()) {
        if (!enemyStats.isAlive()) {
            // SpawnEnemyDeadSprite, DropItemsOnDeath.
            if (!enemyDeadSprite.empty() && hasEnemyRoom && !scene.bossRoom && scene.spawnEnemyDeadSprite)
                scene.spawnEnemyDeadSprite(enemyDeadSprite, position());
            if (scene.dropItemsOnDeath) scene.dropItemsOnDeath(*this);
        }
        enemyBehaviour->OnEnemyDestroyed();
    }
    destroyed = true;
    if (scene.calls) {
        scene.calls->Forget(enemyBehaviour.get());
        scene.calls->Forget(this);
    }
}

void Enemy::ReparentToContainer()
{
    // SetParent(parent.parent) keeping the world position: the new local position is the old
    // world one through the remaining parents.
    if (navAgent.parents.empty()) return;
    const Vector2 world = position();
    navAgent.parents.erase(navAgent.parents.begin());
    setPosition(world);
    // The physics takes the transform as it now reads (through the new parents), not the body's
    // own position (slime.jsonl: a slime killed in a step starts the next one where its
    // transform is, 1.4e-6 off where its body was).
    transformDirty_ = true;
}

// ---- EnemyBehaviour ---------------------------------------------------------------------

EnemyBehaviour::EnemyBehaviour(Enemy& e) : enemy(e) {}

Vector2 EnemyBehaviour::HeroPosition() const { return enemy.scene.hero ? enemy.scene.hero->position : Vector2::zero(); }

void EnemyBehaviour::SetAnimatorFloat(const char* name, float value)
{
    if (enemy.enemyAnimator && enemy.enemyAnimator->Controller().Param(name) >= 0) enemy.enemyAnimator->SetFloat(name, value);
}

void EnemyBehaviour::SetAnimatorTrigger(const char* name, bool set)
{
    // (Unity logs a missing parameter and carries on.)
    if (!enemy.enemyAnimator || enemy.enemyAnimator->Controller().Param(name) < 0) return;
    if (set) enemy.enemyAnimator->SetTrigger(name);
    else enemy.enemyAnimator->ResetTrigger(name);
}

void EnemyBehaviour::Init()
{
    checkHolesOnColliderCenter = enemy.colliderOffset; // GetComponent<Collider2D>().offset (the root's box)
    raycastObstaclesLayer = (1 << kLayerLevelBoundary) | (1 << kLayerEnemyBoundary) | (1 << kLayerRisenObstacle) | (1 << kLayerSunkenObstacle);
    targetPlayer = false;
    playingDamagedEffect = false;
    // Constants.GetFloat("merchantColliderYOffset") * Vector2.up: (0 * y, 1 * y), so x is -0.
    const float yOffset = Constants::GetFloat("merchantColliderYOffset");
    playerOffset = Vector2::up() * yOffset;
    if (enemy.hasNavAgent) enemy.navAgent.maxSpeed = enemy.enemyStats.speed();
    enemyState = EnemyState::Idle;
    enemy.enemyStats.OnHealthDecreased = [this](float v) { EnemyWasHit(v); };
    enemy.enemyStats.OnDeath = [this] { EnemyStats_OnDeath(); };
    if (enemy.hasEnemyRoom) subscribedToRoomEnter = true;
    SetEnemyShaderToDefault();
    deadForce = static_cast<int>(Constants::GetFloat("deadEnemyPushForce"));
}

void EnemyBehaviour::EnemyStats_OnDeath()
{
    if (enemy.hasEnemyRoom) subscribedToRoomEnter = false;
    OnDeath();
}

void EnemyBehaviour::EnemyRoom_OnRoomEnter()
{
    if (!subscribedToRoomEnter) return;
    enemy.navAgent.localPosition = Vector2::zero();
    enemy.transformDirty_ = true;
    enemy.enemyStats.SetCurrentHealth(enemy.enemyStats.maxHealth());
}

void EnemyBehaviour::StartTargetingPlayerAfterTime(float time)
{
    Calls().Invoke(this, "StartTargetingPlayer", time, [this] { StartTargetingPlayer(); });
}

void EnemyBehaviour::CancelPlayerTargetingAfterTime() { Calls().CancelInvoke(this, "StartTargetingPlayer"); }

void EnemyBehaviour::StartTargetingPlayer() { targetPlayer = true; }

void EnemyBehaviour::StopTargetingPlayer() { targetPlayer = false; }

void EnemyBehaviour::Update(float deltaTime)
{
    if (!enabled || !enemy.active || enemy.destroyed) return;
    if (targetPlayer) ChooseAndApplyAction(deltaTime);
    // Falling into a hole (not a boss, alive, on GroundEnemy): the box at the collider's centre
    // overlaps a SunkenObstacle.
    if (fallInHoles && enemy.enemyStats.isAlive() && enemy.layer == kLayerGroundEnemy && !IsBoss() && enemy.scene.queries) {
        const Vector2 p = enemy.position();
        const Vector2 at{p.x + checkHolesOnColliderCenter.x, p.y + checkHolesOnColliderCenter.y};
        const Vector2 size = Vector2{1.0f, 1.0f} * checkHoles;
        if (enemy.scene.queries->OverlapBox(at, size, 0.0f, 1 << kLayerSunkenObstacle) != nullptr) {
            deadForce = 0;
            enemy.enemyStats.SetInvencible(false);
            enemy.enemyStats.SetCurrentHealth(0.0f);
            const float num = !(unity::Random::Range(0.0f, 1.0f) > 0.5f) ? 1.0f : -1.0f;
            const float num2 = 0.8f;
            if (unity::Tweens* tw = enemy.scene.tweens) {
                Enemy* e = &enemy;
                // transform.DOScale(0, 0.4).SetEase(InCirc).SetUpdate(false); the sprite's DORotate(0,
                // 0, +-180) over 0.8, FastBeyond360 (the angle as given, not wrapped), the default
                // ease and update (unscaled: see OnDeath).
                tw->Move([e] { return Vector2{e->scale, 0.0f}; }, [e](Vector2 v) { e->scale = v.x; }, Vector2{0.0f, 0.0f}, num2 * 0.5f,
                         unity::Ease::InCirc);
                tw->Move([e] { return Vector2{e->spriteRotation, 0.0f}; }, [e](Vector2 v) { e->spriteRotation = v.x; },
                         Vector2{num < 0.0f ? -180.0f : 180.0f, 0.0f}, num2, unity::Ease::OutQuad, 0.0f, true);
            }
        }
    }
}

void EnemyBehaviour::ChooseAndApplyAction(float deltaTime)
{
    const Vector2 p = enemy.position(), h = HeroPosition();
    if (enemy.hasNavAgent && unity::Distance(p, h) <= enemy.navAgent.stoppingDistance + 1.0f) {
        AttackPlayer();
    } else if (canMove) {
        MoveEnemy(deltaTime);
        LookToPosition();
    }
}

void EnemyBehaviour::MoveEnemy(float deltaTime)
{
    SetAnimatorTrigger("Attacking", false);
    if (!canMove) return;
    if (_lapse > 0.0f) {
        _lapse -= deltaTime;
        return;
    }
    SetPlayerDestination();
    _lapse = 0.1f;
}

void EnemyBehaviour::SetPlayerDestination()
{
    if (!enemy.hasNavAgent) return;
    PolyNav2D* nav = PolyNav2D::current;
    PolyNavAgent& agent = enemy.navAgent;
    const Vector2 h = HeroPosition(), p = enemy.position();
    // heroMerchantTransform.position + playerOffset (Vector3s).
    const Vector2 vector{h.x + playerOffset.x, h.y + playerOffset.y};
    const float num = Mathf::Abs(vector.x - p.x) + Mathf::Abs(vector.y - p.y); // vector.DiamondDistance(position)
    const float tileSize = Constants::GetFloat("tileSize");
    if (num > tileSize * 1.5f && num < tileSize * 3.0f) {
        // Not straight at him: a tile and a fifth above or below him (or beside him), on his
        // side of the slime. Note the test: x dominant and on his right picks above/below.
        const Vector2 me{p.x - vector.x, p.y - vector.y};
        const Vector2 oneDim = Mathf::Max(Mathf::Abs(me.x), Mathf::Abs(me.y)) != Mathf::Abs(me.x) ? Vector2::up() * Mathf::Sign(me.y)
                                                                                                  : Vector2::right() * Mathf::Sign(me.x);
        Vector2 vector2 = oneDim.x > 0.0f ? Vector2::up() * Mathf::Sign(me.y) : Vector2::right() * Mathf::Sign(me.x);
        vector2 = vector2 * (tileSize * 1.2f);
        vector2 = vector2 + vector;
        if (nav && nav->PointIsValid(vector2)) {
            agent.SetDestination(vector2);
            return;
        }
        if (nav && nav->PointIsValid(vector)) {
            agent.SetDestination(vector);
            return;
        }
        int num2 = 20;
        while (nav && !nav->PointIsValid(vector2) && num2 > 0) {
            num2--;
            vector2 = vector + unity::Random::insideUnitCircle() * 24.0f;
        }
        agent.SetDestination(num2 > 0 ? vector2 : vector);
    } else {
        agent.SetDestination(vector);
    }
}

void EnemyBehaviour::StopEnemy()
{
    if (enemy.hasNavAgent) enemy.navAgent.Stop();
}

void EnemyBehaviour::AttackPlayer()
{
    enemyState = EnemyState::Attacking;
    SetAnimatorTrigger("Attacking", true);
}

void EnemyBehaviour::LookToPosition()
{
    const Vector2 p = enemy.position(), h = HeroPosition();
    distanceX = h.x - p.x;
    distanceY = h.y - p.y;
    if (Mathf::Abs(distanceX) >= Mathf::Abs(distanceY)) {
        SetAnimatorFloat("DistanceX", distanceX);
        SetAnimatorFloat("DistanceY", 0.0f);
    } else {
        SetAnimatorFloat("DistanceY", distanceY);
        SetAnimatorFloat("DistanceX", 0.0f);
    }
}

void EnemyBehaviour::AttackHit(bool otherIsPlayer)
{
    EnemyHero* hero = enemy.scene.hero;
    if (!otherIsPlayer || !hero) return;
    if (hero->isShieldProtected && hero->isShieldProtected(enemy)) return;
    if (hero->isDead || !enemy.enemyStats.isAlive()) return;
    // (effectOnAttack: a MoonlighterEffect put on the hero; none on the enemies ported.)
    if (hero->dealDamage) hero->dealDamage(enemy.enemyStats.strength(), enemy.name, enemy);
}

void EnemyBehaviour::PushPlayerBackwards() { PushPlayerBackwards(attackPushBackwardsForce); }

void EnemyBehaviour::PushPlayerBackwards(float attackPushForce)
{
    // (The one-argument version also starts the hero's WaitAndCheckForHoles for
    // stopCheckingHolesDelay: the hero's lane.)
    EnemyHero* hero = enemy.scene.hero;
    if (!hero || !hero->addForce) return;
    const Vector2 p = enemy.position();
    const Vector2 vector = unity::Normalized(Vector2{hero->position.x - p.x, hero->position.y - p.y});
    hero->addForce(vector * attackPushForce);
}

void EnemyBehaviour::EnemyDashTowardsThePlayer(float dashPushForce)
{
    const Vector2 p = enemy.position(), h = HeroPosition();
    const Vector2 vector = unity::Normalized(Vector2{h.x - p.x, h.y - p.y});
    enemy.AddImpulse(vector * dashPushForce);
}

void EnemyBehaviour::ShieldPushEnemy(Vector2 direction, float dashPushForce)
{
    const Vector2 vector = unity::Normalized(direction);
    enemy.AddImpulse(vector * dashPushForce * enemy.enemyStats.shieldPushForce());
}

void EnemyBehaviour::ForbidEnemyMovement()
{
    if (enemy.hasNavAgent) enemy.navAgent.Stop();
    canMove = false;
}

void EnemyBehaviour::AllowEnemyMovement()
{
    // (!isRigidbodyStatic: Rigidbody2D.isKinematic = false; the bodies ported stay Dynamic.)
    canMove = true;
}

void EnemyBehaviour::EnemyWasHit(float newHealthValue)
{
    // Inside CurrentHealth's setter: the health is still the old one.
    if (enemy.enemyStats.CurrentHealth() > newHealthValue) HealthDecreasedWhenHit(newHealthValue);
    else if (enemy.enemyStats.CurrentHealth() == newHealthValue) HealthNotChangedWhenHit(newHealthValue);
}

void EnemyBehaviour::PlayDamagedEffect()
{
    SetPlayingDamagedEffectToTrue();
    unity::DelayedCalls& calls = Calls();
    calls.CancelInvoke(this, "SetEnemyShaderColorToWhite");
    calls.CancelInvoke(this, "SetEnemyShaderColorToRed");
    // (The game cancels "SetEnemyShaderColorToDefault", a name nothing invokes: an earlier
    // hit's SetEnemyShaderToDefault still comes.)
    calls.CancelInvoke(this, "SetEnemyShaderColorToDefault");
    SetEnemyShaderFlashAmount(1.0f);
    SetEnemyShaderColorToRed();
    const float num = Constants::GetFloat("enemyDamagedRedTime");
    const float num2 = Constants::GetFloat("enemyDamagedWhiteTime");
    calls.Invoke(this, "SetEnemyShaderColorToWhite", num, [this] { SetEnemyShaderColorToWhite(); });
    calls.Invoke(this, "SetEnemyShaderColorToRed", num + num2, [this] { SetEnemyShaderColorToRed(); });
    calls.Invoke(this, "SetEnemyShaderToDefault", 2.0f * num + num2, [this] { SetEnemyShaderToDefault(); });
    // StartCoroutine(SetPlayingDamagedEffectToFalse(LastDamageReceived, 2 num + num2)).
    const EnemyDamageReceivedType type = enemy.LastDamageReceived;
    calls.Wait(this, 2.0f * num + num2, [this, type] { SetPlayingDamagedEffectToFalse(type); });
}

void EnemyBehaviour::SetPlayingDamagedEffectToTrue()
{
    if (enemy.LastDamageReceived != EnemyDamageReceivedType::Effect) enemy.enemyStats.SetInvencible(true);
    playingDamagedEffect = true;
    ForbidEnemyMovement();
}

void EnemyBehaviour::SetPlayingDamagedEffectToFalse(EnemyDamageReceivedType type)
{
    if (type != EnemyDamageReceivedType::Effect) enemy.enemyStats.SetInvencible(false);
    playingDamagedEffect = false;
    if (!hasConfusedEffect) AllowEnemyMovement();
}

void EnemyBehaviour::SetEnemyShaderFlashAmount(float value) { enemy.flashAmount = value; }

void EnemyBehaviour::SetEnemyShaderColor(const float color[4])
{
    for (int i = 0; i < 4; i++) enemy.color[i] = color[i];
}

void EnemyBehaviour::SetEnemyShaderColorToRed() { SetEnemyShaderColor(kRed); }

void EnemyBehaviour::SetEnemyShaderColorToWhite() { SetEnemyShaderColor(kWhite); }

void EnemyBehaviour::SetEnemyShaderToDefault()
{
    if (enemy.enemyStats.isChampion) SetEnemyShaderColor(enemy.enemyStats.tint());
    else SetEnemyShaderColorToWhite();
    SetEnemyShaderFlashAmount(0.0f);
}

void EnemyBehaviour::SpawnDeathParticles()
{
    if (enemy.scene.spawnDeathParticles) enemy.scene.spawnDeathParticles(enemy);
}

void EnemyBehaviour::HealthDecreasedWhenHit(float)
{
    if (!IsEnemyPlayingDamagedEffect()) PlayDamagedEffect();
}

void EnemyBehaviour::HealthNotChangedWhenHit(float) { SetAnimatorTrigger("NotDamagedWhenHit", true); }

void EnemyBehaviour::SetInvencible() { enemy.enemyStats.SetInvencible(true); }

void EnemyBehaviour::SetVencible() { enemy.enemyStats.SetInvencible(false); }

void EnemyBehaviour::OnEnemyDestroyed()
{
    PlayDeathSound();
    subscribedToRoomEnter = false;
}

void EnemyBehaviour::OnPlayerLeaveRoom() { SetVencible(); }

void EnemyBehaviour::OnDeath()
{
    const float num = 0.6f;
    if (enemy.destroyed) return; // (!gameObject)
    enemy.layer = kLayerTransparentFX;
    if (enemy.physics) enemy.physics->SetLayer(enemy, kLayerTransparentFX);
    // The sprite fades to grey and to the flash colour over 0.6 s (DOTween, the default ease),
    // from what the material has when the tweens start. Those tweens don't say SetUpdate, and
    // the project's default is time-scale independent: they run through the weapon's hit-stop
    // (slime.jsonl: the colour moves on in frames whose deltaTime is 0), on DOTween's own
    // unscaled clock (realtimeSinceStartup between its updates).
    if (unity::Tweens* tw = enemy.scene.tweens) {
        Enemy* e = &enemy;
        const unity::Ease ease = unity::Ease::OutQuad;
        tw->Move([e] { return Vector2{e->flashAmount, 0.0f}; }, [e](Vector2 v) { e->flashAmount = v.x; }, Vector2{1.0f, 0.0f}, num, ease, 0.0f,
                 true);
        // material.DOColor(0.6 grey): the colour's components as two pairs (the same per-component formula).
        tw->Move([e] { return Vector2{e->color[0], e->color[1]}; },
                 [e](Vector2 v) {
                     e->color[0] = v.x;
                     e->color[1] = v.y;
                 },
                 Vector2{0.6f, 0.6f}, num, ease, 0.0f, true);
        tw->Move([e] { return Vector2{e->color[2], e->color[3]}; },
                 [e](Vector2 v) {
                     e->color[2] = v.x;
                     e->color[3] = v.y;
                 },
                 Vector2{0.6f, 1.0f}, num, ease, 0.0f, true);
    }
    unity::DelayedCalls& calls = Calls();
    calls.Invoke(this, "SpawnDeathParticles", num * 0.8f, [this] { SpawnDeathParticles(); });
    Enemy* e = &enemy;
    calls.Destroy(e, num, [e] { e->Destroy(); }); // Object.Destroy(gameObject, 0.6)
    if (enemy.enemyAnimator) {
        enemy.enemyAnimator->speed = 0.0f;
        enemy.animatorEnabled = false;
    }
    if (enemy.body) {
        // A push away from the hero: (position - his).normalized * deadForce.
        const Vector2 p = enemy.position(), h = HeroPosition();
        const Vector2 away = unity::Normalized(Vector2{p.x - h.x, p.y - h.y});
        enemy.AddImpulse(away * static_cast<float>(deadForce));
    }
    enabled = false;
}

// ---- EnemyPhysics -----------------------------------------------------------------------

// Box2D's BeginContact for the attack triggers (sensors: they begin when their shapes start
// overlapping, at a step's collide).
class EnemyPhysics::Listener : public b2ContactListener {
public:
    explicit Listener(EnemyPhysics& owner) : owner_(owner) {}
    void BeginContact(b2Contact* contact) override
    {
        b2Fixture* a = contact->GetFixtureA();
        b2Fixture* b = contact->GetFixtureB();
        for (Enemy* e : owner_.enemies_) {
            if (!e->attackFixture) continue;
            b2Fixture* other = a == e->attackFixture ? b : b == e->attackFixture ? a : nullptr;
            if (!other || other->GetBody() == e->attackFixture->GetBody()) continue;
            Hit hit;
            hit.enemy = e;
            hit.player = owner_.hero_ && other->GetBody() == owner_.hero_->Body() && !other->IsSensor();
            owner_.hits_.push_back(hit);
        }
    }

private:
    EnemyPhysics& owner_;
};

EnemyPhysics::EnemyPhysics(unity::PhysicsWorld& world, const Options& options) : world_(world), options_(options)
{
    listener_ = new Listener(*this);
    world_.World().SetContactListener(listener_);
}

EnemyPhysics::~EnemyPhysics()
{
    world_.World().SetContactListener(nullptr);
    delete listener_;
}

void EnemyPhysics::Add(Enemy& e)
{
    e.physics = this;
    enemies_.push_back(&e);
    if (!e.hasRigidbody) return;
    e.body = &world_.AddBody(e.rigidbody, e.position(), e.layer, e.colliderOffset, e.colliderSize);
    if (e.hasAttackCollider) {
        // The child's trigger on the parent's body (it has no Rigidbody2D of its own); no
        // density, so the mass set by hand stays.
        const float hx = e.attackSize.x / 2.0f, hy = e.attackSize.y / 2.0f;
        const float x0 = e.attackOffset.x - hx, x1 = e.attackOffset.x + hx, y0 = e.attackOffset.y - hy, y1 = e.attackOffset.y + hy;
        const b2Vec2 corners[4] = {b2Vec2(x0, y0), b2Vec2(x1, y0), b2Vec2(x1, y1), b2Vec2(x0, y1)};
        b2PolygonShape shape;
        shape.Set(corners, 4);
        b2FixtureDef fd;
        fd.shape = &shape;
        fd.density = 0.0f;
        fd.isSensor = true;
        fd.userData = &e.attackLayer;
        e.attackFixture = e.body->Body()->CreateFixture(&fd);
    }
    if (!e.active || e.destroyed) e.body->Body()->SetActive(false);
    e.synced_ = e.position();
}

void EnemyPhysics::PlaceBody(Enemy& e, Vector2 bodyPosition)
{
    if (!e.body) return;
    b2Body* b = e.body->Body();
    b->SetTransform(b2Vec2(bodyPosition.x, bodyPosition.y), b->GetAngle());
    e.synced_ = e.position();
}

void EnemyPhysics::SetLayer(Enemy& e, int layer)
{
    if (e.body) world_.SetLayer(*e.body, layer);
}

void EnemyPhysics::SetBodyActive(Enemy& e, bool active)
{
    if (e.body) e.body->Body()->SetActive(active);
}

void EnemyPhysics::Sync()
{
    // Physics2D syncs the transforms the scripts moved since the last write-back.
    for (Enemy* e : enemies_) {
        if (!e->body || e->destroyed || !e->active) continue;
        const Vector2 p = e->position();
        if (p.x != e->synced_.x || p.y != e->synced_.y || e->transformDirty_) {
            world_.SyncTransform(*e->body, p);
            e->synced_ = p;
            e->transformDirty_ = false;
        }
    }
}

void EnemyPhysics::WriteBack()
{
    for (Enemy* e : enemies_) {
        if (!e->body || e->destroyed || !e->active || !e->body->IsAwake()) continue;
        const Vector2 b = e->body->position();
        if (!options_.writeBackUnmoved && b.x == e->synced_.x && b.y == e->synced_.y) continue;
        e->setPosition(b);
        e->synced_ = e->position();
    }
}

void EnemyPhysics::ReportTriggers()
{
    // EnemyAttackCollisionDetection.OnTriggerEnter2D -> gameObjectToAdvise's AttackHit (sent to
    // the dead too: AttackHit checks).
    for (const Hit& h : hits_)
        if (!h.enemy->destroyed && h.enemy->active && h.enemy->enemyBehaviour) h.enemy->enemyBehaviour->AttackHit(h.player);
}

void EnemyPhysics::Step(float dt)
{
    Sync();
    hits_.clear();
    world_.Step(dt);
    // Unity's trigger pass after the solve: a trigger paired at the step's end (FindNewContacts,
    // after the solve) reports in this step, not the next (slime.jsonl: the hero back on Default
    // inside a slime's trigger is hit in the first step, while his body only meets theirs in
    // the second, where he's intangible again).
    world_.UpdateContacts();
    WriteBack();
    ReportTriggers();
}

} // namespace game
