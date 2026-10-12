#pragma once
// A run of dungeon floors without the game around it: the data the generator needs, read
// through a DataSource (files on the PC, the disc on the GameCube), and floors generated one
// after another from one RNG stream, as the game does from a save's seed.
//
// The generator's engine side is answered from the extracted data (StandaloneEngine). Where
// the game would consult objects the data doesn't describe yet (chest prefabs' drop tables,
// the breakables FindObjectsOfType sees, chest contents), it answers neutrally: the floors are
// the same kind of floor, but past such a point not draw-for-draw the game's.

#include <map>
#include <memory>
#include <string>

#include "cs/Object.h"
#include "game/Composite.h"
#include "game/DungeonFloor.h"
#include "game/RoomDetails.h"

namespace game {

class DataSource {
public:
    virtual ~DataSource() = default;
    // A data file's contents ("constants.json", "packs/golem/rooms.json", ...); throws if missing.
    virtual std::string Read(const std::string& path) = 0;
};

class StandaloneEngine : public FloorEngine {
public:
    explicit StandaloneEngine(const std::map<std::pair<int, int>, DetailsData>& details) : details_(details) {}

    ChestConfig NextChest(bool& ngp) override;
    void RandomChestContents(const ChestConfig&) override {}
    int BreakablesInScene() override { return 0; }
    int SkeletonSkins() override { return 0; }
    DetailsRoom RoomPrefab(Culture culture, Culture floorCulture) override;
    const DetailsData* Details(Culture culture, int level) override;
    int ChestsInPrefabs(DungeonRoomInfo*) override { return 0; }

    void BeginRoom(DungeonRoomInfo*, int, int, bool) override {}
    DungeonRoomInfo* MinibossOverride() override { return nullptr; }
    void BossRoomPlaced() override {}
    bool BossDefeated() override { return bossDefeated; }
    void BossStartFrame() override {}
    void BeforeSpecialRooms() override {}
    void SpecialRoomsAfterGeneration() override {}

    bool bossDefeated = false;

private:
    const std::map<std::pair<int, int>, DetailsData>& details_;
};

// One generated floor and everything it owns.
struct Floor {
    cs::Heap heap;
    Culture culture = Culture::Golem;
    int level = 0;
    DungeonRoomDatabase* db = nullptr;
    std::unique_ptr<DungeonFloor> dungeon;
};

class Session {
public:
    explicit Session(DataSource& data) : data_(data) {}

    // Constants, generation data, details registers, the enemy prefab index. Once.
    void Load();
    // Unity's Random.InitState(seed) and a fresh run (floor info reset on the first floor).
    void Start(int seed);
    // The next floor of `culture` at `level` (0..2): its room database and prefabs are loaded
    // for it. The previous floor (and its heap) must be gone first.
    std::unique_ptr<Floor> Generate(Culture culture, int level, bool bossDefeated = false);

    static const char* PackName(Culture c); // "golem", "forest", "desert", "tech"
    // The tiles a room of `c` paints with on a floor of `level`: its holes and that floor's
    // details (tiles/<c>/holes.bin, details<level>.bin), into `out` (cleared first).
    static void LoadTiles(DataSource& data, Culture c, int level, TileTextures& out);

private:
    DataSource& data_;
    std::map<std::pair<int, int>, DetailsData> details_;
    std::string generationText_;
    FloorGenerationSaveInfo info_;
    bool started_ = false;
};

} // namespace game
