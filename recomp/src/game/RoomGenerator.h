#pragma once
// DungeonRoomGenerator.cs, the RNG-relevant parts of the per-room visual passes:
//  - AddLightShadow (with the game's list-aliasing behaviour, see below);
//  - RasterizeHolesImage's sprite choices (which hole/border/wall sprite and flip per tile);
//  - the one-flip-per-tile pass of GenerateSlime/Acid/Lava/ElectricPrefab.
// Sprites are identified by their index in the culture's list; drawing them is the renderer's job.

#include <string>
#include <vector>

#include "cs/List.h"
#include "game/RoomDatabase.h"

namespace game {

// DungeonRoomGenerationData: one per culture, cached for the whole session
// (DungeonRoomGenerator.generationDataByCulture is static). Lists hold sprite indices.
class RoomGenerationData : public cs::Object {
public:
    cs::List<int>* lights = cs::gcnew<cs::List<int>>();
    cs::List<int>* shadows = cs::gcnew<cs::List<int>>();
    bool holeSprite = false;
    cs::List<int>* holeBorderHorizontalUpSprite = cs::gcnew<cs::List<int>>();
    cs::List<int>* holeBorderHorizontalDownSprite = cs::gcnew<cs::List<int>>();
    cs::List<int>* holeBorderVerticalSprite = cs::gcnew<cs::List<int>>();
    cs::List<int>* holeBorderCornerUpSprites = cs::gcnew<cs::List<int>>();
    cs::List<int>* holeBorderCornerDownSprites = cs::gcnew<cs::List<int>>();
    cs::List<int>* holeWallHorizontalSprite = cs::gcnew<cs::List<int>>();
    cs::List<int>* holeWallVerticalSprite = cs::gcnew<cs::List<int>>();
    cs::List<int>* holeWallCornerSprite = cs::gcnew<cs::List<int>>();
    cs::List<int>* holeWallBorderHorizontalUpSprite = cs::gcnew<cs::List<int>>();
    cs::List<int>* holeWallBorderHorizontalDownSprite = cs::gcnew<cs::List<int>>();
    cs::List<int>* holeWallBorderVerticalSprite = cs::gcnew<cs::List<int>>();
    cs::List<int>* holeSlopeSprite = cs::gcnew<cs::List<int>>();
    cs::List<int>* holeWallSlopeVerticalSprite = cs::gcnew<cs::List<int>>();
    cs::List<int>* holeWallSlopeHorizontalSprite = cs::gcnew<cs::List<int>>();
    cs::List<int>* holeSlopeCornerSprite = cs::gcnew<cs::List<int>>();

    // From extracted list sizes (generation_data_counts.json "counts" object).
    static RoomGenerationData* FromCounts(const json::Value& counts);
    // From an extracted entry ({counts, sprites}): the lists, and the sprites' names.
    static RoomGenerationData* FromExtracted(const json::Value& entry);

    // Sprite names by list index (empty when the data had only counts).
    std::vector<std::string> lightNames, shadowNames, backgroundNames, backgroundWallNames;
};

// One placed tile sprite: list name, index in that list (-1 = holeSprite), flips.
struct TileSprite {
    int x, y;
    const char* list;
    int index;
    int scaleX, scaleY;
};

class RoomGenerator : public cs::Object {
public:
    // Session-wide cache, indexed by culture (static in the game). On each floor transition
    // only the floor's own culture entry comes back fresh: the registers' UnloadBundlesExcept
    // has an inverted test and unloads just that culture's bundle (verified against traces:
    // corrupted rooms find their culture's lights list still emptied from earlier floors).
    static RoomGenerationData* generationDataByCulture[6];

    cs::List<int>* lights = cs::gcnew<cs::List<int>>();
    cs::List<int>* shadows = cs::gcnew<cs::List<int>>();
    int gridSlicesX = 20; // the generator's own field, used for grid indexing

    // RoomPlaced calls SetLights(new List) / SetShadows(new List) before AddLightShadow.
    // Clear() acts on whatever list `lights` refers to; after AddLightShadow that is the
    // culture data's own list, so the next room's SetLights empties the shared data.
    void SetLights(cs::List<int>* l);
    void SetShadows(cs::List<int>* s);

    struct LightShadow {
        int light = -1, shadow = -1;
        int flipX = 1, flipY = 1;
    };
    LightShadow AddLightShadow(Culture roomCulture, bool hasLightsRenderer = true, bool hasShadowsRenderer = true);

    // RasterizeHolesImage's choices for a room's grid (tiles 2 and 8 are holes).
    std::vector<TileSprite> RasterizeHoles(Culture roomCulture, const std::vector<int>& grid, int roomSlicesX, int roomSlicesY);

    // RasterizeSlimeImage (slime, acid, lava, electric): per tile of value `tile`, in
    // column-major order, one flip draw; `index` is the neighbour mask picking the sprite.
    std::vector<TileSprite> RasterizeSlime(const std::vector<int>& grid, int roomSlicesX, int roomSlicesY, int tile);
};

} // namespace game
