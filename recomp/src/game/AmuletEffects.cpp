#include "game/AmuletEffects.h"

#include "unity/Random.h"

namespace game {

void WandererAmuletEffect::Enemy_OnEnemyRecivesDamage(const Enemy& enemy, float, EnemyDamageReceivedType type)
{
    // (The conditions in order, && short-circuiting: Random.value is drawn only past the rest.)
    if (enemy.isBreakableObject || type == EnemyDamageReceivedType::Effect || !dungeonManager || wandererActive) return;
    if (!(unity::Random::value() < probToCallWanderer)) return;
    wandererActive = true; // (SpawnWanderer's isWandererActive = true)
    if (spawnWanderer) spawnWanderer();
}

} // namespace game
