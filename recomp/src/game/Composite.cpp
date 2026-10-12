#include "game/Composite.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

#include "game/Constants.h"

namespace game {

namespace {

// DungeonRoomGenerator's texture size and border (gridX/Y + borderX/Y * 2); DungeonRoom's
// floor size (roomFloorWidth/Height), the details textures' size.
constexpr int kTextureWidth = 480 + 2 * 2, kTextureHeight = 264 + 2 * 2, kBorder = 2;
constexpr int kRoomFloorWidthPx = 484, kRoomFloorHeightPx = 268;

// RoomGenerator::RasterizeHoles' list names -> the DungeonRoomGenerationData fields they are
// (OnCultureChanged maps holeSlopeVerticalSprite to holeWallSlopeVerticalSprite, ...).
const char* Field(const std::string& list)
{
    static const std::map<std::string, const char*> fields = {
        {"hole", "holeSprite"},
        {"holeSlope", "holeSlopeSprite"},
        {"borderCornerUp", "holeBorderCornerUpSprites"},
        {"borderCornerDown", "holeBorderCornerDownSprites"},
        {"borderVertical", "holeBorderVerticalSprite"},
        {"borderHorizontalUp", "holeBorderHorizontalUpSprite"},
        {"borderHorizontalDown", "holeBorderHorizontalDownSprite"},
        {"slopeHorizontal", "holeWallSlopeHorizontalSprite"},
        {"slopeVertical", "holeWallSlopeVerticalSprite"},
        {"slopeCorner", "holeSlopeCornerSprite"},
        {"wallHorizontal", "holeWallHorizontalSprite"},
        {"wallVertical", "holeWallVerticalSprite"},
        {"wallCorner", "holeWallCornerSprite"},
        {"wallBorderVertical", "holeWallBorderVerticalSprite"},
        {"wallBorderHorizontalUp", "holeWallBorderHorizontalUpSprite"},
        {"wallBorderHorizontalDown", "holeWallBorderHorizontalDownSprite"},
    };
    auto it = fields.find(list);
    return it == fields.end() ? nullptr : it->second;
}

// CompareDarkerThanPixel (not the outline variant).
bool Darker(const uint8_t* c, const uint8_t* c2)
{
    if (c[1] == 0 && c[2] == 0) return true;
    if (c2[1] == 0 && c2[2] == 0) return false;
    return c[0] + c[1] + c[2] < c2[0] + c2[1] + c2[2];
}

} // namespace

void TileTextures::Load(const std::string& b)
{
    size_t p = 0;
    auto u = [&](int n) {
        if (p + static_cast<size_t>(n) > b.size()) throw std::runtime_error("tile textures: truncated");
        uint32_t v = 0;
        for (int i = 0; i < n; i++) v = (v << 8) | static_cast<uint8_t>(b[p++]);
        return v;
    };
    if (b.compare(0, 4, "MLTX") != 0) throw std::runtime_error("tile textures: not MLTX");
    p = 4;
    if (u(4) != 2) throw std::runtime_error("tile textures: version");
    const size_t first = textures_.size();
    for (uint32_t i = 0, count = u(4); i < count; i++) {
        auto t = std::make_unique<TileTexture>();
        t->width = static_cast<int>(u(2));
        t->height = static_cast<int>(u(2));
        size_t bytes = static_cast<size_t>(t->width) * static_cast<size_t>(t->height) * 4;
        if (p + bytes > b.size()) throw std::runtime_error("tile textures: truncated");
        t->rgba.assign(b.begin() + static_cast<long>(p), b.begin() + static_cast<long>(p + bytes));
        p += bytes;
        textures_.push_back(std::move(t));
    }
    for (uint32_t i = 0, count = u(4); i < count; i++) {
        uint32_t len = u(2);
        if (p + len > b.size()) throw std::runtime_error("tile textures: truncated");
        std::string key = b.substr(p, len);
        p += len;
        size_t texture = first + u(4);
        if (texture >= textures_.size()) throw std::runtime_error("tile textures: bad index");
        byKey_[key] = textures_[texture].get();
    }
}

void TileTextures::Clear()
{
    byKey_.clear();
    textures_.clear();
}

const TileTexture* TileTextures::Find(const std::string& key) const
{
    auto it = byKey_.find(key);
    return it == byKey_.end() ? nullptr : it->second;
}

uint32_t RoomTexture::Crc() const
{
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t v : rgba) crc = table[(crc ^ v) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

namespace {

// DungeonRoomGenerator.SpriteTransform: a texture at a position, with its x/y scale (+-1).
struct Placed {
    const TileTexture* tex;
    float x, y;
    int scaleX, scaleY;
};

// A cleared generator texture: (255, 255, 255, 0).
RoomTexture Blank()
{
    RoomTexture out;
    out.width = kTextureWidth;
    out.height = kTextureHeight;
    out.rgba.assign(static_cast<size_t>(kTextureWidth) * kTextureHeight * 4, 0);
    for (size_t i = 0; i < out.rgba.size(); i += 4) out.rgba[i] = out.rgba[i + 1] = out.rgba[i + 2] = 255;
    return out;
}

// FillTextureWithSpriteTransforms (not the outline variant).
void FillWithTransforms(RoomTexture& out, const std::vector<Placed>& placed)
{
    for (const Placed& p : placed) {
        const int w = p.tex->width, h = p.tex->height;
        const int ix = static_cast<int>(p.x), iy = static_cast<int>(p.y);
        for (int r = 0; r < h; r++)
            for (int c = 0; c < w; c++) {
                // (the mirror is width - j, one past the texture's last column: the game's)
                int x = ix + (p.scaleX == -1 ? (w - c) : c) + kBorder;
                int y = iy + (p.scaleY == -1 ? (h - r) : r) + kBorder;
                if (x < 0 || x >= kTextureWidth || y < 0 || y >= kTextureHeight) continue;
                const uint8_t* color = &p.tex->rgba[(static_cast<size_t>(r) * static_cast<size_t>(w) + static_cast<size_t>(c)) * 4];
                uint8_t* dst = &out.rgba[(static_cast<size_t>(y) * kTextureWidth + static_cast<size_t>(x)) * 4];
                if (color[3] > dst[3] || (color[3] == dst[3] && Darker(color, dst))) std::memcpy(dst, color, 4);
            }
    }
}

} // namespace

RoomTexture PaintHoles(const std::vector<TileSprite>& tiles, const TileTextures& textures)
{
    const TileTexture* hole = textures.Find("holeSprite#0");
    if (tiles.empty() || !hole) return RoomTexture();
    const float tileSize = Constants::GetFloat("tileSize");
    const float num = static_cast<float>(hole->width) / 2.0f;
    std::vector<Placed> placed;
    for (const TileSprite& t : tiles) {
        const char* field = Field(t.list);
        if (!field) continue;
        const TileTexture* tex = textures.Find(std::string(field) + "#" + std::to_string(t.index < 0 ? 0 : t.index));
        if (!tex) continue; // a null sprite: AddTileSprite skips it
        // AddTileSprite: the texture's corner from the tile's centre.
        float vx = t.scaleX == 1 ? -num : -(static_cast<float>(tex->width) - num + 1.0f);
        float vy = t.scaleY == 1 ? -num : -(static_cast<float>(tex->height) - num + 1.0f);
        placed.push_back({tex, (static_cast<float>(t.x) + 0.5f) * tileSize + vx, (static_cast<float>(t.y) + 0.5f) * tileSize + vy, t.scaleX, t.scaleY});
    }
    RoomTexture out = Blank();
    FillWithTransforms(out, placed);
    return out;
}

RoomTexture PaintSlime(const std::vector<TileSprite>& tiles, int tile, const TileTextures& common)
{
    if (tiles.empty()) return RoomTexture();
    const char* list = tile == 16 ? "slimeSprites" : tile == 64 ? "acidSprites" : tile == 128 ? "lavaSprites" : "electricSprites";
    const float num = Constants::GetFloat("tileSize");
    std::vector<Placed> placed;
    for (const TileSprite& t : tiles) {
        const TileTexture* tex = common.Find(std::string(list) + "#" + std::to_string(t.index));
        if (!tex) continue;
        bool flag = t.scaleX == -1;
        float x = static_cast<float>(t.x) * num - static_cast<float>(tex->width) * 0.5f + num * 0.5f + static_cast<float>(flag ? -1 : 0);
        float y = static_cast<float>(t.y) * num - static_cast<float>(tex->height) * 0.5f + num * 0.5f;
        placed.push_back({tex, x, y, t.scaleX, 1});
    }
    RoomTexture out = Blank();
    FillWithTransforms(out, placed);
    return out;
}

RoomTexture PaintDetailsLayer(const LayerPaint& paint, const std::string& prefix, const TileTextures& textures, const uint8_t blank[3])
{
    if (paint.Empty()) return RoomTexture();
    const int W = kRoomFloorWidthPx, H = kRoomFloorHeightPx;
    RoomTexture out;
    out.width = W;
    out.height = H;
    out.rgba.resize(static_cast<size_t>(W) * H * 4);
    for (size_t i = 0; i < out.rgba.size(); i += 4) {
        out.rgba[i] = blank[0];
        out.rgba[i + 1] = blank[1];
        out.rgba[i + 2] = blank[2];
        out.rgba[i + 3] = 0;
    }
    for (const LayerPaint::Entry& e : paint.entries) {
        const TileTexture* tex = textures.Find(prefix + "#" + std::to_string(e.sprite));
        if (!tex) continue;
        const int w = tex->width, h = tex->height;
        // Mathf.Round: to the nearest, halves to even.
        const int x0 = static_cast<int>(std::nearbyint(static_cast<double>(e.x - static_cast<float>(w) / 2.0f + static_cast<float>(W) / 2.0f)));
        const int y0 = static_cast<int>(std::nearbyint(static_cast<double>(e.y - static_cast<float>(h) / 2.0f + static_cast<float>(H) / 2.0f)));
        for (int k = 0; k < h; k++)
            for (int l = 0; l < w; l++) {
                int x = x0 + (e.flip ? l : (w - l)), y = y0 + k;
                if (x >= W - 2 || x < 2 || y >= H - 2 || y < 2) continue;
                const uint8_t* color = &tex->rgba[(static_cast<size_t>(k) * static_cast<size_t>(w) + static_cast<size_t>(l)) * 4];
                if (color[3] != 0) std::memcpy(&out.rgba[(static_cast<size_t>(y) * W + static_cast<size_t>(x)) * 4], color, 4);
            }
    }
    return out;
}

} // namespace game
