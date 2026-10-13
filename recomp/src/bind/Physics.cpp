#include "bind/Physics.h"

#include <algorithm>
#include <cstdio>

#include <Box2D/Box2D.h>

#include "unity/Physics2DOverlap.h"

namespace UnityEngine {

namespace {
bind::PhysicsScene* P() { return bind::Scene::current ? bind::Scene::current->physics : nullptr; }
} // namespace

// ---- Collider2D ------------------------------------------------------------------------------------

Rigidbody2D* Collider2D::get_attachedRigidbody()
{
    for (Transform* t = get_transform(); t; t = t->parent_)
        if (auto* rb = t->gameObject_->GetComponent<Rigidbody2D*>()) return rb;
    return nullptr;
}

void Collider2D::Changed() { changed_ = true; }
void Collider2D::cs_ActiveChanged(bool) { Changed(); }
void Collider2D::cs_EnabledChanged(bool) { Changed(); }
void Collider2D::cs_Destroyed()
{
    Changed();
    if (P()) P()->ColliderDestroyed(this);
}

void Collider2D::set_isTrigger(bool v)
{
    if (isTrigger_ == v) return;
    isTrigger_ = v;
    Changed();
    if (P()) P()->TriggerChanged(this);
}

cs::Array<Vector2>* PolygonCollider2D::GetPath(int32_t index)
{
    if (index < 0 || index >= get_pathCount()) cs::ThrowArgumentOutOfRange();
    return cs::Array<Vector2>::FromVector(paths_[static_cast<size_t>(index)]);
}

void PolygonCollider2D::SetPath(int32_t index, cs::Array<Vector2>* points)
{
    if (index < 0) cs::ThrowArgumentOutOfRange();
    if (static_cast<size_t>(index) >= paths_.size()) paths_.resize(static_cast<size_t>(index) + 1);
    paths_[static_cast<size_t>(index)] = cs::nc(points)->vec().ToStd();
    Changed();
}

// ---- Rigidbody2D --------------------------------------------------------------------------------------

// The body is made when the object is first active (Unity's: a Rigidbody2D under an inactive
// parent has no body: a pool's objects).
void Rigidbody2D::cs_Attached()
{
    if (P() && gameObject_->get_activeInHierarchy()) P()->Add(this);
}

void Rigidbody2D::cs_ActiveChanged(bool active)
{
    if (!P()) return;
    if (active && !body_) {
        P()->Add(this);
        return;
    }
    P()->SetActive(this, active && simulated_);
    if (active) {
        // Back on: its shapes are made again (Box2D's e_newFixture, which b2Body::SetActive doesn't
        // flag: the next step pairs the moved proxies before it collides), and the colliders below
        // it as they are now.
        P()->world.FixtureMadeElsewhere();
        P()->SyncColliders(this);
    }
}

void Rigidbody2D::cs_Destroyed()
{
    if (P()) P()->Remove(this);
}

Vector2 Rigidbody2D::get_velocity() { return body_ ? body_->velocity() : Vector2{}; }

void Rigidbody2D::set_velocity(Vector2 v)
{
    if (body_) body_->SetVelocity(v);
}

void Rigidbody2D::AddForce(Vector2 force, ForceMode2D mode)
{
    if (!body_ || !gameObject_->get_activeInHierarchy() || destroyed_) return;
    b2Body* b = body_->Body();
    if (mode == ForceMode2D::Impulse) b->ApplyLinearImpulse(b2Vec2(force.x, force.y), b->GetWorldCenter(), true);
    else b->ApplyForce(b2Vec2(force.x, force.y), b->GetWorldCenter(), true);
}

float Rigidbody2D::get_mass() { return body_ ? body_->Body()->GetMass() : def_.mass; }

void Rigidbody2D::set_mass(float mass)
{
    def_.mass = mass;
    if (body_ && body_->bodyType() == unity::RigidbodyType2D::Dynamic) {
        // Rigidbody2D.mass = m: the centre of mass stays, the rotational inertia scales.
        b2MassData md;
        body_->Body()->GetMassData(&md);
        const float before = md.mass;
        md.I = before > 0.0f ? (md.I - before * b2Dot(md.center, md.center)) * (mass / before) + mass * b2Dot(md.center, md.center) : md.I;
        md.mass = mass;
        body_->Body()->SetMassData(&md);
    }
}

void Rigidbody2D::set_isKinematic(bool k)
{
    set_bodyType(k ? RigidbodyType2D::Kinematic : RigidbodyType2D::Dynamic);
}

void Rigidbody2D::set_bodyType(RigidbodyType2D t)
{
    const auto bt = static_cast<unity::RigidbodyType2D>(t);
    if (def_.bodyType == bt) return;
    def_.bodyType = bt;
    if (!body_) return;
    // (A body made kinematic and back: the mass set by hand comes back with it.)
    b2Body* b = body_->Body();
    b2MassData md;
    b->GetMassData(&md);
    b->SetType(bt == unity::RigidbodyType2D::Dynamic ? b2_dynamicBody : bt == unity::RigidbodyType2D::Kinematic ? b2_kinematicBody : b2_staticBody);
    if (bt == unity::RigidbodyType2D::Dynamic) {
        md.mass = def_.mass;
        b->SetMassData(&md);
    }
}

Vector2 Rigidbody2D::get_position()
{
    if (body_) return body_->position();
    const Vector3 p = get_transform()->get_position();
    return Vector2{p.x, p.y};
}

void Rigidbody2D::set_simulated(bool s)
{
    if (simulated_ == s) return;
    simulated_ = s;
    if (P()) P()->SetActive(this, s && gameObject_->get_activeInHierarchy());
}

bool Rigidbody2D::IsAwake() { return body_ && body_->IsAwake(); }
void Rigidbody2D::WakeUp()
{
    if (body_) body_->Body()->SetAwake(true);
}
void Rigidbody2D::Sleep()
{
    if (body_) body_->Body()->SetAwake(false);
}
void Rigidbody2D::set_drag(float d)
{
    def_.linearDrag = d;
    if (body_) body_->Body()->SetLinearDamping(d);
}

// ---- Collision2D ---------------------------------------------------------------------------------------

void Collision2D::cs_trace(cs::Tracer& t)
{
    t.Mark(collider);
    t.Mark(otherCollider);
}

GameObject* Collision2D::get_gameObject()
{
    if (!collider) return nullptr;
    if (Rigidbody2D* rb = collider->get_attachedRigidbody()) return rb->gameObject_;
    return collider->gameObject_;
}

Transform* Collision2D::get_transform()
{
    GameObject* g = get_gameObject();
    return g ? g->transform_ : nullptr;
}

// ---- Physics2D's queries ---------------------------------------------------------------------------------

Collider2D* Physics2D::OverlapBox(Vector2 point, Vector2 size, float angle, int32_t layerMask)
{
    bind::PhysicsScene* p = P();
    if (!p || !p->queries) return nullptr;
    const unity::Collider2D* c = p->queries->OverlapBox(point, size, angle, layerMask);
    return c ? p->ObjectOf(c) : nullptr;
}

RaycastHit2D Physics2D::Raycast(Vector2 origin, Vector2 direction, float distance, int32_t layerMask)
{
    RaycastHit2D r;
    bind::PhysicsScene* p = P();
    if (!p || !p->queries) return r;
    const unity::RaycastHit2D h = p->queries->Raycast(origin, direction, distance, layerMask);
    if (p->onRaycast) p->onRaycast(origin, direction, distance, layerMask, h);
    if (!h.collider) return r;
    r.collider = p->ObjectOf(h.collider);
    if (!r.collider) cs::NotTranslated("Physics2D.Raycast hit a room collider without its object (bind::PhysicsScene::RegisterQuery)");
    r.point = h.point;
    r.normal = h.normal;
    r.centroid = h.centroid;
    r.distance = h.distance;
    r.fraction = h.fraction;
    return r;
}

cs::Array<Collider2D*>* Physics2D::OverlapCircleAll(Vector2 point, float radius, int32_t layerMask)
{
    std::vector<Collider2D*> found;
    if (bind::PhysicsScene* p = P()) {
        const bool triggers = p->queries ? p->queries->queriesHitTriggers : true;
        for (const b2Fixture* f : unity::OverlapCircleAll(p->world, point, radius, layerMask, triggers)) {
            Collider2D* c = p->ObjectOf(f);
            if (!c) cs::NotTranslated("Physics2D.OverlapCircleAll found a fixture without its collider object (bind::PhysicsScene::Register)");
            found.push_back(c);
        }
        if (p->onOverlapCircle) p->onOverlapCircle(point, radius, found);
    }
    return cs::Array<Collider2D*>::FromVector(found);
}

} // namespace UnityEngine

