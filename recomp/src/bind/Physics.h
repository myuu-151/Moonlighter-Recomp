#pragma once
// Unity's 2D physics for translated objects: the Rigidbody2D and Collider2D components on the
// hand-written Unity layer's Box2D world (unity/PhysicsWorld.h), the fixed step as Unity runs it,
// and the trigger and collision messages. The step is the enemy replays' (game::EnemyPhysics and
// game::EnemyProjectiles, checked against the game frame by frame), generalized from the enemies'
// fixed set of boxes to any body and the colliders below it:
//   - a Rigidbody2D's body is made when its object is first active (Unity's Awake: a pooled
//     object under an inactive parent has none until it's taken out), at its transform; it leaves
//     the world while the object is inactive and comes back with it, its shapes made again (Box2D's
//     e_newFixture: PhysicsWorld::FixtureMadeElsewhere);
//   - its object's first BoxCollider2D is the body's own box: on a Dynamic body as
//     PhysicsWorld::AddBody makes it (density 1, the mass set by hand), a trigger one a sensor
//     without mass (the mass at the body's origin), one disabled taken away with the mass kept
//     (game/EnemyRootCollider.cpp); on a Kinematic or Static body (the KamikazeGolem's
//     AttackCollider) a box made only while it's on (game::EnemyPhysics::SyncAttackCollider). Remade
//     when it changes (an Animator drives it: the HermitSlime's trigger root; a script switches it:
//     the FlyingKamikazeGolem's), the body's rest restarted; isTrigger set makes its shapes again at
//     once (game::EnemyProjectiles::RemakeFixture: the turret's shot as it crashes);
//   - the colliders of its children (without a Rigidbody2D of their own) as fixtures in the body's
//     frame (their local position plus offset), remade when they change: the body's rest restarts,
//     what a remade trigger touched doesn't enter again, what it touches in the next step's collide
//     has its own rest restarted, the mass set by hand stays;
//   - each fixed step: the transforms the scripts set since go to their bodies (a set always counts,
//     as Unity's change flags do; the physics' own write-back doesn't; a body below one a script
//     moved is synced with it, its rest restarted), the world's step, the collisions (non-trigger
//     contacts touching after the solve that weren't after the last one: OnCollisionEnter2D; the
//     others Stay and Exit), Unity's trigger pass (UpdateContacts), each awake Dynamic body's
//     position back to its transform (and the bodies below it synced there), then OnTriggerEnter2D
//     to the scripts of both colliders' objects in Box2D's order (one per collider: a
//     PolygonCollider2D's pieces are one), then the collision messages.

#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bind/Scene.h"
#include "unity/Physics2D.h"
#include "unity/PhysicsWorld.h"

class b2Fixture;

namespace UnityEngine {

class Rigidbody2D;

enum class ForceMode2D : int32_t { Force = 0, Impulse = 1 };
enum class RigidbodyType2D : int32_t { Dynamic = 0, Kinematic = 1, Static = 2 };

class Collider2D : public Behaviour {
public:
    const char* cs_typename() const override { return "UnityEngine.Collider2D"; }
    static const char* cs_static_typename() { return "UnityEngine.Collider2D"; }
    Vector2 get_offset() { return offset_; }
    void set_offset(Vector2 o)
    {
        offset_ = o;
        Changed();
    }
    bool get_isTrigger() { return isTrigger_; }
    // Unity makes the collider's shapes again when isTrigger changes (at once).
    void set_isTrigger(bool v);
    Rigidbody2D* get_attachedRigidbody();
    Object* cs_Clone() override;
    void cs_ActiveChanged(bool active) override;
    void cs_Destroyed() override;

