// UnityEngine.TextGenerator's native side (TextGenerator.h): Unity 2019.2's
// ScriptingTextGenerator::GetStringRenderInfo, its NativeTextGenerator (Setup, GenerateVertices,
// ProcessFormatForPosition, SetCharacterPosition, InsertCharacter, WordWrap, RegisterNewLine,
// FixLineHorizontalOffset, OffsetCharacters, GetBounds, ReorderAndTrimVertices), the rich-text
// parser (GetFormatString, GetTag, GetParameter, ParseImageParameters, the colour names) and
// GetTextOffset, as the x64 player does them: every float operation single precision and in its
// order (the build has no fused multiply-add), ints truncated or floored where it does.
//
// The generator works y-down with each line aligned around x = 0; GetStringRenderInfo places the
// whole in the rect and flips y. Every UTF-16 unit is a slot (plus a closing '\n' at the end):
// slot i writes the quad at vertices 4i..4i+3.

#include "unity/TextGenerator.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace unity {

namespace {

// Unity's FloorfToInt (inlined everywhere): below 0 (or NaN) take off 0.99999994f, then truncate.
int FloorfToInt(float f)
{
    if (!(f >= 0.0f)) f = f - 0.99999994f;
    return static_cast<int>(f);
}

float MaxF(float a, float b) { return (a > b) ? a : b; } // comiss a,b; ja: a if greater, else b
float MinF(float a, float b) { return (a > b) ? b : a; }

struct TextVertex {
    Vector3f pos;
    Color32 color{0, 0, 0, 0};
    Vector2 uv;
};

// The format stack's element.
struct TextFormat {
    int style = 0;
    Color32 color;
    int size = 0;
    int material = 0;
};

// A rich-text tag (GetFormatString).
struct TextFormatChange {
    int startPos = 0;
    int skipLength = 0;
    Color32 color{255, 255, 255, 255};
    int size = 0;
    int material = 0;
    Rect imageRect{0.0f, 0.0f, 1.0f, 1.0f};
    uint32_t flags = 0x8000;
};

enum : uint32_t {
    kBold = 1,
    kItalic = 2,
    kColor = 4,
    kSize = 8,
    kMaterial = 0x10,
    kImage = 0x20,
    kClose = 0x8000,
};

struct LineInfo {
    int startCharIdx;
    int height;
    float topY;
    float leading;
};

// --- the rich-text parser ---

const char* const kTags[11] = {"b", "i", "color", "size", "material", "quad", "x", "y", "width", "height", "a"};

class Utf16 {
public:
    explicit Utf16(const std::u16string& s) : s_(s) {}
    int length() const { return static_cast<int>(s_.size()); }
    // Unity's UTF16String reads one unit past the end in places (its buffer's next unit): 0 here.
    uint16_t operator[](int i) const { return (i >= 0 && i < length()) ? static_cast<uint16_t>(s_[static_cast<size_t>(i)]) : 0; }

private:
    const std::u16string& s_;
};

// Whether the tag name `n` is at `k`: the low byte of each unit, ASCII upper case lowered.
bool NameAt(const Utf16& t, int k, const char* n)
{
    for (; *n; ++n, ++k) {
        if (k == t.length()) return false;
        uint8_t lo = static_cast<uint8_t>(t[k]);
        if (static_cast<uint8_t>(lo - 'A') <= 25) lo = static_cast<uint8_t>(lo + 0x20);
        if (lo != static_cast<uint8_t>(*n)) return false;
    }
    return true;
}

int GetTag(const Utf16& t, int& pos, bool& closing)
{
    int p = pos;
    if (t[p] != '<') return -1;
    if (p + 1 == t.length()) return -1;
    closing = (t[p + 1] == '/');
    int nameStart = p + 1 + (closing ? 1 : 0);
    for (int tag = 0; tag < 11; ++tag) {
        if (!NameAt(t, nameStart, kTags[tag])) continue;
        int e = nameStart + static_cast<int>(std::strlen(kTags[tag]));
        uint16_t ch = t[e];
        bool accept;
        if (!closing && ch == '=') accept = true;
        else if (ch == ' ') accept = (tag == 5 || tag == 10);
        else accept = (ch == '>');
        if (!accept) continue;
        pos = e;
        return tag;
    }
    return -1;
}

std::string GetParameter(const Utf16& t, int& pos, bool stopAtSpace)
{
    std::string r;
    int p = pos;
    if (t[p] == '=') {
        pos = p + 1;
        if (t[p + 1] != '>') {
            for (int k = p + 1;;) {
                if (t[k] == ' ' && stopAtSpace) break;
                if (k >= t.length()) break;
                pos = k + 1;
                r.push_back(static_cast<char>(static_cast<uint8_t>(t[k])));
                k = pos;
                if (t[k] == '>') break;
            }
        }
    }
    if (r.size() > 2 && r.front() == r.back() && (r.front() == '"' || r.front() == '\'')) r = r.substr(1, r.size() - 2);
    return r;
}

// atoi-like: white space (9..13, ' '), a sign, digits; the low 32 bits.
int StringToInt(const std::string& s)
{
    size_t i = 0;
    while (i < s.size() && ((s[i] >= 9 && s[i] <= 13) || s[i] == ' ')) ++i;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        neg = s[i] == '-';
        ++i;
    }
    uint64_t v = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') v = v * 10u + static_cast<uint64_t>(s[i++] - '0');
    int32_t low = static_cast<int32_t>(static_cast<uint32_t>(v));
    return neg ? static_cast<int32_t>(0u - static_cast<uint32_t>(low)) : low;
}

