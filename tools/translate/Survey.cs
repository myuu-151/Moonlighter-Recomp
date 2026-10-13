using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace MoonlighterTranslate;

// `survey`: every class in progress/classes.tsv, classified by what it depends on, with the
// Unity and third-party APIs it uses and the C# semantics a translator has to reproduce in it.
// Writes progress/class_deps.tsv; prints (or writes with --summary) the summary tables.
//
// Kinds (the first that applies):
//   unity   a MonoBehaviour / StateMachineBehaviour / other Component, or code that drives scene
//           objects (GameObject, Transform, Component, Object.Instantiate/Destroy, coroutines...).
//   data    a ScriptableObject, or a [Serializable] class/struct whose only engine dependencies
//           are asset references (Sprite, GameObject prefab fields...) and value types.
//   values  plain C# that also uses Unity value types and statics only (Vector2/3, Mathf,
//           Random, Color, Rect...): what tools/translate maps onto recomp/src/unity today.
//   plain   no UnityEngine, DOTween or third-party library at all.
public static class Survey
{
    // UnityEngine types that are plain values or static math: no engine object behind them.
    static readonly HashSet<string> ValueTypes = new(StringComparer.Ordinal)
    {
        "Vector2", "Vector3", "Vector4", "Vector2Int", "Vector3Int", "Mathf", "Random", "Color", "Color32",
        "Rect", "RectInt", "Bounds", "BoundsInt", "Quaternion", "Matrix4x4", "LayerMask", "Keyframe",
        "AnimationCurve", "Gradient", "GradientColorKey", "GradientAlphaKey", "Debug", "Time", "Ray2D",
        "RangeInt", "WrapMode",
    };

    // Unity object types that a data class may hold as references to assets.
    static readonly HashSet<string> AssetTypes = new(StringComparer.Ordinal)
    {
        "Object", "Sprite", "Texture", "Texture2D", "GameObject", "AudioClip", "Material", "RuntimeAnimatorController",
        "AnimationClip", "TextAsset", "Font", "Mesh", "Shader", "ScriptableObject", "Transform", "Component",
    };

    // Unity types whose use means scene/engine-object code.
    static readonly HashSet<string> SceneTypes = new(StringComparer.Ordinal)
    {
        "MonoBehaviour", "Behaviour", "Component", "GameObject", "Transform", "RectTransform", "Coroutine",
        "WaitForSeconds", "WaitForEndOfFrame", "WaitForFixedUpdate", "WaitForSecondsRealtime", "WaitUntil", "WaitWhile",
        "Rigidbody2D", "Collider2D", "BoxCollider2D", "CircleCollider2D", "PolygonCollider2D", "Physics2D",
        "SpriteRenderer", "Renderer", "Animator", "Camera", "AudioSource", "ParticleSystem", "Canvas", "Input",
    };

    // Folders of Assembly-CSharp that are third-party libraries compiled from source.
    static readonly string[] SourceLibraries =
    {
        "InControl", "I2", "FullSerializer", "TMPro", "DG", "DigitalRuby", "XInputDotNetPure", "PixelArtRotation",
        "ChromaSDK", "UnityStandardAssets",
    };

    sealed class FileInfo
    {
        public string Path;
        public int Lines;
        public string Category;
        public double Ported;
        public string Kind;
        public string Base = "-";
        public readonly SortedSet<string> Features = new(StringComparer.Ordinal);
        // lib -> type -> members
        public readonly SortedDictionary<string, SortedDictionary<string, SortedSet<string>>> Apis = new(StringComparer.Ordinal);
        public readonly HashSet<INamedTypeSymbol> GameTypesUsed = new(SymbolEqualityComparer.Default);
        public readonly HashSet<ISymbol> External = new(SymbolEqualityComparer.Default); // every library symbol used, BCL included
        public readonly List<INamedTypeSymbol> Declared = new();
        public bool UsesScene, UsesNonValueUnity, UsesThirdParty, UsesUnity, UsesAssetsOnly = true;
        public bool IsBehaviour, IsScriptable, IsSerializable;
        public bool Closed; // translatable with only plain/values/data game classes around it
        public TranslationSurvey.FileStats T = new();
    }

