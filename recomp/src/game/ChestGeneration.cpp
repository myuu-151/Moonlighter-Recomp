#include "game/ChestGeneration.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "unity/Random.h"

namespace game {

namespace Random = unity::Random;

namespace {

// WeightedList<T>.GetRandom(filter) (WeightedList.cs). Returns the index into `items`, or -1.
template <typename T, typename Filter>
int WeightedPick(const Weighted<T>& wl, Filter keep)
{
    if (wl.maxWeight <= 0.0f) return -1;
    std::vector<int> list;
    float total = 0.0f;
    for (int i = 0; i < static_cast<int>(wl.items.size()); i++)
        if (keep(wl.items[i].first)) {
            list.push_back(i);
            total += wl.items[i].second;
        }
    if (list.empty()) return -1;
    float num = Random::Range(0.0f, total);
    int k = 0;
    while (num > 0.0f) {
        if (k == static_cast<int>(list.size())) return list.back();
        num -= wl.items[list[k]].second;
        k++;
    }
    return list[std::max(0, k - 1)];
}

// Unfiltered: same walk over the whole list with the stored _maxWeight as the total.
template <typename T>
int WeightedPick(const Weighted<T>& wl)
{
    if (wl.maxWeight <= 0.0f || wl.items.empty()) return -1;
    float num = Random::Range(0.0f, wl.maxWeight);
    int k = 0;
    while (num > 0.0f) {
        if (k == static_cast<int>(wl.items.size())) return static_cast<int>(wl.items.size()) - 1;
        num -= wl.items[k].second;
        k++;
    }
    return std::max(0, k - 1);
}

// Effect.Init -> action OnCreate.
void OnCreate(const ChestEffect* e)
{
    if (!e || !e->slotAction) return;
    for (int i = 0; i < e->randomDirections; i++) (void)Random::Range(0, 8); // {1,2,4,...,128}.GetRandom()
    if (e->selectRandom) (void)Random::Range(0, 2);                          // {1,2}.GetRandom()
}

// ItemStack.Create(master) (+ ItemStack.Init's trick effect for run-bound items).
ChestStack Create(const ChestItem* item, const std::string& name, bool destroyedOnRunEnded, const ChestTables& t)
{
    ChestStack s;
    s.item = item;
    s.itemName = name;
    s.quantity = 1;
    if (destroyedOnRunEnded) {
        s.effect = &t.trick;
        OnCreate(s.effect);
    }
    return s;
}

int CeilToInt(float f) { return static_cast<int>(std::ceil(f)); }

bool TryAddItem(const ChestStack& stack, std::vector<ChestStack>& content)
{
    // Merging into an equal stack only happens for the final amulet; stacks of 1 merge only
    // while maxStack allows, which the port doesn't need for RNG (no draws).
    for (auto& c : content)
        if (c.itemName.empty()) {
            c = stack;
            return true;
        }
    return false;
}

} // namespace

std::vector<ChestStack> GenerateChestContent(const ChestGenArgs& a, const ChestTables& t, const std::vector<std::string>& owned, AmuletState& amulets)
{
    std::vector<ChestStack> content(static_cast<size_t>(a.capacity));
    auto inContent = [&](const ChestItem* item) {
        for (const ChestStack& c : content)
            if (c.item == item) return true;
        return false;
    };
    auto isOwned = [&](const std::string& name) { return std::find(owned.begin(), owned.end(), name) != owned.end(); };

    auto listIt = t.lists.find({a.chestType, a.listCulture});
    if (listIt == t.lists.end()) return content; // the game throws (KeyNotFound) here
    const auto& outer = listIt->second;

    int num = CeilToInt(static_cast<float>(a.maxDifferentItems) * (1.0f + a.dropIncrement));
    while (num > 0) {
        int tier = WeightedPick(outer);
        if (tier < 0) break;
        const auto& inner = outer.items[static_cast<size_t>(tier)].first;
        int pick = WeightedPick(inner, [&](const ChestItem* it) { return !inContent(it); });
        if (pick < 0) {
            num--;
            continue;
        }
        const ChestItem* item = inner.items[static_cast<size_t>(pick)].first;
        if (item->amulet && isOwned(item->name)) continue; // retry without counting
        ChestStack stack = Create(item, item->name, item->destroyedOnRunEnded, t);
        if (stack.effect == nullptr) {
            // ItemEffectManager.GetRandomEffect(type): weighted evilness level, then uniform effect.
            const auto& levels = t.effects.at(static_cast<size_t>(a.chestType));
            int lv = WeightedPick(levels);
            if (lv < 0) throw std::runtime_error("GetRandomEffect on an empty level list"); // NRE in the game
            const auto& effects = levels.items[static_cast<size_t>(lv)].first;
            if (!effects.empty()) {
                stack.effect = effects[static_cast<size_t>(Random::Range(0, static_cast<int>(effects.size())))];
                OnCreate(stack.effect);
            }
        }
        if (a.culture == 7) { // Wanderer
            int q = item->fixedChestStack == 0 ? Random::Range(item->minChestStack, item->maxChestStack) : item->fixedChestStack;
            stack.quantity = std::min(q, item->maxStack);
        } else {
            const ChestEffect* e = stack.effect;
            if (!e || !e->slotAction || e->fixedStack == 0) stack.quantity = Random::Range(1, a.maxItemQuantityPerStack + 1);
            else stack.quantity = std::min(e->fixedStack, item->maxStack);
        }
        if (stack.quantity > 0) TryAddItem(stack, content);
        num--;
    }

    // AmuletUtility.GetAmuletToGenerate (Init runs lazily on the first call; no draws).
    amulets.initialized = true;
    if (amulets.remaining > 0) {
        if (a.chestType == 4) { // PURPLE
            Weighted<int> wl;
            wl.maxWeight = amulets.maxWeight;
            for (size_t i = 0; i < amulets.amulets.size(); i++) wl.items.push_back({static_cast<int>(i), amulets.amulets[i].weight});
            int pick = WeightedPick(wl, [&](int i) { return amulets.amulets[static_cast<size_t>(i)].culture == a.culture || amulets.amulets[static_cast<size_t>(i)].culture == 0; });
            amulets.remaining--;
            if (pick >= 0) {
                AmuletState::Amulet am = amulets.amulets[static_cast<size_t>(wl.items[static_cast<size_t>(pick)].first)];
                amulets.maxWeight -= am.weight;
                amulets.amulets.erase(amulets.amulets.begin() + wl.items[static_cast<size_t>(pick)].first);
                // ItemStack.Create(amulet): amulets aren't run-bound in this data, so no trick effect.
                ChestStack s;
                s.itemName = am.name;
                s.quantity = 1;
                TryAddItem(s, content);
            }
        }
    }

    // ChestGeneration.Shuffle: move every occupied slot to a random empty one (rejection sampling).
    std::vector<int> moved;
    for (int i = 0; i < static_cast<int>(content.size()); i++) {
        if (std::find(moved.begin(), moved.end(), i) != moved.end() || content[static_cast<size_t>(i)].itemName.empty()) continue;
        int r = Random::Range(0, static_cast<int>(content.size()));
        while (!content[static_cast<size_t>(r)].itemName.empty()) r = Random::Range(0, static_cast<int>(content.size()));
        content[static_cast<size_t>(r)] = content[static_cast<size_t>(i)];
        content[static_cast<size_t>(i)] = ChestStack();
        moved.push_back(r);
    }
    return content;
}

} // namespace game
