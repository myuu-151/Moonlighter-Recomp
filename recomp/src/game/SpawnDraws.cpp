#include "game/SpawnDraws.h"

#include "game/Constants.h"
#include "unity/Random.h"

namespace game {

namespace Random = unity::Random;

bool EnemyInit(const std::string& enemyName, TangleInit* out)
{
    // Prefabs using TangleBehaviour, ClothTangleBehaviour or TeslaTangleBehaviour. Verified
    // against traces: these four draw exactly twice at spawn; the other 47 enemy types none.
    if (enemyName != "Tangle" && enemyName != "ForestTangle" && enemyName != "ClothTangle" && enemyName != "TeslaTangle") return false;
    int x = Random::Range(-1, 2) >= 0 ? 1 : -1;
    int y = Random::Range(-1, 2) >= 0 ? 1 : -1;
    if (out) *out = {x, y};
    return true;
}

SkeletonPick RollSkeleton(int breakablesInScene, int& skeletonsDeployed, int skinCount)
{
    SkeletonPick pick;
    if (!(Random::Range(0.0f, 1.0f) < Constants::GetFloat("roomSkeletonProbability"))) return pick;
    pick.rolled = true;
    if (breakablesInScene <= 0) return pick;
    pick.candidate = Random::Range(0, breakablesInScene);
    // TransformToSkeleton
    if (skinCount <= 0 || skeletonsDeployed >= Constants::GetInt("maxSkeletonsInFloor")) return pick;
    skeletonsDeployed++;
    pick.skin = Random::Range(0, skinCount);
    return pick;
}

} // namespace game
