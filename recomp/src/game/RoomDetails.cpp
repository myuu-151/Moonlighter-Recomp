#include "game/RoomDetails.h"

#include <set>
#include <utility>

#include "unity/Random.h"
#include "unity/Vector2.h"

namespace game {

namespace Random = unity::Random;
using unity::Rect;
using unity::Vector2;

namespace {

// List.GetRandom: one Range(0, Count) draw, none (default) when empty.
int GetRandom(int count) { return count == 0 ? -1 : Random::Range(0, count); }

// Dictionary<Vector2, Sprite> as SpawnDetails uses it: insertion order, overwrite on equal key.
struct SpriteDict {
    std::vector<std::pair<Vector2, int>> entries;
    void Set(Vector2 key, int sprite)
    {
        for (auto& e : entries)
            if (unity::Equals(e.first, key)) {
                e.second = sprite;
                return;
            }
        entries.emplace_back(key, sprite);
    }
};

// FillRoomWithSprites: one flip draw per entry, in enumeration order; what it paints.
LayerPaint FillRoomWithSprites(const SpriteDict& dict)
{
    LayerPaint paint;
    for (const auto& [key, sprite] : dict.entries) {
        LayerPaint::Entry e;
        e.x = key.x;
        e.y = key.y;
        e.sprite = sprite;
        e.flip = Random::Range(0.0f, 1.0f) > 0.5f;
        paint.entries.push_back(e);
    }
    return paint;
}

// FillRoomTextureWithSprites: one draw per probability-image pixel, a pick on each hit.
LayerPaint FillRoomTextureWithSprites(const DetailsData::ProbImage& prob, int detailsCount, const DetailsRoom& room, int& hits)
{
    SpriteDict dict;
    float vx = static_cast<float>(room.roomFloorWidth) / static_cast<float>(prob.w);
    float vy = static_cast<float>(room.roomFloorHeight) / static_cast<float>(prob.h);
    Vector2 offset(static_cast<float>(room.roomFloorWidth) * -0.5f, static_cast<float>(room.roomFloorHeight) * -0.5f);
    const float percentLearnedAjustment = 0.5f;
    for (int i = 0; i < prob.w; i++) {
        for (int j = 0; j < prob.h; j++) {
            float r = static_cast<float>(prob.red[static_cast<size_t>(i + j * prob.w)]) / 255.0f;
            float threshold = r * percentLearnedAjustment;
            if (Random::Range(0.0f, 1.0f) <= threshold) {
                int sprite = GetRandom(detailsCount);
                float kx = static_cast<float>(i) * vx;
                float ky = static_cast<float>(j) * vy;
                dict.Set(Vector2(kx, ky) + offset, sprite);
                hits++;
            }
        }
    }
    if (!dict.entries.empty()) return FillRoomWithSprites(dict);
    return LayerPaint();
}

int FillRoomZonesWithSprites(const std::vector<Rect>& zones, const std::vector<DetailsData::Size>& details, float generationProb, int perZone,
                             LayerPaint& paint)
{
    SpriteDict dict;
    int placed = 0;
    for (const Rect& z : zones) {
        for (int i = 0; i < perZone; i++) {
            if (Random::Range(0.0f, 1.0f) <= generationProb) {
                int sprite = GetRandom(static_cast<int>(details.size()));
                const DetailsData::Size& s = details.at(static_cast<size_t>(sprite));
                if (s.null) throw cs::NullReferenceException(); // random.texture on a null sprite
                float halfW = static_cast<float>(s.w) / 2.0f;
                float halfH = static_cast<float>(s.h) / 2.0f;
                Rect rect{z.x + halfW, z.y + halfH, z.width - static_cast<float>(s.w), z.height - static_cast<float>(s.h)};
                float kx = Random::Range(rect.x, rect.x + rect.width);
                float ky = Random::Range(rect.y, rect.y + rect.height);
                dict.Set(Vector2(kx, ky), sprite);
                placed++;
            }
        }
    }
    if (!dict.entries.empty()) paint = FillRoomWithSprites(dict);
    return placed;
}

// Extensions.Intersect (with FlipNegative; sizes here are never negative).
bool Intersect(const Rect& a, const Rect& b)
{
    bool f1 = a.x < b.x + b.width;
    bool f2 = a.x + a.width > b.x;
    bool f3 = a.y < b.y + b.height;
    bool f4 = a.y + a.height > b.y;
    return f1 & f2 & f3 & f4;
}

int FillRoomWithBreakables(const std::vector<Rect>& zones, int breakables, int perZone, float probability, std::vector<DetailsResult::Placed>& picks)
{
    std::vector<Rect> placed;
    Rect rect{0.0f, 0.0f, 24.0f, 24.0f}; // rect.size = Vector2.one * 24
    const Vector2 vector(24.0f, 24.0f);
    const Vector2 half = vector * 0.5f;
    int count = 0;
    for (const Rect& z : zones) {
        Rect inner{z.x + half.x, z.y + half.y, z.width - vector.x, z.height - vector.y};
        for (int i = 0; i < perZone; i++) {
            if (!(Random::Range(0.0f, 1.0f) < probability)) continue;
            float px = Random::Range(inner.x, inner.x + inner.width);
            float py = Random::Range(inner.y, inner.y + inner.height);
            rect.x = px - half.x;
            rect.y = py - half.y;
            bool overlap = false;
            for (const Rect& r : placed)
                if (Intersect(r, rect)) {
                    overlap = true;
                    break;
                }
            if (!overlap) {
                placed.push_back(rect);
                picks.push_back({GetRandom(breakables), px, py});
                count++;
            }
        }
    }
    return count;
}

int FillRoomWithCreatures(const std::vector<Rect>& zones, int creatures, int perZone, float probability, std::vector<DetailsResult::Placed>& picks)
{
    int count = 0;
    for (const Rect& z : zones) {
        for (int i = 0; i < perZone; i++) {
            if (Random::Range(0.0f, 1.0f) < probability) {
                // Named arguments evaluate in source order: position (x, y), then original.
                float x = Random::Range(z.x, z.x + z.width);
                float y = Random::Range(z.y, z.y + z.height);
                picks.push_back({GetRandom(creatures), x, y});
                count++;
            }
        }
    }
    return count;
}

} // namespace

DetailsResult SpawnDetails(const DetailsRoom& room, const DetailsData& data, DetailsCache& cache)
{
    DetailsResult res;
    if (room.culture == Culture::Unknown) return res;

    if (data.walls > 0 && room.spawners >= 0) {
        float num = 1.0f - room.wallProb;
        for (int i = 0; i < room.spawners; i++) {
            if (Random::value() > num) {
                res.wallPicks.emplace_back(i, GetRandom(data.walls));
                res.walls++;
            }
        }
    }

    bool corrupted = room.culture != room.floorCulture;
    auto layer = [&](const std::vector<DetailsData::Size>& sprites, const std::vector<DetailsData::ProbImage>& probs, int& index, int& hits,
                     LayerPaint* slots, LayerPaint& paint) {
        if (sprites.empty()) return;
        if ((index < DetailsCache::kLength) | corrupted) {
            int pick = GetRandom(static_cast<int>(probs.size()));
            if (pick >= 0 && !probs[static_cast<size_t>(pick)].null) {
                paint = FillRoomTextureWithSprites(probs[static_cast<size_t>(pick)], static_cast<int>(sprites.size()), room, hits);
                if (!paint.Empty() && !corrupted) slots[index++] = paint;
            }
        } else {
            paint = slots[Random::Range(0, DetailsCache::kLength)]; // reuse a cached texture
        }
    };
    layer(data.lower, data.lowerProb, cache.lowerDetailsIndex, res.lowerHits, cache.lower, res.lower);
    layer(data.lower2, data.lower2Prob, cache.lower2DetailsIndex, res.lower2Hits, cache.lower2, res.lower2);

    if (!data.middle.empty()) res.middle = FillRoomZonesWithSprites(room.zones, data.middle, room.middleProb, room.middlePerZone, res.middleLayer);

    // The roll is drawn before the list check (left operand first).
    if (Random::Range(0.0f, 1.0f) < room.particlesProb && data.particles > 0) {
        res.particlePick = GetRandom(data.particles);
        res.particles = true;
    }
    if (data.breakables > 0)
        res.breakables = FillRoomWithBreakables(room.zones, data.breakables, room.breakablesPerZone, room.breakablesProb, res.breakablePicks);
    if (data.creatures > 0) res.creatures = FillRoomWithCreatures(room.zones, data.creatures, room.creaturesPerZone, room.creaturesProb, res.creaturePicks);
    return res;
}

std::map<std::pair<int, int>, DetailsData> LoadDetailsData(const json::Value& extracted)
{
    static const std::string b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    auto names = [](const json::Value& v) {
        std::vector<std::string> out;
        for (const json::Value& x : v.arr) out.push_back(x.IsNull() ? std::string() : x.s);
        return out;
    };
    auto sizes = [](const json::Value& v) {
        std::vector<DetailsData::Size> out;
        for (const json::Value& x : v.arr) {
            DetailsData::Size s;
            if (x.IsNull()) s.null = true;
            else {
                s.w = static_cast<int>(x["w"].i);
                s.h = static_cast<int>(x["h"].i);
            }
            out.push_back(s);
        }
        return out;
    };
    auto images = [](const json::Value& v) {
        std::vector<DetailsData::ProbImage> out;
        for (const json::Value& x : v.arr) {
            DetailsData::ProbImage im;
            if (x.IsNull()) {
                im.null = true;
                out.push_back(im);
                continue;
            }
            im.w = static_cast<int>(x["w"].i);
            im.h = static_cast<int>(x["h"].i);
            uint32_t buf = 0;
            int bits = 0;
            for (char c : x["red"].s) {
                size_t pos = b64.find(c);
                if (pos == std::string::npos) continue;
                buf = (buf << 6) | static_cast<uint32_t>(pos);
                bits += 6;
                if (bits >= 8) {
                    bits -= 8;
                    im.red.push_back(static_cast<uint8_t>((buf >> bits) & 0xFF));
                }
            }
            out.push_back(im);
        }
        return out;
    };
    std::map<std::pair<int, int>, DetailsData> out;
    for (const json::Value& r : extracted.arr) {
        DetailsData& d = out[{static_cast<int>(r["culture"].i), static_cast<int>(r["level"].i)}];
        d.wallNames = names(r["walls"]);
        d.particleNames = names(r["particles"]);
        d.creatureNames = names(r["creatures"]);
        d.breakableNames = names(r["breakables"]);
        d.walls = static_cast<int>(d.wallNames.size());
        d.particles = static_cast<int>(d.particleNames.size());
        d.creatures = static_cast<int>(d.creatureNames.size());
        d.breakables = static_cast<int>(d.breakableNames.size());
        d.lower = sizes(r["lower"]);
        d.lower2 = sizes(r["lower2"]);
        d.middle = sizes(r["middle"]);
        d.lowerProb = images(r["lowerProb"]);
        d.lower2Prob = images(r["lower2Prob"]);
    }
    return out;
}

std::vector<Rect> CreateMiddleDetailZones(const std::vector<int>& grid, int roomX, int roomY, float tileSize, int genX, int genY)
{
    std::vector<Rect> zones;
    std::set<std::pair<int, int>> used;
    auto inGrid = [&](int i, int j) { return i > -1 && j > -1 && i < roomX && j < roomY; };
    auto blocked = [&](int i, int j) {
        if (!inGrid(i, j)) return false;
        int v = grid.at(static_cast<size_t>(i + j * genX));
        return v >= 1 && v <= 10;
    };
    auto horizontal = [&](int i, int j, int height, int width) {
        for (int k = 0; k <= height; k++) {
            int x = i + width + 1, y = j + k;
            if (!inGrid(x, y) || blocked(x, y) || used.count({x, y})) return false;
        }
        return true;
    };
    auto vertical = [&](int i, int j, int height, int width) {
        for (int k = 0; k <= width; k++) {
            int x = i + k, y = j + height + 1;
            if (!inGrid(x, y) || blocked(x, y) || used.count({x, y})) return false;
        }
        return true;
    };
    for (int i = 0; i < roomY; i++) {
        for (int j = 0; j < roomX; j++) {
            if (blocked(j, i) || used.count({j, i})) continue;
            int h = 0, w = 0;
            bool doneW = false, doneH = false;
            while (!doneW || !doneH) {
                if (!doneW && horizontal(j, i, h, w)) w++;
                else doneW = true;
                if (!doneH && vertical(j, i, h, w)) h++;
                else doneH = true;
            }
            if (w < 2 || h < 2) continue;
            float x = (static_cast<float>(j) - static_cast<float>(genX) * 0.5f) * tileSize;
            float y = (static_cast<float>(i) - static_cast<float>(genY) * 0.5f) * tileSize;
            zones.push_back({x, y, static_cast<float>(w + 1) * tileSize, static_cast<float>(h + 1) * tileSize});
            for (int k = 0; k <= w; k++)
                for (int l = 0; l <= h; l++) used.insert({k + j, l + i});
        }
    }
    return zones;
}

} // namespace game
