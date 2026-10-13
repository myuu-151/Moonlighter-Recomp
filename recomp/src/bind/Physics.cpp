#include "bind/Physics.h"

#include <algorithm>
#include <cstdio>

#include <Box2D/Box2D.h>

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
void Collider2D::cs_Destroyed() { Changed(); }

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

void Rigidbody2D::cs_Attached()
{
    if (P()) P()->Add(this);
}

void Rigidbody2D::cs_ActiveChanged(bool active)
{
    if (!P()) return;
    P()->SetActive(this, active && simulated_);
    // The colliders below it as they are now (an object made inactive and activated: a prefab's).
    if (active) P()->SyncColliders(this);
}

void Rigidbody2D::cs_Destroyed()
{
    if (P()) P()->SetActive(this, false);
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

void Collision2D::cs_trace(cs::Tracer& t)
{
    t.Mark(collider);
    t.Mark(otherCollider);
}

Collider2D* Physics2D::OverlapBox(Vector2 point, Vector2 size, float angle, int32_t layerMask)
{
    bind::PhysicsScene* p = P();
    if (!p || !p->queries) return nullptr;
    const unity::Collider2D* c = p->queries->OverlapBox(point, size, angle, layerMask);
    return c ? p->ObjectOf(c) : nullptr;
}

} // namespace UnityEngine

namespace bind {

using namespace UnityEngine;

// Box2D's BeginContact: the pairs with a trigger (sensors begin when their shapes start
// overlapping, at a step's collide).
class PhysicsScene::Listener : public b2ContactListener {
public:
    explicit Listener(PhysicsScene& owner) : owner_(owner) {}
    void BeginContact(b2Contact* contact) override
    {
        const b2Fixture* a = contact->GetFixtureA();
        const b2Fixture* b = contact->GetFixtureB();
        if (!a->IsSensor() && !b->IsSensor()) return;
        if (a->GetBody() == b->GetBody()) return;
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

PhysicsScene::Body* PhysicsScene::Find(Rigidbody2D* rb)
{
    for (Body& b : bodies_)
        if (b.rb == rb) return &b;
    return nullptr;
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

void PhysicsScene::Add(Rigidbody2D* rb)
{
    if (Find(rb)) return;
    GameObject* go = rb->gameObject_;
    Body b;
    b.rb = rb;
    b.box = go->GetComponent<BoxCollider2D*>();
    if (!b.box) cs::NotTranslated("Rigidbody2D without a BoxCollider2D on its object (bind/Physics.cpp)");
    const Vector3 pos = go->transform_->get_position();
    rb->body_ = &world.AddBody(rb->def_, Vector2{pos.x, pos.y}, go->layer_, b.box->offset_, b.box->size_);
    fixtures_[rb->body_->Body()->GetFixtureList()] = b.box;
    b.box->changed_ = false;
    bodies_.push_back(b);
    Body& added = bodies_.back();
    Rescan(added);
    for (Attached& a : added.attached) SyncAttached(added, a);
    if (!go->get_activeInHierarchy() || !rb->simulated_) rb->body_->Body()->SetActive(false);
    rb->synced_ = Vector2{pos.x, pos.y};
}

void PhysicsScene::Remove(Rigidbody2D* rb) { SetActive(rb, false); }

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
    }
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
        // Remade while on: what it touched doesn't enter again (nor what an earlier remake since
        // the last step carried).
        for (size_t k = carried_.size(); k-- > 0;)
            if (carried_[k].first == a.fixture) {
                if (on) carried_[k].first = nullptr;
                else carried_.erase(carried_.begin() + static_cast<std::ptrdiff_t>(k));
            }
        if (on)
            for (const b2ContactEdge* ce = body->GetContactList(); ce; ce = ce->next) {
                const b2Contact* ct = ce->contact;
                if (!ct->IsTouching()) continue;
                if (ct->GetFixtureA() == a.fixture) carried_.emplace_back(nullptr, ct->GetFixtureB());
                else if (ct->GetFixtureB() == a.fixture) carried_.emplace_back(nullptr, ct->GetFixtureA());
            }
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
    const float hx = size.x / 2.0f, hy = size.y / 2.0f;
    const float x0 = center.x - hx, x1 = center.x + hx, y0 = center.y - hy, y1 = center.y + hy;
    const b2Vec2 corners[4] = {b2Vec2(x0, y0), b2Vec2(x1, y0), b2Vec2(x1, y1), b2Vec2(x0, y1)};
    b2PolygonShape shape;
    shape.Set(corners, 4);
    b2FixtureDef fd;
    fd.shape = &shape;
    fd.density = 0.0f; // the mass set by hand stays
    fd.isSensor = c->isTrigger_;
    c->layerSlot_ = c->gameObject_->layer_;
    fd.userData = &c->layerSlot_;
    a.fixture = body->CreateFixture(&fd);
    fixtures_[a.fixture] = c;
    world.RestartRest(*b.rb->body_);
    for (auto& cr : carried_)
        if (!cr.first) cr.first = a.fixture;
}

void PhysicsScene::Sync()
{
    // The colliders that changed get their new fixtures; the transforms set since the last
    // step go to their bodies.
    for (Body& b : bodies_) {
        Rigidbody2D* rb = b.rb;
        if (!rb->body_ || rb->destroyed_ || !rb->gameObject_->get_activeInHierarchy()) continue;
        Rescan(b);
        for (Attached& a : b.attached) SyncAttached(b, a);
        Transform* t = rb->get_transform();
        const Vector3 p = t->get_position();
        if (p.x != rb->synced_.x || p.y != rb->synced_.y || t->physicsDirty_) {
            world.SyncTransform(*rb->body_, Vector2{p.x, p.y});
            rb->synced_ = Vector2{p.x, p.y};
            t->physicsDirty_ = false;
        }
    }
}

void PhysicsScene::WriteBack()
{
    for (Body& b : bodies_) {
        Rigidbody2D* rb = b.rb;
        if (!rb->body_ || rb->destroyed_ || !rb->gameObject_->get_activeInHierarchy() || !rb->body_->IsAwake()) continue;
        const Vector2 bp = rb->body_->position();
        Transform* t = rb->get_transform();
        const Vector3 cur = t->get_position();
        t->set_position(Vector3{bp.x, bp.y, cur.z});
        t->physicsDirty_ = false; // the physics' own write doesn't count as a script's set
        const Vector3 p = t->get_position();
        rb->synced_ = Vector2{p.x, p.y};
    }
}

void PhysicsScene::ReportTriggers()
{
    // OnTriggerEnter2D to both objects' scripts (disabled ones too, as Unity does), in Box2D's order.
    hits_.clear();
    for (const auto& pr : began_) {
        bool carried = false;
        for (const auto& c : carried_) carried = carried || (c.first == pr.first && c.second == pr.second) || (c.first == pr.second && c.second == pr.first);
        if (carried) continue;
        auto fa = fixtures_.find(pr.first);
        auto fb = fixtures_.find(pr.second);
        Collider2D* a = fa == fixtures_.end() ? nullptr : fa->second;
        Collider2D* b = fb == fixtures_.end() ? nullptr : fb->second;
        hits_.push_back(Hit{a, b});
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

void PhysicsScene::Step(float dt)
{
    Sync();
    began_.clear();
    world.Step(dt);
    // Unity's trigger pass after the solve: a trigger paired at the step's end reports in this
    // step (game::EnemyPhysics::Step).
    world.UpdateContacts();
    WriteBack();
    ReportTriggers();
    carried_.clear();
}

} // namespace bind
