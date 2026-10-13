using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using BepInEx;
using HarmonyLib;
using I2.Loc;
using UnityEngine;
using UnityEngine.UI;

namespace MoonlighterOracle
{
    /// The game's text, as Unity's font engine and text generator make it (docs/ui_plan.md, "Text").
    ///
    /// `-oracle-fonts`: the font probe, fonts.jsonl. For every font the game has (the ones loaded
    /// at the main menu, the OptionsDialog prefab's Zpix, the built-in Arial), at the sizes and
    /// styles its texts use, over the characters its languages need (from I2's LanguageSource, the
    /// game's localization data): what Font.RequestCharactersInTexture puts in the font texture,
    /// each glyph's CharacterInfo as it's stored (the raw uv and vert rects, the advance, size,
    /// style, flipped) and its texels (the alpha of its rect in the font texture, read right after
    /// the request).
    ///
    /// `-oracle-text`: the text layout probe, text.jsonl. TextGenerator.Populate on real inputs:
    /// every Text of the loaded scenes and of a few UI prefabs (the bag's Canvas - Will, the
    /// dungeon's, the options popups) with its own generation settings (Text.GetGenerationSettings
    /// with its rect's size, what Text.OnPopulateMesh does) and the preferred-size variants
    /// (Text.preferredWidth/Height's settings); every localized Text in every language (I2's
    /// translation for the language, its font from the secondary term, the size and line spacing
    /// FontSizeLocalize or FontSizeManager give it); item names and descriptions in every language
    /// in the bag's Texts; and a synthetic grid over alignment, overflow, wrap, rich text, best fit,
    /// line spacing, scale, pivot and colour. Each generation: the settings, the string, and what
    /// came out (verts, characters, lines, extents, the best fit's size), with the font texture's
    /// glyphs for the string's characters as the generator had them (their uv rects).
    ///
    /// Neither changes anything of the game: no input, no language switch (the translations are
    /// read for each language from I2's source, LocalizationManager.CurrentLanguage stays as it
    /// is, and a run where it changed fails), saving blocked (Harvest.InstallSafety), and a run
    /// whose screen size changes fails, as the UI oracle's.
    ///
    /// Rows: one JSON object per line. Floats are their IEEE bits (ints); bulk data is base64 of
    /// little-endian records (below, GlyphRecord and the "gen" row's fields).
    internal static class TextOracle
    {
        private static StreamWriter writer;
        private static int startW, startH;
        private static string startLanguage;
        private static int rebuilt;

        public static void Start(BaseUnityPlugin host, Harmony harmony, string[] args, bool quit)
        {
            bool fonts = Array.Exists(args, a => a == "-oracle-fonts");
            bool text = Array.Exists(args, a => a == "-oracle-text");
            if (!fonts && !text) return;
            Harvest.InstallSafety(harmony);
            host.StartCoroutine(Run(fonts, text, quit));
        }

        private static void Fail(string why)
        {
            OraclePlugin.Log.LogError("text: " + why);
            File.WriteAllText(Path.Combine(OraclePlugin.OutDir, "FAILED"), why);
        }

        private static IEnumerator Run(bool fonts, bool text, bool quit)
        {
            var log = OraclePlugin.Log;
            float t0 = Time.realtimeSinceStartup;
            while (GUIManager.Instance == null || LocalizationManager.Sources.Count == 0
                   || UnityEngine.Object.FindObjectOfType<MainMenuPanel>() == null)
            {
                if (Time.realtimeSinceStartup - t0 > 240f) { Fail("the main menu never came up"); yield break; }
                yield return new WaitForSecondsRealtime(0.5f);
            }
            // The main menu's texts localized and laid out.
            yield return new WaitForSecondsRealtime(5f);
            startW = Screen.width;
            startH = Screen.height;
            startLanguage = LocalizationManager.CurrentLanguage;
            Font.textureRebuilt += OnRebuilt;
            bool ok = true;
            if (text)
            {
                var e = TextProbe();
                while (true)
                {
                    bool more;
                    try { more = e.MoveNext(); }
                    catch (Exception ex) { Fail("text probe failed: " + ex); ok = false; break; }
                    if (!more) break;
                    yield return e.Current;
                }
                Close();
            }
            if (ok && fonts)
            {
                var e = FontProbe();
                while (true)
                {
                    bool more;
                    try { more = e.MoveNext(); }
                    catch (Exception ex) { Fail("font probe failed: " + ex); ok = false; break; }
                    if (!more) break;
                    yield return e.Current;
                }
                Close();
            }
            Font.textureRebuilt -= OnRebuilt;
            if (ok && LocalizationManager.CurrentLanguage != startLanguage)
            {
                Fail($"the language changed ({startLanguage} to {LocalizationManager.CurrentLanguage})");
                ok = false;
            }
            if (ok && (Screen.width != startW || Screen.height != startH))
            {
                Fail($"the screen changed to {Screen.width}x{Screen.height} (from {startW}x{startH})");
                ok = false;
            }
            if (ok)
            {
                if (text) File.WriteAllText(Path.Combine(OraclePlugin.OutDir, "TEXT_DONE"), DateTime.Now.ToString("o"));
                if (fonts) File.WriteAllText(Path.Combine(OraclePlugin.OutDir, "FONTS_DONE"), DateTime.Now.ToString("o"));
                log.LogInfo($"text: done in {Time.realtimeSinceStartup - t0:F0} s");
            }
            if (quit) Application.Quit();
        }

        private static void OnRebuilt(Font f) => rebuilt++;

        private static void Open(string name)
        {
            writer = new StreamWriter(Path.Combine(OraclePlugin.OutDir, name), append: false, new UTF8Encoding(false)) { AutoFlush = false };
        }

        private static void Close()
        {
            if (writer == null) return;
            writer.Flush();
            writer.Close();
            writer = null;
        }

        private static void Emit(Json j) => writer.WriteLine(j.EndObj().ToString());

        private static string PathOf(Transform t)
        {
            string p = t.name;
            for (t = t.parent; t != null; t = t.parent) p = t.name + "/" + p;
            return p;
        }

        // --- the localization data ---

        private sealed class Lang
        {
            public string name, code;
            public int index;
            public bool enabled;
        }

        private static LanguageSourceData Source => LocalizationManager.Sources[0];

        private static List<Lang> Languages()
        {
            var src = Source;
            var list = new List<Lang>();
            for (int i = 0; i < src.mLanguages.Count; i++)
                list.Add(new Lang { name = src.mLanguages[i].Name, code = src.mLanguages[i].Code, index = i, enabled = src.mLanguages[i].IsEnabled() });
            return list;
        }

