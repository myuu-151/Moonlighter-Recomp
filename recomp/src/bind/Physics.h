#pragma once
// Unity's 2D physics for translated objects: the Rigidbody2D and Collider2D components on the
// hand-written Unity layer's Box2D world (unity/PhysicsWorld.h), the fixed step as Unity runs it,
// and the trigger messages. The step is the enemy replays' (game::EnemyPhysics, checked against
// the game frame by frame), generalized from the enemies' fixed set of boxes to any body and
// the colliders below it:
//   - a Rigidbody2D's body at its transform, its object's first BoxCollider2D as the body's box
//     (density 1, the mass set by hand), the colliders of its children (without a Rigidbody2D
//     of their own) as fixtures in the body's frame (their local position plus offset), remade
//     when they change (an Animator writes them): the body's rest restarts, what a remade
//     trigger touched doesn't enter again, the mass set by hand stays;
//   - each fixed step: the transforms the scripts set since go to their bodies (a set always
//     counts, as Unity's change flags do; the physics' own write-back doesn't), the world's step,
//     Unity's trigger pass (UpdateContacts), each awake body's position back to its transform,
//     then OnTriggerEnter2D to the scripts of both colliders' objects, in Box2D's order.

#include <cstdint>
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
    void set_isTrigger(bool v)
    {
        isTrigger_ = v;
        Changed();
    }
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
    unity::Rigidbody2D* body_ = nullptr; // once the physics made it
    bool simulated_ = true;
    Vector2 synced_; // the transform as the body last had it
};

// What an OnCollisionEnter2D gets.
class Collision2D : public cs::Object {
public:
    const char* cs_typename() const override { return "UnityEngine.Collision2D"; }
    bool cs_traced() const override { return true; }
    void cs_trace(cs::Tracer& t) override;
    GameObject* get_gameObject() { return collider ? collider->gameObject_ : nullptr; }
    Collider2D* get_collider() { return collider; }
    Collider2D* get_otherCollider() { return otherCollider; }
    Rigidbody2D* get_rigidbody() { return collider ? collider->get_attachedRigidbody() : nullptr; }
    Transform* get_transform() { return collider ? collider->get_transform() : nullptr; }
    Collider2D* collider = nullptr;      // the other's
    Collider2D* otherCollider = nullptr; // this one's
};

// Physics2D's static queries, over the scene's room colliders (bind::PhysicsScene::queries).
struct Physics2D {
    static Collider2D* OverlapBox(Vector2 point, Vector2 size, float angle, int32_t layerMask);
    static Collider2D* OverlapBox(Vector2 point, Vector2 size, float angle) { return OverlapBox(point, size, angle, -5); }
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
    // Physics2D's queries see these (the room's colliders, as the enemy replays build them).
    const unity::Physics2D* queries = nullptr;

    // A fixture made outside (the room's, the hero's body) and the collider object that stands
    // for it in trigger messages.
    void Register(const b2Fixture* fixture, UnityEngine::Collider2D* collider);
    // The facade collider of a room collider (Physics2D's results).
    void RegisterQuery(const unity::Collider2D* collider, UnityEngine::Collider2D* object);
    UnityEngine::Collider2D* ObjectOf(const unity::Collider2D* collider) const;

    void Add(UnityEngine::Rigidbody2D* rb);
    void Remove(UnityEngine::Rigidbody2D* rb);
    void SetActive(UnityEngine::Rigidbody2D* rb, bool active);
    void LayerChanged(UnityEngine::GameObject* go);
    // A replay's start: the body somewhere its transform isn't, without the transform counting as moved.
    void PlaceBody(UnityEngine::Rigidbody2D* rb, UnityEngine::Vector2 bodyPosition);
    // The colliders under a body as their components have them now (an Animator just wrote them).
    void SyncColliders(UnityEngine::Rigidbody2D* rb);

    // One fixed step (Time.fixedDeltaTime), and its parts.
    void Step(float dt);
    void Sync();
    void WriteBack();
    void ReportTriggers();

    // The triggers that began touching in the last step: (the trigger's or the toucher's collider, the other).
    struct Hit {
        UnityEngine::Collider2D* a = nullptr;
        UnityEngine::Collider2D* b = nullptr;
    };
    const std::vector<Hit>& LastHits() const { return hits_; }

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
        UnityEngine::BoxCollider2D* box = nullptr; // the body's own box (AddBody's fixture)
        std::vector<Attached> attached;            // the children's colliders
    };
    Body* Find(UnityEngine::Rigidbody2D* rb);
    void SyncAttached(Body& b, Attached& a);
    void Rescan(Body& b);
    UnityEngine::Vector2 LocalCenter(Body& b, UnityEngine::Collider2D* c);

    std::vector<Body> bodies_;
    std::unordered_map<const b2Fixture*, UnityEngine::Collider2D*> fixtures_;
    std::vector<std::pair<const unity::Collider2D*, UnityEngine::Collider2D*>> queryObjects_;
    Listener* listener_ = nullptr;
    std::vector<std::pair<const b2Fixture*, const b2Fixture*>> began_;
    std::vector<Hit> hits_;
    // What remade trigger fixtures were touching: no new enter with these in the next step.
    std::vector<std::pair<const b2Fixture*, const b2Fixture*>> carried_;
};

} // namespace bind
