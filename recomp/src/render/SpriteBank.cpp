#include "render/SpriteBank.h"

#include <cstring>
#include <stdexcept>

namespace render {

namespace {

// Big-endian reads (the files are written for the GameCube).
struct Reader {
    const std::string& b;
    size_t p = 0;
    uint32_t U(int n)
    {
        if (p + static_cast<size_t>(n) > b.size()) throw std::runtime_error("sprite index: truncated");
        uint32_t v = 0;
        for (int i = 0; i < n; i++) v = (v << 8) | static_cast<uint8_t>(b[p++]);
        return v;
    }
    float F()
    {
        uint32_t u = U(4);
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
};

void Rgb5a3(uint16_t v, uint8_t* out)
{
    if (v & 0x8000) { // 1 RRRRR GGGGG BBBBB
        out[0] = static_cast<uint8_t>(((v >> 10) & 31) * 255 / 31);
        out[1] = static_cast<uint8_t>(((v >> 5) & 31) * 255 / 31);
        out[2] = static_cast<uint8_t>((v & 31) * 255 / 31);
        out[3] = 255;
    } else { // 0 AAA RRRR GGGG BBBB
        out[3] = static_cast<uint8_t>(((v >> 12) & 7) * 255 / 7);
        out[0] = static_cast<uint8_t>(((v >> 8) & 15) * 17);
        out[1] = static_cast<uint8_t>(((v >> 4) & 15) * 17);
        out[2] = static_cast<uint8_t>((v & 15) * 17);
    }
}

} // namespace

void SpriteBank::Load(game::DataSource& data, const std::string& folder, const std::vector<std::string>& others)
{
    folder_ = folder;
    byName_.clear();
    LoadIndex(data, folder);
    for (const std::string& other : others)
        if (other != folder) LoadIndex(data, other);
}

void SpriteBank::LoadIndex(game::DataSource& data, const std::string& folder)
{
    std::string b = data.Read(folder + "index.bin");
    Reader r{b};
    if (b.compare(0, 4, "MLSI") != 0) throw std::runtime_error("sprite index: not MLSI");
    r.p = 4;
    if (r.U(4) != 2) throw std::runtime_error("sprite index: version");
    uint32_t count = r.U(4);
    for (uint32_t i = 0; i < count; i++) {
        Sprite s;
        uint32_t len = r.U(2);
        s.name = b.substr(r.p, len);
        s.folder = folder;
        r.p += len;
        s.index = static_cast<int>(i);
        s.width = static_cast<int>(r.U(2));
        s.height = static_cast<int>(r.U(2));
        s.left = r.F();
        s.bottom = r.F();
        s.unitsPerPixel = r.F();
        s.format = static_cast<TexFormat>(r.U(1));
        byName_.emplace(s.name, s);
    }
}

const Sprite* SpriteBank::Find(const std::string& name) const
{
    auto it = byName_.find(name);
    return it == byName_.end() ? nullptr : &it->second;
}

size_t SpriteBank::TexelBytes(const Sprite& s)
{
    if (s.format == TexFormat::CI8) return static_cast<size_t>((s.width + 7) / 8 * 8) * static_cast<size_t>((s.height + 3) / 4 * 4) + 512;
    return static_cast<size_t>((s.width + 3) / 4 * 4) * static_cast<size_t>((s.height + 3) / 4 * 4) * 2;
}

std::vector<uint8_t> SpriteBank::DecodeRGBA(const Sprite& s, const std::string& t)
{
    std::vector<uint8_t> out(static_cast<size_t>(s.width) * static_cast<size_t>(s.height) * 4);
    auto put = [&](int x, int y, uint16_t v) {
        if (x < s.width && y < s.height) Rgb5a3(v, &out[(static_cast<size_t>(y) * static_cast<size_t>(s.width) + static_cast<size_t>(x)) * 4]);
    };
    auto u16 = [&](size_t at) { return static_cast<uint16_t>((static_cast<uint8_t>(t[at]) << 8) | static_cast<uint8_t>(t[at + 1])); };
    if (t.size() < TexelBytes(s)) return out;
    size_t p = 0;
    if (s.format == TexFormat::CI8) {
        int tw = (s.width + 7) / 8 * 8, th = (s.height + 3) / 4 * 4;
        size_t palette = static_cast<size_t>(tw) * static_cast<size_t>(th);
        for (int ty = 0; ty < th; ty += 4)
            for (int tx = 0; tx < tw; tx += 8)
                for (int y = ty; y < ty + 4; y++)
                    for (int x = tx; x < tx + 8; x++) put(x, y, u16(palette + static_cast<uint8_t>(t[p++]) * 2));
    } else {
        int tw = (s.width + 3) / 4 * 4, th = (s.height + 3) / 4 * 4;
        for (int ty = 0; ty < th; ty += 4)
            for (int tx = 0; tx < tw; tx += 4)
                for (int y = ty; y < ty + 4; y++)
                    for (int x = tx; x < tx + 4; x++, p += 2) put(x, y, u16(p));
    }
    return out;
}

} // namespace render