        /// A term's text in a language as the game shows it: I2's translation (its fallback when
        /// missing), then Regex.Unescape (LocalizeTarget_UnityUI_Text.DoLocalize, Localization.Get).
        private static string Translate(string term, string language)
        {
            string s = LocalizationManager.GetTranslation(term, FixForRTL: false, overrideLanguage: language);
            if (string.IsNullOrEmpty(s)) return s;
            try { return Regex.Unescape(s); }
            catch (Exception) { return s; }
        }

        private static Json Languages(Json j)
        {
            j.Key("languages").Arr();
            foreach (var l in Languages())
                j.Obj().Prop("name", l.name).Prop("code", l.code).Prop("enabled", l.enabled).EndObj();
            j.EndArr();
            return j;
        }

        // --- glyph records ---
        //
        // A glyph record, little-endian: u16 char, u8 found (GetCharacterInfo's result), u8 flipped,
        // i32 index, f32 uv x y w h, f32 vert x y w h, f32 advance (CharacterInfo.width), i32 size,
        // i32 style; with texels (the font probe) i16 x y w h of its rect in the font texture (from
        // the uv rect) and w*h alpha bytes, rows from the rect's bottom row up (the texture's order).

#pragma warning disable 618 // CharacterInfo's raw fields: what the font cache holds
        private static void WriteGlyph(BinaryWriter b, char c, bool found, CharacterInfo ci)
        {
            b.Write((ushort)c);
            b.Write((byte)(found ? 1 : 0));
            b.Write((byte)(ci.flipped ? 1 : 0));
            b.Write(ci.index);
            b.Write(ci.uv.x); b.Write(ci.uv.y); b.Write(ci.uv.width); b.Write(ci.uv.height);
            b.Write(ci.vert.x); b.Write(ci.vert.y); b.Write(ci.vert.width); b.Write(ci.vert.height);
            b.Write(ci.width);
            b.Write(ci.size);
            b.Write((int)ci.style);
        }

        private static void TexelRect(CharacterInfo ci, int w, int h, out int x0, out int y0, out int tw, out int th)
        {
            float ax = ci.uv.x, bx = ci.uv.x + ci.uv.width, ay = ci.uv.y, by = ci.uv.y + ci.uv.height;
            x0 = Mathf.RoundToInt(Mathf.Min(ax, bx) * w);
            y0 = Mathf.RoundToInt(Mathf.Min(ay, by) * h);
            tw = Mathf.RoundToInt(Mathf.Max(ax, bx) * w) - x0;
            th = Mathf.RoundToInt(Mathf.Max(ay, by) * h) - y0;
            if (x0 < 0 || y0 < 0 || tw < 0 || th < 0 || x0 + tw > w || y0 + th > h) { x0 = y0 = tw = th = 0; }
        }
#pragma warning restore 618

        /// The font texture's alpha, w*h bytes from the bottom row up: the texture's own bytes when
        /// it's a readable Alpha8, its pixels when readable, else drawn into a render texture and
        /// read back.
        private static byte[] ReadAlpha(Texture2D tex, out string how)
        {
            int w = tex.width, h = tex.height;
            var alpha = new byte[w * h];
            if (tex.isReadable)
            {
                if (tex.format == TextureFormat.Alpha8)
                {
                    byte[] raw = tex.GetRawTextureData();
                    if (raw.Length >= w * h)
                    {
                        Buffer.BlockCopy(raw, 0, alpha, 0, w * h);
                        how = "raw";
                        return alpha;
                    }
                }
                Color32[] px = tex.GetPixels32();
                for (int i = 0; i < w * h; i++) alpha[i] = px[i].a;
                how = "pixels";
                return alpha;
            }
            var prev = RenderTexture.active;
            var rt = RenderTexture.GetTemporary(w, h, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.Linear);
            Graphics.Blit(tex, rt);
            RenderTexture.active = rt;
            var tmp = new Texture2D(w, h, TextureFormat.RGBA32, false, true);
            tmp.ReadPixels(new Rect(0, 0, w, h), 0, 0, false);
            tmp.Apply(false);
            Color32[] read = tmp.GetPixels32();
            RenderTexture.active = prev;
            RenderTexture.ReleaseTemporary(rt);
            UnityEngine.Object.Destroy(tmp);
            for (int i = 0; i < w * h; i++) alpha[i] = read[i].a;
            how = "blit";
            return alpha;
        }

        // --- the fonts ---

        private static List<Font> GameFonts()
        {
            // The OptionsDialog prefab brings Zpix (loaded, not instantiated); Arial is built in.
            Resources.Load<GameObject>("dialogs/optionsdialog");
            var fonts = Resources.FindObjectsOfTypeAll<Font>().ToList();
            var arial = Resources.GetBuiltinResource<Font>("Arial.ttf");
            if (arial != null && !fonts.Contains(arial)) fonts.Add(arial);
            return fonts.Where(f => f != null).Distinct().OrderBy(f => f.name, StringComparer.Ordinal).ThenBy(f => f.GetInstanceID()).ToList();
        }

        private static Json FontRow(Font f)
        {
            var j = new Json().Obj().Prop("ev", "font").Prop("name", f.name).Prop("id", f.GetInstanceID());
            j.Key("fontNames").Arr();
            if (f.fontNames != null) foreach (string n in f.fontNames) j.Val(n);
            j.EndArr();
            j.Prop("dynamic", f.dynamic).Prop("fontSize", f.fontSize).Prop("ascent", f.ascent).Prop("lineHeight", f.lineHeight);
            var tex = f.material != null ? f.material.mainTexture as Texture2D : null;
            if (tex != null)
                j.Key("tex").Arr().Val(tex.width).Val(tex.height).Val((int)tex.format).Val(tex.isReadable).Val((int)tex.filterMode).EndArr();
            return j;
        }

        private static readonly int[] ProbeSizes = { 7, 8, 9, 10, 12, 14, 15, 16, 20, 24, 32 };
        private static readonly int[] CjkSizes = { 7, 8, 9, 10, 12, 16, 20 };

        private static string Chars(IEnumerable<char> set) => new string(set.Distinct().OrderBy(c => c).ToArray());

        /// Hangul jamo, the CJK blocks (radicals, punctuation, kana, ideographs), Hangul syllables,
        /// compatibility ideographs, full-width forms.
        private static bool Cjk(char c) =>
            (c >= 0x1100 && c < 0x1200) || (c >= 0x2E80 && c < 0xA000) || (c >= 0xAC00 && c < 0xD800) || (c >= 0xF900 && c < 0xFB00) || (c >= 0xFF00 && c < 0xFFF0);