bool IsXDigit(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

int HexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

void HexStringToBytes(const char* s, size_t n, Color32& c)
{
    uint8_t* out[4] = {&c.r, &c.g, &c.b, &c.a};
    for (size_t k = 0; k < n && k < 4; ++k) *out[k] = static_cast<uint8_t>(HexValue(s[2 * k]) * 16 + HexValue(s[2 * k + 1]));
}

struct NamedColor {
    const char* name;
    Color32 c;
};

const NamedColor kColorNames[23] = {
    {"red", {0xff, 0x00, 0x00, 0xff}},       {"cyan", {0x00, 0xff, 0xff, 0xff}},      {"blue", {0x00, 0x00, 0xff, 0xff}},
    {"darkblue", {0x00, 0x00, 0xa0, 0xff}},  {"lightblue", {0xad, 0xd8, 0xe6, 0xff}}, {"purple", {0x80, 0x00, 0x80, 0xff}},
    {"yellow", {0xff, 0xff, 0x00, 0xff}},    {"lime", {0x00, 0xff, 0x00, 0xff}},      {"fuchsia", {0xff, 0x00, 0xff, 0xff}},
    {"white", {0xff, 0xff, 0xff, 0xff}},     {"silver", {0xc0, 0xc0, 0xc0, 0xff}},    {"grey", {0x80, 0x80, 0x80, 0xff}},
    {"black", {0x00, 0x00, 0x00, 0xff}},     {"orange", {0xff, 0xa5, 0x00, 0xff}},    {"brown", {0xa5, 0x2a, 0x2a, 0xff}},
    {"maroon", {0x80, 0x00, 0x00, 0xff}},    {"green", {0x00, 0x80, 0x00, 0xff}},     {"olive", {0x80, 0x80, 0x00, 0xff}},
    {"navy", {0x00, 0x00, 0x80, 0xff}},      {"teal", {0x00, 0x80, 0x80, 0xff}},      {"aqua", {0x00, 0xff, 0xff, 0xff}},
    {"magenta", {0xff, 0x00, 0xff, 0xff}},   {"transparent", {0x00, 0x00, 0x00, 0x00}},
};

bool EqualsNoCase(const std::string& a, const char* b)
{
    size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x + 32);
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y + 32);
        if (x != y) return false;
    }
    return true;
}

bool TryParseHtmlColor(const std::string& s, Color32& c)
{
    c = Color32{255, 255, 255, 255};
    if (!s.empty() && s[0] == '#') {
        size_t n = s.size();
        if (n > 9) return false;
        for (size_t k = 1; k < n; ++k)
            if (!IsXDigit(s[k])) return false;
        if (n == 4 || n == 5) {
            std::string d = "#";
            for (size_t k = 1; k < n; ++k) {
                d += s[k];
                d += s[k];
            }
            HexStringToBytes(d.c_str() + 1, d.size() / 2, c);
            return true;
        }
        if (n != 7 && n != 9) return false;
        HexStringToBytes(s.c_str() + 1, n / 2, c);
        return true;
    }
    for (const NamedColor& nc : kColorNames)
        if (EqualsNoCase(s, nc.name)) {
            c = nc.c;
            return true;
        }
    return false;
}

void ParseImageParameters(const Utf16& t, int& pos, TextFormatChange& fc)
{
    while (pos < t.length()) {
        if (t[pos] == '>') return;
        int tag = -1;
        for (int k = 0; k < 11; ++k) {
            int n = static_cast<int>(std::strlen(kTags[k]));
            if (NameAt(t, pos, kTags[k]) && t[pos + n] == '=') {
                tag = k;
                pos += n;
                break;
            }
        }
        if (tag == -1) {
            pos = pos + 1;
            continue;
        }
        switch (tag) {
        case 2: {
            std::string p = GetParameter(t, pos, true);
            fc.flags |= kColor;
            if (!TryParseHtmlColor(p, fc.color)) fc.color = Color32{255, 255, 255, 255};
            break;
        }
        case 3:
            fc.flags |= kSize;
            fc.size = StringToInt(GetParameter(t, pos, true));
            break;
        case 4:
            fc.flags |= kMaterial;
            fc.material = StringToInt(GetParameter(t, pos, true));
            break;
        case 6: fc.imageRect.x = std::strtof(GetParameter(t, pos, true).c_str(), nullptr); break;
        case 7: fc.imageRect.y = std::strtof(GetParameter(t, pos, true).c_str(), nullptr); break;
        case 8: fc.imageRect.width = std::strtof(GetParameter(t, pos, true).c_str(), nullptr); break;
        case 9: fc.imageRect.height = std::strtof(GetParameter(t, pos, true).c_str(), nullptr); break;
        default: break;
        }
    }
}

