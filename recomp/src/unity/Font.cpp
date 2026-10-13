// UnityEngine.Font's glyph lookups (Font.h), as FontImpl does them in Unity 2019.2
// (FontImpl::GetCharacterAdvance, GetCharacterRenderInfo, HasCharacterInTexture).

#include "unity/Font.h"

#include <algorithm>
#include <cstring>

namespace unity {

uint32_t Font::ScaleBits(float scale)
{
    if (scale == 0.0f) scale = 0.0f; // -0 and 0 compare equal, as Unity's float compare does
    uint32_t u;
    std::memcpy(&u, &scale, 4);
    return u;
}

// The cache's key for a character: a non-dynamic font's case conversion (a byte, sign-extended),
// minus m_AsciiStartOffset; the size (0: the font's own; at most 500); a non-dynamic font only at
// its own size and style 0 (else size 0 and the error bits), and at scale 1.
Font::Key Font::MakeKey(uint32_t c, int size, int style, float scale, uint32_t* error) const
{
    int code = static_cast<int>(c);
    if (convertCase == 1) {
        uint8_t b = static_cast<uint8_t>(c);
        uint8_t r = static_cast<uint8_t>(b - 'a') <= 25 ? static_cast<uint8_t>(b - 0x20) : b;
        code = static_cast<int8_t>(r);
    } else if (convertCase == 2) {
        uint8_t b = static_cast<uint8_t>(c);
        uint8_t r = static_cast<uint8_t>(b - 'A') <= 25 ? static_cast<uint8_t>(b + 0x20) : b;
        code = static_cast<int8_t>(r);
    }
    int sz = size != 0 ? size : fontSize;
    if (sz > 500) sz = 500;
    float keyScale = 1.0f;
    if (!dynamic()) {
        if (sz != fontSize) {
            sz = 0;
            if (error) *error |= 1;
        }
        if (style != 0) {
            style = 0;
            if (error) *error |= 2;
        }
    } else {
        keyScale = scale;
    }
    return Key{static_cast<uint32_t>(code - asciiStartOffset), sz, style, ScaleBits(keyScale)};
}

void Font::SortKerning()
{
    std::stable_sort(kerning.begin(), kerning.end(), [](const KerningPair& a, const KerningPair& b) {
        return a.first != b.first ? a.first < b.first : a.second < b.second;
    });
}

void Font::AddGlyph(const CharacterInfo& info)
{
    glyphs_[Key{info.index, info.size, info.style, ScaleBits(info.scale)}] = info;
}

const CharacterInfo* Font::Find(uint32_t c, int size, int style, float scale, uint32_t* error) const
{
    auto it = glyphs_.find(MakeKey(c, size, style, scale, error));
    if (it != glyphs_.end()) return &it->second;
    return glyphFallback ? glyphFallback->Find(c, size, style, scale, nullptr) : nullptr;
}

float Font::GetCharacterAdvance(uint32_t c, int size, int style, float scale, uint32_t* error) const
{
    const CharacterInfo* info = Find(c, size, style, scale, error);
    return info ? info->advance * tracking : 0.0f;
}

void Font::GetCharacterRenderInfo(uint32_t c, int size, int style, float scale, Rect& vert, Rect& uv, bool& flipped,
                                  uint32_t* error) const
{
    const CharacterInfo* info = Find(c, size, style, scale, error);
    if (info) {
        vert = info->vert;
        uv = info->uv;
        flipped = info->flipped;
    } else {
        vert = Rect{};
        uv = Rect{};
        flipped = false;
    }
}

} // namespace unity
