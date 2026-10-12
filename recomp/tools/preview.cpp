// Floor preview on the PC: generates floors the way the GameCube build does (game/Session
// over data/extracted), draws each room from its draw list (render/DrawList) with a small
// software rasterizer, and writes PNGs: one per room (640 x 360, the game's view) and the
// floor's map (every room, a quarter size).
//
// Usage: preview <data dir> <out dir> [culture 1-4] [level 0-2] [seed] [boss defeated 0/1]

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "game/RoomScene.h"
#include "game/Session.h"
#include "render/DrawList.h"
#include "render/RoomDraw.h"
#include "render/SpriteBank.h"

using namespace game;
using namespace render;

namespace {

class FileSource : public DataSource {
public:
    explicit FileSource(std::string root) : root_(std::move(root)) {}
    std::string Read(const std::string& path) override
    {
        std::ifstream f(root_ + "/" + path, std::ios::binary);
        if (!f) throw std::runtime_error("missing data file: " + path);
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

private:
    std::string root_;
};

// ---- PNG (stored deflate blocks: no compression library needed)

uint32_t Crc(const uint8_t* p, size_t n, uint32_t c = 0xFFFFFFFFu)
{
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return c;
}

void WritePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba)
{
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba.begin() + static_cast<long>(y) * w * 4, rgba.begin() + static_cast<long>(y + 1) * w * 4);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t off = 0; off < raw.size(); off += 65535) {
        size_t n = std::min<size_t>(65535, raw.size() - off);
        z.push_back(off + n == raw.size() ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n));
        z.push_back(static_cast<uint8_t>(~n >> 8));
        z.insert(z.end(), raw.begin() + static_cast<long>(off), raw.begin() + static_cast<long>(off + n));
    }
    uint32_t adler = (b << 16) | a;
    for (int s = 24; s >= 0; s -= 8) z.push_back(static_cast<uint8_t>(adler >> s));
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) {
        uint8_t c[4] = {static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)};
        f.write(reinterpret_cast<char*>(c), 4);
    };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        u32(static_cast<uint32_t>(data.size()));
        std::vector<uint8_t> td(type, type + 4);
        td.insert(td.end(), data.begin(), data.end());
        f.write(reinterpret_cast<const char*>(td.data()), static_cast<long>(td.size()));
        u32(Crc(td.data(), td.size()) ^ 0xFFFFFFFFu);
    };
    f.write("\x89PNG\r\n\x1a\n", 8);
    std::vector<uint8_t> ihdr = {static_cast<uint8_t>(w >> 24), static_cast<uint8_t>(w >> 16), static_cast<uint8_t>(w >> 8), static_cast<uint8_t>(w),
                                 static_cast<uint8_t>(h >> 24), static_cast<uint8_t>(h >> 16), static_cast<uint8_t>(h >> 8), static_cast<uint8_t>(h),
                                 8, 6, 0, 0, 0};
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
}

// ---- A software rasterizer over draw lists

struct Canvas {
    int w, h;
    float left, top, scale; // world x at pixel 0, world y at row 0, pixels per unit
    std::vector<uint8_t> px;
    Canvas(int w_, int h_, float left_, float top_, float scale_) : w(w_), h(h_), left(left_), top(top_), scale(scale_), px(static_cast<size_t>(w_) * h_ * 4)
    {
        for (size_t i = 0; i < px.size(); i += 4) px[i + 3] = 255;
    }
};

std::map<std::string, std::vector<uint8_t>> gDecoded;

const std::vector<uint8_t>& Texels(DataSource& data, const SpriteBank& bank, const Sprite& s)
{
    std::string key = s.folder + s.name;
    auto it = gDecoded.find(key);
    if (it != gDecoded.end()) return it->second;
    return gDecoded[key] = SpriteBank::DecodeRGBA(s, data.Read(bank.TexelsPath(s)));
}

