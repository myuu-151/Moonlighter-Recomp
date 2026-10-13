using System.Text;
using Microsoft.CodeAnalysis;

namespace MoonlighterTranslate;

// C++ names and types for C# symbols (docs/translator_plan.md, "Output").
//   game types       ::gen::<namespace path>::<Outer_Inner>[_<arity>]   (nested types flattened)
//   reference types  T*   (classes, interfaces, delegates, arrays, string, object)
//   value types      T    (structs, enums, primitives)
//   string/object    ::cs::String* / ::cs::Object*
//   delegates        ::cs::Delegate<R(A...)>*  (by signature)
//   arrays           ::cs::Array<T>* / ::cs::Array2<T>*
//   others           bindings.tsv
public sealed class Names
{
    readonly Translator tr;
    public Names(Translator tr) { this.tr = tr; }

    static readonly HashSet<string> Keywords = new(StringComparer.Ordinal)
    {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break", "case", "catch", "char",
        "char8_t", "char16_t", "char32_t", "class", "compl", "concept", "const", "consteval", "constexpr", "constinit",
        "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype", "default", "delete", "do", "double",
        "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "float", "for", "friend", "goto", "if",
        "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or",
        "or_eq", "private", "protected", "public", "register", "reinterpret_cast", "requires", "return", "short", "signed",
        "sizeof", "static", "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local", "throw",
        "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual", "void", "volatile",
        "wchar_t", "while", "xor", "xor_eq", "override", "final", "import", "module",
        // macros and names the C/C++ libraries may define
        "NULL", "EOF", "errno", "stdin", "stdout", "stderr", "assert", "offsetof", "TRUE", "FALSE", "min", "max",
        "INFINITY", "NAN", "major", "minor", "unix", "linux", "DOMAIN", "OVERFLOW", "UNDERFLOW", "near", "far",
        // our runtime's names that a member must not shadow inside generated classes
        "cs", "gen", "unity",
    };

    public static string Ident(string name)
    {
        var sb = new StringBuilder(name.Length + 2);
        foreach (char c in name)
            sb.Append(char.IsLetterOrDigit(c) && c < 128 || c == '_' ? c : '_');
        string s = sb.ToString();
        if (s.Length == 0 || char.IsDigit(s[0])) s = "_" + s;
        if (Keywords.Contains(s)) s += "_";
        // __ and _Upper are reserved in C++
        if (s.StartsWith("__") && !s.StartsWith("__cs")) s = "u" + s;
        if (s.Length > 1 && s[0] == '_' && char.IsUpper(s[1])) s = "u" + s;
        return s;
    }

    // ---- types ---------------------------------------------------------------------------------

    public bool IsGame(ISymbol s) => s != null && SymbolEqualityComparer.Default.Equals(s.ContainingAssembly, tr.Game);

    // Is this a reference in C++ (a pointer)?
    public static bool IsRef(ITypeSymbol t) =>
        t.TypeKind != TypeKind.TypeParameter && t.IsReferenceType;

    // The C++ type of a value of type t: T* for references.
    public string Ref(ITypeSymbol t)
    {
        string n = Type(t);
        return IsRef(t) ? n + "*" : n;
    }

    // The C++ type name of t (without the * of a reference).
    public string Type(ITypeSymbol t)
    {
        switch (t.SpecialType)
        {
            case SpecialType.System_Void: return "void";
            case SpecialType.System_Boolean: return "bool";
            case SpecialType.System_Byte: return "uint8_t";
            case SpecialType.System_SByte: return "int8_t";
            case SpecialType.System_Int16: return "int16_t";
            case SpecialType.System_UInt16: return "uint16_t";
            case SpecialType.System_Int32: return "int32_t";
            case SpecialType.System_UInt32: return "uint32_t";
            case SpecialType.System_Int64: return "int64_t";
            case SpecialType.System_UInt64: return "uint64_t";
            case SpecialType.System_Char: return "char16_t";
            case SpecialType.System_Single: return "float";
            case SpecialType.System_Double: return "double";
            case SpecialType.System_String: return "::cs::String";
            case SpecialType.System_Object: return "::cs::Object";
            case SpecialType.System_IntPtr: return "intptr_t";
            case SpecialType.System_UIntPtr: return "uintptr_t";
        }
        switch (t)
        {
            case ITypeParameterSymbol tp:
                return TypeParam(tp);
            case IArrayTypeSymbol arr:
                return arr.Rank == 1 ? $"::cs::Array<{Ref(arr.ElementType)}>" : arr.Rank == 2 ? $"::cs::Array2<{Ref(arr.ElementType)}>" : tr.Fail($"array of rank {arr.Rank}");
            case IPointerTypeSymbol:
                return tr.Fail("pointer type");
            case INamedTypeSymbol nt:
                if (nt.TypeKind == TypeKind.Delegate) return DelegateType(nt);
                if (nt.IsTupleType) return tr.Fail("tuple type");
                if (nt.OriginalDefinition.SpecialType == SpecialType.System_Nullable_T) return $"::cs::Nullable<{Ref(nt.TypeArguments[0])}>";
                if (IsGame(nt)) return GameType(nt);
                return ExternalType(nt);
        }
        return tr.Fail($"type {t}");
    }