    public static int Run(Options o)
    {
        var gc = GameCompilation.Load(o);
        var ported = ReadPorted(o.Ported);
        var files = new List<FileInfo>();
        foreach (var line in File.ReadLines(o.Classes))
        {
            if (line.StartsWith('#') || line.Trim().Length == 0) continue;
            var f = line.Split('\t');
            var fi = new FileInfo { Path = f[0], Lines = int.Parse(f[1]), Category = f[2] };
            fi.Ported = ported.TryGetValue(fi.Path, out var p) ? p : 0.0;
            files.Add(fi);
        }

        var bindings = File.Exists(o.Bindings) ? Bindings.Load(o.Bindings) : null;
        Parallel.ForEach(files, fi => Scan(gc, fi));

        // Which game types each file declares, to close "plain" over the game's own classes.
        var declaredIn = new Dictionary<INamedTypeSymbol, FileInfo>(SymbolEqualityComparer.Default);
        foreach (var fi in files) foreach (var t in fi.Declared) declaredIn[t] = fi;
        foreach (var fi in files) fi.Closed = fi.Kind is "plain" or "values" or "data";
        for (bool changed = true; changed;)
        {
            changed = false;
            foreach (var fi in files)
            {
                if (!fi.Closed) continue;
                foreach (var t in fi.GameTypesUsed)
                {
                    var def = t.OriginalDefinition;
                    while (def.ContainingType != null && !declaredIn.ContainsKey(def)) def = def.ContainingType;
                    if (declaredIn.TryGetValue(def, out var other) && other != fi && !other.Closed)
                    {
                        fi.Closed = false;
                        changed = true;
                        break;
                    }
                }
            }
        }

        // What translates today: every method of the game through the translator.
        var ts = TranslationSurvey.Run(gc, bindings ?? throw new UsageException($"no {o.Bindings}"));
        foreach (var fi in files)
            if (ts.Files.TryGetValue(fi.Path, out var st)) fi.T = st;
        var counted = new HashSet<string>(files.Select(f => f.Path), StringComparer.Ordinal);
        string gapsPath = Path.Combine(Path.GetDirectoryName(o.Out ?? Path.Combine(o.Repo, "progress", "x")), "unity_api_gaps.tsv");
        ts.WriteGaps(gapsPath, counted.Contains);
        Console.Error.WriteLine($"wrote {gapsPath}");

        string outPath = o.Out ?? Path.Combine(o.Repo, "progress", "class_deps.tsv");
        WriteTsv(outPath, files);
        string summary = TranslationSummary(files, ts) + Summarise(files, bindings);
        if (o.Summary != null) File.WriteAllText(o.Summary, summary, new UTF8Encoding(false));
        else Console.Write(summary);
        Console.Error.WriteLine($"wrote {outPath} ({files.Count} classes)");
        return 0;
    }

    static Dictionary<string, double> ReadPorted(string path)
    {
        var d = new Dictionary<string, double>(StringComparer.Ordinal);
        if (!File.Exists(path)) return d;
        foreach (var line in File.ReadLines(path))
        {
            if (line.StartsWith('#') || line.Trim().Length == 0) continue;
            var f = line.Split('\t');
            if (f.Length > 1 && double.TryParse(f[1], System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var v))
                d[f[0]] = v;
        }
        return d;
    }

    // ---- scanning one file -------------------------------------------------------------------

