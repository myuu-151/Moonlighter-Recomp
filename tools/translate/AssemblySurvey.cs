using System.Collections.Immutable;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;

namespace MoonlighterTranslate;

// `survey-assembly [names...] [--out progress/ui_assembly_gaps.tsv]`: another of the game's managed
// assemblies (default: UnityEngine.UI, TextMeshPro-2017.3-Runtime, DOTween, DOTweenPro, the DLLs in
// Moonlighter_Data/Managed) run through the translator as `survey` runs Assembly-CSharp: decompiled with the
// same lowered settings (Decompile.Run; the cache key is the DLL's hash), compiled by Roslyn against the
// assemblies it references (their closure in the Managed folder), every type translated in memory, with
// bindings.tsv less its lines binding the surveyed assembly's own types (DOTween's are bound for the game).
// Writes <out> (every API of another assembly it uses that bindings.tsv doesn't bind, classified native /
// managed / bcl from the metadata) and its _native.tsv sibling (ui_assembly_native.tsv: the non-BCL ones
// grouped by type); prints a summary.
// Only API names and counts are written: the decompiled code stays in the cache (recomp/gen, gitignored).
public static class AssemblySurvey
{
    static readonly string[] DefaultAssemblies = { "UnityEngine.UI", "TextMeshPro-2017.3-Runtime", "DOTween", "DOTweenPro" };

    sealed class GapRow
    {
        public string Kind, Api, ApiAssembly, TypeKey, Native, Detail;
        public int Methods, Lines;
        public readonly SortedSet<string> Files = new(StringComparer.Ordinal);
        public List<string> Sites = new();
        public List<IMethodSymbol> Users = new();
    }

    sealed class Construct
    {
        public int Methods, Lines;
        public readonly SortedSet<string> Sites = new(StringComparer.Ordinal);
    }

    sealed class Result
    {
        public string Name, AsmName;
        public int Files, FileLines, Types, TopTypes, RoslynErrors, BindingsDropped;
        public int Methods, Lines, Ok, OkLines, NeedBind, NeedBindLines, Unsupported, UnsupportedLines;
        public int BclOnly, BclOnlyLines, ManagedOnly, ManagedOnlyLines, Native, NativeLines; // of NeedBind
        public readonly Dictionary<string, Construct> Constructs = new(StringComparer.Ordinal);
        public readonly List<GapRow> Gaps = new();
        public readonly Dictionary<IMethodSymbol, int> LinesOf = new(SymbolEqualityComparer.Default);
        public readonly List<string> TopErrors = new();
    }

    public static int Run(Options o)
    {
        var sw = System.Diagnostics.Stopwatch.StartNew();
        var names = o.Positional.Count > 0 ? o.Positional : DefaultAssemblies.ToList();
        string outPath = o.Out ?? Path.Combine(o.Repo, "progress", "ui_assembly_gaps.tsv");
        string nativePath = outPath.EndsWith("_gaps.tsv", StringComparison.Ordinal)
            ? outPath[..^"_gaps.tsv".Length] + "_native.tsv"
            : Path.Combine(Path.GetDirectoryName(Path.GetFullPath(outPath)), Path.GetFileNameWithoutExtension(outPath) + "_native.tsv");
        var meta = new MetadataIndex(o.Managed);
        var results = new List<Result>();
        foreach (var name in names) results.Add(SurveyOne(o, name, meta));
        WriteGaps(outPath, results);
        WriteNative(nativePath, results);
        foreach (var r in results) Print(r);
        Console.WriteLine($"wrote {outPath} and {nativePath} ({sw.Elapsed.TotalSeconds:F1} s)");
        return 0;
    }

    // ---- one assembly --------------------------------------------------------------------------------

    static Result SurveyOne(Options o, string name, MetadataIndex meta)
    {
        string dll = Path.Combine(o.Managed, name.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) ? name : name + ".dll");
        if (!File.Exists(dll)) throw new UsageException($"no {dll} (pass --managed or set ML_GAME)");
        var gc = Load(dll, o.Cache);
        var r = new Result { Name = Path.GetFileNameWithoutExtension(dll), AsmName = gc.Compilation.AssemblyName };
        var errors = gc.Compilation.GetDiagnostics().Where(d => d.Severity == DiagnosticSeverity.Error).ToList();
        r.RoslynErrors = errors.Count;
        foreach (var g in errors.GroupBy(d => d.Id).OrderByDescending(g => g.Count()).Take(8))
            r.TopErrors.Add($"{g.Key} x{g.Count()}: {g.First().GetMessage()}");
        r.Files = gc.Trees.Count;
        foreach (var t in gc.Trees.Values) r.FileLines += t.GetText().Lines.Count;
        CountTypes(gc.Compilation.Assembly.GlobalNamespace, r);

        var bind = FilteredBindings(o.Bindings, gc, out r.BindingsDropped);
        var tr = new Translator(gc, bind, gc.BeforeFieldInit);
        tr.AddAll();
        tr.Run();

