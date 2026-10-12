// game/RoomDoors on its own, no data: rooms placed, connected, a dead end entered with
// enemies (CloseAllDoors), the camera's arrival and the enemies defeated (OpenAllDoors),
// against what DungeonRoom/DungeonDoor do (decomp: DungeonRoom.cs CloseAllDoors,
// OpenAllDoors, Connect*Room, DisableAllDoors, Enable/DisableRoomDoorsColliders;
// DungeonDoor.cs). The generation half is checked against the game in player_walk_test.
//
// Usage: room_doors_test

#include <cstdio>
#include <string>

#include "cs/Object.h"
#include "game/RoomDatabase.h"
#include "game/RoomDoors.h"
#include "game/RoomScene.h"

using game::DungeonRoomInfo;
using game::RoomDoors;
namespace Layer = game::Layer;

namespace {

int checks = 0, failures = 0;

void Check(bool ok, const std::string& what)
{
    checks++;
    if (!ok) {
        failures++;
        std::printf("FAIL %s\n", what.c_str());
    }
}

DungeonRoomInfo* Info(std::initializer_list<DungeonRoomInfo::Doors> doors)
{
    auto* info = cs::gcnew<DungeonRoomInfo>();
    info->availableDoors = cs::gcnew<cs::List<DungeonRoomInfo::Doors>>();
    for (DungeonRoomInfo::Doors d : doors) info->availableDoors->Add(d);
    return info;
}

} // namespace

