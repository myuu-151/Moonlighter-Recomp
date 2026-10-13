using System.Collections.Concurrent;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;

namespace MoonlighterTranslate;

// The game's Assembly-CSharp as decompiled in-process (Decompile.cs), compiled by Roslyn against
// the game's own managed DLLs (its Mono mscorlib, UnityEngine modules, DOTween,
// Assembly-CSharp-firstpass...), so every expression has its symbols, types, overloads and
// conversions resolved: the translator works on the semantic model, not on syntax.
public sealed class GameCompilation
{
    public string DecompDir { get; }
    public CSharpCompilation Compilation { get; }
    // Path relative to DecompDir (forward slashes) -> tree.
    public IReadOnlyDictionary<string, SyntaxTree> Trees { get; }
    // Type.FullName of the types with beforefieldinit (Decompile.cs).
    public HashSet<string> BeforeFieldInit { get; private set; } = new(StringComparer.Ordinal);

    readonly ConcurrentDictionary<SyntaxTree, SemanticModel> models = new();

    GameCompilation(string decompDir, CSharpCompilation compilation, Dictionary<string, SyntaxTree> trees)
    {
        DecompDir = decompDir;
        Compilation = compilation;
        Trees = trees;
    }

    public SemanticModel Model(SyntaxTree tree) => models.GetOrAdd(tree, t => Compilation.GetSemanticModel(t));

    public static GameCompilation Load(Options o)
    {
        string managedDir = o.Managed;
        string gameDll = Path.Combine(managedDir, "Assembly-CSharp.dll");
        if (!File.Exists(gameDll)) throw new UsageException($"no {gameDll} (pass --managed or set ML_GAME)");
        string decompDir = Decompile.Run(gameDll, o.Cache).Dir;

        var parse = new CSharpParseOptions(LanguageVersion.Latest, DocumentationMode.None);
        var files = Directory.GetFiles(decompDir, "*.cs", SearchOption.AllDirectories);
        var trees = new Dictionary<string, SyntaxTree>(StringComparer.Ordinal);
        var list = new SyntaxTree[files.Length];
        Parallel.For(0, files.Length, i =>
        {
            string text = File.ReadAllText(files[i]);
            list[i] = CSharpSyntaxTree.ParseText(text, parse, files[i]);
        });
        for (int i = 0; i < files.Length; i++)
            trees[Path.GetRelativePath(decompDir, files[i]).Replace('\\', '/')] = list[i];

        var refs = new List<MetadataReference>();
        foreach (var dll in Directory.GetFiles(managedDir, "*.dll"))
        {
            string name = Path.GetFileNameWithoutExtension(dll);
            if (name == "Assembly-CSharp") continue; // that's what we compile
            try { refs.Add(MetadataReference.CreateFromFile(dll)); } catch (BadImageFormatException) { }
        }

        var options = new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary,
            allowUnsafe: true, nullableContextOptions: NullableContextOptions.Disable,
            concurrentBuild: true, checkOverflow: false)
            .WithAssemblyIdentityComparer(DesktopAssemblyIdentityComparer.Default);
        var compilation = CSharpCompilation.Create("Assembly-CSharp", list, refs, options);
        var gcomp = new GameCompilation(Path.GetFullPath(decompDir), compilation, trees);
        string bfi = Path.Combine(decompDir, ".beforefieldinit");
        if (File.Exists(bfi)) gcomp.BeforeFieldInit = new HashSet<string>(File.ReadAllLines(bfi), StringComparer.Ordinal);
        return gcomp;
    }

    public SyntaxTree Tree(string relPath)
    {
        if (!Trees.TryGetValue(relPath.Replace('\\', '/'), out var t))
            throw new UsageException($"no {relPath} under {DecompDir}");
        return t;
    }

    // A type by its C# metadata name ("CaveGenerator", "DungeonGenerator+FloorGenerationSaveInfo",
    // "Moonlighter.DungeonGeneration.ChestGeneration").
    public INamedTypeSymbol Type(string metadataName)
    {
        var t = Compilation.Assembly.GetTypeByMetadataName(metadataName);
        if (t == null) throw new UsageException($"no type {metadataName} in the decomp");
        return t;
    }
}

public sealed class UsageException(string message) : Exception(message);
