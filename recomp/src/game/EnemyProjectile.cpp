#include "game/EnemyProjectile.h"

#include <algorithm>

#include <Box2D/Box2D.h>

#include "game/BrokenGolemTurret.h"

namespace game {

using unity::Vector2;

// ---- EnemyProjectile ------------------------------------------------------------------

EnemyProjectile::EnemyProjectile(EnemyProjectiles& sys, const Prefab& prefab, const unity::AnimatorController* controller)
    : system(sys), prefabName(prefab.name), controller_(controller)
{
    const PrefabNode& root = prefab.nodes.at(0);
    localPosition = {root.x, root.y};
    layer = root.objectLayer;
    tag = root.tag;
    for (const ColliderDef& c : root.colliders)
        if (c.type == "BoxCollider2D") {
            boxOffset = {c.ox, c.oy};
            boxSize = {c.sx, c.sy};
            isTrigger = c.trigger;
            break;
        }
    rigidbody = root.rigidbody;
    // The renderer's sprite as the prefab has it (the Animator's from its first pass).
    for (const PrefabNode& n : prefab.nodes)
        if (n.HasRenderer() && !n.sprite.empty()) {
            sprite = defaultSprite = n.sprite;
            break;
        }
    if (controller_) animator = std::make_unique<unity::Animator>(*controller_);
}

EnemyProjectile::~EnemyProjectile() = default;

EnemyScene& EnemyProjectile::scene() const { return system.scene; }

Vector2 EnemyProjectile::position() const
{
    // Transform.position: the local position through each parent, nearest first.
    Vector2 p = localPosition;
    for (const Vector2& parent : parents) p = p + parent;
    return p;
}

void EnemyProjectile::SetLocalFromWorld(Vector2 value)
{
    // The parents' InverseTransformPoint, from the root down.
    Vector2 p = value;
    for (auto it = parents.rbegin(); it != parents.rend(); ++it) p = p - *it;
    localPosition = p;
}

void EnemyProjectile::setPosition(Vector2 value)
{
    SetLocalFromWorld(value);
    transformDirty_ = true;
}

void EnemyProjectile::SetParentActive(const std::vector<Vector2>& newParents)
{
    const Vector2 world = position();
    parents = newParents;
    SetLocalFromWorld(world);
    transformDirty_ = true;
    if (!active) system.Activate(*this);
}

void EnemyProjectile::SetTrigger(bool value)
{
    const bool changed = isTrigger != value;
    isTrigger = value;
    if (!body) return;
    // Unity makes the box's shapes again when isTrigger changes (FixtureProbe's "trigger" in
    // "fxOwn"): its contacts end there, and Box2D's e_newFixture has the next step pair the moved
    // proxies before it collides. enemy_BrokenGolemTurret.jsonl (enemyfix-turret-2): frame 206, a
    // shot crashed in the step before, and the hero synced into a turret is pushed out in the next
    // step, not the one after; frame 301, the shot a crashed one had hit hands back no contacts.
    if (changed) system.RemakeFixture(*this);
    else
        for (b2Fixture* f = body->Body()->GetFixtureList(); f; f = f->GetNext()) f->SetSensor(value);
}

void EnemyProjectile::UpdateAnimator(float deltaTime)
{
    if (!active || !animator) return;
    animator->Update(deltaTime);
    animatorFresh = false;
    if (const std::string* s = animator->Sprite()) sprite = *s;
}

void EnemyProjectile::OnReturnedToPool()
{
    // SetParent(the pool's parent), keeping the world position: inactive.
    const Vector2 world = position();
    parents.clear();
    SetLocalFromWorld(world);
    system.Deactivate(*this);
}

// ---- EnemyProjectiles -----------------------------------------------------------------

EnemyProjectiles::EnemyProjectiles(EnemyScene& s) : scene(s) {}

EnemyProjectiles::~EnemyProjectiles() = default;

void EnemyProjectiles::Register(const Prefab* prefab, const unity::AnimatorController* controller)
{
    if (prefab) prefabs_[prefab->name] = {prefab, controller};
}

PoolManager::Maker EnemyProjectiles::Model(const std::string& prefabName)
{
    const auto it = prefabs_.find(prefabName);
    if (it == prefabs_.end()) return {};
    const Prefab* prefab = it->second.first;
    const unity::AnimatorController* controller = it->second.second;
    return [this, prefab, controller]() -> std::unique_ptr<PooledObject> { return MakeProjectile(*this, *prefab, controller); };
}

void EnemyProjectiles::SetPhysics(unity::PhysicsWorld& world, EnemyPhysics& physics)
{
    world_ = &world;
    physics.AddCompanion(this);
}

std::vector<EnemyProjectile*> EnemyProjectiles::All() const
{
    std::vector<EnemyProjectile*> out;
    for (const auto& o : poolManager.Objects())
        if (auto* p = dynamic_cast<EnemyProjectile*>(o.get())) out.push_back(p);
    return out;
}

void EnemyProjectiles::UpdateAnimators(float deltaTime)
{
    for (EnemyProjectile* p : All()) p->UpdateAnimator(deltaTime);
}

void EnemyProjectiles::Activate(EnemyProjectile& p)
{
    p.active = true;
    if (!p.body && world_) {
        // Its Awake: the Rigidbody2D and box made where the transform is now.
        p.body = &world_->AddBody(p.rigidbody, p.position(), p.layer, p.boxOffset, p.boxSize);
        if (p.isTrigger) p.SetTrigger(true);
        p.synced_ = p.position();
    } else if (p.body) {
        p.body->Body()->SetActive(true);
        // Unity makes the box's fixtures again as the object comes back on: Box2D's e_newFixture
        // (the next step pairs the moved proxies before it collides), which b2Body::SetActive
        // doesn't flag (the first activation's AddBody does, by CreateFixture). FixtureProbe's
        // "made" (oracle: the shots count in "fxOwn", which a replay doesn't take this from).
        if (world_) world_->FixtureMadeElsewhere();
        if (!keepVelocityInPool) p.body->SetVelocity(Vector2::zero());
    }
    // The Animator starts over (its default state, its parameters' defaults).
    if (p.controller_) p.animator = std::make_unique<unity::Animator>(*p.controller_);
    p.animatorFresh = true;
    touching_.erase(std::remove_if(touching_.begin(), touching_.end(), [&](const auto& t) { return t.first == &p; }), touching_.end());
    p.Start();
}

void EnemyProjectiles::Deactivate(EnemyProjectile& p)
{
    p.active = false;
    // The Animator switched off with the object writes its properties' default values back
    // (keepAnimatorControllerStateOnDisable off): the sprite its clips drive, the prefab's again.
    // enemy_BrokenGolemTurret.jsonl (enemyfix-turret-2), frame 776: shot 0, crashed (its last
    // sprite golemturret_projectile_collision_04), shows golemturret_projectile_01 the frame it's
    // shot again, before its Animator's first pass.
    if (p.animator) p.sprite = p.defaultSprite;
    if (p.body) p.body->Body()->SetActive(false);
    touching_.erase(std::remove_if(touching_.begin(), touching_.end(), [&](const auto& t) { return t.first == &p; }), touching_.end());
}

void EnemyProjectiles::RemakeFixture(EnemyProjectile& p)
{
    b2Body* b = p.body->Body();
    b2MassData mass;
    b->GetMassData(&mass);
    std::vector<b2Fixture*> old;
    for (b2Fixture* f = b->GetFixtureList(); f; f = f->GetNext()) old.push_back(f);
    // (What touched the old shapes: no longer, as their contacts end.)
    auto gone = [&](const b2Fixture* f) { return std::find(old.begin(), old.end(), f) != old.end(); };
    touching_.erase(std::remove_if(touching_.begin(), touching_.end(), [&](const auto& t) { return t.first == &p || gone(t.second); }),
                    touching_.end());
    points_.erase(std::remove_if(points_.begin(), points_.end(), [&](const PointsRow& r) { return r.p == &p || gone(r.other); }), points_.end());
    // Box2D lists a body's fixtures newest first: made again in their order.
    for (auto it = old.rbegin(); it != old.rend(); ++it) {
        b2Fixture* f = *it;
        b2PolygonShape shape = *static_cast<const b2PolygonShape*>(f->GetShape());
        b2FixtureDef fd;
        fd.shape = &shape;
        fd.density = f->GetDensity();
        fd.friction = f->GetFriction();
        fd.restitution = f->GetRestitution();
        fd.filter = f->GetFilterData();
        fd.userData = f->GetUserData();
        fd.isSensor = p.isTrigger;
        b->DestroyFixture(f);
        b->CreateFixture(&fd);
    }
    if (b->GetType() == b2_dynamicBody) b->SetMassData(&mass); // (the mass the body had)
}

void EnemyProjectiles::Sync()
{
    if (!world_) return;
    for (EnemyProjectile* p : All()) {
        if (!p->active || !p->body) continue;
        const Vector2 at = p->position();
        if (at.x != p->synced_.x || at.y != p->synced_.y || p->transformDirty_) {
            world_->SyncTransform(*p->body, at);
            p->synced_ = at;
            p->transformDirty_ = false;
        }
    }
}

Collision2D EnemyProjectiles::Describe(const b2Fixture* other) const
{
    Collision2D c;
    c.otherFixture = other;
    const b2Body* b = other->GetBody();
    if (hero_ && b == hero_->Body()) {
        c.hero = true;
        c.tag = "Player";
        c.name = "HeroMerchant";
        return c;
    }
    for (EnemyProjectile* p : All())
        if (p->body && p->body->Body() == b) {
            c.projectile = p;
            c.tag = p->tag;
            c.name = p->name;
            return c;
        }
    if (world_)
        if (const unity::Collider2D* col = world_->ColliderOf(other)) {
            c.collider = col;
            c.tag = col->tag;
            const size_t slash = col->path.rfind('/');
            c.name = slash == std::string::npos ? col->path : col->path.substr(slash + 1);
        }
    return c;
}

void EnemyProjectiles::AfterWorldStep()
{
    if (!world_) return;
    std::vector<std::pair<const EnemyProjectile*, const b2Fixture*>> now;
    points_.clear();
    const std::vector<EnemyProjectile*> all = All();
    auto projectileOf = [&](const b2Body* b) -> EnemyProjectile* {
        for (EnemyProjectile* p : all)
            if (p->active && p->body && p->body->Body() == b) return p;
        return nullptr;
    };
    for (b2Contact* c = world_->World().GetContactList(); c; c = c->GetNext()) {
        if (!c->IsTouching()) continue;
        const b2Fixture* fa = c->GetFixtureA();
        const b2Fixture* fb = c->GetFixtureB();
        if (fa->IsSensor() || fb->IsSensor()) continue;
        for (int side = 0; side < 2; side++) {
            const b2Fixture* mine = side == 0 ? fa : fb;
            const b2Fixture* other = side == 0 ? fb : fa;
            EnemyProjectile* p = projectileOf(mine->GetBody());
            if (!p) continue;
            now.emplace_back(p, other);
            points_.push_back({p, other, c->GetManifold()->pointCount});
            if (std::find(touching_.begin(), touching_.end(), std::make_pair(static_cast<const EnemyProjectile*>(p), other)) == touching_.end())
                enters_.emplace_back(p, Describe(other));
        }
    }
    touching_ = std::move(now);
}

int EnemyProjectiles::GetContactsCount(const EnemyProjectile& p) const
{
    if (p.isTrigger) return 0;
    int n = 0;
    for (const PointsRow& r : points_)
        if (r.p == &p) n += r.points;
    return n;
}

void EnemyProjectiles::WriteBack()
{
    for (EnemyProjectile* p : All()) {
        if (!p->active || !p->body || !p->body->IsAwake()) continue;
        p->SetLocalFromWorld(p->body->position());
        p->synced_ = p->position();
    }
}

void EnemyProjectiles::Report()
{
    if (heroFollowsBody && hero_ && scene.hero) scene.hero->position = hero_->position();
    std::vector<std::pair<EnemyProjectile*, Collision2D>> enters = std::move(enters_);
    enters_.clear();
    if (enters.size() > 1 && orderEnters) orderEnters(enters);
    for (auto& [p, collision] : enters) {
        if (onCollisionEnter) onCollisionEnter(*p, collision, false);
        p->OnCollisionEnter2D(collision);
        if (onCollisionEnter) onCollisionEnter(*p, collision, true);
    }
}

std::unique_ptr<EnemyProjectile> MakeProjectile(EnemyProjectiles& system, const Prefab& prefab, const unity::AnimatorController* controller)
{
    for (const Behaviour& b : prefab.nodes.at(0).behaviours)
        if (b.cls == "BrokenGolemTurretProjectile") return MakeBrokenGolemTurretProjectile(system, prefab, controller, b);
    return std::make_unique<EnemyProjectile>(system, prefab, controller);
}

} // namespace game
