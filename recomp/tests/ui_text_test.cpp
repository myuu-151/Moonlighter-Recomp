// UGUI text layout against the game (unity/TextGenerator.h): the text oracle's recording
// (`-oracle-text`, data/oracle/text.jsonl; skipped if there's none) holds TextGenerator.Populate
// on the game's own inputs: every Text of the loaded scenes and of the UI prefabs with its own
// settings (and Text.preferredWidth/Height's variants), every localized Text in every language
// (I2's translation, the language's font and FontSizeManager's size), item names and
// descriptions in every language, and a synthetic grid (alignment, overflow, wrap, rich text,
// best fit, line spacing, scale, pivot, colour). Each row has the settings, the string, the font
// atlas's glyphs of the string's characters as they were (their uv rects: the atlas is Unity's
// own, filled as the game went), and what came out.
//
// Each generation is redone by unity::TextGenerator with the font asset's metrics
// (data/extracted/ui/fonts.json) and the row's glyphs, and compared bit for bit: the result,
// the best fit's size, the counts, the extents, every line (start, height, top, leading), every
// character (cursor position, width), every vertex (position, colour, uv0, uv1, and normal,
// tangent, uv2, uv3). Where Unity's output keeps the quads of slots without a glyph (a
// generation that doesn't fit isn't trimmed), those quads' colour and uv are uninitialised
// memory in Unity: their positions are compared, their colour and uv counted apart.
//
// With the font probe's recording (`-oracle-fonts`, data/oracle/fonts.jsonl) also: every glyph
// the layouts used against the font probe's (quad rect, advance, flipped), as the port takes
// its glyphs from the font probe.
//
// Usage: ui_text_test <text.jsonl> <fonts.json> [<fonts.jsonl>]   (ML_TEXT_DEBUG=1: details)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "unity/Font.h"
#include "unity/TextGenerator.h"
#include "util/Json.h"

using namespace unity;

namespace {

float FromBits(int64_t v)
{
    uint32_t u = static_cast<uint32_t>(v);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

bool Same(float a, float b) { return Bits(a) == Bits(b); }

std::vector<uint8_t> Base64(const std::string& s)
{
    static int8_t table[256];
    static bool init = false;
    if (!init) {
        std::memset(table, -1, sizeof table);
        const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) table[static_cast<uint8_t>(a[i])] = static_cast<int8_t>(i);
        init = true;
    }
    std::vector<uint8_t> out;
    out.reserve(s.size() * 3 / 4);
    uint32_t acc = 0;
    int bits = 0;
    for (char ch : s) {
        int v = table[static_cast<uint8_t>(ch)];
        if (v < 0) continue; // '=' padding
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xff));
        }
    }
    return out;
}

class Bytes {
public:
    explicit Bytes(std::vector<uint8_t> b) : b_(std::move(b)) {}
    bool done() const { return p_ >= b_.size(); }
    size_t left() const { return b_.size() - p_; }
    template <typename T> T Get()
    {
        T v{};
        if (p_ + sizeof(T) > b_.size()) throw std::runtime_error("truncated record");
        std::memcpy(&v, &b_[p_], sizeof(T));
        p_ += sizeof(T);
        return v;
    }
    void Skip(size_t n) { p_ += n; }

private:
    std::vector<uint8_t> b_;
    size_t p_ = 0;
};

