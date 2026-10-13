#pragma once
// DungeonGenerator.GenerateDungeonCoroutine for a normal floor, from the cave layout through
// the room loop to SpawnSpecialRoomsAfterGeneration: room selection, room placement and
// filling (PlaceRoom), secret rooms, the next-floor door and the boss room, in the game's
// order. Then, after frames of asset loading (GenerateSounds: draws there depend on frame
// timing), the Wanderer DLC's post-processing: WandererPostprocess.

#include <vector>

#include "game/BossRoomStart.h"
#include "game/DungeonLayout.h"
#include "game/RoomPlacement.h"
#include "game/RoomSelection.h"

namespace game {

class FloorEngine : public RoomEngine {
public:
    // A room is about to be placed (the main room of a cell, or its secret room).
    virtual void BeginRoom(DungeonRoomInfo* info, int x, int y, bool secret) = 0;
    // IDLCControllerOverrideRoom for the miniboss room (nullptr = none).
    virtual DungeonRoomInfo* MinibossOverride() = 0;
    // AddBossRoom on level 3: the boss room prefab is instantiated (its boss chest's Awake).
    virtual void BossRoomPlaced() = 0;
    // Save state: is this culture's boss already defeated (DungeonBossRoom.IsBossDefeated)?
    virtual bool BossDefeated() = 0;
    // After the boss's Start() frame (the port consumes its measured draw count): a hook for
    // checking/resyncing.
    virtual void BossStartFrame() = 0;
    // Around SpawnSpecialRoomsAfterGeneration (Desert hard-enemies shrine): hooks for checking.
    virtual void BeforeSpecialRooms() = 0;
    virtual void SpecialRoomsAfterGeneration() = 0;
    // Frames pass here: GenerateDungeonCoroutine yields to the bundles' loading before the
    // first room (and a corrupted room's culture's, after its pick) and `yield return new
    // WaitForEndOfFrame()` ends each room's iteration. Other scripts' Updates run in those
    // frames and can draw from UnityEngine.Random (none of the generation's own draws are
    // there): the engine's to run (by default nothing does).
    virtual void FramePasses() {}
};

struct PlacedCell {
    CaveRoom* cave = nullptr;
    int x = 0, y = 0;
    DungeonRoomInfo* info = nullptr;
    RoomFill* room = nullptr;
    DungeonRoomInfo* secretInfo = nullptr;
    RoomFill* secret = nullptr;
    bool wandererIntermediate = false; // `secret` is the Wanderer DLC's intermediate entrance
};

class DungeonFloor {
public:
    DungeonFloor(DungeonRoomDatabase* db, Culture culture, int level) : db_(db), culture_(culture), level_(level) {}

    // Runs from CaveGenerator.Init's RNG state. `floorInfo` is updated (next floor's door).
    void Generate(FloorGenerationSaveInfo& floorInfo, FloorEngine& engine);

    // WandererDLCController.DungeonGenerator_PostprocessDungeonEvent (DLC installed), on
    // floors 1-2: the holes rooms are shuffled by a sort whose comparison is a random number,
    // and the first one that isn't the initial room and has no secret room gets the
    // intermediate-dungeon entrance (`intermediate`, WND_INTERMEDIATE) as its secret room.
    // Returns the host's index in `cells`, or -1.
    int WandererPostprocess(DungeonRoomInfo* intermediate, FloorEngine& engine);
    // Just the shuffle and the host pick (draws only; no room placed).
    int PickWandererHost();

    DungeonLayout layout;
    std::vector<PlacedCell> cells;
    int nextLevelDoor = -1; // floorGenerationSaveInfo.lastDoorDirectionIndex chosen by AddNextLevelDoor
    // The boss room's Start() picks (a defeated boss's room only).
    std::vector<StartPick> bossStart;
    // Desert hard-enemies shrine: the room (index into `cells`) and tile, or -1.
    int shrineCell = -1, shrineTile = -1;

private:
    void SpawnSpecialRoomsAfterGeneration(const FloorGenerationSaveInfo& fi, CaveRoom* bossEnterRoom);

    void AddSecretRoom(PlacedCell& host, const char* advisor);

    DungeonRoomDatabase* db_;
    Culture culture_;
    int level_;
    FloorRoomState floor_;
    CaveRoom* initCaveRoom_ = nullptr;
};

} // namespace game