        private static IEnumerator FontProbe()
        {
            var log = OraclePlugin.Log;
            var sw = Stopwatch.StartNew();
            Open("fonts.jsonl");
            var src = Source;
            var langs = Languages();

            // Each enabled language's characters: every text term's translation, as shown.
            var perLang = new Dictionary<string, HashSet<char>>();
            foreach (var l in langs.Where(l => l.enabled))
            {
                var set = new HashSet<char>();
                foreach (var term in src.mTerms)
                {
                    if (term.TermType != eTermType.Text) continue;
                    string s = Translate(term.Term, l.name);
                    if (!string.IsNullOrEmpty(s)) foreach (char c in s) if (c >= 0x20) set.Add(c);
                }
                perLang[l.name] = set;
            }
            var latin = new HashSet<char>();
            for (char c = (char)0x20; c < 0x7F; c++) latin.Add(c);
            foreach (var kv in perLang) foreach (char c in kv.Value) if (!Cjk(c)) latin.Add(c);
            string latinChars = Chars(latin);

            // The fonts each language's I2 font terms name.
            var fontTerms = src.mTerms.Where(t => t.TermType == eTermType.Font).Select(t => t.Term).ToList();
            var fonts = GameFonts();
            var cjkFor = new Dictionary<string, HashSet<char>>();
            foreach (var l in langs.Where(l => l.enabled))
                foreach (string ft in fontTerms)
                {
                    string fname = LocalizationManager.GetTranslation(ft, FixForRTL: false, overrideLanguage: l.name);
                    if (string.IsNullOrEmpty(fname)) continue;
                    if (!cjkFor.TryGetValue(fname, out var set)) cjkFor[fname] = set = new HashSet<char>();
                    foreach (char c in perLang[l.name]) if (Cjk(c)) set.Add(c);
                }
            // Zpix (the OptionsDialog's) is a Chinese pixel font: both Chinese languages.
            if (!cjkFor.TryGetValue("Zpix", out var zpix)) cjkFor["Zpix"] = zpix = new HashSet<char>();
            foreach (var l in langs.Where(l => l.enabled && l.code.StartsWith("zh"))) foreach (char c in perLang[l.name]) if (Cjk(c)) zpix.Add(c);

            var start = new Json().Obj().Prop("ev", "fontsStart").Prop("unity", Application.unityVersion)
                .Prop("sw", Screen.width).Prop("sh", Screen.height).Prop("language", startLanguage);
            Languages(start);
            start.Key("sizes").Arr(); foreach (int s in ProbeSizes) start.Val(s); start.EndArr();
            start.Key("cjkSizes").Arr(); foreach (int s in CjkSizes) start.Val(s); start.EndArr();
            start.Key("fonts").Arr(); foreach (var f in fonts) start.Val(f.name); start.EndArr();
            Emit(start);
            Emit(new Json().Obj().Prop("ev", "charset").Prop("name", "latin").Prop("chars", latinChars));
            foreach (var kv in perLang.OrderBy(k => k.Key, StringComparer.Ordinal))
                Emit(new Json().Obj().Prop("ev", "charset").Prop("name", "lang:" + kv.Key).Prop("chars", Chars(kv.Value)));
            foreach (var kv in cjkFor.OrderBy(k => k.Key, StringComparer.Ordinal))
                if (kv.Value.Count > 0) Emit(new Json().Obj().Prop("ev", "charset").Prop("name", "cjk:" + kv.Key).Prop("chars", Chars(kv.Value)));
            foreach (var f in fonts) Emit(FontRow(f));
            writer.Flush();
            yield return null;

            int groups = 0, glyphs = 0, missing = 0;
            foreach (var f in fonts)
            {
                var plan = new List<KeyValuePair<string, KeyValuePair<int, string>>>(); // set name -> (size, chars)
                foreach (int size in ProbeSizes) plan.Add(new KeyValuePair<string, KeyValuePair<int, string>>("latin", new KeyValuePair<int, string>(size, latinChars)));
                if (cjkFor.TryGetValue(f.name, out var cjk) && cjk.Count > 0)
                {
                    string cjkChars = Chars(cjk);
                    foreach (int size in CjkSizes) plan.Add(new KeyValuePair<string, KeyValuePair<int, string>>("cjk:" + f.name, new KeyValuePair<int, string>(size, cjkChars)));
                }
                foreach (var item in plan)
                {
                    bool isCjk = item.Key != "latin";
                    for (int style = 0; style < 4; style++)
                    {
                        if (isCjk && style == 3) continue; // bold italic: no CJK text asks for it
                        int size = item.Value.Key;
                        string chars = item.Value.Value;
                        int before = rebuilt;
                        f.RequestCharactersInTexture(chars, size, (FontStyle)style);
                        int after = rebuilt;
                        var tex = f.material != null ? f.material.mainTexture as Texture2D : null;
                        byte[] alpha = null;
                        string how = "none";
                        int w = 0, h = 0;
                        if (tex != null)
                        {
                            w = tex.width;
                            h = tex.height;
                            alpha = ReadAlpha(tex, out how);
                        }
                        var ms = new MemoryStream();
                        var b = new BinaryWriter(ms);
                        int found = 0;
                        foreach (char c in chars)
                        {
                            bool ok = f.GetCharacterInfo(c, out CharacterInfo ci, size, (FontStyle)style);
                            if (!ok) ci = default;
                            WriteGlyph(b, c, ok, ci);
                            int x0 = 0, y0 = 0, tw = 0, th = 0;
                            if (ok && alpha != null) TexelRect(ci, w, h, out x0, out y0, out tw, out th);
                            b.Write((short)x0); b.Write((short)y0); b.Write((short)tw); b.Write((short)th);
                            for (int y = 0; y < th; y++) b.Write(alpha, (y0 + y) * w + x0, tw);
                            if (ok) found++; else missing++;
                            glyphs++;
                        }
                        b.Flush();
                        var j = new Json().Obj().Prop("ev", "glyphs").Prop("font", f.name).Prop("size", size).Prop("style", style)
                            .Prop("set", item.Key).Prop("n", chars.Length).Prop("found", found)
                            .Key("tex").Arr().Val(w).Val(h).Val(tex != null ? (int)tex.format : -1).EndArr().Prop("read", how)
                            .Prop("rebuilt", after - before).Prop("rebuiltAfterRead", rebuilt - after)
                            .Prop("data", Convert.ToBase64String(ms.ToArray()));
                        Emit(j);
                        groups++;
                        if (rebuilt != after) OraclePlugin.Log.LogWarning($"fonts: {f.name} {size} {style}: the texture was rebuilt while being read");
                    }
                    writer.Flush();
                    yield return null;
                }
                log.LogInfo($"fonts: {f.name} done ({groups} groups, {glyphs} glyphs, {sw.ElapsedMilliseconds} ms)");
            }
            Emit(new Json().Obj().Prop("ev", "fontsEnd").Prop("groups", groups).Prop("glyphs", glyphs).Prop("missing", missing)
                .Prop("ms", (int)sw.ElapsedMilliseconds));
        }

