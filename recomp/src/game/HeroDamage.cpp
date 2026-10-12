#include "game/HeroDamage.h"

#include "game/Enemy.h"
#include "unity/Random.h"

namespace game {

void DealDamageToPlayer(EnemyHero& hero, float attackStrength, const std::string& damageSource, HeroDamageType type, Enemy& enemySource)
{
    if (hero.dealTypedDamage) hero.dealTypedDamage(attackStrength, damageSource, type, enemySource);
    else if (hero.dealDamage) hero.dealDamage(attackStrength, damageSource, enemySource);
}

std::vector<ItemDrainDrop> DropRandomItemsDraws(std::vector<int> slots, int number)
{
    std::vector<ItemDrainDrop> drops;
    for (int j = 0; j < number; j++) {
        if (slots.empty()) continue;
        const int index = unity::Random::Range(0, static_cast<int>(slots.size()));
        ItemDrainDrop d;
        d.slot = slots[static_cast<size_t>(index)];
        // ItemStack.Drop: Vector2.right.Rotate(Random.Range(0, 360)).
        d.angle = unity::Random::Range(0, 360);
        drops.push_back(d);
        slots.erase(slots.begin() + index);
    }
    return drops;
}

} // namespace game