void Draw(Canvas& cv, DataSource& data, const SpriteBank& bank, const DrawQuad& q)
{
    // A sprite's texels (rows from the top), or a painted texture's (rows from the bottom).
    int width, height;
    const uint8_t* texels;
    bool bottomUp = false;
    if (q.sprite) {
        width = q.sprite->width;
        height = q.sprite->height;
        texels = Texels(data, bank, *q.sprite).data();
    } else {
        width = q.texture->width;
        height = q.texture->height;
        texels = q.texture->rgba.data();
        bottomUp = true;
    }
    // Pixel -> (u, v) on the quad: inverse of the parallelogram (corner 0, edges 0->1, 0->3).
    float ex = q.x[1] - q.x[0], ey = q.y[1] - q.y[0], fx = q.x[3] - q.x[0], fy = q.y[3] - q.y[0];
    float det = ex * fy - ey * fx;
    if (std::fabs(det) < 1e-9f) return;
    float minx = std::min({q.x[0], q.x[1], q.x[2], q.x[3]}), maxx = std::max({q.x[0], q.x[1], q.x[2], q.x[3]});
    float miny = std::min({q.y[0], q.y[1], q.y[2], q.y[3]}), maxy = std::max({q.y[0], q.y[1], q.y[2], q.y[3]});
    int px0 = std::max(0, static_cast<int>(std::floor((minx - cv.left) * cv.scale)));
    int px1 = std::min(cv.w - 1, static_cast<int>(std::ceil((maxx - cv.left) * cv.scale)));
    int py0 = std::max(0, static_cast<int>(std::floor((cv.top - maxy) * cv.scale)));
    int py1 = std::min(cv.h - 1, static_cast<int>(std::ceil((cv.top - miny) * cv.scale)));
    for (int py = py0; py <= py1; py++)
        for (int pxx = px0; pxx <= px1; pxx++) {
            float wx = cv.left + (static_cast<float>(pxx) + 0.5f) / cv.scale - q.x[0];
            float wy = cv.top - (static_cast<float>(py) + 0.5f) / cv.scale - q.y[0];
            float u = (wx * fy - wy * fx) / det, v = (ex * wy - ey * wx) / det;
            if (u < 0 || u >= 1 || v < 0 || v >= 1) continue;
            int tx = static_cast<int>(u * static_cast<float>(width)), ty = static_cast<int>(v * static_cast<float>(height));
            if (!bottomUp) ty = height - 1 - ty;
            const uint8_t* t = &texels[(static_cast<size_t>(ty) * static_cast<size_t>(width) + static_cast<size_t>(tx)) * 4];
            float a = static_cast<float>(t[3]) / 255.0f * q.color[3];
            if (a <= 0) continue;
            uint8_t* d = &cv.px[(static_cast<size_t>(py) * static_cast<size_t>(cv.w) + static_cast<size_t>(pxx)) * 4];
            for (int c = 0; c < 3; c++) d[c] = static_cast<uint8_t>(static_cast<float>(t[c]) * q.color[c] * a + static_cast<float>(d[c]) * (1 - a));
        }
}

} // namespace

int main(int argc, char** argv)
{
    std::string dataDir = argc > 1 ? argv[1] : "data/extracted";
    std::string outDir = argc > 2 ? argv[2] : "data/preview";
    Culture culture = static_cast<Culture>(argc > 3 ? std::atoi(argv[3]) : 1);
    int level = argc > 4 ? std::atoi(argv[4]) : 0;
    int seed = argc > 5 ? std::atoi(argv[5]) : 1;
    bool bossDefeated = argc > 6 && std::atoi(argv[6]) != 0;
    try {
        FileSource data(dataDir);
        Session session(data);
        session.Load();
        session.Start(seed);
        std::unique_ptr<Floor> floor;
        for (int l = 0; l <= level; l++) floor = session.Generate(culture, l, bossDefeated);
        SpriteBank bank;
        bank.Load(data, std::string("sprites/") + Session::PackName(culture) + "/",
                  {"sprites/golem/", "sprites/forest/", "sprites/desert/", "sprites/tech/"});
        std::map<Culture, TileTextures> tiles;
        TileTextures common;
        common.Load(data.Read("tiles/common.bin"));

        std::vector<std::string> missing;
        int minX = 1 << 30, minY = 1 << 30, maxX = -(1 << 30), maxY = -(1 << 30);
        for (const PlacedCell& c : floor->dungeon->cells) {
            minX = std::min(minX, c.x);
            maxX = std::max(maxX, c.x);
            minY = std::min(minY, c.y);
            maxY = std::max(maxY, c.y);
        }
        const float quarter = 0.25f;
        Canvas map((maxX - minX + 1) * 160, (maxY - minY + 1) * 90, static_cast<float>(minX) * kRoomSizeX - kRoomSizeX / 2,
                   static_cast<float>(maxY) * kRoomSizeY + kRoomSizeY / 2, quarter);
        int n = 0;
        for (const PlacedCell& c : floor->dungeon->cells) {
            unity::Vector2 at = RoomPosition(c.x, c.y);
            Culture rc = c.room->roomInfo->culture;
            if (!tiles.count(rc)) Session::LoadTiles(data, rc, level, tiles[rc]);
            RoomDraw draw = BuildRoomDraw(*c.room, at, level, bank, tiles[rc], common, &missing);
            Canvas room(640, 360, at.x - 320, at.y + 180, 1.0f);
            for (const DrawQuad& q : draw.quads) {
                Draw(room, data, bank, q);
                Draw(map, data, bank, q);
            }
            char name[256];
            std::snprintf(name, sizeof name, "%s/room_%02d_%s.png", outDir.c_str(), n++, c.info->name.c_str());
            WritePng(name, room.w, room.h, room.px);
        }
        WritePng(outDir + "/floor.png", map.w, map.h, map.px);
        std::sort(missing.begin(), missing.end());
        missing.erase(std::unique(missing.begin(), missing.end()), missing.end());
        std::printf("%s floor %d (seed %d): %zu rooms; %zu sprites not in the bank%s\n", Session::PackName(culture), level + 1, seed,
                    floor->dungeon->cells.size(), missing.size(), missing.empty() ? "" : ":");
        for (size_t i = 0; i < missing.size() && i < 20; i++) std::printf("    %s\n", missing[i].c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "preview: %s\n", e.what());
        return 1;
    }
    return 0;
}