    static void Scan(GameCompilation gc, FileInfo fi)
    {
        var tree = gc.Tree(fi.Path);
        var model = gc.Model(tree);
        var root = tree.GetRoot();
        var own = gc.Compilation.Assembly;

        foreach (var decl in root.DescendantNodes().OfType<BaseTypeDeclarationSyntax>())
        {
            if (model.GetDeclaredSymbol(decl) is not INamedTypeSymbol t) continue;
            fi.Declared.Add(t);
            if (t.GetAttributes().Any(a => a.AttributeClass?.Name == "SerializableAttribute")) fi.IsSerializable = true;
            if (t.TypeKind == TypeKind.Struct) fi.Features.Add("struct");
            if (t.IsGenericType) fi.Features.Add("generic");
            // The first engine (non-game) base class names the class's kind.
            for (var b = t.BaseType; b != null; b = b.BaseType)
            {
                if (SymbolEqualityComparer.Default.Equals(b.ContainingAssembly, own) && !IsSourceLibrary(b)) continue;
                string name = b.ToDisplayString(SymbolDisplayFormat.MinimallyQualifiedFormat);
                if (b.SpecialType == SpecialType.System_Object || b.SpecialType == SpecialType.System_ValueType || b.SpecialType == SpecialType.System_Enum) break;
                if (fi.Base == "-" || decl.Parent is not BaseTypeDeclarationSyntax) fi.Base = DirectGameBase(t, own) ?? name;
                if (name is "MonoBehaviour" or "Behaviour" or "Component" or "StateMachineBehaviour" || name.StartsWith("UIBehaviour")) fi.IsBehaviour = true;
                else if (name == "ScriptableObject") fi.IsScriptable = true;
                break;
            }
            if (fi.Base == "-" && t.TypeKind is TypeKind.Enum or TypeKind.Interface or TypeKind.Delegate or TypeKind.Struct)
                fi.Base = t.TypeKind.ToString().ToLowerInvariant();
        }

        // Attributes ([SerializeField], [Header]...) say nothing about runtime behaviour.
        foreach (var node in root.DescendantNodes(n => n is not AttributeListSyntax))
        {
            switch (node)
            {
                case TryStatementSyntax or ThrowStatementSyntax or ThrowExpressionSyntax:
                    fi.Features.Add("exceptions");
                    break;
                case YieldStatementSyntax:
                    fi.Features.Add("iterator");
                    break;
                case GotoStatementSyntax:
                    fi.Features.Add("goto");
                    break;
                case UnsafeStatementSyntax or FixedStatementSyntax or PointerTypeSyntax:
                    fi.Features.Add("unsafe");
                    break;
                case LockStatementSyntax:
                    fi.Features.Add("threads");
                    break;
                case InterpolatedStringExpressionSyntax:
                    fi.Features.Add("format");
                    break;
                case AnonymousFunctionExpressionSyntax lambda:
                    fi.Features.Add("lambda");
                    if (CapturesLocals(model, lambda)) fi.Features.Add("closure");
                    break;
                case EventFieldDeclarationSyntax or EventDeclarationSyntax:
                    fi.Features.Add("events");
                    break;
                case FieldDeclarationSyntax fd when fd.Modifiers.Any(SyntaxKind.StaticKeyword):
                    foreach (var v in fd.Declaration.Variables)
                        if (v.Initializer != null && model.GetConstantValue(v.Initializer.Value) is { HasValue: false })
                            fi.Features.Add("static-init");
                    break;
                case ConstructorDeclarationSyntax cd when cd.Modifiers.Any(SyntaxKind.StaticKeyword):
                    fi.Features.Add("static-init");
                    break;
                case ForEachStatementSyntax fe:
                {
                    var info = model.GetForEachStatementInfo(fe);
                    var et = info.GetEnumeratorMethod?.ContainingType?.OriginalDefinition;
                    string en = et?.ToDisplayString() ?? "";
                    if (en.Contains("Dictionary") || en.Contains("HashSet") || en.Contains("Hashtable")) fi.Features.Add("hash-order");
                    break;
                }
                case BinaryExpressionSyntax be when be.IsKind(SyntaxKind.AddExpression):
                {
                    var t = model.GetTypeInfo(be).Type;
                    if (t?.SpecialType == SpecialType.System_String)
                    {
                        var l = model.GetTypeInfo(be.Left).Type; var r = model.GetTypeInfo(be.Right).Type;
                        if (l?.SpecialType != SpecialType.System_String || r?.SpecialType != SpecialType.System_String) fi.Features.Add("format");
                    }
                    break;
                }
            }

            if (node is ExpressionSyntax expr)
            {
                var conv = model.GetConversion(expr);
                if (conv.IsBoxing) fi.Features.Add("boxing");
                if (conv.IsUserDefined && conv.MethodSymbol != null) Record(gc, fi, conv.MethodSymbol);
                if (conv.IsNullable) fi.Features.Add("nullable");
            }

            if (node is TypeSyntax or ExpressionSyntax or ConstructorInitializerSyntax)
            {
                var si = model.GetSymbolInfo(node);
                var sym = si.Symbol ?? si.CandidateSymbols.FirstOrDefault();
                if (sym != null) Record(gc, fi, sym);
            }
        }

        if (fi.IsBehaviour || fi.UsesScene) fi.Kind = "unity";
        else if (fi.IsScriptable || (fi.IsSerializable && !fi.UsesThirdParty && (!fi.UsesNonValueUnity || fi.UsesAssetsOnly)))
            fi.Kind = fi.UsesThirdParty ? "unity" : "data";
        else if (fi.UsesThirdParty || fi.UsesNonValueUnity) fi.Kind = fi.UsesNonValueUnity && fi.UsesAssetsOnly && !fi.UsesThirdParty ? "data" : "unity";
        else if (fi.UsesUnity) fi.Kind = "values";
        else fi.Kind = "plain";
    }

