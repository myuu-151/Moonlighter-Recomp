#include "game/Items.h"

namespace game {
namespace Items {

const json::Value* Find(const json::Value& items, const std::string& name)
{
    const json::Value* list = items.Find("items");
    if (!list) return nullptr;
    for (const json::Value& item : list->arr) {
        const json::Value* n = item.Find("name");
        if (n && n->s == name) return &item;
    }
    return nullptr;
}

float StartingAbilityCooldown(const json::Value& items)
{
    const json::Value* save = items.Find("initialSave");
    const json::Value* equipped = save ? save->Find("initialEquippedItems") : nullptr;
    if (!equipped) return -1.0f;
    for (const json::Value& name : equipped->arr) {
        const json::Value* item = Find(items, name.s);
        const json::Value* type = item ? item->Find("$type") : nullptr;
        if (type && type->s == "AbilityItemMaster") return static_cast<float>((*item)["cooldown"].AsDouble());
    }
    return -1.0f;
}

} // namespace Items
} // namespace game