        var fileOf = new Dictionary<IMethodSymbol, string>(SymbolEqualityComparer.Default);
        foreach (var (m, mr) in tr.Methods)
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
            fileOf[m] = file;
            r.LinesOf[m] = lines;
            r.Methods++;
            r.Lines += lines;
            if (mr.Translated) { r.Ok++; r.OkLines += lines; }
            else if (mr.Error != null)
            {
                r.Unsupported++;
                r.UnsupportedLines += lines;
                string key = ConstructName(mr.Error);
                if (!r.Constructs.TryGetValue(key, out var c)) r.Constructs[key] = c = new Construct();
                c.Methods++;
                c.Lines += lines;
                c.Sites.Add(Site(m));
            }
            else { r.NeedBind++; r.NeedBindLines += lines; }
        }

        // Every API the translator found unbound, classified; methods with an unsupported construct
        // count too (the API is needed either way; gaps after the construct aren't seen).
        var nativeOf = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var (key, (kind, symbol, users)) in tr.Gaps)
        {
            var (native, detail) = meta.Classify(symbol, kind);
            nativeOf[key] = native;
            var row = new GapRow
            {
                Kind = kind,
                Api = key[(key.IndexOf(' ') + 1)..],
                ApiAssembly = symbol.ContainingAssembly?.Name ?? "?",
                TypeKey = symbol is ITypeSymbol ts ? Bindings.TypeKey(ts) : symbol.ContainingType != null ? Bindings.TypeKey(symbol.ContainingType) : "?",
                Native = native,
                Detail = detail,
            };
            foreach (var u in users)
            {
                if (!r.LinesOf.TryGetValue(u, out int l)) continue;
                row.Users.Add(u);
                row.Methods++;
                row.Lines += l;
                row.Files.Add(fileOf[u]);
            }
            row.Sites = row.Users.Select(Site).OrderBy(s => s, StringComparer.Ordinal).Take(4).ToList();
            r.Gaps.Add(row);
        }
        foreach (var (m, mr) in tr.Methods)
        {
            if (mr.Translated || mr.Error != null || !r.LinesOf.TryGetValue(m, out int l)) continue;
            var kinds = mr.Gaps.Select(g => nativeOf.GetValueOrDefault(g, "managed")).ToList();
            if (kinds.Contains("native")) { r.Native++; r.NativeLines += l; }
            else if (kinds.Contains("managed")) { r.ManagedOnly++; r.ManagedOnlyLines += l; }
            else { r.BclOnly++; r.BclOnlyLines += l; }
        }
        return r;
    }

    static string Site(IMethodSymbol m) => $"{Names.FullName(m.ContainingType)}.{m.Name}";

    // A construct's name without the code some messages quote.
    static string ConstructName(string error)
    {
        string e = error.Replace('\n', ' ').Replace('\r', ' ').Replace('\t', ' ');
        if (e.StartsWith("invalid C#", StringComparison.Ordinal)) return "invalid C# (a Roslyn error in the decompiled code)";
        if (e.StartsWith("temporary without a type", StringComparison.Ordinal)) return "temporary without a type";
        return e.Length > 120 ? e[..120] : e;
    }

    static void CountTypes(INamespaceOrTypeSymbol ns, Result r)
    {
        foreach (var m in ns.GetMembers())
        {
            if (m is INamespaceSymbol n) CountTypes(n, r);
            else if (m is INamedTypeSymbol t)
            {
                if (t.ContainingType == null) r.TopTypes++;
                r.Types++;
                CountTypes(t, r);
            }
        }
    }

    // ---- loading: GameCompilation.Load for any of the Managed folder's DLLs ---------------------------

    static GameCompilation Load(string dll, string cache)
    {
        string asmName;
        using (var pe = new PEReader(File.OpenRead(dll)))
        {
            var md = pe.GetMetadataReader();
            asmName = md.GetString(md.GetAssemblyDefinition().Name);
        }
        string decompDir = Decompile.Run(dll, cache).Dir;

        var parse = new CSharpParseOptions(LanguageVersion.Latest, DocumentationMode.None);
        var files = Directory.GetFiles(decompDir, "*.cs", SearchOption.AllDirectories);
        var list = new SyntaxTree[files.Length];
        Parallel.For(0, files.Length, i => list[i] = CSharpSyntaxTree.ParseText(File.ReadAllText(files[i]), parse, files[i]));
        var trees = new Dictionary<string, SyntaxTree>(StringComparer.Ordinal);
        for (int i = 0; i < files.Length; i++)
            trees[Path.GetRelativePath(decompDir, files[i]).Replace('\\', '/')] = list[i];

        var refs = new List<MetadataReference>();
        foreach (var path in ReferenceClosure(dll))
            try { refs.Add(MetadataReference.CreateFromFile(path)); } catch (BadImageFormatException) { }

        // Its own name, so the engine modules' InternalsVisibleTo (UnityEngine.UI's) apply as they do in Unity.
        var options = new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary,
            allowUnsafe: true, nullableContextOptions: NullableContextOptions.Disable,
            concurrentBuild: true, checkOverflow: false)
            .WithAssemblyIdentityComparer(DesktopAssemblyIdentityComparer.Default);
        var compilation = CSharpCompilation.Create(asmName, list, refs, options);

        // GameCompilation's constructor is private (its Load is Assembly-CSharp's); same object, other DLL.
        var ctor = typeof(GameCompilation).GetConstructor(BindingFlags.NonPublic | BindingFlags.Instance, null,
            new[] { typeof(string), typeof(CSharpCompilation), typeof(Dictionary<string, SyntaxTree>) }, null)
            ?? throw new UsageException("GameCompilation's constructor changed: update AssemblySurvey.Load");
        var gc = (GameCompilation)ctor.Invoke(new object[] { Path.GetFullPath(decompDir), compilation, trees });
        string bfi = Path.Combine(decompDir, ".beforefieldinit");
        if (File.Exists(bfi))
            typeof(GameCompilation).GetProperty(nameof(GameCompilation.BeforeFieldInit))!
                .SetValue(gc, new HashSet<string>(File.ReadAllLines(bfi), StringComparer.Ordinal));
        return gc;
    }

    // The DLLs of the folder the assembly references, transitively (the assembly itself excluded).
    static List<string> ReferenceClosure(string dll)
    {
        string dir = Path.GetDirectoryName(Path.GetFullPath(dll));
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { Path.GetFileNameWithoutExtension(dll) };
        var result = new List<string>();
        var queue = new Queue<string>();
        queue.Enqueue(dll);
        while (queue.Count > 0)
        {
            using var pe = new PEReader(File.OpenRead(queue.Dequeue()));
            var md = pe.GetMetadataReader();
            seen.Add(md.GetString(md.GetAssemblyDefinition().Name));
            foreach (var h in md.AssemblyReferences)
            {
                string n = md.GetString(md.GetAssemblyReference(h).Name);
                if (!seen.Add(n)) continue;
                string p = Path.Combine(dir, n + ".dll");
                if (!File.Exists(p)) continue;
                result.Add(p);
                queue.Enqueue(p);
            }
        }
        result.Sort(StringComparer.OrdinalIgnoreCase);
        return result;
    }

    // bindings.tsv without the lines that bind the surveyed assembly's own types (they would make its
    // types "bound", i.e. not translated).
    static Bindings FilteredBindings(string path, GameCompilation gc, out int dropped)
    {
        var own = new HashSet<string>(StringComparer.Ordinal);
        void Walk(INamespaceOrTypeSymbol ns)
        {
            foreach (var m in ns.GetMembers())
                if (m is INamespaceSymbol n) Walk(n);
                else if (m is INamedTypeSymbol t) { own.Add(Bindings.TypeKey(t)); Walk(t); }
        }
        Walk(gc.Compilation.Assembly.GlobalNamespace);
        dropped = 0;
        var lines = new List<string>();
        foreach (var raw in File.ReadLines(path))
        {
            var f = raw.Split('\t');
            if (f.Length >= 2 && !raw.TrimStart().StartsWith('#') && own.Contains(TypePartOfKey(f[0].Trim(), f[1].Trim())))
            {
                dropped++;
                continue;
            }
            lines.Add(raw);
        }
        string tmp = Path.Combine(Path.GetTempPath(), $"mltranslate-bindings-{Environment.ProcessId}.tsv");
        File.WriteAllLines(tmp, lines);
        try { return Bindings.Load(tmp); }
        finally { File.Delete(tmp); }
    }

    // The type of a binding's key (Bindings.KeyOf): `type`/`members` lines name it; members are
    // Type.member, Type.Method(params), Type.this[params].
    static string TypePartOfKey(string kind, string key)
    {
        if (kind is "type" or "members") return key;
        int cut = key.IndexOf('(');
        int ix = key.IndexOf(".this[", StringComparison.Ordinal);
        if (ix >= 0 && (cut < 0 || ix < cut)) return key[..ix];
        string head = cut >= 0 ? key[..cut] : key;
        int depth = 0;
        for (int i = head.Length - 1; i >= 0; i--)
        {
            char c = head[i];
            if (c == '>') depth++;
            else if (c == '<') depth--;
            else if (c == '.' && depth == 0) return head[..i];
        }
        return head;
    }

    // ---- output --------------------------------------------------------------------------------------

    static string Pct(int a, int b) => b == 0 ? "-" : (100.0 * a / b).ToString("F1") + "%";

    static string Totals(Result r) =>
        $"{r.Name}: {r.TopTypes} top-level types ({r.Types} with nested and compiler-generated), {r.Files} files, {r.FileLines} decompiled lines; " +
        $"{r.Methods} methods ({r.Lines} lines): {r.Ok} translate today ({Pct(r.Ok, r.Methods)}; lines {Pct(r.OkLines, r.Lines)}), " +
        $"{r.NeedBind} need bindings ({r.Native} need a native API, {r.ManagedOnly} only managed and BCL ones, {r.BclOnly} only BCL ones), " +
        $"{r.Unsupported} unsupported; {r.Gaps.Count} APIs missing ({r.Gaps.Count(g => g.Native == "native")} native, " +
        $"{r.Gaps.Count(g => g.Native == "managed")} managed, {r.Gaps.Count(g => g.Native == "bcl")} BCL); Roslyn errors {r.RoslynErrors}";

    static void WriteGaps(string path, List<Result> results)
    {
        var sb = new StringBuilder();
        sb.Append("# The managed assemblies the game ships besides its own code (Moonlighter_Data/Managed), run through tools/translate\n");
        sb.Append("# as the game's Assembly-CSharp is (`mltranslate survey-assembly`, tools/translate/AssemblySurvey.cs): decompiled with the\n");
        sb.Append("# same lowered settings, compiled by Roslyn against the DLLs they reference, every type translated in memory with\n");
        sb.Append("# tools/translate/bindings.tsv (less its bindings of the surveyed assembly's own types). Each row is an API of ANOTHER\n");
        sb.Append("# assembly one of them uses that bindings.tsv doesn't bind. Sorted by assembly, then lines blocked.\n");
        sb.Append("#   assembly      the surveyed assembly\n");
        sb.Append("#   api_assembly  the assembly defining the API (UnityEngine.CoreModule, mscorlib, UnityEngine.UI for TextMeshPro...)\n");
        sb.Append("#   native        native: the engine implements it (an InternalCall method, an accessor that is one, or a wrapper\n");
        sb.Append("#                   of one in an engine module: a body calling an X_Injected/INTERNAL_ icall, or a body of at most 48\n");
        sb.Append("#                   IL bytes calling a native one and otherwise only code reaching no icall, as an overload forwarding\n");
        sb.Append("#                   to one through a conversion); a type: a class of an engine\n");
        sb.Append("#                   module deriving from UnityEngine.Object, or a class with icalls (a native pointer inside).\n");
        sb.Append("#                   The hand-written layer's work (recomp/src/unity + bind).\n");
        sb.Append("#                 managed: C# in that assembly (translatable like the game's code; for a method, `detail` lists\n");
        sb.Append("#                   the icalls its body reaches through its managed calls, 8 levels deep; structs and enums are\n");
        sb.Append("#                   managed). bcl: mscorlib/System/System.Core... (recomp/src/cs)\n");
        sb.Append("#   kind          as in unity_api_gaps.tsv: type (no C++ mapping), get/set (a property), call, new (a constructor),\n");
        sb.Append("#                   op (an operator or conversion), field, event/add/remove, delegate (a method as a delegate), base\n");
        sb.Append("#   methods/lines the surveyed assembly's methods (their decompiled C# lines) using it; a method missing several APIs\n");
        sb.Append("#                   counts for each; 0: only named in declarations (a field's or a signature's type)\n");
        sb.Append("#   classes       the surveyed assembly's files (top-level types) with such methods\n");
        sb.Append("#   detail        how it is native (icall, the icalls a wrapper reaches, [its UnityEngine.Bindings attributes]),\n");
        sb.Append("#                   the icalls a managed body reaches, or what kind of type it is\n");
        sb.Append("# Totals (\"translate today\": every construct supported and every API bound):\n");
        foreach (var r in results) sb.Append("#   ").Append(Totals(r)).Append('\n');
        sb.Append("# Unsupported constructs (methods, lines):\n");
        foreach (var r in results)
            foreach (var (k, c) in r.Constructs.OrderByDescending(kv => kv.Value.Methods).ThenBy(kv => kv.Key, StringComparer.Ordinal))
                sb.Append("#   ").Append(r.Name).Append(": ").Append(k).Append(" (").Append(c.Methods).Append(", ").Append(c.Lines).Append("): ")
                  .Append(string.Join(", ", c.Sites.Take(4))).Append('\n');
        sb.Append("# assembly\tapi_assembly\tnative\tkind\tapi\tmethods\tlines\tclasses\tdetail\tcall sites (some)\n");
        foreach (var r in results)
            foreach (var g in r.Gaps.OrderByDescending(g => g.Lines).ThenByDescending(g => g.Methods).ThenBy(g => g.Api, StringComparer.Ordinal).ThenBy(g => g.Kind, StringComparer.Ordinal))
                sb.Append(r.Name).Append('\t').Append(g.ApiAssembly).Append('\t').Append(g.Native).Append('\t').Append(g.Kind).Append('\t')
                  .Append(g.Api).Append('\t').Append(g.Methods).Append('\t').Append(g.Lines).Append('\t').Append(g.Files.Count).Append('\t')
                  .Append(g.Detail).Append('\t').Append(string.Join(", ", g.Sites)).Append('\n');
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)));
        File.WriteAllText(path, sb.ToString(), new UTF8Encoding(false));
    }

    // The non-BCL APIs grouped by their type: what the hand-written layer needs per Unity type.
    sealed class TypeGroup
    {
        public string Assembly, Type;
        public readonly SortedSet<string> Native = new(StringComparer.Ordinal), Managed = new(StringComparer.Ordinal);
        public readonly Dictionary<string, HashSet<IMethodSymbol>> Users = new(StringComparer.Ordinal); // surveyed assembly -> methods
        public int Methods, Lines;
    }

    static List<TypeGroup> Groups(List<Result> results)
    {
        var groups = new Dictionary<string, TypeGroup>(StringComparer.Ordinal);
        foreach (var r in results)
            foreach (var g in r.Gaps.Where(g => g.Native != "bcl"))
            {
                string k = g.ApiAssembly + "|" + g.TypeKey;
                if (!groups.TryGetValue(k, out var tg)) groups[k] = tg = new TypeGroup { Assembly = g.ApiAssembly, Type = g.TypeKey };
                string member = g.Kind == "type" ? "(type)" : g.Kind + " " + (g.Api.StartsWith(g.TypeKey + ".", StringComparison.Ordinal) ? g.Api[(g.TypeKey.Length + 1)..] : g.Api);
                (g.Native == "native" ? tg.Native : tg.Managed).Add(member);
                if (!tg.Users.TryGetValue(r.Name, out var set)) tg.Users[r.Name] = set = new HashSet<IMethodSymbol>(SymbolEqualityComparer.Default);
                set.UnionWith(g.Users);
            }
        foreach (var tg in groups.Values)
            foreach (var r in results)
                if (tg.Users.TryGetValue(r.Name, out var set))
                {
                    tg.Methods += set.Count;
                    tg.Lines += set.Sum(m => r.LinesOf[m]);
                }
        return groups.Values.OrderByDescending(t => t.Native.Count > 0).ThenByDescending(t => t.Lines)
                            .ThenBy(t => t.Type, StringComparer.Ordinal).ToList();
    }

    static void WriteNative(string path, List<Result> results)
    {
        var sb = new StringBuilder();
        sb.Append("# progress/ui_assembly_gaps.tsv's non-BCL APIs grouped by the type that declares them (`mltranslate survey-assembly`):\n");
        sb.Append("# per Unity (or DOTween, UnityEngine.UI) type, the members the surveyed assemblies use that bindings.tsv doesn't bind,\n");
        sb.Append("# split into native ones (the engine's: to hand-write in recomp/src/unity + bind) and managed ones (C# that could be\n");
        sb.Append("# translated). Types with a native API first, then by lines blocked.\n");
        sb.Append("#   methods/lines  the surveyed methods (their decompiled C# lines) using any of the type's listed APIs (each once)\n");
        sb.Append("#   used_by        surveyed assembly:methods\n");
        sb.Append("#   members        kind member ((type): the type itself has no C++ mapping)\n");
        sb.Append("# api_assembly\ttype\tnative_apis\tmanaged_apis\tmethods\tlines\tused_by\tnative members\tmanaged members\n");
        foreach (var t in Groups(results))
            sb.Append(t.Assembly).Append('\t').Append(t.Type).Append('\t').Append(t.Native.Count).Append('\t').Append(t.Managed.Count).Append('\t')
              .Append(t.Methods).Append('\t').Append(t.Lines).Append('\t')
              .Append(string.Join(" ", t.Users.OrderBy(kv => kv.Key, StringComparer.Ordinal).Select(kv => kv.Key + ":" + kv.Value.Count))).Append('\t')
              .Append(string.Join("; ", t.Native)).Append('\t').Append(string.Join("; ", t.Managed)).Append('\n');
        File.WriteAllText(path, sb.ToString(), new UTF8Encoding(false));
    }

    static void Print(Result r)
    {
        Console.WriteLine();
        Console.WriteLine(Totals(r));
        Console.WriteLine($"  bindings.tsv lines dropped (the assembly's own types): {r.BindingsDropped}");
        foreach (var e in r.TopErrors) Console.WriteLine($"  roslyn {e}");
        foreach (var (k, c) in r.Constructs.OrderByDescending(kv => kv.Value.Methods))
            Console.WriteLine($"  unsupported: {k} x{c.Methods} ({c.Lines} lines): {string.Join(", ", c.Sites.Take(5))}");
        foreach (var lib in new[] { "native", "managed", "bcl" })
        {
            var rows = r.Gaps.Where(g => g.Native == lib).OrderByDescending(g => g.Lines).ToList();
            Console.WriteLine($"  {lib}: {rows.Count} APIs, top: " + string.Join(", ", rows.Take(12).Select(g => $"{g.Kind} {g.Api} [{g.Methods}/{g.Lines}]")));
        }
    }

    // ---- native or managed: the metadata of the API's assembly -----------------------------------------

    sealed class MetadataIndex(string managedDir)
    {
        sealed class Module
        {
            public string Name;
            public PEReader Pe;
            public MetadataReader Md;
            public readonly Dictionary<string, TypeDefinitionHandle> TopTypes = new(StringComparer.Ordinal);
        }

        readonly Dictionary<string, Module> modules = new(StringComparer.OrdinalIgnoreCase);
        readonly Dictionary<ISymbol, (string, string)> cache = new(SymbolEqualityComparer.Default);

        Module Get(string asm)
        {
            if (modules.TryGetValue(asm, out var m)) return m;
            string path = Path.Combine(managedDir, asm + ".dll");
            if (File.Exists(path))
            {
                var pe = new PEReader(ImmutableArray.Create(File.ReadAllBytes(path)));
                m = new Module { Name = asm, Pe = pe, Md = pe.GetMetadataReader() };
                foreach (var h in m.Md.TypeDefinitions)
                {
                    var td = m.Md.GetTypeDefinition(h);
                    if (!td.GetDeclaringType().IsNil) continue;
                    string ns = m.Md.GetString(td.Namespace), n = m.Md.GetString(td.Name);
                    m.TopTypes[ns.Length == 0 ? n : ns + "." + n] = h;
                }
            }
            modules[asm] = m;
            return m;
        }

        public static bool IsBcl(string asm) =>
            asm is "mscorlib" or "System" or "System.Core" or "netstandard" || asm.StartsWith("System.", StringComparison.Ordinal)
            || asm.StartsWith("Mono.", StringComparison.Ordinal);

        static bool IsEngine(string asm) =>
            asm == "UnityEngine" || asm.StartsWith("UnityEngine.", StringComparison.Ordinal) && asm.EndsWith("Module", StringComparison.Ordinal);

        public (string native, string detail) Classify(ISymbol s, string kind)
        {
            if (s is IMethodSymbol { ReducedFrom: not null } red) s = red.ReducedFrom;
            s = s.OriginalDefinition;
            string asm = s.ContainingAssembly?.Name ?? "";
            if (IsBcl(asm)) return ("bcl", "");
            string key = kind is "get" or "set" or "add" or "remove" ? kind : "";
            // (cache per symbol and accessor)
            if (key.Length == 0 && cache.TryGetValue(s, out var hit)) return hit;
            var res = s switch
            {
                INamedTypeSymbol t => ClassifyType(t),
                IPropertySymbol p => ClassifyMethod((kind == "set" ? p.SetMethod ?? p.GetMethod : p.GetMethod ?? p.SetMethod)),
                IEventSymbol e => ClassifyMethod(kind == "remove" ? e.RemoveMethod : e.AddMethod),
                IMethodSymbol m => ClassifyMethod(m),
                IFieldSymbol f => ("managed", f.IsConst ? "constant" : "field"),
                _ => ("managed", s.Kind.ToString()),
            };
            if (key.Length == 0) cache[s] = res;
            return res;
        }

        // A class of an engine module that is a UnityEngine.Object, or whose methods include icalls (an object
        // around a native pointer: TextGenerator, RectOffset, Event...), is native; a struct is a layout the
        // translator can declare (its icall members are listed one by one).
        (string, string) ClassifyType(INamedTypeSymbol t)
        {
            string asm = t.ContainingAssembly.Name;
            bool engineObject = false;
            for (var b = t; b != null; b = b.BaseType)
                if (b.ToDisplayString() == "UnityEngine.Object") engineObject = true;
            if (engineObject && IsEngine(asm)) return ("native", "engine object");
            bool icalls = false;
            var mod = Get(asm);
            if (mod != null && t.MetadataToken != 0 && MetadataTokens.EntityHandle(t.MetadataToken) is { Kind: HandleKind.TypeDefinition } eh)
                icalls = mod.Md.GetTypeDefinition((TypeDefinitionHandle)eh).GetMethods().Any(h => IsIcall(mod.Md.GetMethodDefinition(h)));
            if (icalls && t.TypeKind == TypeKind.Class) return ("native", "class with icalls");
            return ("managed", TypeKindName(t) + (engineObject ? " (UnityEngine.Object)" : "") + (icalls ? " (some members are icalls)" : ""));
        }

        static string TypeKindName(INamedTypeSymbol t) => t.TypeKind switch
        {
            TypeKind.Enum => "enum",
            TypeKind.Struct => "struct",
            TypeKind.Interface => "interface",
            TypeKind.Delegate => "delegate",
            _ => t.IsStatic ? "static class" : "class",
        };

        // A method: native if it is an icall, or a wrapper the bindings generator writes around one (a body calling an
        // X_Injected / INTERNAL_ icall, or a short body whose calls are all native: an overload forwarding to one);
        // managed otherwise, with the icalls its body reaches (through managed calls, a few levels deep).
        (string, string) ClassifyMethod(IMethodSymbol m)
        {
            if (m == null) return ("managed", "no accessor");
            m = m.OriginalDefinition;
            var mod = Get(m.ContainingAssembly.Name);
            if (mod == null || m.MetadataToken == 0 || MetadataTokens.EntityHandle(m.MetadataToken).Kind != HandleKind.MethodDefinition)
                return ("managed", "no metadata");
            var h = (MethodDefinitionHandle)MetadataTokens.EntityHandle(m.MetadataToken);
            var def = mod.Md.GetMethodDefinition(h);
            string attrs = BindingAttributes(mod, h);
            if ((def.ImplAttributes & MethodImplAttributes.InternalCall) != 0) return ("native", "icall" + attrs);
            if ((def.Attributes & MethodAttributes.PinvokeImpl) != 0) return ("native", "pinvoke" + attrs);
            if ((def.Attributes & MethodAttributes.Abstract) != 0) return ("managed", m.ContainingType.TypeKind == TypeKind.Interface ? "interface member" : "abstract");
            if ((def.ImplAttributes & MethodImplAttributes.CodeTypeMask) == MethodImplAttributes.Runtime) return ("managed", "runtime (delegate)");
            if (def.RelativeVirtualAddress == 0) return ("native", "no IL body" + attrs);
            var reached = ReachedIcalls(mod, h);
            string list = string.Join(" ", reached.Take(6)) + (reached.Count > 6 ? $" ... ({reached.Count})" : "");
            // (a short method of UnityEngine.UI or DOTween calling an icall is still their C#, e.g. UIBehaviour.IsActive)
            if (IsEngine(mod.Name) && IsNative(mod, h)) return ("native", "wrapper of " + list + attrs);
            return ("managed", (reached.Count > 0 ? "reaches icalls: " + list : "no icalls") + attrs);
        }

        sealed class MInfo
        {
            public string Name;
            public bool Icall;
            public int Size;
            public List<(Module mod, MethodDefinitionHandle h)> Callees = new();
            public bool? Native;
        }

        readonly Dictionary<(Module, MethodDefinitionHandle), MInfo> infos = new();

        MInfo Info(Module mod, MethodDefinitionHandle h)
        {
            if (infos.TryGetValue((mod, h), out var i)) return i;
            var md = mod.Md;
            var d = md.GetMethodDefinition(h);
            i = new MInfo { Name = md.GetString(md.GetTypeDefinition(d.GetDeclaringType()).Name) + "." + md.GetString(d.Name), Icall = IsIcall(d) };
            infos[(mod, h)] = i;
            if (!i.Icall && d.RelativeVirtualAddress != 0)
            {
                var body = mod.Pe.GetMethodBody(d.RelativeVirtualAddress);
                i.Size = body.Size;
                i.Callees = Callees(mod, body);
            }
            return i;
        }

        // Native: an icall; a body calling an X_Injected / INTERNAL_ icall (the bindings generator's marshalling);
        // or a short body (at most 48 IL bytes) calling something native and otherwise only code that reaches no
        // icall (a conversion, a constant: Camera.ScreenPointToRay(Vector3) -> (Vector2, eye) -> _Injected).
        bool IsNative(Module mod, MethodDefinitionHandle h)
        {
            var i = Info(mod, h);
            if (i.Icall) return true;
            if (i.Native is bool known) return known;
            i.Native = false; // a cycle is managed
            bool native = i.Callees.Any(c => Info(c.mod, c.h) is { Icall: true } ci
                                            && (ci.Name.EndsWith("_Injected", StringComparison.Ordinal) || ci.Name.Contains(".INTERNAL_", StringComparison.Ordinal)))
                       || i.Size <= 48 && i.Callees.Any(c => !IsBcl(c.mod.Name) && IsNative(c.mod, c.h))
                          && i.Callees.All(c => IsBcl(c.mod.Name) || IsNative(c.mod, c.h) || ReachedIcalls(c.mod, c.h).Count == 0);
            i.Native = native;
            return native;
        }

        readonly Dictionary<(Module, MethodDefinitionHandle), List<string>> reachedCache = new();

        // The icalls a managed body reaches through managed calls (not into the BCL), breadth first, 8 levels.
        List<string> ReachedIcalls(Module mod, MethodDefinitionHandle h)
        {
            if (reachedCache.TryGetValue((mod, h), out var cached)) return cached;
            var found = new SortedSet<string>(StringComparer.Ordinal);
            var seen = new HashSet<(Module, MethodDefinitionHandle)> { (mod, h) };
            var level = new List<(Module, MethodDefinitionHandle)> { (mod, h) };
            for (int depth = 0; depth < 8 && level.Count > 0 && seen.Count < 2000; depth++)
            {
                var next = new List<(Module, MethodDefinitionHandle)>();
                foreach (var (m, mh) in level)
                    foreach (var c in Info(m, mh).Callees)
                    {
                        if (IsBcl(c.mod.Name) || !seen.Add(c)) continue;
                        var ci = Info(c.mod, c.h);
                        if (ci.Icall) found.Add(ci.Name);
                        else next.Add(c);
                    }
                level = next;
            }
            return reachedCache[(mod, h)] = found.ToList();
        }

        static bool IsIcall(MethodDefinition d) =>
            (d.ImplAttributes & MethodImplAttributes.InternalCall) != 0 || (d.Attributes & MethodAttributes.PinvokeImpl) != 0
            || d.RelativeVirtualAddress == 0 && (d.Attributes & MethodAttributes.Abstract) == 0
               && (d.ImplAttributes & MethodImplAttributes.CodeTypeMask) != MethodImplAttributes.Runtime;

        // Unity's binding attributes (UnityEngine.Bindings: NativeMethod, FreeFunction, NativeName, NativeProperty,
        // NativeThrows, StaticAccessor...) on the method or its property.
        string BindingAttributes(Module mod, MethodDefinitionHandle h)
        {
            var md = mod.Md;
            var def = md.GetMethodDefinition(h);
            var names = new SortedSet<string>(StringComparer.Ordinal);
            void Add(CustomAttributeHandleCollection attrs)
            {
                foreach (var ah in attrs)
                {
                    var (ns, n) = AttributeType(md, md.GetCustomAttribute(ah));
                    if (ns == "UnityEngine.Bindings") names.Add(n.EndsWith("Attribute", StringComparison.Ordinal) ? n[..^9] : n);
                }
            }
            Add(def.GetCustomAttributes());
            var td = md.GetTypeDefinition(def.GetDeclaringType());
            foreach (var ph in td.GetProperties())
            {
                var p = md.GetPropertyDefinition(ph);
                var acc = p.GetAccessors();
                if (acc.Getter == h || acc.Setter == h) Add(p.GetCustomAttributes());
            }
            return names.Count == 0 ? "" : " [" + string.Join(" ", names) + "]";
        }

        static (string ns, string name) AttributeType(MetadataReader md, CustomAttribute a)
        {
            EntityHandle t = default;
            if (a.Constructor.Kind == HandleKind.MemberReference) t = md.GetMemberReference((MemberReferenceHandle)a.Constructor).Parent;
            else if (a.Constructor.Kind == HandleKind.MethodDefinition) t = md.GetMethodDefinition((MethodDefinitionHandle)a.Constructor).GetDeclaringType();
            if (t.Kind == HandleKind.TypeReference)
            {
                var tr = md.GetTypeReference((TypeReferenceHandle)t);
                return (md.GetString(tr.Namespace), md.GetString(tr.Name));
            }
            if (t.Kind == HandleKind.TypeDefinition)
            {
                var td = md.GetTypeDefinition((TypeDefinitionHandle)t);
                return (md.GetString(td.Namespace), md.GetString(td.Name));
            }
            return ("", "");
        }

        // The methods a body calls (call, callvirt, newobj, ldftn, ldvirtftn), resolved to their definitions (a
        // member reference to every overload with its name and parameter count).
        List<(Module, MethodDefinitionHandle)> Callees(Module mod, MethodBodyBlock body)
        {
            var list = new List<(Module, MethodDefinitionHandle)>();
            var r = body.GetILReader();
            while (r.RemainingBytes > 0)
            {
                int op = r.ReadByte();
                if (op == 0xFE) op = 0xFE00 | r.ReadByte();
                if (op == 0x45) { int n = r.ReadInt32(); r.Offset += 4 * n; continue; }
                if (op is 0x28 or 0x6F or 0x73 or 0xFE06 or 0xFE07)
                {
                    foreach (var c in Resolve(mod, MetadataTokens.EntityHandle(r.ReadInt32())))
                        if (!list.Contains(c)) list.Add(c);
                    continue;
                }
                r.Offset += OperandSize(op);
            }
            return list;
        }

        static int OperandSize(int c) => c switch
        {
            >= 0x0E and <= 0x13 => 1,
            0x1F => 1,
            0x20 or 0x22 => 4,
            0x21 or 0x23 => 8,
            0x27 or 0x28 or 0x29 => 4,
            >= 0x2B and <= 0x37 => 1,
            >= 0x38 and <= 0x44 => 4,
            >= 0x6F and <= 0x75 => 4,
            0x79 => 4,
            >= 0x7B and <= 0x81 => 4,
            0x8C or 0x8D or 0x8F => 4,
            0xA3 or 0xA4 or 0xA5 => 4,
            0xC2 or 0xC6 or 0xD0 => 4,
            0xDD => 4,
            0xDE => 1,
            0xFE06 or 0xFE07 => 4,
            >= 0xFE09 and <= 0xFE0E => 2,
            0xFE12 or 0xFE19 => 1,
            0xFE15 or 0xFE16 or 0xFE1C => 4,
            _ => 0,
        };

        IEnumerable<(Module, MethodDefinitionHandle)> Resolve(Module mod, EntityHandle h)
        {
            var md = mod.Md;
            switch (h.Kind)
            {
                case HandleKind.MethodDefinition:
                    return new[] { (mod, (MethodDefinitionHandle)h) };
                case HandleKind.MethodSpecification:
                    return Resolve(mod, md.GetMethodSpecification((MethodSpecificationHandle)h).Method);
                case HandleKind.MemberReference:
                {
                    var mr = md.GetMemberReference((MemberReferenceHandle)h);
                    if (mr.Parent.Kind == HandleKind.MethodDefinition) return Resolve(mod, mr.Parent);
                    var parent = mr.Parent;
                    if (parent.Kind == HandleKind.TypeSpecification) parent = GenericTypeOf(md, (TypeSpecificationHandle)parent);
                    (Module mod, TypeDefinitionHandle type) target;
                    if (parent.Kind == HandleKind.TypeDefinition) target = (mod, (TypeDefinitionHandle)parent);
                    else if (parent.Kind == HandleKind.TypeReference) target = ResolveTypeRef(mod, (TypeReferenceHandle)parent);
                    else return Array.Empty<(Module, MethodDefinitionHandle)>();
                    if (target.mod == null) return Array.Empty<(Module, MethodDefinitionHandle)>();
                    string name = md.GetString(mr.Name);
                    int count = ParamCount(md, mr.Signature);
                    var tmd = target.mod.Md;
                    return tmd.GetTypeDefinition(target.type).GetMethods()
                        .Where(dh => { var d = tmd.GetMethodDefinition(dh); return tmd.GetString(d.Name) == name && ParamCount(tmd, d.Signature) == count; })
                        .Select(dh => (target.mod, dh)).ToList();
                }
            }
            return Array.Empty<(Module, MethodDefinitionHandle)>();
        }

        // A generic instantiation's type (its first element after GENERICINST).
        static EntityHandle GenericTypeOf(MetadataReader md, TypeSpecificationHandle h)
        {
            var r = md.GetBlobReader(md.GetTypeSpecification(h).Signature);
            if (r.ReadSignatureTypeCode() != SignatureTypeCode.GenericTypeInstance) return default;
            r.ReadSignatureTypeCode(); // class / valuetype
            return r.ReadTypeHandle();
        }

        static int ParamCount(MetadataReader md, BlobHandle sig)
        {
            var r = md.GetBlobReader(sig);
            var header = r.ReadSignatureHeader();
            if (header.IsGeneric) r.ReadCompressedInteger();
            return r.ReadCompressedInteger();
        }

        (Module mod, TypeDefinitionHandle type) ResolveTypeRef(Module mod, TypeReferenceHandle h)
        {
            var md = mod.Md;
            var tr = md.GetTypeReference(h);
            string ns = md.GetString(tr.Namespace), n = md.GetString(tr.Name);
            switch (tr.ResolutionScope.Kind)
            {
                case HandleKind.AssemblyReference:
                {
                    var target = Get(md.GetString(md.GetAssemblyReference((AssemblyReferenceHandle)tr.ResolutionScope).Name));
                    return target == null ? default : FindTop(target, ns, n);
                }
                case HandleKind.TypeReference:
                {
                    var outer = ResolveTypeRef(mod, (TypeReferenceHandle)tr.ResolutionScope);
                    if (outer.mod == null) return default;
                    foreach (var nh in outer.mod.Md.GetTypeDefinition(outer.type).GetNestedTypes())
                        if (outer.mod.Md.GetString(outer.mod.Md.GetTypeDefinition(nh).Name) == n) return (outer.mod, nh);
                    return default;
                }
                case HandleKind.ModuleDefinition:
                    return FindTop(mod, ns, n);
            }
            return default;
        }

        // A top-level type, following type forwarders (UnityEngine.dll's into the modules).
        (Module, TypeDefinitionHandle) FindTop(Module mod, string ns, string n)
        {
            if (mod.TopTypes.TryGetValue(ns.Length == 0 ? n : ns + "." + n, out var th)) return (mod, th);
            foreach (var eh in mod.Md.ExportedTypes)
            {
                var et = mod.Md.GetExportedType(eh);
                if (mod.Md.GetString(et.Name) != n || mod.Md.GetString(et.Namespace) != ns) continue;
                if (et.Implementation.Kind != HandleKind.AssemblyReference) break;
                var target = Get(mod.Md.GetString(mod.Md.GetAssemblyReference((AssemblyReferenceHandle)et.Implementation).Name));
                return target == null ? default : FindTop(target, ns, n);
            }
            return default;
        }
    }
}