    static string DirectGameBase(INamedTypeSymbol t, IAssemblySymbol own)
    {
        var b = t.BaseType;
        if (b == null || b.SpecialType == SpecialType.System_Object) return null;
        return b.ToDisplayString(SymbolDisplayFormat.MinimallyQualifiedFormat);
    }

    static bool CapturesLocals(SemanticModel model, AnonymousFunctionExpressionSyntax lambda)
    {
        var flow = model.AnalyzeDataFlow(lambda);
        return flow.Succeeded && flow.CapturedInside.Any(s => s is not IParameterSymbol { IsThis: true });
    }

    static bool IsSourceLibrary(ISymbol s)
    {
        var ns = s.ContainingNamespace;
        while (ns != null && ns.ContainingNamespace is { IsGlobalNamespace: false }) ns = ns.ContainingNamespace;
        return ns != null && !ns.IsGlobalNamespace && SourceLibraries.Contains(ns.Name);
    }

    static string TopNamespace(ISymbol s)
    {
        var ns = s.ContainingNamespace;
        while (ns != null && ns.ContainingNamespace is { IsGlobalNamespace: false }) ns = ns.ContainingNamespace;
        return ns == null || ns.IsGlobalNamespace ? "" : ns.Name;
    }

    // Which library a symbol comes from, or null for the game's own code.
    static string Library(GameCompilation gc, ISymbol s)
    {
        var asm = s.ContainingAssembly;
        if (asm == null) return null;
        if (SymbolEqualityComparer.Default.Equals(asm, gc.Compilation.Assembly))
            return IsSourceLibrary(s) ? TopNamespace(s) switch { "DG" => "DOTween", "I2" => "I2Loc", var n => n } : null;
        string a = asm.Name;
        if (a.StartsWith("UnityEngine")) return "Unity";
        if (a is "DOTween" or "DOTweenPro" or "DemiLib") return "DOTween";
        if (a.StartsWith("TextMeshPro")) return "TMPro";
        if (a == "Assembly-CSharp-firstpass")
            return TopNamespace(s) switch
            {
                "DarkTonic" => "MasterAudio",
                "CodeStage" => "AntiCheat",
                "" => "firstpass",
                var n => n,
            };
        if (a is "mscorlib" or "netstandard" or "System" or "System.Core" || a.StartsWith("System.")) return "BCL";
        return a;
    }

    static readonly SymbolDisplayFormat TypeFormat = new(
        typeQualificationStyle: SymbolDisplayTypeQualificationStyle.NameAndContainingTypes,
        genericsOptions: SymbolDisplayGenericsOptions.None);

