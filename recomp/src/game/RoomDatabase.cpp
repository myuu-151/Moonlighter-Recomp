#include "game/RoomDatabase.h"

#include "util/Json.h"

namespace game {

namespace {

// JsonUtility-style field readers: leave the target untouched when the key is absent.
void Read(const json::Value& o, const char* k, float& out)
{
    if (const json::Value* v = o.Find(k)) out = static_cast<float>(v->AsDouble());
}
void Read(const json::Value& o, const char* k, int& out)
{
    if (const json::Value* v = o.Find(k)) out = static_cast<int>(v->AsInt());
}
void Read(const json::Value& o, const char* k, bool& out)
{
    if (const json::Value* v = o.Find(k)) out = v->kind == json::Value::Kind::Bool ? v->b : v->AsInt() != 0;
}
void Read(const json::Value& o, const char* k, std::string& out)
{
    if (const json::Value* v = o.Find(k)) out = v->s;
}

cs::List<std::string>* StringList(const json::Value* v)
{
    auto* l = cs::gcnew<cs::List<std::string>>();
    if (v)
        for (const json::Value& s : v->arr) l->Add(s.s);
    return l;
}

PrefabSpawnInfo* PrefabInfoFrom(const json::Value& v)
{
    auto* p = cs::gcnew<PrefabSpawnInfo>();
    Read(v, "name", p->name);
    Read(v, "quantity", p->quantity);
    return p;
}

} // namespace

DungeonRoomInfo::DungeonRoomInfo()
{
    obstaclesSpawnPacks = cs::gcnew<cs::List<ObstaclePack*>>();
    obstaclesSpawnPacks->Add(cs::gcnew<ObstaclePack>());
    enemiesSpawnPacks = cs::gcnew<cs::List<EnemyPack*>>();
    enemiesSpawnPacks->Add(cs::gcnew<EnemyPack>());
    prefabSpawnPacks = cs::gcnew<cs::List<PrefabSpawn*>>();
    detailGenerationInfo = cs::gcnew<DungeonRoomDetailsGenerationInfo>();
    detailGenerationInfo->middleDetailZones = cs::gcnew<cs::List<unity::Rect>>();
}

DungeonRoomInfo* DungeonRoomInfo::FromJson(const json::Value& v)
{
    auto* r = cs::gcnew<DungeonRoomInfo>();
    int culture = static_cast<int>(r->culture);
    Read(v, "culture", culture);
    r->culture = static_cast<Culture>(culture);
    if (const json::Value* f = v.Find("floors")) {
        Read(*f, "x", r->floors.x);
        Read(*f, "y", r->floors.y);
    }
    Read(v, "name", r->name);
    Read(v, "weight", r->weight);
    if (const json::Value* d = v.Find("availableDoors")) {
        r->availableDoors = cs::gcnew<cs::List<Doors>>();
        for (const json::Value& x : d->arr) r->availableDoors->Add(static_cast<Doors>(x.AsInt()));
    }
    if (const json::Value* g = v.Find("detailGenerationInfo")) {
        DungeonRoomDetailsGenerationInfo* di = r->detailGenerationInfo;
        Read(*g, "wallDetailGenerationProb", di->wallDetailGenerationProb);
        Read(*g, "breakablesPerZone", di->breakablesPerZone);
        Read(*g, "breakablesProbability", di->breakablesProbability);
        Read(*g, "creaturesPerZone", di->creaturesPerZone);
        Read(*g, "creaturesDetailGenerationProb", di->creaturesDetailGenerationProb);
        Read(*g, "particlesDetailGenerationProb", di->particlesDetailGenerationProb);
        Read(*g, "middleDetailsPerZone", di->middleDetailsPerZone);
        Read(*g, "middleDetailGenerationProb", di->middleDetailGenerationProb);
        if (const json::Value* zones = g->Find("middleDetailZones")) {
            di->middleDetailZones = cs::gcnew<cs::List<unity::Rect>>();
            for (const json::Value& z : zones->arr) {
                unity::Rect rc;
                Read(z, "x", rc.x);
                Read(z, "y", rc.y);
                Read(z, "width", rc.width);
                Read(z, "height", rc.height);
                di->middleDetailZones->Add(rc);
            }
        }
    }
    Read(v, "canContainChampion", r->canContainChampion);
    Read(v, "isTimedRoom", r->isTimedRoom);
    Read(v, "isOnLevel1", r->isOnLevel1);
    Read(v, "isOnLevel2", r->isOnLevel2);
    Read(v, "isOnLevel3", r->isOnLevel3);
    if (const json::Value* packs = v.Find("obstaclesSpawnPacks")) {
        r->obstaclesSpawnPacks = cs::gcnew<cs::List<ObstaclePack*>>();
        for (const json::Value& p : packs->arr) {
            auto* pack = cs::gcnew<ObstaclePack>();
            if (const json::Value* sp = p.Find("spawns"))
                for (const json::Value& s : sp->arr) {
                    auto* t = cs::gcnew<TileSpawn>();
                    Read(s, "index", t->index);
                    int tile = static_cast<int>(t->tile);
                    Read(s, "tile", tile);
                    t->tile = static_cast<DungeonTile>(tile);
                    pack->spawns->Add(t);
                }
            r->obstaclesSpawnPacks->Add(pack);
        }
    }
    if (const json::Value* packs = v.Find("enemiesSpawnPacks")) {
        r->enemiesSpawnPacks = cs::gcnew<cs::List<EnemyPack*>>();
        for (const json::Value& p : packs->arr) {
            auto* pack = cs::gcnew<EnemyPack>();
            if (const json::Value* sp = p.Find("spawns"))
                for (const json::Value& s : sp->arr) {
                    auto* e = cs::gcnew<EnemySpawn>();
                    Read(s, "index", e->index);
                    e->enemy = s.Find("enemy") ? PrefabInfoFrom(s["enemy"]) : cs::gcnew<PrefabSpawnInfo>();
                    pack->spawns->Add(e);
                }
            r->enemiesSpawnPacks->Add(pack);
        }
    }
    if (const json::Value* packs = v.Find("prefabSpawnPacks")) {
        r->prefabSpawnPacks = cs::gcnew<cs::List<PrefabSpawn*>>();
        for (const json::Value& p : packs->arr) {
            auto* ps = cs::gcnew<PrefabSpawn>();
            if (const json::Value* pos = p.Find("position")) {
                Read(*pos, "x", ps->position.x);
                Read(*pos, "y", ps->position.y);
            }
            if (const json::Value* pf = p.Find("prefab")) ps->prefab = PrefabInfoFrom(*pf);
            r->prefabSpawnPacks->Add(ps);
        }
    }
    return r;
}

void DungeonRoomDatabase::LoadSpecialRooms(const json::Value& d)
{
    auto special = [&](const char* k) -> DungeonRoomInfo* {
        const json::Value* v = d.Find(k);
        return v ? DungeonRoomInfo::FromJson(*v) : cs::gcnew<DungeonRoomInfo>();
    };
    campRoomInfo = special("campRoomInfo");
    initRoomInfo = special("initRoomInfo");
    healingRoomInfo = special("healingRoomInfo");
    waveRoomInfo = special("waveRoomInfo");
    sendToHomeRoomInfo = special("sendToHomeRoomInfo");
    testRoomInfo = special("testRoomInfo");
    wandererNestRoomInfo = special("wandererNestRoomInfo");
    abandonedItemsRoomInfo = special("abandonedItemsRoomInfo");
    specialForestRoomInfo = special("specialForestRoomInfo");
}

void DungeonRoomDatabase::LoadFromJSON(const json::Value& t)
{
    // FromJsonOverwrite on the struct: lists are replaced by freshly built objects.
    if (const json::Value* rooms = t.Find("rooms")) {
        database.rooms = cs::gcnew<cs::List<DungeonRoomInfo*>>();
        for (const json::Value& r : rooms->arr) database.rooms->Add(DungeonRoomInfo::FromJson(r));
    }
    if (t.Find("GolemCorruptedRooms")) database.GolemCorruptedRooms = StringList(t.Find("GolemCorruptedRooms"));
    if (t.Find("ForestCorruptedRooms")) database.ForestCorruptedRooms = StringList(t.Find("ForestCorruptedRooms"));
    if (t.Find("DesertCorruptedRooms")) database.DesertCorruptedRooms = StringList(t.Find("DesertCorruptedRooms"));
    if (t.Find("TeslaCorruptedRooms")) database.TeslaCorruptedRooms = StringList(t.Find("TeslaCorruptedRooms"));
    Read(t, "GolemMinibossRoomName", database.GolemMinibossRoomName);
    Read(t, "CorruptedGolemMinibossRoomName", database.CorruptedGolemMinibossRoomName);
    Read(t, "ForestMinibossRoomName", database.ForestMinibossRoomName);
    Read(t, "CorruptedForestMinibossRoomName", database.CorruptedForestMinibossRoomName);
    Read(t, "DesertMinibossRoomName", database.DesertMinibossRoomName);
    Read(t, "CorruptedDesertMinibossRoomName", database.CorruptedDesertMinibossRoomName);
    Read(t, "TeslaMinibossRoomName", database.TeslaMinibossRoomName);
    Read(t, "CorruptedTeslaMinibossRoomName", database.CorruptedTeslaMinibossRoomName);
}

namespace {
bool OnLevel(const DungeonRoomInfo* x, int level)
{
    return level == 2 ? x->isOnLevel3 : (level == 1 ? x->isOnLevel2 : x->isOnLevel1);
}

bool HasChestTile(const DungeonRoomInfo* x)
{
    cs::List<DungeonRoomInfo::TileSpawn*>* spawns = (*x->obstaclesSpawnPacks)[0]->spawns;
    return spawns->FindIndex([](DungeonRoomInfo::TileSpawn* y) { return y->tile == DungeonRoomInfo::DungeonTile::Chest; }) != -1;
}
} // namespace

cs::List<DungeonRoomInfo*>* DungeonRoomDatabase::GetRoomsByCulture(Culture culture, int level)
{
    auto* l = cs::gcnew<cs::List<DungeonRoomInfo*>>();
    for (DungeonRoomInfo* x : *database.rooms)
        if (x->culture == culture && OnLevel(x, level) && !x->isTimedRoom && !HasChestTile(x)) l->Add(x);
    return l;
}

cs::List<DungeonRoomInfo*>* DungeonRoomDatabase::GetTimedChestRoomsByCulture(Culture culture, int level)
{
    auto* l = cs::gcnew<cs::List<DungeonRoomInfo*>>();
    for (DungeonRoomInfo* x : *database.rooms)
        if (x->culture == culture && OnLevel(x, level) && x->isTimedRoom) l->Add(x);
    return l;
}

cs::List<DungeonRoomInfo*>* DungeonRoomDatabase::GetChestRoomsByCulture(Culture culture, int level)
{
    auto* l = cs::gcnew<cs::List<DungeonRoomInfo*>>();
    for (DungeonRoomInfo* x : *database.rooms)
        if (x->culture == culture && OnLevel(x, level) && HasChestTile(x)) l->Add(x);
    return l;
}

DungeonRoomInfo* DungeonRoomDatabase::FindRoom(Culture culture, const std::string& name)
{
    for (DungeonRoomInfo* x : *database.rooms)
        if (x->culture == culture && x->name == name) return x;
    return nullptr;
}

void DungeonRoomDatabase::UpdateCorruptedRooms()
{
    // Reproduces the game: each culture's names land in the *previous* culture's list,
    // and Tesla gets Golem's + (Forest's) + (Desert's).
    auto* list = cs::gcnew<cs::List<DungeonRoomInfo*>>();
    GolemCorruptedRooms = cs::gcnew<cs::List<DungeonRoomInfo*>>();
    ForestCorruptedRooms = cs::gcnew<cs::List<DungeonRoomInfo*>>();
    DesertCorruptedRooms = cs::gcnew<cs::List<DungeonRoomInfo*>>();
    TeslaCorruptedRooms = cs::gcnew<cs::List<DungeonRoomInfo*>>();
    for (const std::string& n : *database.GolemCorruptedRooms)
        if (DungeonRoomInfo* r = FindRoom(Culture::Golem, n)) list->Add(r);
    for (const std::string& n : *database.ForestCorruptedRooms)
        if (DungeonRoomInfo* r = FindRoom(Culture::Forest, n)) GolemCorruptedRooms->Add(r);
    for (const std::string& n : *database.DesertCorruptedRooms)
        if (DungeonRoomInfo* r = FindRoom(Culture::Desert, n)) ForestCorruptedRooms->Add(r);
    for (const std::string& n : *database.TeslaCorruptedRooms)
        if (DungeonRoomInfo* r = FindRoom(Culture::Tech, n)) DesertCorruptedRooms->Add(r);
    TeslaCorruptedRooms->AddRange(list);
    TeslaCorruptedRooms->AddRange(GolemCorruptedRooms);
    TeslaCorruptedRooms->AddRange(ForestCorruptedRooms);
}

cs::List<DungeonRoomInfo*>* DungeonRoomDatabase::GetCorruptedRooms(Culture culture)
{
    UpdateCorruptedRooms();
    switch (culture) {
    case Culture::Golem: return GolemCorruptedRooms;
    case Culture::Forest: return ForestCorruptedRooms;
    case Culture::Desert: return DesertCorruptedRooms;
    case Culture::Tech: return TeslaCorruptedRooms;
    default: return nullptr;
    }
}

} // namespace game
