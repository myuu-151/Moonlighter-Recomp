#pragma once
// UnityEngine.Font as the text generator reads it (Unity 2019.2's TextRendering::Font and
// FontImpl): the font asset's metrics and the glyphs of its dynamic atlas.
//
// The game's fonts are dynamic TrueType: Unity renders glyphs with FreeType into the font's
// texture as texts ask for them (FontImpl::CacheFontForText). The port doesn't render: its
// glyphs are the game's own, recorded by the font probe (`-oracle-fonts`, fonts.jsonl: every
// glyph's CharacterInfo and texels at the sizes and styles the game uses), and the layout reads
// them exactly as Unity's FontImpl does:
//
//  - a glyph is found by (character, size, style, scale), all equal: the size 0 means the font's
//    own (m_FontSize), sizes are capped at 500; a dynamic font takes any size and style, the others
//    only their own size and style 0 (and flag the error bits 1 and 2 otherwise);
//  - its advance is the cached advance (rounded to whole pixels when the font says so, at caching)
//    times m_Tracking; its quad and uv rects are the cached ones, bit for bit (the quad: x, top (y
//    up), width, negative height; the uv: the top-left corner and a negative height; `flipped`
//    when the atlas holds it turned);
//  - a glyph the font hasn't got has empty rects and no advance.
//
// What the font asset gives (data/extracted/ui/fonts.json): m_FontSize (the size the metrics are
// for; serialized as a float, read truncated), m_LineSpacing and m_Ascent (at that size; the
// layout scales them by size / m_FontSize), m_Tracking, m_CharacterPadding, m_AsciiStartOffset,
// m_ConvertCase (-2: dynamic), m_UseLegacyBoundsCalculation.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "unity/Rect.h"

namespace unity {

// FontImpl::CharacterInfo: a glyph of the font's atlas (Font.characterInfo, the raw fields of
// UnityEngine.CharacterInfo: uv, vert, width (the advance, without tracking), size, style,
// flipped).
struct CharacterInfo {
    uint32_t index = 0; // the character (minus m_AsciiStartOffset)
    Rect uv;            // in the atlas: x, top, width, negative height
    Rect vert;          // the quad from the pen on the baseline, y up: x, top, width, negative height
    float advance = 0.0f;
    int size = 0;
    int style = 0;      // FontStyle bits: 1 bold, 2 italic
    float scale = 1.0f;
    bool flipped = false; // stored turned a quarter in the atlas
};

class Font {
public:
    std::string name;
    int fontSize = 0;            // m_FontSize
    float lineSpacing = 0.0f;    // m_LineSpacing (at m_FontSize)
    float ascent = 0.0f;         // m_Ascent (at m_FontSize)
    float descent = 0.0f;        // m_Descent
    float tracking = 1.0f;       // m_Tracking
    int characterPadding = 1;    // m_CharacterPadding
    int asciiStartOffset = 0;    // m_AsciiStartOffset
    int convertCase = -2;        // m_ConvertCase: -2 dynamic, 1 to upper, 2 to lower, others as is
    bool useLegacyBounds = false; // m_UseLegacyBoundsCalculation
    // m_KerningValues, sorted by (first, second): only fonts that have a table (the built-in Arial).
    struct KerningPair {
        uint16_t first = 0, second = 0;
        float value = 0.0f;
    };
    std::vector<KerningPair> kerning;
    // The atlas texture's size (only read for a font without character padding).
    int textureWidth = 0, textureHeight = 0;

    bool dynamic() const { return convertCase == -2; }
    // Sorts the kerning table as Unity's vector_map keeps it (the first of equal pairs found).
    void SortKerning();

    // The atlas' glyphs; a glyph not there is looked up in `glyphFallback`'s when it's set (the
    // test: a recorded atlas's glyphs, with their uv, over the font probe's).
    const Font* glyphFallback = nullptr;
    void ClearGlyphs() { glyphs_.clear(); }
    void AddGlyph(const CharacterInfo& info);
    size_t GlyphCount() const { return glyphs_.size(); }

    // FontImpl's lookups. `error` (may be null) gets 1 for a size and 2 for a style a non-dynamic
    // font hasn't got.
    const CharacterInfo* Find(uint32_t c, int size, int style, float scale, uint32_t* error) const;
    float GetCharacterAdvance(uint32_t c, int size, int style, float scale, uint32_t* error) const;
    void GetCharacterRenderInfo(uint32_t c, int size, int style, float scale, Rect& vert, Rect& uv, bool& flipped,
                                uint32_t* error) const;
    bool HasCharacterInTexture(uint32_t c, int size, int style, float scale, uint32_t* error) const
    {
        return Find(c, size, style, scale, error) != nullptr;
    }

private:
    struct Key {
        uint32_t index;
        int size;
        int style;
        uint32_t scaleBits;
        bool operator==(const Key& o) const
        {
            return index == o.index && size == o.size && style == o.style && scaleBits == o.scaleBits;
        }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const
        {
            return (static_cast<size_t>(k.index) * 1000003u) ^ (static_cast<size_t>(k.size) * 131u) ^
                   (static_cast<size_t>(k.style) << 20) ^ (static_cast<size_t>(k.scaleBits) * 7u);
        }
    };
    static uint32_t ScaleBits(float scale);
    Key MakeKey(uint32_t c, int size, int style, float scale, uint32_t* error) const;

    std::unordered_map<Key, CharacterInfo, KeyHash> glyphs_;
};

} // namespace unity