    static void Record(GameCompilation gc, FileInfo fi, ISymbol sym)
    {
        if (sym is IAliasSymbol alias) sym = alias.Target;
        if (sym is ILocalSymbol or IParameterSymbol or IRangeVariableSymbol or ILabelSymbol or INamespaceSymbol or IDiscardSymbol)
            return;
        if (sym is ITypeParameterSymbol) return;
        if (sym is IArrayTypeSymbol arr) { Record(gc, fi, arr.ElementType); return; }
        if (sym is IPointerTypeSymbol) return;
        if (sym is IMethodSymbol { ReducedFrom: not null } ext) sym = ext.ReducedFrom;
        if (sym is IMethodSymbol { MethodKind: MethodKind.LambdaMethod or MethodKind.LocalFunction }) return;

        INamedTypeSymbol type = sym as INamedTypeSymbol ?? sym.ContainingType;
        if (type == null) return;
        if (type.IsTupleType) return;
        string lib = Library(gc, type);
        if (lib == null)
        {
            if (sym is INamedTypeSymbol nt) fi.GameTypesUsed.Add(nt.OriginalDefinition);
            else fi.GameTypesUsed.Add(type.OriginalDefinition);
            // Inherited engine members reached through a game class (this.transform...): the
            // symbol's own containing type is the engine's, handled below when it is.
            return;
        }

        string typeName = type.OriginalDefinition.ToDisplayString(TypeFormat);
        string member = sym is INamedTypeSymbol ? null : MemberName(sym);
        lock (fi.External) fi.External.Add(sym.OriginalDefinition);

        if (lib == "BCL")
        {
            Bcl(fi, type, sym, typeName, member);
            return;
        }
        if (lib == "Unity")
        {
            fi.UsesUnity = true;
            string top = typeName.Split('.')[0];
            if (!ValueTypes.Contains(top) && !(top == "Object" && member is null or "==" or "!=" or "implicit" or "name"))
            {
                fi.UsesNonValueUnity = true;
                if (SceneTypes.Contains(top) || (member != null && IsSceneMember(top, member))) fi.UsesScene = true;
                // A field or parameter typed Sprite/GameObject is an asset reference; calling
                // members on them (other than reading) is engine work.
                if (!AssetTypes.Contains(top) || member != null) fi.UsesAssetsOnly = false;
            }
            if (member is "StartCoroutine" or "StopCoroutine" or "StopAllCoroutines") fi.Features.Add("coroutine");
            if (member is "Invoke" or "InvokeRepeating" or "CancelInvoke" or "IsInvoking" or "SendMessage" or "BroadcastMessage") fi.Features.Add("invoke");
            if (top == "Random") fi.Features.Add("rng");
        }
        else if (lib == "AntiCheat")
        {
            fi.UsesUnity = true; // ObscuredFloat/Int/Bool...: encrypted plain values, a value type to the port
        }
        else
        {
            fi.UsesThirdParty = true;
            fi.UsesAssetsOnly = false;
        }

        if (!fi.Apis.TryGetValue(lib, out var types)) fi.Apis[lib] = types = new(StringComparer.Ordinal);
        if (!types.TryGetValue(typeName, out var members)) types[typeName] = members = new(StringComparer.Ordinal);
        if (member != null) members.Add(member);
    }

    static bool IsSceneMember(string type, string member) =>
        type == "Object" && member is "Instantiate" or "Destroy" or "DestroyImmediate" or "FindObjectOfType" or "FindObjectsOfType" or "DontDestroyOnLoad";

    static void Bcl(FileInfo fi, INamedTypeSymbol type, ISymbol sym, string typeName, string member)
    {
        string ns = type.ContainingNamespace?.ToDisplayString() ?? "";
        if (ns == "System.Linq") fi.Features.Add("linq");
        if (ns.StartsWith("System.Reflection") || (member is "GetType" or "GetValues" or "GetNames" or "Parse" && typeName == "Enum") || typeName == "Activator")
            fi.Features.Add("reflection");
        if (ns.StartsWith("System.Threading") || ns.StartsWith("System.IO")) fi.Features.Add(ns.StartsWith("System.IO") ? "io" : "threads");
        if (typeName == "Nullable") fi.Features.Add("nullable");
        if (typeName == "Random" && ns == "System") fi.Features.Add("rng");
        if (member is "Parse" or "TryParse" && typeName is "Int32" or "Single" or "Double" or "Int64" or "Boolean" or "Enum" ||
            typeName == "Convert" && member != null && member.StartsWith("To"))
            fi.Features.Add("parse");
        if (member == "ToString" && type.SpecialType is SpecialType.System_Single or SpecialType.System_Double or SpecialType.System_Int32 or SpecialType.System_Int64 ||
            typeName is "StringBuilder" || (typeName == "String" && member is "Format" or "Concat"))
            fi.Features.Add("format");
        if (typeName == "String" && member is "Compare" or "CompareTo" or "StartsWith" or "EndsWith" or "IndexOf" or "LastIndexOf" or "ToLower" or "ToUpper")
            fi.Features.Add("culture");
        if (typeName is "Dictionary" or "HashSet" or "Hashtable" or "Dictionary.KeyCollection" or "Dictionary.ValueCollection" && member == "GetEnumerator")
            fi.Features.Add("hash-order");
        if (sym is IEventSymbol || type.TypeKind == TypeKind.Delegate) fi.Features.Add("delegates");
        if (typeName is "IEnumerator" or "IEnumerable") { /* iterators are flagged by yield */ }
    }

