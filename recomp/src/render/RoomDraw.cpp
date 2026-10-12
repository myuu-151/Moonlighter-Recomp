#include "render/RoomDraw.h"

#include "game/Prefabs.h"

namespace render {

RoomDraw BuildRoomDraw(const game::RoomFill& room, unity::Vector2 roomPosition, int level, const SpriteBank& bank,
                       const game::TileTextures& own, const game::TileTextures& common, std::vector<std::string>* missing,
                       const std::function<void(game::RoomTexture&)>& painted)
{
    RoomDraw out;
    out.quads = BuildDrawList(game::RoomSceneTree(room, roomPosition), roomPosition, bank, missing);

    int background = 0;
    const std::vector<std::string>& layers = game::PrefabLibrary::SortingLayers();
    for (size_t i = 0; i < layers.size(); i++)
        if (layers[i] == "Background") background = static_cast<int>(i);

    auto add = [&](game::RoomTexture t, int order) {
        if (t.Empty()) return;
        out.textures.push_back(std::make_unique<game::RoomTexture>(std::move(t)));
        const game::RoomTexture* tex = out.textures.back().get();
        DrawQuad q;
        q.texture = tex;
        float hw = static_cast<float>(tex->width) / 2.0f, hh = static_cast<float>(tex->height) / 2.0f;
        float x0 = roomPosition.x - hw, x1 = roomPosition.x + hw, y0 = roomPosition.y - hh, y1 = roomPosition.y + hh;
        q.x[0] = x0, q.y[0] = y0;
        q.x[1] = x1, q.y[1] = y0;
        q.x[2] = x1, q.y[2] = y1;
        q.x[3] = x0, q.y[3] = y1;
        q.layer = background;
        q.order = order;
        q.sortY = roomPosition.y;
        out.quads.push_back(q);
        if (painted) painted(*out.textures.back());
    };

    // DungeonRoom.SpawnDetails' floor textures (the game's grey-205 blank, alpha cleared).
    static const uint8_t blank[3] = {205, 205, 205};
    const std::string prefix = "details" + std::to_string(level) + ".";
    add(game::PaintDetailsLayer(room.details.lower, prefix + "lower", own, blank), 0);
    add(game::PaintDetailsLayer(room.details.lower2, prefix + "lower2", own, blank), 1);
    add(game::PaintDetailsLayer(room.details.middleLayer, prefix + "middle", own, blank), 2);
    for (const auto& [tile, tiles] : room.slimeTiles) add(game::PaintSlime(tiles, tile, common), 5);
    add(game::PaintHoles(room.holeTiles, own), 10);
    SortDrawList(out.quads);
    return out;
}

} // namespace render
