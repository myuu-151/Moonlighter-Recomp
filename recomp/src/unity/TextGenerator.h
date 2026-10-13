#pragma once
// UnityEngine.TextGenerator's native side (Unity 2019.2: ScriptingTextGenerator::GetStringRenderInfo
// and TextRenderingPrivate::NativeTextGenerator), what UGUI's Text lays its string out with:
// TextGenerator.Populate_Internal and the vertices, characters, lines, extents and best-fit size
// read back. The managed TextGenerator (its cache of the last string and settings), UnityEngine.UI's
// Text (OnPopulateMesh's 1/pixelsPerUnit and pixel snap) are C#, translated.
//
// The rules (read from UnityPlayer.dll's x64 code with its public symbols; all float math single
// precision in the engine's order, no fused multiply-add; R(v) is the pixel rounding
// floorf(v + 0.5f), the engine's at one pixel per point):
//
//  - Sizes. fontSize (the font's own for 0), resizeTextMinSize and max(min, max) are each scaled by
//    the scale factor and floored, at most 500. The extents become scale * extents + 0.0001f; an
//    axis whose overflow mode is Overflow lays out against FLT_MAX.
//  - Best fit (on, and not both axes Overflow): the size clamped to [min, max] first, then a
//    binary search: a size that fits raises the low bound to it, one that doesn't lowers the high
//    bound below it, the next size is (lo + 1 + hi) / 2; it stops when lo >= hi after a
//    generation and keeps that last one (which may not fit). "Fits": no word had to be broken
//    mid-word, no line ran below the box.
//  - Rich text: <b> <i> <color=..> <size=N> <material=N> <quad ...> (and <a>), matched without
//    case on the low byte of each UTF-16 unit; tags must nest exactly (a closing tag closes the
//    innermost open one of its kind) or the whole string is plain text; a tag's characters stay
//    as invisible zero-width slots. <color> takes #RGB, #RGBA, #RRGGBB, #RRGGBBAA or one of 23
//    names (else white), alpha included, not multiplied; <size=N> is (int)(N * scale * ratio).
//  - The pen moves by R(advance) (the glyph's cached advance times the font's tracking); only ' '
//    and '\t' are white space, only '\n' breaks a line; a tab goes to the next multiple of
//    (int)(4 * the space's advance). Kerning (a font with a kerning table) shifts the pen by
//    R(value * size / the font's size).
//  - Wrap (horizontal Wrap): a glyph whose advance takes the pen past the width breaks the line
//    before its word (the spaces before it stay on the old line), or before itself when the line
//    has no word start (one long word, CJK), which doesn't "fit".
//  - Lines: the line's largest character size gives its metrics, S(m) = m * size / the font's
//    size: ascent R(S(m_Ascent)), descent R(S(m_Descent)), line height R(S(m_LineSpacing)),
//    leading = height - (ascent - descent). The first baseline is at R(ascent) (no line
//    spacing); each next one R(((ascent - previous descent) + leading) * lineSpacing) below. A
//    line whose bottom is below the box (vertical Truncate) is dropped with everything after it.
//    UILineInfo: height (int)S(m_LineSpacing), topY the baseline less the ascent, leading.
//  - Alignment: each line shifted by -R(minX), -R((maxX + minX) * 0.5f) or -R(maxX) of its
//    characters (trailing spaces count after a '\n', not after a wrap); then the whole text placed
//    in the rect by the anchor and pivot, rounded with R, y flipped up.
//  - A glyph's quad is its cached quad rect, each edge rounded with R, at the pen; v0 top-left,
//    v1 top-right, v2 bottom-right, v3 bottom-left; its uv the cached uv rect's corners (v0 and
//    v2 swapped for a glyph stored turned); the colour the current format's. Italic and bold are
//    the font's glyphs (FreeType's), no shear.
//  - Output: when the generation fits, the glyph (and <quad>) quads only, by material then string
//    order; when it doesn't, a quad for every slot (4 * (length + 1), or up to the dropped line),
//    the slots without a glyph degenerate with no colour or uv of their own (uninitialised in
//    Unity: QuadDefined() tells them apart).

