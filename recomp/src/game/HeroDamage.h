#pragma once
// HeroMerchant.DealDamageToPlayer's damage type, and what the ItemDrain one does to his bag.
//
// An enemy hits the hero through EnemyHero (game/Enemy.h): dealTypedDamage takes the type,
// dealDamage (the older hook) doesn't. DealDamageToPlayer calls whichever is set.
//
// ItemDrain (the HermitSlime's AttackHit) ends DealDamageToPlayer with
// heroMerchantInventory.DropRandomItems(3, mustDropCursedItems: false) (HeroMerchantInventory.cs:1311):
//   - the slots holding an item without a curse (item.effect == null), in slot order;
//   - per drop while any are left: Random.Range(0, count) picks one; it's dropped
//     (ItemStack.Drop(his position, 30, the room): Random.Range(0, 360) for its direction, then a
//     Physics2D.Raycast against LevelBoundary and DOTweens, no more draws), and it leaves the list.
// An empty bag draws nothing. The port has no inventory yet: DropRandomItemsDraws makes the
// draws for a bag the caller describes.

#include <functional>
#include <string>
#include <vector>

namespace game {

class Enemy;
struct EnemyHero;

// HeroMerchant.DamageType.
enum class HeroDamageType { Normal = 0, ItemDrain = 1, Self = 2 };

// HeroMerchant.Instance.DealDamageToPlayer(attackStrength, damageSource, type, enemySource):
// the hero's dealTypedDamage, else his dealDamage (the type dropped); none: nothing.
void DealDamageToPlayer(EnemyHero& hero, float attackStrength, const std::string& damageSource, HeroDamageType type, Enemy& enemySource);

// One item DropRandomItems dropped: its slot, and ItemStack.Drop's Random.Range(0, 360).
struct ItemDrainDrop {
    int slot = -1;
    int angle = 0;
};

// DropRandomItems(number)'s Random draws for a bag whose droppable slots (an item, and with
// mustDropCursedItems false no curse) are `slots`, in slot order: the drops in order.
std::vector<ItemDrainDrop> DropRandomItemsDraws(std::vector<int> slots, int number);

// DealDamageToPlayer's ItemDrain branch: DropRandomItems(3, mustDropCursedItems: false).
constexpr int kItemDrainDrops = 3;

} // namespace game
