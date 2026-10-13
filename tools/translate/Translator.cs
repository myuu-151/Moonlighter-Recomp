using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Operations;

namespace MoonlighterTranslate;

// The translation of a set of the game's classes and methods (tools/translate/units.txt) into
// C++ (docs/translator_plan.md):
//   1. seeds: the units' types (every member) and methods;
//   2. closure: every game method a translated body calls, every override of a virtual method
//      that is translated in a type that is used, every interface implementation;
//   3. output: game.h (every type, in dependency order), one .cpp per C# file, sources.cmake,
//      and report.tsv (what didn't translate, and why).
public sealed class Translator
{
    public readonly GameCompilation GC;
    public readonly CSharpCompilation Comp;
    public readonly IAssemblySymbol Game;
    public readonly Bindings Bind;
    public readonly Names N;
    readonly HashSet<string> beforeFieldInit;

    public sealed class MethodResult
    {
        public IMethodSymbol M;
        public string Body;           // the C++ statements (null: no body)
        public string Error;          // an unsupported construct
        public List<string> Gaps = new();
        public bool Stub;             // declared only: units.txt stubs its type (reaching it throws)
        public bool Hand;             // declared only: units.txt's `hand`, defined in hand-written C++
        public bool Translated => Error == null && Gaps.Count == 0 && !Stub && !Hand;
        public int Lines;
    }

    // units.txt's `stub <type>` and `stub-others`: types whose methods are declared but not
    // translated (a call compiles; reaching it at run time throws), so the closure stops there.
    readonly HashSet<INamedTypeSymbol> stubTypes = new(SymbolEqualityComparer.Default);
    readonly HashSet<INamedTypeSymbol> listedTypes = new(SymbolEqualityComparer.Default);
    readonly HashSet<IMethodSymbol> listedMethods = new(SymbolEqualityComparer.Default);
    // units.txt's `hand <type> <member...>`: game methods whose bodies are written by hand (a
    // driver's stand-in for a service the translated code calls: the hero, the camera, the
    // room's bookkeeping); declared like the translated ones, defined elsewhere.
    readonly HashSet<IMethodSymbol> handMethods = new(SymbolEqualityComparer.Default);
    // Types named by `type <name> <members...>`: what calls reach of them translates, but the
    // engine's seeds (their messages, overrides, interface implementations) don't: those are
    // declared as stubs unless listed (a scene service the enemies reach for one property).
    readonly HashSet<INamedTypeSymbol> partialTypes = new(SymbolEqualityComparer.Default);
    bool stubOthers;
    public bool IsHand(IMethodSymbol m) => handMethods.Contains(m.OriginalDefinition);

    // Stubbing is by type: a type the units name (whole or some of its members) translates what
    // the closure reaches of it; a compiler-generated class (a closure, an iterator) follows the
    // methods that use it, so it is never stubbed itself.
    bool IsStubbed(IMethodSymbol m)
    {
        var t = m.ContainingType.OriginalDefinition;
        if (listedMethods.Contains(m)) return false;
        for (var o = t; o != null; o = o.ContainingType)
        {
            if (stubTypes.Contains(o)) return true;
            if (listedTypes.Contains(o)) return false;
            if (o.Name.StartsWith("_003C", StringComparison.Ordinal) || o.IsImplicitlyDeclared) return false;
        }
        return stubOthers;
    }

    readonly HashSet<INamedTypeSymbol> types = new(SymbolEqualityComparer.Default);
    readonly HashSet<INamedTypeSymbol> fullTypes = new(SymbolEqualityComparer.Default);
    readonly HashSet<INamedTypeSymbol> constructed = new(SymbolEqualityComparer.Default);
    readonly Dictionary<IMethodSymbol, MethodResult> methods = new(SymbolEqualityComparer.Default);
    readonly Queue<IMethodSymbol> queue = new();
    readonly HashSet<IFieldSymbol> usedFields = new(SymbolEqualityComparer.Default);
    readonly HashSet<IEventSymbol> usedEvents = new(SymbolEqualityComparer.Default);
    readonly HashSet<string> includes = new(StringComparer.Ordinal);
    MethodResult current;

    // gap key -> (kind, symbol, the methods that need it)
    public readonly Dictionary<string, (string kind, ISymbol symbol, HashSet<IMethodSymbol> users)> Gaps = new(StringComparer.Ordinal);