void GetFormatString(const Utf16& t, std::vector<TextFormatChange>& fcs)
{
    fcs.clear();
    for (int pos = 0; pos < t.length();) {
        int start = pos;
        bool closing = false;
        int tag = GetTag(t, pos, closing);
        if (tag == -1) {
            pos = pos + 1;
            continue;
        }
        TextFormatChange fc;
        uint32_t f;
        switch (tag) {
        case 0: f = kBold; break;
        case 1: f = kItalic; break;
        case 2:
        case 10: f = kColor; break;
        case 3: f = kSize; break;
        case 4: f = kMaterial; break;
        case 5: f = kImage; break;
        default: f = kClose; break;
        }
        fc.flags = f;
        if (closing) {
            fc.flags = f | kClose;
        } else {
            switch (tag) {
            case 2: {
                std::string p = GetParameter(t, pos, false);
                if (!TryParseHtmlColor(p, fc.color)) fc.color = Color32{255, 255, 255, 255};
                break;
            }
            case 3: fc.size = StringToInt(GetParameter(t, pos, false)); break;
            case 4: fc.material = StringToInt(GetParameter(t, pos, false)); break;
            case 5: ParseImageParameters(t, pos, fc); break;
            case 10:
                while (pos < t.length() && t[pos] != '>') pos++;
                fc.color = Color32{0x00, 0x00, 0xff, 0xff}; // opaque blue
                break;
            default: break;
            }
        }
        fc.startPos = start;
        fc.skipLength = pos - start + 1;
        fcs.push_back(fc);
        if (tag == 5) { // a quad closes itself at once (also "</quad>")
            fc.flags |= kClose;
            fc.skipLength = 0;
            fcs.push_back(fc);
        }
        pos = pos + 1;
    }
    // The tags must nest exactly (by their flags), else none of them counts.
    std::vector<uint32_t> st;
    bool ok = true;
    for (const TextFormatChange& fc : fcs) {
        if (fc.flags & kClose) {
            if (st.empty() || st.back() != (fc.flags & ~static_cast<uint32_t>(kClose))) {
                ok = false;
                break;
            }
            st.pop_back();
        } else {
            st.push_back(fc.flags);
        }
    }
    if (!ok || !st.empty()) fcs.clear();
}

// --- the generator ---

class NativeTextGenerator {
public:
    NativeTextGenerator(const std::u16string& text, const Font* font, Color32 color, int fontSize, float scaleFactor,
                        Vector2 extents, bool richText, int style, float lineSpacing, int alignment, int tabSize,
                        bool pixelCorrect, float pixelsPerPoint, float sizeScale, bool generateOutOfBounds,
                        bool alignByGeometry, std::vector<TextVertex>& verts)
        : text_(text), t_(text_), font_(font), scaleFactor_(scaleFactor), alignment_(alignment), extents_(extents),
          richText_(richText), pixelCorrect_(pixelCorrect), style_(style), tabSize_(tabSize), lineSpacing_(lineSpacing),
          sizeScale_(sizeScale), ppp_(pixelsPerPoint), generateOutOfBounds_(generateOutOfBounds),
          alignByGeometry_(alignByGeometry), verts_(verts)
    {
        int s = fontSize != 0 ? fontSize : font->fontSize;
        fontSize_ = FloorfToInt(static_cast<float>(s) * sizeScale + 0.5f);
        TextFormat base;
        base.style = style;
        base.color = color;
        base.size = FloorfToInt(static_cast<float>(s) * sizeScale + 0.5f);
        base.material = 0;
        stack_.push_back(base);
    }

    int FontSize() const { return fontSize_; }

    bool Setup();
    bool GenerateVertices(uint32_t* err);
    Rect GetBounds() const;
    void ReorderAndTrimVertices();

    const std::vector<Vector2>& CharPositions() const { return charPos_; }
    const std::vector<float>& CharWidths() const { return charWidth_; }
    const std::vector<LineInfo>& Lines() const { return lines_; }
    // Per slot whose quad is the generator's own (a glyph, a <quad>), for an untrimmed output.
    const std::vector<uint8_t>& SlotDefined() const { return slotDefined_; }
    const std::vector<std::vector<int>>& MaterialChars() const { return materialChars_; }

private:
    float R(float v) const
    {
        if (!pixelCorrect_) return v;
        float p = ppp_;
        return std::floor(p * v + 0.5f) / p;
    }
    // A metric at the font's size scaled to `size` (at most 500): (size * m) / the font's size.
    float Scaled(int size, float m) const
    {
        if (size == 0) return m;
        float fs = static_cast<float>(font_->fontSize);
        if (fs == 0.0f) return m;
        int s = size > 500 ? 500 : size;
        return (static_cast<float>(s) * m) / fs;
    }
    uint32_t Char(int i) const { return t_[i]; }
    TextVertex* Quad(int slot) { return &verts_[static_cast<size_t>(slot) * 4]; }

    bool ProcessFormatForPosition();
    void SetCharacterPosition(int i);
    bool InsertCharacter(uint32_t c, uint32_t* err);
    bool WordWrap();
    bool RegisterNewLine(int lineStart, int lineEnd, float& outDY);
    void FixLineHorizontalOffset(int lineStart, int lastChar, int lineEnd);
    void OffsetCharacters(Vector2 off, int firstSlot, int count);
    void DegenerateQuad(int slot)
    {
        TextVertex* v = Quad(slot);
        v[0].pos = cursor_;
        v[1].pos = cursor_;
        v[2].pos = cursor_;
        v[3].pos = cursor_;
    }
    std::u16string text_;
    Utf16 t_;
    const Font* font_;
    int fontSize_ = 0;
    float scaleFactor_;
    int alignment_;
    Vector2 extents_;
    bool richText_;
    bool pixelCorrect_;
    int style_;
    int tabSize_;
    float lineSpacing_;
    float sizeScale_;
    float ppp_;
    bool generateOutOfBounds_;
    bool alignByGeometry_;
    std::vector<TextVertex>& verts_;

