#pragma once
// A room's scene rows (RoomSceneTree) as the hierarchy they are: each row's parent, world
// transform and whether it's active in the hierarchy. What the drawing (render/DrawList)
// and the physics (game/RoomPhysics) both place things with.

#include <vector>

#include "game/RoomScene.h"
#include "unity/Vector2.h"

namespace game {

// A 2D affine transform: (x, y) -> (a x + c y + tx, b x + d y + ty).
struct Affine {
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
    Affine Then(const Affine& local) const; // this * local
    void Apply(float x, float y, float& ox, float& oy) const
    {
        ox = a * x + c * y + tx;
        oy = b * x + d * y + ty;
    }
    // A row's local transform (rotation about z in degrees; quarter turns exact).
    static Affine Local(const SceneRow& r);
};

struct SceneWorld {
    std::vector<int> parent;    // row index, -1 for the room's own children
    std::vector<Affine> world;  // the room placed at roomPosition
    std::vector<char> active;   // activeInHierarchy
};

// Each row's parent is the latest row before it with its parent's path ("@n" in a path picks
// the n-th row with that path).
SceneWorld ResolveScene(const std::vector<SceneRow>& rows, unity::Vector2 roomPosition);

} // namespace game
