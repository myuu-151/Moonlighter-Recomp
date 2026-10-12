#pragma once
// DungeonRoom's dropped items (DungeonRoom.cs: _droppedItems, OnDropListChanged, AddDropItems,
// RemoveDropItem, CleanDropList, OnHeroPickedItem) and the ItemStacks in it, as much of them as
// the enemies that go after them see (FlyingRepairGolemBehaviour: game/FlyingRepairGolem.h).
//
// Items on the floor aren't ported yet (ItemStack.Drop's tweens, the hero's pick-up): a
// DroppedItem is what the port (or a replay) feeds in: an id, where its transform is, whether
// it's active and enabled, whether it's been destroyed, the sprite it shows. The list is the
// room's, in its order; OnDropListChanged is a C# multicast delegate: its handlers in the order
// they were added (the same one can be added twice: it's then called twice), -= takes the last
// one added away, and a change calls the handlers the delegate had when it was invoked (one that
// unsubscribes another mid-call doesn't stop it), each with the room's list itself (not a copy).
//
// An ItemStack that's been destroyed is Unity's null: `item != null` is false for it, the list
// keeps it until CleanDropList (a coroutine: RemoveAll(x => x == null) every frame, the handlers
// called if it removed any).

#include <functional>
#include <string>
#include <vector>

#include "unity/Vector2.h"

namespace game {

// An ItemStack (a MonoBehaviour on its own object, tagged "Item").
struct DroppedItem {
    int id = -1;             // the caller's (a replay's: the oracle's)
    std::string item;        // its master's name ("Iron Bar"), for checks
    std::string sprite;      // itemImage.sprite's name (what a golem carrying it shows)
    unity::Vector2 position; // transform.position
    bool active = true;      // gameObject.activeInHierarchy
    bool enabled = true;     // the ItemStack's enabled flag
    bool destroyed = false;  // Object.Destroy has run: Unity's null
    const void* parent = nullptr; // what transform.parent was set to (a golem's Enemy); none: the room
    std::string tag = "Item";

    bool isActiveAndEnabled() const { return !destroyed && active && enabled; }
};

// Unity's `item != null` for an ItemStack reference.
inline bool Alive(const DroppedItem* item) { return item != nullptr && !item->destroyed; }

class RoomDrops {
public:
    using List = std::vector<DroppedItem*>;
    using Handler = std::function<void(List& objectsInRoom)>;

    // OnDropListChanged += handler (`key`: the delegate's target, for -=).
    void Subscribe(const void* key, Handler handler);
    // OnDropListChanged -= handler: the last one added with this key goes (none: nothing).
    void Unsubscribe(const void* key);
    int Subscriptions(const void* key) const;
    int SubscriptionCount() const { return static_cast<int>(handlers_.size()); }

    // GetDropList(): the list itself.
    List& GetDropList() { return droppedItems_; }
    // AddDropItems(items): appended, then OnDropListChanged.
    void AddDropItems(const List& items);
    // RemoveDropItem(item): if it's not null and List.Remove takes it out (its first
    // occurrence), OnDropListChanged.
    void RemoveDropItem(DroppedItem* item);
    // CleanDropList's pass (each frame): the destroyed ones out; if any were, OnDropListChanged.
    void CleanDropList();
    // OnHeroPickedItem(item, n): if it's not null and in the list, its first occurrence out and
    // OnDropListChanged.
    void OnHeroPickedItem(DroppedItem* item);

    // ItemStack.Drop(position, radius, parent): not ported (its Random.Range(0, 360) and its
    // tweens are the caller's); `inRoom`: the room's transform its parent (else none: the item
    // canvas). FlyingRepairGolemBehaviour.DropAllStolenItems calls it.
    std::function<void(DroppedItem& item, unity::Vector2 position, float radius, bool inRoom)> drop;
    // Called before each handler runs and after (for checks: a replay's Random resync).
    std::function<void(const void* key, bool after)> onHandler;

private:
    void Changed();
    struct Entry {
        const void* key;
        Handler handler;
    };
    List droppedItems_;
    std::vector<Entry> handlers_;
};

} // namespace game