    Vector2 offset_;
    bool isTrigger_ = false;
    // the physics' bookkeeping
    int32_t layerSlot_ = 0;   // the fixture's userData (its object's layer)
    bool changed_ = true;     // remake its fixture at the next sync
    void Changed();

protected:
    void cs_EnabledChanged(bool enabled) override;
};

class BoxCollider2D : public Collider2D {
public:
    const char* cs_typename() const override { return "UnityEngine.BoxCollider2D"; }
    static const char* cs_static_typename() { return "UnityEngine.BoxCollider2D"; }
    Vector2 get_size() { return size_; }
    void set_size(Vector2 s)
    {
        size_ = s;
        Changed();
    }
    Object* cs_Clone() override;
    Vector2 size_{1.0f, 1.0f};
};

class CircleCollider2D : public Collider2D {
public:
    const char* cs_typename() const override { return "UnityEngine.CircleCollider2D"; }
    static const char* cs_static_typename() { return "UnityEngine.CircleCollider2D"; }
    float get_radius() { return radius_; }
    void set_radius(float r)
    {
        radius_ = r;
        Changed();
    }
    Object* cs_Clone() override;
    float radius_ = 0.5f;
};

class PolygonCollider2D : public Collider2D {
public:
    const char* cs_typename() const override { return "UnityEngine.PolygonCollider2D"; }
    static const char* cs_static_typename() { return "UnityEngine.PolygonCollider2D"; }
    int32_t get_pathCount() { return static_cast<int32_t>(paths_.size()); }
    cs::Array<Vector2>* GetPath(int32_t index);
    void SetPath(int32_t index, cs::Array<Vector2>* points);
    Object* cs_Clone() override;
    std::vector<std::vector<Vector2>> paths_;
};

class Rigidbody2D : public Component {
public:
    const char* cs_typename() const override { return "UnityEngine.Rigidbody2D"; }
    static const char* cs_static_typename() { return "UnityEngine.Rigidbody2D"; }
    void cs_Attached() override;
    void cs_ActiveChanged(bool active) override;
    void cs_Destroyed() override;
    Object* cs_Clone() override;

    Vector2 get_velocity();
    void set_velocity(Vector2 v);
    void AddForce(Vector2 force) { AddForce(force, ForceMode2D::Force); }
    void AddForce(Vector2 force, ForceMode2D mode);
    float get_mass();
    void set_mass(float m);
    bool get_isKinematic() { return def_.bodyType == unity::RigidbodyType2D::Kinematic; }
    void set_isKinematic(bool k);
    RigidbodyType2D get_bodyType() { return static_cast<RigidbodyType2D>(def_.bodyType); }
    void set_bodyType(RigidbodyType2D t);
    Vector2 get_position();
    bool get_simulated() { return simulated_; }
    void set_simulated(bool s);
    bool IsAwake();
    bool IsSleeping() { return !IsAwake(); }
    void WakeUp();
    void Sleep();
    float get_drag() { return def_.linearDrag; }
    void set_drag(float d);

    unity::Rigidbody2DDef def_;
    unity::Rigidbody2D* body_ = nullptr; // once the physics made it (its object first active)
    bool simulated_ = true;
    Vector2 synced_; // the transform as the body last had it
};

// What an OnCollisionEnter2D gets.
class Collision2D : public cs::Object {
public:
    const char* cs_typename() const override { return "UnityEngine.Collision2D"; }
    bool cs_traced() const override { return true; }
    void cs_trace(cs::Tracer& t) override;
    // The other's: its attached Rigidbody2D's object, else its collider's.
    GameObject* get_gameObject();
    Collider2D* get_collider() { return collider; }
    Collider2D* get_otherCollider() { return otherCollider; }
    Rigidbody2D* get_rigidbody() { return collider ? collider->get_attachedRigidbody() : nullptr; }
    Transform* get_transform();
    Collider2D* collider = nullptr;      // the other's
    Collider2D* otherCollider = nullptr; // this one's
};

// RaycastHit2D (a value): what Physics2D.Raycast met (collider null: nothing).
struct RaycastHit2D {
    Collider2D* collider = nullptr;
    Vector2 point, normal, centroid;
    float distance = 0.0f, fraction = 0.0f;
    // operator bool: a collider was hit
    static bool op_Implicit(const RaycastHit2D& h) { return h.collider != nullptr; }
};
inline void cs_trace_value(cs::Tracer& t, const RaycastHit2D& h) { t.Mark(h.collider); }

// Physics2D's static queries: OverlapBox and Raycast over the scene's room colliders
// (bind::PhysicsScene::queries: their objects by RegisterQuery), OverlapCircleAll over every
// collider of the world (unity/Physics2DOverlap.h: the moving bodies' too).
struct Physics2D {
    static Collider2D* OverlapBox(Vector2 point, Vector2 size, float angle, int32_t layerMask);
    static Collider2D* OverlapBox(Vector2 point, Vector2 size, float angle) { return OverlapBox(point, size, angle, -5); }
    static RaycastHit2D Raycast(Vector2 origin, Vector2 direction, float distance, int32_t layerMask);
    static RaycastHit2D Raycast(Vector2 origin, Vector2 direction, float distance) { return Raycast(origin, direction, distance, -5); }
    static RaycastHit2D Raycast(Vector2 origin, Vector2 direction)
    {
        return Raycast(origin, direction, std::numeric_limits<float>::infinity(), -5);
    }
    static cs::Array<Collider2D*>* OverlapCircleAll(Vector2 point, float radius, int32_t layerMask);
    static cs::Array<Collider2D*>* OverlapCircleAll(Vector2 point, float radius) { return OverlapCircleAll(point, radius, -5); }
};

} // namespace UnityEngine