    static string MemberName(ISymbol sym)
    {
        string n = sym.Name;
        if (sym is IMethodSymbol m)
        {
            if (m.MethodKind == MethodKind.Constructor) return "new";
            if (m.MethodKind is MethodKind.UserDefinedOperator or MethodKind.Conversion) return OperatorToken(n);
            if (m.MethodKind is MethodKind.PropertyGet or MethodKind.PropertySet) return m.AssociatedSymbol?.Name ?? n;
        }
        if (sym is IPropertySymbol { IsIndexer: true }) return "this[]";
        return n;
    }

    static string OperatorToken(string n) => n switch
    {
        "op_Addition" => "+", "op_Subtraction" => "-", "op_Multiply" => "*", "op_Division" => "/",
        "op_UnaryNegation" => "neg", "op_Equality" => "==", "op_Inequality" => "!=", "op_Implicit" => "implicit",
        "op_Explicit" => "explicit", "op_LessThan" => "<", "op_GreaterThan" => ">", _ => n,
    };

    // ---- output ------------------------------------------------------------------------------

    static string ApiText(FileInfo fi)
    {
        var sb = new StringBuilder();
        foreach (var (lib, types) in fi.Apis)
        {
            if (lib == "BCL") continue; // the cs runtime's business; the features column covers it
            foreach (var (type, members) in types)
            {
                if (sb.Length > 0) sb.Append(' ');
                sb.Append(lib).Append(':').Append(type);
                if (members.Count > 0) sb.Append('(').Append(string.Join(',', members)).Append(')');
            }
        }
        return sb.Length == 0 ? "-" : sb.ToString();
    }

    static void WriteTsv(string path, List<FileInfo> files)
    {
        var sb = new StringBuilder();
        sb.Append("# What each of the game's classes (progress/classes.tsv) depends on: tools/translate survey.\n");
        sb.Append("# kind: plain (no engine or library API), values (plus Unity value types/math/Random only),\n");
        sb.Append("#   data (ScriptableObject / [Serializable] data, asset references), unity (MonoBehaviour or scene code).\n");
        sb.Append("# closed: y if it and every game class it reaches are plain/values/data (translatable without new Unity layer).\n");
        sb.Append("# translates: y when every method of the file translates today (tools/translate: no unsupported construct,\n");
        sb.Append("#   every API bound), part when some do, n when none; translated: the share of its methods' lines that do;\n");
        sb.Append("#   methods: translated/all; blockers: what stops the rest (most lines first; progress/unity_api_gaps.tsv).\n");
        sb.Append("# features: C# semantics the translator must reproduce in it. apis: Library:Type(members) used, BCL left out.\n");
        sb.Append("# path\tlines\tkind\tbase\tclosed\ttranslates\ttranslated\tmethods\tblockers\tfeatures\tapis\n");
        foreach (var fi in files)
        {
            var t = fi.T;
            string translates = t.Methods == 0 ? "y" : t.MethodsOk == t.Methods ? "y" : t.MethodsOk == 0 ? "n" : "part";
            string pct = (t.Lines == 0 ? 100.0 : 100.0 * t.LinesOk / t.Lines).ToString("F1", System.Globalization.CultureInfo.InvariantCulture) + "%"; // (one decimal: tools/progress.py reads it)
            string blockers = t.Blockers.Count == 0 ? "-" : string.Join("; ", t.Blockers.OrderByDescending(kv => kv.Value).Take(3).Select(kv => kv.Key));
            sb.Append(fi.Path).Append('\t').Append(fi.Lines).Append('\t').Append(fi.Kind).Append('\t').Append(fi.Base)
              .Append('\t').Append(fi.Closed ? 'y' : 'n')
              .Append('\t').Append(translates).Append('\t').Append(pct).Append('\t').Append(t.MethodsOk).Append('/').Append(t.Methods)
              .Append('\t').Append(blockers.Replace('\t', ' '))
              .Append('\t').Append(fi.Features.Count == 0 ? "-" : string.Join(',', fi.Features))
              .Append('\t').Append(ApiText(fi)).Append('\n');
        }
        File.WriteAllText(path, sb.ToString(), new UTF8Encoding(false));
    }

