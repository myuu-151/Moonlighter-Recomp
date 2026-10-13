using System.Text;
using Microsoft.CodeAnalysis;

namespace MoonlighterTranslate;

// tools/translate/bindings.tsv: how the C# symbols the game uses from other assemblies (mscorlib,
// UnityEngine, DOTween, firstpass...) map onto hand-written C++: the .NET runtime in
// recomp/src/cs and the Unity layer (recomp/src/unity, with its binding facade recomp/src/bind).
//
// One binding a line, tab-separated:   kind  key  template  [flags...]
//   type     System.Collections.Generic.List<T>     cs::List<{T0}>        conv
//   members  System.Collections.Generic.List<T>     Add Remove get_Count ...   (what a conv type has)
//   get      UnityEngine.Vector2.up                 unity::Vector2::up()   pure
//   set      UnityEngine.Transform.position         {this}->set_position({value})
//   call     UnityEngine.Random.Range(float,float)  unity::Random::Range({0}, {1})
//   new      UnityEngine.Vector2.new(float,float)   unity::Vector2({0}, {1})   pure
//   field    UnityEngine.Vector2.x                  {this}.x
// Keys are Bindings.KeyOf(symbol): the declaring type's full name with its generic parameters,
// the member, and for methods the parameter types as C# writes them; `Name(*)` matches every
// overload. Templates: {this} the receiver (a pointer for classes, a value for structs), {0}..
// the arguments, {value} a setter's value, {T0}.. the declaring type's type arguments and {M0}..
// the method's, as C++ types.
// A `type` with the flag `conv` maps its members by convention (instance method M ->
// {this}->M(args), property P -> get_P()/set_P(v), indexer -> get_Item/set_Item, static ->
// Type::M, constructor -> cs::New<Type>(args) or cs::NewS<Type>(args) for a value type);
// a `members` line limits that to the listed names (get_P/set_P for properties, .ctor).
// Other flags: value (a value type in C++), pure (no side effects: callers need not order it
// against other operands), nullok (the receiver may be null: no null check), include=<header>.
public sealed class Bindings
{
    public sealed class Entry
    {
        public string Kind, Key, Template, Include;
        public bool Pure, Conv, Value, NullOk;
        public HashSet<string> Members; // for conv types with a members line
    }

    readonly Dictionary<string, Entry> entries = new(StringComparer.Ordinal);
    public readonly SortedSet<string> Includes = new(StringComparer.Ordinal);

    public static Bindings Load(string path)
    {
        var b = new Bindings();
        int n = 0;
        foreach (var raw in File.ReadLines(path))
        {
            n++;
            string line = raw.TrimEnd('\r');
            if (line.Trim().Length == 0 || line.TrimStart().StartsWith('#')) continue;
            var f = line.Split('\t');
            if (f.Length < 3) throw new UsageException($"{path}:{n}: want kind<TAB>key<TAB>template[<TAB>flags]");
            string kind = f[0].Trim(), key = f[1].Trim();
            if (kind == "members")
            {
                if (!b.entries.TryGetValue("type " + key, out var te)) throw new UsageException($"{path}:{n}: members before its type {key}");
                te.Members ??= new HashSet<string>(StringComparer.Ordinal);
                foreach (var m in f[2].Split(' ', StringSplitOptions.RemoveEmptyEntries)) te.Members.Add(m);
                continue;
            }
            var e = new Entry { Kind = kind, Key = key, Template = f[2].Trim() };
            for (int i = 3; i < f.Length; i++)
                foreach (var flag in f[i].Split(' ', StringSplitOptions.RemoveEmptyEntries))
                {
                    if (flag == "pure") e.Pure = true;
                    else if (flag == "conv") e.Conv = true;
                    else if (flag == "value") e.Value = true;
                    else if (flag == "nullok") e.NullOk = true;
                    else if (flag.StartsWith("include=")) { e.Include = flag[8..]; b.Includes.Add(e.Include); }
                    else throw new UsageException($"{path}:{n}: unknown flag {flag}");
                }
            b.entries[e.Kind + " " + e.Key] = e;
        }
        return b;
    }

    public Entry Get(string kind, string key) => entries.TryGetValue(kind + " " + key, out var e) ? e : null;

    // The binding of a member: exact key, else Name(*).
    public Entry Find(string kind, ISymbol s)
    {
        string key = KeyOf(s);
        if (entries.TryGetValue(kind + " " + key, out var e)) return e;
        int paren = key.IndexOf('(');
        if (paren >= 0 && entries.TryGetValue(kind + " " + key[..paren] + "(*)", out e)) return e;
        return null;
    }

    // Has some type binding with this simple name ("Unity:Vector2" or "Vector2")?
    public bool HasType(string libAndSimpleName)
    {
        string simple = libAndSimpleName[(libAndSimpleName.IndexOf(':') + 1)..];
        foreach (var e in entries.Values)
            if (e.Kind == "type")
            {
                string k = e.Key;
                int lt = k.IndexOf('<');
                if (lt >= 0) k = k[..lt];
                if (k[(k.LastIndexOf('.') + 1)..] == simple) return true;
            }
        return false;
    }

    public Entry FindType(ITypeSymbol t) => entries.TryGetValue("type " + TypeKey(t), out var e) ? e : null;

    // ---- keys ----------------------------------------------------------------------------------

    static readonly SymbolDisplayFormat FullType = new(
        globalNamespaceStyle: SymbolDisplayGlobalNamespaceStyle.Omitted,
        typeQualificationStyle: SymbolDisplayTypeQualificationStyle.NameAndContainingTypesAndNamespaces,
        genericsOptions: SymbolDisplayGenericsOptions.IncludeTypeParameters,
        miscellaneousOptions: SymbolDisplayMiscellaneousOptions.ExpandNullable);

    static readonly SymbolDisplayFormat ParamType = new(
        globalNamespaceStyle: SymbolDisplayGlobalNamespaceStyle.Omitted,
        typeQualificationStyle: SymbolDisplayTypeQualificationStyle.NameAndContainingTypesAndNamespaces,
        genericsOptions: SymbolDisplayGenericsOptions.IncludeTypeParameters,
        miscellaneousOptions: SymbolDisplayMiscellaneousOptions.UseSpecialTypes);

    public static string TypeKey(ITypeSymbol t) => t.OriginalDefinition.ToDisplayString(FullType).Replace(" ", "");

    public static string KeyOf(ISymbol s)
    {
        if (s is IMethodSymbol { ReducedFrom: not null } red) s = red.ReducedFrom;
        s = s.OriginalDefinition;
        if (s is ITypeSymbol t) return TypeKey(t);
        string type = s.ContainingType != null ? TypeKey(s.ContainingType) : "";
        switch (s)
        {
            case IPropertySymbol { IsIndexer: true } ix:
                return $"{type}.this[{Params(ix.Parameters)}]";
            case IMethodSymbol m:
            {
                string name = m.MethodKind switch
                {
                    MethodKind.Constructor => "new",
                    MethodKind.StaticConstructor => "cctor",
                    _ => m.Name,
                };
                if (m.IsGenericMethod) name += "<" + string.Join(",", m.TypeParameters.Select(p => p.Name)) + ">";
                return $"{type}.{name}({Params(m.Parameters)})";
            }
            default:
                return $"{type}.{s.Name}";
        }
    }

    static string Params(IEnumerable<IParameterSymbol> ps) =>
        string.Join(",", ps.Select(p => (p.RefKind switch { RefKind.Ref => "ref ", RefKind.Out => "out ", RefKind.In => "in ", _ => "" })
                                       + p.Type.ToDisplayString(ParamType).Replace(" ", "")));
}
