#pragma once
// The hero's amulet effects (AmuletMoonlighterEffect subclasses) where they reach the enemies'
// code: their Enemy.OnEnemyRecivesDamage handlers (EnemyScene::onEnemyReceivesDamage).
//
// WandererAmuletEffect.cs (the Wanderer Amulet): on every enemy hit, unless the enemy's
// behaviour is a BreakableObjectBehaviour (a breakable's: an Enemy that is a BreakableObject
// here, as the breakable prefabs have both), the damage type is Effect, there's no
// DungeonManager, or its Wanderer is out already (isWandererActive): Random.value, and below
// probToCallWanderer (the effect's serialized field) DungeonManager.SpawnWanderer (which sets
// isWandererActive). Its strength bonus (ApplyToHeroMerchant) is the hero's stats.
// recq kamifix-3 (enemy_KamikazeGolem.jsonl, the oracle's probe): the one Random draw in the
// KamikazeGolem's CreateExplosionToDamageEnemies is this handler's, for its explosion's hit on
// itself (invincible: its health stays, the event is raised all the same).

#include <functional>

#include "game/Enemy.h"

namespace game {

struct WandererAmuletEffect {
    float probToCallWanderer = 0.0f;
    bool dungeonManager = true; // DungeonManager.Instance exists
    bool wandererActive = false; // DungeonManager.isWandererActive
    // DungeonManager.SpawnWanderer (not ported: the Wanderer's arrival).
    std::function<void()> spawnWanderer;

    void Enemy_OnEnemyRecivesDamage(const Enemy& enemy, float damage, EnemyDamageReceivedType type);
};

} // namespace game
