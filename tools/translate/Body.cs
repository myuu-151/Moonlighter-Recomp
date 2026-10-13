using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Operations;

namespace MoonlighterTranslate;

// A translated C++ expression.
//   Wide: a float expression evaluated in double (Mono's float model): its C++ type is double
//         until a store, argument, return or cast rounds it (Body.Coerce).
//   Pure: no side effects, so it needn't be ordered against other operands (constants, locals,
//         `this`, reads the bindings mark pure).
//   Const: a constant (never needs a temporary).
public sealed class Ex
{
    public string C;
    public bool Wide, Pure, Const;
    public ITypeSymbol T; // the C# type of the value (for temporaries)
    public Ex(string c, bool pure = false, bool wide = false, bool isConst = false, ITypeSymbol t = null)
    {
        C = c;
        Pure = pure || isConst;
        Wide = wide;
        Const = isConst;
        T = t;
    }
    public Ex With(string c) => new Ex(c, Pure, Wide, Const, T);
    public override string ToString() => C;
}

// Translates one method body (Roslyn's bound tree, IOperation) into C++ statements.
//
// Locals are hoisted to the top of the function (C++ forbids jumping over an initialized
// declaration; the decompiled iterators' MoveNext jump between cases and labels) and renamed
// apart (C# reuses names in sibling scopes). Temporaries are hoisted too: an expression that
// needs C#'s strict left-to-right evaluation spills its earlier operands into temporaries with
// the comma operator, `(t1 = a(), t2 = b(), f(t1, t2, c()))`, as C++ leaves the order of
// arguments and of most operands unspecified.
public sealed class Body
{
    readonly Translator tr;
    readonly Names N;
    readonly SemanticModel model;
    readonly IMethodSymbol method;
    readonly StringBuilder sb = new();
    int indent = 1;
    readonly Dictionary<ILocalSymbol, string> locals = new(SymbolEqualityComparer.Default);
    readonly HashSet<string> usedNames = new(StringComparer.Ordinal);
    readonly List<(string type, string name)> hoisted = new();
    readonly Dictionary<ILabelSymbol, string> labels = new(SymbolEqualityComparer.Default);
    // break targets: null for a native C++ loop/switch, else a label to goto.
    readonly Stack<string> breakTargets = new();
    int labelCounter;
    readonly bool isStruct;

    public Body(Translator tr, SemanticModel model, IMethodSymbol method)
    {
        this.tr = tr;
        N = tr.N;
        this.model = model;
        this.method = method;
        isStruct = method.ContainingType.IsValueType;
        foreach (var p in method.Parameters) usedNames.Add(ParamName(p));
    }

    public static string ParamName(IParameterSymbol p) => "p_" + Names.Ident(p.Name);

    public string Text()
    {
        var head = new StringBuilder();
        foreach (var (type, name) in hoisted) head.Append("    ").Append(type).Append(' ').Append(name).Append("{};\n");
        return head.ToString() + sb.ToString();
    }

    void Line(string s) => sb.Append(' ', indent * 4).Append(s).Append('\n');

    string Fresh(string baseName)
    {
        string n = Names.Ident(baseName);
        if (usedNames.Add(n)) return n;
        for (int i = 1; ; i++)
            if (usedNames.Add(n + "_" + i)) return n + "_" + i;
    }

    string Local(ILocalSymbol l)
    {
        if (locals.TryGetValue(l, out var n)) return n;
        n = Fresh("l_" + l.Name);
        locals[l] = n;
        if (l.IsRef) throw new TranslateException("ref local");
        hoisted.Add((N.Ref(l.Type), n));
        return n;
    }

    public string Temp(string cppType)
    {
        string n = Fresh("t" + hoisted.Count);
        hoisted.Add((cppType, n));
        return n;
    }
    string Temp(ITypeSymbol t, bool wide = false) => Temp(wide ? "double" : N.Ref(t));

    string Label(ILabelSymbol l)
    {
        if (labels.TryGetValue(l, out var n)) return n;
        n = Fresh("L_" + l.Name);
        labels[l] = n;
        return n;
    }

    // ---- entry points ----------------------------------------------------------------------------

    public void Statement(IOperation op)
    {
        switch (op)
        {
            case IBlockOperation b:
                foreach (var s in b.Operations) Statement(s);
                break;
            case IVariableDeclarationGroupOperation g:
                foreach (var d in g.Declarations)
                    foreach (var v in d.Declarators)
                    {
                        string name = Local(v.Symbol);
                        var init = v.Initializer?.Value ?? d.Initializer?.Value;
                        if (init != null) Line($"{name} = {Coerce(Expr(init), v.Symbol.Type)};");
                    }
                break;
            case IExpressionStatementOperation es:
                ExpressionStatement(es.Operation);
                break;
            case IReturnOperation r:
                if (r.Kind == OperationKind.YieldReturn || r.Kind == OperationKind.YieldBreak) throw new TranslateException("yield");
                if (r.ReturnedValue == null) Line("return;");
                else Line($"return {Coerce(Expr(r.ReturnedValue), method.ReturnType)};");
                break;
            case IConditionalOperation c:
            {
                Line($"if ({Cond(c.Condition)}) {{");
                Nested(c.WhenTrue);
                if (c.WhenFalse != null)
                {
                    if (c.WhenFalse is IConditionalOperation)
                    {
                        Line("} else {");
                        Nested(c.WhenFalse);
                        Line("}");
                    }
                    else
                    {
                        Line("} else {");
                        Nested(c.WhenFalse);
                        Line("}");
                    }
                }
                else Line("}");
                break;
            }
            case IWhileLoopOperation w:
                Loop(w);
                break;
            case IForLoopOperation f:
                For(f);
                break;
            case IForEachLoopOperation fe:
                ForEach(fe);
                break;
            case ISwitchOperation sw:
                Switch(sw);
                break;
            case IBranchOperation br:
                Branch(br);
                break;
            case ILabeledOperation lab:
                indent--;
                Line($"{Label(lab.Label)}:;");
                indent++;
                if (lab.Operation != null) Statement(lab.Operation);
                break;
            case ITryOperation t:
                Try(t);
                break;
            case IThrowOperation th:
                if (th.Exception == null) Line("throw;");
                else Line($"::cs::Throw({Expr(th.Exception)});");
                break;
            case IUsingOperation u:
                Using(u);
                break;
            case ILockOperation lk:
            {
                // One thread: the lock only evaluates its object (and checks it for null).
                Line($"::cs::nc({Expr(lk.LockedValue)});");
                Line("{");
                Nested(lk.Body);
                Line("}");
                break;
            }
            case IEmptyOperation:
                break;
            case ILocalFunctionOperation:
                throw new TranslateException("local function");
            default:
                throw new TranslateException($"statement {op.Kind}");
        }
    }

    // A line of C++ the caller made (a static-init guard, a base constructor call).
    public void Raw(string line) => Line(line);

    // lhs = value, rounded to the C# type where Mono stores it.
    public void Assignment(string lhs, Ex value, ITypeSymbol type, bool unused = false) => Line($"{lhs} = {Coerce(value, type)};");

    // A constructor initializer: base(...) or this(...).
    public void ExpressionStatementOf(IInvocationOperation inv)
    {
        var ctor = inv.TargetMethod;
        var args = Args(inv.Arguments.ToList(), out string pre);
        string argText = string.Join(", ", args.Select(a => a.C));
        if (pre.Length > 0) Line($"(void)({pre.TrimEnd(' ', ',')});");
        if (SymbolEqualityComparer.Default.Equals(ctor.ContainingType, method.ContainingType))
        {
            tr.NeedMethod(ctor);
            Line($"this->__ctor({argText});");
            return;
        }
        if (N.IsGame(ctor))
        {
            tr.NeedMethod(ctor);
            Line($"{N.Type(ctor.ContainingType)}::__ctor({argText});");
            return;
        }
        // an engine or runtime base class: its C++ constructor already ran
        if (ctor.Parameters.Length > 0)
        {
            var b = tr.Bind.Find("new", ctor);
            if (b == null) { Line(tr.Gap(ctor, "base")); return; }
            Line($"{N.Type(ctor.ContainingType)}::__ctor({argText});");
        }
    }

    void Nested(IOperation op)
    {
        indent++;
        Statement(op);
        indent--;
    }

    void ExpressionStatement(IOperation e)
    {
        if (e is IInvalidOperation && e.Syntax is InvocationExpressionSyntax ies && ies.Expression is MemberAccessExpressionSyntax { Expression: BaseExpressionSyntax } ma
            && ma.Name.Identifier.Text == "_002Ector")
        {
            // ILSpy shows a base constructor call that follows field initializers it couldn't fold
            if (ies.ArgumentList.Arguments.Count > 0) throw new TranslateException("base constructor call with arguments after statements");
            var bt = method.ContainingType.BaseType;
            if (bt != null && N.IsGame(bt))
            {
                var bc = bt.InstanceConstructors.FirstOrDefault(c => c.Parameters.Length == 0) ?? throw new TranslateException("no parameterless base constructor");
                tr.NeedMethod(bc);
                Line($"{N.Type(bt)}::__ctor();");
            }
            return;
        }
        // Statement forms that need no value.
        switch (e)
        {
            case ISimpleAssignmentOperation a:
                Line(Assign(a.Target, a.Value, false) + ";");
                return;
            case ICompoundAssignmentOperation ca:
                Line(CompoundAssign(ca, false) + ";");
                return;
            case IIncrementOrDecrementOperation inc:
                Line(IncDec(inc, false) + ";");
                return;
        }
        var x = Expr(e);
        Line(x.C + ";");
    }

    string Cond(IOperation c) => Expr(c).C;

    void Loop(IWhileLoopOperation w)
    {
        breakTargets.Push(null);
        if (w.ConditionIsTop)
        {
            string cond = w.Condition == null ? "true" : Cond(w.Condition);
            if (w.ConditionIsUntil) cond = $"!({cond})";
            Line($"while ({cond}) {{");
            Nested(w.Body);
            Line("}");
        }
        else
        {
            Line("do {");
            Nested(w.Body);
            string cond = w.Condition == null ? "true" : Cond(w.Condition);
            if (w.ConditionIsUntil) cond = $"!({cond})";
            Line($"}} while ({cond});");
        }
        breakTargets.Pop();
    }

    void For(IForLoopOperation f)
    {
        foreach (var b in f.Before) Statement(b);
        string cond = f.Condition == null ? "" : Cond(f.Condition);
        var incs = new List<string>();
        foreach (var a in f.AtLoopBottom)
        {
            var e = a is IExpressionStatementOperation es ? es.Operation : a;
            incs.Add(e switch
            {
                ISimpleAssignmentOperation sa => Assign(sa.Target, sa.Value, false),
                ICompoundAssignmentOperation ca => CompoundAssign(ca, false),
                IIncrementOrDecrementOperation inc => IncDec(inc, false),
                _ => Expr(e).C,
            });
        }
        breakTargets.Push(null);
        Line($"for (; {cond}; {string.Join(", ", incs.Select(s => "(void)(" + s + ")"))}) {{");
        Nested(f.Body);
        Line("}");
        breakTargets.Pop();
    }