        // --- the text layout probe ---

        private struct Sample
        {
            public string set, path, lang, term, str;
            public TextGenerationSettings s;
        }

        private static int genCount;

        private static Json SettingsJson(Json j, TextGenerationSettings s)
        {
            j.Key("s").Obj().Prop("font", s.font != null ? s.font.name : null).Prop("size", s.fontSize).Prop("style", (int)s.fontStyle);
            j.Key("ls").Bits(s.lineSpacing).Prop("rich", s.richText).Key("scale").Bits(s.scaleFactor).Prop("anchor", (int)s.textAnchor)
                .Prop("geom", s.alignByGeometry).Prop("best", s.resizeTextForBestFit).Prop("min", s.resizeTextMinSize).Prop("max", s.resizeTextMaxSize)
                .Prop("hov", (int)s.horizontalOverflow).Prop("vov", (int)s.verticalOverflow).Prop("bounds", s.updateBounds).Prop("oob", s.generateOutOfBounds);
            j.Key("ext").Arr().Bits(s.generationExtents.x).Bits(s.generationExtents.y).EndArr();
            j.Key("pivot").Arr().Bits(s.pivot.x).Bits(s.pivot.y).EndArr();
            j.Key("color").Arr().Bits(s.color.r).Bits(s.color.g).Bits(s.color.b).Bits(s.color.a).EndArr();
            j.EndObj();
            return j;
        }

        private static readonly Vector3 StdNormal = new Vector3(0f, 0f, -1f);
        private static readonly Vector4 StdTangent = new Vector4(1f, 0f, 0f, -1f);

        /// One generation: Populate (again if the font texture was rebuilt meanwhile), its outputs,
        /// and the font texture's glyphs of the string's characters as they were then.
        private static void Generate(Sample sm)
        {
            var gen = new TextGenerator();
            int tries = 0, rebuiltDuring;
            bool ok;
            do
            {
                int before = rebuilt;
                gen.Invalidate();
                ok = gen.Populate(sm.str, sm.s);
                rebuiltDuring = rebuilt - before;
            } while (rebuiltDuring > 0 && ++tries < 3);
            int rebuiltRead = rebuilt;
            var verts = gen.verts;
            var chars = gen.characters;
            var lines = gen.lines;
            Rect ext = gen.rectExtents;

            var j = new Json().Obj().Prop("ev", "gen").Prop("n", genCount++).Prop("set", sm.set).Prop("path", sm.path)
                .Prop("lang", sm.lang).Prop("term", sm.term);
            SettingsJson(j, sm.s);
            j.Prop("str", sm.str).Prop("ok", ok).Prop("tries", tries).Prop("rebuilt", rebuiltDuring)
                .Prop("bestSize", gen.fontSizeUsedForBestFit).Prop("nv", gen.vertexCount).Prop("nc", gen.characterCount).Prop("nl", gen.lineCount);
            j.Key("rect").Arr().Bits(ext.x).Bits(ext.y).Bits(ext.width).Bits(ext.height).EndArr();
            j.Key("lines").Arr();
            foreach (var l in lines) j.Arr().Val(l.startCharIdx).Val(l.height).Bits(l.topY).Bits(l.leading).EndArr();
            j.EndArr();
            // chars: f32 cursorPos x y, f32 charWidth.
            var ms = new MemoryStream();
            var b = new BinaryWriter(ms);
            foreach (var c in chars) { b.Write(c.cursorPos.x); b.Write(c.cursorPos.y); b.Write(c.charWidth); }
            b.Flush();
            j.Prop("chars", Convert.ToBase64String(ms.ToArray()));
            // verts: f32 position x y z, u8 colour r g b a, f32 uv0 x y, f32 uv1 x y; the other fields
            // ("vx": f32 normal x y z, tangent x y z w, uv2 x y, uv3 x y) only when any differs from
            // UIVertex's defaults (normal (0,0,-1), tangent (1,0,0,-1), uv2 = uv3 = 0).
            ms = new MemoryStream();
            b = new BinaryWriter(ms);
            bool std = true;
            foreach (var v in verts)
            {
                b.Write(v.position.x); b.Write(v.position.y); b.Write(v.position.z);
                b.Write(v.color.r); b.Write(v.color.g); b.Write(v.color.b); b.Write(v.color.a);
                b.Write(v.uv0.x); b.Write(v.uv0.y); b.Write(v.uv1.x); b.Write(v.uv1.y);
                if (v.normal != StdNormal || v.tangent != StdTangent || v.uv2 != Vector2.zero || v.uv3 != Vector2.zero) std = false;
            }
            b.Flush();
            j.Prop("v", Convert.ToBase64String(ms.ToArray()));
            if (!std)
            {
                ms = new MemoryStream();
                b = new BinaryWriter(ms);
                foreach (var v in verts)
                {
                    b.Write(v.normal.x); b.Write(v.normal.y); b.Write(v.normal.z);
                    b.Write(v.tangent.x); b.Write(v.tangent.y); b.Write(v.tangent.z); b.Write(v.tangent.w);
                    b.Write(v.uv2.x); b.Write(v.uv2.y); b.Write(v.uv3.x); b.Write(v.uv3.y);
                }
                b.Flush();
                j.Prop("vx", Convert.ToBase64String(ms.ToArray()));
            }
            // The font cache's glyphs (every size and style it holds) of the string's characters.
            var font = sm.s.font;
            if (font != null)
            {
                // (The space too: Setup caches it for the tab width.)
                var wanted = new HashSet<char>(sm.str ?? "") { ' ' };
                var sizes = GlyphSizes(sm.s, sm.str, gen.fontSizeUsedForBestFit);
                ms = new MemoryStream();
                b = new BinaryWriter(ms);
                int n = 0;
                CharacterInfo[] all = font.characterInfo;
                if (all != null)
                    foreach (var ci in all.OrderBy(c => c.index).ThenBy(c => c.size).ThenBy(c => (int)c.style))
                        if (ci.index >= 0 && ci.index < 0x10000 && wanted.Contains((char)ci.index) && sizes.Contains(ci.size)) { WriteGlyph(b, (char)ci.index, true, ci); n++; }
                b.Flush();
                j.Prop("ng", n).Prop("glyphs", Convert.ToBase64String(ms.ToArray()));
                var tex = font.material != null ? font.material.mainTexture as Texture2D : null;
                if (tex != null) j.Key("tex").Arr().Val(tex.width).Val(tex.height).EndArr();
                if (rebuilt != rebuiltRead) j.Prop("rebuiltAfter", true);
            }
            Emit(j);
        }

