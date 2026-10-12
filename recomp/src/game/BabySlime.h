#pragma once
// BabySlimeBehaviour.cs: the first enemy, an EnemyBehaviour with nothing of its own but two
// details: it never falls into holes (Init clears fallInHoles, which its prefab sets, before
// the base Init) and its death counts in HeroStatistics by culture. Everything else is
// EnemyBehaviour's (game/Enemy.h): after DungeonRoom.EnableEnemies it waits a second
// (StartTargetingPlayerAfterTime(1)), then every frame chases the hero through its
// PolyNavAgent (a new destination every 0.1 s: his feet, or beside him when it's 1.5 to 3
// tiles away), and hurts him by touching him with its "AttackCollider" trigger.
//
// Its Animator (animators/BabySlime.json) has a walk loop as its default state and an
// "Attack" state nothing transitions to: the slime walks in place forever (AttackPlayer's
// trigger and LookToPosition's floats are set for nothing).
//
// MakeEnemy builds one from its prefab (enemies_golem/BabySlime and the other cultures'):
// the root's BoxCollider2D, Rigidbody2D, layer and tag, the BabySlimeBehaviour and
// PolyNavAgent fields, the "AttackCollider" child's trigger, the sprite's first frame.

#include <functional>
#include <memory>
#include <string>

#include "game/Enemy.h"
#include "game/Prefabs.h"

namespace game {

class BabySlimeBehaviour : public EnemyBehaviour {
public:
    explicit BabySlimeBehaviour(Enemy& enemy) : EnemyBehaviour(enemy) {}

    CommonEnemyCulture babySlimeCulture = CommonEnemyCulture::Golem;
    // HeroStatistics.Register<Culture>BabySlimeDeath.
    std::function<void(CommonEnemyCulture)> registerDeath;

    void Init() override;

protected:
    void OnDeath() override;
};

// The BabySlime prefabs' culture by name (the serialized babySlimeCulture isn't extracted:
// "BabySlime" Golem, "ForestBabySlime", "DesertBabySlime", "TechBabySlime").
EnemyBehaviour::CommonEnemyCulture BabySlimeCulture(const std::string& prefabName);

// An enemy object from its prefab, Instantiated under `parents` (nearest first: the spawn
// point's local position, the enemy container's, the room's) with its behaviour (a
// BabySlimeBehaviour for the BabySlime prefabs, a TangleBehaviour (game/Tangle.h) for the
// prefabs that have one, else a plain EnemyBehaviour). Not yet Init:
// call enemy->Init(name, stats, context) (SpawnEnemy does, at the floor's generation).
std::unique_ptr<Enemy> MakeEnemy(const Prefab& prefab, EnemyScene& scene, const unity::AnimatorController* controller,
                                 const std::vector<unity::Vector2>& parents);

} // namespace game