#include <cstdint>
#include <string>
#include <vector>

#include "unity/Font.h"
#include "unity/Rect.h"
#include "unity/RectTransform.h"
#include "unity/Vector2.h"

namespace unity {

struct Color32 {
    uint8_t r = 255, g = 255, b = 255, a = 255;
};

struct ColorRGBAf {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

struct Vector4f {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
};

struct UIVertex {
    Vector3f position;
    Vector3f normal{0.0f, 0.0f, -1.0f};
    Vector4f tangent{1.0f, 0.0f, 0.0f, -1.0f};
    Color32 color;
    Vector2 uv0, uv1, uv2, uv3;
};

struct UICharInfo {
    Vector2 cursorPos;
    float charWidth = 0.0f;
};

struct UILineInfo {
    int startCharIdx = 0;
    int height = 0;
    float topY = 0.0f;
    float leading = 0.0f;
};

enum class HorizontalWrapMode { Wrap = 0, Overflow = 1 };
enum class VerticalWrapMode { Truncate = 0, Overflow = 1 };

// UnityEngine.TextGenerationSettings (font as the port's Font).
struct TextGenerationSettings {
    const Font* font = nullptr;
    ColorRGBAf color;
    int fontSize = 0;
    float lineSpacing = 1.0f;
    bool richText = true;
    float scaleFactor = 1.0f;
    int fontStyle = 0;  // FontStyle: 0 normal, 1 bold, 2 italic, 3 bold and italic
    int textAnchor = 0; // TextAnchor: 0 upper left ... 8 lower right
    bool alignByGeometry = false;
    bool resizeTextForBestFit = false;
    int resizeTextMinSize = 10;
    int resizeTextMaxSize = 40;
    bool updateBounds = false;
    VerticalWrapMode verticalOverflow = VerticalWrapMode::Truncate;
    HorizontalWrapMode horizontalOverflow = HorizontalWrapMode::Wrap;
    Vector2 generationExtents;
    Vector2 pivot;
    bool generateOutOfBounds = false;
};

// TextGenerationError bits.
enum : uint32_t {
    kTextErrorCustomSizeOnNonDynamicFont = 1,
    kTextErrorCustomStyleOnNonDynamicFont = 2,
    kTextErrorNoFont = 4,
};

// ColorRGBA32 from a float colour, as the binding converts it: each channel clamped to [0, 1],
// (int)(v * 255 + 0.5).
Color32 ToColor32(const ColorRGBAf& c);

class TextGenerator {
public:
    // TextGenerator.Populate_Internal: lays the string (UTF-16 units) out; returns the error bits.
    uint32_t Populate(const std::u16string& text, const TextGenerationSettings& settings);

    // The managed getters: the counts are 0 unless the last Populate had a font.
    int vertexCount() const { return valid_ ? static_cast<int>(verts_.size()) : 0; }
    int characterCount() const { return valid_ ? static_cast<int>(chars_.size()) : 0; }
    int lineCount() const { return valid_ ? static_cast<int>(lines_.size()) : 0; }
    int fontSizeUsedForBestFit() const { return fontSizeFoundForBestFit_; }
    Rect rectExtents() const { return Rect{0.0f, 0.0f, extents_.x, extents_.y}; }
    // GetVerticesInternal / GetCharactersInternal / GetLinesInternal: what's there, whatever valid says.
    const std::vector<UIVertex>& verts() const { return verts_; }
    const std::vector<UICharInfo>& characters() const { return chars_; }
    const std::vector<UILineInfo>& lines() const { return lines_; }
    // Per output quad: whether its colour and uv are the generator's (false for the empty quads of
    // slots without a glyph, left in an untrimmed output: Unity's memory there is uninitialised).
    const std::vector<uint8_t>& QuadDefined() const { return quadDefined_; }

private:
    std::vector<UIVertex> verts_;
    std::vector<UICharInfo> chars_;
    std::vector<UILineInfo> lines_;
    std::vector<uint8_t> quadDefined_;
    bool valid_ = false;
    Vector2 extents_;
    int fontSizeFoundForBestFit_ = 0;
};

} // namespace unity