        private static readonly Regex SizeTag = new Regex(@"<size=([^>]*)>", RegexOptions.IgnoreCase);

        private static int FloorToInt(float f) => (int)(f >= 0f ? f : f - 0.99999994f);

        /// The sizes a generation can have asked the font for, as GetStringRenderInfo and the
        /// generator's Setup make them: each best-fit try's size `cur` (the scaled size alone without
        /// best fit), the font's own for 0, and each <size=N> tag's (int)((N * scale) * (cur / initial))
        /// ((int)(cur * scale) for 0); each give or take one.
        private static HashSet<int> GlyphSizes(TextGenerationSettings s, string str, int bestSize)
        {
            float scale = s.scaleFactor;
            int fs = s.fontSize == 0 && s.font != null ? s.font.fontSize : s.fontSize;
            int maxS = Math.Max(s.resizeTextMinSize, s.resizeTextMaxSize);
            int size = Math.Min(FloorToInt(fs * scale), 500);
            int lo = Math.Min(FloorToInt(s.resizeTextMinSize * scale), 500);
            int hi = Math.Min(FloorToInt(maxS * scale), 500);
            var tries = new List<int> { size, bestSize };
            if (s.resizeTextForBestFit)
                for (int c = Math.Max(0, lo); c <= hi; c++) tries.Add(c);
            var tags = new List<int>();
            if (!string.IsNullOrEmpty(str))
                foreach (Match m in SizeTag.Matches(str))
                {
                    string p = m.Groups[1].Value.Trim().Trim('"', '\'');
                    int sign = 1, i = 0, v = 0;
                    if (i < p.Length && (p[i] == '+' || p[i] == '-')) { sign = p[i] == '-' ? -1 : 1; i++; }
                    while (i < p.Length && p[i] >= '0' && p[i] <= '9') v = v * 10 + (p[i++] - '0');
                    tags.Add(sign * v);
                }
            var sizes = new HashSet<int>();
            void Add(int x) { sizes.Add(x - 1); sizes.Add(x); sizes.Add(x + 1); }
            if (s.font != null) Add(s.font.fontSize);
            foreach (int cur in tries)
            {
                Add(cur);
                float ratio = size == 0 ? 1f : (float)cur / size;
                int baseSize = size != 0 ? FloorToInt(size * ratio + 0.5f) : (s.font != null ? s.font.fontSize : 0);
                Add(baseSize);
                foreach (int n in tags) Add(n != 0 ? (int)((n * scale) * ratio) : (int)(baseSize * scale));
            }
            return sizes;
        }

        private static string Signature(TextGenerationSettings s, string str)
        {
            var j = new Json().Obj();
            SettingsJson(j, s);
            return j.Prop("str", str).EndObj().ToString();
        }

        private static IEnumerable<Text> SceneTexts() =>
            Resources.FindObjectsOfTypeAll<Text>().Where(t => t != null && t.gameObject.scene.IsValid() && t.gameObject.scene.isLoaded && t.hideFlags == HideFlags.None)
                .OrderBy(t => PathOf(t.transform), StringComparer.Ordinal).ThenBy(t => t.GetInstanceID());

        private static readonly string[] Prefabs =
        {
            "Canvas/Canvas - Will", "Canvas/Canvas - Dungeon", "Canvas/PopUp - Options", "Canvas/PopUp - Pause", "dialogs/optionsdialog",
        };

        private static List<KeyValuePair<string, Text>> PrefabTexts()
        {
            var list = new List<KeyValuePair<string, Text>>();
            foreach (string p in Prefabs)
            {
                var go = Resources.Load<GameObject>(p);
                if (go == null) { OraclePlugin.Log.LogWarning("text: no prefab " + p); continue; }
                foreach (var t in go.GetComponentsInChildren<Text>(true))
                    list.Add(new KeyValuePair<string, Text>(p + ":" + PathOf(t.transform), t));
            }
            return list;
        }

        /// What FontSizeLocalize (on the Text) or FontSizeManager (for the rest) gives a localized
        /// Text in a language: its size and line spacing.
        private static bool SizeFor(Localize loc, string language, out int size, out float spacing)
        {
            size = 0;
            spacing = 0f;
            var fsl = loc.GetComponent<FontSizeLocalize>();
            if (fsl != null)
            {
                size = fsl.fontSize;
                spacing = fsl.lineSpacing;
                if (fsl.entries != null)
                    foreach (var e in fsl.entries)
                        if (e.language == language) { size = e.fontSize; spacing = e.lineSpacing; break; }
                return true;
            }
            var fsm = UnityEngine.Object.FindObjectOfType<FontSizeManager>();
            if (fsm == null) return false;
            size = fsm.defaultFontSize;
            spacing = fsm.defaultFontLineSpacing;
            if (fsm.entries != null)
                foreach (var e in fsm.entries)
                    if (e.language == language) { size = e.fontSize; spacing = e.lineSpacing; break; }
            return true;
        }

        private static Font FontFor(Localize loc, string secondaryTerm, string language)
        {
            if (string.IsNullOrEmpty(secondaryTerm) || secondaryTerm == "-") return null;
            string name = LocalizationManager.GetTranslation(secondaryTerm, FixForRTL: false, overrideLanguage: language);
            if (string.IsNullOrEmpty(name)) return null;
            if (loc.TranslatedObjects != null)
                foreach (var o in loc.TranslatedObjects)
                    if (o is Font lf && lf.name == name) return lf;
            return LocalizationManager.FindAsset(name) as Font;
        }

        private static TextGenerationSettings InLanguage(TextGenerationSettings s, Localize loc, string secondaryTerm, string language)
        {
            var f = FontFor(loc, secondaryTerm, language);
            if (f != null) s.font = f;
            if (s.font != null && s.font.dynamic && SizeFor(loc, language, out int size, out float spacing))
            {
                s.fontSize = size;
                s.lineSpacing = spacing;
            }
            return s;
        }

