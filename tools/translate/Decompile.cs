using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using ICSharpCode.Decompiler.CSharp.OutputVisitor;
using System.Security.Cryptography;
using System.Text;
using ICSharpCode.Decompiler;
using ICSharpCode.Decompiler.CSharp;
using ICSharpCode.Decompiler.CSharp.Transforms;
using ICSharpCode.Decompiler.Metadata;
using ICSharpCode.Decompiler.TypeSystem;

namespace MoonlighterTranslate;

// Step 1 of the pipeline: the game's own Assembly-CSharp.dll, decompiled in-process by
// ICSharpCode.Decompiler (the engine of ILSpy, which made the repo's decomp/), one file per
// top-level type at the same relative path decomp/ uses (progress/classes.tsv's paths).
//
// The settings lower what the C# compiler lowered, so the translator sees what Mono runs:
//   - iterators (coroutines), lambdas, local functions stay the compiler's classes: the
//     `<Start>d__12` state machines with their MoveNext switch, the `<>c__DisplayClass`
//     closures, the cached `<>c.<>9__0_0` delegates. Exact by construction: the same state
//     machines, the same capture sharing, the same delegate instances.
//   - no string interpolation, object/collection initializers, pattern matching, out vars,
//     lifted nullable operators, query syntax, extension-method call syntax, optional or named
//     arguments: each appears as the calls and temporaries the IL has.
//   - ILSpy's default inlining keeps every IL local (it inlines stack slots only), so a float
//     stored to a local is rounded where Mono rounds it (docs/translator_plan.md, floats).
// Invalid identifiers (`<>c`) are escaped the way ILSpy's project export does (`_003C_003Ec`).
//
// The output is cached in <cache>/<sha of the dll + settings>/, so later runs parse only.
public static class Decompile
{
    // Bump when the settings below change (it is part of the cache key).
    const string SettingsVersion = "lowered-2";

    public static DecompilerSettings Settings()
    {
        var s = new DecompilerSettings(ICSharpCode.Decompiler.CSharp.LanguageVersion.CSharp7_3)
        {
            ThrowOnAssemblyResolveErrors = false,
            AnonymousMethods = false,
            AnonymousTypes = false,
            ExpressionTrees = false,
            YieldReturn = false,
            AsyncAwait = false,
            AsyncEnumerator = false,
            LocalFunctions = false,
            StringInterpolation = false,
            ObjectOrCollectionInitializers = false,
            DictionaryInitializers = false,
            ExtensionMethodsInCollectionInitializers = false,
            QueryExpressions = false,
            ExtensionMethods = false,
            OptionalArguments = false,
            NamedArguments = false,
            OutVariables = false,
            Discards = false,
            PatternMatching = false,
            ThrowExpressions = false,
            LiftNullables = false,
            NullPropagation = false,
            Deconstruction = false,
            TupleTypes = false,
            TupleConversions = false,
            TupleComparisons = false,
            UseDebugSymbols = false,
            ShowXmlDocumentation = false,
            RemoveDeadCode = false,
            RemoveDeadStores = false,
            AlwaysUseBraces = true,
            UseLambdaSyntax = false,
            ArrayInitializers = true,
            SwitchStatementOnString = true,
            ForEachStatement = true,
            UsingStatement = true,
            LockStatement = true,
            AutomaticProperties = true,
            AutomaticEvents = true,
            FixedBuffers = true,
            DecimalConstants = true,
            LoadInMemory = true,
        };
        return s;
    }

    public sealed class Result
    {
        public string Dir;                      // the folder of .cs files
        public List<string> Files = new();      // relative paths
    }