int main()
{
    using D = DungeonRoomInfo::Doors;
    cs::Heap heap;
    cs::SetCurrentHeap(&heap);

    // A room without a west door, placed: no west door, its boundary on Default; the others
    // disabled with their boundaries on Default too (DisableAllDoors runs before RoomPlaced).
    RoomDoors a;
    a.Place(*Info({D::North, D::South, D::East}));
    Check(!a.doors[2].present && a.boundaryLayer[2] == Layer::Default, "InitializeRoom drops the west door");
    for (int i : {0, 1, 3}) {
        Check(a.doors[i].present && a.doors[i].disabled && !a.doors[i].colliderEnabled, "DisableAllDoors disables door " + std::to_string(i));
        Check(a.boundaryLayer[i] == Layer::Default && a.boundaryTag[i] == "LevelBoundary", "boundary " + std::to_string(i) + " on Default");
    }
    Check(a.doorClosedLayer == Layer::LevelBoundary && a.doorOpenedLayer == Layer::EnemyBoundary, "RoomPlaced's door layers");

    // Its east neighbour (every door): connected both ways, enabled, boundaries on EnemyBoundary,
    // each arrival point 35 into the other room in front of its door.
    RoomDoors b;
    b.Place(*Info({D::North, D::South, D::West, D::East}));
    const unity::Vector2 pa = game::RoomPosition(0, 0), pb = game::RoomPosition(1, 0);
    Check(RoomDoors::Connect(a, pa, 3, b, pb), "ConnectEastRoom");
    Check(!RoomDoors::Connect(a, pa, 3, b, pb), "a connected door doesn't connect again");
    Check(a.doors[3].connected && !a.doors[3].disabled && a.doors[3].colliderEnabled, "east door enabled");
    Check(b.doors[2].connected && !b.doors[2].disabled && b.doors[2].colliderEnabled, "neighbour's west door enabled");
    Check(a.boundaryLayer[3] == Layer::EnemyBoundary && b.boundaryLayer[2] == Layer::EnemyBoundary, "connected boundaries on EnemyBoundary");
    Check(a.doors[3].arrival.x == 640.0f - 256.0f + 35.0f && a.doors[3].arrival.y == 0.0f, "east door arrives right of the west door");
    Check(b.doors[2].arrival.x == 256.0f - 35.0f && b.doors[2].arrival.y == 0.0f, "west door arrives left of the east door");
    // West of `a` there's no door: no connection.
    RoomDoors c;
    c.Place(*Info({D::East}));
    Check(!RoomDoors::Connect(c, game::RoomPosition(-1, 0), 3, a, pa), "no connection into a missing door");
    Check(c.doors[3].disabled && c.boundaryLayer[3] == Layer::Default, "its door stays disabled");

    // `a` has one door to another room: a dead end. Entered with enemies, it closes: the
    // boundaries of every door there at once, the doors 0.3 s later (colliders stay).
    Check(a.IsDeadEnd(), "one connected door: dead end");
    Check(b.IsDeadEnd() && !b.CloseAllDoors(0), "b (one connection too) with no enemies: nothing closes");
    a.DisableRoomDoorsColliders(); // ChangeRoomTo
    Check(!a.doors[3].colliderEnabled, "door colliders off during the transition");
    Check(a.CloseAllDoors(3), "CloseAllDoors in a dead end with enemies");
    for (int i : {0, 1, 3}) Check(a.boundaryLayer[i] == Layer::LevelBoundary, "boundary " + std::to_string(i) + " closed at once");
    Check(a.doors[3].opened, "the door closes later");
    a.Update(0.25f);
    a.EnableRoomDoorsColliders(); // the camera arrives (0.5 s in the game; here while CloseDoor is pending)
    Check(!a.doors[3].colliderEnabled, "a door about to close keeps its collider off");
    a.Update(0.06f);
    Check(!a.doors[3].opened && a.doors[3].closeIn < 0.0f, "CloseDoor after 0.3 s");
    a.EnableRoomDoorsColliders();
    Check(!a.doors[3].colliderEnabled, "a closed door's collider stays off");

    // The enemies defeated: the connected door opens (collider on, boundary EnemyBoundary);
    // the unconnected doors' boundaries stay closed.
    a.OpenAllDoors();
    Check(a.doors[3].opened && a.doors[3].colliderEnabled && a.boundaryLayer[3] == Layer::EnemyBoundary, "OpenAllDoors opens the connected door");
    Check(a.boundaryLayer[0] == Layer::LevelBoundary && a.doors[0].disabled, "unconnected doors stay as they were");

    // Two connected doors and no chests: not a dead end, the doors stay open with enemies.
    RoomDoors d;
    d.Place(*Info({D::North, D::South, D::West, D::East}));
    RoomDoors n, s;
    n.Place(*Info({D::South}));
    s.Place(*Info({D::North}));
    RoomDoors::Connect(d, pa, 0, n, game::RoomPosition(0, 1));
    RoomDoors::Connect(d, pa, 1, s, game::RoomPosition(0, -1));
    Check(d.doors[0].arrival.y == 360.0f - 148.0f + 35.0f && d.doors[1].arrival.y == -360.0f + 148.0f - 35.0f, "north/south arrival points");
    Check(!d.IsDeadEnd() && !d.CloseAllDoors(4), "two doors: not a dead end, nothing closes");
    d.roomChests = 1;
    Check(d.IsDeadEnd() && d.CloseAllDoors(4), "a chest makes it a dead end");
    // Locked: closes whatever it is.
    RoomDoors e = b;
    e.SetLocked(true, 0, true);
    Check(e.boundaryLayer[2] == Layer::LevelBoundary && e.doors[2].closeIn > 0.0f, "isLocked = true closes");
    e.SetLocked(false, 0, true);
    Check(e.boundaryLayer[2] == Layer::EnemyBoundary, "unlocked with no enemies left: opens");

    // Special doors: the init room's door goes (its boundary as DisableAllDoors left it), the
    // next-floor door takes its slot's boundary to EnemyBoundary and closes/opens with the room.
    RoomDoors init;
    init.Place(*Info({D::North, D::South, D::West, D::East}));
    init.isInit = true;
    init.AddSpecialInitDoor(1);
    Check(!init.doors[1].present && init.boundaryLayer[1] == Layer::Default && init.specialRoomIndex == 1, "AddSpecialInitDoor");
    Check(!init.IsDeadEnd(), "the init room isn't a dead end");
    RoomDoors boss;
    boss.Place(*Info({D::North, D::South, D::West, D::East}));
    boss.isEnd = true;
    boss.ReplaceSpecialDoor(2, true);
    Check(!boss.doors[2].present && boss.hasSpecialDoor && boss.boundaryLayer[2] == Layer::EnemyBoundary, "ReplaceSpecialDoor");
    Check(boss.CloseAllDoors(1) && boss.boundaryLayer[2] == Layer::LevelBoundary, "the special door's opening closes");
    boss.Update(0.3f);
    Check(!boss.special.opened, "the special door closes");
    boss.OpenAllDoors();
    Check(boss.special.opened && boss.boundaryLayer[2] == Layer::EnemyBoundary, "and opens");

    cs::SetCurrentHeap(nullptr);
    std::printf("\nroom doors %d checks, %d failed\n\n%s\n", checks, failures, failures ? "MISMATCHES" : "ALL MATCH");
    return failures ? 1 : 0;
}
