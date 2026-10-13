#include "game/DungeonFloor.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "game/BossRoomStart.h"
#include "game/Constants.h"
#include "game/Prefabs.h"
#include "game/RoomScene.h"
#include "unity/Random.h"
#include "unity/Vector2.h"

namespace game {

namespace Random = unity::Random;
using Doors = DungeonRoomInfo::Doors;

namespace {

int GetRandomIndex(int count) { return count == 0 ? -1 : Random::Range(0, count); }

// Vector2.Distance: (a - b).magnitude, the sum of squares in float and Math.Sqrt in double.
float Distance(unity::Vector2 a, unity::Vector2 b)
{
    float dx = a.x - b.x, dy = a.y - b.y;
    return static_cast<float>(std::sqrt(static_cast<double>(dx * dx + dy * dy)));
}

} // namespace

void DungeonFloor::Generate(FloorGenerationSaveInfo& fi, FloorEngine& engine)
{
    layout.level = level_;
    layout.culture = static_cast<int>(culture_);
    layout.Generate(fi, false);

    FloorRoomState& floor = floor_;
    floor.generator = cs::gcnew<RoomGenerator>();
    floor.level = level_;
    floor.culture = culture_;
    floor.enemyBundles = {culture_};
    initCaveRoom_ = layout.initCaveRoom;

    RoomSelector sel(db_, culture_, level_);
    cs::List<DungeonRoomInfo*>* rooms = db_->GetRoomsByCulture(culture_, level_);
    auto* holesOnly = cs::gcnew<cs::List<DungeonRoomInfo*>>();
    for (DungeonRoomInfo* x : *rooms) {
        if (x->obstaclesSpawnPacks->Count() != 1) continue;
        cs::List<DungeonRoomInfo::TileSpawn*>* sp = (*x->obstaclesSpawnPacks)[0]->spawns;
        if (sp->FindIndex([](DungeonRoomInfo::TileSpawn* y) { return y->tile == DungeonRoomInfo::DungeonTile::Holes; }) != -1) holesOnly->Add(x);
    }
    cs::List<DungeonRoomInfo*>* timedChestRooms = db_->GetTimedChestRoomsByCulture(culture_, level_);
    cs::List<DungeonRoomInfo*>* chestRooms = db_->GetChestRoomsByCulture(culture_, level_);
    cs::List<DungeonRoomInfo*>* corruptedRooms = db_->GetCorruptedRooms(culture_);

    CaveGenerator* cave = layout.cave;
    const DungeonLayout& L = layout;
    CaveRoom* bossEnterRoom = nullptr;
    for (int i = 0; i < cave->rooms->Count(); i++) {
        CaveRoom* caveRoom = (*cave->rooms)[i];
        int x = static_cast<int>(caveRoom->position.x - cave->minPos.x);
        int y = static_cast<int>(caveRoom->position.y - cave->minPos.y);
        // The bundles' loading frames before the first room; the frame each earlier room's
        // WaitForEndOfFrame ended with.
        engine.FramePasses();
        if (cs::Is<BossCaveRoom>(caveRoom)) {
            // The room the boss enter door goes in: per unit the last of S, N, W, E that's
            // connected, from the first unit with any.
            for (CaveRoomUnit* bu : *caveRoom->units) {
                if (bu->connectedSouth) bossEnterRoom = bu->connectedSouth;
                if (bu->connectedNorth) bossEnterRoom = bu->connectedNorth;
                if (bu->connectedWest) bossEnterRoom = bu->connectedWest;
                if (bu->connectedEast) bossEnterRoom = bu->connectedEast;
                if (bossEnterRoom) break;
            }
            // AddBossRoom (level 3): instantiating the boss room prefab wakes its boss chest
            // (Chest.Awake, engine side). DungeonBossRoom.RoomPlaced then depends on the save:
            // if this culture's boss is already defeated, noBossEnemyController.InitEnemies()
            // picks the post-boss enemy set (a weighted pick: one roll) and spawns it.
            for (PlacedCell& c : cells)
                if (bossEnterRoom && c.cave == bossEnterRoom) {
                    c.room->specialDoors.push_back({0, "EnterBossDoor", std::string("dungeon_") + CultureBundle(culture_)});
                    c.room->doors.ReplaceSpecialDoor(0, false);
                    c.room->doors.isEnd = true;
                }
            engine.BossRoomPlaced();
            bool defeated = engine.BossDefeated();
            if (defeated) (void)Random::Range(0.0f, 1.0f);
            auto* boss = cs::gcnew<PlacedRoom>();
            boss->x = x;
            boss->y = y;
            sel._roomsGenerated->Add(boss);
            // `yield return null`: with the boss defeated the room is live and its objects'
            // Start() run (BossRoomStart); with the boss alive none of them draw.
            if (defeated) {
                const Prefab* room = PrefabLibrary::Find("DungeonLevelRoom_3_4_boss", std::string("dungeon_") + CultureBundle(culture_));
                if (!room) throw std::runtime_error("no boss room prefab (data/extracted/prefabs.json)");
                bossStart = BossRoomStart(*room);
            }
            engine.BossStartFrame();
            continue;
        }
        if (!cs::Is<SingleCaveRoom>(caveRoom)) continue;

        CaveRoomUnit* u = (*caveRoom->units)[0];
        bool isBoss = caveRoom == L.bossCaveRoom;
        auto* desired = cs::gcnew<cs::List<Doors>>();
        if (isBoss || u->connectedNorth) desired->Add(Doors::North);
        if (isBoss || u->connectedSouth) desired->Add(Doors::South);
        if (isBoss || u->connectedEast) desired->Add(Doors::East);
        if (isBoss || u->connectedWest) desired->Add(Doors::West);

        DungeonRoomInfo* info = nullptr;
        bool corrupted = caveRoom == L.corruptedCaveRoom;
        bool secretHost = caveRoom == L.wavesCaveRoom || caveRoom == L.homeChestCaveRoom || caveRoom == L.wandererNestCaveRoom ||
                          caveRoom == L.abandonedItemsCaveRoom;
        if (caveRoom == L.initCaveRoom) info = db_->initRoomInfo;
        else if (caveRoom == L.onsenCaveRoom) info = db_->healingRoomInfo;
        else if (caveRoom == L.crazyPeteCaveRoom) info = db_->campRoomInfo;
        else if (corrupted) {
            int pick = GetRandomIndex(corruptedRooms->Count()); // corruptedRooms.GetRandom()
            info = pick >= 0 ? (*corruptedRooms)[pick] : nullptr;
            engine.FramePasses(); // (its culture's bundles: EnemiesPrefabRegister.LoadBundle, ...)
        } else if (secretHost) {
            info = sel.GetRandomRoom(holesOnly, desired);
        } else {
            if (L.timedChestCaveRooms->IndexOf(caveRoom) != -1) info = sel.GetRandomRoom(timedChestRooms, desired);
            if (L.chestCaveRooms->IndexOf(caveRoom) != -1) info = sel.GetRandomRoom(chestRooms, desired);
            if (info == nullptr) info = sel.GetRandomRoom(rooms, desired, sel.GetContiguousRooms(x, y));
        }
        if (!corrupted && info) info->culture = culture_;
        if (isBoss) info = sel.GetMinibossRoom(engine.MinibossOverride());
        if (info == nullptr) info = sel.GetRandomRoom(rooms, desired, sel.GetContiguousRooms(x, y));

        PlacedCell cell;
        cell.cave = caveRoom;
        cell.x = x;
        cell.y = y;
        cell.info = info;
        engine.BeginRoom(info, x, y, false);
        // A corrupted room loads its culture's enemy bundle first (EnemiesPrefabRegister.LoadBundle).
        if (corrupted && std::find(floor.enemyBundles.begin(), floor.enemyBundles.end(), info->culture) == floor.enemyBundles.end())
            floor.enemyBundles.push_back(info->culture);
        cell.room = PlaceRoom(info, 20, 11, info->culture, floor, engine);
        // ConnectRoomsByDirection with the neighbours already placed: east, west, south, north.
        for (auto [to, side] : {std::pair<CaveRoom*, int>{u->connectedEast, 3}, {u->connectedWest, 2}, {u->connectedSouth, 1}, {u->connectedNorth, 0}}) {
            if (!to) continue;
            for (PlacedCell& n : cells)
                if (n.cave == to) RoomDoors::Connect(cell.room->doors, RoomPosition(x, y), side, n.room->doors, RoomPosition(n.x, n.y));
        }
        const std::string floorBundle = std::string("dungeon_") + CultureBundle(culture_);
        if (caveRoom == L.initCaveRoom) {
            cell.room->specialDoors.push_back({L.lastDoorDirectionIndex, level_ == 0 ? "FirstLevelDoor" : "LastLevelDoor", floorBundle});
            cell.room->doors.isInit = true;
            cell.room->doors.AddSpecialInitDoor(L.lastDoorDirectionIndex);
        }
        auto* placed = cs::gcnew<PlacedRoom>();
        placed->x = x;
        placed->y = y;
        placed->roomInfo = info;
        sel._roomsGenerated->Add(placed);

        if (isBoss) {
            // AddNextLevelDoor: free directions in the order East, West, South, North.
            auto* dirs = cs::gcnew<cs::List<int>>();
            if (u->connectedEast == nullptr) dirs->Add(3);
            if (u->connectedWest == nullptr) dirs->Add(2);
            if (u->connectedSouth == nullptr) dirs->Add(1);
            if (u->connectedNorth == nullptr) dirs->Add(0);
            int pick = GetRandomIndex(dirs->Count());
            nextLevelDoor = pick >= 0 ? (*dirs)[pick] : 0; // GetRandom on an empty list gives default(int)
            fi.lastDoorDirectionIndex = nextLevelDoor;
            cell.room->specialDoors.push_back({nextLevelDoor, "NextLevelDoor", floorBundle});
            cell.room->doors.isEnd = true;
            cell.room->doors.ReplaceSpecialDoor(nextLevelDoor, true);
        }
        if (secretHost) {
            DungeonRoomInfo* secretInfo = db_->sendToHomeRoomInfo;
            if (caveRoom == L.wavesCaveRoom) secretInfo = db_->waveRoomInfo;
            if (caveRoom == L.wandererNestCaveRoom) secretInfo = db_->wandererNestRoomInfo;
            if (caveRoom == L.abandonedItemsCaveRoom) secretInfo = db_->abandonedItemsRoomInfo;
            secretInfo->culture = culture_;
            engine.BeginRoom(secretInfo, x, y, true);
            cell.secretInfo = secretInfo;
            cell.secret = PlaceRoom(secretInfo, 20, 11, secretInfo->culture, floor, engine);
            AddSecretRoom(cell, "SecretRoomAdvisor");
        }
        cells.push_back(cell);
    }
    if (cave->rooms->Count() > 0) engine.FramePasses(); // (the last room's WaitForEndOfFrame)
    engine.BeforeSpecialRooms();
    SpawnSpecialRoomsAfterGeneration(fi, bossEnterRoom);
    engine.SpecialRoomsAfterGeneration();
}

// AddSecretRoom: the rope in the secret room; the advisor goes on one of the host room's
// hole tiles.
void DungeonFloor::AddSecretRoom(PlacedCell& host, const char* advisor)
{
    const std::string floorBundle = std::string("dungeon_") + CultureBundle(culture_);
    host.secret->ropeBundle = floorBundle;
    host.room->advisorPrefab = advisor;
    host.room->advisorBundle = std::string(advisor) == "IntermediateAdvisor" ? "dungeon_common" : floorBundle;
    std::vector<int> holes;
    for (size_t t = 0; t < host.room->obstaclesGrid.size(); t++)
        if (host.room->obstaclesGrid[t] == 2) holes.push_back(static_cast<int>(t));
    if (!holes.empty()) host.room->advisorTile = holes[static_cast<size_t>(Random::Range(0, static_cast<int>(holes.size())))];
}

int DungeonFloor::PickWandererHost()
{
    if (level_ >= 2) return -1;
    // GeneratedRooms.FindAll(one obstacle pack containing Holes).ToList(): capacity = count.
    auto* found = cs::gcnew<cs::List<int>>();
    for (size_t i = 0; i < cells.size(); i++) {
        DungeonRoomInfo* info = cells[i].info;
        if (info->obstaclesSpawnPacks->Count() != 1) continue;
        cs::List<DungeonRoomInfo::TileSpawn*>* sp = (*info->obstaclesSpawnPacks)[0]->spawns;
        if (sp->FindIndex([](DungeonRoomInfo::TileSpawn* y) { return y->tile == DungeonRoomInfo::DungeonTile::Holes; }) != -1)
            found->Add(static_cast<int>(i));
    }
    cs::List<int>* list = cs::List<int>::CopyOf(found);
    list->Sort([](int, int) { return Random::Range(0, 1000); });
    for (int i : *list) {
        const PlacedCell& c = cells[static_cast<size_t>(i)];
        if (c.cave != initCaveRoom_ && c.secret == nullptr) return i;
    }
    return -1;
}

int DungeonFloor::WandererPostprocess(DungeonRoomInfo* intermediate, FloorEngine& engine)
{
    int host = PickWandererHost();
    if (host < 0) return -1;
    PlacedCell& c = cells[static_cast<size_t>(host)];
    intermediate->culture = culture_;
    engine.BeginRoom(intermediate, c.x, c.y, true);
    c.secretInfo = intermediate;
    c.secret = PlaceRoom(intermediate, 20, 11, intermediate->culture, floor_, engine);
    c.wandererIntermediate = true;
    AddSecretRoom(c, "IntermediateAdvisor");
    return host;
}

void DungeonFloor::SpawnSpecialRoomsAfterGeneration(const FloorGenerationSaveInfo& fi, CaveRoom* bossEnterRoom)
{
    if (culture_ != Culture::Desert || fi.desertHardEnemiesRoom != level_) return;
    // The room with the most enemies (first on ties) among the generated rooms (secret rooms
    // aren't in _roomsGenerated), except the boss room, the room with the boss enter door and
    // the room with the next-floor door.
    int best = -1, num = 0;
    for (size_t i = 0; i < cells.size(); i++) {
        const PlacedCell& c = cells[i];
        bool enterDoor = bossEnterRoom != nullptr && c.cave == bossEnterRoom;
        bool nextDoor = c.cave == layout.bossCaveRoom;
        if (c.room->roomEnemies > num && !enterDoor && !nextDoor) {
            num = c.room->roomEnemies;
            best = static_cast<int>(i);
        }
    }
    if (best < 0) return; // the game's log line dereferences the null room: the coroutine dies here
    RoomFill* room = cells[static_cast<size_t>(best)].room;
    // Up to 50 tries for a free tile more than 40 units from every door boundary.
    int tiles = Constants::GetInt("roomTileCountX") * Constants::GetInt("roomTileCountY");
    for (int j = 0; j < 50 && shrineTile == -1; j++) {
        int t = Random::Range(0, tiles);
        unity::Vector2 a = TranslateTileToPosition(t, room->gridSlicesX);
        bool far = true;
        for (const unity::Vector2& d : kDoorBoundaries) far = far && Distance(a, d) > 40.0f;
        if (room->obstaclesGrid[static_cast<size_t>(t)] == 0 && far) shrineTile = t;
    }
    if (shrineTile != -1) shrineCell = best;
}

} // namespace game
