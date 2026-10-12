#pragma once
// A room's colliders for Physics2D (unity/Physics2D): every enabled BoxCollider2D on an
// active object of the room's scene tree (RoomSceneTree), with its object's layer and tag,
// placed through the hierarchy (SceneTransform).

#include <vector>

#include "game/RoomScene.h"
#include "unity/Physics2D.h"

namespace game {

// Adds them; returns how many. `pathPrefix` is put before each row's path (the scene's own
// names: "Dungeon/<room>/...").
int AddRoomColliders(const std::vector<SceneRow>& rows, unity::Vector2 roomPosition, unity::Physics2D& physics,
                     const std::string& pathPrefix = "");

} // namespace game