    void ForEach(IForEachLoopOperation fe)
    {
        var syntax = fe.Syntax as ForEachStatementSyntax ?? throw new TranslateException("foreach without syntax");
        var info = model.GetForEachStatementInfo(syntax);
        var collType = fe.Collection is IConversionOperation conv && conv.IsImplicit ? conv.Operand.Type : fe.Collection.Type;
        var collOp = fe.Collection is IConversionOperation conv2 && conv2.IsImplicit ? conv2.Operand : fe.Collection;
        // The iteration variable(s).
        string loopVar;
        ITypeSymbol varType;
        if (fe.LoopControlVariable is IVariableDeclaratorOperation vd) { loopVar = Local(vd.Symbol); varType = vd.Symbol.Type; }
        else if (fe.LoopControlVariable is IVariableDeclarationOperation vdo && vdo.Declarators.Length == 1) { loopVar = Local(vdo.Declarators[0].Symbol); varType = vdo.Declarators[0].Symbol.Type; }
        else throw new TranslateException("foreach variable");

        if (collType is IArrayTypeSymbol arr && arr.Rank == 1)
        {
            // The compiler's lowering: the array once, an index loop.
            string a = Temp(collType);
            string i = Temp("int32_t");
            Line($"{a} = {Expr(collOp)};");
            breakTargets.Push(null);
            Line($"for ({i} = 0; {i} < ::cs::nc({a})->get_Length(); {i} = ::cs::add({i}, 1)) {{");
            indent++;
            Line($"{loopVar} = {ElementConvert($"{a}->at({i})", arr.ElementType, varType, info)};");
            Statement(fe.Body);
            indent--;
            Line("}");
            breakTargets.Pop();
            return;
        }
        if (collType?.SpecialType == SpecialType.System_String)
        {
            string s = Temp(collType);
            string i = Temp("int32_t");
            Line($"{s} = {Expr(collOp)};");
            breakTargets.Push(null);
            Line($"for ({i} = 0; {i} < ::cs::nc({s})->get_Length(); {i} = ::cs::add({i}, 1)) {{");
            indent++;
            Line($"{loopVar} = {s}->get_Chars({i});");
            Statement(fe.Body);
            indent--;
            Line("}");
            breakTargets.Pop();
            return;
        }
        var getEnum = info.GetEnumeratorMethod ?? throw new TranslateException("foreach without GetEnumerator");
        var moveNext = info.MoveNextMethod;
        var current = info.CurrentProperty;
        var enumType = getEnum.ReturnType;
        string e = Temp(enumType);
        // collection.GetEnumerator() (through the collection's static type, as the compiler binds it)
        var collEx = Expr(collOp);
        string recvType = collOp.Type is ITypeSymbol rt ? rt.ToDisplayString() : "";
        Line($"{e} = {CallOn(getEnum, collEx, collOp.Type, new List<Ex>())};");
        var disposeNeeded = info.DisposeMethod != null || (enumType.IsReferenceType && !enumType.IsSealed) || tr.ImplementsIDisposable(enumType);
        Line("{");
        indent++;
        if (disposeNeeded)
        {
            string disp = DisposeCall(e, enumType);
            if (disp != null) Line($"auto fin_{e} = ::cs::MakeFinally([&] {{ {disp}; }});");
        }
        breakTargets.Push(null);
        var eEx = new Ex(e, pure: true);
        Line($"while ({CallOn(moveNext, eEx, enumType, new List<Ex>())}) {{");
        indent++;
        string cur = CallOn(current.GetMethod, eEx, enumType, new List<Ex>());
        Line($"{loopVar} = {ElementConvert(cur, current.Type, varType, info)};");
        Statement(fe.Body);
        indent--;
        Line("}");
        breakTargets.Pop();
        indent--;
        Line("}");
    }

    // The Dispose a foreach/using calls on a resource of type t (null if none).
    string DisposeCall(string e, ITypeSymbol t)
    {
        if (t.IsValueType)
        {
            var d = t.GetMembers("Dispose").OfType<IMethodSymbol>().FirstOrDefault(m => m.Parameters.Length == 0);
            if (d != null) return CallOn(d, new Ex(e, pure: true), t, new List<Ex>());
            return null;
        }
        // through IDisposable, when the object implements it
        return $"if (auto* d_ = dynamic_cast<::cs::IDisposable*>({e})) d_->IDisposable_Dispose()";
    }

    string ElementConvert(string value, ITypeSymbol from, ITypeSymbol to, ForEachStatementInfo info)
    {
        if (SymbolEqualityComparer.Default.Equals(from, to)) return value;
        return ConvertCode(new Ex(value), from, to, info.ElementConversion.IsExplicit || info.ElementConversion.IsUnboxing || info.ElementConversion.IsReference && !info.ElementConversion.IsImplicit).C;
    }

    void Switch(ISwitchOperation sw)
    {
        var vt = sw.Value.Type;
        bool native = vt.SpecialType is SpecialType.System_Int32 or SpecialType.System_UInt32 or SpecialType.System_Int64 or SpecialType.System_UInt64
            or SpecialType.System_Int16 or SpecialType.System_UInt16 or SpecialType.System_Byte or SpecialType.System_SByte or SpecialType.System_Char
            || vt.TypeKind == TypeKind.Enum;
        foreach (var c in sw.Cases)
            foreach (var cl in c.Clauses)
                if (cl is not ISingleValueCaseClauseOperation and not IDefaultCaseClauseOperation) throw new TranslateException($"switch clause {cl.CaseKind}");
        if (native)
        {
            breakTargets.Push(null);
            Line($"switch ({Expr(sw.Value)}) {{");
            foreach (var c in sw.Cases)
            {
                foreach (var cl in c.Clauses)
                {
                    if (cl is ISingleValueCaseClauseOperation sv) Line($"case {CaseConstant(sv.Value, vt)}:");
                    else Line("default:");
                    if (cl.Label != null) { indent--; Line($"{Label(cl.Label)}:;"); indent++; }
                }
                Line("{");
                indent++;
                foreach (var s in c.Body) Statement(s);
                indent--;
                Line("}");
            }
            Line("}");
            breakTargets.Pop();
            return;
        }
        // Strings (and anything else): compare in order, then jump to the section.
        string v = Temp(vt);
        string end = Fresh("switch_end");
        Line($"{v} = {Expr(sw.Value)};");
        var sectionLabels = sw.Cases.Select(_ => Fresh("case")).ToList();
        string defaultLabel = null;
        for (int i = 0; i < sw.Cases.Length; i++)
            foreach (var cl in sw.Cases[i].Clauses)
            {
                if (cl is ISingleValueCaseClauseOperation sv)
                {
                    string test = vt.SpecialType == SpecialType.System_String
                        ? $"::cs::String::op_Equality({v}, {Expr(sv.Value)})"
                        : $"({v} == {Expr(sv.Value)})";
                    Line($"if ({test}) goto {sectionLabels[i]};");
                }
                else defaultLabel = sectionLabels[i];
            }
        Line($"goto {defaultLabel ?? end};");
        breakTargets.Push(end);
        for (int i = 0; i < sw.Cases.Length; i++)
        {
            indent--;
            Line($"{sectionLabels[i]}:;");
            foreach (var cl in sw.Cases[i].Clauses)
                if (cl.Label != null) Line($"{Label(cl.Label)}:;");
            indent++;
            Line("{");
            indent++;
            foreach (var s in sw.Cases[i].Body) Statement(s);
            indent--;
            Line("}");
        }
        breakTargets.Pop();
        indent--;
        Line($"{end}:;");
        indent++;
    }

    string CaseConstant(IOperation value, ITypeSymbol switchType)
    {
        var cv = value.ConstantValue;
        if (!cv.HasValue) throw new TranslateException("non-constant case");
        if (switchType.TypeKind == TypeKind.Enum) return EnumConstant((INamedTypeSymbol)switchType, cv.Value);
        return Names.Literal(cv.Value, switchType);
    }

    void Branch(IBranchOperation br)
    {
        switch (br.BranchKind)
        {
            case BranchKind.Break:
            {
                string target = breakTargets.Count > 0 ? breakTargets.Peek() : null;
                Line(target == null ? "break;" : $"goto {target};");
                break;
            }
            case BranchKind.Continue:
                Line("continue;");
                break;
            case BranchKind.GoTo:
                Line($"goto {Label(br.Target)};");
                break;
            default:
                throw new TranslateException($"branch {br.BranchKind}");
        }
    }

    void Try(ITryOperation t)
    {
        if (t.Finally != null)
        {
            // try { body } finally { fin }: the finally runs when the block is left, whichever way.
            Line("{");
            indent++;
            var fin = new Body(tr, model, method, this);
            fin.indent = indent + 1;
            fin.Statement(t.Finally);
            Line("auto fin_ = ::cs::MakeFinally([&] {");
            sb.Append(fin.sb);
            Line("});");
            if (t.Catches.Length > 0) TryCatch(t);
            else Statement(t.Body);
            indent--;
            Line("}");
            return;
        }
        TryCatch(t);
    }

    // A Body that shares the enclosing method's locals (for finally blocks emitted as lambdas).
    Body(Translator tr, SemanticModel model, IMethodSymbol method, Body parent)
    {
        this.tr = tr;
        N = tr.N;
        this.model = model;
        this.method = method;
        isStruct = parent.isStruct;
        locals = parent.locals;
        usedNames = parent.usedNames;
        hoisted = parent.hoisted;
        labels = parent.labels;
    }