    // Decompiles `dll` (with the other assemblies of its folder as references) into
    // <cacheRoot>/<key>/, unless that folder is already complete.
    public static Result Run(string dll, string cacheRoot, bool log = true)
    {
        string key;
        using (var sha = SHA256.Create())
        {
            byte[] h = sha.ComputeHash(File.ReadAllBytes(dll));
            key = Convert.ToHexString(h)[..16].ToLowerInvariant() + "-" + SettingsVersion;
        }
        string dir = Path.Combine(cacheRoot, key);
        string done = Path.Combine(dir, ".complete");
        var result = new Result { Dir = dir };
        if (File.Exists(done))
        {
            result.Files.AddRange(File.ReadAllLines(done).Where(l => l.Length > 0));
            return result;
        }

        if (log) Console.Error.WriteLine($"decompiling {dll} into {dir} ...");
        var sw = System.Diagnostics.Stopwatch.StartNew();
        if (Directory.Exists(dir)) Directory.Delete(dir, true);
        Directory.CreateDirectory(dir);

        var settings = Settings();
        var module = new PEFile(dll, PEStreamOptions.PrefetchEntireImage);
        var resolver = new UniversalAssemblyResolver(dll, false, module.DetectTargetFrameworkId());
        resolver.AddSearchDirectory(Path.GetDirectoryName(dll));
        var ts = new DecompilerTypeSystem(module, resolver, settings);
        var metadata = module.Metadata;

        // One file per top-level type, at decomp/'s path (namespace folder / type name).
        var groups = new Dictionary<string, List<TypeDefinitionHandle>>(StringComparer.OrdinalIgnoreCase);
        foreach (var h in metadata.GetTopLevelTypeDefinitions())
        {
            var td = metadata.GetTypeDefinition(h);
            string name = metadata.GetString(td.Name);
            string ns = metadata.GetString(td.Namespace);
            if (name == "<Module>") continue;
            if (CSharpDecompiler.MemberIsHidden(module, h, settings)) continue;
            int tick = name.IndexOf('`');
            if (tick >= 0) name = name[..tick];
            string rel = (ns.Length == 0 ? "" : CleanDir(ns) + "/") + CleanFile(name) + ".cs";
            if (!groups.TryGetValue(rel, out var list)) groups[rel] = list = new();
            list.Add(h);
        }

        var files = groups.Keys.OrderBy(k => k, StringComparer.Ordinal).ToList();
        Parallel.ForEach(files, new ParallelOptions { MaxDegreeOfParallelism = Math.Max(1, Environment.ProcessorCount / 2) }, rel =>
        {
            var decompiler = new CSharpDecompiler(ts, settings);
            decompiler.AstTransforms.Add(new EscapeInvalidIdentifiers());
            var tree = decompiler.DecompileTypes(groups[rel]);
            var w = new StringWriter();
            tree.AcceptVisitor(new CSharpOutputVisitor(w, settings.CSharpFormattingOptions));
            string path = Path.Combine(dir, rel.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            File.WriteAllText(path, w.ToString(), new UTF8Encoding(false));
        });
        // Types with beforefieldinit (C# types without an explicit static constructor): their
        // static initialization runs at the first static field access only.
        var bfi = new List<string>();
        foreach (var h in metadata.TypeDefinitions)
        {
            var td = metadata.GetTypeDefinition(h);
            if ((td.Attributes & System.Reflection.TypeAttributes.BeforeFieldInit) == 0) continue;
            bfi.Add(FullName(metadata, h));
        }
        bfi.Sort(StringComparer.Ordinal);
        File.WriteAllLines(Path.Combine(dir, ".beforefieldinit"), bfi);
        File.WriteAllLines(done, files);
        result.Files.AddRange(files);
        if (log) Console.Error.WriteLine($"decompiled {files.Count} files in {sw.Elapsed.TotalSeconds:F1} s");
        return result;
    }

    // Type.FullName: Namespace.Outer+Inner`1
    static string FullName(MetadataReader md, TypeDefinitionHandle h)
    {
        var td = md.GetTypeDefinition(h);
        string name = md.GetString(td.Name);
        var outer = td.GetDeclaringType();
        if (!outer.IsNil) return FullName(md, outer) + "+" + name;
        string ns = md.GetString(td.Namespace);
        return ns.Length == 0 ? name : ns + "." + name;
    }

    static string CleanFile(string name)
    {
        var sb = new StringBuilder();
        foreach (char c in name) sb.Append(Path.GetInvalidFileNameChars().Contains(c) || c == '<' || c == '>' ? '_' : c);
        return sb.ToString();
    }

    static string CleanDir(string ns) => CleanFile(ns);
}