namespace bind {

class PhysicsScene {
public:
    PhysicsScene(unity::PhysicsWorld& world, Scene& scene);
    ~PhysicsScene();
    PhysicsScene(const PhysicsScene&) = delete;
    PhysicsScene& operator=(const PhysicsScene&) = delete;

    unity::PhysicsWorld& world;
    Scene& scene;
    // Physics2D's room queries see these (the room's colliders, as the enemy replays build them).
    const unity::Physics2D* queries = nullptr;

    // A fixture made outside (the room's, the hero's body) and the collider object that stands
    // for it in trigger and collision messages and OverlapCircleAll.
    void Register(const b2Fixture* fixture, UnityEngine::Collider2D* collider);
    // The facade collider of a room collider (Physics2D's room queries' results).
    void RegisterQuery(const unity::Collider2D* collider, UnityEngine::Collider2D* object);
    UnityEngine::Collider2D* ObjectOf(const unity::Collider2D* collider) const;
    // The collider object of a fixture (nullptr: none registered).
    UnityEngine::Collider2D* ObjectOf(const b2Fixture* fixture) const;

    void Add(UnityEngine::Rigidbody2D* rb);
    void Remove(UnityEngine::Rigidbody2D* rb);
    void SetActive(UnityEngine::Rigidbody2D* rb, bool active);
    void LayerChanged(UnityEngine::GameObject* go);
    // A replay's start: the body somewhere its transform isn't, without the transform counting as moved.
    void PlaceBody(UnityEngine::Rigidbody2D* rb, UnityEngine::Vector2 bodyPosition);
    // The colliders under a body as their components have them now (an Animator just wrote them).
    void SyncColliders(UnityEngine::Rigidbody2D* rb);
    // Collider2D.isTrigger changed: a body's own box is made again now (the others at their sync).
    void TriggerChanged(UnityEngine::Collider2D* c);
    // A replay's start: the fixtures made so far count as made with their bodies (as the game's
    // were, long before), not as remade ones whose touches restart rests in the next step.
    void Settle()
    {
        remade_.clear();
        carried_.clear();
    }

    // One fixed step (Time.fixedDeltaTime), and its parts.
    void Step(float dt);
    void Sync();
    void WriteBack();
    void ReportTriggers();
    void ReportCollisions();

    // The triggers that began touching in the last step: (the trigger's or the toucher's collider, the other).
    struct Hit {
        UnityEngine::Collider2D* a = nullptr;
        UnityEngine::Collider2D* b = nullptr;
        const b2Fixture* fa = nullptr;
        const b2Fixture* fb = nullptr;
    };
    const std::vector<Hit>& LastHits() const { return hits_; }
    // Rigidbody2D.GetContacts on a body between steps: the manifold points of its non-trigger
    // contacts the last world step left touching (none for a trigger's; none for one paired at
    // the step's end and not yet collided). game::EnemyProjectiles::GetContactsCount's.
    int ContactPoints(UnityEngine::Rigidbody2D* rb) const;

