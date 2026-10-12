#pragma once
// A dungeon room's doors as DungeonRoom and DungeonDoor keep them: which of roomDoors are
// there, connected, disabled and open, and the layer and tag of roomDoorsBoundaries (the
// box in each door's opening) that go with them. Both lists are indexed 0 north
// (LevelDoorUp), 1 south (Down), 2 west (Left), 3 east (Right).
//
// Generation (DungeonGenerator, base game) sets them up:
//  - InitializeRoom(info): doors the design doesn't have are destroyed, their boundary goes
//    to layer 0 (Default);
//  - DisableAllDoors, before RoomPlaced gives the room its door layers: every door without a
//    destination is disabled (collider and sprites off) and its boundary goes to the closed
//    layer, which is still 0 then (the tag, still null, stays "LevelBoundary");
//  - Connect{North,South,East,West}Room: the two facing doors get each other's room, are
//    enabled, their boundaries go to EnemyBoundary and each door's arrival point is put 35
//    units inside the other room, in front of its door;
//  - ReplaceSpecialDoor (the next-floor door, the boss enter door): the slot's door is
//    destroyed, the special door takes its place, the slot's boundary goes to EnemyBoundary;
//    AddSpecialInitDoor (the init room's first/last-floor door): the slot's door is disabled
//    and destroyed, its boundary keeps what DisableAllDoors gave it.
// At runtime EnterRoom closes the doors only in a locked room or a dead end with enemies
// (CloseAllDoors: boundaries closed at once, the doors 0.3 s later); OpenAllDoors (all
// enemies defeated) opens the connected doors and the special door. The door colliders are
// off while the camera moves to a new room and come back on (open doors only) when it's there.
//
// ApplyDoors puts the state on a room's scene rows: what RoomPhysics makes its colliders from.

#include <string>
#include <vector>

#include "game/RoomDatabase.h"
#include "unity/Vector2.h"

namespace game {

struct SceneRow; // game/RoomScene.h

// Physics layers (the project's TagManager).
namespace Layer {
constexpr int Default = 0, LevelBoundary = 11, EnemyBoundary = 21, Door = 25;
}

// The DungeonRoom prefab's doors and the boundaries in their openings, by roomDoors index.
extern const char* const kDoorPaths[4];
extern const char* const kDoorBoundaryPaths[4];
extern const unity::Vector2 kDoorPositions[4]; // the doors' local positions
constexpr float kDoorSeparation = 35.0f;       // DungeonRoom.doorSeparation
constexpr float kDoorCloseDelay = 0.3f;        // CloseAllDoors: CloseDoorAfterTime(0.3f)

// GetComplementaryDoorIndex: the facing door (north <-> south, west <-> east).
inline int OppositeDoor(int side) { return side ^ 1; }

struct DoorState {
    bool present = true;         // roomDoors[i] != null: the design has it, no special door took its place
    bool connected = false;      // destinationRoom set
    bool disabled = false;       // isDisabled (DisableDoor: collider and sprites off)
    bool colliderEnabled = true; // its BoxCollider2D
    bool opened = true;          // isOpened
    float closeIn = -1.0f;       // CloseDoorAfterTime's pending CloseDoor: seconds left (< 0: none)
    bool hasArrival = false;     // playerEnterDestinyRoomPoint was moved (Connect*Room)
    unity::Vector2 arrival;      // its world position: where the hero comes out in the other room
};

class RoomDoors {
public:
    DoorState doors[4];
    int boundaryLayer[4] = {Layer::EnemyBoundary, Layer::EnemyBoundary, Layer::EnemyBoundary, Layer::EnemyBoundary};
    std::string boundaryTag[4] = {"LevelBoundary", "LevelBoundary", "LevelBoundary", "LevelBoundary"};
    // specialRoomIndex: the slot of the special door (ReplaceSpecialDoor) or of the init
    // room's door (AddSpecialInitDoor); -1 none. Only ReplaceSpecialDoor sets specialDoor.
    int specialRoomIndex = -1;
    bool hasSpecialDoor = false, specialIsNextLevelDoor = false;
    DoorState special; // the special door's opened / pending close
    // RoomPlaced's door layers (base game); before it they're 0 and the tags null.
    int doorClosedLayer = 0, doorOpenedLayer = 0;
    std::string doorClosedTag, doorOpenedTag;
    // DungeonRoom fields the doors read.
    bool isInit = false, isEnd = false, isLocked = false, containsMimics = false;
    int roomChests = 0; // SpawnChests' chests (mimics are enemies)

    // ---- generation ----
    // AddSingleDungeonRoom's part: InitializeRoom(info), DisableAllDoors, RoomPlaced's layers.
    void Place(const DungeonRoomInfo& info);
    void DisableAllDoors();
    // Connect{North,South,West,East}Room: `a`'s door on `side` to `b`'s facing door (`b` is
    // the neighbour on that side). False if either door is missing or already connected.
    static bool Connect(RoomDoors& a, unity::Vector2 aPosition, int side, RoomDoors& b, unity::Vector2 bPosition);
    void ReplaceSpecialDoor(int side, bool nextLevelDoor);
    void AddSpecialInitDoor(int side);

    // ---- runtime ----
    int ConnectedDoors() const; // roomDoors with a destination
    bool IsDeadEnd() const;
    // EnterRoom (and isLocked = true): closes if locked, or a dead end with enemies. True if it did.
    bool CloseAllDoors(int roomEnemies);
    // OnAllEnemiesDefeated (or a shrine). `intermediateDungeon`: the Wanderer DLC's
    // intermediate floor, where a room with the next-floor door opens only that one.
    void OpenAllDoors(bool intermediateDungeon = false);
    // The isLocked setter: locking closes the doors; unlocking with every enemy defeated opens them.
    void SetLocked(bool locked, int roomEnemies, bool allEnemiesDefeated);
    // Time passing: pending CloseDoor calls (CloseDoorAfterTime) run.
    void Update(float deltaTime);
    void CloseDoor(int side); // DungeonDoor.CloseDoor + OnDoorClosed
    void OpenDoor(int side);  // DungeonDoor.OpenDoor + OnDoorOpened
    // ChangeRoomTo (the room being entered) and the camera's arrival (FinishedRoomTransitionMovement).
    void DisableRoomDoorsColliders();
    void EnableRoomDoorsColliders();
};

// Puts the doors' state on the room's rows: missing doors' objects removed, each door's
// collider and sprites, its arrival point (local to the door), the boundaries' layer and tag.
void ApplyDoors(std::vector<SceneRow>& rows, const RoomDoors& doors, unity::Vector2 roomPosition);

} // namespace game
