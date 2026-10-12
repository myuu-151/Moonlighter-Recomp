#include "game/RoomScene.h"

#include "game/Constants.h"
#include "game/Prefabs.h"

#include <algorithm>

namespace game {

const unity::Vector2 kDoorBoundaries[4] = {{0.0f, 159.0f}, {0.0f, -159.0f}, {-262.0f, 0.0f}, {262.0f, 0.0f}};
const unity::Vector2 kDetailSpawners[kDetailSpawnerCount] = {
    {170.0f, 157.0f},  {100.0f, 157.0f},  {-100.0f, 157.0f},  {-170.0f, 157.0f}, {-265.0f, 75.0f}, {-265.0f, -75.0f},
    {-170.0f, -157.0f}, {-100.0f, -157.0f}, {100.0f, -157.0f}, {170.0f, -157.0f}, {265.0f, 75.0f},  {265.0f, -75.0f},
};

namespace {

// Instantiate(prefab, local + room position) then `transform.parent = room`: the local
// position Unity keeps is the world position less the room's, both rounded to float.
unity::Vector2 Reparented(unity::Vector2 local, unity::Vector2 room)
{
    float wx = local.x + room.x, wy = local.y + room.y;
    return {wx - room.x, wy - room.y};
}

const std::string& NameAt(const std::vector<std::string>& names, int index)
{
    static const std::string none;
    return index >= 0 && index < static_cast<int>(names.size()) ? names[static_cast<size_t>(index)] : none;
}

} // namespace

unity::Vector2 TranslateTileToPosition(int tile, int gridSlicesX)
{
    float num = Constants::GetFloat("tileSize");
    unity::Vector2 v;
    v.x = (static_cast<float>(tile % gridSlicesX) + 0.5f) * num - static_cast<float>(kRoomFloorWidth) * 0.5f + 2.0f;
    v.y = (static_cast<float>(tile / gridSlicesX) + 0.5f) * num - static_cast<float>(kRoomFloorHeight) * 0.5f + static_cast<float>(kRoomCenterYOffset) + 2.0f;
    return v;
}

std::vector<SceneObject> RoomSceneObjects(const RoomFill& room, unity::Vector2 roomPosition)
{
    std::vector<SceneObject> out;
    DungeonRoomInfo* info = room.roomInfo;

    // SpawnEnemies -> CreateEnemyPrefab: an "EnemySpawn" at the tile, the enemy inside it.
    for (const auto& [tile, prefab] : room.enemies) {
        unity::Vector2 p = TranslateTileToPosition(tile, room.gridSlicesX);
        out.push_back({"DungeonLevelEnemies/EnemySpawn", p.x, p.y});
        out.push_back({"DungeonLevelEnemies/EnemySpawn/" + prefab + "(Clone)", p.x, p.y});
    }

    // SpawnChests: chests at their tile; mimics move into a new "EnemySpawn" there.
    for (const auto& [tile, name] : room.chests) {
        unity::Vector2 p = TranslateTileToPosition(tile, room.gridSlicesX);
        if (name.find("Mimic") != std::string::npos) {
            out.push_back({"DungeonLevelEnemies/EnemySpawn", p.x, p.y});
            out.push_back({"DungeonLevelEnemies/EnemySpawn/" + name + "(Clone)", p.x, p.y});
        } else {
            out.push_back({name + "(Clone)", p.x, p.y});
        }
    }

    // GenerateRockObjects: "<culture>Rock" on every rock tile (4), column by column.
    if (!room.obstaclesGrid.empty()) {
        int num = Constants::GetInt("tileSize");
        int num2 = -((kRoomFloorWidth - kRoomFloorWidth % num - num) / 2);
        int num3 = -((kRoomFloorHeight - kRoomFloorHeight % num - num) / 2);
        std::string rock = std::string("DungeonLevelRoomRocks/") + RoomFill::CultureName(info->culture) + "Rock(Clone)";
        for (int i = 0; i < room.gridSlicesX; i++)
            for (int j = 0; j < room.gridSlicesY; j++)
                if (room.obstaclesGrid[static_cast<size_t>(i + j * room.gridSlicesX)] == 4)
                    out.push_back({rock, static_cast<float>(num2 + i * num), static_cast<float>(num3 + j * num)});
    }

    // The room's art: AddLightShadow's light and shadow, SetBackgroundImages' floor and walls.
    if (room.art.set) {
        out.push_back({"DungeonLevelArt/room", 0.0f, 0.0f, room.art.background});
        out.push_back({"DungeonLevelArt/walls", 0.0f, 0.0f, room.art.walls});
        out.push_back({"DungeonLevelArt/Lights", 0.0f, 0.0f, room.art.light});
        out.push_back({"DungeonLevelArt/Shadows", 0.0f, 0.0f, room.art.shadow});
    }

    // SpawnDetails (needs the register's prefab names).
    if (const DetailsData* d = room.detailsData) {
        const DetailsResult& r = room.details;
        // Wall details: into spawner i, local position reset to zero.
        if (room.detailSpawners == kDetailSpawnerCount)
            for (const auto& [spawner, pick] : r.wallPicks) {
                const std::string& name = NameAt(d->wallNames, pick);
                if (name.empty() || spawner >= kDetailSpawnerCount) continue;
                unity::Vector2 p = kDetailSpawners[spawner];
                out.push_back({"Details/WallDetails/DetailSpawn/" + name + "(Clone)", p.x, p.y});
            }
        // Particles: at the room's origin.
        if (r.particlePick >= 0 && !NameAt(d->particleNames, r.particlePick).empty())
            out.push_back({NameAt(d->particleNames, r.particlePick) + "(Clone)", 0.0f, 0.0f});
        for (const auto& b : r.breakablePicks) {
            if (NameAt(d->breakableNames, b.index).empty()) continue;
            unity::Vector2 p = Reparented({b.x, b.y}, roomPosition);
            out.push_back({NameAt(d->breakableNames, b.index) + "(Clone)", p.x, p.y});
        }
        for (const auto& c : r.creaturePicks) {
            if (NameAt(d->creatureNames, c.index).empty()) continue;
            unity::Vector2 p = Reparented({c.x, c.y}, roomPosition);
            out.push_back({NameAt(d->creatureNames, c.index) + "(Clone)", p.x, p.y});
        }
    }
    return out;
}

const char* CultureBundle(Culture c)
{
    switch (c) {
    case Culture::Golem: return "golem";
    case Culture::Forest: return "forest";
    case Culture::Desert: return "desert";
    case Culture::Tech: return "tesla";
    default: return "unknown";
    }
}

namespace {

SceneRow RowFrom(const PrefabNode& n, const std::string& path)
{
    SceneRow r;
    r.path = path;
    r.active = n.active;
    r.x = n.x;
    r.y = n.y;
    r.sx = n.sx;
    r.sy = n.sy;
    r.rotation = n.rotation;
    r.sprite = n.sprite;
    r.layer = n.layer;
    r.order = n.order;
    r.flipX = n.flipX;
    r.flipY = n.flipY;
    r.rendererEnabled = n.rendererEnabled;
    for (int c = 0; c < 4; c++) r.color[c] = n.color[c];
    return r;
}

// Instantiate(prefab) as a child of `parent` ("" = the room) at a local position: the
// clone's rows. False if there's no such prefab.
bool Instantiate(std::vector<SceneRow>& out, const std::string& parent, const std::string& name, const std::string& bundle, float x,
                 float y)
{
    const Prefab* p = PrefabLibrary::Find(name, bundle);
    if (!p) return false;
    std::string root = (parent.empty() ? "" : parent + "/") + p->name + "(Clone)";
    for (const PrefabNode& n : p->nodes) {
        SceneRow r = RowFrom(n, n.path.empty() ? root : root + "/" + n.path);
        if (n.path.empty()) {
            r.x = x;
            r.y = y;
        }
        out.push_back(r);
    }
    return true;
}

// new GameObject(name) under the room, at (0, 0).
void Container(std::vector<SceneRow>& out, const std::string& path)
{
    SceneRow r;
    r.path = path;
    out.push_back(r);
}

SceneRow* FindRow(std::vector<SceneRow>& rows, const std::string& path)
{
    for (SceneRow& r : rows)
        if (r.path == path) return &r;
    return nullptr;
}

} // namespace

std::vector<SceneRow> RoomSceneTree(const RoomFill& room, unity::Vector2 roomPosition)
{
    std::vector<SceneRow> out;
    DungeonRoomInfo* info = room.roomInfo;
    const std::string culture = CultureBundle(info->culture);
    const std::string dungeonBundle = "dungeon_" + culture, enemiesBundle = "enemies_" + culture;

    // The room prefab's own objects (not its root: that's the room).
    if (const Prefab* p = PrefabLibrary::Find("DungeonRoom", "dungeon_common"))
        for (const PrefabNode& n : p->nodes)
            if (!n.path.empty()) out.push_back(RowFrom(n, n.path));

    static const char* const kDoors[4] = {"DungeonLevelDoors/LevelDoorUp", "DungeonLevelDoors/LevelDoorDown",
                                          "DungeonLevelDoors/LevelDoorLeft", "DungeonLevelDoors/LevelDoorRight"};
    auto removeDoor = [&](const std::string& door) {
        out.erase(std::remove_if(out.begin(), out.end(), [&](const SceneRow& r) { return r.path == door || r.path.rfind(door + "/", 0) == 0; }),
                  out.end());
    };

    // InitializeRoom: doors the design doesn't have are destroyed.
    static const DungeonRoomInfo::Doors kSides[4] = {DungeonRoomInfo::Doors::North, DungeonRoomInfo::Doors::South,
                                                     DungeonRoomInfo::Doors::West, DungeonRoomInfo::Doors::East};
    for (int i = 0; i < 4; i++)
        if (!info->availableDoors || info->availableDoors->IndexOf(kSides[i]) == -1) removeDoor(kDoors[i]);

    // Special doors: the door's objects go, the special door takes its place and rotation
    // (ReplaceSpecialDoor / AddSpecialInitDoor).
    for (const RoomFill::SpecialDoor& sd : room.specialDoors) {
        if (sd.index < 0 || sd.index > 3) continue;
        const std::string door = kDoors[sd.index];
        SceneRow* d = FindRow(out, door);
        if (!d) continue;
        float x = d->x, y = d->y, rotation = d->rotation;
        removeDoor(door);
        size_t first = out.size();
        if (Instantiate(out, "DungeonLevelDoors", sd.prefab, sd.bundle, x, y)) out[first].rotation = rotation;
    }

    // AddSecretRoom: the advisor (host room) and the rope (secret room).
    if (!room.advisorPrefab.empty() && room.advisorTile >= 0) {
        unity::Vector2 p = TranslateTileToPosition(room.advisorTile, room.gridSlicesX);
        Instantiate(out, "", room.advisorPrefab, room.advisorBundle, p.x, p.y);
    }
    if (!room.ropeBundle.empty())
        if (const Prefab* rope = PrefabLibrary::Find("Rope", room.ropeBundle))
            Instantiate(out, "", "Rope", room.ropeBundle, rope->nodes[0].x, rope->nodes[0].y);

    // AddLightShadow and SetBackgroundImages.
    if (room.art.set) {
        if (SceneRow* r = FindRow(out, "DungeonLevelArt/room")) r->sprite = room.art.background;
        if (SceneRow* r = FindRow(out, "DungeonLevelArt/walls")) r->sprite = room.art.walls;
        if (SceneRow* r = FindRow(out, "DungeonLevelArt/Lights")) r->sprite = room.art.light;
        if (SceneRow* r = FindRow(out, "DungeonLevelArt/Shadows")) {
            r->sprite = room.art.shadow;
            r->sx = static_cast<float>(room.art.shadowFlipX);
            r->sy = static_cast<float>(room.art.shadowFlipY);
        }
    }

    // SpawnEnemies: the container, and each enemy in an "EnemySpawn" at its tile.
    Container(out, "DungeonLevelEnemies");
    auto spawn = [&](int tile, const std::string& prefab) {
        unity::Vector2 p = TranslateTileToPosition(tile, room.gridSlicesX);
        SceneRow s;
        s.path = "DungeonLevelEnemies/EnemySpawn";
        s.x = p.x;
        s.y = p.y;
        out.push_back(s);
        if (!Instantiate(out, "DungeonLevelEnemies/EnemySpawn", prefab, enemiesBundle, 0.0f, 0.0f))
            Instantiate(out, "DungeonLevelEnemies/EnemySpawn", prefab, "", 0.0f, 0.0f);
    };
    for (const auto& [tile, prefab] : room.enemies) spawn(tile, prefab);

    // SpawnChests: chests at their tile; mimics into an EnemySpawn there.
    for (const auto& [tile, name] : room.chests) {
        if (name.find("Mimic") != std::string::npos) {
            spawn(tile, name);
            continue;
        }
        unity::Vector2 p = TranslateTileToPosition(tile, room.gridSlicesX);
        Instantiate(out, "", name, "sharedassets0.assets", p.x, p.y);
    }

    if (!room.obstaclesGrid.empty()) {
        int num = Constants::GetInt("tileSize");
        int num2 = -((kRoomFloorWidth - kRoomFloorWidth % num - num) / 2);
        int num3 = -((kRoomFloorHeight - kRoomFloorHeight % num - num) / 2);
        auto tiles = [&](int value, const std::string& container, const std::string& prefab) {
            Container(out, container);
            for (int i = 0; i < room.gridSlicesX; i++)
                for (int j = 0; j < room.gridSlicesY; j++)
                    if (room.obstaclesGrid[static_cast<size_t>(i + j * room.gridSlicesX)] == value)
                        Instantiate(out, container, prefab, dungeonBundle, static_cast<float>(num2 + i * num), static_cast<float>(num3 + j * num));
        };
        tiles(4, "DungeonLevelRoomRocks", std::string(RoomFill::CultureName(info->culture)) + "Rock");
        Container(out, "DungeonLevelRoomHoles");
        Container(out, "DungeonLevelRoomSlime");
        tiles(8, "DungeonLevelRoomFAllingRocks", std::string(RoomFill::CultureName(info->culture)) + "FallingFloor");

        // SpawnRoomPrefabs: the design's own props, at their positions (ObstaclePrefabRegister:
        // the culture's obstacles, then the common ones).
        for (DungeonRoomInfo::PrefabSpawn* ps : *info->prefabSpawnPacks) {
            const std::string& name = ps->prefab->name;
            if (!Instantiate(out, "", name, dungeonBundle, ps->position.x, ps->position.y) &&
                !Instantiate(out, "", name, "dungeon_common", ps->position.x, ps->position.y))
                Instantiate(out, "", name, "", ps->position.x, ps->position.y);
        }
    }

    // SpawnDetails.
    if (const DetailsData* d = room.detailsData) {
        const DetailsResult& r = room.details;
        if (room.detailSpawners == kDetailSpawnerCount) {
            // Spawners share a name: their rows are told apart by an "@index" the test strips.
            const std::string spawner = "Details/WallDetails/DetailSpawn";
            for (const auto& [index, pick] : r.wallPicks) {
                const std::string& name = NameAt(d->wallNames, pick);
                if (name.empty() || index >= kDetailSpawnerCount) continue;
                std::vector<SceneRow> clone;
                Instantiate(clone, spawner, name, dungeonBundle, 0.0f, 0.0f);
                for (SceneRow& c : clone) c.path = spawner + "@" + std::to_string(index) + c.path.substr(spawner.size());
                out.insert(out.end(), clone.begin(), clone.end());
            }
        }
        if (r.particlePick >= 0) Instantiate(out, "", NameAt(d->particleNames, r.particlePick), dungeonBundle, 0.0f, 0.0f);
        for (const auto& b : r.breakablePicks) {
            unity::Vector2 p = Reparented({b.x, b.y}, roomPosition);
            Instantiate(out, "", NameAt(d->breakableNames, b.index), dungeonBundle, p.x, p.y);
        }
        for (const auto& c : r.creaturePicks) {
            unity::Vector2 p = Reparented({c.x, c.y}, roomPosition);
            Instantiate(out, "", NameAt(d->creatureNames, c.index), dungeonBundle, p.x, p.y);
        }
    }
    return out;
}

} // namespace game
