#include "game/RoomSelection.h"

#include "unity/Random.h"

namespace game {

namespace Random = unity::Random;
using Doors = DungeonRoomInfo::Doors;

RoomSelector::RoomSelector(DungeonRoomDatabase* db, Culture c, int lv) : roomDatabase(db), culture(c), level(lv) {}

DungeonRoomInfo* RoomSelector::GetRandomRoom(cs::List<DungeonRoomInfo*>* rooms, cs::List<Doors>* desiredDoorIndex,
                                             cs::List<DungeonRoomInfo*>* ignore)
{
    auto* list = cs::gcnew<cs::List<DungeonRoomInfo*>>();
    float num = 0.0f;
    for (int i = 0; i < rooms->Count(); i++) {
        bool flag = true;
        for (int j = 0; j < desiredDoorIndex->Count(); j++)
            if ((*rooms)[i]->availableDoors->IndexOf((*desiredDoorIndex)[j]) == -1) flag = false;
        if (flag && (ignore == nullptr || !ignore->Contains((*rooms)[i]))) {
            num += (*rooms)[i]->weight;
            list->Add((*rooms)[i]);
        }
    }
    float num2 = Random::Range(0.0f, num);
    DungeonRoomInfo* info = nullptr;
    for (int k = 0; k < list->Count(); k++) {
        info = (*list)[k];
        num2 -= info->weight;
        if (num2 <= 0.0f) break;
    }
    if (info != nullptr) info->weight *= 0.001f;
    cs::NullCheck(info); // the game reads info.name next, unconditionally
    roomNames->Set(info->name, 1); // (logs "REPEATED ROOM" if already present)
    return info;
}

DungeonRoomInfo* RoomSelector::GetRandomRoom(cs::List<DungeonRoomInfo*>* rooms, int desiredDoorIndex)
{
    Doors item = Doors::North;
    switch (desiredDoorIndex) {
    case 0: item = Doors::North; break;
    case 1: item = Doors::South; break;
    case 2: item = Doors::West; break;
    case 3: item = Doors::East; break;
    }
    auto* list = cs::gcnew<cs::List<DungeonRoomInfo*>>();
    float num = 0.0f;
    for (int i = 0; i < rooms->Count(); i++) {
        if ((*rooms)[i]->availableDoors->IndexOf(item) != -1) {
            num += (*rooms)[i]->weight;
            list->Add((*rooms)[i]);
        }
    }
    float num2 = Random::Range(0.0f, num);
    DungeonRoomInfo* info = nullptr;
    for (int j = 0; j < list->Count(); j++) {
        info = (*list)[j];
        num2 -= info->weight;
        if (num2 <= 0.0f) break;
    }
    cs::NullCheck(info)->weight *= 0.5f;
    return info;
}

cs::List<DungeonRoomInfo*>* RoomSelector::GetContiguousRooms(int x, int y)
{
    auto* list = cs::gcnew<cs::List<DungeonRoomInfo*>>();
    for (int i = 0; i < _roomsGenerated->Count(); i++) {
        PlacedRoom* r = (*_roomsGenerated)[i];
        if (((r->x + 1 == x || r->x - 1 == x) && r->y == y) || ((r->y + 1 == y || r->y - 1 == y) && r->x == x)) list->Add(r->roomInfo);
    }
    return list;
}

DungeonRoomInfo* RoomSelector::GetMinibossRoom(DungeonRoomInfo* dlcOverride)
{
    const DungeonRoomDatabase::RoomDatabase& d = roomDatabase->database;
    std::string roomName;
    switch (culture) {
    case Culture::Golem:
        if (level == 0) roomName = d.GolemMinibossRoomName;
        if (level == 1) roomName = d.CorruptedGolemMinibossRoomName;
        break;
    case Culture::Forest:
        if (level == 0) roomName = d.ForestMinibossRoomName;
        if (level == 1) roomName = d.CorruptedForestMinibossRoomName;
        break;
    case Culture::Desert:
        if (level == 0) roomName = d.DesertMinibossRoomName;
        if (level == 1) roomName = d.CorruptedDesertMinibossRoomName;
        break;
    case Culture::Tech:
        if (level == 0) roomName = d.TeslaMinibossRoomName;
        if (level == 1) roomName = d.CorruptedTeslaMinibossRoomName;
        break;
    default: break;
    }
    DungeonRoomInfo* info = dlcOverride;
    if (info == nullptr && !roomName.empty())
        for (DungeonRoomInfo* x : *d.rooms)
            if (x->name == roomName) {
                info = x;
                break;
            }
    return info;
}

} // namespace game
