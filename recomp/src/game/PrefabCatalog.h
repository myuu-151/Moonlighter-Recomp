#pragma once
// Which prefabs the game's bundles hold (data/extracted/bundle_assets.json, from the bundles'
// manifests) and the game's lookups over them. The game finds prefabs by path substring
// (AssetBundleUtils.LoadAssetContaining: lower-cased Contains over the bundle's asset paths),
// and silently spawns nothing when a lookup misses (Golem_Chest_7 names a
// "ChampionGolemTurret" that doesn't exist).

#include <string>
#include <vector>

#include "game/RoomDatabase.h"
#include "util/Json.h"

namespace game {

class PrefabCatalog {
public:
    static void Load(const json::Value& bundleAssets);
    static bool Loaded();

    // EnemiesPrefabRegister.GetEnemyByName(name), searching the enemy bundles of `loaded`
    // cultures in culture order. Returns the prefab's name, or "" when there's none.
    static std::string FindEnemy(const std::string& name, const std::vector<Culture>& loaded);
};

} // namespace game
