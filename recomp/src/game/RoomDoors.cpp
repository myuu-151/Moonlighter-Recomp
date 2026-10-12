#include "game/RoomDoors.h"

#include <algorithm>

#include "game/RoomScene.h"
#include "game/SceneTransform.h"

namespace game {

const char* const kDoorPaths[4] = {"DungeonLevelDoors/LevelDoorUp", "DungeonLevelDoors/LevelDoorDown", "DungeonLevelDoors/LevelDoorLeft",
                                   "DungeonLevelDoors/LevelDoorRight"};
const char* const kDoorBoundaryPaths[4] = {"DungeonLevelBoundaries/DoorBoundaryUp", "DungeonLevelBoundaries/DoorBoundaryDown",
                                           "DungeonLevelBoundaries/DoorBoundaryLeft", "DungeonLevelBoundaries/DoorBoundaryRight"};
const unity::Vector2 kDoorPositions[4] = {{0.0f, 148.0f}, {0.0f, -148.0f}, {-256.0f, 0.0f}, {256.0f, 0.0f}};

void RoomDoors::Place(const DungeonRoomInfo& info)
{
    // InitializeRoom(info): doors the design doesn't have go, their boundary to layer 0.
    static const DungeonRoomInfo::Doors kSides[4] = {DungeonRoomInfo::Doors::North, DungeonRoomInfo::Doors::South,
                                                     DungeonRoomInfo::Doors::West, DungeonRoomInfo::Doors::East};
    for (int i = 0; i < 4; i++)
        if (!info.availableDoors || info.availableDoors->IndexOf(kSides[i]) == -1) {
            boundaryLayer[i] = Layer::Default;
            doors[i].present = false;
        }
    // AddSingleDungeonRoom: DisableAllDoors before RoomPlaced, so with the closed layer still 0.
    DisableAllDoors();
    // RoomPlaced (version 0; every base-game room has an obstacles grid and the floor a culture).
    doorClosedLayer = Layer::LevelBoundary;
    doorOpenedLayer = Layer::EnemyBoundary;
    doorClosedTag = "LevelBoundary";
    doorOpenedTag = "LevelBoundary";
    // (RoomPlaced's OnAllEnemiesDefeated -> OpenAllDoors finds nothing connected yet.)
}

void RoomDoors::DisableAllDoors()
{
    for (int i = 0; i < 4; i++) {
        DoorState& d = doors[i];
        if (!d.present || d.connected) continue;
        d.disabled = true;
        d.colliderEnabled = false;
        boundaryLayer[i] = doorClosedLayer;
        if (!doorClosedTag.empty()) boundaryTag[i] = doorClosedTag;
    }
}

bool RoomDoors::Connect(RoomDoors& a, unity::Vector2 aPosition, int side, RoomDoors& b, unity::Vector2 bPosition)
{
    const int other = OppositeDoor(side);
    DoorState& da = a.doors[side];
    DoorState& db = b.doors[other];
    if (!da.present || !db.present || da.connected || db.connected) return false;
    for (DoorState* d : {&da, &db}) {
        d->connected = true;
        // EnableDoor.
        d->disabled = false;
        d->colliderEnabled = true;
    }
    a.boundaryLayer[side] = Layer::EnemyBoundary;
    b.boundaryLayer[other] = Layer::EnemyBoundary;
    // Each door's arrival point: in front of the other door, kDoorSeparation into its room.
    const unity::Vector2 inward[4] = {{0.0f, 1.0f}, {0.0f, -1.0f}, {-1.0f, 0.0f}, {1.0f, 0.0f}};
    auto doorAt = [](unity::Vector2 room, int s) { return unity::Vector2{room.x + kDoorPositions[s].x, room.y + kDoorPositions[s].y}; };
    unity::Vector2 pa = doorAt(bPosition, other), pb = doorAt(aPosition, side);
    da.arrival = {pa.x + inward[side].x * kDoorSeparation, pa.y + inward[side].y * kDoorSeparation};
    db.arrival = {pb.x + inward[other].x * kDoorSeparation, pb.y + inward[other].y * kDoorSeparation};
    da.hasArrival = db.hasArrival = true;
    a.isEnd = false;
    return true;
}

void RoomDoors::ReplaceSpecialDoor(int side, bool nextLevelDoor)
{
    // Nothing happens if the slot has no door.
    if (side < 0 || side > 3 || !doors[side].present) return;
    boundaryLayer[side] = Layer::EnemyBoundary;
    doors[side].present = false;
    hasSpecialDoor = true;
    specialIsNextLevelDoor = nextLevelDoor;
    special = DoorState();
    specialRoomIndex = side;
}

void RoomDoors::AddSpecialInitDoor(int side)
{
    if (!isInit || side < 0 || side > 3 || !doors[side].present) return;
    doors[side].disabled = true;
    doors[side].colliderEnabled = false;
    doors[side].present = false;
    specialRoomIndex = side;
}

int RoomDoors::ConnectedDoors() const
{
    int n = 0;
    for (const DoorState& d : doors) n += d.present && d.connected;
    return n;
}

bool RoomDoors::IsDeadEnd() const
{
    if (!isEnd && (ConnectedDoors() != 1 || specialRoomIndex != -1 || isInit) && roomChests <= 0) return containsMimics;
    return true;
}

bool RoomDoors::CloseAllDoors(int roomEnemies)
{
    if (!isLocked && !(IsDeadEnd() && roomEnemies > 0)) return false;
    // Every door there, connected or not: the boundary closes now, the door in 0.3 s.
    for (int i = 0; i < 4; i++) {
        if (!doors[i].present) continue;
        if (doors[i].closeIn < 0.0f) doors[i].closeIn = kDoorCloseDelay;
        boundaryLayer[i] = doorClosedLayer;
        boundaryTag[i] = doorClosedTag;
    }
    if (hasSpecialDoor) {
        if (special.closeIn < 0.0f) special.closeIn = kDoorCloseDelay;
        if (specialRoomIndex >= 0) {
            boundaryLayer[specialRoomIndex] = doorClosedLayer;
            boundaryTag[specialRoomIndex] = doorClosedTag;
        }
    }
    return true;
}

void RoomDoors::OpenAllDoors(bool intermediateDungeon)
{
    if (isLocked && !IsDeadEnd()) return;
    const bool onlySpecial = intermediateDungeon && hasSpecialDoor && specialIsNextLevelDoor;
    for (int i = 0; i < 4; i++)
        if (!onlySpecial && doors[i].present && doors[i].connected) OpenDoor(i);
    if (hasSpecialDoor) {
        // (OnDoorOpened doesn't find the special door in roomDoors: just isOpened.)
        special.opened = true;
        if (specialRoomIndex >= 0) {
            boundaryLayer[specialRoomIndex] = doorOpenedLayer;
            boundaryTag[specialRoomIndex] = doorOpenedTag;
        }
    }
}

void RoomDoors::SetLocked(bool locked, int roomEnemies, bool allEnemiesDefeated)
{
    isLocked = locked;
    if (isLocked) CloseAllDoors(roomEnemies);
    if (!isLocked && allEnemiesDefeated) OpenAllDoors();
}

void RoomDoors::Update(float deltaTime)
{
    for (int i = 0; i < 4; i++) {
        DoorState& d = doors[i];
        if (d.closeIn < 0.0f) continue;
        d.closeIn -= deltaTime;
        if (d.closeIn <= 0.0f) {
            d.closeIn = -1.0f;
            if (d.present) CloseDoor(i);
        }
    }
    if (hasSpecialDoor && special.closeIn >= 0.0f) {
        special.closeIn -= deltaTime;
        if (special.closeIn <= 0.0f) {
            special.closeIn = -1.0f;
            special.opened = false; // (OnDoorClosed doesn't find it in roomDoors)
        }
    }
}

void RoomDoors::CloseDoor(int side)
{
    doors[side].opened = false;
    // OnDoorClosed (disableColliderOnClose is off in the base game: the collider stays).
    boundaryLayer[side] = doorClosedLayer;
    boundaryTag[side] = doorClosedTag;
}

void RoomDoors::OpenDoor(int side)
{
    doors[side].opened = true;
    // OnDoorOpened.
    boundaryLayer[side] = doorOpenedLayer;
    boundaryTag[side] = doorOpenedTag;
    doors[side].colliderEnabled = true;
}

void RoomDoors::DisableRoomDoorsColliders()
{
    for (DoorState& d : doors)
        if (d.present && !d.disabled) d.colliderEnabled = false;
}

void RoomDoors::EnableRoomDoorsColliders()
{
    for (DoorState& d : doors)
        if (d.present && !d.disabled && d.opened && d.closeIn < 0.0f) d.colliderEnabled = true;
}

void ApplyDoors(std::vector<SceneRow>& rows, const RoomDoors& doors, unity::Vector2 roomPosition)
{
    auto under = [](const std::string& path, const std::string& root) { return path == root || path.rfind(root + "/", 0) == 0; };
    // Doors that aren't there any more: their objects go.
    for (int i = 0; i < 4; i++)
        if (!doors.doors[i].present)
            rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const SceneRow& r) { return under(r.path, kDoorPaths[i]); }), rows.end());

    const SceneWorld scene = ResolveScene(rows, roomPosition);
    for (size_t k = 0; k < rows.size(); k++) {
        SceneRow& r = rows[k];
        for (int i = 0; i < 4; i++) {
            const DoorState& d = doors.doors[i];
            const std::string door = kDoorPaths[i];
            if (r.path == kDoorBoundaryPaths[i]) {
                r.objectLayer = doors.boundaryLayer[i];
                r.tag = doors.boundaryTag[i];
            } else if (r.path == door) {
                for (ColliderDef& c : r.colliders) c.enabled = d.colliderEnabled;
            } else if (under(r.path, door)) {
                // EnableDoor / DisableDoor: every SpriteRenderer in the door.
                if (!r.layer.empty()) r.rendererEnabled = !d.disabled;
                // The arrival point's world position, through the door's transform.
                if (d.hasArrival && r.path == door + "/PlayerEnterDestinyRoomPoint" && scene.parent[k] >= 0) {
                    const Affine& m = scene.world[static_cast<size_t>(scene.parent[k])];
                    float det = m.a * m.d - m.b * m.c;
                    float dx = d.arrival.x - m.tx, dy = d.arrival.y - m.ty;
                    r.x = (m.d * dx - m.c * dy) / det;
                    r.y = (m.a * dy - m.b * dx) / det;
                }
            }
        }
    }
}

} // namespace game
