#include "unity/PhysicsWorld.h"

#include <Box2D/Box2D.h>

#include "util/Json.h"

namespace unity {

namespace {

b2Vec2 B2(Vector2 v) { return b2Vec2(v.x, v.y); }
Vector2 V2(const b2Vec2& v) { return {v.x, v.y}; }

// Unity's default PhysicsMaterial2D (the project sets none).
constexpr float kFriction = 0.4f, kBounciness = 0.0f, kDensity = 1.0f;

// A box from `mn` to `mx`, relative to `origin`.
void MakeBox(b2PolygonShape& shape, Vector2 mn, Vector2 mx, Vector2 origin, bool set)
{
    const Vector2 a{mn.x - origin.x, mn.y - origin.y}, b{mx.x - origin.x, mx.y - origin.y};
    if (set) {
        const b2Vec2 corners[4] = {b2Vec2(a.x, a.y), b2Vec2(b.x, a.y), b2Vec2(b.x, b.y), b2Vec2(a.x, b.y)};
        shape.Set(corners, 4);
    } else {
        const float hx = (b.x - a.x) * 0.5f, hy = (b.y - a.y) * 0.5f;
        shape.SetAsBox(hx, hy, b2Vec2(a.x + hx, a.y + hy), 0.0f);
    }
}

} // namespace

// The layer collision matrix (Physics2D.GetIgnoreLayerCollision), on the fixtures' layers.
class PhysicsWorld::Filter : public b2ContactFilter {
public:
    explicit Filter(const Physics2DSettings& settings) : settings_(settings) {}
    bool ShouldCollide(b2Fixture* a, b2Fixture* b) override
    {
        const int la = *static_cast<const int*>(a->GetUserData()), lb = *static_cast<const int*>(b->GetUserData());
        return settings_.Collide(la, lb) && settings_.Collide(lb, la);
    }

private:
    const Physics2DSettings& settings_;
};

Physics2DSettings::Physics2DSettings()
{
    // TODO: callers load data/extracted/physics2d.json (Load) for all 32 rows; these three
    // are Moonlighter's (globalgamemanagers) for the hero, the rocks and the rolling hero.
    for (uint32_t& row : layerCollisionMatrix) row = 0xffffffffu;
    layerCollisionMatrix[kLayerDefault] = 0xaf92fffdu;
    layerCollisionMatrix[kLayerRisenObstacle] = 0xe7ddddfdu;
    layerCollisionMatrix[kLayerIntangibleHero] = 0xa7020ffdu;
    // Unity's matrix is symmetric: the unknown rows follow the known ones.
    for (int known : {kLayerDefault, kLayerRisenObstacle, kLayerIntangibleHero})
        for (int k = 0; k < 32; k++)
            if (!(layerCollisionMatrix[known] >> k & 1u)) layerCollisionMatrix[k] &= ~(1u << known);
}

bool Physics2DSettings::Load(const json::Value& physics2d)
{
    const json::Value* matrix = physics2d.Find("layerCollisionMatrix");
    if (!matrix || !matrix->IsArray() || matrix->size() != 32) return false;
    for (size_t i = 0; i < 32; i++) layerCollisionMatrix[i] = static_cast<uint32_t>((*matrix)[i].AsInt());
    if (const json::Value* g = physics2d.Find("gravity"))
        gravity = {static_cast<float>((*g)[0].AsDouble()), static_cast<float>((*g)[1].AsDouble())};
    if (const json::Value* v = physics2d.Find("velocityIterations")) velocityIterations = static_cast<int>(v->AsInt());
    if (const json::Value* p = physics2d.Find("positionIterations")) positionIterations = static_cast<int>(p->AsInt());
    return true;
}

RigidbodyType2D Rigidbody2D::bodyType() const
{
    switch (body_->GetType()) {
    case b2_dynamicBody: return RigidbodyType2D::Dynamic;
    case b2_kinematicBody: return RigidbodyType2D::Kinematic;
    default: return RigidbodyType2D::Static;
    }
}

Vector2 Rigidbody2D::position() const { return V2(body_->GetPosition()); }
Vector2 Rigidbody2D::velocity() const { return V2(body_->GetLinearVelocity()); }
void Rigidbody2D::SetVelocity(Vector2 v) { body_->SetLinearVelocity(B2(v)); }
Vector2 Rigidbody2D::worldCenterOfMass() const { return V2(body_->GetWorldCenter()); }
bool Rigidbody2D::IsAwake() const { return body_->IsAwake(); }

int Rigidbody2D::TouchingCount() const
{
    int n = 0;
    for (const b2ContactEdge* e = body_->GetContactList(); e; e = e->next)
        if (e->contact->IsTouching() && !e->contact->GetFixtureA()->IsSensor() && !e->contact->GetFixtureB()->IsSensor()) n++;
    return n;
}

PhysicsWorld::PhysicsWorld(const Physics2DSettings& settings, const Options& options) : settings_(settings), options_(options)
{
    world_ = new b2World(B2(settings_.gravity));
    filter_ = new Filter(settings_);
    world_->SetContactFilter(filter_);
    world_->SetContinuousPhysics(options_.continuousPhysics);
    world_->SetContinuousBulletsOnly(options_.continuousBulletsOnly);
    b2BodyDef ground;
    ground.type = b2_staticBody;
    ground_ = world_->CreateBody(&ground);
}

PhysicsWorld::~PhysicsWorld()
{
    delete world_;
    delete filter_;
}

void PhysicsWorld::CreateFixtures(ColliderEntry& e)
{
    const Collider2D& c = *e.collider;
    const Vector2 origin = e.body == ground_ ? Vector2{} : V2(e.body->GetPosition());
    const size_t boxes = !c.shapes.empty() ? c.shapes.size() : c.parts.empty() ? 1 : c.parts.size();
    for (size_t i = 0; i < boxes; i++) {
        b2PolygonShape shape;
        if (!c.shapes.empty()) {
            b2Vec2 v[b2_maxPolygonVertices];
            const std::vector<Vector2>& poly = c.shapes[i];
            const int n = static_cast<int>(poly.size() < b2_maxPolygonVertices ? poly.size() : b2_maxPolygonVertices);
            for (int k = 0; k < n; k++) v[k] = b2Vec2(poly[static_cast<size_t>(k)].x - origin.x, poly[static_cast<size_t>(k)].y - origin.y);
            shape.Set(v, n);
        } else
            MakeBox(shape, c.parts.empty() ? c.Min() : c.parts[i].first, c.parts.empty() ? c.Max() : c.parts[i].second, origin,
                    options_.polygonSet);
        b2FixtureDef fd;
        fd.shape = &shape;
        fd.density = kDensity;
        fd.friction = kFriction;
        fd.restitution = kBounciness;
        fd.isSensor = c.trigger;
        fd.userData = &e.layer;
        e.fixtures.push_back(e.body->CreateFixture(&fd));
    }
}

void PhysicsWorld::DestroyFixtures(ColliderEntry& e)
{
    for (b2Fixture* f : e.fixtures) e.body->DestroyFixture(f);
    e.fixtures.clear();
}

void PhysicsWorld::AddCollider(const Collider2D& collider, RigidbodyType2D body, bool hasRigidbody)
{
    colliders_.emplace_back();
    ColliderEntry& e = colliders_.back();
    e.collider = &collider;
    e.layer = collider.layer;
    e.enabled = collider.enabled;
    e.trigger = collider.trigger;
    if (body == RigidbodyType2D::Static && !hasRigidbody && options_.groundBody) {
        e.body = ground_;
    } else {
        b2BodyDef bd;
        bd.type = body == RigidbodyType2D::Kinematic ? b2_kinematicBody : body == RigidbodyType2D::Dynamic ? b2_dynamicBody : b2_staticBody;
        bd.position = B2(collider.position);
        e.body = world_->CreateBody(&bd);
    }
    if (e.enabled) CreateFixtures(e);
}

void PhysicsWorld::AddColliders(const Physics2D& physics, int kinematicLayers)
{
    for (const Collider2D& c : physics.Colliders()) {
        const bool kinematic = (kinematicLayers >> c.layer & 1) != 0;
        AddCollider(c, kinematic ? RigidbodyType2D::Kinematic : RigidbodyType2D::Static, kinematic);
    }
}

Rigidbody2D& PhysicsWorld::AddBody(const Rigidbody2DDef& def, Vector2 position, int layer, Vector2 boxOffset, Vector2 boxSize)
{
    bodies_.emplace_back();
    Rigidbody2D& rb = bodies_.back();
    rb.layer_ = layer;
    b2BodyDef bd;
    bd.type = def.bodyType == RigidbodyType2D::Dynamic ? b2_dynamicBody : def.bodyType == RigidbodyType2D::Kinematic ? b2_kinematicBody : b2_staticBody;
    bd.position = B2(position);
    bd.linearDamping = def.linearDrag;
    bd.angularDamping = def.angularDrag;
    bd.gravityScale = def.gravityScale;
    bd.fixedRotation = (def.constraints & Rigidbody2DDef::FreezeRotation) != 0; // (FreezePositionX/Y: TODO)
    bd.bullet = def.collisionDetection == Rigidbody2DDef::Continuous;
    bd.allowSleep = def.sleepingMode != Rigidbody2DDef::NeverSleep;
    bd.awake = def.sleepingMode != Rigidbody2DDef::StartAsleep;
    rb.body_ = world_->CreateBody(&bd);

    b2PolygonShape shape;
    const float hx = boxSize.x / 2.0f, hy = boxSize.y / 2.0f;
    MakeBox(shape, Vector2{boxOffset.x - hx, boxOffset.y - hy}, Vector2{boxOffset.x + hx, boxOffset.y + hy}, Vector2{}, options_.polygonSet);
    b2FixtureDef fd;
    fd.shape = &shape;
    fd.density = kDensity;
    fd.friction = kFriction;
    fd.restitution = kBounciness;
    fd.userData = &rb.layer_;
    rb.body_->CreateFixture(&fd);

    if (def.bodyType == RigidbodyType2D::Dynamic && (!def.useAutoMass || def.hasCenterOfMass)) {
        // The mass set by hand (useAutoMass off): the colliders' centre, the rotational
        // inertia scaled with the mass.
        const float autoMass = rb.body_->GetMass();
        const b2Vec2 lc = rb.body_->GetLocalCenter();
        const float centreInertia = rb.body_->GetInertia() - autoMass * b2Dot(lc, lc);
        b2MassData md;
        md.mass = def.useAutoMass ? autoMass : def.mass;
        md.center = def.hasCenterOfMass ? B2(def.centerOfMass) : lc;
        md.I = (autoMass > 0.0f ? centreInertia * (md.mass / autoMass) : 0.0f) + md.mass * b2Dot(md.center, md.center);
        rb.body_->SetMassData(&md);
    }
    return rb;
}

Rigidbody2D& PhysicsWorld::AddBody(const Rigidbody2DDef& def, Vector2 position, int layer)
{
    bodies_.emplace_back();
    Rigidbody2D& rb = bodies_.back();
    rb.layer_ = layer;
    b2BodyDef bd;
    bd.type = def.bodyType == RigidbodyType2D::Dynamic ? b2_dynamicBody : def.bodyType == RigidbodyType2D::Kinematic ? b2_kinematicBody : b2_staticBody;
    bd.position = B2(position);
    bd.linearDamping = def.linearDrag;
    bd.angularDamping = def.angularDrag;
    bd.gravityScale = def.gravityScale;
    bd.fixedRotation = (def.constraints & Rigidbody2DDef::FreezeRotation) != 0;
    bd.bullet = def.collisionDetection == Rigidbody2DDef::Continuous;
    bd.allowSleep = def.sleepingMode != Rigidbody2DDef::NeverSleep;
    bd.awake = def.sleepingMode != Rigidbody2DDef::StartAsleep;
    rb.body_ = world_->CreateBody(&bd);
    if (def.bodyType == RigidbodyType2D::Dynamic) {
        b2MassData md;
        md.mass = def.mass;
        md.center = def.hasCenterOfMass ? B2(def.centerOfMass) : b2Vec2(0.0f, 0.0f);
        md.I = 0.0f;
        rb.body_->SetMassData(&md);
    }
    return rb;
}

void PhysicsWorld::Refilter(int layer)
{
    for (ColliderEntry& e : colliders_)
        if (e.layer == layer)
            for (b2Fixture* f : e.fixtures) f->Refilter();
    for (Rigidbody2D& rb : bodies_)
        if (rb.layer_ == layer)
            for (b2Fixture* f = rb.body_->GetFixtureList(); f; f = f->GetNext()) f->Refilter();
}

void PhysicsWorld::SetLayer(Rigidbody2D& body, int layer)
{
    if (body.layer_ == layer) return;
    body.layer_ = layer;
    for (b2Fixture* f = body.body_->GetFixtureList(); f; f = f->GetNext()) f->Refilter();
}

void PhysicsWorld::FixtureMadeElsewhere() { newContacts_ = true; }

const Collider2D* PhysicsWorld::ColliderOf(const b2Fixture* fixture) const
{
    for (const ColliderEntry& e : colliders_)
        for (const b2Fixture* f : e.fixtures)
            if (f == fixture) return e.collider;
    return nullptr;
}

void PhysicsWorld::UpdateContacts()
{
    // (b2ContactManager::Collide is public; the world only hands the manager out const.)
    const_cast<b2ContactManager&>(world_->GetContactManager()).Collide();
}

void PhysicsWorld::IgnoreLayerCollision(int a, int b, bool ignore)
{
    const uint32_t before = settings_.layerCollisionMatrix[a];
    if (ignore) {
        settings_.layerCollisionMatrix[a] &= ~(1u << b);
        settings_.layerCollisionMatrix[b] &= ~(1u << a);
    } else {
        settings_.layerCollisionMatrix[a] |= 1u << b;
        settings_.layerCollisionMatrix[b] |= 1u << a;
    }
    if (settings_.layerCollisionMatrix[a] == before) return;
    Refilter(a);
    if (b != a) Refilter(b);
}

void PhysicsWorld::SyncColliders()
{
    for (ColliderEntry& e : colliders_) {
        const Collider2D& c = *e.collider;
        if (c.layer != e.layer) {
            e.layer = c.layer;
            for (b2Fixture* f : e.fixtures) f->Refilter();
        }
        if (c.trigger != e.trigger) {
            e.trigger = c.trigger;
            for (b2Fixture* f : e.fixtures) f->SetSensor(c.trigger);
        }
        if (c.enabled != e.enabled) {
            e.enabled = c.enabled;
            if (e.enabled) CreateFixtures(e);
            else DestroyFixtures(e);
        }
    }
}

void PhysicsWorld::SyncTransform(Rigidbody2D& body, Vector2 position)
{
    b2Body* b = body.body_;
    const b2Vec2 at = b->GetPosition();
    if (at.x == position.x && at.y == position.y) return; // not moved since the write-back
    SyncWrittenTransform(body, position);
}

void PhysicsWorld::SyncWrittenTransform(Rigidbody2D& body, Vector2 position)
{
    b2Body* b = body.body_;
    b->SetTransform(B2(position), b->GetAngle());
    // Unity's sync starts the body's rest over (slime.jsonl: an enemy its PolyNavAgent moves by
    // transform never falls asleep).
    if (options_.wakeOnSync) RestartRest(body);
    // (b2ContactManager::FindNewContacts is public; the world only hands it out const.)
    if (options_.contactsOnSync) const_cast<b2ContactManager&>(world_->GetContactManager()).FindNewContacts();
}

void PhysicsWorld::RestartRest(Rigidbody2D& body)
{
    // Box2D's SetAwake(true) only resets a sleeping body's time at rest, so: asleep, then awake,
    // keeping the velocities.
    b2Body* b = body.body_;
    const b2Vec2 v = b->GetLinearVelocity();
    const float32 w = b->GetAngularVelocity();
    b->SetAwake(false);
    b->SetAwake(true);
    b->SetLinearVelocity(v);
    b->SetAngularVelocity(w);
}

void PhysicsWorld::Step(float dt)
{
    SyncColliders();
    if (newContacts_) {
        // b2World::Step's e_newFixture: the moved proxies are paired before this step's collide.
        const_cast<b2ContactManager&>(world_->GetContactManager()).FindNewContacts();
        newContacts_ = false;
    }
    world_->Step(dt, settings_.velocityIterations, settings_.positionIterations);
}

Vector2 PhysicsWorld::Simulate(Rigidbody2D& body, Vector2 position, float dt)
{
    SyncTransform(body, position);
    Step(dt);
    return body.position();
}

} // namespace unity
