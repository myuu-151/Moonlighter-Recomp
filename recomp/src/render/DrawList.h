#pragma once
// What a room draws: its scene tree (game/RoomScene) turned into sprite quads in world
// units, in the order Unity draws them: by sorting layer (the TagManager's order), then
// sortingOrder, then from the top of the screen down (the farther-up sprite behind).
// SpriteDepthOrderRender's Start() is applied (its sortingOrder from the world y), as it has
// run by the time the player sees a room.

#include <string>
#include <vector>

#include "game/RoomScene.h"
#include "render/SpriteBank.h"

namespace render {

struct DrawQuad {
    const Sprite* sprite = nullptr;
    // Corners in world units: the image's lower-left, lower-right, upper-right, upper-left.
    float x[4] = {}, y[4] = {};
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    int layer = 0, order = 0;
    float sortY = 0.0f; // the object's world y
    int sequence = 0;   // its place in the tree (ties)
    std::string path;   // for diagnostics
};

// The room's quads, sorted. `missing` (optional) collects sprite names the bank lacks.
std::vector<DrawQuad> BuildDrawList(const std::vector<game::SceneRow>& rows, unity::Vector2 roomPosition, const SpriteBank& bank,
                                    std::vector<std::string>* missing = nullptr);

} // namespace render
