#pragma once
// A room's colliders for Physics2D (unity/Physics2D): every enabled BoxCollider2D on an
// active object of the room's scene tree (RoomSceneTree), with its object's layer and tag,
// placed through the hierarchy (SceneTransform).

#include <string>
#include <vector>

#include "game/RoomDoors.h"
#include "game/RoomScene.h"
#include "unity/Physics2D.h"

namespace game {

// Adds them; returns how many. `pathPrefix` is put before each row's path (the scene's own
// names: "Dungeon/<room>/...").
int AddRoomColliders(const std::vector<SceneRow>& rows, unity::Vector2 roomPosition, unity::Physics2D& physics,
                     const std::string& pathPrefix = "");

// A transform's world turn as Unity holds it (Transform.rotation, a turn about z: the
// quaternion's z and w) and the scale its localToWorldMatrix carries: a collider's, for the
// physics world (unity::Collider2D::rotZ, rotW, matrixScale).
struct Turn {
    float z = 0.0f, w = 1.0f;
    unity::Vector2 scale{1.0f, 1.0f};
    // Then a child's local turn and scale (Quaternionf's operator*, the parent's first).
    Turn Then(float localZ, float localW, float sx, float sy) const;
};

// The object at `path` below a dungeon room's root as the DungeonRoom prefab has it ("DungeonRoom"
// of dungeon_common: its walls, doors and the boundaries in its openings), turned down its
// hierarchy from the room's unturned root; false if the prefab has no such object (or isn't
// loaded). What a recording's collider rows lack (the oracle writes no rotation): the door
// boundaries are turned (Down half a turn, Left and Right a quarter).
bool DungeonRoomTurn(const std::string& path, Turn& turn);

// The doors' state changed (RoomDoors' runtime calls): their colliders and the boundaries in
// their openings, already added, follow it (enabled; layer and tag).
void UpdateDoorColliders(const RoomDoors& doors, unity::Physics2D& physics, const std::string& pathPrefix = "");

} // namespace game