    std::vector<TextFormatChange> formatChanges_;
    std::vector<std::vector<int>> materialChars_;
    std::vector<TextFormat> stack_;
    int formatIndex_ = 0;
    int charIndex_ = 0;
    int prevChar_ = -1;
    int wordStart_ = 0;
    int lineStart_ = 0;
    int lastWordEnd_ = 0;
    float boundsMinY_ = 0.0f;
    float boundsMaxY_ = 0.0f;
    float prevLineDescent_ = 0.0f;
    float lineWidth_ = 0.0f;
    float wordWidth_ = 0.0f;
    float wordStartX_ = 0.0f;
    int whitespaceCount_ = 0;
    float maxLineWidth_ = 0.0f;
    Vector3f cursor_;
    bool fits_ = true;
    std::vector<Vector2> charPos_;
    std::vector<float> charWidth_;
    std::vector<float> charSize_;
    std::vector<LineInfo> lines_;
    std::vector<uint8_t> slotDefined_;
};

bool NativeTextGenerator::Setup()
{
    if (richText_) GetFormatString(t_, formatChanges_);
    int matCount = 1;
    for (TextFormatChange& fc : formatChanges_) {
        if (fc.flags & kSize) {
            if (fc.size != 0) fc.size = static_cast<int>((static_cast<float>(fc.size) * scaleFactor_) * sizeScale_);
            else fc.size = static_cast<int>(static_cast<float>(fontSize_) * scaleFactor_);
        }
        if (static_cast<unsigned>(fc.material) > 7) fc.material = 0; // "Only 8 materials are allowed per ..."
        if (fc.flags & (kMaterial | kImage)) matCount = std::max(matCount, fc.material + 1);
    }
    materialChars_.assign(static_cast<size_t>(matCount), std::vector<int>());
    // (FontImpl::CacheFontForText for ' ' and the text: the port's glyphs are all there already.)
    float sp = font_->GetCharacterAdvance(' ', fontSize_, style_, ppp_, nullptr);
    if (sp == 0.0f) sp = 4.0f;
    tabSize_ = static_cast<int>(static_cast<float>(tabSize_) * sp);
    boundsMinY_ = FLT_MAX;
    boundsMaxY_ = 0.0f;
    prevLineDescent_ = 0.0f;
    cursor_ = Vector3f{};
    size_t n = static_cast<size_t>(t_.length()) + 1;
    charPos_.assign(n, Vector2{});
    charWidth_.assign(n, 0.0f);
    charSize_.assign(n, 0.0f);
    verts_.assign(n * 4, TextVertex{});
    slotDefined_.assign(n, 0);
    fits_ = true;
    return true;
}

void NativeTextGenerator::SetCharacterPosition(int i)
{
    float asc = R(Scaled(stack_.back().size, font_->ascent));
    charPos_[static_cast<size_t>(i)] = Vector2{cursor_.x, cursor_.y - asc};
}

bool NativeTextGenerator::ProcessFormatForPosition()
{
    const int len = t_.length();
    while (formatIndex_ < static_cast<int>(formatChanges_.size())) {
        const TextFormatChange& fc = formatChanges_[static_cast<size_t>(formatIndex_)];
        int i = charIndex_;
        if (i < fc.startPos) break;
        int end = fc.skipLength + i;
        if (fc.flags & kClose) {
            stack_.pop_back();
        } else {
            stack_.push_back(stack_.back());
            TextFormat& tf = stack_.back();
            if (fc.flags & kBold) tf.style |= 1;
            if (fc.flags & kItalic) tf.style |= 2;
            if (fc.flags & kColor) tf.color = fc.color;
            if (fc.flags & kSize) tf.size = fc.size;
            if (fc.flags & kMaterial) tf.material = fc.material;
        }
        if ((fc.flags & (kClose | kImage)) == kImage) { // <quad>: the '<' slot is the image
            const TextFormat& tf = stack_.back();
            float sz = R(static_cast<float>(tf.size));
            if (sz == 0.0f) {
                sz = font_->fontSize != 0 ? static_cast<float>(font_->fontSize) : font_->ascent;
                sz = R(sz);
            }
            const Rect& r = fc.imageRect;
            float w = R((r.width / r.height) * sz);
            float x = cursor_.x, y = cursor_.y, z = cursor_.z + 0.0f;
            float x0 = x + 0.0f, x1 = x + w, yT = y + (-sz), yB = y + 0.0f;
            TextVertex* v = Quad(i);
            v[0].pos = Vector3f{x0, yT, z};
            v[0].uv = Vector2{r.x, r.height + r.y};
            v[1].pos = Vector3f{x1, yT, z};
            v[1].uv = Vector2{r.x + r.width, r.height + r.y};
            v[2].pos = Vector3f{x1, yB, z};
            v[2].uv = Vector2{r.x + r.width, r.y};
            v[3].pos = Vector3f{x0, yB, z};
            v[3].uv = Vector2{r.x, r.y};
            for (int k = 0; k < 4; ++k) v[k].color = tf.color;
            slotDefined_[static_cast<size_t>(i)] = 1;
            materialChars_[static_cast<size_t>(tf.material)].push_back(i);
            charPos_[static_cast<size_t>(i)] = Vector2{cursor_.x, cursor_.y};
            charWidth_[static_cast<size_t>(i)] = w;
            charSize_[static_cast<size_t>(i)] = sz;
            if (whitespaceCount_ > 0) {
                wordStart_ = i;
                wordStartX_ = cursor_.x;
                whitespaceCount_ = 0;
            }
            if (extents_.x > 0.0f && w + cursor_.x > extents_.x)
                if (!WordWrap()) return false;
            charIndex_++;
            prevChar_ = 0;
            wordWidth_ = w + wordWidth_;
            cursor_.x = w + cursor_.x;
            lineWidth_ = w + lineWidth_;
        }
        for (int k = charIndex_; k < end && k < len; k = ++charIndex_) { // the tag's characters: hidden
            DegenerateQuad(k);
            SetCharacterPosition(k);
            charWidth_[static_cast<size_t>(k)] = 0.0f;
            charSize_[static_cast<size_t>(k)] = 0.0f;
        }
        formatIndex_++;
    }
    charSize_[static_cast<size_t>(charIndex_)] = static_cast<float>(stack_.back().size);
    return true;
}

