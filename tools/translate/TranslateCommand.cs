using System.Text;

namespace MoonlighterTranslate;

// `translate`: the units (tools/translate/units.txt) and everything they reach, into --out
// (default recomp/gen/cpp).
public static class TranslateCommand
{
    public static int Run(Options o)
    {
        var sw = System.Diagnostics.Stopwatch.StartNew();
        var gc = GameCompilation.Load(o);
        var bind = Bindings.Load(o.Bindings);
        var tr = new Translator(gc, bind, gc.BeforeFieldInit);
        tr.AddUnits(o.Units);
        tr.Run();
        string outDir = o.Out ?? Path.Combine(o.Repo, "recomp", "gen", "cpp");
        var em = new Emitter(tr, outDir);
        em.Write();
        int ok = tr.Methods.Values.Count(r => r.Translated);
        int stub = tr.Methods.Values.Count(r => r.Stub);
        int hand = tr.Methods.Values.Count(r => r.Hand);
        int err = tr.Methods.Values.Count(r => !r.Stub && !r.Hand && r.Error != null);
        int gaps = tr.Methods.Values.Count(r => !r.Stub && !r.Hand && r.Error == null && r.Gaps.Count > 0);
        Console.WriteLine($"translated {tr.Types.Count} types, {tr.Methods.Count} methods: {ok} ok, {gaps} need bindings, {err} unsupported, {hand} by hand, {stub} stubbed ({sw.Elapsed.TotalSeconds:F1} s) -> {outDir}");
        foreach (var r in tr.Methods.Values.Where(r => !r.Stub && !r.Hand && (r.Error != null || r.Gaps.Count > 0)).Take(60))
            Console.WriteLine($"  {r.M.ContainingType.Name}.{r.M.Name}: {r.Error ?? string.Join("; ", r.Gaps)}");
        return 0;
    }
}
