#pragma once
// FlyingRepairGolemBehaviour.cs (and SoundFlyingRepairPick.cs on the same object): the Golem
// dungeon's flying repair golem, an EnemyBehaviour (game/Enemy.h) on the FlyingEnemy layer that
// moves by setting its Rigidbody2D's velocity and steals the items dropped in its room.
//
// While it targets the hero (StartTargetingPlayerAfterTime also subscribes it to the room's
// OnDropListChanged: game/RoomDrops.h; CancelPlayerTargetingAfterTime and OnDeath unsubscribe),
// every frame, if it can move (ChooseAndApplyAction), MoveEnemy:
//   - carrying nothing:
//       - a target item that isn't null and is active and enabled: ChaseItem, a
//         transform.Translate of moveDirection.normalized * speed * deltaTime towards it
//         (moveDirection = the item's position - its own, as Vector2s);
//       - else ChasePlayer: if it was hit since and its body stands still (velocity.magnitude
//         == 0), a Random.insideUnitCircle thrown away and the velocity set to -(hero - it)
//         (wasHit off); then moveDirection = hero - it (Vector3s) and the velocity
//         (velocity.normalized * 0.95 + 0.05 * moveDirection.normalized).normalized * speed * 1.6;
//   - carrying something: MoveRandomly, the velocity (velocity.normalized * 0.85 + 0.15 *
//     moveDirection.normalized).normalized * speed; then while patrolLapse is above 0 it runs
//     down by deltaTime, else patrolLapse = Random.Range(0.5, 1) and ChooseRandomPoint:
//     Random.insideUnitCircle, then Random.Range(40, 90) (an int), the point transform.position +
//     (circle.x, circle.y, position.z) * that, until PolyNav2D.current.PointIsValid takes it or 6
//     more tries (7 at most, 3 draws each); moveDirection = point - position.
// Each Vector2/Vector3 operator's result is rounded as the game's Mono stores it (the
// magnitudes' sums in double: unity/Mathf.h).
//
// The room's OnDropListChanged (OnItemDroppedInRoom): alive, it takes a random item of the list
// as its target (Extensions.GetRandom: Random.Range(0, Count), none if it's empty). Its root's
// OnTriggerEnter2D with the target's object (tagged "Item") sets the Animator's "PickItem".
//
// Its Animator (animators/FlyingRepairGolem.json): HelixFly (the loop, 0.643 s: its curve turns
// the AttackCollider child on for the first 1/14 s of each cycle, then off: every cycle it can
// hit the hero anew) and PickingItem (0.5 s, "PickItem" from HelixFly, back at its end), whose
// events are PlayFlyingRepairPick (SoundFlyingRepairPick: MasterAudio.PlaySound
// "golem_dungeon_flyingrepair_pick"), ForbidEnemyMovement, an event with no function (Unity
// calls nothing), PickItemCollisionedAndRemoveItFromRoom (the target stolen: carried, its object
// off and under the golem, the room's RemoveDropItem) and AllowEnemyMovement. PickingItem
// animates nothing of the AttackCollider: the write defaults put it back on (the prefab's).
//
// A hit (EnemyWasHit) sets wasHit; one that takes health (HealthDecreasedWhenHit) also stops
// its body (velocity zero). Its death: the base's, off OnDropListChanged, DropAllStolenItems
// (each carried item back on and ItemStack.Drop'ped from where it is, 30 around: not back in
// the room's list) and HeroStatistics.RegisterFlyingRepairGolemDeath (registerDeath).
//
// Its OnCollisionEnter2D compares collision.gameObject.layer with 1 << LevelBoundary (2048,
// not a layer): it never does anything (OnCollisionEnter2D below, which nothing calls).

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "game/Enemy.h"
#include "game/Prefabs.h"
#include "game/RoomDrops.h"
#include "unity/Vector2.h"