    public string TypeParam(ITypeParameterSymbol tp) => (tp.TypeParameterKind == TypeParameterKind.Method ? "M_" : "T_") + Ident(tp.Name);

    public string DelegateType(INamedTypeSymbol d)
    {
        var inv = d.DelegateInvokeMethod;
        if (inv == null) return tr.Fail($"delegate {d}");
        if (inv.Parameters.Any(p => p.RefKind != RefKind.None)) return tr.Fail($"delegate with ref parameters {d}");
        return $"::cs::Delegate<{Ref(inv.ReturnType)}({string.Join(", ", inv.Parameters.Select(p => Ref(p.Type)))})>";
    }

    // ::gen::NS::Outer_Inner_1<args>
    public string GameType(INamedTypeSymbol nt)
    {
        tr.NeedType(nt);
        string name = QualifiedGameName(nt.OriginalDefinition);
        var args = AllTypeArguments(nt);
        if (args.Count > 0) name += "<" + string.Join(", ", args.Select(Ref)) + ">";
        return name;
    }

    // Type arguments of the flattened class: the outer types' first, then its own.
    public static List<ITypeSymbol> AllTypeArguments(INamedTypeSymbol nt)
    {
        var list = new List<ITypeSymbol>();
        void Walk(INamedTypeSymbol t)
        {
            if (t.ContainingType != null) Walk(t.ContainingType);
            list.AddRange(t.TypeArguments);
        }
        Walk(nt);
        return list;
    }
    public static List<ITypeParameterSymbol> AllTypeParameters(INamedTypeSymbol nt)
    {
        var list = new List<ITypeParameterSymbol>();
        void Walk(INamedTypeSymbol t)
        {
            if (t.ContainingType != null) Walk(t.ContainingType);
            list.AddRange(t.TypeParameters);
        }
        Walk(nt.OriginalDefinition);
        return list;
    }

    public string FlatName(INamedTypeSymbol def)
    {
        string n = Ident(def.Name) + (def.Arity > 0 ? "_" + def.Arity : "");
        return def.ContainingType != null ? FlatName(def.ContainingType) + "_" + n : n;
    }

    public static string NamespacePath(INamedTypeSymbol def)
    {
        while (def.ContainingType != null) def = def.ContainingType;
        var parts = new List<string>();
        for (var ns = def.ContainingNamespace; ns != null && !ns.IsGlobalNamespace; ns = ns.ContainingNamespace) parts.Insert(0, Ident(ns.Name));
        return string.Join("::", parts);
    }

    public string QualifiedGameName(INamedTypeSymbol def)
    {
        string ns = NamespacePath(def);
        return "::gen::" + (ns.Length > 0 ? ns + "::" : "") + FlatName(def);
    }

    // C# Type.FullName: Namespace.Outer+Inner`1
    public static string FullName(INamedTypeSymbol def)
    {
        string n = def.MetadataName;
        if (def.ContainingType != null) return FullName(def.ContainingType) + "+" + n;
        var ns = def.ContainingNamespace;
        return ns == null || ns.IsGlobalNamespace ? n : ns.ToDisplayString() + "." + n;
    }