    public Translator(GameCompilation gc, Bindings bind, HashSet<string> beforeFieldInit)
    {
        GC = gc;
        Comp = gc.Compilation;
        Game = Comp.Assembly;
        Bind = bind;
        N = new Names(this);
        this.beforeFieldInit = beforeFieldInit;
    }

    // ---- recording what translated code needs -----------------------------------------------------

    public string Fail(string what) => throw new TranslateException(what);

    public string Gap(ISymbol s, string kind)
    {
        string key = kind + " " + Bindings.KeyOf(s);
        if (!Gaps.TryGetValue(key, out var g)) Gaps[key] = g = (kind, s, new HashSet<IMethodSymbol>(SymbolEqualityComparer.Default));
        if (current != null)
        {
            g.users.Add(current.M);
            if (!current.Gaps.Contains(key)) current.Gaps.Add(key);
        }
        return $"CS_GAP(\"{key.Replace("\"", "'")}\")";
    }

    public void UseBinding(Bindings.Entry e)
    {
        if (e.Include != null) includes.Add(e.Include);
    }

    public void NeedType(INamedTypeSymbol t)
    {
        t = t.OriginalDefinition;
        if (!N.IsGame(t)) return;
        if (!types.Add(t)) return;
        // its containing types are flattened, but their type parameters live on: nothing to do.
        if (t.BaseType != null && N.IsGame(t.BaseType)) NeedType(t.BaseType);
        foreach (var i in t.Interfaces) if (N.IsGame(i)) NeedType(i);
    }

    // A seed of the engine's (a message, an override, an interface implementation).
    void NeedSeed(IMethodSymbol m)
    {
        var o = m.OriginalDefinition;
        var t = o.ContainingType.OriginalDefinition;
        bool partial = partialTypes.Contains(t) && !fullTypes.Contains(t) && !listedMethods.Contains(o) && !handMethods.Contains(o);
        NeedMethod(m, partial);
    }

    public void NeedMethod(IMethodSymbol m) => NeedMethod(m, false);

    void NeedMethod(IMethodSymbol m, bool seedStub)
    {
        m = m.OriginalDefinition;
        if (m.ReducedFrom != null) m = m.ReducedFrom.OriginalDefinition;
        if (!N.IsGame(m)) return;
        if (m.MethodKind == MethodKind.DelegateInvoke) return;
        NeedType(m.ContainingType);
        if (methods.ContainsKey(m)) return;
        bool hand = handMethods.Contains(m);
        methods[m] = new MethodResult { M = m, Hand = hand, Stub = !hand && (seedStub || IsStubbed(m)) };
        queue.Enqueue(m);
        // overridden declarations are part of the class hierarchy's C++ (override needs them)
        if (m.OverriddenMethod != null) NeedMethod(m.OverriddenMethod);
        // property accessors bring their property's backing field if auto
    }

    public void UseField(IFieldSymbol f)
    {
        f = f.OriginalDefinition;
        if (usedFields.Add(f)) NeedType(f.ContainingType);
    }

    public void UseEvent(IEventSymbol e)
    {
        e = e.OriginalDefinition;
        if (usedEvents.Add(e)) NeedType(e.ContainingType);
    }

    public void Constructed(INamedTypeSymbol t)
    {
        t = t.OriginalDefinition;
        NeedType(t);
        constructed.Add(t);
    }

    public bool NeedsStaticInit(INamedTypeSymbol t)
    {
        t = t.OriginalDefinition;
        if (t.StaticConstructors.Any(c => !c.IsImplicitlyDeclared)) return true;
        return StaticFieldInitializers(t).Any();
    }

    // Does translated code run this type's static initialization? (Its static fields are used, or
    // it has an explicit static constructor, which runs before any static member or instance.)
    public bool StaticInitActive(INamedTypeSymbol t)
    {
        t = t.OriginalDefinition;
        if (!NeedsStaticInit(t)) return false;
        if (!IsBeforeFieldInit(t)) return true;
        return t.GetMembers().OfType<IFieldSymbol>().Any(f => f.IsStatic && !f.IsConst && usedFields.Contains(f))
            || t.GetMembers().OfType<IEventSymbol>().Any(e => e.IsStatic && usedEvents.Contains(e));
    }

    public bool IsBeforeFieldInit(INamedTypeSymbol t) => beforeFieldInit.Contains(Names.FullName(t.OriginalDefinition));

    public bool HidesBase(IMethodSymbol m)
    {
        if (m.IsStatic || m.IsOverride || m.MethodKind != MethodKind.Ordinary) return false;
        for (var b = m.ContainingType.BaseType; b != null && N.IsGame(b); b = b.BaseType)
            foreach (var o in b.GetMembers(m.Name).OfType<IMethodSymbol>())
                if ((o.IsVirtual || o.IsAbstract || o.IsOverride) && SameSignature(o, m)) return true;
        return false;
    }

    public int HideDepth(IMethodSymbol m)
    {
        int d = 0;
        for (var b = m.ContainingType; b != null; b = b.BaseType) d++;
        return d;
    }

    static bool SameSignature(IMethodSymbol a, IMethodSymbol b) =>
        a.Parameters.Length == b.Parameters.Length &&
        a.Parameters.Zip(b.Parameters).All(p => SymbolEqualityComparer.Default.Equals(p.First.Type, p.Second.Type) && p.First.RefKind == p.Second.RefKind);

    public bool ImplementsIDisposable(ITypeSymbol t) =>
        t.AllInterfaces.Any(i => i.SpecialType == SpecialType.System_IDisposable) || t.SpecialType == SpecialType.System_IDisposable;

    // ---- seeds --------------------------------------------------------------------------------------

    // units.txt: one unit a line:  file <path.cs>   (every type declared in the C# file, whole)
    //                              type <MetadataName> [Member ...]   (the type; only those members if given)
    public void AddUnits(string path)
    {
        // Two passes: first what is listed (so a stub decision never comes before the listing
        // that overrides it), then the closure's seeds.
        var actions = new List<Action>();
        int n = 0;
        foreach (var raw in File.ReadLines(path))
        {
            n++;
            var line = raw.Trim();
            int hash = line.IndexOf('#');
            if (hash >= 0) line = line[..hash].Trim();
            if (line.Length == 0) continue;
            var f = line.Split((char[])null, StringSplitOptions.RemoveEmptyEntries);
            int at = n;
            switch (f[0])
            {
                case "all":
                    actions.Add(AddAll);
                    break;
                case "file":
                {
                    var tree = GC.Tree(f[1]);
                    var model = GC.Model(tree);
                    var types = tree.GetRoot().DescendantNodes().OfType<BaseTypeDeclarationSyntax>()
                        .Select(d => model.GetDeclaredSymbol(d)).OfType<INamedTypeSymbol>().ToList();
                    foreach (var t in types) ListWhole(t);
                    actions.Add(() => { foreach (var t in types) AddWhole(t); });
                    break;
                }
                case "stub":
                    stubTypes.Add(GC.Type(f[1]));
                    break;
                case "stub-others":
                    stubOthers = true;
                    break;
                case "hand":
                {
                    var t = GC.Type(f[1]);
                    if (f.Length < 3) throw new UsageException($"{path}:{at}: want `hand <type> <member...>`");
                    foreach (var name in f.Skip(2))
                    {
                        var ms = name == ".ctor" ? t.InstanceConstructors.Cast<ISymbol>().ToList() : t.GetMembers(name).ToList();
                        if (ms.Count == 0) throw new UsageException($"{path}:{at}: no member {name} in {t}");
                        foreach (var s in ms)
                            switch (s)
                            {
                                case IMethodSymbol hm: handMethods.Add(hm.OriginalDefinition); break;
                                case IPropertySymbol hp:
                                    if (hp.GetMethod != null) handMethods.Add(hp.GetMethod.OriginalDefinition);
                                    if (hp.SetMethod != null) handMethods.Add(hp.SetMethod.OriginalDefinition);
                                    break;
                                default: throw new UsageException($"{path}:{at}: {name} in {t} is not a method or property");
                            }
                    }
                    // (declared whether or not the closure reaches them: the hand-written file defines them all)
                    var hands = f.Skip(2).SelectMany(name => name == ".ctor" ? t.InstanceConstructors.Cast<ISymbol>() : t.GetMembers(name)).ToList();
                    actions.Add(() =>
                    {
                        foreach (var s in hands)
                            if (s is IMethodSymbol hm) NeedMethod(hm);
                            else if (s is IPropertySymbol hp)
                            {
                                if (hp.GetMethod != null) NeedMethod(hp.GetMethod);
                                if (hp.SetMethod != null) NeedMethod(hp.SetMethod);
                            }
                    });
                    break;
                }
                case "type":
                {
                    var t = GC.Type(f[1]);
                    if (f.Length == 2)
                    {
                        ListWhole(t);
                        actions.Add(() => AddWhole(t));
                        break;
                    }
                    var members = new List<ISymbol>();
                    foreach (var name in f.Skip(2))
                    {
                        var ms = t.GetMembers(name).ToList();
                        if (name == ".ctor") ms = t.InstanceConstructors.Cast<ISymbol>().ToList();
                        if (ms.Count == 0) throw new UsageException($"{path}:{at}: no member {name} in {t}");
                        members.AddRange(ms);
                    }
                    foreach (var m in members) ListMember(m);
                    listedTypes.Add(t.OriginalDefinition); // (named: what the closure reaches of it translates)
                    partialTypes.Add(t.OriginalDefinition);
                    actions.Add(() =>
                    {
                        NeedType(t);
                        foreach (var m in members) AddMember(m);
                    });
                    break;
                }
                default:
                    throw new UsageException($"{path}:{n}: want `file <path>`, `type <name> [members]`, `hand <name> <members>`, `stub <name>` or `stub-others`");
            }
        }
        foreach (var a in actions) a();
    }

    void ListWhole(INamedTypeSymbol t)
    {
        listedTypes.Add(t.OriginalDefinition);
        foreach (var nt in t.GetTypeMembers()) ListWhole(nt);
    }

    void ListMember(ISymbol m)
    {
        switch (m)
        {
            case IMethodSymbol ms: listedMethods.Add(ms.OriginalDefinition); break;
            case IPropertySymbol p:
                if (p.GetMethod != null) listedMethods.Add(p.GetMethod.OriginalDefinition);
                if (p.SetMethod != null) listedMethods.Add(p.SetMethod.OriginalDefinition);
                break;
        }
    }

    // Every type of the game (the survey's measure of what translates).
    public void AddAll()
    {
        foreach (var (rel, tree) in GC.Trees.OrderBy(kv => kv.Key, StringComparer.Ordinal))
        {
            var model = GC.Model(tree);
            foreach (var decl in tree.GetRoot().DescendantNodes().OfType<BaseTypeDeclarationSyntax>())
                if (model.GetDeclaredSymbol(decl) is INamedTypeSymbol t && t.ContainingType == null) AddWhole(t);
        }
    }

    void AddWhole(INamedTypeSymbol t)
    {
        t = t.OriginalDefinition;
        fullTypes.Add(t);
        listedTypes.Add(t);
        NeedType(t);
        if (t.TypeKind is TypeKind.Class or TypeKind.Struct && !t.IsAbstract && !t.IsStatic) constructed.Add(t);
        foreach (var m in t.GetMembers()) AddMember(m);
        foreach (var nt in t.GetTypeMembers()) AddWhole(nt);
    }

    void AddMember(ISymbol m)
    {
        switch (m)
        {
            case IMethodSymbol ms when ms.MethodKind != MethodKind.DelegateInvoke && ms.ContainingType.TypeKind != TypeKind.Delegate:
                listedMethods.Add(ms.OriginalDefinition);
                NeedMethod(ms);
                break;
            case IPropertySymbol p:
                if (p.GetMethod != null) { listedMethods.Add(p.GetMethod.OriginalDefinition); NeedMethod(p.GetMethod); }
                if (p.SetMethod != null) { listedMethods.Add(p.SetMethod.OriginalDefinition); NeedMethod(p.SetMethod); }
                break;
            case IEventSymbol e:
                UseEvent(e);
                break;
            case IFieldSymbol f:
                UseField(f);
                break;
        }
    }

    // ---- the closure ------------------------------------------------------------------------------------

    public void Run()
    {
        for (;;)
        {
            while (queue.Count > 0) TranslateMethod(queue.Dequeue());
            int before = methods.Count;
            // Overrides and interface implementations of what is translated, in the types in use.
            foreach (var t in types.ToList())
            {
                if (StaticInitActive(t) && t.StaticConstructors.FirstOrDefault() is IMethodSymbol cctor) NeedMethod(cctor);
                // The engine calls a MonoBehaviour's messages: they are seeds of any one in use.
                if (t.TypeKind == TypeKind.Class && Emitter.DerivesFrom(t, "UnityEngine.MonoBehaviour"))
                    foreach (var m in t.GetMembers().OfType<IMethodSymbol>())
                        if (!m.IsStatic && Emitter.Messages.Contains(m.Name)) NeedSeed(m);
                if (t.TypeKind is not (TypeKind.Class or TypeKind.Struct)) continue;
                foreach (var m in t.GetMembers().OfType<IMethodSymbol>())
                {
                    if (methods.ContainsKey(m)) continue;
                    for (var o = m.OverriddenMethod; o != null; o = o.OverriddenMethod)
                        if (methods.ContainsKey(o.OriginalDefinition)) { NeedSeed(m); break; }
                }
                foreach (var i in t.AllInterfaces)
                    foreach (var im in i.GetMembers().OfType<IMethodSymbol>())
                    {
                        if (!N.IsGame(im) && !methods.ContainsKey(im.OriginalDefinition) && !IsRuntimeInterfaceMember(im)) continue;
                        if (N.IsGame(im) && !methods.ContainsKey(im.OriginalDefinition)) continue;
                        if (t.FindImplementationForInterfaceMember(im) is IMethodSymbol impl && N.IsGame(impl)) NeedSeed(impl);
                    }
            }
            // Accessors of auto-properties and field-like events of used types come with them.
            if (methods.Count == before && queue.Count == 0) break;
        }
        CloseDeclarations();
    }

    // The types the declarations name (fields, signatures, events) are declared too.
    void CloseDeclarations()
    {
        for (int count = -1; count != types.Count;)
        {
            count = types.Count;
            foreach (var t in types.ToList())
            {
                foreach (var m in t.GetMembers())
                {
                    try
                    {
                        switch (m)
                        {
                            case IFieldSymbol f when !f.IsConst && (usedFields.Contains(f) || fullTypes.Contains(t) && f.AssociatedSymbol == null
                                                                    || f.AssociatedSymbol is IPropertySymbol p && (p.GetMethod != null && methods.ContainsKey(p.GetMethod) || p.SetMethod != null && methods.ContainsKey(p.SetMethod))):
                                N.Ref(f.Type);
                                break;
                            case IMethodSymbol ms when methods.ContainsKey(ms):
                                N.Ref(ms.ReturnType);
                                foreach (var p in ms.Parameters) N.Ref(p.Type);
                                break;
                            case IEventSymbol e when usedEvents.Contains(e) || fullTypes.Contains(t):
                                N.Ref(e.Type);
                                break;
                        }
                    }
                    catch (TranslateException) { }
                }
            }
        }
    }

    // The members of runtime interfaces a class must implement when it declares them
    // (IEnumerator.MoveNext of a coroutine, IDisposable.Dispose...).
    static bool IsRuntimeInterfaceMember(IMethodSymbol im) => true;

    void TranslateMethod(IMethodSymbol m)
    {
        var r = methods[m];
        if (r.Stub || r.Hand)
        {
            // Declared with a throwing body; its signature's types are still declared.
            return;
        }
        current = r;
        try
        {
            r.Body = new MethodTranslator(this, m).Translate();
        }
        catch (TranslateException e)
        {
            r.Error = e.Message;
            r.Body = null;
        }
        catch (Exception e) when (e is InvalidCastException or NullReferenceException or ArgumentException or KeyNotFoundException or IndexOutOfRangeException or InvalidOperationException)
        {
            r.Error = "translator: " + e.GetType().Name + ": " + e.Message.Split('\n')[0];
            r.Body = null;
        }
        finally
        {
            current = null;
        }
    }

    // ---- helpers for the emitter -------------------------------------------------------------------------

    public IEnumerable<(IFieldSymbol field, IOperation value)> StaticFieldInitializers(INamedTypeSymbol t) => FieldInitializers(t, true);
    public IEnumerable<(IFieldSymbol field, IOperation value)> InstanceFieldInitializers(INamedTypeSymbol t) => FieldInitializers(t, false);

    IEnumerable<(IFieldSymbol, IOperation)> FieldInitializers(INamedTypeSymbol t, bool isStatic)
    {
        foreach (var r in t.DeclaringSyntaxReferences)
        {
            var decl = r.GetSyntax() as TypeDeclarationSyntax;
            if (decl == null) continue;
            var model = GC.Model(decl.SyntaxTree);
            foreach (var member in decl.Members)
            {
                if (member is FieldDeclarationSyntax fd && fd.Modifiers.Any(SyntaxKind.StaticKeyword) == isStatic && !fd.Modifiers.Any(SyntaxKind.ConstKeyword))
                {
                    foreach (var v in fd.Declaration.Variables)
                        if (v.Initializer != null && model.GetDeclaredSymbol(v) is IFieldSymbol f)
                            yield return (f, (model.GetOperation(v.Initializer) as IFieldInitializerOperation)?.Value);
                }
                else if (member is PropertyDeclarationSyntax pd && pd.Initializer != null && pd.Modifiers.Any(SyntaxKind.StaticKeyword) == isStatic)
                {
                    var p = model.GetDeclaredSymbol(pd);
                    var bf = p?.ContainingType.GetMembers().OfType<IFieldSymbol>().FirstOrDefault(x => SymbolEqualityComparer.Default.Equals(x.AssociatedSymbol, p));
                    if (bf != null) yield return (bf, (model.GetOperation(pd.Initializer) as IPropertyInitializerOperation)?.Value);
                }
                else if (member is EventFieldDeclarationSyntax ed && ed.Modifiers.Any(SyntaxKind.StaticKeyword) == isStatic)
                {
                    foreach (var v in ed.Declaration.Variables)
                        if (v.Initializer != null) throw new TranslateException("event initializer");
                }
            }
        }
    }

    public bool IsAutoProperty(IPropertySymbol p)
    {
        foreach (var r in p.DeclaringSyntaxReferences)
            if (r.GetSyntax() is PropertyDeclarationSyntax pd && pd.AccessorList != null && pd.ExpressionBody == null
                && pd.AccessorList.Accessors.All(a => a.Body == null && a.ExpressionBody == null))
                return !p.IsAbstract && !p.IsExtern;
        return false;
    }

    public bool IsFieldLikeEvent(IEventSymbol e) =>
        e.DeclaringSyntaxReferences.Any(r => r.GetSyntax() is VariableDeclaratorSyntax { Parent.Parent: EventFieldDeclarationSyntax });

    public IReadOnlyCollection<INamedTypeSymbol> Types => types;
    public IReadOnlyDictionary<IMethodSymbol, MethodResult> Methods => methods;
    public bool IsFull(INamedTypeSymbol t) => fullTypes.Contains(t.OriginalDefinition);
    public bool IsFieldUsed(IFieldSymbol f) => usedFields.Contains(f.OriginalDefinition);
    public bool IsFieldUsedOrFull(IFieldSymbol f) => usedFields.Contains(f.OriginalDefinition) || fullTypes.Contains(f.ContainingType.OriginalDefinition) && f.AssociatedSymbol == null;
    public bool IsEventUsed(IEventSymbol e) => usedEvents.Contains(e.OriginalDefinition);
    public IReadOnlyCollection<string> Includes => includes;
}

