#include "game/EnemyFactory.h"

#include "game/BabySlime.h"
#include "game/BrokenGolemTurret.h"
#include "game/GolemSoldier.h"
#include "game/Tangle.h"

namespace game {

std::unique_ptr<EnemyBehaviour> MakeBehaviour(const std::string& cls, const std::string& prefabName, Enemy& enemy)
{
    if (cls == "BabySlimeBehaviour") {
        auto slime = std::make_unique<BabySlimeBehaviour>(enemy);
        slime->babySlimeCulture = BabySlimeCulture(prefabName);
        return slime;
    }
    if (cls == "GolemSoldierBehaviour") return std::make_unique<GolemSoldierBehaviour>(enemy);
    if (cls == "EnemyBehaviour") return std::make_unique<EnemyBehaviour>(enemy);
    return nullptr;
}

std::unique_ptr<Enemy> MakeEnemy(const Prefab& prefab, EnemyScene& scene, const unity::AnimatorController* controller,
                                 const std::vector<unity::Vector2>& parents)
{
    auto enemy = std::make_unique<Enemy>(scene, controller);
    const PrefabNode& root = prefab.nodes.at(0);
    enemy->layer = root.objectLayer;
    enemy->tag = root.tag;
    for (const ColliderDef& c : root.colliders)
        if (c.type == "BoxCollider2D") {
            enemy->colliderOffset = {c.ox, c.oy};
            enemy->colliderSize = {c.sx, c.sy};
            break;
        }
    enemy->hasRigidbody = root.hasRigidbody;
    enemy->rigidbody = root.rigidbody;
    const Behaviour* behaviourFields = nullptr;
    for (const Behaviour& b : root.behaviours) {
        if (!enemy->enemyBehaviour && b.cls == "TangleBehaviour") { // (its fields: game/Tangle.h)
            enemy->enemyBehaviour = MakeTangleBehaviour(*enemy, b);
            behaviourFields = &b;
        } else if (!enemy->enemyBehaviour && b.cls == "BrokenGolemTurretBehaviour") { // (game/BrokenGolemTurret.h)
            enemy->enemyBehaviour = MakeBrokenGolemTurretBehaviour(*enemy, b);
            behaviourFields = &b;
        } else if (!enemy->enemyBehaviour) {
            if (auto behaviour = MakeBehaviour(b.cls, prefab.name, *enemy)) {
                enemy->enemyBehaviour = std::move(behaviour);
                behaviourFields = &b;
                continue;
            }
        }
        if (b.cls == "PolyNavAgent") {
            PolyNavAgent& a = enemy->navAgent;
            a.maxSpeed = static_cast<float>(b.Field("maxSpeed", a.maxSpeed));
            a.mass = static_cast<float>(b.Field("mass", a.mass));
            a.stoppingDistance = static_cast<float>(b.Field("stoppingDistance", a.stoppingDistance));
            a.slowingDistance = static_cast<float>(b.Field("slowingDistance", a.slowingDistance));
            a.accelerationRate = static_cast<float>(b.Field("accelerationRate", a.accelerationRate));
            a.decelerationRate = static_cast<float>(b.Field("decelerationRate", a.decelerationRate));
            a.avoidRadius = static_cast<float>(b.Field("avoidRadius", a.avoidRadius));
            a.lookAheadDistance = static_cast<float>(b.Field("lookAheadDistance", a.lookAheadDistance));
            a.repath = b.Field("repath", a.repath ? 1.0 : 0.0) != 0.0;
            a.restrict = b.Field("restrict", a.restrict ? 1.0 : 0.0) != 0.0;
            a.closerPointOnInvalid = b.Field("closerPointOnInvalid", a.closerPointOnInvalid ? 1.0 : 0.0) != 0.0;
            a.roundCorners = b.Field("roundCorners", a.roundCorners ? 1.0 : 0.0) != 0.0;
            a.rotateTransform = b.Field("rotateTransform", a.rotateTransform ? 1.0 : 0.0) != 0.0;
        }
    }
    if (!enemy->enemyBehaviour) enemy->enemyBehaviour = std::make_unique<EnemyBehaviour>(*enemy);
    if (behaviourFields) {
        // EnemyBehaviour's serialized fields (missing from older data: the class's defaults).
        EnemyBehaviour& eb = *enemy->enemyBehaviour;
        eb.canMove = behaviourFields->Field("canMove", 1.0) != 0.0;
        eb.fallInHoles = behaviourFields->Field("fallInHoles", 1.0) != 0.0;
        eb.attackPushBackwardsForce = static_cast<float>(behaviourFields->Field("attackPushBackwardsForce", 200.0));
        if (auto* soldier = dynamic_cast<GolemSoldierBehaviour*>(&eb))
            soldier->iDirectionLocked = behaviourFields->Field("iDirectionLocked", 0.0) != 0.0;
    }
    for (const PrefabNode& n : prefab.nodes) {
        if (n.path == "AttackCollider")
            for (const ColliderDef& c : n.colliders)
                if (c.type == "BoxCollider2D") {
                    // (The GolemSoldier's box is off in the prefab: its swing turns it on.)
                    enemy->hasAttackCollider = true;
                    enemy->attackEnabled = c.enabled;
                    enemy->attackActive = n.active;
                    enemy->attackPosition = {n.x, n.y};
                    enemy->attackOffset = {c.ox, c.oy};
                    enemy->attackSize = {c.sx, c.sy};
                    enemy->attackLayer = n.objectLayer;
                    break;
                }
        // Enemy.enemySprite: the first SpriteRenderer below the root (GetComponentInChildren).
        if (enemy->sprite.empty() && n.HasRenderer() && !n.sprite.empty()) enemy->sprite = n.sprite;
    }
    // The Animator binds its write defaults to the object as the prefab has it.
    enemy->ResetAnimator();
    enemy->hasNavAgent = true;
    enemy->Instantiate(parents);
    return enemy;
}

} // namespace game