        private static IEnumerable<Sample> LiveSamples(string set, string path, Text t)
        {
            var size = t.rectTransform.rect.size;
            var s = t.GetGenerationSettings(size);
            yield return new Sample { set = set, path = path, str = t.text, s = s };
            // Text.preferredWidth's and preferredHeight's generations.
            var pw = t.GetGenerationSettings(Vector2.zero);
            pw.horizontalOverflow = HorizontalWrapMode.Overflow;
            pw.verticalOverflow = VerticalWrapMode.Overflow;
            pw.updateBounds = true;
            yield return new Sample { set = set + ":pw", path = path, str = t.text, s = pw };
            var ph = t.GetGenerationSettings(new Vector2(t.GetPixelAdjustedRect().size.x, 0f));
            ph.verticalOverflow = VerticalWrapMode.Overflow;
            ph.updateBounds = true;
            yield return new Sample { set = set + ":ph", path = path, str = t.text, s = ph };
        }

        private static IEnumerator TextProbe()
        {
            var log = OraclePlugin.Log;
            var sw = Stopwatch.StartNew();
            Open("text.jsonl");
            var langs = Languages().Where(l => l.enabled).ToList();
            var start = new Json().Obj().Prop("ev", "textStart").Prop("unity", Application.unityVersion)
                .Prop("sw", Screen.width).Prop("sh", Screen.height).Prop("language", startLanguage);
            Languages(start);
            var fsm = UnityEngine.Object.FindObjectOfType<FontSizeManager>();
            if (fsm != null)
            {
                start.Key("fontSizeManager").Obj().Prop("defaultSize", fsm.defaultFontSize).Key("defaultSpacing").Bits(fsm.defaultFontLineSpacing).Key("entries").Arr();
                foreach (var e in fsm.entries) start.Obj().Prop("language", e.language).Prop("size", e.fontSize).Key("spacing").Bits(e.lineSpacing).EndObj();
                start.EndArr().EndObj();
            }
            Emit(start);
            foreach (var f in GameFonts()) Emit(FontRow(f));
            writer.Flush();

            var seen = new HashSet<string>();
            int batch = 0;
            Func<Sample, bool> Fresh = sm => seen.Add(sm.set.Split(':')[0] + "|" + Signature(sm.s, sm.str));

            // 1. Every Text of the loaded scenes (active or not), as itself.
            var scene = SceneTexts().ToList();
            foreach (var t in scene)
                foreach (var sm in LiveSamples("scene", PathOf(t.transform), t))
                    if (Fresh(sm)) { Generate(sm); if (++batch % 200 == 0) { writer.Flush(); yield return null; } }
            log.LogInfo($"text: {scene.Count} scene texts ({genCount} generations, {sw.ElapsedMilliseconds} ms)");

            // 2. The prefabs' Texts, as serialized.
            var prefab = PrefabTexts();
            foreach (var kv in prefab)
                foreach (var sm in LiveSamples("prefab", kv.Key, kv.Value))
                    if (!string.IsNullOrEmpty(sm.str) && Fresh(sm)) { Generate(sm); if (++batch % 200 == 0) { writer.Flush(); yield return null; } }
            log.LogInfo($"text: {prefab.Count} prefab texts ({genCount} generations, {sw.ElapsedMilliseconds} ms)");

            // 3. Every localized Text in every language.
            var all = scene.Select(t => new KeyValuePair<string, Text>(PathOf(t.transform), t)).Concat(prefab).ToList();
            var termsDone = new HashSet<string>();
            foreach (var kv in all)
            {
                var t = kv.Value;
                var loc = t.GetComponent<Localize>();
                if (loc == null) continue;
                string term = string.IsNullOrEmpty(loc.mTerm) ? t.text : loc.mTerm;
                term = term?.Trim();
                if (string.IsNullOrEmpty(term) || term == "-" || Source.GetTermData(term) == null) continue;
                string secondary = !string.IsNullOrEmpty(loc.mTermSecondary) ? loc.mTermSecondary.Trim() : (t.font != null ? t.font.name : "");
                var baseS = t.GetGenerationSettings(t.rectTransform.rect.size);
                if (!termsDone.Add(term + "|" + Signature(baseS, "") + "|" + secondary + "|" + (loc.GetComponent<FontSizeLocalize>() != null))) continue;
                foreach (var l in langs)
                {
                    string str = Translate(term, l.name);
                    if (str == null) continue;
                    var sm = new Sample { set = "lang", path = kv.Key, lang = l.name, term = term, str = str, s = InLanguage(baseS, loc, secondary, l.name) };
                    if (Fresh(sm)) { Generate(sm); if (++batch % 200 == 0) { writer.Flush(); yield return null; } }
                }
            }
            log.LogInfo($"text: localized texts ({genCount} generations, {sw.ElapsedMilliseconds} ms)");

            // 4. Item names and descriptions in every language, in the bag's selected-item Texts.
            Text title = null, desc = null;
            foreach (var kv in prefab)
            {
                if (!kv.Key.StartsWith("Canvas/Canvas - Will:")) continue;
                if (title == null && kv.Key.EndsWith("Image - SelectedItem/Title/Text")) title = kv.Value;
                if (desc == null && kv.Key.Contains("Image - SelectedItem") && kv.Key.IndexOf("Desc", StringComparison.OrdinalIgnoreCase) >= 0) desc = kv.Value;
            }
            if (desc == null)
                foreach (var kv in prefab)
                    if (kv.Key.Contains("Image - DescriptionBase")) { desc = kv.Value; break; }
            log.LogInfo($"text: item texts: {(title != null ? PathOf(title.transform) : "none")}, {(desc != null ? PathOf(desc.transform) : "none")}");
            var items = Source.mTerms.Where(x => x.TermType == eTermType.Text && x.Term.StartsWith("ITEMS/") && x.Term.EndsWith("_NAME"))
                .Select(x => x.Term.Substring(0, x.Term.Length - "_NAME".Length)).OrderBy(x => x, StringComparer.Ordinal).ToList();
            var picked = new List<string>();
            for (int i = 0; i < items.Count; i += Math.Max(1, items.Count / 30)) picked.Add(items[i]);
            foreach (string item in picked)
                foreach (var pair in new[] { new KeyValuePair<string, Text>(item + "_NAME", title), new KeyValuePair<string, Text>(item + "_DESCRIPTION", desc) })
                {
                    var t = pair.Value;
                    if (t == null || Source.GetTermData(pair.Key) == null) continue;
                    var loc = t.GetComponent<Localize>();
                    string secondary = loc != null && !string.IsNullOrEmpty(loc.mTermSecondary) ? loc.mTermSecondary.Trim() : "";
                    var baseS = t.GetGenerationSettings(t.rectTransform.rect.size);
                    foreach (var l in langs)
                    {
                        string str = Translate(pair.Key, l.name);
                        if (str == null) continue;
                        var s = loc != null ? InLanguage(baseS, loc, secondary, l.name) : baseS;
                        var sm = new Sample { set = "item", path = PathOf(t.transform), lang = l.name, term = pair.Key, str = str, s = s };
                        if (Fresh(sm)) { Generate(sm); if (++batch % 200 == 0) { writer.Flush(); yield return null; } }
                    }
                }
            log.LogInfo($"text: items ({genCount} generations, {sw.ElapsedMilliseconds} ms)");

            // 5. The synthetic grid.
            foreach (var sm in Synthetic())
                if (Fresh(sm)) { Generate(sm); if (++batch % 200 == 0) { writer.Flush(); yield return null; } }
            log.LogInfo($"text: synthetic ({genCount} generations, {sw.ElapsedMilliseconds} ms)");

            Emit(new Json().Obj().Prop("ev", "textEnd").Prop("generations", genCount).Prop("rebuilds", rebuilt).Prop("ms", (int)sw.ElapsedMilliseconds));
        }

