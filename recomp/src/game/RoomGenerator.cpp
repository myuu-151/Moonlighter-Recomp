#include "game/RoomGenerator.h"

#include "unity/Random.h"
#include "util/Json.h"

namespace game {

namespace Random = unity::Random;

RoomGenerationData* RoomGenerator::generationDataByCulture[6] = {};

namespace {

// List<T>.GetRandom (Extensions.cs): one Range(0, Count) draw, none when empty.
int GetRandom(cs::List<int>* list)
{
    if (list->Count() == 0) return -1;
    return (*list)[Random::Range(0, list->Count())];
}

int Flip() { return Random::Range(0.0f, 1.0f) > 0.5f ? 1 : -1; }

void Fill(cs::List<int>* list, const json::Value& counts, const char* key)
{
    const json::Value* v = counts.Find(key);
    int n = v && v->IsInt() ? static_cast<int>(v->i) : 0;
    for (int i = 0; i < n; i++) list->Add(i);
}

} // namespace

RoomGenerationData* RoomGenerationData::FromCounts(const json::Value& c)
{
    auto* d = cs::gcnew<RoomGenerationData>();
    Fill(d->lights, c, "lights");
    Fill(d->shadows, c, "shadows");
    const json::Value* hs = c.Find("holeSprite");
    d->holeSprite = hs && hs->IsString() && hs->s == "set";
    Fill(d->holeBorderHorizontalUpSprite, c, "holeBorderHorizontalUpSprite");
    Fill(d->holeBorderHorizontalDownSprite, c, "holeBorderHorizontalDownSprite");
    Fill(d->holeBorderVerticalSprite, c, "holeBorderVerticalSprite");
    Fill(d->holeBorderCornerUpSprites, c, "holeBorderCornerUpSprites");
    Fill(d->holeBorderCornerDownSprites, c, "holeBorderCornerDownSprites");
    Fill(d->holeWallHorizontalSprite, c, "holeWallHorizontalSprite");
    Fill(d->holeWallVerticalSprite, c, "holeWallVerticalSprite");
    Fill(d->holeWallCornerSprite, c, "holeWallCornerSprite");
    Fill(d->holeWallBorderHorizontalUpSprite, c, "holeWallBorderHorizontalUpSprite");
    Fill(d->holeWallBorderHorizontalDownSprite, c, "holeWallBorderHorizontalDownSprite");
    Fill(d->holeWallBorderVerticalSprite, c, "holeWallBorderVerticalSprite");
    Fill(d->holeSlopeSprite, c, "holeSlopeSprite");
    Fill(d->holeWallSlopeVerticalSprite, c, "holeWallSlopeVerticalSprite");
    Fill(d->holeWallSlopeHorizontalSprite, c, "holeWallSlopeHorizontalSprite");
    Fill(d->holeSlopeCornerSprite, c, "holeSlopeCornerSprite");
    return d;
}

RoomGenerationData* RoomGenerationData::FromExtracted(const json::Value& entry)
{
    RoomGenerationData* d = FromCounts(entry["counts"]);
    if (const json::Value* sprites = entry.Find("sprites")) {
        auto names = [&](const char* key, std::vector<std::string>& out) {
            if (const json::Value* list = sprites->Find(key))
                for (const json::Value& n : list->arr) out.push_back(n.IsString() ? n.s : std::string());
        };
        names("lights", d->lightNames);
        names("shadows", d->shadowNames);
        names("roomBackground", d->backgroundNames);
        names("roomBackgroundWalls", d->backgroundWallNames);
    }
    return d;
}

void RoomGenerator::SetLights(cs::List<int>* l)
{
    lights->Clear();
    lights->AddRange(l);
}

void RoomGenerator::SetShadows(cs::List<int>* s)
{
    shadows->Clear();
    shadows->AddRange(s);
}

RoomGenerator::LightShadow RoomGenerator::AddLightShadow(Culture culture, bool hasLightsRenderer, bool hasShadowsRenderer)
{
    LightShadow out;
    RoomGenerationData* data = static_cast<int>(culture) < 6 ? generationDataByCulture[static_cast<int>(culture)] : nullptr;
    // `lights = GetLightsByCulture(culture)` stores the data's own list (aliasing).
    if (lights->Count() == 0) lights = data ? data->lights : cs::gcnew<cs::List<int>>();
    if (shadows->Count() == 0) shadows = data ? data->shadows : cs::gcnew<cs::List<int>>();
    if (hasLightsRenderer && lights->Count() > 0) out.light = GetRandom(lights);
    if (hasShadowsRenderer) {
        // new Vector3(flip, flip, 1): x's draw happens first.
        out.flipX = Random::Range(0.0f, 1.0f) > 0.5f ? -1 : 1;
        out.flipY = Random::Range(0.0f, 1.0f) > 0.5f ? -1 : 1;
        if (shadows->Count() > 0) out.shadow = GetRandom(shadows);
    }
    return out;
}

std::vector<TileSprite> RoomGenerator::RasterizeHoles(Culture culture, const std::vector<int>& grid, int roomX, int roomY)
{
    std::vector<TileSprite> out;
    RoomGenerationData* d = static_cast<int>(culture) < 6 ? generationDataByCulture[static_cast<int>(culture)] : nullptr;
    if (!d) d = cs::gcnew<RoomGenerationData>(); // OnCultureChanged keeps the previous lists then; not hit in data

    auto at = [&](int i, int j) { return grid.at(static_cast<size_t>(i + j * gridSlicesX)); };
    auto inGrid = [&](int i, int j) { return i > -1 && j > -1 && i < roomX && j < roomY; };
    auto isHole = [&](int v) { return v == 2 || v == 8; };
    auto checkTrue = [&](int i, int j) { return inGrid(i, j) && isHole(at(i, j)); };
    auto checkFalse = [&](int i, int j) { return inGrid(i, j) && !isHole(at(i, j)); };
    // Arguments evaluate left to right: the list pick comes before the flip draw.
    auto add = [&](int k, int l, const char* name, cs::List<int>* list, int sx, int sy) { out.push_back({k, l, name, GetRandom(list), sx, sy}); };
    auto addFlipX = [&](int k, int l, const char* name, cs::List<int>* list, int sy) {
        int idx = GetRandom(list);
        out.push_back({k, l, name, idx, Flip(), sy});
    };
    auto addFlipY = [&](int k, int l, const char* name, cs::List<int>* list, int sx) {
        int idx = GetRandom(list);
        out.push_back({k, l, name, idx, sx, Flip()});
    };

    for (int k = 0; k < roomX; k++) {
        for (int l = 0; l < roomY; l++) {
            if (!isHole(at(k, l))) continue;
            if (!checkTrue(k, l + 1)) addFlipX(k, l, "holeSlope", d->holeSlopeSprite, 1);
            else out.push_back({k, l, "hole", -1, 1, 1});
            if (checkFalse(k, l + 1) && checkFalse(k + 1, l)) add(k, l, "borderCornerUp", d->holeBorderCornerUpSprites, 1, 1);
            if (checkFalse(k, l + 1) && checkFalse(k - 1, l)) add(k, l, "borderCornerUp", d->holeBorderCornerUpSprites, -1, 1);
            if (checkFalse(k, l - 1) && checkFalse(k + 1, l)) add(k, l, "borderCornerDown", d->holeBorderCornerDownSprites, 1, -1);
            if (checkFalse(k, l - 1) && checkFalse(k - 1, l)) add(k, l, "borderCornerDown", d->holeBorderCornerDownSprites, -1, -1);
            if (checkFalse(k + 1, l)) addFlipY(k, l, "borderVertical", d->holeBorderVerticalSprite, 1);
            if (checkFalse(k - 1, l)) addFlipY(k, l, "borderVertical", d->holeBorderVerticalSprite, -1);
            if (checkFalse(k, l + 1)) addFlipX(k, l, "borderHorizontalUp", d->holeBorderHorizontalUpSprite, 1);
            if (checkFalse(k, l - 1)) addFlipX(k, l, "borderHorizontalDown", d->holeBorderHorizontalDownSprite, -1);
            if (!inGrid(k, l + 1)) {
                add(k, l, "slopeHorizontal", d->holeWallSlopeHorizontalSprite, 1, 1);
                if (checkFalse(k - 1, l)) add(k, l, "wallBorderVertical", d->holeWallBorderVerticalSprite, -1, 1);
                if (checkFalse(k + 1, l)) add(k, l, "wallBorderVertical", d->holeWallBorderVerticalSprite, 1, 1);
            }
            if (!inGrid(k, l - 1)) {
                add(k, l, "wallHorizontal", d->holeWallHorizontalSprite, 1, -1);
                if (checkFalse(k - 1, l)) add(k, l, "wallBorderHorizontalDown", d->holeWallBorderHorizontalDownSprite, -1, -1);
                if (checkFalse(k + 1, l)) add(k, l, "wallBorderHorizontalDown", d->holeWallBorderHorizontalDownSprite, 1, -1);
            }
            if (!inGrid(k + 1, l)) {
                if (!checkTrue(k, l + 1)) add(k, l, "slopeVertical", d->holeWallSlopeVerticalSprite, 1, 1);
                else add(k, l, "wallVertical", d->holeWallVerticalSprite, 1, 1);
                if (checkFalse(k, l - 1)) add(k, l, "wallBorderHorizontalUp", d->holeWallBorderHorizontalUpSprite, 1, -1);
                if (checkFalse(k, l + 1)) add(k, l, "wallBorderHorizontalUp", d->holeWallBorderHorizontalUpSprite, 1, 1);
            }
            if (!inGrid(k - 1, l)) {
                if (!checkTrue(k, l + 1)) add(k, l, "slopeVertical", d->holeWallSlopeVerticalSprite, -1, 1);
                else add(k, l, "wallVertical", d->holeWallVerticalSprite, -1, 1);
                if (checkFalse(k, l - 1)) add(k, l, "wallBorderHorizontalUp", d->holeWallBorderHorizontalUpSprite, -1, -1);
                if (checkFalse(k, l + 1)) add(k, l, "wallBorderHorizontalUp", d->holeWallBorderHorizontalUpSprite, -1, 1);
            }
            if (!inGrid(k, l + 1) && !inGrid(k + 1, l)) add(k, l, "slopeCorner", d->holeSlopeCornerSprite, 1, 1);
            if (!inGrid(k, l + 1) && !inGrid(k - 1, l)) add(k, l, "slopeCorner", d->holeSlopeCornerSprite, -1, 1);
            if (!inGrid(k, l - 1) && !inGrid(k + 1, l)) add(k, l, "wallCorner", d->holeWallCornerSprite, 1, -1);
            if (!inGrid(k, l - 1) && !inGrid(k - 1, l)) add(k, l, "wallCorner", d->holeWallCornerSprite, -1, -1);
        }
    }
    return out;
}

std::vector<TileSprite> RoomGenerator::RasterizeSlime(const std::vector<int>& grid, int roomX, int roomY, int tile)
{
    std::vector<TileSprite> out;
    auto inGrid = [&](int i, int j) { return i > -1 && j > -1 && i < roomX && j < roomY; };
    // GetTilesWithDungeonTile indexes with the room's gridSlicesX; CheckGridTrue with the generator's.
    auto checkTrue = [&](int i, int j) { return inGrid(i, j) && grid.at(static_cast<size_t>(i + j * gridSlicesX)) == tile; };
    for (int x = 0; x < roomX; x++) {
        for (int y = 0; y < roomY; y++) {
            if (grid.at(static_cast<size_t>(x + y * roomX)) != tile) continue;
            bool flag = Random::Range(0.0f, 1.0f) < 0.5f;
            int mask = 0;
            mask += (flag ? 2 : 8) * (checkTrue(x + 1, y) ? 1 : 0);
            mask += 4 * (checkTrue(x, y + 1) ? 1 : 0);
            mask += (!flag ? 2 : 8) * (checkTrue(x - 1, y) ? 1 : 0);
            mask += 1 * (checkTrue(x, y - 1) ? 1 : 0);
            out.push_back({x, y, "slime", mask, flag ? -1 : 1, 1});
        }
    }
    return out;
}

} // namespace game