    public string ExternalType(INamedTypeSymbol nt)
    {
        var b = tr.Bind.FindType(nt);
        if (b == null)
        {
            // A placeholder that still compiles in declarations (the methods using it are
            // reported and stubbed): one distinct type per C# type, so overloads stay apart.
            tr.Gap(nt, "type");
            uint h = 2166136261;
            foreach (char c in TypeKeyOf(nt)) h = (h ^ c) * 16777619;
            return nt.IsReferenceType ? $"::cs::UnmappedRef<0x{h:X8}u>" : $"::cs::UnmappedValue<0x{h:X8}u>";
        }
        tr.UseBinding(b);
        return Substitute(b.Template, null, AllTypeArguments(nt), null);
    }

    public bool ExternalIsValue(INamedTypeSymbol nt) => !nt.IsReferenceType;

    static string TypeKeyOf(INamedTypeSymbol nt) => nt.ToDisplayString();

    // {T0}.. from type arguments, {M0}.. from method type arguments.
    public string Substitute(string template, string receiver, IReadOnlyList<ITypeSymbol> typeArgs, IReadOnlyList<ITypeSymbol> methodArgs)
    {
        var sb = new StringBuilder();
        for (int i = 0; i < template.Length; i++)
        {
            if (template[i] == '{')
            {
                int end = template.IndexOf('}', i);
                string tok = template.Substring(i + 1, end - i - 1);
                if (tok.Length >= 2 && tok[0] == 'T' && char.IsDigit(tok[1])) { sb.Append(Ref(typeArgs[int.Parse(tok[1..])])); i = end; continue; }
                if (tok.Length >= 2 && tok[0] == 'M' && char.IsDigit(tok[1])) { sb.Append(Ref(methodArgs[int.Parse(tok[1..])])); i = end; continue; }
                if (tok == "this" && receiver != null) { sb.Append(receiver); i = end; continue; }
            }
            sb.Append(template[i]);
        }
        return sb.ToString();
    }

    // ---- members -------------------------------------------------------------------------------

    // The C++ name of a method of a game type.
    public string Method(IMethodSymbol m)
    {
        m = m.OriginalDefinition;
        if (m.ReducedFrom != null) m = m.ReducedFrom;
        switch (m.MethodKind)
        {
            case MethodKind.Constructor: return "__ctor";
            case MethodKind.StaticConstructor: return "__cctor";
            case MethodKind.Conversion:
                return Ident(m.Name) + "_" + Ident(m.ReturnType.Name);
            case MethodKind.ExplicitInterfaceImplementation:
                return InterfaceMember(m.ExplicitInterfaceImplementations[0]);
        }
        if (m.ContainingType?.TypeKind == TypeKind.Interface) return InterfaceMember(m);
        // Accessors of explicitly implemented properties/events.
        if (m.ExplicitInterfaceImplementations.Length > 0) return InterfaceMember(m.ExplicitInterfaceImplementations[0]);
        string name = Ident(m.Name);
        if (tr.HidesBase(m)) name += "_n" + tr.HideDepth(m);
        return name;
    }

    // Interface members: <Interface>[_<arity>]_<member> (IEnumerator_MoveNext).
    public string InterfaceMember(IMethodSymbol im)
    {
        im = im.OriginalDefinition;
        var it = im.ContainingType;
        string member = im.Name;
        int dot = member.LastIndexOf('.');
        if (dot >= 0) member = member[(dot + 1)..];
        return Ident(it.Name) + (it.Arity > 0 ? "_" + it.Arity : "") + "_" + Ident(member);
    }

    public string Field(IFieldSymbol f)
    {
        f = f.OriginalDefinition;
        if (f.AssociatedSymbol is IPropertySymbol p) return Ident(p.Name); // an auto-property's backing field
        if (f.AssociatedSymbol is IEventSymbol e) return Ident(e.Name);
        return Ident(f.Name);
    }

