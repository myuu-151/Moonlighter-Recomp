#include "game/Session.h"

#include "game/Constants.h"
#include "game/PrefabCatalog.h"
#include "game/Prefabs.h"
#include "game/RoomGenerator.h"
#include "game/RoomScene.h"
#include "unity/Random.h"

namespace game {

ChestConfig StandaloneEngine::NextChest(bool& ngp)
{
    ngp = false;
    return ChestConfig(); // the prefab's drop table isn't extracted yet: no fixed drops
}

DetailsRoom StandaloneEngine::RoomPrefab(Culture, Culture)
{
    DetailsRoom r; // the DungeonRoom prefab: 12 wall spawners, 484 x 268 floor
    r.spawners = kDetailSpawnerCount;
    r.roomFloorWidth = kRoomFloorWidth;
    r.roomFloorHeight = kRoomFloorHeight;
    return r;
}

const DetailsData* StandaloneEngine::Details(Culture culture, int level)
{
    auto it = details_.find({static_cast<int>(culture), level});
    return it == details_.end() ? nullptr : &it->second;
}

namespace {

// The heap that outlives floors: the generator's per-culture data (static in the game).
cs::Heap& SessionHeap()
{
    static cs::Heap heap;
    return heap;
}

json::Value& Generation()
{
    static json::Value value;
    return value;
}

void FreshGenerationData(Culture c)
{
    const json::Value* entry = Generation().Find(RoomFill::CultureName(c));
    cs::Heap* previous = &cs::CurrentHeap();
    cs::SetCurrentHeap(&SessionHeap());
    RoomGenerator::generationDataByCulture[static_cast<int>(c)] = entry ? RoomGenerationData::FromExtracted(*entry) : nullptr;
    cs::SetCurrentHeap(previous);
}

} // namespace

const char* Session::PackName(Culture c)
{
    switch (c) {
    case Culture::Golem: return "golem";
    case Culture::Forest: return "forest";
    case Culture::Desert: return "desert";
    case Culture::Tech: return "tech";
    default: return "golem";
    }
}

void Session::Load()
{
    Constants::LoadFromText(data_.Read("constants.json"));
    details_ = LoadDetailsData(json::Parse(data_.Read("details.json")));
    Generation() = json::Parse(data_.Read("generation_data_counts.json"));
    PrefabCatalog::Load(json::Parse(data_.Read("bundle_assets.json")));
}

void Session::Start(int seed)
{
    unity::Random::InitState(seed);
    // DungeonRoomGenerator.generationDataByCulture: fresh for every culture at the start.
    for (int c = 1; c <= 5; c++) FreshGenerationData(static_cast<Culture>(c));
    info_ = FloorGenerationSaveInfo();
    started_ = true;
}

std::unique_ptr<Floor> Session::Generate(Culture culture, int level, bool bossDefeated)
{
    if (!started_) Start(0);
    auto floor = std::make_unique<Floor>();
    floor->culture = culture;
    floor->level = level;
    cs::SetCurrentHeap(&floor->heap);

    const std::string pack = std::string("packs/") + PackName(culture) + "/";
    {
        json::Value rooms = json::Parse(data_.Read(pack + "rooms.json"));
        floor->db = cs::gcnew<DungeonRoomDatabase>();
        floor->db->LoadSpecialRooms(rooms);
        floor->db->LoadFromJSON(rooms["database"]);
    }
    PrefabLibrary::Load(json::Parse(data_.Read(pack + "prefabs.json")));
    // The floor transition reloads only the floor's own culture's data (UnloadBundlesExcept's
    // inverted test, see RoomGenerator.h).
    FreshGenerationData(culture);
    if (level == 0) info_.ResetValues();

    StandaloneEngine engine(details_);
    engine.bossDefeated = bossDefeated;
    floor->dungeon = std::make_unique<DungeonFloor>(floor->db, culture, level);
    floor->dungeon->Generate(info_, engine);
    cs::SetCurrentHeap(nullptr); // nothing may land in the floor's heap after it's gone
    return floor;
}

} // namespace game