bool NativeTextGenerator::InsertCharacter(uint32_t c, uint32_t* err)
{
    int i = charIndex_;
    const TextFormat f = stack_.back();
    Rect vert, uv;
    bool flipped = false;
    font_->GetCharacterRenderInfo(c, f.size, f.style, ppp_, vert, uv, flipped, err);
    float adv = font_->GetCharacterAdvance(c, f.size, f.style, ppp_, nullptr);
    adv = R(adv);
    charWidth_[static_cast<size_t>(i)] = adv;
    float w, h;
    if (vert.width == 0.0f) {
        vert.width = adv;
        vert.height = 0.0f;
        w = adv;
        h = 0.0f;
    } else {
        w = vert.width;
        h = vert.height;
    }
    float top = R(-vert.y);
    float bottom = R(-(vert.y + h));
    float left = R(vert.x);
    float right = R(vert.x + w);

    if (!font_->kerning.empty() && prevChar_ != -1) {
        Font::KerningPair key{static_cast<uint16_t>(prevChar_), static_cast<uint16_t>(c), 0.0f};
        auto it = std::lower_bound(font_->kerning.begin(), font_->kerning.end(), key,
                                   [](const Font::KerningPair& a, const Font::KerningPair& b) {
                                       return a.first != b.first ? a.first < b.first : a.second < b.second;
                                   });
        if (it != font_->kerning.end() && it->first == key.first && it->second == key.second) {
            float k = it->value;
            if (fontSize_ != 0) {
                float s = static_cast<float>(f.size);
                float sc = (s != 0.0f && font_->fontSize != 0) ? s / static_cast<float>(font_->fontSize) : 1.0f;
                k = k * sc;
            }
            k = R(k);
            cursor_.x = k + cursor_.x;
            lineWidth_ = k + lineWidth_;
            wordWidth_ = k + wordWidth_;
        }
    }
    if (font_->characterPadding == 0 && font_->textureWidth > 0 && font_->textureHeight > 0) {
        float hp = 0.5f / ppp_;
        left = left - hp;
        right = right + hp;
        top = top - hp;
        bottom = bottom + hp;
        float su = (uv.width >= 0.0f ? 1.0f : -1.0f) / static_cast<float>(font_->textureWidth);
        float sv = (uv.height >= 0.0f ? 1.0f : -1.0f) / static_cast<float>(font_->textureHeight);
        uv.x = uv.x - su * 0.5f;
        uv.width = uv.width + su;
        uv.y = uv.y - sv * 0.5f;
        uv.height = uv.height + sv;
    }
    float x = cursor_.x, y = cursor_.y, z = cursor_.z + 0.0f;
    TextVertex* v = Quad(i);
    v[0].pos = Vector3f{left + x, top + y, z};
    v[1].pos = Vector3f{right + x, top + y, z};
    v[2].pos = Vector3f{right + x, bottom + y, z};
    v[3].pos = Vector3f{left + x, bottom + y, z};
    Vector2 uTL{uv.x, uv.y + uv.height};
    Vector2 uTR{uv.x + uv.width, uv.y + uv.height};
    Vector2 uBR{uv.x + uv.width, uv.y};
    Vector2 uBL{uv.x, uv.y};
    v[flipped ? 2 : 0].uv = uTL;
    v[1].uv = uTR;
    v[flipped ? 0 : 2].uv = uBR;
    v[3].uv = uBL;
    for (int k = 0; k < 4; ++k) v[k].color = f.color;
    slotDefined_[static_cast<size_t>(i)] = 1;
    materialChars_[static_cast<size_t>(f.material)].push_back(i);
    if (whitespaceCount_ > 0) {
        wordStart_ = i;
        wordStartX_ = cursor_.x;
        whitespaceCount_ = 0;
    }
    if (extents_.x > 0.0f && adv + cursor_.x > extents_.x)
        if (!WordWrap()) return false;
    prevChar_ = static_cast<int>(c);
    wordWidth_ = adv + wordWidth_;
    cursor_.x = adv + cursor_.x;
    lineWidth_ = adv + lineWidth_;
    return true;
}

