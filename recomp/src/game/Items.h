#pragma once
// The item masters (data/extracted/items.json, tools/extract/items.py: the Resources "items"
// text ItemDatabase reads, plus the new game's starting equipment from level0's GameManager).

#include <string>

#include "util/Json.h"

namespace game {
namespace Items {

// An item master by name in items.json's "items" (the base game's), or nullptr.
const json::Value* Find(const json::Value& items, const std::string& name);

// A new game's equipped ability (GameManager.ResetSlotInfo: initialEquippedItems' first
// AbilityItemMaster goes to Ability1): its cooldown, -1 if there's none.
float StartingAbilityCooldown(const json::Value& items);

} // namespace Items
} // namespace game
