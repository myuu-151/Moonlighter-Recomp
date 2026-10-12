#pragma once
// An enemy object from its prefab (DungeonRoom's Instantiate of enemies_<culture>/<name>): the
// root's BoxCollider2D, Rigidbody2D, layer and tag; the behaviour the prefab carries, by its
// class (MakeBehaviour) with its serialized fields; the PolyNavAgent's fields; the
// "AttackCollider" child's trigger (box, enabled flag, local position), the children with an
// EnemyDamageReceiver (hurtboxes), a trigger root's flag, and the sprite's first frame. The Animator is bound once those are in (its write defaults are the prefab's).

#include <memory>
#include <string>
#include <vector>

#include "game/Enemy.h"
#include "game/Prefabs.h"

namespace game {

// The behaviour for a prefab's MonoBehaviour class ("BabySlimeBehaviour",
// "GolemSoldierBehaviour", "EnemyBehaviour"); nullptr for a class that isn't an enemy
// behaviour the port has. `prefabName` picks what the class leaves to the prefab (the
// BabySlime's culture).
std::unique_ptr<EnemyBehaviour> MakeBehaviour(const std::string& cls, const std::string& prefabName, Enemy& enemy);

// Instantiated under `parents` (nearest first: the spawn point's local position, the enemy
// container's, the room's) with its behaviour (a plain EnemyBehaviour if the prefab's isn't
// ported). Not yet Init: call enemy->Init(name, stats, context) (SpawnEnemy does, at the
// floor's generation).
std::unique_ptr<Enemy> MakeEnemy(const Prefab& prefab, EnemyScene& scene, const unity::AnimatorController* controller,
                                 const std::vector<unity::Vector2>& parents);

} // namespace game
