#pragma once
// Everything a room draws: its scene tree's sprites (DrawList) and the textures the game paints
// for it at runtime (game/Composite): the floor details (LowerDetail, BaseDetail, MiddleDetail:
// Background 0, 1, 2), the slime family (Background 5) and the holes (Background 10), each
// 484 x 268, centred on the room, one unit a pixel.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "game/Composite.h"
#include "game/RoomFill.h"
#include "render/DrawList.h"

namespace render {

struct RoomDraw {
    std::vector<std::unique_ptr<game::RoomTexture>> textures; // what the texture quads draw
    std::vector<DrawQuad> quads;                              // sorted
};

// `level` is the floor's (its details registers); `own` the room culture's tiles, `common` the
// slime family's. `painted` (optional) sees each texture as soon as it's painted (the
// GameCube makes its GX texture there and frees the RGBA, so a room's textures are never
// all in RGBA at once).
RoomDraw BuildRoomDraw(const game::RoomFill& room, unity::Vector2 roomPosition, int level, const SpriteBank& bank,
                       const game::TileTextures& own, const game::TileTextures& common, std::vector<std::string>* missing = nullptr,
                       const std::function<void(game::RoomTexture&)>& painted = {});

} // namespace render