    void TryCatch(ITryOperation t)
    {
        Line("try {");
        Nested(t.Body);
        string x = Fresh("ex");
        Line($"}} catch (const ::cs::Thrown& {x}) {{");
        indent++;
        bool first = true;
        bool catchAll = false;
        foreach (var c in t.Catches)
        {
            if (c.Filter != null) throw new TranslateException("exception filter");
            var ct = c.ExceptionType;
            bool all = ct == null || ct.ToDisplayString() is "System.Exception" or "object";
            string test = all ? "true" : $"::cs::Is<{N.Type(ct)}>({x}.ex)";
            Line($"{(first ? "" : "} else ")}if ({test}) {{");
            indent++;
            if (c.ExceptionDeclarationOrExpression is IVariableDeclaratorOperation d)
                Line($"{Local(d.Symbol)} = ::cs::Cast<{N.Type(d.Symbol.Type)}>({x}.ex);");
            Statement(c.Handler);
            indent--;
            first = false;
            if (all) { catchAll = true; break; }
        }
        if (!catchAll) Line("} else {\n" + new string(' ', (indent + 1) * 4) + "throw;");
        Line("}");
        indent--;
        Line("}");
    }

    void Using(IUsingOperation u)
    {
        string res;
        ITypeSymbol rt;
        if (u.Resources is IVariableDeclarationGroupOperation g && g.Declarations.Length == 1 && g.Declarations[0].Declarators.Length == 1)
        {
            var d = g.Declarations[0].Declarators[0];
            res = Local(d.Symbol);
            rt = d.Symbol.Type;
            var init = d.Initializer?.Value ?? g.Declarations[0].Initializer?.Value;
            Line($"{res} = {Coerce(Expr(init), rt)};");
        }
        else
        {
            rt = u.Resources.Type;
            res = Temp(rt);
            Line($"{res} = {Expr(u.Resources)};");
        }
        Line("{");
        indent++;
        string disp = rt.IsValueType ? DisposeCall(res, rt) : $"if ({res}) {DisposeCall(res, rt)}";
        if (disp != null) Line($"auto fin_{res} = ::cs::MakeFinally([&] {{ {disp}; }});");
        Statement(u.Body);
        indent--;
        Line("}");
    }

    // ---- expressions ---------------------------------------------------------------------------------

    public Ex Expr(IOperation op)
    {
        // A value of a type with no C++ mapping can't be used: the method waits on that binding.
        if (op.Type != null && op.Type.TypeKind != TypeKind.Error) TouchType(op.Type);
        var e = ExprCore(op);
        e.T ??= op.Type;
        return e;
    }

    // Records the gaps of a type (and of the engine bases a game type derives from: an object
    // of a class whose base isn't mapped can't be used as any of its mapped bases).
    void TouchType(ITypeSymbol t)
    {
        try { N.Ref(t); } catch (TranslateException) { }
        if (t.TypeKind != TypeKind.Class || !N.IsGame(t)) return;
        // the first engine base of a game class: mapped, or the class can't be used as one
        for (var b = (t as INamedTypeSymbol)?.BaseType; b != null && b.SpecialType != SpecialType.System_Object; b = b.BaseType)
        {
            if (N.IsGame(b)) continue;
            if (tr.Bind.FindType(b) == null) tr.Gap(b, "type");
            break;
        }
    }

    Ex ExprCore(IOperation op)
    {
        if (op.ConstantValue.HasValue && op.Type != null && op.Kind != OperationKind.DelegateCreation)
            return Constant(op.ConstantValue.Value, op.Type);
        switch (op)
        {
            case ILiteralOperation lit:
                return Constant(lit.ConstantValue.Value, lit.Type);
            case ILocalReferenceOperation l:
                return new Ex(Local(l.Local), pure: true);
            case IParameterReferenceOperation p:
                return new Ex(ParamName(p.Parameter), pure: true);
            case IInstanceReferenceOperation:
                return new Ex(isStruct ? "(*this)" : "this", pure: true);
            case IFieldReferenceOperation f:
                return FieldRef(f);
            case IPropertyReferenceOperation pr:
                return PropertyGet(pr);
            case IEventReferenceOperation ev:
                return EventField(ev);
            case IInvocationOperation inv:
                return Invocation(inv);
            case IObjectCreationOperation oc:
                return ObjectCreation(oc);
            case IArrayCreationOperation ac:
                return ArrayCreation(ac);
            case IArrayElementReferenceOperation ae:
                return ArrayElement(ae);
            case IConversionOperation cv:
                return Conversion(cv);
            case IBinaryOperation b:
                return Binary(b);
            case IUnaryOperation u:
                return Unary(u);
            case IConditionalOperation c:
                return Ternary(c);
            case ICoalesceOperation co:
                return Coalesce(co);
            case ISimpleAssignmentOperation a:
                return new Ex(Assign(a.Target, a.Value, true));
            case ICompoundAssignmentOperation ca:
                return new Ex(CompoundAssign(ca, true));
            case IIncrementOrDecrementOperation inc:
                return new Ex(IncDec(inc, true));
            case IDelegateCreationOperation dc:
                return DelegateCreation(dc);
            case IEventAssignmentOperation ea:
                return EventAssign(ea);
            case IIsTypeOperation it:
            {
                var v = Expr(it.ValueOperand);
                var tt = it.TypeOperand;
                string test = tt.TypeKind == TypeKind.TypeParameter ? $"::cs::IsT<{N.Ref(tt)}>({ObjectOf(v, it.ValueOperand.Type)})"
                    : Names.IsRef(tt) ? $"::cs::Is<{N.Type(tt)}>({ObjectOf(v, it.ValueOperand.Type)})" : $"::cs::IsBoxed<{N.Ref(tt)}>({ObjectOf(v, it.ValueOperand.Type)})";
                return new Ex(it.IsNegated ? $"!{test}" : test, pure: v.Pure);
            }
            case IDefaultValueOperation dv:
                return new Ex(Default(dv.Type), isConst: true);
            case ITypeOfOperation to:
                return new Ex($"::cs::TypeOf<{N.Ref(to.TypeOperand)}>()", pure: true);
            case ISizeOfOperation so:
                throw new TranslateException("sizeof");
            case IDiscardOperation:
                return new Ex("(void)0", pure: true);
            case IParenthesizedOperation par:
                return Expr(par.Operand);
            case IThrowOperation th:
                return new Ex($"(::cs::Throw({Expr(th.Exception)}), {Default(th.Type ?? tr.Comp.GetSpecialType(SpecialType.System_Object))})");
            case IMethodReferenceOperation:
                throw new TranslateException("method group outside a delegate creation");
            case IAnonymousFunctionOperation:
                throw new TranslateException("lambda (decompiled as a closure class: should not appear)");
            case IConditionalAccessOperation:
                throw new TranslateException("?. (the decompiler lowers it)");
            case IInterpolatedStringOperation:
                throw new TranslateException("string interpolation");
            case IIsPatternOperation:
                throw new TranslateException("is pattern");
            case ITupleOperation:
                throw new TranslateException("tuple");
            case IDeclarationExpressionOperation:
                throw new TranslateException("declaration expression");
            case IAddressOfOperation:
                throw new TranslateException("address-of");
            case IArgumentOperation arg:
                return Expr(arg.Value);
            case IInvalidOperation:
                throw new TranslateException($"invalid C#: {op.Syntax}");
        }
        throw new TranslateException($"expression {op.Kind}");
    }

    string Default(ITypeSymbol t)
    {
        if (Names.IsRef(t)) return $"static_cast<{N.Ref(t)}>(nullptr)";
        return $"{N.Ref(t)}{{}}";
    }

    Ex Constant(object v, ITypeSymbol t)
    {
        if (v == null) return new Ex(Names.IsRef(t) ? $"static_cast<{N.Ref(t)}>(nullptr)" : $"{N.Ref(t)}{{}}", isConst: true);
        if (t.TypeKind == TypeKind.Enum) return new Ex(EnumConstant((INamedTypeSymbol)t, v), isConst: true);
        if (t.SpecialType == SpecialType.System_Object || t.TypeKind == TypeKind.Interface)
        {
            // a boxed constant: (object)5
            var ct = v switch { int => SpecialType.System_Int32, float => SpecialType.System_Single, double => SpecialType.System_Double, bool => SpecialType.System_Boolean, string => SpecialType.System_String, long => SpecialType.System_Int64, char => SpecialType.System_Char, _ => SpecialType.None };
            if (ct == SpecialType.System_String) return new Ex(Names.Literal(v, t), isConst: true);
            if (ct == SpecialType.None) throw new TranslateException("boxed constant");
            return new Ex($"::cs::BoxAny({Names.Literal(v, tr.Comp.GetSpecialType(ct))})");
        }
        // The literal's own C# type may differ from t (an implicitly converted constant): convert the value.
        object cv = t.SpecialType switch
        {
            SpecialType.System_Single => Convert.ToSingle(v),
            SpecialType.System_Double => Convert.ToDouble(v),
            SpecialType.System_Int32 => Convert.ToInt32(v),
            SpecialType.System_UInt32 => Convert.ToUInt32(v),
            SpecialType.System_Int64 => Convert.ToInt64(v),
            SpecialType.System_UInt64 => Convert.ToUInt64(v),
            SpecialType.System_Int16 => Convert.ToInt16(v),
            SpecialType.System_UInt16 => Convert.ToUInt16(v),
            SpecialType.System_Byte => Convert.ToByte(v),
            SpecialType.System_SByte => Convert.ToSByte(v),
            SpecialType.System_Char => Convert.ToChar(v),
            _ => v,
        };
        return new Ex(Names.Literal(cv, t), isConst: true);
    }

    public string EnumConstant(INamedTypeSymbol et, object v)
    {
        string type = N.Type(et);
        // the member with that value, if any
        foreach (var f in et.GetMembers().OfType<IFieldSymbol>())
            if (f.HasConstantValue && Equals(f.ConstantValue, v) && N.IsGame(et)) return $"{type}::{Names.Ident(f.Name)}";
        var u = et.EnumUnderlyingType;
        object cv = u.SpecialType switch
        {
            SpecialType.System_Int32 => Convert.ToInt32(v),
            SpecialType.System_UInt32 => Convert.ToUInt32(v),
            SpecialType.System_Int64 => Convert.ToInt64(v),
            SpecialType.System_Byte => Convert.ToByte(v),
            SpecialType.System_Int16 => Convert.ToInt16(v),
            SpecialType.System_UInt16 => Convert.ToUInt16(v),
            SpecialType.System_SByte => Convert.ToSByte(v),
            SpecialType.System_UInt64 => Convert.ToUInt64(v),
            _ => v,
        };
        return $"static_cast<{type}>({Names.Literal(cv, u)})";
    }

    // ---- members ---------------------------------------------------------------------------------------

    // The receiver of an instance member: `p` checked for null (a class), or the value (a struct).
    string Receiver(IOperation instance, out bool isValue)
    {
        var t = instance.Type;
        isValue = t != null && !Names.IsRef(t) && t.TypeKind != TypeKind.TypeParameter;
        if (instance is IInstanceReferenceOperation) return isStruct ? "(*this)" : "this";
        var e = Expr(instance);
        if (isValue) return e.C;
        return $"::cs::nc({e.C})";
    }
    string Receiver(Ex e, ITypeSymbol t, bool isThis, out bool isValue)
    {
        isValue = t != null && !Names.IsRef(t) && t.TypeKind != TypeKind.TypeParameter;
        if (isThis || isValue) return e.C;
        return $"::cs::nc({e.C})";
    }

    Ex FieldRef(IFieldReferenceOperation f)
    {
        var fs = f.Field;
        if (fs.IsConst) return Constant(fs.ConstantValue, fs.Type);
        if (!N.IsGame(fs))
        {
            var b = tr.Bind.Find("field", fs) ?? tr.Bind.Find("get", fs);
            if (b != null)
            {
                tr.UseBinding(b);
                string recv = fs.IsStatic ? null : Receiver(f.Instance, out _);
                return new Ex(N.Substitute(b.Template, recv, fs.ContainingType.TypeArguments, null), pure: b.Pure);
            }
            var tb = tr.Bind.FindType(fs.ContainingType);
            if (tb != null && tb.Conv && (tb.Members == null || tb.Members.Contains(fs.Name)))
            {
                tr.UseBinding(tb);
                if (fs.IsStatic) return new Ex($"{N.Type(fs.ContainingType)}::{Names.Ident(fs.Name)}");
                string recv = Receiver(f.Instance, out bool isValue);
                return new Ex($"{recv}{(isValue ? "." : "->")}{Names.Ident(fs.Name)}");
            }
            return new Ex(tr.Gap(fs, "field"));
        }
        tr.UseField(fs);
        if (fs.IsStatic)
        {
            string cls = N.Type(fs.ContainingType);
            return new Ex(tr.NeedsStaticInit(fs.ContainingType) ? $"({cls}::__si(), {cls}::{N.Field(fs)})" : $"{cls}::{N.Field(fs)}");
        }
        {
            bool isThis = f.Instance is IInstanceReferenceOperation;
            string recv = Receiver(f.Instance, out bool isValue);
            return new Ex($"{recv}{(isValue ? "." : "->")}{N.Field(fs)}", pure: isThis);
        }
    }

    Ex EventField(IEventReferenceOperation ev)
    {
        var es = ev.Event;
        if (!N.IsGame(es)) return new Ex(tr.Gap(es, "event"));
        var field = es.ContainingType.GetMembers(es.Name).OfType<IFieldSymbol>().FirstOrDefault()
                    ?? es.ContainingType.GetMembers().OfType<IFieldSymbol>().FirstOrDefault(x => SymbolEqualityComparer.Default.Equals(x.AssociatedSymbol, es));
        tr.UseEvent(es);
        if (es.IsStatic)
        {
            string cls = N.Type(es.ContainingType);
            return new Ex(tr.NeedsStaticInit(es.ContainingType) ? $"({cls}::__si(), {cls}::{Names.Ident(es.Name)})" : $"{cls}::{Names.Ident(es.Name)}");
        }
        string recv = Receiver(ev.Instance, out bool isValue);
        return new Ex($"{recv}{(isValue ? "." : "->")}{Names.Ident(es.Name)}");
    }

    Ex PropertyGet(IPropertyReferenceOperation pr)
    {
        var get = pr.Property.GetMethod ?? (pr.Property.OverriddenProperty?.GetMethod) ?? throw new TranslateException($"property without getter {pr.Property}");
        var args = pr.Arguments.Select(a => a).ToList();
        return Call(get, pr.Instance, args, pr.Property);
    }

    // ---- calls -------------------------------------------------------------------------------------

    Ex Invocation(IInvocationOperation inv)
    {
        var m = inv.TargetMethod;
        if (m.MethodKind == MethodKind.DelegateInvoke)
        {
            var recv = Expr(inv.Instance);
            var args = Args(inv.Arguments.ToList(), out string pre);
            if (pre.Length > 0 && !recv.Const)
            {
                string t = Temp(recv);
                pre = $"{t} = {recv.C}, " + pre;
                recv = new Ex(t, pure: true, t: recv.T);
            }
            string code = $"::cs::nc({recv.C})->Invoke({string.Join(", ", args.Select(a => a.C))})";
            return new Ex(Wrap(pre, code), t: m.ReturnType);
        }
        bool baseCall = inv.Instance?.Syntax is BaseExpressionSyntax;
        return Call(m, inv.Instance, inv.Arguments.ToList(), null, nonVirtual: baseCall);
    }

    // Arguments, converted to their parameters, in C#'s evaluation order (prefix: the temporaries).
    List<Ex> Args(List<IArgumentOperation> args, out string prefix)
    {
        var list = new List<Ex>();
        foreach (var a in args)
        {
            if (a.ArgumentKind == ArgumentKind.ParamArray && a.Value is IArrayCreationOperation pac)
            {
                list.Add(ArrayCreation(pac));
                continue;
            }
            var p = a.Parameter;
            if (p != null && p.RefKind is RefKind.Ref or RefKind.Out or RefKind.In)
            {
                // a variable's address: not reordered, never copied
                var lv = LValue(a.Value);
                var r = new Ex(lv.C, pure: false, t: a.Value.Type);
                refArgs.Add(r);
                list.Add(r);
                continue;
            }
            var v = Expr(a.Value);
            if (p != null) v = new Ex(Coerce(v, p.Type), v.Pure, false, v.Const, p.Type);
            list.Add(v);
        }
        return Sequence(list, out prefix);
    }

    // C#'s left-to-right order. When an operand has side effects, every operand before the last
    // such one is computed into a temporary first (constants excepted); the rest stay in place.
    List<Ex> Sequence(List<Ex> ops, out string prefix)
    {
        prefix = "";
        int lastImpure = -1;
        for (int i = 0; i < ops.Count; i++) if (!ops[i].Pure) lastImpure = i;
        if (lastImpure <= 0) return ops;
        var outList = new List<Ex>();
        var pre = new StringBuilder();
        for (int i = 0; i < ops.Count; i++)
        {
            if (i < lastImpure && !ops[i].Const && ops[i].C != "this" && ops[i].C != "(*this)" && !refArgs.Contains(ops[i]))
            {
                string t = Temp(ops[i]);
                pre.Append($"{t} = {ops[i].C}, ");
                outList.Add(new Ex(t, pure: true, wide: ops[i].Wide, t: ops[i].T));
            }
            else outList.Add(ops[i]);
        }
        prefix = pre.ToString();
        return outList;
    }

    readonly HashSet<Ex> refArgs = new();

    static bool IsSimpleLocal(Ex e) => e.C.StartsWith("l_") || e.C.StartsWith("p_") || e.C == "this";

    // A temporary for e's value: double for a wide float, else e's C# type.
    string Temp(Ex e)
    {
        if (e.Wide) return Temp("double");
        if (e.T == null) throw new TranslateException($"temporary without a type for {e.C}");
        return Temp(N.Ref(e.T));
    }

    static string Wrap(string prefix, string code) => prefix.Length > 0 ? $"({prefix}{code})" : code;

    Ex Call(IMethodSymbol m, IOperation instance, List<IArgumentOperation> args, IPropertySymbol prop, bool nonVirtual = false)
    {
        bool isThis = instance is IInstanceReferenceOperation;
        Ex recv = null;
        if (instance != null && !m.IsStatic) recv = isThis ? new Ex(isStruct ? "(*this)" : "this", pure: true, t: instance.Type) : Expr(instance);
        var converted = Args(args, out string pre);
        // C# evaluates the receiver, then the arguments, then checks the receiver for null:
        // a class receiver goes to a temporary when the arguments have side effects.
        if (recv != null && !isThis && Names.IsRef(instance.Type) && !recv.Const && (pre.Length > 0 || converted.Any(a => !a.Pure)))
        {
            string t = Temp(recv);
            pre = $"{t} = {recv.C}, " + pre;
            recv = new Ex(t, pure: true, t: recv.T);
        }
        string code = CallCode(m, recv, instance?.Type, isThis, converted, prop, nonVirtual, out bool pure);
        return new Ex(Wrap(pre, code), pure && converted.All(a => a.Pure) && (recv == null || recv.Pure), t: m.ReturnType);
    }

    // A call on an already translated receiver (foreach's GetEnumerator/MoveNext/Current).
    string CallOn(IMethodSymbol m, Ex recv, ITypeSymbol recvType, List<Ex> args) =>
        CallCode(m, recv, recvType, false, args, null, false, out _);

    string CallCode(IMethodSymbol m, Ex recv, ITypeSymbol recvType, bool isThis, List<Ex> args, IPropertySymbol prop, bool nonVirtual, out bool pure)
    {
        pure = false;
        string argText = string.Join(", ", args.Select(a => a.C));
        if (!N.IsGame(m))
            return ExternalCall(m, recv, recvType, isThis, args, prop, out pure);
        if (m.IsGenericMethod && !nonVirtual && (m.IsVirtual || m.IsAbstract || m.IsOverride || m.ContainingType.TypeKind == TypeKind.Interface))
            throw new TranslateException("a call to a virtual generic method (C++ has no virtual templates)");
        tr.NeedMethod(m);
        string name = N.Method(m);
        string targs = m.IsGenericMethod ? "<" + string.Join(", ", m.TypeArguments.Select(N.Ref)) + ">" : "";
        string tname = m.IsGenericMethod ? "template " + name : name;
        if (m.IsStatic)
        {
            string cls = N.Type(m.ContainingType);
            return $"{cls}::{tname}{targs}({argText})";
        }
        bool isValue = recvType != null && !Names.IsRef(recvType) && recvType.TypeKind != TypeKind.TypeParameter;
        string r = isThis || isValue ? recv.C : $"::cs::nc({recv.C})";
        if (recvType != null && recvType.TypeKind == TypeKind.TypeParameter)
            return $"::cs::Deref({recv.C}).{tname}{targs}({argText})";
        if (nonVirtual)
        {
            // base.M(): the base class's method, not the override.
            string cls = N.Type(m.ContainingType);
            return $"{r}{(isValue ? "." : "->")}{cls}::{name}{targs}({argText})";
        }
        if (m.ContainingType.TypeKind == TypeKind.Interface && recvType != null && recvType.TypeKind != TypeKind.Interface)
            r = $"static_cast<{N.Ref(m.ContainingType)}>({r})";
        return $"{r}{(isValue ? "." : "->")}{tname}{targs}({argText})";
    }

    string ExternalCall(IMethodSymbol m, Ex recv, ITypeSymbol recvType, bool isThis, List<Ex> args, IPropertySymbol prop, out bool pure)
    {
        pure = false;
        string kind = m.MethodKind switch
        {
            MethodKind.PropertyGet => "get",
            MethodKind.PropertySet => "set",
            MethodKind.Constructor => "new",
            MethodKind.UserDefinedOperator or MethodKind.BuiltinOperator => "op",
            MethodKind.Conversion => "op",
            _ => "call",
        };
        ISymbol key = m;
        if (kind is "get" or "set" && m.AssociatedSymbol is IPropertySymbol ap) key = ap;
        var containing = m.ContainingType;
        // System.Object's members on any receiver (an object, an interface, or a value as its box answers).
        if (containing.SpecialType is SpecialType.System_Object or SpecialType.System_ValueType or SpecialType.System_Enum)
        {
            string argText = string.Join(", ", args.Select(a => a.C));
            if (m.Name is "ToString" or "Equals" or "GetHashCode" && !m.IsStatic && recv != null)
                return $"::cs::ObjectCall_{m.Name}({ObjectOrValue(recv, recvType)}{(argText.Length > 0 ? ", " + argText : "")})";
            if (m.Name == "GetType" && !m.IsStatic && recv != null)
                return recvType != null && !Names.IsRef(recvType) && recvType.TypeKind != TypeKind.TypeParameter
                    ? $"::cs::TypeOf<{N.Ref(recvType)}>()"
                    : $"::cs::GetTypeOf({ObjectOrValue(recv, recvType)})";
            if (m.Name == "ReferenceEquals") return $"::cs::RefEq({argText})";
            if (m.Name == "Equals" && m.IsStatic) return $"::cs::ObjectEquals({argText})";
            if (m.MethodKind == MethodKind.Constructor) return "::cs::New<::cs::Object>()";
        }
        var b = tr.Bind.Find(kind, key) ?? (kind == "op" ? tr.Bind.Find("call", key) : null);
        var typeArgs = Names.AllTypeArguments(containing);
        bool recvIsValue = recvType != null && !Names.IsRef(recvType) && recvType.TypeKind != TypeKind.TypeParameter;
        if (b != null)
        {
            tr.UseBinding(b);
            pure = b.Pure;
            string receiver = null;
            if (!m.IsStatic && recv != null)
            {
                receiver = isThis || recvIsValue || b.NullOk ? recv.C : $"::cs::nc({recv.C})";
                if (containing.TypeKind == TypeKind.Interface && recvType != null && recvType.TypeKind != TypeKind.Interface)
                    receiver = $"static_cast<{N.Ref(containing)}>({receiver})";
            }
            string t = N.Substitute(b.Template, receiver, typeArgs, m.TypeArguments);
            return FillArgs(t, args, kind == "set" ? args.Count - 1 : -1);
        }
        // By convention, if the containing type maps with `conv`.
        var tb = tr.Bind.FindType(containing);
        string member = m.MethodKind == MethodKind.Constructor ? ".ctor" : m.Name;
        if (tb != null && tb.Conv && (tb.Members == null || tb.Members.Contains(member)))
        {
            tr.UseBinding(tb);
            string cls = N.Type(containing);
            string argText = string.Join(", ", args.Select(a => a.C));
            string targs = m.IsGenericMethod ? "<" + string.Join(", ", m.TypeArguments.Select(N.Ref)) + ">" : "";
            string name = Names.Ident(m.Name);
            if (m.MethodKind == MethodKind.Constructor)
                return Names.IsRef(containing) ? $"::cs::New<{cls}>({argText})" : $"::cs::NewS<{cls}>({argText})";
            if (m.MethodKind is MethodKind.UserDefinedOperator or MethodKind.Conversion)
                return $"{cls}::{name}{targs}({argText})";
            if (containing.TypeKind == TypeKind.Interface) name = N.InterfaceMember(m);
            if (m.IsGenericMethod) name = "template " + name;
            if (m.IsStatic) return $"{cls}::{name}{targs}({argText})";
            if (recvType != null && recvType.TypeKind == TypeKind.TypeParameter)
                return $"::cs::Deref({recv.C}).{name}{targs}({argText})";
            string r = isThis || recvIsValue ? recv.C : $"::cs::nc({recv.C})";
            if (containing.TypeKind == TypeKind.Interface && recvType != null && recvType.TypeKind != TypeKind.Interface)
                r = $"static_cast<{N.Ref(containing)}>({r})";
            return $"{r}{(recvIsValue ? "." : "->")}{name}{targs}({argText})";
        }
        return tr.Gap(key, kind);
    }

    // A receiver for System.Object's methods: an object pointer, or a value (boxed semantics).
    string ObjectOrValue(Ex recv, ITypeSymbol t)
    {
        if (t == null || Names.IsRef(t)) return $"::cs::nc({ObjectOf(recv, t)})";
        if (recv.Wide) return $"static_cast<float>({recv.C})";
        return recv.C;
    }

    string ObjectOf(Ex e, ITypeSymbol t)
    {
        if (t != null && t.TypeKind == TypeKind.Interface) return $"::cs::AsObject({e.C})";
        if (t != null && !Names.IsRef(t) && t.TypeKind != TypeKind.TypeParameter) return $"::cs::BoxAny({(e.Wide ? $"static_cast<float>({e.C})" : e.C)})";
        if (t != null && t.TypeKind == TypeKind.TypeParameter) return $"::cs::BoxAny({e.C})";
        return e.C;
    }

    static string FillArgs(string template, List<Ex> args, int valueIndex)
    {
        var sb = new StringBuilder();
        for (int i = 0; i < template.Length; i++)
        {
            if (template[i] == '{')
            {
                int end = template.IndexOf('}', i);
                string tok = template.Substring(i + 1, end - i - 1);
                if (tok.Length > 0 && tok.All(char.IsDigit)) { sb.Append(args[int.Parse(tok)].C); i = end; continue; }
                if (tok == "value" && valueIndex >= 0) { sb.Append(args[valueIndex].C); i = end; continue; }
                if (tok == "args") { sb.Append(string.Join(", ", args.Select(a => a.C))); i = end; continue; }
            }
            sb.Append(template[i]);
        }
        return sb.ToString();
    }

    Ex ObjectCreation(IObjectCreationOperation oc)
    {
        var t = oc.Type;
        var ctor = oc.Constructor;
        if (oc.Initializer != null) throw new TranslateException("object initializer");
        if (t.TypeKind == TypeKind.Delegate) throw new TranslateException("delegate from delegate");
        if (ctor == null || t.IsValueType && ctor.Parameters.Length == 0 && ctor.IsImplicitlyDeclared)
        {
            if (t.IsValueType) return new Ex($"{N.Ref(t)}{{}}", pure: true, t: t);
            throw new TranslateException("object creation without constructor");
        }
        var args = Args(oc.Arguments.ToList(), out string pre);
        string argText = string.Join(", ", args.Select(a => a.C));
        string code;
        bool pure = false;
        if (!N.IsGame(t))
            code = ExternalCall(ctor, null, t, false, args, null, out pure);
        else
        {
            tr.NeedMethod(ctor);
            tr.Constructed((INamedTypeSymbol)t);
            string cls = N.Type(t);
            code = t.IsValueType ? $"::cs::NewS<{cls}>({argText})" : $"::cs::New<{cls}>({argText})";
        }
        return new Ex(Wrap(pre, code), pure && args.All(a => a.Pure), t: t);
    }

    Ex ArrayCreation(IArrayCreationOperation ac)
    {
        var at = (IArrayTypeSymbol)ac.Type;
        string elem = N.Ref(at.ElementType);
        if (ac.Initializer != null)
        {
            if (at.Rank != 1) throw new TranslateException("multi-dimensional array initializer");
            // A braced list is evaluated left to right.
            var items = ac.Initializer.ElementValues.Select(v => Coerce(Expr(v), at.ElementType)).ToList();
            return new Ex($"::cs::Array<{elem}>::Of({{{string.Join(", ", items)}}})", t: at);
        }
        var dims = Sequence(ac.DimensionSizes.Select(d => Expr(d)).ToList(), out string pre);
        string code = at.Rank == 1 ? $"::cs::Array<{elem}>::New({dims[0].C})" : at.Rank == 2 ? $"::cs::Array2<{elem}>::New({dims[0].C}, {dims[1].C})" : throw new TranslateException("array rank > 2");
        return new Ex(Wrap(pre, code), t: at);
    }

    Ex ArrayElement(IArrayElementReferenceOperation ae)
    {
        var parts = new List<Ex> { Expr(ae.ArrayReference) };
        parts.AddRange(ae.Indices.Select(i => Expr(i)));
        var all = Sequence(parts, out string pre);
        string code = $"::cs::nc({all[0].C})->at({string.Join(", ", all.Skip(1).Select(i => i.C))})";
        return new Ex(Wrap(pre, code), pure: false, t: ae.Type);
    }

    // An lvalue for ref/out arguments and assignment targets.
    Ex LValue(IOperation op)
    {
        switch (op)
        {
            case ILocalReferenceOperation or IParameterReferenceOperation or IFieldReferenceOperation or IArrayElementReferenceOperation:
                return Expr(op);
            case IInstanceReferenceOperation:
                return new Ex("(*this)", pure: true);
            case IEventReferenceOperation ev:
                return EventField(ev);
        }
        throw new TranslateException($"ref to {op.Kind}");
    }

    // ---- delegates and events --------------------------------------------------------------------------

    Ex DelegateCreation(IDelegateCreationOperation dc)
    {
        var dt = (INamedTypeSymbol)dc.Type;
        string dtype = N.Type(dt);
        if (dc.Target is not IMethodReferenceOperation mr) throw new TranslateException($"delegate from {dc.Target.Kind}");
        var m = mr.Method;
        if (!N.IsGame(m)) return new Ex(tr.Gap(m, "delegate"));
        tr.NeedMethod(m);
        var cls = N.Type(m.ContainingType);
        string name = N.Method(m);
        string targs = m.IsGenericMethod ? "<" + string.Join(", ", m.TypeArguments.Select(N.Ref)) + ">" : "";
        if (m.IsStatic) return new Ex($"{dtype}::template BindStatic<&{cls}::{name}{targs}>()");
        if (m.ContainingType.IsValueType) throw new TranslateException("delegate to a struct method");
        var recv = mr.Instance is IInstanceReferenceOperation ? new Ex("this", pure: true) : Expr(mr.Instance);
        return new Ex($"{dtype}::template Bind<{cls}, &{cls}::{name}{targs}>({recv.C})");
    }

    Ex EventAssign(IEventAssignmentOperation ea)
    {
        var er = ea.EventReference as IEventReferenceOperation ?? throw new TranslateException("event assignment target");
        var es = er.Event;
        var handler = Expr(ea.HandlerValue);
        if (!N.IsGame(es))
        {
            var b = tr.Bind.Find(ea.Adds ? "add" : "remove", es);
            if (b == null) return new Ex(tr.Gap(es, ea.Adds ? "add" : "remove"));
            tr.UseBinding(b);
            string recv = es.IsStatic ? null : Receiver(er.Instance, out _);
            return new Ex(FillArgs(N.Substitute(b.Template, recv, es.ContainingType.TypeArguments, null), new List<Ex> { handler }, -1));
        }
        tr.UseEvent(es);
        string acc = (ea.Adds ? "add_" : "remove_") + Names.Ident(es.Name);
        if (es.IsStatic) return new Ex($"{N.Type(es.ContainingType)}::{acc}({handler.C})");
        var r = er.Instance is IInstanceReferenceOperation ? "this" : $"::cs::nc({Expr(er.Instance).C})";
        if (!handler.Pure && er.Instance is not IInstanceReferenceOperation)
        {
            string t = Temp(er.Instance.Type);
            return new Ex($"({t} = {Expr(er.Instance).C}, ::cs::nc({t})->{acc}({handler.C}))");
        }
        return new Ex($"{r}->{acc}({handler.C})");
    }

    // ---- conversions -----------------------------------------------------------------------------------

    // e converted to the C# type `target` where Mono stores or passes it: a wide float is rounded.
    public string Coerce(Ex e, ITypeSymbol target)
    {
        if (e.Wide && target.SpecialType == SpecialType.System_Single) return $"static_cast<float>({e.C})";
        if (e.Wide && target.SpecialType == SpecialType.System_Double) return e.C;
        if (e.Wide && target.SpecialType == SpecialType.System_Object) return $"::cs::BoxAny(static_cast<float>({e.C}))";
        return e.C;
    }

    Ex Conversion(IConversionOperation cv)
    {
        var from = cv.Operand.Type;
        var to = cv.Type;
        var c = cv.GetConversion();
        if (cv.OperatorMethod != null)
        {
            // user-defined conversion: a call
            var m = cv.OperatorMethod;
            var arg = Expr(cv.Operand);
            var p = m.Parameters[0].Type;
            // the operand may first convert to the parameter type (int -> float for Vector3(int))
            Ex a = from != null && !SymbolEqualityComparer.Default.Equals(from, p) ? ConvertCode(arg, from, p, false) : arg;
            a = new Ex(Coerce(a, p), a.Pure);
            string code;
            bool pure = false;
            if (N.IsGame(m))
            {
                tr.NeedMethod(m);
                code = $"{N.Type(m.ContainingType)}::{N.Method(m)}({a.C})";
            }
            else code = ExternalCall(m, null, null, false, new List<Ex> { a }, null, out pure);
            var r = new Ex(code, pure && a.Pure, t: m.ReturnType);
            if (!SymbolEqualityComparer.Default.Equals(m.ReturnType, to)) r = ConvertCode(r, m.ReturnType, to, true);
            r.T = to;
            return r;
        }
        if (cv.Operand is ILiteralOperation { ConstantValue.Value: null } || cv.Operand.ConstantValue is { HasValue: true, Value: null })
            return new Ex(Default(to), isConst: true);
        var operand = Expr(cv.Operand);
        if (from == null) return operand;
        bool isExplicit = !cv.IsImplicit && !c.IsImplicit;
        if (cv.IsTryCast)
            return to.TypeKind == TypeKind.TypeParameter
                ? new Ex($"::cs::AsT<{N.Ref(to)}>({ObjectOrInterface(operand, from)})", operand.Pure)
                : new Ex($"::cs::As<{N.Type(to)}>({ObjectOrInterface(operand, from)})", operand.Pure);
        var res = ConvertCode(operand, from, to, isExplicit || c.IsExplicit);
        if (!ReferenceEquals(res, operand)) res.T = to;
        return res;
    }

    string ObjectOrInterface(Ex e, ITypeSymbol from) => from.TypeKind == TypeKind.Interface ? $"::cs::AsObject({e.C})" : e.C;

    // The C++ for a C# conversion between two types.
    Ex ConvertCode(Ex e, ITypeSymbol from, ITypeSymbol to, bool isExplicit)
    {
        if (SymbolEqualityComparer.Default.Equals(from, to))
        {
            // (float)x: conv.r4, Mono rounds even a float.
            if (isExplicit && to.SpecialType == SpecialType.System_Single && e.Wide) return new Ex($"static_cast<float>({e.C})", e.Pure);
            return e;
        }
        var fs = from.SpecialType;
        var ts = to.SpecialType;
        bool fromNum = IsNumeric(from), toNum = IsNumeric(to);
        // enums go through their underlying type
        var fromU = from.TypeKind == TypeKind.Enum ? ((INamedTypeSymbol)from).EnumUnderlyingType : from;
        var toU = to.TypeKind == TypeKind.Enum ? ((INamedTypeSymbol)to).EnumUnderlyingType : to;
        if (from.TypeKind == TypeKind.Enum || to.TypeKind == TypeKind.Enum)
        {
            if (IsNumeric(fromU) && IsNumeric(toU))
            {
                var inner = from.TypeKind == TypeKind.Enum ? new Ex($"static_cast<{N.Ref(fromU)}>({e.C})", e.Pure) : e;
                var conv = ConvertCode(inner, fromU, toU, true);
                return to.TypeKind == TypeKind.Enum ? new Ex($"static_cast<{N.Type(to)}>({conv.C})", e.Pure) : conv;
            }
        }
        if (fromNum && toNum)
        {
            string tt = N.Type(to);
            bool fromFloat = fs is SpecialType.System_Single or SpecialType.System_Double;
            if (fromFloat)
            {
                string d = e.Wide || fs == SpecialType.System_Double ? e.C : $"static_cast<double>({e.C})";
                switch (ts)
                {
                    case SpecialType.System_Single: return new Ex($"static_cast<float>({e.C})", e.Pure);
                    case SpecialType.System_Double: return new Ex(e.Wide ? e.C : $"static_cast<double>({e.C})", e.Pure);
                    case SpecialType.System_Int32: return new Ex($"::cs::conv_i4({d})", e.Pure);
                    case SpecialType.System_Int64: return new Ex($"::cs::conv_i8({d})", e.Pure);
                    case SpecialType.System_Int16: return new Ex($"::cs::conv_i2({d})", e.Pure);
                    case SpecialType.System_SByte: return new Ex($"::cs::conv_i1({d})", e.Pure);
                    case SpecialType.System_UInt16: return new Ex($"::cs::conv_u2({d})", e.Pure);
                    case SpecialType.System_Byte: return new Ex($"::cs::conv_u1({d})", e.Pure);
                    case SpecialType.System_UInt32: return new Ex($"::cs::conv_u4({d})", e.Pure);
                    case SpecialType.System_UInt64: return new Ex($"::cs::conv_u8({d})", e.Pure);
                    case SpecialType.System_Char: return new Ex($"::cs::conv_c2({d})", e.Pure);
                }
            }
            if (ts == SpecialType.System_Single && fs is SpecialType.System_Int64 or SpecialType.System_UInt64)
                return new Ex($"::cs::conv_r4({e.C})", e.Pure);
            return new Ex($"static_cast<{tt}>({e.C})", e.Pure);
        }
        if ((fs == SpecialType.System_Boolean || ts == SpecialType.System_Boolean) && !Names.IsRef(from) && !Names.IsRef(to))
            return new Ex($"static_cast<{N.Type(to)}>({e.C})", e.Pure);
        // boxing: value -> object / interface
        if (!Names.IsRef(from) && from.TypeKind != TypeKind.TypeParameter && Names.IsRef(to))
        {
            string v = e.Wide ? $"static_cast<float>({e.C})" : e.C;
            if (from.OriginalDefinition.SpecialType == SpecialType.System_Nullable_T) return new Ex($"::cs::BoxAny({v})");
            if (to.TypeKind == TypeKind.Interface) return new Ex($"::cs::Cast<{N.Type(to)}>(::cs::BoxAny({v}))");
            return new Ex($"::cs::BoxAny({v})");
        }
        if (from.TypeKind == TypeKind.TypeParameter && Names.IsRef(to))
        {
            if (to.TypeKind == TypeKind.Interface) return new Ex($"::cs::Cast<{N.Type(to)}>(::cs::BoxAny({e.C}))", e.Pure);
            if (to.SpecialType == SpecialType.System_Object) return new Ex($"::cs::BoxAny({e.C})", e.Pure);
            return new Ex($"::cs::Cast<{N.Type(to)}>(::cs::BoxAny({e.C}))", e.Pure);
        }
        // unboxing: object -> value
        if (Names.IsRef(from) && !Names.IsRef(to) && to.TypeKind != TypeKind.TypeParameter)
        {
            if (to.OriginalDefinition.SpecialType == SpecialType.System_Nullable_T) throw new TranslateException("unboxing to Nullable");
            return new Ex($"::cs::Unbox<{N.Ref(to)}>({ObjectOrInterface(e, from)})", e.Pure);
        }
        if (Names.IsRef(from) && to.TypeKind == TypeKind.TypeParameter)
            return new Ex($"::cs::UnboxAny<{N.Ref(to)}>({ObjectOrInterface(e, from)})", e.Pure);
        // nullable
        if (to.OriginalDefinition.SpecialType == SpecialType.System_Nullable_T)
        {
            var inner = ((INamedTypeSymbol)to).TypeArguments[0];
            if (from.OriginalDefinition.SpecialType == SpecialType.System_Nullable_T) throw new TranslateException("nullable to nullable conversion");
            var v = ConvertCode(e, from, inner, isExplicit);
            return new Ex($"{N.Type(to)}({Coerce(v, inner)})", e.Pure);
        }
        if (from.OriginalDefinition.SpecialType == SpecialType.System_Nullable_T)
        {
            // (T)nullable: .Value
            var inner = ((INamedTypeSymbol)from).TypeArguments[0];
            var v = new Ex($"({e.C}).get_Value()", e.Pure);
            return ConvertCode(v, inner, to, isExplicit);
        }
        // references
        if (Names.IsRef(from) && Names.IsRef(to))
        {
            var conv = tr.Comp.ClassifyConversion(from, to);
            if (to.SpecialType == SpecialType.System_Object)
                return new Ex(from.TypeKind == TypeKind.Interface ? $"::cs::AsObject({e.C})" : $"static_cast<::cs::Object*>({e.C})", e.Pure);
            if (conv.IsImplicit && conv.IsReference)
            {
                if (from.TypeKind == TypeKind.Interface && to.TypeKind != TypeKind.Interface) return new Ex($"::cs::Cast<{N.Type(to)}>(::cs::AsObject({e.C}))", e.Pure);
                if (from.TypeKind == TypeKind.Interface && to.TypeKind == TypeKind.Interface && !InterfaceInherits((INamedTypeSymbol)from, (INamedTypeSymbol)to))
                    return new Ex($"::cs::Cast<{N.Type(to)}>(::cs::AsObject({e.C}))", e.Pure);
                if (from.TypeKind == TypeKind.Array || to.TypeKind == TypeKind.Array)
                {
                    if (to is IArrayTypeSymbol) throw new TranslateException("array covariance");
                }
                if (from.TypeKind == TypeKind.Delegate && to.TypeKind == TypeKind.Delegate) return e;
                return new Ex($"static_cast<{N.Ref(to)}>({e.C})", e.Pure);
            }
            // explicit reference conversion: checked downcast
            if (from.TypeKind == TypeKind.Interface) return new Ex($"::cs::Cast<{N.Type(to)}>(::cs::AsObject({e.C}))", e.Pure);
            return new Ex($"::cs::Cast<{N.Type(to)}>({e.C})", e.Pure);
        }
        if (from.TypeKind == TypeKind.TypeParameter || to.TypeKind == TypeKind.TypeParameter)
            return new Ex($"static_cast<{N.Ref(to)}>({e.C})", e.Pure);
        // value type to itself through another name (identity of generic instantiations etc.)
        throw new TranslateException($"conversion {from} -> {to}");
    }

    static bool InterfaceInherits(INamedTypeSymbol from, INamedTypeSymbol to) =>
        from.AllInterfaces.Any(i => SymbolEqualityComparer.Default.Equals(i, to));

    static bool IsNumeric(ITypeSymbol t) => t.SpecialType is SpecialType.System_Byte or SpecialType.System_SByte or SpecialType.System_Int16
        or SpecialType.System_UInt16 or SpecialType.System_Int32 or SpecialType.System_UInt32 or SpecialType.System_Int64 or SpecialType.System_UInt64
        or SpecialType.System_Char or SpecialType.System_Single or SpecialType.System_Double;

    static bool IsFloat(ITypeSymbol t) => t?.SpecialType == SpecialType.System_Single;

    // ---- operators ------------------------------------------------------------------------------------

    static string Wid(Ex e) => e.Wide ? e.C : $"static_cast<double>({e.C})";

    Ex Binary(IBinaryOperation b)
    {
        var kind = b.OperatorKind;
        var lt = b.LeftOperand.Type;
        var rt = b.RightOperand.Type;
        if (b.IsChecked && IsNumeric(b.Type ?? lt) && !IsFloat(lt) && lt?.SpecialType != SpecialType.System_Double)
            throw new TranslateException("checked arithmetic");
        // && || ?: are sequenced in C++.
        if (kind is BinaryOperatorKind.ConditionalAnd or BinaryOperatorKind.ConditionalOr)
        {
            if (b.OperatorMethod != null) throw new TranslateException("user-defined && / ||");
            var l0 = Expr(b.LeftOperand);
            var r0 = Expr(b.RightOperand);
            return new Ex($"({l0.C} {(kind == BinaryOperatorKind.ConditionalAnd ? "&&" : "||")} {r0.C})", l0.Pure && r0.Pure);
        }
        if (b.OperatorMethod != null && b.Type?.SpecialType != SpecialType.System_String && !(lt?.TypeKind == TypeKind.Delegate))
        {
            var m = b.OperatorMethod;
            var ops0 = new List<Ex> { Expr(b.LeftOperand), Expr(b.RightOperand) };
            var conv = new List<Ex>
            {
                new Ex(Coerce(ops0[0], m.Parameters[0].Type), ops0[0].Pure, false, ops0[0].Const, m.Parameters[0].Type),
                new Ex(Coerce(ops0[1], m.Parameters[1].Type), ops0[1].Pure, false, ops0[1].Const, m.Parameters[1].Type),
            };
            var seq = Sequence(conv, out string pre);
            string code;
            bool pure = false;
            if (N.IsGame(m))
            {
                tr.NeedMethod(m);
                code = $"{N.Type(m.ContainingType)}::{N.Method(m)}({seq[0].C}, {seq[1].C})";
            }
            else code = ExternalCall(m, null, null, false, seq, null, out pure);
            return new Ex(Wrap(pre, code), pure && seq.All(x => x.Pure), t: b.Type);
        }
        var l = Expr(b.LeftOperand);
        var r = Expr(b.RightOperand);
        var ops = Sequence(new List<Ex> { l, r }, out string prefix);
        l = ops[0];
        r = ops[1];
        Ex res = BinaryCore(b, kind, l, r, lt, rt);
        if (prefix.Length > 0) res = new Ex($"({prefix}{res.C})", false, res.Wide);
        res.T = b.Type;
        return res;
    }

    Ex BinaryCore(IBinaryOperation b, BinaryOperatorKind kind, Ex l, Ex r, ITypeSymbol lt, ITypeSymbol rt)
    {
        bool pure = l.Pure && r.Pure;
        var t = b.Type;
        // string concatenation
        if (t?.SpecialType == SpecialType.System_String && kind == BinaryOperatorKind.Add)
        {
            bool ls = lt?.SpecialType == SpecialType.System_String, rs = rt?.SpecialType == SpecialType.System_String;
            if (ls && rs) return new Ex($"::cs::String::Concat({l.C}, {r.C})");
            return new Ex($"::cs::String::Concat({AsObj(l, lt)}, {AsObj(r, rt)})");
        }
        // delegate combine/remove
        if (lt?.TypeKind == TypeKind.Delegate && kind is BinaryOperatorKind.Add or BinaryOperatorKind.Subtract)
        {
            string dt = N.Type(lt);
            return new Ex($"{dt}::{(kind == BinaryOperatorKind.Add ? "Combine" : "Remove")}({l.C}, {r.C})");
        }
        if (lt?.TypeKind == TypeKind.Delegate && kind is BinaryOperatorKind.Equals or BinaryOperatorKind.NotEquals && rt?.TypeKind == TypeKind.Delegate)
        {
            string eq = IsNullLit(b.LeftOperand) || IsNullLit(b.RightOperand) ? $"({l.C} == {r.C})" : $"::cs::DelegateEquals({l.C}, {r.C})";
            return new Ex(kind == BinaryOperatorKind.Equals ? eq : $"!{eq}", pure);
        }
        // reference equality
        if (kind is BinaryOperatorKind.Equals or BinaryOperatorKind.NotEquals && lt != null && rt != null
            && (Names.IsRef(lt) || lt.TypeKind == TypeKind.TypeParameter || IsNullLit(b.LeftOperand)) && (Names.IsRef(rt) || rt.TypeKind == TypeKind.TypeParameter || IsNullLit(b.RightOperand)))
        {
            string eq;
            if (IsNullLit(b.RightOperand)) eq = lt.TypeKind == TypeKind.TypeParameter ? $"::cs::IsNullT({l.C})" : $"({l.C} == nullptr)";
            else if (IsNullLit(b.LeftOperand)) eq = rt.TypeKind == TypeKind.TypeParameter ? $"::cs::IsNullT({r.C})" : $"({r.C} == nullptr)";
            else eq = $"::cs::RefEq({l.C}, {r.C})";
            return new Ex(kind == BinaryOperatorKind.Equals ? eq : $"!{eq}", pure);
        }
        var ot = lt; // operand type (both converted alike)
        if (ot == null) throw new TranslateException("binary operand without type");
        // enums
        if (ot.TypeKind == TypeKind.Enum || rt?.TypeKind == TypeKind.Enum)
        {
            var et = (INamedTypeSymbol)(ot.TypeKind == TypeKind.Enum ? ot : rt);
            string u = N.Type(et.EnumUnderlyingType);
            string lu = ot.TypeKind == TypeKind.Enum ? $"static_cast<{u}>({l.C})" : l.C;
            string ru = rt?.TypeKind == TypeKind.Enum ? $"static_cast<{u}>({r.C})" : r.C;
            string op = Op(kind);
            if (kind is BinaryOperatorKind.Equals or BinaryOperatorKind.NotEquals or BinaryOperatorKind.LessThan or BinaryOperatorKind.LessThanOrEqual
                or BinaryOperatorKind.GreaterThan or BinaryOperatorKind.GreaterThanOrEqual)
                return new Ex($"({lu} {op} {ru})", pure);
            string val = kind switch
            {
                BinaryOperatorKind.Add => $"::cs::step({lu}, 0) + {ru}",
                _ => $"{lu} {op} {ru}",
            };
            if (kind is BinaryOperatorKind.Add or BinaryOperatorKind.Subtract)
                val = $"static_cast<{u}>(static_cast<{u}>({lu}) {op} static_cast<{u}>({ru}))";
            return t?.TypeKind == TypeKind.Enum ? new Ex($"static_cast<{N.Type(t)}>({val})", pure) : new Ex($"static_cast<{N.Type(t)}>({val})", pure);
        }
        var s = ot.SpecialType;
        switch (s)
        {
            case SpecialType.System_Single:
            {
                // Mono: in double, rounded where stored
                string op = Op(kind);
                if (kind == BinaryOperatorKind.Remainder) return new Ex($"::std::fmod({Wid(l)}, {Wid(r)})", pure, wide: true);
                if (IsComparison(kind)) return new Ex($"({Wid(l)} {op} {Wid(r)})", pure);
                return new Ex($"({Wid(l)} {op} {Wid(r)})", pure, wide: true);
            }
            case SpecialType.System_Double:
            {
                string op = Op(kind);
                if (kind == BinaryOperatorKind.Remainder) return new Ex($"::std::fmod({l.C}, {r.C})", pure);
                return new Ex($"({l.C} {op} {r.C})", pure);
            }
            case SpecialType.System_Boolean:
            {
                string op = Op(kind);
                if (kind is BinaryOperatorKind.And or BinaryOperatorKind.Or or BinaryOperatorKind.ExclusiveOr)
                    return new Ex($"static_cast<bool>({l.C} {op} {r.C})", pure);
                return new Ex($"({l.C} {op} {r.C})", pure);
            }
            case SpecialType.System_Int32 or SpecialType.System_Int64 or SpecialType.System_UInt32 or SpecialType.System_UInt64:
            {
                string tt = N.Type(ot);
                switch (kind)
                {
                    case BinaryOperatorKind.Add: return new Ex($"::cs::add({l.C}, {r.C})", pure);
                    case BinaryOperatorKind.Subtract: return new Ex($"::cs::sub({l.C}, {r.C})", pure);
                    case BinaryOperatorKind.Multiply: return new Ex($"::cs::mul({l.C}, {r.C})", pure);
                    case BinaryOperatorKind.Divide: return new Ex($"::cs::div<{tt}>({l.C}, {r.C})", false);
                    case BinaryOperatorKind.Remainder: return new Ex($"::cs::rem<{tt}>({l.C}, {r.C})", false);
                    case BinaryOperatorKind.LeftShift: return new Ex($"::cs::shl({l.C}, {r.C})", pure);
                    case BinaryOperatorKind.RightShift: return new Ex($"::cs::shr({l.C}, {r.C})", pure);
                    case BinaryOperatorKind.And: return new Ex($"static_cast<{tt}>({l.C} & {r.C})", pure);
                    case BinaryOperatorKind.Or: return new Ex($"static_cast<{tt}>({l.C} | {r.C})", pure);
                    case BinaryOperatorKind.ExclusiveOr: return new Ex($"static_cast<{tt}>({l.C} ^ {r.C})", pure);
                    default: return new Ex($"({l.C} {Op(kind)} {r.C})", pure);
                }
            }
            case SpecialType.System_Char or SpecialType.System_Byte or SpecialType.System_Int16 or SpecialType.System_UInt16 or SpecialType.System_SByte:
                // only comparisons reach here (arithmetic converts to int first)
                return new Ex($"({l.C} {Op(kind)} {r.C})", pure);
        }
        if (ot.OriginalDefinition.SpecialType == SpecialType.System_Nullable_T) throw new TranslateException("lifted operator");
        throw new TranslateException($"binary {kind} on {ot}");
    }

    static bool IsNullLit(IOperation o) =>
        o.ConstantValue is { HasValue: true, Value: null } || o is IConversionOperation { Operand.ConstantValue: { HasValue: true, Value: null } };

    string AsObj(Ex e, ITypeSymbol t)
    {
        if (t == null) return e.C;
        if (t.SpecialType == SpecialType.System_String) return $"static_cast<::cs::Object*>({e.C})";
        return ObjectOf(e, t);
    }

    static bool IsComparison(BinaryOperatorKind k) => k is BinaryOperatorKind.Equals or BinaryOperatorKind.NotEquals or BinaryOperatorKind.LessThan
        or BinaryOperatorKind.LessThanOrEqual or BinaryOperatorKind.GreaterThan or BinaryOperatorKind.GreaterThanOrEqual;

    static string Op(BinaryOperatorKind k) => k switch
    {
        BinaryOperatorKind.Add => "+",
        BinaryOperatorKind.Subtract => "-",
        BinaryOperatorKind.Multiply => "*",
        BinaryOperatorKind.Divide => "/",
        BinaryOperatorKind.Remainder => "%",
        BinaryOperatorKind.LeftShift => "<<",
        BinaryOperatorKind.RightShift => ">>",
        BinaryOperatorKind.And => "&",
        BinaryOperatorKind.Or => "|",
        BinaryOperatorKind.ExclusiveOr => "^",
        BinaryOperatorKind.Equals => "==",
        BinaryOperatorKind.NotEquals => "!=",
        BinaryOperatorKind.LessThan => "<",
        BinaryOperatorKind.LessThanOrEqual => "<=",
        BinaryOperatorKind.GreaterThan => ">",
        BinaryOperatorKind.GreaterThanOrEqual => ">=",
        _ => throw new TranslateException($"operator {k}"),
    };

    Ex Unary(IUnaryOperation u)
    {
        if (u.OperatorMethod != null)
        {
            var m = u.OperatorMethod;
            var a = Expr(u.Operand);
            a = new Ex(Coerce(a, m.Parameters[0].Type), a.Pure);
            if (N.IsGame(m))
            {
                tr.NeedMethod(m);
                return new Ex($"{N.Type(m.ContainingType)}::{N.Method(m)}({a.C})", t: u.Type);
            }
            string code = ExternalCall(m, null, null, false, new List<Ex> { a }, null, out bool pure);
            return new Ex(code, pure && a.Pure, t: u.Type);
        }
        if (u.IsChecked && u.OperatorKind == UnaryOperatorKind.Minus && u.Type?.SpecialType is SpecialType.System_Int32 or SpecialType.System_Int64)
            throw new TranslateException("checked negation");
        var e = Expr(u.Operand);
        var t = u.Operand.Type;
        switch (u.OperatorKind)
        {
            case UnaryOperatorKind.Not: return new Ex($"!{Paren(e.C)}", e.Pure);
            case UnaryOperatorKind.Plus: return e;
            case UnaryOperatorKind.Minus:
                if (t.SpecialType is SpecialType.System_Int32 or SpecialType.System_Int64) return new Ex($"::cs::neg({e.C})", e.Pure);
                if (t.SpecialType == SpecialType.System_Single) return new Ex($"(-{Paren(e.C)})", e.Pure, e.Wide);
                return new Ex($"(-{Paren(e.C)})", e.Pure);
            case UnaryOperatorKind.BitwiseNegation:
                if (t.TypeKind == TypeKind.Enum)
                {
                    string un = N.Type(((INamedTypeSymbol)t).EnumUnderlyingType);
                    return new Ex($"static_cast<{N.Type(t)}>(~static_cast<{un}>({e.C}))", e.Pure);
                }
                return new Ex($"static_cast<{N.Type(t)}>(~{Paren(e.C)})", e.Pure);
        }
        throw new TranslateException($"unary {u.OperatorKind}");
    }

    static string Paren(string s) => s.StartsWith("(") && s.EndsWith(")") && Balanced(s) ? s : $"({s})";
    static bool Balanced(string s)
    {
        int d = 0;
        for (int i = 0; i < s.Length; i++)
        {
            if (s[i] == '(') d++;
            else if (s[i] == ')') { d--; if (d == 0 && i != s.Length - 1) return false; }
        }
        return d == 0;
    }

    Ex Ternary(IConditionalOperation c)
    {
        var cond = Expr(c.Condition);
        var a = Expr(c.WhenTrue);
        var b = Expr(c.WhenFalse);
        var t = c.Type;
        bool wide = (a.Wide || b.Wide) && t.SpecialType == SpecialType.System_Single;
        string ac = wide ? Wid(a) : Coerce(a, t);
        string bc = wide ? Wid(b) : Coerce(b, t);
        if (Names.IsRef(t))
        {
            ac = $"static_cast<{N.Ref(t)}>({ac})";
            bc = $"static_cast<{N.Ref(t)}>({bc})";
        }
        return new Ex($"({cond.C} ? {ac} : {bc})", cond.Pure && a.Pure && b.Pure, wide, t: t);
    }

    Ex Coalesce(ICoalesceOperation co)
    {
        var t = co.Type;
        var v = Expr(co.Value);
        var w = Expr(co.WhenNull);
        string tmp = Temp(co.Value.Type);
        if (co.Value.Type.OriginalDefinition.SpecialType == SpecialType.System_Nullable_T)
            return new Ex($"({tmp} = {v.C}, {tmp}.hasValue ? {tmp}.value : {Coerce(w, t)})");
        string wc = Coerce(w, t);
        return new Ex($"({tmp} = {v.C}, {tmp} != nullptr ? static_cast<{N.Ref(t)}>({tmp}) : static_cast<{N.Ref(t)}>({wc}))", t: t);
    }

    // ---- assignments ---------------------------------------------------------------------------------

    string Assign(IOperation target, IOperation value, bool needValue)
    {
        var tt = target.Type;
        switch (target)
        {
            case IPropertyReferenceOperation pr:
            {
                var set = pr.Property.SetMethod ?? FindSetter(pr.Property) ?? throw new TranslateException($"property without setter {pr.Property}");
                var args = pr.Arguments.ToList();
                // setter(args..., value): the value is the last argument
                var call = SetterCall(set, pr, args, value, needValue);
                return call;
            }
            case IDiscardOperation:
                return $"(void)({Expr(value).C})";
            case IEventReferenceOperation ev:
            {
                var v = Expr(value);
                var lhs = EventField(ev);
                return $"{lhs.C} = {v.C}";
            }
        }
        var val = Expr(value);
        string valCode = Coerce(val, tt);
        // The target's parts first (receiver, array, index), then the value.
        switch (target)
        {
            case ILocalReferenceOperation or IParameterReferenceOperation:
                return $"{Expr(target).C} = {valCode}";
            case IFieldReferenceOperation f:
            {
                if (f.Field.IsStatic || f.Instance is IInstanceReferenceOperation || val.Pure)
                    return $"{FieldRef(f).C} = {valCode}";
                // receiver into a temporary, then the value
                var recv = Expr(f.Instance);
                if (recv.Pure && IsSimpleLocal(recv)) return $"{FieldRef(f).C} = {valCode}";
                bool isValue = !Names.IsRef(f.Instance.Type);
                if (isValue) return $"{FieldRef(f).C} = {valCode}";
                string t = Temp(f.Instance.Type);
                string name = N.IsGame(f.Field) ? N.Field(f.Field) : Names.Ident(f.Field.Name);
                if (N.IsGame(f.Field)) tr.UseField(f.Field);
                return $"({t} = {recv.C}, ::cs::nc({t})->{name} = {valCode})";
            }
            case IArrayElementReferenceOperation ae:
            {
                var parts = new List<Ex> { Expr(ae.ArrayReference) };
                parts.AddRange(ae.Indices.Select(i => Expr(i)));
                if (!val.Pure)
                {
                    // the array and the indices before the value (C++17 would evaluate the value first)
                    var pre = new StringBuilder();
                    for (int i = 0; i < parts.Count; i++)
                        if (!parts[i].Const)
                        {
                            string t = Temp(parts[i]);
                            pre.Append($"{t} = {parts[i].C}, ");
                            parts[i] = new Ex(t, pure: true, t: parts[i].T);
                        }
                    return $"({pre}::cs::nc({parts[0].C})->at({string.Join(", ", parts.Skip(1).Select(p => p.C))}) = {valCode})";
                }
                var seq = Sequence(parts, out string pre2);
                string lhs = $"::cs::nc({seq[0].C})->at({string.Join(", ", seq.Skip(1).Select(p => p.C))})";
                return pre2.Length > 0 ? $"({pre2}{lhs} = {valCode})" : $"{lhs} = {valCode}";
            }
            case IInstanceReferenceOperation:
                return $"(*this) = {valCode}";
        }
        throw new TranslateException($"assignment to {target.Kind}");
    }

    static IMethodSymbol FindSetter(IPropertySymbol p)
    {
        for (var o = p.OverriddenProperty; o != null; o = o.OverriddenProperty)
            if (o.SetMethod != null) return o.SetMethod;
        return null;
    }

    string SetterCall(IMethodSymbol set, IPropertyReferenceOperation pr, List<IArgumentOperation> indexArgs, IOperation value, bool needValue)
    {
        var val = Expr(value);
        var valType = set.Parameters[^1].Type;
        string valCode = Coerce(val, valType);
        bool isThis = pr.Instance is IInstanceReferenceOperation;
        Ex recv = pr.Instance == null || set.IsStatic ? null : isThis ? new Ex(isStruct ? "(*this)" : "this", pure: true) : Expr(pr.Instance);
        var parts = new List<Ex>();
        if (recv != null) parts.Add(recv);
        foreach (var a in indexArgs)
        {
            var av = Expr(a.Value);
            parts.Add(new Ex(Coerce(av, a.Parameter.Type), av.Pure, false, av.Const, a.Parameter.Type));
        }
        var valEx = new Ex(valCode, val.Pure, false, val.Const, valType);
        parts.Add(valEx);
        var seq = Sequence(parts, out string pre);
        Ex r = recv != null ? seq[0] : null;
        var args = seq.Skip(recv != null ? 1 : 0).ToList();
        string code = CallCode(set, r, pr.Instance?.Type, isThis, args, pr.Property, false, out _);
        if (needValue)
        {
            string t = Temp(valType);
            // value semantics: the assigned value
            args[^1] = new Ex(t, pure: true);
            code = CallCode(set, r, pr.Instance?.Type, isThis, args, pr.Property, false, out _);
            return $"({pre}{t} = {seq[^1].C}, {code}, {t})";
        }
        return pre.Length > 0 ? $"({pre}{code})" : code;
    }

    string CompoundAssign(ICompoundAssignmentOperation ca, bool needValue)
    {
        var target = ca.Target;
        var tt = target.Type;
        // the target as a stable lvalue: receiver and index in temporaries when they aren't simple
        if (target is IPropertyReferenceOperation pr)
        {
            var get = pr.Property.GetMethod ?? throw new TranslateException("compound on a setter-only property");
            var set = pr.Property.SetMethod ?? FindSetter(pr.Property) ?? throw new TranslateException("compound on a getter-only property");
            bool isThis = pr.Instance is IInstanceReferenceOperation;
            Ex recv = set.IsStatic ? null : isThis ? new Ex(isStruct ? "(*this)" : "this", pure: true) : Expr(pr.Instance);
            var pre = new StringBuilder();
            if (recv != null && !recv.Pure || recv != null && !isThis && !IsSimpleLocal(recv))
            {
                string t = Temp(pr.Instance.Type);
                pre.Append($"{t} = {recv.C}, ");
                recv = new Ex(t, pure: true);
            }
            var idx = new List<Ex>();
            foreach (var a in pr.Arguments)
            {
                var v = Expr(a.Value);
                if (!v.Const)
                {
                    string t = Temp(a.Value.Type);
                    pre.Append($"{t} = {v.C}, ");
                    v = new Ex(t, pure: true);
                }
                idx.Add(v);
            }
            string old = Temp(tt);
            pre.Append($"{old} = {CallCode(get, recv, pr.Instance?.Type, isThis, idx, pr.Property, false, out _)}, ");
            var newVal = CompoundValue(ca, new Ex(old, pure: true));
            string nv = Temp(tt);
            pre.Append($"{nv} = {Coerce(newVal, tt)}, ");
            var args = new List<Ex>(idx) { new Ex(nv, pure: true) };
            string call = CallCode(set, recv, pr.Instance?.Type, isThis, args, pr.Property, false, out _);
            return needValue ? $"({pre}{call}, {nv})" : $"({pre}{call})";
        }
        string lv = StableLValue(target, out string prefix);
        var rhs = CompoundValue(ca, new Ex(lv, pure: true));
        string assign = $"{lv} = {Coerce(rhs, tt)}";
        return prefix.Length > 0 ? $"({prefix}{assign})" : (needValue ? $"({assign})" : assign);
    }

    // old op value (with the operation's conversions)
    Ex CompoundValue(ICompoundAssignmentOperation ca, Ex old)
    {
        var tt = ca.Target.Type;
        // Read the old value before evaluating the right side (C# order).
        var rhsOp = ca.Value;
        var rhs = Expr(rhsOp);
        Ex oldRead = old;
        if (!rhs.Pure)
        {
            string t = Temp(tt);
            // evaluate: t = old, then rhs; build with sequence order through a comma
            string tmpOld = t;
            var lhsEx2 = new Ex(tmpOld, pure: true);
            var core2 = CompoundCore(ca, lhsEx2, rhs);
            return new Ex($"({tmpOld} = {old.C}, {core2.C})", false, core2.Wide);
        }
        return CompoundCore(ca, oldRead, rhs);
    }

    Ex CompoundCore(ICompoundAssignmentOperation ca, Ex lhs, Ex rhs)
    {
        var tt = ca.Target.Type;
        var opType = ca.OperatorMethod?.Parameters[0].Type ?? (ca.InConversion.Exists && !ca.InConversion.IsIdentity ? null : tt);
        if (ca.OperatorMethod != null)
        {
            var m = ca.OperatorMethod;
            if (tt.SpecialType == SpecialType.System_String)
                return new Ex($"::cs::String::Concat({AsObj(lhs, tt)}, {AsObj(rhs, ca.Value.Type)})");
            if (tt.TypeKind == TypeKind.Delegate)
                return new Ex($"{N.Type(tt)}::{(ca.OperatorKind == BinaryOperatorKind.Add ? "Combine" : "Remove")}({lhs.C}, {rhs.C})");
            string code = N.IsGame(m)
                ? $"{N.Type(m.ContainingType)}::{N.Method(m)}({lhs.C}, {Coerce(rhs, m.Parameters[1].Type)})"
                : ExternalCall(m, null, null, false, new List<Ex> { lhs, new Ex(Coerce(rhs, m.Parameters[1].Type)) }, null, out _);
            if (N.IsGame(m)) tr.NeedMethod(m);
            return new Ex(code);
        }
        if (tt.SpecialType == SpecialType.System_String)
            return new Ex($"::cs::String::Concat({AsObj(lhs, tt)}, {AsObj(rhs, ca.Value.Type)})");
        if (tt.TypeKind == TypeKind.Delegate)
            return new Ex($"{N.Type(tt)}::{(ca.OperatorKind == BinaryOperatorKind.Add ? "Combine" : "Remove")}({lhs.C}, {rhs.C})");
        // byte += int: the operation runs in the promoted type, then narrows
        ITypeSymbol work = ca.Value.Type;
        if (tt.TypeKind == TypeKind.Enum)
        {
            var et = (INamedTypeSymbol)tt;
            string u = N.Type(et.EnumUnderlyingType);
            string lu = $"static_cast<{u}>({lhs.C})";
            string ru = ca.Value.Type?.TypeKind == TypeKind.Enum ? $"static_cast<{u}>({rhs.C})" : rhs.C;
            return new Ex($"static_cast<{N.Type(tt)}>(static_cast<{u}>({lu} {Op(ca.OperatorKind)} {ru}))");
        }
        var lt = tt;
        if (IsNumeric(tt) && IsNumeric(work) && !SymbolEqualityComparer.Default.Equals(tt, work))
        {
            // promote the target to the operation's type, compute, convert back
            var promoted = BinaryType(tt, work);
            var lp = ConvertCode(lhs, tt, promoted, false);
            var rp = SymbolEqualityComparer.Default.Equals(work, promoted) ? rhs : ConvertCode(rhs, work, promoted, false);
            var res = CoreArith(ca.OperatorKind, lp, rp, promoted);
            return ConvertCode(res, promoted, tt, true);
        }
        return CoreArith(ca.OperatorKind, lhs, rhs, tt);
    }

    ITypeSymbol BinaryType(ITypeSymbol a, ITypeSymbol b)
    {
        int Rank(ITypeSymbol t) => t.SpecialType switch
        {
            SpecialType.System_Double => 9,
            SpecialType.System_Single => 8,
            SpecialType.System_UInt64 => 7,
            SpecialType.System_Int64 => 6,
            SpecialType.System_UInt32 => 5,
            _ => 4,
        };
        var r = Rank(a) >= Rank(b) ? a : b;
        if (Rank(r) == 4) return tr.Comp.GetSpecialType(SpecialType.System_Int32);
        if (r.SpecialType == SpecialType.System_UInt32 && (a.SpecialType is SpecialType.System_SByte or SpecialType.System_Int16 or SpecialType.System_Int32 || b.SpecialType is SpecialType.System_SByte or SpecialType.System_Int16 or SpecialType.System_Int32))
            return tr.Comp.GetSpecialType(SpecialType.System_Int64);
        return r;
    }

    Ex CoreArith(BinaryOperatorKind kind, Ex l, Ex r, ITypeSymbol t)
    {
        var s = t.SpecialType;
        if (s == SpecialType.System_Single)
        {
            if (kind == BinaryOperatorKind.Remainder) return new Ex($"::std::fmod({Wid(l)}, {Wid(r)})", false, true);
            return new Ex($"({Wid(l)} {Op(kind)} {Wid(r)})", false, true);
        }
        if (s == SpecialType.System_Double)
        {
            if (kind == BinaryOperatorKind.Remainder) return new Ex($"::std::fmod({l.C}, {r.C})");
            return new Ex($"({l.C} {Op(kind)} {r.C})");
        }
        if (s == SpecialType.System_Boolean)
            return new Ex($"static_cast<bool>({l.C} {Op(kind)} {r.C})");
        string tt = N.Type(t);
        if (s is SpecialType.System_Int32 or SpecialType.System_Int64 or SpecialType.System_UInt32 or SpecialType.System_UInt64)
        {
            return kind switch
            {
                BinaryOperatorKind.Add => new Ex($"::cs::add({l.C}, {r.C})"),
                BinaryOperatorKind.Subtract => new Ex($"::cs::sub({l.C}, {r.C})"),
                BinaryOperatorKind.Multiply => new Ex($"::cs::mul({l.C}, {r.C})"),
                BinaryOperatorKind.Divide => new Ex($"::cs::div<{tt}>({l.C}, {r.C})"),
                BinaryOperatorKind.Remainder => new Ex($"::cs::rem<{tt}>({l.C}, {r.C})"),
                BinaryOperatorKind.LeftShift => new Ex($"::cs::shl({l.C}, {r.C})"),
                BinaryOperatorKind.RightShift => new Ex($"::cs::shr({l.C}, {r.C})"),
                _ => new Ex($"static_cast<{tt}>({l.C} {Op(kind)} {r.C})"),
            };
        }
        return new Ex($"static_cast<{tt}>({l.C} {Op(kind)} {r.C})");
    }

    // An lvalue that can be read and written without re-evaluating its parts.
    string StableLValue(IOperation target, out string prefix)
    {
        prefix = "";
        switch (target)
        {
            case ILocalReferenceOperation or IParameterReferenceOperation:
                return Expr(target).C;
            case IFieldReferenceOperation f:
            {
                if (f.Field.IsStatic || f.Instance is IInstanceReferenceOperation) return FieldRef(f).C;
                var recv = Expr(f.Instance);
                if (!Names.IsRef(f.Instance.Type))
                {
                    // a struct field of a struct lvalue: the lvalue itself
                    return FieldRef(f).C;
                }
                if (recv.Pure && IsSimpleLocal(recv)) return FieldRef(f).C;
                string t = Temp(f.Instance.Type);
                prefix = $"{t} = {recv.C}, ";
                string name = N.IsGame(f.Field) ? N.Field(f.Field) : Names.Ident(f.Field.Name);
                if (N.IsGame(f.Field)) tr.UseField(f.Field);
                return $"::cs::nc({t})->{name}";
            }
            case IArrayElementReferenceOperation ae:
            {
                var arr = Expr(ae.ArrayReference);
                string ta = Temp(ae.ArrayReference.Type);
                var sbp = new StringBuilder($"{ta} = {arr.C}, ");
                var idx = new List<string>();
                foreach (var i in ae.Indices)
                {
                    var v = Expr(i);
                    if (v.Const) { idx.Add(v.C); continue; }
                    string ti = Temp(i.Type);
                    sbp.Append($"{ti} = {v.C}, ");
                    idx.Add(ti);
                }
                prefix = sbp.ToString();
                return $"::cs::nc({ta})->at({string.Join(", ", idx)})";
            }
            case IEventReferenceOperation ev:
                return EventField(ev).C;
        }
        throw new TranslateException($"compound assignment to {target.Kind}");
    }

    string IncDec(IIncrementOrDecrementOperation inc, bool needValue)
    {
        bool isInc = inc.Kind == OperationKind.Increment;
        bool post = inc.IsPostfix;
        var target = inc.Target;
        if (inc.OperatorMethod != null) throw new TranslateException("user-defined ++");
        if (target is IPropertyReferenceOperation pr)
        {
            var get = pr.Property.GetMethod;
            var set = pr.Property.SetMethod ?? FindSetter(pr.Property);
            if (get == null || set == null) throw new TranslateException("++ on a property without both accessors");
            bool isThis = pr.Instance is IInstanceReferenceOperation;
            Ex recv = set.IsStatic ? null : isThis ? new Ex(isStruct ? "(*this)" : "this", pure: true) : Expr(pr.Instance);
            var pre = new StringBuilder();
            if (recv != null && !isThis)
            {
                string t = Temp(pr.Instance.Type);
                pre.Append($"{t} = {recv.C}, ");
                recv = new Ex(t, pure: true);
            }
            var idx = new List<Ex>();
            foreach (var a in pr.Arguments)
            {
                var v = Expr(a.Value);
                string t = Temp(a.Value.Type);
                pre.Append($"{t} = {v.C}, ");
                idx.Add(new Ex(t, pure: true));
            }
            string old = Temp(pr.Type);
            string nw = Temp(pr.Type);
            pre.Append($"{old} = {CallCode(get, recv, pr.Instance?.Type, isThis, idx, pr.Property, false, out _)}, ");
            pre.Append($"{nw} = ::cs::step({old}, {(isInc ? 1 : -1)}), ");
            var args = new List<Ex>(idx) { new Ex(nw, pure: true) };
            string call = CallCode(set, recv, pr.Instance?.Type, isThis, args, pr.Property, false, out _);
            return $"({pre}{call}, {(post ? old : nw)})";
        }
        string lv = StableLValue(target, out string prefix);
        string fn = post ? (isInc ? "postinc" : "postdec") : (isInc ? "preinc" : "predec");
        string code = $"::cs::{fn}({lv})";
        return prefix.Length > 0 ? $"({prefix}{code})" : code;
    }
}
