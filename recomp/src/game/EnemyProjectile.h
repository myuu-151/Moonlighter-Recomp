#pragma once
// The enemies' projectiles (the Broken turret's shot, game/BrokenGolemTurret.h; the Butler's and
// the Gold mimic's are the same machinery): pooled objects (game/PoolManager.h) with a Dynamic
// Rigidbody2D and a BoxCollider2D on their root, an Animator, and a script that a tween moves
// (transform.DOMoveX/Y: the transform, which the physics then follows) and that reacts to
// OnCollisionEnter2D.
//
// EnemyProjectile is the object as the game builds it from its prefab (prefabs.json's root:
// layer EnemyProjectile, tag, box, body; "ProjectileArt/Projectile"'s sprite; the script's
// class picks the C++ class, MakeProjectile): its transform (a local position under its
// parents: the pool's while pooled, its room's once shot), active or not (in the hierarchy),
// its body and box (isTrigger as the script sets it), its Animator and the sprite shown, the
// art's rotation.
//
// EnemyProjectiles is a room's: the floor's PoolManager, the projectile prefabs by name (the
// models the pools make from), the room's transform chain (where shots are parented), and their
// part of each fixed step (an EnemyPhysics::Companion):
//   - Sync: a transform moved since the last write-back (a tween's update, a shot placed) goes
//     to its body (SyncTransform: a teleport that restarts its rest);
//   - after b2World::Step: the projectiles' contacts touching now that weren't after the last
//     step (non-trigger on both sides) are their OnCollisionEnter2D, in the world's contact
//     order (Box2D's BeginContact order within the step's collide: Unity's collision callbacks;
//     Unity's trigger pass after the solve is for triggers only, so a contact first touching
//     there begins in the next step);
//   - WriteBack: an awake body's position to its transform (through its parents);
//   - Report: the OnCollisionEnter2D calls (the hero's transform at his body first).
// The tag and name of what was hit: the hero's body ("Player"), a room collider (its tag and
// object name, PhysicsWorld::ColliderOf), another projectile.
//
// Activation as Unity has it: the body is made the first time the object is active (its
// Awake), leaves the world while the object is inactive (back in the pool) and comes back with
// it; the Animator starts over in its default state each time the object becomes active
// (keepAnimatorControllerStateOnDisable is off), and first updates in the next frame's animation
// pass (one activated during a pass, by an enemy's clip event, isn't in that pass: the driver
// runs UpdateAnimators before the enemies' Animators).

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "game/Enemy.h"
#include "game/PoolManager.h"
#include "game/Prefabs.h"
#include "unity/Animator.h"
#include "unity/PhysicsWorld.h"
#include "unity/Vector2.h"

class b2Fixture;

namespace game {

class EnemyProjectile;
class EnemyProjectiles;

// What OnCollisionEnter2D gets: the other object (Collision2D.gameObject: the other body's
// object, else its collider's).
struct Collision2D {
    const b2Fixture* otherFixture = nullptr;
    std::string tag, name;
    bool hero = false;                    // the hero's body ("Player")
    EnemyProjectile* projectile = nullptr; // another projectile
    const unity::Collider2D* collider = nullptr; // a room collider
    bool CompareTag(const char* t) const { return tag == t; }
};

class EnemyProjectile : public PooledObject {
public:
    EnemyProjectile(EnemyProjectiles& system, const Prefab& prefab, const unity::AnimatorController* controller);
    ~EnemyProjectile() override;
    EnemyProjectile(const EnemyProjectile&) = delete;
    EnemyProjectile& operator=(const EnemyProjectile&) = delete;

    EnemyProjectiles& system;
    EnemyScene& scene() const;
    std::string prefabName;

    // The transform: the local position under `parents` (their local positions, nearest
    // first), the local scale.
    std::vector<unity::Vector2> parents;
    unity::Vector2 localPosition;
    float scale = 1.0f;
    unity::Vector2 position() const;
    // Transform.position's setter (a script's, a tween's): the physics syncs it at the next step.
    void setPosition(unity::Vector2 p);
    // Transform.SetParent(parent) keeping the world position (an active parent: the room's).
    void SetParentActive(const std::vector<unity::Vector2>& newParents);
    bool active = false; // activeInHierarchy (the pool's parent is inactive)

    // The root's BoxCollider2D and Rigidbody2D.
    int layer = 15;
    std::string tag;
    unity::Vector2 boxOffset, boxSize{1.0f, 1.0f};
    bool isTrigger = false;
    unity::Rigidbody2DDef rigidbody;
    unity::Rigidbody2D* body = nullptr; // once first active
    // BoxCollider2D.isTrigger = value (the fixture a sensor or not).
    void SetTrigger(bool value);

    // The Animator, the sprite ProjectileArt/Projectile shows, ProjectileArt's rotation about z
    // (degrees, as Rotate left it: presentation).
    std::unique_ptr<unity::Animator> animator;
    bool animatorFresh = true; // reset on activation, not yet updated
    std::string sprite;
    // The renderer's sprite as the prefab has it: what the Animator writes back as it's switched
    // off with its object (back in the pool), the default value of the property its clips drive.
    std::string defaultSprite;
    float artRotation = 0.0f;
    void UpdateAnimator(float deltaTime);