    static string TranslationSummary(List<FileInfo> files, TranslationSurvey ts)
    {
        var sb = new StringBuilder();
        int total = files.Sum(f => f.Lines);
        double weighted = files.Sum(f => f.T.Lines == 0 ? f.Lines : f.Lines * (double)f.T.LinesOk / f.T.Lines);
        var whole = files.Where(f => f.T.MethodsOk == f.T.Methods).ToList();
        int methods = files.Sum(f => f.T.Methods), methodsOk = files.Sum(f => f.T.MethodsOk);
        sb.Append($"# Translation today (tools/translate, the whole game)\n\n");
        sb.Append($"- counted classes (progress/classes.tsv): {files.Count:N0} files, {total:N0} lines\n");
        sb.Append($"- methods that translate: {methodsOk:N0} of {methods:N0} ({100.0 * methodsOk / Math.Max(1, methods):F1}%)\n");
        sb.Append($"- lines that translate (each file's lines times its translated share of method lines): {weighted:N0} of {total:N0} ({100.0 * weighted / total:F1}%)\n");
        sb.Append($"- classes that translate whole: {whole.Count:N0} files, {whole.Sum(f => f.Lines):N0} lines\n");
        sb.Append($"- the whole assembly (with third-party code compiled into it): {ts.MethodsOk:N0} of {ts.MethodsTotal:N0} methods\n\n");
        sb.Append("Unsupported constructs (methods):\n\n");
        foreach (var (k, v) in ts.Errors.OrderByDescending(kv => kv.Value)) sb.Append($"- {k}: {v}\n");
        sb.Append("\nTop missing APIs (progress/unity_api_gaps.tsv), by lines of counted classes blocked:\n\n| library | api | methods | lines |\n|---|---|---:|---:|\n");
        var counted = new HashSet<string>(files.Select(f => f.Path), StringComparer.Ordinal);
        foreach (var g in ts.Gaps.Values.Where(g => g.Files.Any(counted.Contains)).OrderByDescending(g => g.Lines).Take(40))
            sb.Append($"| {g.Library} | {g.Kind} {g.Api} | {g.Methods} | {g.Lines} |\n");
        var libs = ts.Gaps.Values.Where(g => g.Files.Any(counted.Contains)).GroupBy(g => g.Library).OrderByDescending(x => x.Sum(g => g.Lines));
        sb.Append("\nMissing APIs by library (distinct APIs, lines blocked):\n\n");
        foreach (var l in libs) sb.Append($"- {l.Key}: {l.Count()} APIs, {l.Sum(g => g.Lines):N0} lines\n");
        sb.Append('\n');
        return sb.ToString();
    }

