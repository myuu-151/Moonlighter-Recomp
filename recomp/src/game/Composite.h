#pragma once
// The textures the generator paints at runtime (DungeonRoomGenerator.FillTextureWithSprite-
// Transforms): a room's holes are tile sprites' whole textures painted into one 484 x 268
// texture, the darker (or more opaque) pixel winning. The tiles' textures are data
// (tools/extract/tiles.py, exact); the result is checked against the game's own texture by
// its CRC (the oracle records it).

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "game/RoomDetails.h"
#include "game/RoomGenerator.h"

namespace game {

struct TileTexture {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba; // GetPixels32 order: rows from the bottom
};

class TileTextures {
public:
    void Load(const std::string& bin); // a tiles/<culture>.bin's contents
    const TileTexture* Find(const std::string& key) const;

private:
    std::map<std::string, TileTexture> byKey_;
};

struct RoomTexture {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba; // GetPixels32 order: rows from the bottom
    bool Empty() const { return rgba.empty(); }
    uint32_t Crc() const; // CRC-32 over the RGBA bytes, as the oracle computes it
};

// RasterizeHolesImage's texture from its tile list (RoomGenerator::RasterizeHoles); empty
// if the room has no hole tiles (the game makes no texture then).
RoomTexture PaintHoles(const std::vector<TileSprite>& tiles, const TileTextures& textures);

// RasterizeSlimeImage's texture (slime, acid, lava, electric: `tile` 16, 64, 128, 256) from
// RoomGenerator::RasterizeSlime's tiles; the sprites are DungeonRoomGenerator's (tiles/
// common.bin). Empty if there are none.
RoomTexture PaintSlime(const std::vector<TileSprite>& tiles, int tile, const TileTextures& common);

// FillRoomWithSprites: one of a room's floor textures (484 x 268) from what SpawnDetails
// chose (LayerPaint); `prefix` names the sprites' list ("details0.lower"). Empty if nothing.
// The texture starts as Unity's new Texture2D (`blank` RGB) with alpha cleared.
RoomTexture PaintDetailsLayer(const LayerPaint& paint, const std::string& prefix, const TileTextures& textures, const uint8_t blank[3]);

} // namespace game
