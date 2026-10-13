using Microsoft.CodeAnalysis;

namespace MoonlighterTranslate;

// mltranslate: the C# -> C++ translator's command line (docs/translator_plan.md).
//
//   decompile  decompiles the game's Assembly-CSharp.dll into the cache (and prints where)
//   survey     [--classes progress/classes.tsv] [--ported progress/ported.tsv]
//              [--out progress/class_deps.tsv] [--summary <file.md>]
//   translate  [--units tools/translate/units.txt] [--out recomp/gen/cpp] [--broken f]
//              (--broken: methods, one C++ name a line (gen::Type::Method), translated as
//              throwing stubs: what doesn't compile yet in a whole-game build, tools/gcsize)
//   diag       the decompiled code's compile errors as Roslyn sees them (a health check)
//
// Common options: --managed <dir> the game's Managed folder (default $ML_GAME/Moonlighter_Data/Managed,
//                 else <main checkout>/Moonlighter/Moonlighter_Data/Managed); the input is its
//                 Assembly-CSharp.dll, the other DLLs there are the references.
//                 --cache <dir> where decompiled code is kept (default recomp/gen/decompiled)
//                 --repo <dir> (default: the repository this tool sits in)
public static class Program
{
    public static int Main(string[] args)
    {
        try
        {
            if (args.Length == 0) throw new UsageException("usage: mltranslate survey|translate|diag [options] (see Program.cs)");
            var opts = Options.Parse(args.Skip(1).ToArray());
            switch (args[0])
            {
                case "decompile":
                {
                    var r = Decompile.Run(Path.Combine(opts.Managed, "Assembly-CSharp.dll"), opts.Cache);
                    Console.WriteLine($"{r.Files.Count} files in {r.Dir}");
                    return 0;
                }
                case "survey": return Survey.Run(opts);
                case "translate": return TranslateCommand.Run(opts);
                case "diag": return Diag(opts);
                case "survey-assembly": return AssemblySurvey.Run(opts);
                default: throw new UsageException($"unknown command {args[0]}");
            }
        }
        catch (UsageException e)
        {
            Console.Error.WriteLine(e.Message);
            return 2;
        }
    }

    static int Diag(Options o)
    {
        var gc = GameCompilation.Load(o);
        var diags = gc.Compilation.GetDiagnostics().Where(d => d.Severity == DiagnosticSeverity.Error).ToList();
        Console.WriteLine($"{gc.Trees.Count} files, {diags.Count} errors");
        foreach (var g in diags.GroupBy(d => d.Id).OrderByDescending(g => g.Count()).Take(15))
            Console.WriteLine($"  {g.Key} x{g.Count()}: {g.First().GetMessage()}  ({g.First().Location.GetLineSpan()})");
        return 0;
    }
}

public sealed class Options
{
    public string Repo = FindRepo();
    public string Managed;
    public string Cache;
    public string Units;
    public string Out;
    public string Classes;
    public string Ported;
    public string Bindings;
    public string Summary;
    public string Broken;
    public List<string> Positional = new();

    // The main checkout holds the game copy (Moonlighter/); worktrees sit next to it in ml-wt/.
    static string MainCheckout(string repo)
    {
        if (Directory.Exists(Path.Combine(repo, "Moonlighter"))) return repo;
        string sibling = Path.GetFullPath(Path.Combine(repo, "..", "..", "moonlighter"));
        return Directory.Exists(Path.Combine(sibling, "Moonlighter")) ? sibling : repo;
    }

    static string FindRepo()
    {
        string dir = AppContext.BaseDirectory;
        while (dir != null && !File.Exists(Path.Combine(dir, "recomp", "CMakeLists.txt"))) dir = Path.GetDirectoryName(dir);
        return dir ?? Directory.GetCurrentDirectory();
    }

    public static Options Parse(string[] args)
    {
        var o = new Options();
        for (int i = 0; i < args.Length; i++)
        {
            string a = args[i];
            string Next() => i + 1 < args.Length ? args[++i] : throw new UsageException($"{a} needs a value");
            switch (a)
            {
                case "--repo": o.Repo = Next(); break;
                case "--cache": o.Cache = Next(); break;
                case "--units": o.Units = Next(); break;
                case "--managed": o.Managed = Next(); break;
                case "--out": o.Out = Next(); break;
                case "--classes": o.Classes = Next(); break;
                case "--ported": o.Ported = Next(); break;
                case "--bindings": o.Bindings = Next(); break;
                case "--summary": o.Summary = Next(); break;
                case "--broken": o.Broken = Next(); break;
                default:
                    if (a.StartsWith("--")) throw new UsageException($"unknown option {a}");
                    o.Positional.Add(a);
                    break;
            }
        }
        string main = MainCheckout(o.Repo);
        o.Cache ??= Path.Combine(o.Repo, "recomp", "gen", "decompiled");
        o.Units ??= Path.Combine(o.Repo, "tools", "translate", "units.txt");
        string game = Environment.GetEnvironmentVariable("ML_GAME") ?? Path.Combine(main, "Moonlighter");
        o.Managed ??= Path.Combine(game, "Moonlighter_Data", "Managed");
        o.Classes ??= Path.Combine(o.Repo, "progress", "classes.tsv");
        o.Ported ??= Path.Combine(o.Repo, "progress", "ported.tsv");
        o.Bindings ??= Path.Combine(o.Repo, "tools", "translate", "bindings.tsv");
        return o;
    }
}