    // A float/double/integer constant as a C++ literal of the C# type.
    public static string Literal(object v, ITypeSymbol t)
    {
        switch (v)
        {
            case null: return "nullptr";
            case bool b: return b ? "true" : "false";
            case int i: return i == int.MinValue ? "(-2147483647 - 1)" : i.ToString(System.Globalization.CultureInfo.InvariantCulture);
            case uint u: return u.ToString(System.Globalization.CultureInfo.InvariantCulture) + "u";
            case long l: return l == long.MinValue ? "(-9223372036854775807LL - 1)" : l.ToString(System.Globalization.CultureInfo.InvariantCulture) + "LL";
            case ulong ul: return ul.ToString(System.Globalization.CultureInfo.InvariantCulture) + "ULL";
            case short s: return $"static_cast<int16_t>({s})";
            case ushort us: return $"static_cast<uint16_t>({us})";
            case byte by: return $"static_cast<uint8_t>({by})";
            case sbyte sb: return $"static_cast<int8_t>({sb})";
            case char c: return c >= 32 && c < 127 && c != '\'' && c != '\\' ? $"u'{c}'" : $"static_cast<char16_t>({(int)c})";
            case float f:
                if (float.IsNaN(f)) return "::cs::BitsToFloat(0x" + BitConverter.SingleToUInt32Bits(f).ToString("X8") + "u)";
                if (float.IsPositiveInfinity(f)) return "::std::numeric_limits<float>::infinity()";
                if (float.IsNegativeInfinity(f)) return "(-::std::numeric_limits<float>::infinity())";
                if (f == 0 && float.IsNegative(f)) return "(-0.0f)";
                {
                    string r = f.ToString("R", System.Globalization.CultureInfo.InvariantCulture);
                    if (float.Parse(r, System.Globalization.CultureInfo.InvariantCulture) != f) r = f.ToString("G9", System.Globalization.CultureInfo.InvariantCulture);
                    if (!r.Contains('.') && !r.Contains('E')) r += ".0";
                    if (r.Contains('E') && !r.Contains('.')) r = r.Replace("E", ".0E");
                    return r + "f";
                }
            case double d:
                if (double.IsNaN(d)) return "::cs::BitsToDouble(0x" + BitConverter.DoubleToUInt64Bits(d).ToString("X16") + "ull)";
                if (double.IsPositiveInfinity(d)) return "::std::numeric_limits<double>::infinity()";
                if (double.IsNegativeInfinity(d)) return "(-::std::numeric_limits<double>::infinity())";
                if (d == 0 && double.IsNegative(d)) return "(-0.0)";
                {
                    string r = d.ToString("R", System.Globalization.CultureInfo.InvariantCulture);
                    if (!r.Contains('.') && !r.Contains('E')) r += ".0";
                    if (r.Contains('E') && !r.Contains('.')) r = r.Replace("E", ".0E");
                    return r;
                }
            case string s:
                return StringLiteral(s);
            case decimal:
                throw new TranslateException("decimal constant");
        }
        throw new TranslateException($"constant {v.GetType()}");
    }

    public static string StringLiteral(string s)
    {
        // C++ compilers cap a string literal's length (MSVC: 16 KB a piece, 64 KB in all):
        // long ones are pieces joined once at run time.
        const int piece = 1500;
        if (s.Length > piece)
        {
            var parts = new List<string>();
            for (int i = 0; i < s.Length;)
            {
                int n = Math.Min(piece, s.Length - i);
                if (i + n < s.Length && char.IsHighSurrogate(s[i + n - 1])) n--; // keep a pair together
                parts.Add(PieceLiteral(s.Substring(i, n)));
                i += n;
            }
            return "CS_LIT_CHUNKS(" + string.Join(", ", parts) + ")";
        }
        return "CS_LIT(" + PieceLiteral(s) + ")";
    }

    static string PieceLiteral(string s)
    {
        var sb = new StringBuilder("u\"");
        bool lastHex = false;
        foreach (char c in s)
        {
            bool hexDigit = Uri.IsHexDigit(c);
            if (c == '"') sb.Append("\\\"");
            else if (c == '\\') sb.Append("\\\\");
            else if (c == '\n') sb.Append("\\n");
            else if (c == '\r') sb.Append("\\r");
            else if (c == '\t') sb.Append("\\t");
            else if (c == '?') sb.Append("\\?"); // no trigraphs
            else if (c >= 32 && c < 127)
            {
                if (lastHex && hexDigit) sb.Append("\" u\"");
                sb.Append(c);
                lastHex = false;
                continue;
            }
            else if (char.IsSurrogate(c) || c < 0xA0) { sb.Append("\\x").Append(((int)c).ToString("X4")); lastHex = true; continue; }
            else sb.Append("\\u").Append(((int)c).ToString("X4"));
            lastHex = false;
        }
        sb.Append('"');
        return sb.ToString();
    }
}

public sealed class TranslateException(string message) : Exception(message);
