#pragma once
// Room-design selection from DungeonGenerator.cs: GetRandomRoom (both overloads),
// GetContiguousRooms and GetMinibossRoom, plus the per-cave-room choice made in
// GenerateDungeonCoroutine's room loop (which list a room is drawn from and with which
// doors). Room *filling* is separate.

#include <string>

#include "cs/Dictionary.h"
#include "cs/List.h"
#include "game/Cave.h"
#include "game/DungeonLayout.h"
#include "game/RoomDatabase.h"

namespace game {

// The parts of a placed DungeonRoom that selection reads.
class PlacedRoom : public cs::Object {
public:
    int x = 0;
    int y = 0;
    DungeonRoomInfo* roomInfo = nullptr;
};

class RoomSelector {
public:
    RoomSelector(DungeonRoomDatabase* db, Culture culture, int level);

    DungeonRoomDatabase* roomDatabase;
    Culture culture;
    int level;
    cs::List<PlacedRoom*>* _roomsGenerated = cs::gcnew<cs::List<PlacedRoom*>>();
    cs::Dictionary<std::string, int>* roomNames = cs::gcnew<cs::Dictionary<std::string, int>>();

    DungeonRoomInfo* GetRandomRoom(cs::List<DungeonRoomInfo*>* rooms, cs::List<DungeonRoomInfo::Doors>* desiredDoorIndex,
                                   cs::List<DungeonRoomInfo*>* ignore = nullptr);
    DungeonRoomInfo* GetRandomRoom(cs::List<DungeonRoomInfo*>* rooms, int desiredDoorIndex);
    cs::List<DungeonRoomInfo*>* GetContiguousRooms(int x, int y);
    // `dlcOverride` stands in for IDLCControllerOverrideRoom (Wanderer DLC); nullptr = none.
    DungeonRoomInfo* GetMinibossRoom(DungeonRoomInfo* dlcOverride = nullptr);
};

} // namespace game
