namespace MoonlighterTranslate;

// Game methods whose translation is replaced by hand-written C++ with the same results
// (docs/translation_strategy.md, "Hand-written overrides"): the C++ body, in terms of the method's
// parameters as the translation names them (p_<name>). Each one is checked by the tests that run
// the method (the same output) and says why it exists.
public static class Overrides
{
    public static readonly Dictionary<string, string> Bodies = new(StringComparer.Ordinal)
    {
        // Copies the whole text at every comment (String.Remove): 6.2 MB of garbage for the 34 KB
        // constants file on the GameCube, 46 ms. The same text in one pass (cs/String.cpp).
        ["FsJSONManager.RemoveComments"] = "    return ::cs::RemoveJsonComments(p_text);\n",
    };

    public static string BodyOf(Microsoft.CodeAnalysis.IMethodSymbol m) =>
        Bodies.TryGetValue(m.ContainingType.Name + "." + m.Name, out var b) ? b : null;
}
