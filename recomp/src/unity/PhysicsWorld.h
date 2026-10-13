#pragma once
// Unity's 2D physics world: Box2D 2.3.1 (third_party/box2d) with the project's
// Physics2DSettings, for what the scripts' own casts don't do: a Dynamic Rigidbody2D's contacts
// (the hero's, with the Kinematic rocks and the walls, which push him out through Box2D's
// position correction).
//
// Each fixed step (Time.fixedDeltaTime, 0.01) Unity syncs the transforms that changed to their
// bodies (autoSyncTransforms is off: b2Body::SetTransform, which places the body's centre of
// mass at transform + local centre), steps the world (velocity iterations 24, position 16) and
// writes each awake body's origin back to its transform (centre of mass - local centre). With
// no contacts that round trip is (p + c) - c; contacts move the centre.
//
// Colliders come from a unity::Physics2D (which must outlive the world): a box, or a compound
// collider's boxes as fixtures of one body. The layer collision matrix filters contacts
// (Physics2D.GetIgnoreLayerCollision); triggers are sensors.
//
// Open questions about Unity's fork, as switches (Options) with stock Box2D 2.3.1 as default
// where it has one: whether a synced transform wakes its body; whether Unity finds new
// contacts right after syncing (stock: at the end of the step's Solve, so the first step that
// starts overlapping corrects nothing); colliders without a Rigidbody2D on one static ground
// body at the origin (world-space vertices) or each on its own; boxes built with
// b2PolygonShape::Set (hull order) or SetAsBox.

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "unity/Physics2D.h"
#include "unity/Vector2.h"

class b2World;
class b2Body;
class b2Fixture;

namespace json {
struct Value;
}

namespace unity {

enum class RigidbodyType2D { Dynamic = 0, Kinematic = 1, Static = 2 };

// A Rigidbody2D's serialized settings (prefabs.json's, Unity's enums as ints).
struct Rigidbody2DDef {
    enum Constraints { FreezePositionX = 1, FreezePositionY = 2, FreezeRotation = 4 };
    enum Detection { Discrete = 0, Continuous = 1 };
    enum Sleeping { NeverSleep = 0, StartAwake = 1, StartAsleep = 2 };
    RigidbodyType2D bodyType = RigidbodyType2D::Dynamic;
    float mass = 1.0f, linearDrag = 0.0f, angularDrag = 0.05f, gravityScale = 1.0f;
    int constraints = 0, collisionDetection = Discrete, sleepingMode = StartAwake;
    bool useAutoMass = false;
    // Rigidbody2D.centerOfMass when it's known; else the colliders' (Box2D's).
    bool hasCenterOfMass = false;
    Vector2 centerOfMass;
};

// The project's Physics2DSettings (globalgamemanagers). The tuning values Box2D keeps as
// constants (b2Settings.h: slop, Baumgarte, corrections, sleep tolerances) are compiled in;
// these are what the world takes at run time.
struct Physics2DSettings {
    Vector2 gravity{0.0f, -9.81f};
    int velocityIterations = 24, positionIterations = 16;
    // Row a, bit b: layers a and b collide. Moonlighter's rows for Default, RisenObstacle and
    // IntangibleHero; the others collide with everything until Load reads them.
    uint32_t layerCollisionMatrix[32];

    Physics2DSettings();
    // From data/extracted/physics2d.json (tools/extract): false if it isn't one.
    bool Load(const json::Value& physics2d);
    bool Collide(int a, int b) const { return (layerCollisionMatrix[a] >> b & 1u) != 0; }
};

class PhysicsWorld;

// A body: the hero's (Dynamic), or a Kinematic/Static one a collider was added with.
class Rigidbody2D {
public:
    RigidbodyType2D bodyType() const;
    Vector2 position() const; // the body's origin (Rigidbody2D.position): its transform's
    Vector2 velocity() const;
    void SetVelocity(Vector2 v);
    Vector2 worldCenterOfMass() const;
    bool IsAwake() const;
    int layer() const { return layer_; }
    // Contacts touching another non-trigger collider now (manifolds with points).
    int TouchingCount() const;
    b2Body* Body() const { return body_; }

private:
    friend class PhysicsWorld;
    b2Body* body_ = nullptr;
    int layer_ = 0; // the fixtures' (their userData points here)
};

class PhysicsWorld {
public:
    struct Options {
        bool wakeOnSync = true;         // a transform that moved wakes its body and restarts its time at rest (else a sleeping one stays put)
        bool contactsOnSync = false;    // find the moved bodies' new contacts right after the sync (stock: end of Solve)
        bool groundBody = true;         // colliders without a Rigidbody2D: one static body at the origin, world vertices
        bool polygonSet = true;         // boxes through b2PolygonShape::Set (else SetAsBox)
        bool continuousPhysics = true;  // b2World's TOI pass
        // Only bodies with continuous detection (Rigidbody2D.collisionDetectionMode Continuous: Box2D's
        // bullets) take the TOI pass; a Discrete one isn't swept even against static colliders
        // (Unity: Discrete "only detects collisions at the new position"). tangle.jsonl, frames
        // 205-492: Tangle 5 pushed into a hole's inside corner ends where the island's position
        // iterations left it; stock Box2D's TOI pushes it on to the slop (y 342.985016 against the
        // game's 342.989410 at frame 205), and the replay differs from there.
        bool continuousBulletsOnly = true;
    };