bool NativeTextGenerator::WordWrap()
{
    int i = charIndex_, lastChar;
    if (wordStart_ == lineStart_) { // no word start on the line: break before this character
        wordStart_ = i;
        wordWidth_ = 0.0f;
        wordStartX_ = cursor_.x;
        lastChar = i - 1;
        lastWordEnd_ = i - 1;
        fits_ = false;
    } else {
        lastChar = lastWordEnd_;
    }
    FixLineHorizontalOffset(lineStart_, lastChar, wordStart_ - 1);
    maxLineWidth_ = MaxF(wordStartX_, maxLineWidth_);
    float dy = 0.0f;
    if (!RegisterNewLine(lineStart_, wordStart_, dy)) return false;
    Vector2 off{-wordStartX_, dy};
    OffsetCharacters(off, wordStart_, i - wordStart_ + 1);
    lineWidth_ = wordWidth_;
    lineStart_ = lastWordEnd_ = wordStart_;
    cursor_.x = cursor_.x - wordStartX_;
    wordStartX_ = 0.0f;
    return true;
}

void NativeTextGenerator::OffsetCharacters(Vector2 off, int firstSlot, int count)
{
    for (int k = 0; k < count * 4; ++k) {
        TextVertex& v = verts_[static_cast<size_t>(firstSlot) * 4 + static_cast<size_t>(k)];
        v.pos.x = off.x + v.pos.x;
        v.pos.y = v.pos.y + off.y;
    }
    for (int k = 0; k < count; ++k) {
        Vector2& p = charPos_[static_cast<size_t>(firstSlot + k)];
        p.x = off.x + p.x;
        p.y = p.y + off.y;
    }
}

void NativeTextGenerator::FixLineHorizontalOffset(int lineStart, int lastChar, int lineEnd)
{
    if (lastChar < lineStart) return;
    int count = lineEnd - lineStart + 1;
    float minX, maxX;
    if (alignByGeometry_) {
        minX = verts_[static_cast<size_t>(lineStart) * 4].pos.x;
        maxX = verts_[static_cast<size_t>(lastChar) * 4 + 1].pos.x;
    } else {
        minX = charPos_[static_cast<size_t>(lineStart)].x;
        maxX = charWidth_[static_cast<size_t>(lastChar)] + charPos_[static_cast<size_t>(lastChar)].x;
    }
    float off;
    switch (alignment_) {
    case 0: off = minX; break;
    case 1: off = (maxX + minX) * 0.5f; break;
    case 2: off = maxX; break;
    default: return;
    }
    off = R(off);
    OffsetCharacters(Vector2{-off, 0.0f}, lineStart, count);
}

bool NativeTextGenerator::RegisterNewLine(int lineStart, int lineEnd, float& outDY)
{
    float maxSize = 0.0f;
    for (int k = lineStart; k < lineEnd; ++k) maxSize = MaxF(maxSize, charSize_[static_cast<size_t>(k)]);
    int size = static_cast<int>(maxSize);
    float asc = R(Scaled(size, font_->ascent));
    float desc = R(Scaled(size, font_->descent));
    float ls = R(Scaled(size, font_->lineSpacing));
    float leading = ls - (asc - desc);
    float dy;
    if (lines_.empty()) dy = R(asc);
    else dy = R(((asc - prevLineDescent_) + leading) * lineSpacing_);
    outDY = dy;
    for (int k = lineStart * 4; k < lineEnd * 4; ++k) {
        TextVertex& v = verts_[static_cast<size_t>(k)];
        v.pos.x = v.pos.x + 0.0f;
        v.pos.y = dy + v.pos.y;
    }
    for (int k = lineStart; k < lineEnd; ++k) {
        Vector2& p = charPos_[static_cast<size_t>(k)];
        p.x = p.x + 0.0f;
        p.y = dy + p.y;
    }
    cursor_.y = cursor_.y + dy;
    if (cursor_.y - desc > extents_.y) { // the line's bottom below the box
        fits_ = false;
        if (!generateOutOfBounds_) {
            verts_.resize(static_cast<size_t>(lineStart) * 4);
            charPos_.resize(static_cast<size_t>(lineStart));
            charWidth_.resize(static_cast<size_t>(lineStart));
            charSize_.resize(static_cast<size_t>(lineStart));
            return false;
        }
    }
    if (alignByGeometry_) {
        for (int k = lineStart * 4; k < lineEnd * 4; ++k) {
            float y = verts_[static_cast<size_t>(k)].pos.y;
            boundsMinY_ = (boundsMinY_ > y) ? y : boundsMinY_;
            boundsMaxY_ = (y > boundsMaxY_) ? y : boundsMaxY_;
        }
    } else {
        float extra = font_->useLegacyBounds ? leading : 0.0f;
        float t = (cursor_.y - asc) - extra;
        boundsMinY_ = (boundsMinY_ > t) ? t : boundsMinY_;
        float b = cursor_.y - desc;
        boundsMaxY_ = (b > boundsMaxY_) ? b : boundsMaxY_;
    }
    prevLineDescent_ = desc;
    float hF = Scaled(size, font_->lineSpacing);
    lines_.push_back(LineInfo{lineStart_, static_cast<int>(hF), cursor_.y - asc, leading});
    return true;
}