// The translation of one method: its signature's parameters, constructors' field initializers
// and base calls, the static-init guard, and the body.
public sealed class MethodTranslator
{
    readonly Translator tr;
    readonly IMethodSymbol m;

    public MethodTranslator(Translator tr, IMethodSymbol m)
    {
        this.tr = tr;
        this.m = m;
    }

    public string Translate()
    {
        var type = m.ContainingType;
        // Accessors the emitter writes itself (auto-properties, field-like events) and bodiless methods.
        if (m.AssociatedSymbol is IPropertySymbol p && tr.IsAutoProperty(p))
        {
            var bf = type.GetMembers().OfType<IFieldSymbol>().FirstOrDefault(f => SymbolEqualityComparer.Default.Equals(f.AssociatedSymbol, p));
            if (bf != null) tr.UseField(bf);
            return null;
        }
        if (m.AssociatedSymbol is IEventSymbol e && tr.IsFieldLikeEvent(e))
        {
            tr.UseEvent(e);
            return null;
        }
        if (m.IsAbstract || m.IsExtern || type.TypeKind == TypeKind.Interface) return null;
        if (m.PartialImplementationPart != null) return null;

        var syntax = m.DeclaringSyntaxReferences.Select(r => r.GetSyntax()).FirstOrDefault();
        SemanticModel model = syntax != null ? tr.GC.Model(syntax.SyntaxTree) : TypeModel(type);
        var body = new Body(tr, model, m);

        // static constructors: the static field initializers, then the C# static constructor's body
        if (m.MethodKind == MethodKind.StaticConstructor)
        {
            foreach (var (f, v) in tr.StaticFieldInitializers(type))
            {
                if (v == null) throw new TranslateException("field initializer without an operation");
                tr.UseField(f);
                body.Assignment($"{tr.N.Type(type)}::{tr.N.Field(f)}", body.Expr(v), f.Type);
            }
            if (syntax is ConstructorDeclarationSyntax cds && model.GetOperation(cds) is IConstructorBodyOperation cb && cb.BlockBody != null)
                body.Statement(cb.BlockBody);
            return body.Text();
        }

        if (m.MethodKind == MethodKind.Constructor)
        {
            // C#'s order: the static constructor (a type without beforefieldinit), the field
            // initializers, the base constructor, the body. A this(...) initializer replaces the first two.
            if (tr.NeedsStaticInit(type) && !tr.IsBeforeFieldInit(type)) body.Raw("__si();");
            IConstructorBodyOperation cb = syntax is ConstructorDeclarationSyntax cds ? model.GetOperation(cds) as IConstructorBodyOperation : null;
            var init = cb?.Initializer;
            IInvocationOperation baseCall = init switch
            {
                IExpressionStatementOperation es => es.Operation as IInvocationOperation,
                IInvocationOperation inv => inv,
                _ => null,
            };
            bool thisInit = baseCall != null && SymbolEqualityComparer.Default.Equals(baseCall.TargetMethod.ContainingType, type);
            // ILSpy writes `base._002Ector()` in the body when statements precede the base call.
            bool baseInBody = syntax is ConstructorDeclarationSyntax cd2 && cd2.Body != null && cd2.Body.DescendantNodes().OfType<InvocationExpressionSyntax>()
                .Any(i => i.Expression is MemberAccessExpressionSyntax { Expression: BaseExpressionSyntax } ma && ma.Name.Identifier.Text == "_002Ector");
            if (baseInBody) baseCall = null;
            if (!thisInit)
            {
                foreach (var (f, v) in tr.InstanceFieldInitializers(type))
                {
                    if (v == null) throw new TranslateException("field initializer without an operation");
                    tr.UseField(f);
                    body.Assignment($"this->{tr.N.Field(f)}", body.Expr(v), f.Type, type.IsValueType);
                }
            }
            if (baseCall != null)
            {
                body.ExpressionStatementOf(baseCall);
            }
            else if (!baseInBody && !type.IsValueType && type.BaseType != null && tr.N.IsGame(type.BaseType))
            {
                // the implicit base(): the base's parameterless constructor
                var bc = type.BaseType.InstanceConstructors.FirstOrDefault(c => c.Parameters.Length == 0)
                         ?? throw new TranslateException("base class without a parameterless constructor");
                tr.NeedMethod(bc);
                body.Raw($"{tr.N.Type(type.BaseType)}::__ctor();");
            }
            if (cb?.BlockBody != null) body.Statement(cb.BlockBody);
            else if (cb?.ExpressionBody != null) body.Statement(cb.ExpressionBody);
            return body.Text();
        }

        if (syntax == null) throw new TranslateException($"no source for {m}");
        if (m.IsStatic && tr.NeedsStaticInit(type) && !tr.IsBeforeFieldInit(type))
            body.Raw("__si();");
        IOperation op = syntax switch
        {
            ArrowExpressionClauseSyntax arrow => model.GetOperation(arrow),
            _ => model.GetOperation(syntax),
        };
        switch (op)
        {
            case IMethodBodyOperation mb:
                if (mb.BlockBody != null) body.Statement(mb.BlockBody);
                else if (mb.ExpressionBody != null) body.Statement(mb.ExpressionBody);
                else return null;
                break;
            case IBlockOperation b:
                body.Statement(b);
                break;
            case null:
                // an accessor of a property with an expression body: `int X => expr;`
                if (syntax is PropertyDeclarationSyntax pd && pd.ExpressionBody != null)
                {
                    body.Statement(model.GetOperation(pd.ExpressionBody));
                    break;
                }
                if (syntax is IndexerDeclarationSyntax ix && ix.ExpressionBody != null)
                {
                    body.Statement(model.GetOperation(ix.ExpressionBody));
                    break;
                }
                throw new TranslateException($"no operation for {syntax.Kind()}");
            default:
                throw new TranslateException($"method body {op.Kind}");
        }
        return body.Text();
    }

    SemanticModel TypeModel(INamedTypeSymbol t)
    {
        var r = t.DeclaringSyntaxReferences.FirstOrDefault() ?? throw new TranslateException($"no source for {t}");
        return tr.GC.Model(r.SyntaxTree);
    }
}
