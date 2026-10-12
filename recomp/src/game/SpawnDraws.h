#pragma once
// RNG use of spawning that happens through prefab scripts:
//  - enemy Init: only the Tangle family draws (TangleBehaviour / ClothTangleBehaviour /
//    TeslaTangleBehaviour.Init: two Range(-1, 2) flips of the swing amplitudes);
//  - SpawnRoomPrefabs' skeleton roll and BreakableObjectBehaviour.TransformToSkeleton.
// Prefab instantiation itself draws nothing except Chest.Awake (see Chest.h).

#include <string>

namespace game {

struct TangleInit {
    int amplitudeXSign = 1, amplitudeYSign = 1;
};

// Enemy.Init for the prefab named `enemyName` (EnemySpawn.enemy.name). Returns whether this
// enemy type draws at Init; `out` gets the Tangle amplitude signs.
bool EnemyInit(const std::string& enemyName, TangleInit* out = nullptr);

struct SkeletonPick {
    bool rolled = false;  // passed the roomSkeletonProbability roll
    int candidate = -1;   // index into the FindObjectsOfType<BreakableObjectBehaviour>() array
    int skin = -1;        // index into skeletonBreakableSkins (-1: cap reached / no skins)
};

// The skeleton part of DungeonRoom.SpawnRoomPrefabs. `breakablesInScene` is the length of
// FindObjectsOfType's array (engine order), `skeletonsDeployed` the floor's count so far
// (DungeonManager.sleketonsDeployed, incremented here), `skinCount` the skins list size.
SkeletonPick RollSkeleton(int breakablesInScene, int& skeletonsDeployed, int skinCount);

} // namespace game
