#pragma once
// Chest.Awake's RNG use (Chest.cs): GenerateFixedDrop rolls each fixed drop, then shuffles
// the contents with Extensions.Shuffle (Count-1 draws, always over the full Count).
// Item creation itself (ItemStack, TryAddItem) draws nothing.

#include <string>
#include <vector>

namespace game {

struct ChestDrop {
    std::string itemName;
    int minQuantity = 1;
    int maxQuantity = 1;
    float prob = 1.0f;
    bool ngpOnly = false;
    bool ownedAmulet = false; // ItemDatabase item is an amulet the player already owns
};

struct ChestConfig {
    int capacity = 28;
    float dropIncrement = 1.0f;
    bool generateRandomContentOnInit = false;
    std::vector<ChestDrop> fixedDrop;
};

struct FixedDropResult {
    std::string itemName;
    int quantity;
};

// Chest.PlayUnlockChestSound's MasterAudio group for a Chest.chestType (WOOD 0, IRON 1, GOLD 2,
// BLUE 3: "dungeon_chest_unlock"; PURPLE 4, PIRATE 5: the general dungeons' magical and pirate
// unlocks, their names with the trailing space Chest.cs has); nullptr: the others play none.
const char* UnlockChestSound(int chestType);

// GenerateFixedDrop for a freshly awoken chest (content = `capacity` empty slots).
// Returns the drops that were added (before the shuffle); `ngp` = NewGamePlus save.
std::vector<FixedDropResult> GenerateFixedDrop(const ChestConfig& chest, bool ngp);

} // namespace game
