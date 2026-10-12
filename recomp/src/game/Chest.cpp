#include "game/Chest.h"

#include <algorithm>

#include "unity/Random.h"

namespace game {

namespace Random = unity::Random;

std::vector<FixedDropResult> GenerateFixedDrop(const ChestConfig& chest, bool ngp)
{
    std::vector<FixedDropResult> added;
    for (const ChestDrop& d : chest.fixedDrop) {
        float value = Random::value();
        float threshold = d.prob * (1.0f + chest.dropIncrement);
        if (((d.ngpOnly & ngp) && value < threshold) || (!d.ngpOnly && value < threshold)) {
            if (!d.ownedAmulet) {
                int qty = Random::Range(std::max(1, d.minQuantity), d.maxQuantity + 1);
                added.push_back({d.itemName, qty});
            }
        }
    }
    // ChestContent.Shuffle(): the content list always holds `capacity` slots here.
    int count = chest.capacity;
    for (int num = count; num > 1;) {
        num--;
        (void)Random::Range(0, count);
    }
    return added;
}

} // namespace game