namespace game {

// A UnityEngine.Vector3 of the golem's sums (z is the transforms').
struct GolemVector3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

class FlyingRepairGolemBehaviour : public EnemyBehaviour {
public:
    explicit FlyingRepairGolemBehaviour(Enemy& enemy) : EnemyBehaviour(enemy) {}
    ~FlyingRepairGolemBehaviour() override;

    // Serialized: itemSprite (the "FlyingGolemArt/Item" SpriteRenderer): set or not, and the
    // sprite it shows now (the stolen item's; "": null).
    bool hasItemSprite = true;
    std::string itemSprite;

    // Private in C# (public here for replays and checks).
    DroppedItem* targetItem = nullptr;
    std::vector<DroppedItem*> stolenItems;
    float patrolLapse = -1.0f;
    float movemmentForce = 100.0f; // (unused)
    unity::Vector2 myPos;
    GolemVector3 moveDirection;
    RoomDrops::List* itemsDroppedInRoom = nullptr;
    bool wasHit = false;
    float velocityPercent = 0.95f;
    // The z of its transform and of the hero's (the Vector3s' third component; the port's
    // transforms are 2D): the driver's to set where they aren't 0.
    float transformZ = 0.0f, heroZ = 0.0f;

    // HeroStatistics.RegisterFlyingRepairGolemDeath.
    std::function<void()> registerDeath;
    // Called as Update begins (a replay puts the target item where the game had it then).
    std::function<void(FlyingRepairGolemBehaviour&)> beforeUpdate;
    // What the port did, for checks (not the game's): the last ChooseRandomPoint's tries and
    // points (in order), the handler calls.
    std::vector<GolemVector3> lastPoints;
    int dropListCalls = 0;

    void Init() override;
    void Update(float deltaTime) override;
    void StartTargetingPlayerAfterTime(float time) override;
    void CancelPlayerTargetingAfterTime() override;
    void ChooseAndApplyAction(float deltaTime) override;
    void MoveEnemy(float deltaTime) override;
    void EnemyWasHit(float newHealthValue) override;
    void HealthDecreasedWhenHit(float newValue) override;
    // OnItemDroppedInRoom(objectsInRoom): the room's OnDropListChanged.
    void OnItemDroppedInRoom(RoomDrops::List& objectsInRoom);
    // OnTriggerEnter2D(collision): what entered its root's (or its AttackCollider's) collider:
    // its object's tag, and the item it is (none: not an item).
    void OnTriggerEnter2D(const std::string& otherTag, const DroppedItem* otherItem);
    // OnCollisionEnter2D(collision): the other object's layer, the first contact's normal (none:
    // no contacts). Dead code in the game (see above).
    void OnCollisionEnter2D(int otherLayer, bool hasContact, unity::Vector2 normal);
    void PickItemCollisionedAndRemoveItFromRoom();
    void DropAllStolenItems();
    const std::vector<DroppedItem*>& GetStolenItems() const { return stolenItems; }
    // SoundFlyingRepairPick.PlayFlyingRepairPick.
    void PlayFlyingRepairPick();
    bool OnAnimationEvent(const unity::AnimationEvent& e) override;

protected:
    void OnDeath() override;

private:
    void MoveRandomly();
    void ChooseRandomPoint();
    bool CheckIfDestinationPointIsReachable(const GolemVector3& point) const;
    void ChasePlayer();
    void ChaseItem(float deltaTime);
    RoomDrops* Room() const { return enemy.hasEnemyRoom ? enemy.scene.drops : nullptr; }
    GolemVector3 Position3() const;
    GolemVector3 Hero3() const;
};

// The behaviour of a prefab whose root has one (MakeEnemy's), with its serialized fields
// (prefabs.json's FlyingRepairGolemBehaviour row).
std::unique_ptr<FlyingRepairGolemBehaviour> MakeFlyingRepairGolemBehaviour(Enemy& enemy, const Behaviour& fields);

} // namespace game
