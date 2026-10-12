#include "game/RoomPlacement.h"

#include "game/Constants.h"
#include "game/PrefabCatalog.h"
#include "game/SpawnDraws.h"

namespace game {

namespace {

void AwakeChest(RoomEngine& engine)
{
    bool ngp = false;
    ChestConfig cfg = engine.NextChest(ngp);
    GenerateFixedDrop(cfg, ngp);
    if (cfg.generateRandomContentOnInit) engine.RandomChestContents(cfg);
}

} // namespace

RoomFill* PlaceRoom(DungeonRoomInfo* info, int slicesX, int slicesY, Culture roomCulture, FloorRoomState& floor, RoomEngine& engine)
{
    auto* fill = cs::gcnew<RoomFill>(info, slicesX, slicesY);

    // InitializeRoom(roomInfo) -> LoadGridFromInfo.
    fill->LoadGridFromInfo();

    // RoomPlaced: SpawnEnemies (champion roll, then each enemy's Init).
    (void)fill->ChooseChampion(false);
    if (info->enemiesSpawnPacks && info->enemiesSpawnPacks->Count() > 0) {
        DungeonRoomInfo::EnemyPack* pack = (*info->enemiesSpawnPacks)[fill->currentSpawnPack];
        for (DungeonRoomInfo::EnemySpawn* spawn : *pack->spawns) {
            if (!spawn->enemy) continue;
            // CreateEnemyPrefab: no prefab, no enemy (and no Init).
            std::string prefab = PrefabCatalog::Loaded() ? PrefabCatalog::FindEnemy(spawn->enemy->name, floor.enemyBundles) : spawn->enemy->name;
            if (prefab.empty()) continue;
            EnemyInit(spawn->enemy->name);
            fill->roomEnemies++;
            fill->enemies.push_back({spawn->index, prefab});
        }
    }

    // SpawnChests: tier/mimic per chest tile; real chests run Chest.Awake.
    for (size_t t = 0; t < fill->obstaclesGrid.size(); t++) {
        if (fill->obstaclesGrid[t] != 32) continue;
        std::string name = fill->GetChestNameByFloor(floor.level, true, false);
        fill->chests.push_back({static_cast<int>(t), name});
        if (name.find("Mimic") != std::string::npos) { // mimic enemies draw nothing at Init
            fill->roomEnemies++;
            continue;
        }
        AwakeChest(engine);
    }

    if (!fill->obstaclesGrid.empty()) {
        RoomGenerator* g = floor.generator;
        g->SetLights(cs::gcnew<cs::List<int>>());
        g->SetShadows(cs::gcnew<cs::List<int>>());
        RoomGenerator::LightShadow ls = g->AddLightShadow(roomCulture);
        // GetMiddleDetailZonesFromGrid and GenerateRocksPrefab draw nothing.
        g->RasterizeHoles(roomCulture, fill->obstaclesGrid, fill->gridSlicesX, fill->gridSlicesY);
        // SetBackgroundImages draws nothing: the room culture's background for this floor.
        if (const RoomGenerationData* d = static_cast<int>(roomCulture) < 6 ? RoomGenerator::generationDataByCulture[static_cast<int>(roomCulture)] : nullptr) {
            auto name = [](const std::vector<std::string>& names, int i) {
                return i >= 0 && i < static_cast<int>(names.size()) ? names[static_cast<size_t>(i)] : std::string();
            };
            fill->art.light = name(d->lightNames, ls.light);
            fill->art.shadow = name(d->shadowNames, ls.shadow);
            fill->art.shadowFlipX = ls.flipX;
            fill->art.shadowFlipY = ls.flipY;
            fill->art.background = name(d->backgroundNames, floor.level);
            fill->art.walls = name(d->backgroundWallNames, floor.level);
            fill->art.set = true;
        }
        for (int tile : {16, 64, 128, 256}) g->RasterizeSlime(fill->obstaclesGrid, fill->gridSlicesX, fill->gridSlicesY, tile);
        // GenerateFallingFloorPrefab draws nothing.

        // SpawnRoomPrefabs: prefabs (Chest.Awake for chest prefabs), then the skeleton roll.
        for (int c = engine.ChestsInPrefabs(info); c > 0; c--) AwakeChest(engine);
        RollSkeleton(engine.BreakablesInScene(), floor.skeletonsDeployed, engine.SkeletonSkins());
    }

    // SpawnDetails.
    if (roomCulture != Culture::Unknown) {
        if (const DetailsData* data = engine.Details(roomCulture, floor.level)) {
            DetailsRoom room = engine.RoomPrefab(roomCulture, floor.culture);
            room.culture = roomCulture;
            room.floorCulture = floor.culture;
            room.zones = CreateMiddleDetailZones(fill->obstaclesGrid, fill->gridSlicesX, fill->gridSlicesY, Constants::GetFloat("tileSize"));
            const DungeonRoomDetailsGenerationInfo* d = info->detailGenerationInfo;
            room.wallProb = d->wallDetailGenerationProb;
            room.middleProb = d->middleDetailGenerationProb;
            room.particlesProb = d->particlesDetailGenerationProb;
            room.breakablesProb = d->breakablesProbability;
            room.creaturesProb = d->creaturesDetailGenerationProb;
            room.middlePerZone = d->middleDetailsPerZone;
            room.breakablesPerZone = d->breakablesPerZone;
            room.creaturesPerZone = d->creaturesPerZone;
            fill->details = SpawnDetails(room, *data, floor.cache);
            fill->detailsData = data;
            fill->detailSpawners = room.spawners;
        }
    }
    return fill;
}

} // namespace game
