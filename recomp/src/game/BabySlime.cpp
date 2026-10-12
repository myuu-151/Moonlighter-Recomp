#include "game/BabySlime.h"

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

} // namespace game