    // (Two constructors, not a defaulted Options argument: GCC wants Options' member
    // initializers parsed before a default argument inside this class uses them.)
    PhysicsWorld() : PhysicsWorld(Physics2DSettings(), Options{}) {}
    explicit PhysicsWorld(const Physics2DSettings& settings) : PhysicsWorld(settings, Options{}) {}
    PhysicsWorld(const Physics2DSettings& settings, const Options& options);
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // A room collider. `body` Static: no Rigidbody2D (on the ground body, or a static body at
    // its object's position without groundBody); Kinematic/Static: its own body there.
    void AddCollider(const Collider2D& collider, RigidbodyType2D body = RigidbodyType2D::Static, bool hasRigidbody = false);
    // Every collider of `physics`, in its order (Unity's creation order sets the broad-phase
    // proxies' ids, which order the contacts). `kinematicLayers`: layers whose colliders
    // have a Kinematic Rigidbody2D of their own (the rocks', 1 << 8).
    void AddColliders(const Physics2D& physics, int kinematicLayers = 0);
    // A body with one BoxCollider2D (local offset and size, density 1), its transform at
    // `position` on `layer`.
    Rigidbody2D& AddBody(const Rigidbody2DDef& def, Vector2 position, int layer, Vector2 boxOffset, Vector2 boxSize);

    // gameObject.layer = layer (Unity refilters its fixtures).
    void SetLayer(Rigidbody2D& body, int layer);
    // A collider was made in Unity's world that this one doesn't hold (the hero's weapon
    // hitbox): Box2D's e_newFixture, so the next step pairs every moved proxy before it
    // collides (b2World::Step), not after it solves.
    void FixtureMadeElsewhere();
    // b2ContactManager::Collide outside a step: contacts made at the step's end are tested now
    // (BeginContact for the ones touching). Unity's triggers (its own pass after the solve)
    // start in the step whose end pairs them; EnemyPhysics runs this after each step for that.
    void UpdateContacts();
    // Physics2D.IgnoreLayerCollision (both rows), refiltering those layers' fixtures.
    void IgnoreLayerCollision(int a, int b, bool ignore = true);
    bool GetIgnoreLayerCollision(int a, int b) const { return !settings_.Collide(a, b); }

    // The colliders' enabled, trigger and layer as their Collider2D has them now.
    void SyncColliders();
    // Physics2D.SyncTransforms for a body whose transform is at `position`.
    void SyncTransform(Rigidbody2D& body, Vector2 position);
    // The body awake with its time at rest (towards sleeping) back to zero, even if it was
    // awake already (what Unity does when it syncs a moved transform, or remakes a collider of
    // the body's: enemy_GolemSoldier.jsonl), its velocities kept.
    void RestartRest(Rigidbody2D& body);
    // b2World::Step at the project's iterations (after SyncColliders).
    void Step(float dt);
    // One fixed step of Unity's for `body` whose transform is at `position`: sync it, step,
    // and the transform it writes back.
    Vector2 Simulate(Rigidbody2D& body, Vector2 position, float dt);

    // The room collider a fixture was made for (AddCollider's), nullptr if it isn't one (a body
    // AddBody made, or another's): what a contact touched (its tag, as Collision2D.gameObject).
    const Collider2D* ColliderOf(const b2Fixture* fixture) const;

    const Physics2DSettings& Settings() const { return settings_; }
    const Options& GetOptions() const { return options_; }
    b2World& World() { return *world_; }

private:
    class Filter;
    struct ColliderEntry {
        const Collider2D* collider = nullptr;
        b2Body* body = nullptr;
        std::vector<b2Fixture*> fixtures;
        int layer = 0;
        bool enabled = false, trigger = false;
    };
    void CreateFixtures(ColliderEntry& e);
    void DestroyFixtures(ColliderEntry& e);
    void Refilter(int layer);

    Physics2DSettings settings_;
    Options options_;
    b2World* world_ = nullptr;
    Filter* filter_ = nullptr;
    bool newContacts_ = false; // FixtureMadeElsewhere: pair the moved proxies before the next step
    b2Body* ground_ = nullptr;
    std::deque<ColliderEntry> colliders_; // stable addresses (fixture userData)
    std::deque<Rigidbody2D> bodies_;
};

// Moonlighter's layers the physics uses.
constexpr int kLayerDefault = 0, kLayerRisenObstacle = 8, kLayerLevelBoundary = 11, kLayerSunkenObstacle = 14, kLayerIntangibleHero = 24;

} // namespace unity
