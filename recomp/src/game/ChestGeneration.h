#pragma once
// ChestGeneration.GenerateChestContent (Moonlighter.DungeonGeneration/ChestGeneration.cs):
// random chest contents. Item lists, effect tables and amulet state are data (built by the
// game at runtime: RefreshWeightLists, ItemEffectManager, AmuletUtility); this file is the
// RNG-exact procedure over them:
//   repeat: weighted tier pick -> weighted item pick among items not yet in the chest
//           (owned amulets retry without counting) -> trick effect (run-bound items) or a
//           random effect (weighted evilness level, uniform effect) and its OnCreate draws ->
//           quantity roll
//   then: PURPLE chests' amulet pick, and the rejection-sampling shuffle.

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace game {

struct ChestItem {
    std::string name;
    int id = 0;
    int maxStack = 1, fixedChestStack = 0, minChestStack = 0, maxChestStack = 0;
    bool destroyedOnRunEnded = false;
    int culture = 0;
    bool amulet = false;
};

// WeightedList<T>: items in their stored (sorted) order, and the stored _maxWeight.
template <typename T>
struct Weighted {
    float maxWeight = 0.0f;
    std::vector<std::pair<T, float>> items;
};

struct ChestEffect {
    std::string name;
    std::string action;
    int randomDirections = 0; // InventorySlotAction._maxRandomDirections
    int fixedStack = 0;       // InventorySlotAction.fixedStack
    bool slotAction = false;  // action is an InventorySlotAction (or subclass)
    bool selectRandom = false; // PreferedPositionAction.selectRandom
};

struct ChestTables {
    // _weightItemLists[chestType][culture]: tiers -> items.
    std::map<std::pair<int, int>, Weighted<Weighted<const ChestItem*>>> lists;
    // ItemEffectManager._prefabsByChestAndEvilness[chestType]: levels -> effects (uniform).
    std::vector<Weighted<std::vector<const ChestEffect*>>> effects;
    ChestEffect trick;
    std::vector<ChestItem> itemStore;     // owns ChestItem objects
    std::vector<ChestEffect> effectStore; // owns ChestEffect objects
};

struct AmuletState {
    struct Amulet {
        std::string name;
        float weight = 0.0f;
        int culture = 0;
    };
    bool initialized = false;
    int remaining = 0;
    float maxWeight = 0.0f; // weightedAmuletsProb._maxWeight (only its sign matters)
    std::vector<Amulet> amulets;
};

struct ChestStack {
    const ChestItem* item = nullptr;
    std::string itemName;
    int quantity = 0;
    const ChestEffect* effect = nullptr;
};

struct ChestGenArgs {
    int culture = 0;       // DungeonManager.culture as passed to GenerateChestContent
    int listCulture = 0;   // key into the lists (the intermediate culture in intermediate dungeons)
    int chestType = 0;
    int capacity = 28;
    int maxDifferentItems = 0;
    int maxItemQuantityPerStack = 0;
    float dropIncrement = 0.0f;
};

// Returns the chest's slots (empty slots have item == nullptr). `ownedAmulets` are names of
// amulets the player owns (CheckIfAmuletOwned); `amulets` is updated (PURPLE picks).
std::vector<ChestStack> GenerateChestContent(const ChestGenArgs& args, const ChestTables& tables, const std::vector<std::string>& ownedAmulets,
                                             AmuletState& amulets);

} // namespace game