        private static readonly string[] SynthStrings =
        {
            "Hello world",
            "The quick brown fox jumps over the lazy dog. 0123456789",
            "Line one\nLine two\n\nLine four",
            "  leading and trailing spaces  ",
            "averyveryverylongwordwithoutanyspacesatallthatmustbreaksomewhere",
            "a\tb\tc\td",
            "a\r\nb\rc\n",
            "<b>Bold</b> <i>italic</i> <color=#ff0000>red</color> <size=20>big</size> normal",
            "<b><i>both</i></b> <color=red>named</color> <color=#00ff0080>half</color> <size=5>s</size> <b>unclosed <i>x</b> </i> <unknown>tag</unknown> a<b",
            "<color=#422909FF>1,250</color> gold <size=12>x</size><size=+2>y</size><size=0>z</size>",
            "Price: 99,999 - Sold!  … “quoted” ’s – —",
            "Pociones: ąćęłńóśźż ÁÉÍ ßäöü Да сегодня ğış",
            "你好，世界。这是一个很长的句子，用来测试自动换行。",
            "こんにちは、世界！テストです。",
            "안녕하세요 세계 테스트입니다",
            "x",
            "",
            " ",
            "\n",
            "Two words",
            "<i>An italic sentence that should wrap over several lines in a narrow box.</i>",
        };

        // Paths the game's own text doesn't take: <quad> and its parameters, <material>, <a>,
        // <size=0> (scaled twice), a tag name in the low byte of a non-ASCII unit, the colour
        // forms, sizes changing from line to line, spaces that aren't ' ' or the tab.
        private static readonly string[] QuirkStrings =
        {
            "<quad material=1 size=20 x=0.1 y=0.1 width=0.5 height=0.5>Icon then text <quad size=10 width=2 height=1> two",
            "Link <a href=x>here</a> and <A HREF=\"y\">there</A> done",
            "<material=1>mat</material> <\u0142>l-stroke</\u0142> a<size=0>zero</size>b <size=>empty</size> <SIZE=12>caps</SIZE>",
            "<size=20>Big</size>\nsmall\n<size=8>tiny</size> line\n\n<size=14>after a gap</size>",
            "<color=#ff000080>half</color> <color=#0f0>short</color> <color=#00f8>shorta</color> <color=lightblue>named</color> <color=\"red\">quoted</color> <color=gray>gray</color> <color=#12345>bad</color> <color=TRANSPARENT>gone</color>",
            "\u00a0nbsp\u3000ideo\u200bzwsp end \u2014dash\u2014words-with-hyphens-that-go-on",
            "<b=x>b with value</b> <x>y</x> <i>open",
        };

        private static Font FontNamed(string name) => GameFonts().FirstOrDefault(f => f.name == name);

        private static TextGenerationSettings Base(Font f, int size)
        {
            return new TextGenerationSettings
            {
                font = f, fontSize = size, lineSpacing = 1f, richText = true, scaleFactor = 1f, fontStyle = FontStyle.Normal,
                textAnchor = TextAnchor.UpperLeft, alignByGeometry = false, resizeTextForBestFit = false, resizeTextMinSize = 10,
                resizeTextMaxSize = 40, updateBounds = false, verticalOverflow = VerticalWrapMode.Truncate,
                horizontalOverflow = HorizontalWrapMode.Wrap, generationExtents = new Vector2(100f, 40f), pivot = new Vector2(0.5f, 0.5f),
                color = Color.white, generateOutOfBounds = false,
            };
        }

