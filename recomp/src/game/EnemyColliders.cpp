#include "game/EnemyColliders.h"

#include <Box2D/Box2D.h>

namespace game {

using unity::Vector2;

namespace {

// The root box's properties a clip can drive (path "": the Animator's own object).
const char* const kRootPath = "";
const char* const kRootAttributes[4] = {"m_Offset.x", "m_Offset.y", "m_Size.x", "m_Size.y"};

// A box fixture on `body` (local to it), a sensor without mass for a trigger.
b2Fixture* MakeBoxFixture(b2Body* body, Vector2 center, Vector2 size, bool sensor, void* userData)
{
    const float hx = size.x / 2.0f, hy = size.y / 2.0f;
    const float x0 = center.x - hx, x1 = center.x + hx, y0 = center.y - hy, y1 = center.y + hy;
    const b2Vec2 corners[4] = {b2Vec2(x0, y0), b2Vec2(x1, y0), b2Vec2(x1, y1), b2Vec2(x0, y1)};
    b2PolygonShape shape;
    shape.Set(corners, 4);
    b2FixtureDef fd;
    fd.shape = &shape;
    fd.density = 0.0f; // (the mass set by hand stays)
    fd.friction = 0.4f;
    fd.isSensor = sensor;
    fd.userData = userData;
    return body->CreateFixture(&fd);
}

} // namespace

float EnemyDamageReceiver::DealDamageToEnemy(float damage, EnemyDamageReceivedType type) const
{
    if (!advise) return 0.0f;
    return advise->DealDamageToEnemy(damage, type);
}

WeaponHitResult WeaponMainAttackHit(Enemy& enemy, float strength, Vector2 heroPosition, float pushForce)
{
    WeaponHitResult r;
    r.enemy = &enemy;
    r.pushed = true;
    enemy.PushBackwards(heroPosition, pushForce);
    r.damage = enemy.DealDamageToEnemy(strength);
    return r;
}

WeaponHitResult WeaponMainAttackHit(const EnemyDamageReceiver& receiver, float strength)
{
    WeaponHitResult r;
    r.enemy = receiver.advise;
    r.damage = receiver.DealDamageToEnemy(strength);
    return r;
}

// ---- The root box -----------------------------------------------------------------------

void Enemy::BindRootBox()
{
    if (!controller_ || !enemyAnimator) return;
    for (int i = 0; i < 4; i++) {
        rootProps_[i] = controller_->Property(kRootPath, kRootAttributes[i]);
        // What Unity binds as the defaults: the box now (write defaults put it back).
        const float now[4] = {colliderOffset.x, colliderOffset.y, colliderSize.x, colliderSize.y};
        if (rootProps_[i] >= 0) enemyAnimator->SetDefault(kRootPath, kRootAttributes[i], now[i]);
    }
}

void Enemy::ApplyRootBox()
{
    if (rootProps_[0] < 0 && rootProps_[1] < 0 && rootProps_[2] < 0 && rootProps_[3] < 0) return;
    unity::Animator& a = *enemyAnimator;
    float* to[4] = {&colliderOffset.x, &colliderOffset.y, &colliderSize.x, &colliderSize.y};
    for (int i = 0; i < 4; i++)
        if (rootProps_[i] >= 0 && a.Driven(rootProps_[i])) *to[i] = a.Property(rootProps_[i]);
    if (physics) physics->SyncRootCollider(*this);
}

// ---- EnemyPhysics -----------------------------------------------------------------------

void EnemyPhysics::AddChildColliders(Enemy& e)
{
    if (!e.body) return;
    b2Body* b = e.body->Body();
    e.rootFixture = b->GetFixtureList(); // AddBody's box
    if (e.colliderTrigger && e.rootFixture) {
        // A trigger: a sensor without mass (the mass set by hand, at the body's origin).
        void* layer = e.rootFixture->GetUserData();
        b->DestroyFixture(e.rootFixture);
        e.rootFixture = MakeBoxFixture(b, e.colliderOffset, e.colliderSize, true, layer);
        if (b->GetType() == b2_dynamicBody) {
            b2MassData md;
            md.mass = e.rigidbody.mass;
            md.center = b2Vec2(0.0f, 0.0f);
            md.I = 0.0f;
            b->SetMassData(&md);
        }
    }
    e.rootBuiltOffset_ = e.colliderOffset;
    e.rootBuiltSize_ = e.colliderSize;
    for (EnemyDamageReceiver& r : e.damageReceivers) {
        if (!r.active || !r.enabled) continue;
        r.fixture = MakeBoxFixture(b, r.position + r.offset, r.size, r.trigger, &r.layer);
    }
}

void EnemyPhysics::SyncRootCollider(Enemy& e)
{
    if (!e.body || !e.rootFixture) return;
    if (e.colliderOffset.x == e.rootBuiltOffset_.x && e.colliderOffset.y == e.rootBuiltOffset_.y && e.colliderSize.x == e.rootBuiltSize_.x &&
        e.colliderSize.y == e.rootBuiltSize_.y)
        return;
    b2Body* b = e.body->Body();
    void* layer = e.rootFixture->GetUserData();
    const bool sensor = e.rootFixture->IsSensor();
    // (No density either way: the body's mass data stays as it is.)
    b2MassData md;
    b->GetMassData(&md);
    b->DestroyFixture(e.rootFixture);
    e.rootFixture = MakeBoxFixture(b, e.colliderOffset, e.colliderSize, sensor, layer);
    if (b->GetType() == b2_dynamicBody) b->SetMassData(&md);
    e.rootBuiltOffset_ = e.colliderOffset;
    e.rootBuiltSize_ = e.colliderSize;
    world_.RestartRest(*e.body);
}

EnemyDamageReceiver* EnemyPhysics::ReceiverOf(const b2Fixture* fixture)
{
    if (!fixture) return nullptr;
    for (Enemy* e : enemies_)
        for (EnemyDamageReceiver& r : e->damageReceivers)
            if (r.fixture == fixture) return &r;
    return nullptr;
}

} // namespace game
