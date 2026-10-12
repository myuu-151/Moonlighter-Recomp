#include "game/BabySlime.h"

#include "game/Tangle.h"

namespace game {

void BabySlimeBehaviour::Init()
{
    fallInHoles = false;
    EnemyBehaviour::Init();
}

void BabySlimeBehaviour::OnDeath()
{
    EnemyBehaviour::OnDeath();
    if (registerDeath) registerDeath(babySlimeCulture);
}

EnemyBehaviour::CommonEnemyCulture BabySlimeCulture(const std::string& prefabName)
{
    using C = EnemyBehaviour::CommonEnemyCulture;
    if (prefabName.rfind("Forest", 0) == 0) return C::Forest;
    if (prefabName.rfind("Desert", 0) == 0) return C::Desert;
    if (prefabName.rfind("Tech", 0) == 0) return C::Tech;
    return C::Golem;
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
        if (b.cls == "BabySlimeBehaviour") {
            auto slime = std::make_unique<BabySlimeBehaviour>(*enemy);
            slime->babySlimeCulture = BabySlimeCulture(prefab.name);
            enemy->enemyBehaviour = std::move(slime);
            behaviourFields = &b;
        } else if (b.cls == "TangleBehaviour") { // (game/Tangle.h)
            enemy->enemyBehaviour = MakeTangleBehaviour(*enemy, b);
            behaviourFields = &b;
        } else if (b.cls == "EnemyBehaviour") {
            enemy->enemyBehaviour = std::make_unique<EnemyBehaviour>(*enemy);
            behaviourFields = &b;
        } else if (b.cls == "PolyNavAgent") {
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
        EnemyBehaviour& eb = *enemy->enemyBehaviour;
        eb.canMove = behaviourFields->Field("canMove", 1.0) != 0.0;
        eb.fallInHoles = behaviourFields->Field("fallInHoles", 1.0) != 0.0;
        eb.attackPushBackwardsForce = static_cast<float>(behaviourFields->Field("attackPushBackwardsForce", 200.0));
    }
    for (const PrefabNode& n : prefab.nodes) {
        if (n.path == "AttackCollider")
            for (const ColliderDef& c : n.colliders)
                if (c.type == "BoxCollider2D" && c.enabled) {
                    enemy->hasAttackCollider = true;
                    enemy->attackOffset = {c.ox, c.oy};
                    enemy->attackSize = {c.sx, c.sy};
                    enemy->attackLayer = n.objectLayer;
                }
        // Enemy.enemySprite: the first SpriteRenderer below the root (GetComponentInChildren).
        if (enemy->sprite.empty() && n.HasRenderer() && !n.sprite.empty()) enemy->sprite = n.sprite;
    }
    enemy->hasNavAgent = true;
    enemy->Instantiate(parents);
    return enemy;
}

} // namespace game
