using System.Text;
using Microsoft.CodeAnalysis;

namespace MoonlighterTranslate;

// What translates today: the whole game run through the translator (every type a unit), each
// method's outcome, rolled up per C# file (progress/class_deps.tsv) and per missing binding
// (progress/unity_api_gaps.tsv: the APIs the hand-written layer still lacks, with the methods and
// lines of game code each one blocks).
public sealed class TranslationSurvey
{
    public sealed class FileStats
    {
        public int Methods, MethodsOk, Lines, LinesOk;
        public readonly Dictionary<string, int> Blockers = new(StringComparer.Ordinal); // gap or error -> lines
    }

    public sealed class GapStats
    {
        public string Kind, Api, Library;
        public int Methods, Lines;
        public readonly SortedSet<string> Files = new(StringComparer.Ordinal);
        public readonly List<string> Sites = new();
    }

    public readonly Dictionary<string, FileStats> Files = new(StringComparer.Ordinal);
    public readonly Dictionary<string, GapStats> Gaps = new(StringComparer.Ordinal);
    public readonly Dictionary<string, int> Errors = new(StringComparer.Ordinal); // construct -> methods
    public int MethodsTotal, MethodsOk;

    public static TranslationSurvey Run(GameCompilation gc, Bindings bind)
    {
        var tr = new Translator(gc, bind, gc.BeforeFieldInit);
        tr.AddAll();
        tr.Run();
        var s = new TranslationSurvey();
        foreach (var (m, r) in tr.Methods)
        {
            var syntax = m.DeclaringSyntaxReferences.FirstOrDefault();
            string file = syntax != null ? Path.GetRelativePath(gc.DecompDir, syntax.SyntaxTree.FilePath).Replace('\\', '/')
                        : m.ContainingType.DeclaringSyntaxReferences.Select(x => Path.GetRelativePath(gc.DecompDir, x.SyntaxTree.FilePath).Replace('\\', '/')).FirstOrDefault();
            if (file == null) continue;
            int lines = 1;
            if (syntax != null)
            {
                var span = syntax.SyntaxTree.GetLineSpan(syntax.Span);
                lines = span.EndLinePosition.Line - span.StartLinePosition.Line + 1;
            }
            if (!s.Files.TryGetValue(file, out var fs)) s.Files[file] = fs = new FileStats();
            fs.Methods++;
            fs.Lines += lines;
            s.MethodsTotal++;
            if (r.Translated)
            {
                fs.MethodsOk++;
                fs.LinesOk += lines;
                s.MethodsOk++;
                continue;
            }
            if (r.Error != null)
            {
                string key = "unsupported: " + r.Error;
                fs.Blockers[key] = fs.Blockers.GetValueOrDefault(key) + lines;
                s.Errors[r.Error] = s.Errors.GetValueOrDefault(r.Error) + 1;
                continue;
            }
            foreach (var g in r.Gaps)
            {
                fs.Blockers[g] = fs.Blockers.GetValueOrDefault(g) + lines;
                if (!s.Gaps.TryGetValue(g, out var gs))
                {
                    int sp = g.IndexOf(' ');
                    var info = tr.Gaps[g];
                    s.Gaps[g] = gs = new GapStats { Kind = g[..sp], Api = g[(sp + 1)..], Library = LibraryOf(info.symbol) };
                }
                gs.Methods++;
                gs.Lines += lines;
                gs.Files.Add(file);
                if (gs.Sites.Count < 6) gs.Sites.Add($"{Names.FullName(m.ContainingType)}.{m.Name}");
            }
        }
        return s;
    }

    static string LibraryOf(ISymbol s)
    {
        var asm = s.ContainingAssembly?.Name ?? "";
        if (asm.StartsWith("UnityEngine")) return asm == "UnityEngine.UI" ? "Unity UI" : "Unity";
        if (asm is "DOTween" or "DOTweenPro" or "DemiLib") return "DOTween";
        if (asm.StartsWith("TextMeshPro")) return "TMPro";
        if (asm == "Assembly-CSharp-firstpass")
        {
            var ns = s.ContainingNamespace;
            while (ns != null && ns.ContainingNamespace is { IsGlobalNamespace: false }) ns = ns.ContainingNamespace;
            return ns?.Name switch { "DarkTonic" => "MasterAudio", "CodeStage" => "AntiCheat", null or "" => "firstpass", var n => n };
        }
        if (asm is "mscorlib" or "System" or "System.Core" or "netstandard" || asm.StartsWith("System.")) return "BCL";
        return asm;
    }

    public void WriteGaps(string path, Func<string, bool> counted)
    {
        var sb = new StringBuilder();
        sb.Append("# The APIs the game's code calls that the hand-written layer (recomp/src/cs, recomp/src/bind on recomp/src/unity)\n");
        sb.Append("# doesn't bind yet (tools/translate survey; bindings in tools/translate/bindings.tsv). Each blocks the methods\n");
        sb.Append("# that call it from translating: `methods` and `lines` (their C# lines; a method missing several APIs counts\n");
        sb.Append("# for each) over the counted classes (progress/classes.tsv). Sorted by lines blocked.\n");
        sb.Append("# kind: type (a type with no C++ mapping), get/set (a property), call, new (a constructor), op (an operator or\n");
        sb.Append("#   conversion), field, add/remove (an event), delegate (a method used as a delegate), base (a base constructor).\n");
        sb.Append("# library\tkind\tapi\tmethods\tlines\tclasses\tcall sites (some)\n");
        foreach (var g in Gaps.Values.Select(g => (g, files: g.Files.Where(counted).ToList()))
                                    .Where(x => x.files.Count > 0)
                                    .OrderByDescending(x => x.g.Lines).ThenBy(x => x.g.Api, StringComparer.Ordinal))
        {
            sb.Append(g.g.Library).Append('\t').Append(g.g.Kind).Append('\t').Append(g.g.Api).Append('\t').Append(g.g.Methods)
              .Append('\t').Append(g.g.Lines).Append('\t').Append(g.files.Count).Append('\t').Append(string.Join(", ", g.g.Sites)).Append('\n');
        }
        File.WriteAllText(path, sb.ToString(), new UTF8Encoding(false));
    }
}