    // MonoBehaviour.Start (once, before its first frame).
    virtual void Start() {}
    virtual void OnCollisionEnter2D(const Collision2D&) {}
    void OnReturnedToPool() override;

private:
    friend class EnemyProjectiles;
    void SetLocalFromWorld(unity::Vector2 world);
    const unity::AnimatorController* controller_ = nullptr;
    unity::Vector2 synced_;
    bool transformDirty_ = false;
};

class EnemyProjectiles : public EnemyPhysics::Companion {
public:
    explicit EnemyProjectiles(EnemyScene& scene);
    ~EnemyProjectiles() override;
    EnemyProjectiles(const EnemyProjectiles&) = delete;
    EnemyProjectiles& operator=(const EnemyProjectiles&) = delete;

    EnemyScene& scene;
    PoolManager poolManager; // DungeonManager.Instance.poolManager
    // enemyRoom.transform: its local position, then its parents', nearest first.
    std::vector<unity::Vector2> roomParents;

    // A projectile prefab the pools can make (its controller: the Animator's).
    void Register(const Prefab* prefab, const unity::AnimatorController* controller);
    // Instantiate(prefab) as the pool does it; an empty maker if it isn't registered.
    PoolManager::Maker Model(const std::string& prefabName);

    // The world the bodies live in, stepped by `physics` (this joins it as a companion).
    void SetPhysics(unity::PhysicsWorld& world, EnemyPhysics& physics);
    unity::PhysicsWorld* World() const { return world_; }
    void SetHero(const unity::Rigidbody2D* hero) { hero_ = hero; }
    // Before the collisions are reported, the hero's transform (scene.hero->position) is where
    // his body is (Unity wrote it back); off: the driver sets it.
    bool heroFollowsBody = true;
    // Bodies keep their velocity while pooled (Box2D's inactive bodies do).
    bool keepVelocityInPool = true;

    // Every projectile the pools made, in the order made.
    std::vector<EnemyProjectile*> All() const;
    // The Animators of the active ones (call before the enemies' in a frame's animation pass).
    void UpdateAnimators(float deltaTime);
    // The object made active (parented under the room): body, Animator, Start.
    void Activate(EnemyProjectile& p);
    void Deactivate(EnemyProjectile& p);
    // Collider2D.isTrigger changed on its box: Unity makes its shapes again (Box2D: the fixtures
    // destroyed, their contacts ended, and made again, the body's mass kept).
    void RemakeFixture(EnemyProjectile& p);

    // OnCollisionEnter2D as it's called, before (after = false) and after (checks).
    std::function<void(EnemyProjectile&, const Collision2D&, bool after)> onCollisionEnter;
    // The order of a step's OnCollisionEnter2D calls when several start in it (Report calls this
    // with two or more, in the world's contact order, before calling them). Unity's order follows
    // Box2D's broad-phase pairs, whose proxy ids every collider the session ever made or destroyed
    // sets, so this world's contact order is Unity's only as far as its history is; a replay of
    // one room sets the recording's order here (tests/turret_replay.h). None: the world's order.
    std::function<void(std::vector<std::pair<EnemyProjectile*, Collision2D>>& enters)> orderEnters;

    // Rigidbody2D.GetContacts(ContactPoint2D[]) on a projectile's body between steps: how many
    // points it hands back. A ContactPoint2D per manifold point of the contacts the last
    // simulation (b2World::Step) left touching, so none for a trigger's (a sensor has no
    // manifold) and none for a contact paired at the step's end but not yet collided (the
    // shot meeting a LevelBoundary: [] at the start of the step whose OnCollisionEnter2D it
    // is); and none once the shot is a trigger, its shape made again by the isTrigger change
    // (enemy_BrokenGolemTurret.jsonl: [] at all 605 of its rows).
    int GetContactsCount(const EnemyProjectile& p) const;

    // EnemyPhysics::Companion.
    void Sync() override;
    void AfterWorldStep() override;
    void WriteBack() override;
    void Report() override;

private:
    Collision2D Describe(const b2Fixture* other) const;
    unity::PhysicsWorld* world_ = nullptr;
    const unity::Rigidbody2D* hero_ = nullptr;
    std::map<std::string, std::pair<const Prefab*, const unity::AnimatorController*>> prefabs_;
    // (projectile, other fixture) pairs touching after the last step; the enters of this one.
    std::vector<std::pair<const EnemyProjectile*, const b2Fixture*>> touching_;
    std::vector<std::pair<EnemyProjectile*, Collision2D>> enters_;
    // (projectile, other fixture, manifold points) of those, after the last step (GetContactsCount).
    struct PointsRow {
        const EnemyProjectile* p;
        const b2Fixture* other;
        int points;
    };
    std::vector<PointsRow> points_;
};

// The script a projectile prefab carries, as its C++ class (BrokenGolemTurretProjectile); a
// plain EnemyProjectile for one that isn't ported.
std::unique_ptr<EnemyProjectile> MakeProjectile(EnemyProjectiles& system, const Prefab& prefab, const unity::AnimatorController* controller);

} // namespace game