        private static IEnumerable<Sample> Synthetic()
        {
            var fairfax = FontNamed("Fairfax");
            var m5x7 = FontNamed("m5x7");
            var droid = FontNamed("DroidSansFallback");
            var arial = FontNamed("Arial");
            var jf = FontNamed("JF-Dot-Shinonome16B");
            var jeju = FontNamed("JejuGothic");
            var m3x6 = FontNamed("m3x6");
            var main = new[] { new KeyValuePair<Font, int>(fairfax, 10), new KeyValuePair<Font, int>(m5x7, 16) };
            var extents = new[] { new Vector2(60f, 24f), new Vector2(160f, 60f) };
            Sample S(string tag, TextGenerationSettings s, string str) => new Sample { set = "synth", path = tag, str = str, s = s };

            // Alignment x overflow x wrap x extents, the two main fonts.
            foreach (var fs in main)
            {
                if (fs.Key == null) continue;
                foreach (string str in SynthStrings)
                    foreach (var e in extents)
                        for (int anchor = 0; anchor < 9; anchor++)
                            for (int ov = 0; ov < 4; ov++)
                            {
                                var s = Base(fs.Key, fs.Value);
                                s.generationExtents = e;
                                s.textAnchor = (TextAnchor)anchor;
                                s.horizontalOverflow = (HorizontalWrapMode)(ov & 1);
                                s.verticalOverflow = (VerticalWrapMode)(ov >> 1);
                                yield return S("grid", s, str);
                            }
            }
            // The other fonts: two alignments, wrap and truncate.
            foreach (var fs in new[] { new KeyValuePair<Font, int>(droid, 9), new KeyValuePair<Font, int>(arial, 14), new KeyValuePair<Font, int>(jf, 9),
                                       new KeyValuePair<Font, int>(jeju, 10), new KeyValuePair<Font, int>(m3x6, 16) })
            {
                if (fs.Key == null) continue;
                foreach (string str in SynthStrings)
                    foreach (var anchor in new[] { TextAnchor.UpperLeft, TextAnchor.MiddleCenter, TextAnchor.LowerRight })
                    {
                        var s = Base(fs.Key, fs.Value);
                        s.textAnchor = anchor;
                        yield return S("fonts", s, str);
                    }
            }
            string[] few = { SynthStrings[1], SynthStrings[2], SynthStrings[7], SynthStrings[12] };
            foreach (var fs in main.Concat(new[] { new KeyValuePair<Font, int>(droid, 9) }))
            {
                if (fs.Key == null) continue;
                foreach (string str in few)
                {
                    // Line spacing.
                    foreach (float ls in new[] { 0.75f, 0.85f, 1.25f, 1.5f, 2f, 0f, -1f })
                        foreach (var anchor in new[] { TextAnchor.UpperLeft, TextAnchor.MiddleCenter, TextAnchor.LowerRight })
                        {
                            var s = Base(fs.Key, fs.Value);
                            s.lineSpacing = ls;
                            s.textAnchor = anchor;
                            yield return S("spacing", s, str);
                        }
                    // Scale factor (Canvas - GUI's 0.98888886, the scaler at other screen sizes).
                    foreach (float scale in new[] { 0.98888886f, 0.5f, 1.5f, 2f, 3f, 2.25f })
                        foreach (var anchor in new[] { TextAnchor.UpperLeft, TextAnchor.MiddleCenter, TextAnchor.LowerRight })
                            foreach (bool wrap in new[] { true, false })
                            {
                                var s = Base(fs.Key, fs.Value);
                                s.scaleFactor = scale;
                                s.generationExtents *= scale;
                                s.textAnchor = anchor;
                                s.horizontalOverflow = wrap ? HorizontalWrapMode.Wrap : HorizontalWrapMode.Overflow;
                                yield return S("scale", s, str);
                            }
                    // Pivot.
                    foreach (var pivot in new[] { new Vector2(0f, 0f), new Vector2(1f, 1f), new Vector2(0.3f, 0.7f), new Vector2(0.5f, 0f) })
                        foreach (var anchor in new[] { TextAnchor.UpperLeft, TextAnchor.MiddleCenter, TextAnchor.LowerRight })
                        {
                            var s = Base(fs.Key, fs.Value);
                            s.pivot = pivot;
                            s.textAnchor = anchor;
                            yield return S("pivot", s, str);
                        }
                    // Odd extents (pixel snapping of centred text).
                    foreach (var e in new[] { new Vector2(61f, 25f), new Vector2(60.5f, 24.5f), new Vector2(33.333332f, 12.666667f), new Vector2(0f, 0f), new Vector2(1f, 1f) })
                        foreach (var anchor in new[] { TextAnchor.UpperCenter, TextAnchor.MiddleCenter, TextAnchor.MiddleRight, TextAnchor.LowerCenter })
                        {
                            var s = Base(fs.Key, fs.Value);
                            s.generationExtents = e;
                            s.textAnchor = anchor;
                            yield return S("extents", s, str);
                        }
                    // Colour, styles, geometry alignment, out of bounds, bounds, no rich text.
                    foreach (var c in new[] { new Color(0.9058824f, 0.9058824f, 0.9058824f, 1f), new Color(0.5f, 0.25f, 0.125f, 0.5f), new Color(0.25882354f, 0.16078432f, 0.03529412f, 1f), new Color(1f, 1f, 1f, 0f), new Color(0.333f, 0.666f, 0.999f, 0.501f) })
                    {
                        var s = Base(fs.Key, fs.Value);
                        s.color = c;
                        yield return S("color", s, str);
                    }
                    for (int style = 1; style < 4; style++)
                    {
                        var s = Base(fs.Key, fs.Value);
                        s.fontStyle = (FontStyle)style;
                        yield return S("style", s, str);
                    }
                    foreach (var anchor in new[] { TextAnchor.UpperLeft, TextAnchor.MiddleCenter, TextAnchor.LowerRight })
                    {
                        var s = Base(fs.Key, fs.Value);
                        s.alignByGeometry = true;
                        s.textAnchor = anchor;
                        yield return S("geom", s, str);
                        s = Base(fs.Key, fs.Value);
                        s.generateOutOfBounds = true;
                        s.generationExtents = new Vector2(30f, 10f);
                        s.textAnchor = anchor;
                        yield return S("oob", s, str);
                        s = Base(fs.Key, fs.Value);
                        s.updateBounds = true;
                        s.textAnchor = anchor;
                        yield return S("bounds", s, str);
                        s = Base(fs.Key, fs.Value);
                        s.richText = false;
                        s.textAnchor = anchor;
                        yield return S("plain", s, str);
                    }
                    // Size 0 (the font's own size), and other sizes.
                    foreach (int size in new[] { 0, 1, 7, 9, 12, 20, 32 })
                    {
                        var s = Base(fs.Key, fs.Value);
                        s.fontSize = size;
                        yield return S("size", s, str);
                    }
                    // Best fit.
                    foreach (var e in new[] { new Vector2(30f, 10f), new Vector2(60f, 20f), new Vector2(100f, 40f), new Vector2(200f, 100f) })
                        foreach (var mm in new[] { new Vector2Int(1, 40), new Vector2Int(5, 20), new Vector2Int(10, 10), new Vector2Int(12, 8) })
                            for (int ov = 0; ov < 4; ov++)
                            {
                                var s = Base(fs.Key, fs.Value);
                                s.resizeTextForBestFit = true;
                                s.resizeTextMinSize = mm.x;
                                s.resizeTextMaxSize = mm.y;
                                s.generationExtents = e;
                                s.horizontalOverflow = (HorizontalWrapMode)(ov & 1);
                                s.verticalOverflow = (VerticalWrapMode)(ov >> 1);
                                s.textAnchor = TextAnchor.MiddleCenter;
                                yield return S("bestfit", s, str);
                            }
                }
            }
            // The quirks: two fonts, three scales, two alignments, wrapped and not, best fit.
            foreach (var fs in main)
            {
                if (fs.Key == null) continue;
                foreach (string str in QuirkStrings)
                    foreach (float scale in new[] { 1f, 2f, 0.98888886f })
                        foreach (var anchor in new[] { TextAnchor.UpperLeft, TextAnchor.MiddleCenter })
                            for (int ov = 0; ov < 3; ov++)
                            {
                                var s = Base(fs.Key, fs.Value);
                                s.scaleFactor = scale;
                                s.generationExtents = new Vector2(120f, 60f) * scale;
                                s.textAnchor = anchor;
                                s.horizontalOverflow = ov == 1 ? HorizontalWrapMode.Overflow : HorizontalWrapMode.Wrap;
                                if (ov == 2)
                                {
                                    s.resizeTextForBestFit = true;
                                    s.resizeTextMinSize = 6;
                                    s.resizeTextMaxSize = 24;
                                }
                                yield return S("quirk", s, str);
                            }
            }
        }
    }
}