    // ---- for drivers' checks (none set: nothing) ----
    // Each Physics2D.Raycast as made: its arguments and what it hit (the room collider; none: null).
    std::function<void(UnityEngine::Vector2 origin, UnityEngine::Vector2 direction, float distance, int32_t layerMask, const unity::RaycastHit2D& hit)>
        onRaycast;
    // Each OverlapCircleAll: what it found, in order.
    std::function<void(UnityEngine::Vector2 point, float radius, const std::vector<UnityEngine::Collider2D*>& found)> onOverlapCircle;
    // Each OnCollisionEnter2D before (after = false) and after the scripts of `self`'s object got it.
    std::function<void(UnityEngine::Collider2D* self, UnityEngine::Collision2D* collision, bool after)> onCollisionEnter;
    // The order of a step's OnCollisionEnter2D calls when several start in it (called with two or
    // more, in the world's contact order, before they're sent). Unity's order follows Box2D's
    // broad-phase pairs, whose proxy ids every collider the session ever made or destroyed sets, so
    // this world's order is Unity's only as far as its history is; a replay of one room may set the
    // recording's here (game::EnemyProjectiles::orderEnters). None: the world's order.
    struct Enter {
        UnityEngine::Collider2D* self = nullptr;
        UnityEngine::Collider2D* other = nullptr;
        const b2Fixture* selfFixture = nullptr;
        const b2Fixture* otherFixture = nullptr;
    };
    std::function<void(std::vector<Enter>& enters)> orderEnters;

private:
    class Listener;
    struct Attached {
        UnityEngine::Collider2D* collider = nullptr;
        b2Fixture* fixture = nullptr;
        bool built = false;
        UnityEngine::Vector2 center, size;
        bool sensor = false;
    };
    struct Body {
        UnityEngine::Rigidbody2D* rb = nullptr;
        UnityEngine::BoxCollider2D* box = nullptr; // the body's own box (its object's first BoxCollider2D)
        b2Fixture* boxFixture = nullptr;           // (none while it's off)
        bool boxBuilt = false, boxSensor = false;
        UnityEngine::Vector2 boxOffset, boxSize;   // as last built
        void* boxUserData = nullptr;               // the body's layer
        std::vector<Attached> attached;            // the children's colliders
        bool everActive = false;
    };
    Body* Find(UnityEngine::Rigidbody2D* rb);
    void SyncAttached(Body& b, Attached& a);
    void SyncOwnBox(Body& b);
    b2Fixture* MakeOwnBox(Body& b, bool sensor, float density);
    void RemakeOwnBox(Body& b);
    void Rescan(Body& b);
    UnityEngine::Vector2 LocalCenter(Body& b, UnityEngine::Collider2D* c);
    void Carry(b2Body* body, b2Fixture* fixture, bool on);
    void MadeTrigger(b2Fixture* fixture);
    bool BelowBody(const Body& b, const UnityEngine::Rigidbody2D* other) const;
    void AfterWorldStep();
    bool InColliderAlready(const b2Fixture* sensor, const b2Fixture* other) const;

    std::vector<Body> bodies_;
    std::unordered_map<const b2Fixture*, UnityEngine::Collider2D*> fixtures_;
    std::vector<std::pair<const unity::Collider2D*, UnityEngine::Collider2D*>> queryObjects_;
    Listener* listener_ = nullptr;
    std::vector<std::pair<const b2Fixture*, const b2Fixture*>> began_;
    std::vector<Hit> hits_;
    // What remade trigger fixtures were touching: no new enter with these in the next step.
    std::vector<std::pair<const b2Fixture*, const b2Fixture*>> carried_;
    // Trigger fixtures made or remade since the last step: what they touch in its collide starts
    // its rest over (game::EnemyPhysics::Listener).
    std::vector<const b2Fixture*> remade_;
    bool adding_ = false; // (Add's fixtures are made, not remade)
    // Non-trigger contacts touching after the last world step (fixture pairs, as (self, other)), and
    // their manifold points; this step's enters, stays and exits.
    struct Touch {
        const b2Fixture* self = nullptr;
        const b2Fixture* other = nullptr;
        int points = 0;
    };
    std::vector<Touch> touching_;
    std::vector<Touch> enters_, stays_, exits_;
};

} // namespace bind