// UTF-8 to UTF-16 as Unity's ConvertUTF8toUTF16 (utfcpp's unchecked next: no validation; code
// points above 0xFFFF as surrogate pairs).
std::u16string Utf16(const std::string& s)
{
    std::u16string out;
    size_t i = 0;
    while (i < s.size()) {
        uint8_t c = static_cast<uint8_t>(s[i]);
        uint32_t cp;
        int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1f; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0f; n = 3; }
        else { cp = c & 0x07; n = 4; }
        for (int k = 1; k < n && i + static_cast<size_t>(k) < s.size(); ++k) cp = (cp << 6) | (static_cast<uint8_t>(s[i + static_cast<size_t>(k)]) & 0x3f);
        i += static_cast<size_t>(n);
        if (cp > 0xffff) {
            out.push_back(static_cast<char16_t>(0xD7C0 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3ff)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

// A glyph record (TextOracle.WriteGlyph).
struct Glyph {
    uint16_t ch = 0;
    bool found = false;
    CharacterInfo info;
};

Glyph ReadGlyph(Bytes& b)
{
    Glyph g;
    g.ch = b.Get<uint16_t>();
    g.found = b.Get<uint8_t>() != 0;
    g.info.flipped = b.Get<uint8_t>() != 0;
    g.info.index = static_cast<uint32_t>(b.Get<int32_t>());
    g.info.uv.x = b.Get<float>();
    g.info.uv.y = b.Get<float>();
    g.info.uv.width = b.Get<float>();
    g.info.uv.height = b.Get<float>();
    g.info.vert.x = b.Get<float>();
    g.info.vert.y = b.Get<float>();
    g.info.vert.width = b.Get<float>();
    g.info.vert.height = b.Get<float>();
    g.info.advance = b.Get<float>();
    g.info.size = b.Get<int32_t>();
    g.info.style = b.Get<int32_t>();
    g.info.scale = 1.0f;
    return g;
}

std::map<std::string, Font> LoadFonts(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("no " + path + " (tools/extract/ui.py)");
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    json::Value all = json::Parse(text);
    std::map<std::string, Font> fonts;
    for (const auto& kv : all.obj) {
        const json::Value& f = kv.second;
        if (f.Find("tmp")) continue;
        Font font;
        font.name = f["name"].s;
        font.fontSize = static_cast<int>(static_cast<float>(f["fontSize"].AsDouble())); // a float, read truncated
        font.lineSpacing = static_cast<float>(f["lineSpacing"].AsDouble());
        font.ascent = static_cast<float>(f["ascent"].AsDouble());
        font.descent = static_cast<float>(f["descent"].AsDouble());
        font.tracking = static_cast<float>(f["tracking"].AsDouble());
        font.characterPadding = static_cast<int>(f["characterPadding"].AsInt());
        font.asciiStartOffset = static_cast<int>(f["asciiStartOffset"].AsInt());
        font.convertCase = static_cast<int>(f["convertCase"].AsInt());
        font.useLegacyBounds = f["useLegacyBounds"].b;
        for (const json::Value& k : f["kerning"].arr)
            font.kerning.push_back(Font::KerningPair{static_cast<uint16_t>(k[0].AsInt()), static_cast<uint16_t>(k[1].AsInt()),
                                                     static_cast<float>(k[2].AsDouble())});
        font.SortKerning();
        fonts[font.name] = font;
    }
    return fonts;
}

struct Tally {
    long rows = 0, match = 0;
};

struct GlyphKey {
    std::string font;
    uint16_t ch;
    int size, style;
    bool operator<(const GlyphKey& o) const { return std::tie(font, ch, size, style) < std::tie(o.font, o.ch, o.size, o.style); }
};

class Check {
public:
    std::map<std::string, Font> fonts;
    std::map<std::string, Font> probe; // the font probe's glyphs, by font
    std::map<GlyphKey, CharacterInfo> probeGlyphs;
    bool debug = std::getenv("ML_TEXT_DEBUG") != nullptr;
    int shown = 0;
    std::map<std::string, Tally> bySet, byLang;
    long rows = 0, match = 0, noFont = 0, rebuilt = 0;
    long verts = 0, vertsBad = 0, undefinedQuads = 0, chars = 0, charsBad = 0, lines = 0, linesBad = 0;
    std::map<std::string, long> fieldBad;
    std::map<GlyphKey, CharacterInfo> used; // the layouts' glyphs (first seen)

    void Mismatch(const json::Value& row, const std::string& what)
    {
        fieldBad[what.substr(0, what.find(' '))]++;
        if (!debug && shown >= 15) return;
        ++shown;
        std::printf("  gen %lld %s %s [%s] %s: %s\n", static_cast<long long>(row["n"].AsInt()), row["set"].s.c_str(),
                    row["lang"].IsString() ? row["lang"].s.c_str() : "-", row["s"]["font"].IsString() ? row["s"]["font"].s.c_str() : "-",
                    row["path"].IsString() ? row["path"].s.substr(0, 80).c_str() : "-", what.c_str());
    }

    void Gen(const json::Value& row)
    {
        const json::Value& sj = row["s"];
        std::string set = row["set"].s;
        std::string lang = row["lang"].IsString() ? row["lang"].s : "-";
        ++rows;
        Tally& ts = bySet[set];
        Tally& tl = byLang[lang];
        ++ts.rows;
        ++tl.rows;
        if (row.Find("rebuilt") && row["rebuilt"].AsInt() > 0) ++rebuilt;

        TextGenerationSettings s;
        Font font;
        bool haveFont = sj["font"].IsString();
        if (haveFont) {
            auto it = fonts.find(sj["font"].s);
            if (it == fonts.end()) {
                ++noFont;
                Mismatch(row, "font " + sj["font"].s + " not in fonts.json");
                return;
            }
            font = it->second;
            font.ClearGlyphs();
            auto pf = probe.find(font.name);
            if (pf != probe.end()) font.glyphFallback = &pf->second;
            if (const json::Value* tex = row.Find("tex")) {
                font.textureWidth = static_cast<int>((*tex)[0].AsInt());
                font.textureHeight = static_cast<int>((*tex)[1].AsInt());
            }
            Bytes b(Base64(row["glyphs"].s));
            while (!b.done()) {
                Glyph g = ReadGlyph(b);
                font.AddGlyph(g.info);
                GlyphKey key{font.name, g.ch, g.info.size, g.info.style};
                if (!used.count(key)) used[key] = g.info;
            }
            s.font = &font;
        }
        s.fontSize = static_cast<int>(sj["size"].AsInt());
        s.fontStyle = static_cast<int>(sj["style"].AsInt());
        s.lineSpacing = FromBits(sj["ls"].AsInt());
        s.richText = sj["rich"].b;
        s.scaleFactor = FromBits(sj["scale"].AsInt());
        s.textAnchor = static_cast<int>(sj["anchor"].AsInt());
        s.alignByGeometry = sj["geom"].b;
        s.resizeTextForBestFit = sj["best"].b;
        s.resizeTextMinSize = static_cast<int>(sj["min"].AsInt());
        s.resizeTextMaxSize = static_cast<int>(sj["max"].AsInt());
        s.horizontalOverflow = static_cast<HorizontalWrapMode>(sj["hov"].AsInt());
        s.verticalOverflow = static_cast<VerticalWrapMode>(sj["vov"].AsInt());
        s.updateBounds = sj["bounds"].b;
        s.generateOutOfBounds = sj["oob"].b;
        s.generationExtents = Vector2{FromBits(sj["ext"][0].AsInt()), FromBits(sj["ext"][1].AsInt())};
        s.pivot = Vector2{FromBits(sj["pivot"][0].AsInt()), FromBits(sj["pivot"][1].AsInt())};
        s.color = ColorRGBAf{FromBits(sj["color"][0].AsInt()), FromBits(sj["color"][1].AsInt()), FromBits(sj["color"][2].AsInt()),
                             FromBits(sj["color"][3].AsInt())};
        std::u16string str = row["str"].IsString() ? Utf16(row["str"].s) : std::u16string();

        TextGenerator gen;
        uint32_t err = gen.Populate(str, s);
        bool good = true;
        auto fail = [&](const std::string& what) {
            if (good) Mismatch(row, what);
            good = false;
        };
        if ((err == 0) != row["ok"].b) fail("ok " + std::to_string(err));
        if (gen.fontSizeUsedForBestFit() != row["bestSize"].AsInt())
            fail("bestSize " + std::to_string(gen.fontSizeUsedForBestFit()) + " vs " + std::to_string(row["bestSize"].AsInt()));
        if (gen.vertexCount() != row["nv"].AsInt()) fail("nv " + std::to_string(gen.vertexCount()) + " vs " + std::to_string(row["nv"].AsInt()));
        if (gen.characterCount() != row["nc"].AsInt()) fail("nc " + std::to_string(gen.characterCount()) + " vs " + std::to_string(row["nc"].AsInt()));
        if (gen.lineCount() != row["nl"].AsInt()) fail("nl " + std::to_string(gen.lineCount()) + " vs " + std::to_string(row["nl"].AsInt()));
        Rect r = gen.rectExtents();
        const json::Value& rr = row["rect"];
        if (!Same(r.x, FromBits(rr[0].AsInt())) || !Same(r.y, FromBits(rr[1].AsInt())) || !Same(r.width, FromBits(rr[2].AsInt())) ||
            !Same(r.height, FromBits(rr[3].AsInt())))
            fail("rect " + std::to_string(r.width) + "," + std::to_string(r.height) + " vs " + std::to_string(FromBits(rr[2].AsInt())) + "," +
                 std::to_string(FromBits(rr[3].AsInt())));

        // lines
        const json::Value& lj = row["lines"];
        const auto& gl = gen.lines();
        for (size_t i = 0; i < lj.size(); ++i) {
            ++lines;
            bool ok = i < gl.size() && gl[i].startCharIdx == lj[i][0].AsInt() && gl[i].height == lj[i][1].AsInt() &&
                      Same(gl[i].topY, FromBits(lj[i][2].AsInt())) && Same(gl[i].leading, FromBits(lj[i][3].AsInt()));
            if (!ok) {
                ++linesBad;
                char buf[200];
                if (i < gl.size())
                    std::snprintf(buf, sizeof buf, "line %zu: %d %d %.9g %.9g vs %lld %lld %.9g %.9g", i, gl[i].startCharIdx, gl[i].height,
                                  gl[i].topY, gl[i].leading, static_cast<long long>(lj[i][0].AsInt()), static_cast<long long>(lj[i][1].AsInt()),
                                  FromBits(lj[i][2].AsInt()), FromBits(lj[i][3].AsInt()));
                else
                    std::snprintf(buf, sizeof buf, "line %zu missing", i);
                fail(buf);
            }
        }
        // characters: f32 cursorPos x y, f32 charWidth
        {
            Bytes b(Base64(row["chars"].s));
            const auto& gc = gen.characters();
            size_t i = 0;
            while (!b.done()) {
                float x = b.Get<float>(), y = b.Get<float>(), w = b.Get<float>();
                ++chars;
                bool ok = i < gc.size() && Same(gc[i].cursorPos.x, x) && Same(gc[i].cursorPos.y, y) && Same(gc[i].charWidth, w);
                if (!ok) {
                    ++charsBad;
                    char buf[200];
                    if (i < gc.size())
                        std::snprintf(buf, sizeof buf, "char %zu: (%.9g, %.9g) %.9g vs (%.9g, %.9g) %.9g", i, gc[i].cursorPos.x, gc[i].cursorPos.y,
                                      gc[i].charWidth, x, y, w);
                    else
                        std::snprintf(buf, sizeof buf, "char %zu missing", i);
                    fail(buf);
                }
                ++i;
            }
        }
        // vertices: f32 position x y z, u8 colour r g b a, f32 uv0 x y, f32 uv1 x y (+ "vx")
        {
            Bytes b(Base64(row["v"].s));
            std::vector<uint8_t> vxRaw;
            const json::Value* vx = row.Find("vx");
            Bytes bx(vx ? Base64(vx->s) : std::vector<uint8_t>());
            const auto& gv = gen.verts();
            const auto& defined = gen.QuadDefined();
            size_t i = 0;
            while (!b.done()) {
                float px = b.Get<float>(), py = b.Get<float>(), pz = b.Get<float>();
                uint8_t cr = b.Get<uint8_t>(), cg = b.Get<uint8_t>(), cb = b.Get<uint8_t>(), ca = b.Get<uint8_t>();
                float u0x = b.Get<float>(), u0y = b.Get<float>(), u1x = b.Get<float>(), u1y = b.Get<float>();
                ++verts;
                if (i >= gv.size()) {
                    ++vertsBad;
                    fail("vertex " + std::to_string(i) + " missing");
                    ++i;
                    continue;
                }
                const UIVertex& v = gv[i];
                bool def = (i / 4) < defined.size() && defined[i / 4];
                if (!def && (i % 4) == 0) ++undefinedQuads;
                bool ok = Same(v.position.x, px) && Same(v.position.y, py) && Same(v.position.z, pz);
                if (def)
                    ok = ok && v.color.r == cr && v.color.g == cg && v.color.b == cb && v.color.a == ca && Same(v.uv0.x, u0x) &&
                         Same(v.uv0.y, u0y);
                ok = ok && Same(v.uv1.x, u1x) && Same(v.uv1.y, u1y);
                if (vx) {
                    float n0 = bx.Get<float>(), n1 = bx.Get<float>(), n2 = bx.Get<float>();
                    float t0 = bx.Get<float>(), t1 = bx.Get<float>(), t2 = bx.Get<float>(), t3 = bx.Get<float>();
                    float u2x = bx.Get<float>(), u2y = bx.Get<float>(), u3x = bx.Get<float>(), u3y = bx.Get<float>();
                    ok = ok && Same(v.normal.x, n0) && Same(v.normal.y, n1) && Same(v.normal.z, n2) && Same(v.tangent.x, t0) &&
                         Same(v.tangent.y, t1) && Same(v.tangent.z, t2) && Same(v.tangent.w, t3) && Same(v.uv2.x, u2x) &&
                         Same(v.uv2.y, u2y) && Same(v.uv3.x, u3x) && Same(v.uv3.y, u3y);
                } else {
                    ok = ok && Same(v.normal.x, 0.0f) && Same(v.normal.y, 0.0f) && Same(v.normal.z, -1.0f) && Same(v.tangent.x, 1.0f) &&
                         Same(v.tangent.y, 0.0f) && Same(v.tangent.z, 0.0f) && Same(v.tangent.w, -1.0f) && Same(v.uv2.x, 0.0f) &&
                         Same(v.uv2.y, 0.0f) && Same(v.uv3.x, 0.0f) && Same(v.uv3.y, 0.0f);
                }
                if (!ok) {
                    ++vertsBad;
                    char buf[300];
                    std::snprintf(buf, sizeof buf,
                                  "vertex %zu: (%.9g, %.9g) %02x%02x%02x%02x uv (%.9g, %.9g) vs (%.9g, %.9g) %02x%02x%02x%02x uv (%.9g, %.9g)%s", i,
                                  v.position.x, v.position.y, v.color.r, v.color.g, v.color.b, v.color.a, v.uv0.x, v.uv0.y, px, py, cr, cg, cb,
                                  ca, u0x, u0y, def ? "" : " (undefined quad)");
                    fail(buf);
                }
                ++i;
            }
        }
        if (good) {
            ++match;
            ++ts.match;
            ++tl.match;
        }
    }
};

// The font probe's glyphs (fonts.jsonl), each font's as a table the layouts fall back on for
// what their own atlas snapshot hasn't got (the port's glyphs are these).
void LoadFontProbe(const std::string& path, Check& c)
{
    std::ifstream in(path, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("\"ev\":\"glyphs\"") == std::string::npos) continue;
        json::Value row = json::Parse(line);
        std::string name = row["font"].s;
        int size = static_cast<int>(row["size"].AsInt()), style = static_cast<int>(row["style"].AsInt());
        auto asset = c.fonts.find(name);
        if (asset == c.fonts.end()) continue;
        Font& font = c.probe[name];
        if (font.name.empty()) {
            font = asset->second;
            font.ClearGlyphs();
        }
        Bytes b(Base64(row["data"].s));
        while (!b.done()) {
            Glyph g = ReadGlyph(b);
            b.Get<int16_t>();
            b.Get<int16_t>();
            int16_t tw = b.Get<int16_t>(), th = b.Get<int16_t>();
            b.Skip(static_cast<size_t>(tw) * static_cast<size_t>(th));
            if (!g.found) continue;
            // (GetCharacterInfo's advance is the atlas's times m_Tracking: 1 for every game font.)
            if (font.tracking != 1.0f) throw std::runtime_error(name + ": a tracking other than 1");
            font.AddGlyph(g.info);
            c.probeGlyphs[GlyphKey{name, g.ch, size, style}] = g.info;
        }
    }
}

// The glyphs the layouts used against the font probe's (all but the uv: the atlas differs).
void CompareFontProbe(const Check& c, long& compared, long& bad, long& absent)
{
    for (const auto& kv : c.used) {
        auto it = c.probeGlyphs.find(kv.first);
        if (it == c.probeGlyphs.end()) {
            ++absent;
            continue;
        }
        ++compared;
        const CharacterInfo& a = kv.second;
        const CharacterInfo& b = it->second;
        bool ok = Same(a.vert.x, b.vert.x) && Same(a.vert.y, b.vert.y) && Same(a.vert.width, b.vert.width) &&
                  Same(a.vert.height, b.vert.height) && a.flipped == b.flipped && Same(a.advance, b.advance);
        if (!ok && ++bad <= 10)
            std::printf("  glyph %s U+%04X size %d style %d: (%g %g %g %g) %g vs the probe's (%g %g %g %g) %g\n", kv.first.font.c_str(),
                        kv.first.ch, kv.first.size, kv.first.style, a.vert.x, a.vert.y, a.vert.width, a.vert.height, a.advance, b.vert.x,
                        b.vert.y, b.vert.width, b.vert.height, b.advance);
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::printf("usage: ui_text_test <text.jsonl> <fonts.json> [<fonts.jsonl>]\n");
        return 2;
    }
    std::string textPath = argv[1], fontsPath = argv[2], probePath = argc > 3 ? argv[3] : "";
    std::ifstream in(textPath, std::ios::binary);
    if (!in) {
        std::printf("%s: no recording (the text oracle's -oracle-text): skipped\n", textPath.c_str());
        return 0;
    }
    Check c;
    bool haveProbe = false;
    try {
        c.fonts = LoadFonts(fontsPath);
        if (!probePath.empty()) {
            std::ifstream probe(probePath);
            haveProbe = static_cast<bool>(probe);
        }
        if (haveProbe) LoadFontProbe(probePath, c);
        std::string line;
        while (std::getline(in, line)) {
            if (line.find("\"ev\":\"gen\"") == std::string::npos) continue;
            c.Gen(json::Parse(line));
        }
    } catch (const std::exception& e) {
        std::printf("%s: %s\n", textPath.c_str(), e.what());
        return 1;
    }
    std::printf("%s: %ld generations (%ld redone by the oracle after a font atlas rebuild)\n", textPath.c_str(), c.rows, c.rebuilt);
    std::printf("by input\n");
    for (const auto& kv : c.bySet)
        std::printf("  %-12s %7ld of %7ld match\n", kv.first.c_str(), kv.second.match, kv.second.rows);
    std::printf("by language (the localized texts and items; '-' the rest)\n");
    for (const auto& kv : c.byLang)
        std::printf("  %-24s %7ld of %7ld match\n", kv.first.c_str(), kv.second.match, kv.second.rows);
    std::printf("vertices %ld (%ld differ; %ld quads without a glyph in untrimmed outputs: positions only)\n", c.verts, c.vertsBad,
                c.undefinedQuads);
    std::printf("characters %ld (%ld differ), lines %ld (%ld differ)\n", c.chars, c.charsBad, c.lines, c.linesBad);
    for (const auto& kv : c.fieldBad) std::printf("  first difference in %s: %ld generations\n", kv.first.c_str(), kv.second);
    bool ok = c.match == c.rows && c.rows > 0 && c.noFont == 0;
    if (!haveProbe) {
        std::printf("%s: no font probe recording (-oracle-fonts): only the rows' own glyphs\n", probePath.c_str());
    } else {
        long compared = 0, bad = 0, absent = 0;
        CompareFontProbe(c, compared, bad, absent);
        std::printf("the layouts' %zu glyphs against the font probe's %zu: %ld match, %ld differ, %ld not probed\n", c.used.size(),
                    c.probeGlyphs.size(), compared - bad, bad, absent);
        ok = ok && bad == 0;
    }
    std::printf("\n%s\n", ok ? "ALL MATCH" : "MISMATCHES");
    return ok ? 0 : 1;
}
