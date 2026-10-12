#include "game/RoomDrops.h"

#include <algorithm>

namespace game {

void RoomDrops::Subscribe(const void* key, Handler handler) { handlers_.push_back(Entry{key, std::move(handler)}); }

void RoomDrops::Unsubscribe(const void* key)
{
    for (size_t k = handlers_.size(); k-- > 0;)
        if (handlers_[k].key == key) {
            handlers_.erase(handlers_.begin() + static_cast<std::ptrdiff_t>(k));
            return;
        }
}

int RoomDrops::Subscriptions(const void* key) const
{
    int n = 0;
    for (const Entry& e : handlers_) n += e.key == key;
    return n;
}

void RoomDrops::Changed()
{
    // `if (OnDropListChanged != null) OnDropListChanged(_droppedItems)`: the invocation list as
    // it is now.
    const std::vector<Entry> now = handlers_;
    for (const Entry& e : now) {
        if (onHandler) onHandler(e.key, false);
        e.handler(droppedItems_);
        if (onHandler) onHandler(e.key, true);
    }
}

void RoomDrops::AddDropItems(const List& items)
{
    droppedItems_.insert(droppedItems_.end(), items.begin(), items.end());
    Changed();
}

void RoomDrops::RemoveDropItem(DroppedItem* item)
{
    if (!Alive(item)) return;
    const auto it = std::find(droppedItems_.begin(), droppedItems_.end(), item);
    if (it == droppedItems_.end()) return;
    droppedItems_.erase(it);
    Changed();
}

void RoomDrops::CleanDropList()
{
    const size_t before = droppedItems_.size();
    droppedItems_.erase(std::remove_if(droppedItems_.begin(), droppedItems_.end(), [](const DroppedItem* x) { return !Alive(x); }),
                        droppedItems_.end());
    if (droppedItems_.size() != before) Changed();
}

void RoomDrops::OnHeroPickedItem(DroppedItem* item)
{
    if (!Alive(item)) return;
    // _droppedItems.FindIndex(x => x == arg1): Unity's ==, a destroyed entry never equal.
    const auto it = std::find(droppedItems_.begin(), droppedItems_.end(), item);
    if (it == droppedItems_.end()) return;
    droppedItems_.erase(it);
    Changed();
}

} // namespace game