bool NativeTextGenerator::GenerateVertices(uint32_t* err)
{
    const int len = t_.length();
    charIndex_ = 0;
    do {
        if (!ProcessFormatForPosition()) return false;
        int i = charIndex_;
        uint32_t c = (i < len) ? Char(i) : '\n';
        SetCharacterPosition(i);
        if (c == '\t') {
            if (whitespaceCount_++ == 0) lastWordEnd_ = i;
            int tab = tabSize_;
            float x = cursor_.x;
            int n = FloorfToInt(x / static_cast<float>(tab)) + 1;
            float nx = static_cast<float>(tab * n);
            nx = R(nx);
            charWidth_[static_cast<size_t>(i)] = nx - x;
            lineWidth_ = nx;
            cursor_.x = nx;
            DegenerateQuad(i);
            wordWidth_ = 0.0f;
            if (extents_.x > 0.0f && static_cast<float>(n * tab) > extents_.x)
                if (!WordWrap()) return false;
        } else if (c == '\n') {
            DegenerateQuad(i);
            charWidth_[static_cast<size_t>(i)] = 0.0f;
            FixLineHorizontalOffset(lineStart_, i - 1, i);
            maxLineWidth_ = MaxF(lineWidth_, maxLineWidth_);
            float dy = 0.0f;
            if (RegisterNewLine(lineStart_, i + 1, dy)) {
                cursor_.x = 0.0f;
                lastWordEnd_ = wordStart_ = lineStart_ = i + 1;
                lineWidth_ = 0.0f;
                whitespaceCount_ = 0;
            } else {
                fits_ = fits_ && (i == len);
                if (!generateOutOfBounds_) return fits_;
            }
        } else if (c == ' ') {
            const TextFormat& f = stack_.back();
            float adv = font_->GetCharacterAdvance(' ', f.size, f.style, ppp_, nullptr);
            adv = R(adv);
            TextVertex* v = Quad(i);
            v[3].pos = cursor_;
            v[0].pos = v[3].pos;
            v[2].pos = Vector3f{adv + cursor_.x, cursor_.y + 0.0f, cursor_.z + 0.0f};
            v[1].pos = v[2].pos;
            charWidth_[static_cast<size_t>(i)] = adv;
            if (whitespaceCount_++ == 0) lastWordEnd_ = i - 1;
            wordWidth_ = 0.0f;
            lineWidth_ = adv + lineWidth_;
            cursor_.x = adv + cursor_.x;
        } else {
            if (!InsertCharacter(c, err)) return false;
        }
        charIndex_++;
    } while (charIndex_ <= len);
    return fits_;
}

Rect NativeTextGenerator::GetBounds() const
{
    Rect r;
    r.x = 0.0f;
    r.width = 0.0f;
    float top = boundsMinY_;
    if (pixelCorrect_) top = R(top);
    r.y = top;
    float bottom = boundsMaxY_;
    if (pixelCorrect_) bottom = R(bottom);
    r.height = bottom - top;
    float w = maxLineWidth_;
    switch (alignment_) {
    case 0: r.width = pixelCorrect_ ? R(w) : w; break;
    case 1: {
        float h = w * 0.5f;
        if (pixelCorrect_) h = R(h);
        r.x = -h;
        r.width = pixelCorrect_ ? R(w) : w;
        break;
    }
    case 2: {
        float v = pixelCorrect_ ? R(w) : w;
        r.x = -v;
        r.width = std::fabs(-v);
        break;
    }
    default: break;
    }
    return r;
}

void NativeTextGenerator::ReorderAndTrimVertices()
{
    std::vector<TextVertex> out;
    for (const std::vector<int>& l : materialChars_)
        for (int c : l)
            for (int k = 0; k < 4; ++k) out.push_back(verts_[static_cast<size_t>(c) * 4 + static_cast<size_t>(k)]);
    verts_ = std::move(out);
}

// GetTextOffset: where the generator's text (aligned around x = 0, from y = 0 down) goes in the
// rect, by the anchor and pivot, rounded.
Vector3f GetTextOffset(Vector2 ext, const Rect& r, int anchor, Vector2 pivot, bool pixelCorrect)
{
    float x;
    switch (anchor) {
    case 0: case 3: case 6: x = 0.0f; break;
    case 1: case 4: case 7: x = ext.x * 0.5f; break;
    case 2: case 5: case 8: x = ext.x; break;
    default: x = 0.0f; break;
    }
    x = x - ext.x * pivot.x;
    float y;
    switch (anchor) {
    case 0: case 1: case 2: y = -r.y; break;
    case 3: case 4: case 5: y = (ext.y - r.height) * 0.5f - r.y; break;
    case 6: case 7: case 8: y = ext.y - (r.height + r.y); break;
    default: y = 0.0f; break;
    }
    y = y - (1.0f - pivot.y) * ext.y;
    if (pixelCorrect) {
        x = std::floor(x + 0.5f);
        y = std::floor(y + 0.5f);
    }
    return Vector3f{x, y, 0.0f};
}

uint8_t ChannelTo8(float v)
{
    if (0.0f > v) v = 0.0f;
    v = (1.0f < v) ? 1.0f : v;
    return static_cast<uint8_t>(static_cast<int64_t>(v * 255.0f + 0.5f));
}

} // namespace

Color32 ToColor32(const ColorRGBAf& c)
{
    return Color32{ChannelTo8(c.r), ChannelTo8(c.g), ChannelTo8(c.b), ChannelTo8(c.a)};
}