namespace bind {

using namespace UnityEngine;

namespace {
// The default material's friction (b2FixtureDef's own is 0.2: Unity's is 0.4).
constexpr float kFriction = 0.4f;

b2Fixture* MakeBox(b2Body* body, Vector2 center, Vector2 size, bool sensor, float density, float friction, void* userData)
{
    const float hx = size.x / 2.0f, hy = size.y / 2.0f;
    const float x0 = center.x - hx, x1 = center.x + hx, y0 = center.y - hy, y1 = center.y + hy;
    const b2Vec2 corners[4] = {b2Vec2(x0, y0), b2Vec2(x1, y0), b2Vec2(x1, y1), b2Vec2(x0, y1)};
    b2PolygonShape shape;
    shape.Set(corners, 4);
    b2FixtureDef fd;
    fd.shape = &shape;
    fd.density = density;
    fd.friction = friction;
    fd.isSensor = sensor;
    fd.userData = userData;
    return body->CreateFixture(&fd);
}

bool SameV(Vector2 a, Vector2 b) { return a.x == b.x && a.y == b.y; }
} // namespace

// Box2D's BeginContact: what a trigger fixture made or remade since the last step touches in this
// step's collide starts its rest over (game::EnemyPhysics::Listener: enemy_KamikazeGolem.jsonl, the
// root body its AttackCollider's growing box overlaps); the pairs with a trigger (sensors begin when
// their shapes start overlapping, at a step's collide or in Unity's trigger pass after it), one per
// collider (a room collider of several fixtures, a PolygonCollider2D's pieces, entered through
// another of them enters no more: enemy_KamikazeGolem.jsonl of recq kamifix-2, frame 194).
class PhysicsScene::Listener : public b2ContactListener {
public:
    explicit Listener(PhysicsScene& owner) : owner_(owner) {}
    void BeginContact(b2Contact* contact) override
    {
        b2Fixture* a = contact->GetFixtureA();
        b2Fixture* b = contact->GetFixtureB();
        for (const b2Fixture* remade : owner_.remade_) {
            b2Fixture* other = a == remade ? b : b == remade ? a : nullptr;
            if (other && other->GetBody() != remade->GetBody() && other->GetBody()->GetType() != b2_staticBody)
                unity::PhysicsWorld::RestartRest(other->GetBody());
        }
        if (!a->IsSensor() && !b->IsSensor()) return;
        if (a->GetBody() == b->GetBody()) return;
        if (owner_.InColliderAlready(a, b) || owner_.InColliderAlready(b, a)) return;
        owner_.began_.emplace_back(a, b);
    }

private:
    PhysicsScene& owner_;
};

PhysicsScene::PhysicsScene(unity::PhysicsWorld& w, Scene& s) : world(w), scene(s)
{
    listener_ = new Listener(*this);
    world.World().SetContactListener(listener_);
    scene.physics = this;
}

PhysicsScene::~PhysicsScene()
{
    world.World().SetContactListener(nullptr);
    delete listener_;
    if (scene.physics == this) scene.physics = nullptr;
}

void PhysicsScene::Register(const b2Fixture* fixture, Collider2D* collider) { fixtures_[fixture] = collider; }

void PhysicsScene::RegisterQuery(const unity::Collider2D* collider, Collider2D* object) { queryObjects_.emplace_back(collider, object); }

Collider2D* PhysicsScene::ObjectOf(const unity::Collider2D* collider) const
{
    for (const auto& q : queryObjects_)
        if (q.first == collider) return q.second;
    return nullptr;
}

Collider2D* PhysicsScene::ObjectOf(const b2Fixture* fixture) const
{
    const auto it = fixtures_.find(fixture);
    return it == fixtures_.end() ? nullptr : it->second;
}

PhysicsScene::Body* PhysicsScene::Find(Rigidbody2D* rb)
{
    for (Body& b : bodies_)
        if (b.rb == rb) return &b;
    return nullptr;
}

bool PhysicsScene::BelowBody(const Body& b, const Rigidbody2D* other) const
{
    if (other == b.rb) return false;
    for (Transform* t = other->gameObject_->transform_->parent_; t; t = t->parent_)
        if (t == b.rb->gameObject_->transform_) return true;
    return false;
}

Vector2 PhysicsScene::LocalCenter(Body& b, Collider2D* c)
{
    // The collider's object's position in the body's frame (its local positions up to the
    // body's object), plus the collider's offset.
    Vector2 p;
    for (Transform* t = c->get_transform(); t && t != b.rb->get_transform(); t = t->parent_)
        p = Vector2{p.x + t->localPosition_.x, p.y + t->localPosition_.y};
    return Vector2{p.x + c->offset_.x, p.y + c->offset_.y};
}

// A fixture going away: what it touched and its pairs are forgotten (a new fixture can be made at
// its address).
namespace {
template <typename V, typename F>
void EraseIf(V& v, F f)
{
    v.erase(std::remove_if(v.begin(), v.end(), f), v.end());
}
} // namespace

void PhysicsScene::Carry(b2Body* body, b2Fixture* fixture, bool on)
{
    // Remade while on: what it touched doesn't enter again (nor what an earlier remake since the
    // last step carried).
    for (size_t k = carried_.size(); k-- > 0;)
        if (carried_[k].first == fixture) {
            if (on) carried_[k].first = nullptr;
            else carried_.erase(carried_.begin() + static_cast<std::ptrdiff_t>(k));
        }
    if (on)
        for (const b2ContactEdge* ce = body->GetContactList(); ce; ce = ce->next) {
            const b2Contact* ct = ce->contact;
            if (!ct->IsTouching()) continue;
            if (ct->GetFixtureA() == fixture) carried_.emplace_back(nullptr, ct->GetFixtureB());
            else if (ct->GetFixtureB() == fixture) carried_.emplace_back(nullptr, ct->GetFixtureA());
        }
}

void PhysicsScene::MadeTrigger(b2Fixture* fixture)
{
    if (!adding_) remade_.push_back(fixture);
    for (auto& cr : carried_)
        if (!cr.first) cr.first = fixture;
}

void PhysicsScene::Add(Rigidbody2D* rb)
{
    if (Find(rb) || rb->body_) return;
    GameObject* go = rb->gameObject_;
    Body nb;
    nb.rb = rb;
    nb.box = go->GetComponent<BoxCollider2D*>();
    const Vector3 pos = go->transform_->get_position();
    const bool dynamic = rb->def_.bodyType == unity::RigidbodyType2D::Dynamic;
    adding_ = true;
    if (dynamic) {
        if (!nb.box) cs::NotTranslated("a Dynamic Rigidbody2D without a BoxCollider2D on its object (bind/Physics.cpp)");
        rb->body_ = &world.AddBody(rb->def_, Vector2{pos.x, pos.y}, go->layer_, nb.box->offset_, nb.box->size_);
    } else {
        rb->body_ = &world.AddBody(rb->def_, Vector2{pos.x, pos.y}, go->layer_);
    }
    bodies_.push_back(nb);
    Body& b = bodies_.back();
    b2Body* body = rb->body_->Body();
    b.boxUserData = rb->body_->LayerData();
    if (dynamic) {
        b.boxFixture = body->GetFixtureList(); // AddBody's box
        fixtures_[b.boxFixture] = b.box;
        b.boxBuilt = true;
        b.boxOffset = b.box->offset_;
        b.boxSize = b.box->size_;
        if (b.box->isTrigger_) {
            // A trigger: a sensor without mass, the mass set by hand at the body's origin
            // (game::EnemyPhysics::AddChildColliders: the HermitSlime's root).
            fixtures_.erase(b.boxFixture);
            body->DestroyFixture(b.boxFixture);
            b.boxFixture = MakeBox(body, b.box->offset_, b.box->size_, true, 0.0f, kFriction, b.boxUserData);
            fixtures_[b.boxFixture] = b.box;
            b.boxSensor = true;
            b2MassData md;
            md.mass = rb->def_.mass;
            md.center = b2Vec2(0.0f, 0.0f);
            md.I = 0.0f;
            body->SetMassData(&md);
        }
    }
    if (b.box) b.box->changed_ = false;
    Rescan(b);
    for (Attached& a : b.attached) SyncAttached(b, a);
    // The box as it is: one off taken away (the mass kept), a Kinematic body's made if it's on.
    SyncOwnBox(b);
    adding_ = false;
    if (!go->get_activeInHierarchy() || !rb->simulated_) body->SetActive(false);
    rb->synced_ = Vector2{pos.x, pos.y};
}

// A fixture this scene won't see again: its pairs and its object forgotten (a new fixture can be
// made at its address).
void PhysicsScene::ForgetFixture(const b2Fixture* f)
{
    if (!f) return;
    fixtures_.erase(f);
    EraseIf(remade_, [&](const b2Fixture* r) { return r == f; });
    EraseIf(carried_, [&](const std::pair<const b2Fixture*, const b2Fixture*>& c) { return c.first == f || c.second == f; });
    EraseIf(touching_, [&](const Touch& t) { return t.self == f || t.other == f; });
    EraseIf(enters_, [&](const Touch& t) { return t.self == f || t.other == f; });
    EraseIf(stays_, [&](const Touch& t) { return t.self == f || t.other == f; });
    EraseIf(exits_, [&](const Touch& t) { return t.self == f || t.other == f; });
}

void PhysicsScene::Remove(Rigidbody2D* rb)
{
    SetActive(rb, false);
    for (size_t i = 0; i < bodies_.size(); i++) {
        if (bodies_[i].rb != rb) continue;
        Body& b = bodies_[i];
        // (The body stays in the world, inactive: no proxies, no contacts; its fixtures' userData
        // point at this scene's slots or the body's own layer, not at the destroyed objects.)
        ForgetFixture(b.boxFixture);
        for (const Attached& a : b.attached) ForgetFixture(a.fixture);
        bodies_.erase(bodies_.begin() + static_cast<std::ptrdiff_t>(i));
        return;
    }
}

void PhysicsScene::ColliderDestroyed(Collider2D* c)
{
    for (Body& b : bodies_) {
        b2Body* body = b.rb->body_ ? b.rb->body_->Body() : nullptr;
        if (b.box == c) {
            if (b.boxFixture && body) {
                b2MassData mass;
                body->GetMassData(&mass);
                ForgetFixture(b.boxFixture);
                body->DestroyFixture(b.boxFixture);
                if (body->GetType() == b2_dynamicBody) body->SetMassData(&mass);
            }
            b.boxFixture = nullptr;
            b.boxBuilt = false;
            b.box = nullptr;
        }
        for (size_t k = b.attached.size(); k-- > 0;) {
            Attached& a = b.attached[k];
            if (a.collider != c) continue;
            if (a.fixture && body) {
                b2MassData mass;
                body->GetMassData(&mass);
                ForgetFixture(a.fixture);
                body->DestroyFixture(a.fixture);
                if (body->GetType() == b2_dynamicBody) body->SetMassData(&mass);
            }
            b.attached.erase(b.attached.begin() + static_cast<std::ptrdiff_t>(k));
        }
    }
}

void PhysicsScene::SetActive(Rigidbody2D* rb, bool active)
{
    if (rb->body_) rb->body_->Body()->SetActive(active);
}

void PhysicsScene::LayerChanged(GameObject* go)
{
    if (auto* rb = go->GetComponent<Rigidbody2D*>())
        if (rb->body_) world.SetLayer(*rb->body_, go->layer_);
    for (Body& b : bodies_)
        for (Attached& a : b.attached)
            if (a.collider->gameObject_ == go) {
                a.collider->layerSlot_ = go->layer_;
                if (a.slot) *a.slot = go->layer_;
                if (a.fixture) a.fixture->Refilter();
            }
}

void PhysicsScene::PlaceBody(Rigidbody2D* rb, Vector2 bodyPosition)
{
    if (!rb->body_) return;
    b2Body* b = rb->body_->Body();
    b->SetTransform(b2Vec2(bodyPosition.x, bodyPosition.y), b->GetAngle());
    const Vector3 p = rb->get_transform()->get_position();
    rb->synced_ = Vector2{p.x, p.y};
    rb->get_transform()->physicsDirty_ = false;
}

void PhysicsScene::SyncColliders(Rigidbody2D* rb)
{
    if (Body* b = Find(rb)) {
        Rescan(*b);
        for (Attached& a : b->attached) SyncAttached(*b, a);
        SyncOwnBox(*b);
    }
}

void PhysicsScene::TriggerChanged(Collider2D* c)
{
    for (Body& b : bodies_)
        if (b.box == c) {
            RemakeOwnBox(b);
            return;
        }
    // (A child's collider: remade at its body's next sync.)
}

// The colliders on the body's object (but its box) and below it, down to an object with a
// Rigidbody2D of its own, in hierarchy order: the ones added since become the body's too.
void PhysicsScene::Rescan(Body& b)
{
    GameObject* go = b.rb->gameObject_;
    auto add = [&](Collider2D* col) {
        if (col == b.box) return;
        for (const Attached& a : b.attached)
            if (a.collider == col) return;
        b.attached.push_back(Attached{col});
    };
    for (Component* c : go->components_)
        if (auto* col = dynamic_cast<Collider2D*>(c)) add(col);
    std::vector<Transform*> stack(go->transform_->children_.rbegin(), go->transform_->children_.rend());
    while (!stack.empty()) {
        Transform* t = stack.back();
        stack.pop_back();
        if (t->gameObject_->GetComponent<Rigidbody2D*>()) continue;
        for (Component* c : t->gameObject_->components_)
            if (auto* col = dynamic_cast<Collider2D*>(c)) add(col);
        for (auto it = t->children_.rbegin(); it != t->children_.rend(); ++it) stack.push_back(*it);
    }
}

void PhysicsScene::SyncAttached(Body& b, Attached& a)
{
    Collider2D* c = a.collider;
    if (!b.rb->body_) return;
    const bool on = c->gameObject_->get_activeInHierarchy() && c->enabled_ && !c->destroyed_;
    const Vector2 center = LocalCenter(b, c);
    Vector2 size{0.0f, 0.0f};
    if (auto* box = dynamic_cast<BoxCollider2D*>(c)) size = box->size_;
    else if (on) cs::NotTranslated("a child collider other than a BoxCollider2D (bind/Physics.cpp)");
    c->changed_ = false;
    if (on == a.built && (!on || (center.x == a.center.x && center.y == a.center.y && size.x == a.size.x && size.y == a.size.y && a.sensor == c->isTrigger_)))
        return;
    b2Body* body = b.rb->body_->Body();
    // Box2D's DestroyFixture recomputes the mass from the densities: Unity keeps the mass set by
    // hand (useAutoMass off), so it goes back after.
    b2MassData mass;
    body->GetMassData(&mass);
    if (a.fixture) {
        if (a.fixture->IsSensor()) Carry(body, a.fixture, on);
        EraseIf(remade_, [&](const b2Fixture* f) { return f == a.fixture; });
        EraseIf(touching_, [&](const Touch& t) { return t.self == a.fixture || t.other == a.fixture; });
        fixtures_.erase(a.fixture);
        body->DestroyFixture(a.fixture);
        a.fixture = nullptr;
        if (body->GetType() == b2_dynamicBody) body->SetMassData(&mass);
    }
    a.built = on;
    a.center = center;
    a.size = size;
    a.sensor = c->isTrigger_;
    if (!on) return;
    c->layerSlot_ = c->gameObject_->layer_;
    if (!a.slot) {
        slots_.push_back(0);
        a.slot = &slots_.back();
    }
    *a.slot = c->layerSlot_;
    a.fixture = MakeBox(body, center, size, c->isTrigger_, 0.0f, 0.2f, a.slot); // (the mass set by hand stays)
    fixtures_[a.fixture] = c;
    world.RestartRest(*b.rb->body_);
    if (c->isTrigger_) MadeTrigger(a.fixture);
}

b2Fixture* PhysicsScene::MakeOwnBox(Body& b, bool sensor, float density)
{
    b2Fixture* f = MakeBox(b.rb->body_->Body(), b.box->offset_, b.box->size_, sensor, density, kFriction, b.boxUserData);
    fixtures_[f] = b.box;
    return f;
}

// The body's own box as its collider has it now (game/EnemyRootCollider.cpp, game::EnemyPhysics's
// SyncRootCollider and SyncAttackCollider): off, its shapes destroyed (the mass set by hand kept);
// on again, made anew (a non-trigger box on a Dynamic body with density 1, the mass kept), the
// body's rest restarted; a new offset or size, made again (the rest restarted).
void PhysicsScene::SyncOwnBox(Body& b)
{
    BoxCollider2D* c = b.box;
    if (!c || !b.rb->body_) return;
    const bool on = c->gameObject_->get_activeInHierarchy() && c->enabled_ && !c->destroyed_;
    c->changed_ = false;
    if (b.boxBuilt && on && b.boxSensor != c->isTrigger_) RemakeOwnBox(b);
    if (on == b.boxBuilt && (!on || (SameV(c->offset_, b.boxOffset) && SameV(c->size_, b.boxSize)))) return;
    b2Body* body = b.rb->body_->Body();
    const bool dynamic = body->GetType() == b2_dynamicBody;
    b2MassData mass;
    body->GetMassData(&mass);
    const bool remake = b.boxFixture != nullptr;
    // (A trigger box on a body of its own that's moved by its transform, the KamikazeGolem's
    // AttackCollider, is an attack trigger: game::EnemyPhysics::SyncAttackCollider's carries and
    // rest restarts. A Dynamic body's own box is its root: SyncRootCollider's, without.)
    const bool attackLike = !dynamic;
    if (b.boxFixture) {
        if (attackLike && b.boxFixture->IsSensor()) Carry(body, b.boxFixture, on);
        EraseIf(remade_, [&](const b2Fixture* f) { return f == b.boxFixture; });
        EraseIf(touching_, [&](const Touch& t) { return t.self == b.boxFixture || t.other == b.boxFixture; });
        fixtures_.erase(b.boxFixture);
        body->DestroyFixture(b.boxFixture);
        b.boxFixture = nullptr;
    }
    b.boxBuilt = on;
    b.boxOffset = c->offset_;
    b.boxSize = c->size_;
    b.boxSensor = c->isTrigger_;
    if (on) {
        b.boxFixture = MakeOwnBox(b, c->isTrigger_, !remake && dynamic && !c->isTrigger_ ? 1.0f : 0.0f);
        world.RestartRest(*b.rb->body_);
        if (attackLike && c->isTrigger_) MadeTrigger(b.boxFixture);
    }
    if (dynamic) body->SetMassData(&mass);
}

// isTrigger changed on a body's own box: its shapes made again now, as they were but the flag
// (game::EnemyProjectiles::RemakeFixture: their contacts end; Box2D's e_newFixture has the next
// step pair the moved proxies before it collides), the body's mass kept.
void PhysicsScene::RemakeOwnBox(Body& b)
{
    b.boxSensor = b.box->isTrigger_;
    if (!b.boxFixture || !b.rb->body_) return;
    b2Body* body = b.rb->body_->Body();
    b2MassData mass;
    body->GetMassData(&mass);
    b2Fixture* old = b.boxFixture;
    b2PolygonShape shape = *static_cast<const b2PolygonShape*>(old->GetShape());
    b2FixtureDef fd;
    fd.shape = &shape;
    fd.density = old->GetDensity();
    fd.friction = old->GetFriction();
    fd.restitution = old->GetRestitution();
    fd.filter = old->GetFilterData();
    fd.userData = old->GetUserData();
    fd.isSensor = b.box->isTrigger_;
    EraseIf(remade_, [&](const b2Fixture* f) { return f == old; });
    EraseIf(carried_, [&](const std::pair<const b2Fixture*, const b2Fixture*>& c) { return c.first == old || c.second == old; });
    EraseIf(touching_, [&](const Touch& t) { return t.self == old || t.other == old; });
    fixtures_.erase(old);
    body->DestroyFixture(old);
    b.boxFixture = body->CreateFixture(&fd);
    fixtures_[b.boxFixture] = b.box;
    if (body->GetType() == b2_dynamicBody) body->SetMassData(&mass);
}

void PhysicsScene::Sync()
{
    // The colliders that changed get their new fixtures; the transforms set since the last step
    // go to their bodies (a body below one a script moved goes with it, its rest restarted).
    std::vector<Rigidbody2D*> moved;
    for (size_t i = 0; i < bodies_.size(); i++) {
        Body& b = bodies_[i];
        Rigidbody2D* rb = b.rb;
        if (!rb->body_ || rb->destroyed_ || !rb->gameObject_->get_activeInHierarchy()) continue;
        Rescan(b);
        for (Attached& a : b.attached) SyncAttached(b, a);
        SyncOwnBox(b);
        Transform* t = rb->get_transform();
        const Vector3 p = t->get_position();
        bool carried = false;
        for (Rigidbody2D* m : moved)
            if (Body* mb = Find(m); mb && BelowBody(*mb, rb)) carried = true;
        if (carried) {
            world.SyncWrittenTransform(*rb->body_, Vector2{p.x, p.y});
        } else if (p.x != rb->synced_.x || p.y != rb->synced_.y || t->physicsDirty_) {
            world.SyncTransform(*rb->body_, Vector2{p.x, p.y});
        } else {
            continue;
        }
        rb->synced_ = Vector2{p.x, p.y};
        t->physicsDirty_ = false;
        moved.push_back(rb);
    }
}

void PhysicsScene::WriteBack()
{
    // Each awake Dynamic body's position to its transform (a Kinematic one is where its transform
    // put it); the bodies below it go with it there and then, moved or not, their rest restarted
    // (Unity's transform-change flags go down the hierarchy: enemy_KamikazeGolem.jsonl's
    // AttackCollider's own body, game::EnemyPhysics::WriteBack).
    for (size_t i = 0; i < bodies_.size(); i++) {
        Body& b = bodies_[i];
        Rigidbody2D* rb = b.rb;
        if (!rb->body_ || rb->destroyed_ || !rb->gameObject_->get_activeInHierarchy() || !rb->body_->IsAwake()) continue;
        if (rb->body_->bodyType() != unity::RigidbodyType2D::Dynamic) continue;
        const Vector2 bp = rb->body_->position();
        Transform* t = rb->get_transform();
        const Vector3 cur = t->get_position();
        t->set_position(Vector3{bp.x, bp.y, cur.z});
        t->physicsDirty_ = false; // the physics' own write doesn't count as a script's set
        const Vector3 p = t->get_position();
        rb->synced_ = Vector2{p.x, p.y};
        for (Body& c : bodies_) {
            Rigidbody2D* crb = c.rb;
            if (&c == &b || !crb->body_ || crb->destroyed_ || !crb->gameObject_->get_activeInHierarchy() || !BelowBody(b, crb)) continue;
            Transform* ct = crb->get_transform();
            const Vector3 q = ct->get_position();
            world.SyncWrittenTransform(*crb->body_, Vector2{q.x, q.y});
            crb->synced_ = Vector2{q.x, q.y};
            ct->physicsDirty_ = false;
        }
    }
}

bool PhysicsScene::InColliderAlready(const b2Fixture* self, const b2Fixture* other) const
{
    const unity::Collider2D* collider = world.ColliderOf(other);
    if (!collider) return false;
    auto same = [&](const b2Fixture* f) { return f && f != other && world.ColliderOf(f) == collider; };
    for (const auto& pr : began_)
        if ((pr.first == self && same(pr.second)) || (pr.second == self && same(pr.first))) return true;
    for (const auto& c : carried_)
        if (c.first == self && same(c.second)) return true;
    for (const b2ContactEdge* ce = self->GetBody()->GetContactList(); ce; ce = ce->next) {
        const b2Contact* c = ce->contact;
        if (!c->IsTouching()) continue;
        if (c->GetFixtureA() == self && same(c->GetFixtureB())) return true;
        if (c->GetFixtureB() == self && same(c->GetFixtureA())) return true;
    }
    return false;
}

void PhysicsScene::AfterWorldStep()
{
    // The non-trigger contacts touching after the solve (Unity's collision callbacks: a contact
    // first touching in its trigger pass after it begins in the next step), each side with an
    // object: the new ones enter, the old ones stay, the gone ones exit (game::EnemyProjectiles).
    std::vector<Touch> now;
    for (b2Contact* c = world.World().GetContactList(); c; c = c->GetNext()) {
        if (!c->IsTouching()) continue;
        const b2Fixture* fa = c->GetFixtureA();
        const b2Fixture* fb = c->GetFixtureB();
        if (fa->IsSensor() || fb->IsSensor()) continue;
        for (int side = 0; side < 2; side++) {
            const b2Fixture* self = side == 0 ? fa : fb;
            const b2Fixture* other = side == 0 ? fb : fa;
            if (!ObjectOf(self)) continue;
            Touch t{self, other, c->GetManifold()->pointCount};
            now.push_back(t);
            bool was = false;
            for (const Touch& o : touching_) was = was || (o.self == self && o.other == other);
            (was ? stays_ : enters_).push_back(t);
        }
    }
    for (const Touch& o : touching_) {
        bool still = false;
        for (const Touch& t : now) still = still || (o.self == t.self && o.other == t.other);
        if (!still) exits_.push_back(o);
    }
    touching_ = std::move(now);
}

int PhysicsScene::ContactPoints(Rigidbody2D* rb) const
{
    if (!rb || !rb->body_) return 0;
    int n = 0;
    for (const Touch& t : touching_)
        if (t.self->GetBody() == rb->body_->Body()) n += t.points;
    return n;
}

void PhysicsScene::ReportTriggers()
{
    // OnTriggerEnter2D to both objects' scripts (disabled ones too, as Unity does), in Box2D's order.
    hits_.clear();
    for (const auto& pr : began_) {
        bool carried = false;
        for (const auto& c : carried_) carried = carried || (c.first == pr.first && c.second == pr.second) || (c.first == pr.second && c.second == pr.first);
        if (carried) continue;
        hits_.push_back(Hit{ObjectOf(pr.first), ObjectOf(pr.second), pr.first, pr.second});
    }
    for (const Hit& h : hits_) {
        for (int side = 0; side < 2; side++) {
            Collider2D* self = side == 0 ? h.a : h.b;
            Collider2D* other = side == 0 ? h.b : h.a;
            if (!self || !other || self->destroyed_) continue;
            GameObject* g = self->gameObject_;
            if (!g || g->destroyed_ || !g->get_activeInHierarchy()) continue;
            std::vector<Component*> comps = g->components_;
            for (Component* c : comps)
                if (auto* m = dynamic_cast<MonoBehaviour*>(c)) m->cs_OnTriggerEnter2D(other);
        }
    }
}

void PhysicsScene::ReportCollisions()
{
    // OnCollisionEnter2D, then Stay and Exit, to the scripts of each side's collider's object.
    auto send = [&](const Touch& t, int kind) {
        Collider2D* self = ObjectOf(t.self);
        Collider2D* other = ObjectOf(t.other);
        if (!self || !other || self->destroyed_) return;
        GameObject* g = self->gameObject_;
        if (!g || g->destroyed_ || !g->get_activeInHierarchy()) return;
        auto* col = cs::gcnew<Collision2D>();
        col->collider = other;
        col->otherCollider = self;
        if (kind == 0 && onCollisionEnter) onCollisionEnter(self, col, false);
        std::vector<Component*> comps = g->components_;
        for (Component* c : comps)
            if (auto* m = dynamic_cast<MonoBehaviour*>(c)) {
                if (kind == 0) m->cs_OnCollisionEnter2D(col);
                else if (kind == 1) m->cs_OnCollisionStay2D(col);
                else m->cs_OnCollisionExit2D(col);
            }
        if (kind == 0 && onCollisionEnter) onCollisionEnter(self, col, true);
    };
    std::vector<Touch> enters, stays, exits;
    enters.swap(enters_);
    stays.swap(stays_);
    exits.swap(exits_);
    if (enters.size() > 1 && orderEnters) {
        std::vector<Enter> list;
        for (const Touch& t : enters) list.push_back(Enter{ObjectOf(t.self), ObjectOf(t.other), t.self, t.other});
        orderEnters(list);
        std::vector<Touch> ordered;
        for (const Enter& e : list)
            for (const Touch& t : enters)
                if (t.self == e.selfFixture && t.other == e.otherFixture) ordered.push_back(t);
        if (ordered.size() == enters.size()) enters.swap(ordered);
    }
    for (const Touch& t : enters) send(t, 0);
    for (const Touch& t : stays) send(t, 1);
    for (const Touch& t : exits) send(t, 2);
}

void PhysicsScene::Step(float dt)
{
    Sync();
    began_.clear();
    world.Step(dt);
    remade_.clear(); // (their touches began in its collide)
    AfterWorldStep();
    // Unity's trigger pass after the solve: a trigger paired at the step's end reports in this
    // step (game::EnemyPhysics::Step).
    world.UpdateContacts();
    WriteBack();
    ReportTriggers();
    ReportCollisions();
    carried_.clear();
}

} // namespace bind