    static string Summarise(List<FileInfo> files, Bindings bindings)
    {
        var sb = new StringBuilder();
        double Unported(FileInfo f) => f.Lines * (1.0 - Math.Min(1.0, f.Ported));
        int total = files.Sum(f => f.Lines);
        double totalUnported = files.Sum(Unported);
        sb.Append($"# Survey: {files.Count} classes, {total:N0} lines, {totalUnported:N0} not yet ported\n\n");

        sb.Append("## By kind\n\n| kind | classes | lines | not ported | closed (lines) |\n|---|---:|---:|---:|---:|\n");
        foreach (var k in new[] { "plain", "values", "data", "unity" })
        {
            var g = files.Where(f => f.Kind == k).ToList();
            sb.Append($"| {k} | {g.Count} | {g.Sum(f => f.Lines):N0} | {g.Sum(Unported):N0} | {g.Where(f => f.Closed).Sum(f => f.Lines):N0} |\n");
        }

        sb.Append("\n## By category (lines not ported, by kind)\n\n| category | plain | values | data | unity | total |\n|---|---:|---:|---:|---:|---:|\n");
        foreach (var cat in files.Select(f => f.Category).Distinct().OrderBy(c => c))
        {
            var g = files.Where(f => f.Category == cat).ToList();
            sb.Append($"| {cat} ");
            foreach (var k in new[] { "plain", "values", "data", "unity" }) sb.Append($"| {g.Where(f => f.Kind == k).Sum(Unported):N0} ");
            sb.Append($"| {g.Sum(Unported):N0} |\n");
        }

        sb.Append("\n## Semantics the translator must reproduce (classes, lines not ported)\n\n| feature | classes | lines |\n|---|---:|---:|\n");
        var feats = files.SelectMany(f => f.Features).Distinct().OrderByDescending(x => files.Where(f => f.Features.Contains(x)).Sum(Unported));
        foreach (var x in feats)
        {
            var g = files.Where(f => f.Features.Contains(x)).ToList();
            sb.Append($"| {x} | {g.Count} | {g.Sum(Unported):N0} |\n");
        }

        // Engine and library APIs, weighted by the unported lines of the classes that use them.
        var typeUse = new Dictionary<string, (int classes, double lines, HashSet<string> members)>();
        var memberUse = new Dictionary<string, (int classes, double lines)>();
        foreach (var f in files)
        {
            double w = Unported(f);
            if (w <= 0) continue;
            foreach (var (lib, types) in f.Apis)
            {
                if (lib == "BCL") continue;
                foreach (var (type, members) in types)
                {
                    string key = $"{lib}:{type}";
                    var cur = typeUse.TryGetValue(key, out var c) ? c : (classes: 0, lines: 0.0, members: new HashSet<string>());
                    cur.members.UnionWith(members);
                    typeUse[key] = (cur.classes + 1, cur.lines + w, cur.members);
                    foreach (var m in members)
                    {
                        string mk = $"{key}.{m}";
                        var mc = memberUse.TryGetValue(mk, out var v) ? v : (0, 0.0);
                        memberUse[mk] = (mc.Item1 + 1, mc.Item2 + w);
                    }
                }
            }
        }
        sb.Append("\n## Engine and library types the unported classes need (by their unported lines)\n\n| type | classes | lines | members used |\n|---|---:|---:|---:|\n");
        foreach (var (k, v) in typeUse.OrderByDescending(kv => kv.Value.lines).Take(60))
            sb.Append($"| {k} | {v.classes} | {v.lines:N0} | {v.members.Count} |\n");
        sb.Append("\n## Most-needed members\n\n| member | classes | lines |\n|---|---:|---:|\n");
        foreach (var (k, v) in memberUse.OrderByDescending(kv => kv.Value.lines).Take(80))
            sb.Append($"| {k} | {v.classes} | {v.lines:N0} |\n");

        var libs = files.SelectMany(f => f.Apis.Keys).Distinct().Where(l => l != "BCL");
        sb.Append("\n## Libraries (classes using them, lines not ported)\n\n| library | classes | lines |\n|---|---:|---:|\n");
        foreach (var lib in libs.OrderByDescending(l => files.Where(f => f.Apis.ContainsKey(l)).Sum(Unported)))
        {
            var g = files.Where(f => f.Apis.ContainsKey(lib)).ToList();
            sb.Append($"| {lib} | {g.Count} | {g.Sum(Unported):N0} |\n");
        }

        // Bases of the unity kind (what the Unity layer must host).
        sb.Append("\n## Engine bases of the unity kind\n\n| base | classes | lines not ported |\n|---|---:|---:|\n");
        foreach (var g in files.Where(f => f.Kind == "unity").GroupBy(f => f.Base).OrderByDescending(g => g.Sum(Unported)).Take(25))
            sb.Append($"| {g.Key} | {g.Count()} | {g.Sum(Unported):N0} |\n");

        if (bindings != null)
        {
            var bound = files.Where(f => f.Apis.Where(a => a.Key != "BCL").SelectMany(a => a.Value.Select(t => $"{a.Key}:{t.Key}")).All(bindings.HasType)).ToList();
            sb.Append($"\nClasses whose every engine/library type has a binding to recomp/src/unity today: {bound.Count}, {bound.Sum(Unported):N0} unported lines.\n");
        }
        return sb.ToString();
    }
}