uint32_t TextGenerator::Populate(const std::u16string& text, const TextGenerationSettings& s)
{
    uint32_t error = 0;
    valid_ = false;
    const Font* font = s.font;
    if (font == nullptr) return kTextErrorNoFont;
    Color32 color = ToColor32(s.color);
    int minSize = s.resizeTextMinSize, maxSize = s.resizeTextMaxSize;
    int maxS = (minSize <= maxSize) ? maxSize : minSize;
    int anchor = s.textAnchor;
    int alignment = 0;
    if (static_cast<unsigned>(anchor) <= 7 && ((0x92u >> anchor) & 1)) alignment = 1;
    else if (static_cast<unsigned>(anchor) <= 8 && ((0x124u >> anchor) & 1)) alignment = 2;
    const float eps = 1.0f * 0.0001f;
    float extX = s.scaleFactor * s.generationExtents.x + eps;
    float extY = s.scaleFactor * s.generationExtents.y + eps;
    Vector2 layoutExt{extX, extY};
    bool vOverflow = s.verticalOverflow == VerticalWrapMode::Overflow;
    bool hOverflow = s.horizontalOverflow == HorizontalWrapMode::Overflow;
    if (vOverflow) layoutExt.y = FLT_MAX;
    if (hOverflow) layoutExt.x = FLT_MAX;
    bool doBestFit = (vOverflow && hOverflow) ? false : s.resizeTextForBestFit;

    int fs = s.fontSize;
    if (fs == 0 && font->dynamic()) fs = font->fontSize;
    int size = std::min(FloorfToInt(static_cast<float>(fs) * s.scaleFactor), 500);
    int lo = std::min(FloorfToInt(static_cast<float>(minSize) * s.scaleFactor), 500);
    int hi = std::min(FloorfToInt(static_cast<float>(maxS) * s.scaleFactor), 500);
    if (doBestFit) {
        if (size < lo) size = lo;
        else if (size > hi) size = hi;
    }

    // UTF16String stops at the first NUL (the managed string goes through UTF-8 and strlen).
    std::u16string units = text;
    size_t nul = units.find(u'\0');
    if (nul != std::u16string::npos) units.resize(nul);

    const int initial = size;
    int cur = size;
    const float fInitial = static_cast<float>(initial);
    std::vector<TextVertex> tverts;
    std::unique_ptr<NativeTextGenerator> gen;
    bool ok = false;
    for (;;) {
        float ratio = (fInitial == 0.0f) ? 1.0f : static_cast<float>(cur) / fInitial;
        tverts.clear();
        gen.reset(new NativeTextGenerator(units, font, color, initial, s.scaleFactor, layoutExt, s.richText, s.fontStyle,
                                          s.lineSpacing, alignment, 4, true, 1.0f, ratio, s.generateOutOfBounds,
                                          s.alignByGeometry, tverts));
        ok = gen->Setup() ? gen->GenerateVertices(&error) : false;
        if (!(doBestFit && lo < hi)) break;
        if (ok) lo = cur;
        else hi = cur - 1;
        cur = (lo + 1 + hi) / 2;
    }
    fontSizeFoundForBestFit_ = cur;
    Rect bounds = gen->GetBounds();
    if (vOverflow && s.updateBounds) extY = bounds.height;
    if (hOverflow && s.updateBounds) extX = bounds.width;
    extents_ = Vector2{extX, extY};

    quadDefined_.clear();
    if (ok) {
        gen->ReorderAndTrimVertices();
        quadDefined_.assign(tverts.size() / 4, 1);
    } else {
        const std::vector<uint8_t>& slots = gen->SlotDefined();
        for (size_t q = 0; q < tverts.size() / 4; ++q) quadDefined_.push_back(q < slots.size() ? slots[q] : 0);
    }

    Vector3f off = GetTextOffset(extents_, bounds, anchor, s.pivot, true);
    verts_.assign(tverts.size(), UIVertex{});
    for (size_t i = 0; i < tverts.size(); ++i) {
        const TextVertex& sv = tverts[i];
        UIVertex& d = verts_[i];
        d.position.x = off.x + sv.pos.x;
        d.position.y = (off.y + sv.pos.y) * -1.0f;
        d.position.z = off.z + sv.pos.z;
        d.color = sv.color;
        d.uv0 = sv.uv;
    }
    const std::vector<Vector2>& pos = gen->CharPositions();
    const std::vector<float>& widths = gen->CharWidths();
    const std::vector<LineInfo>& lines = gen->Lines();
    chars_.assign(pos.size(), UICharInfo{});
    for (size_t i = 0; i < pos.size(); ++i) {
        UICharInfo& c = chars_[i];
        c.cursorPos.x = off.x + pos[i].x;
        c.cursorPos.y = (off.y + pos[i].y) * -1.0f;
        c.charWidth = widths[i];
    }
    lines_.assign(lines.size(), UILineInfo{});
    for (size_t i = 0; i < lines.size(); ++i) {
        UILineInfo& d = lines_[i];
        d.startCharIdx = lines[i].startCharIdx;
        d.height = lines[i].height;
        d.topY = (off.y + lines[i].topY) * -1.0f;
        d.leading = lines[i].leading;
    }
    valid_ = true;
    return error;
}

} // namespace unity
