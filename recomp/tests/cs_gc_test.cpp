// The collector of recomp/src/cs (docs/translator_plan.md, "Memory"): a precise mark-sweep at a
// frame boundary must keep everything the roots reach through the fields translated code has,
// and free the rest. Each check below holds its objects through one kind of field only (a
// reference, a struct enumerator of each collection a lowered `foreach` keeps in an iterator's
// state machine, the boxed enumerator of an interface `foreach`, a KeyValuePair, a Nullable, a
// delegate's target, a cs::Root, a registered root source) and counts what survives a Collect.
//
// Usage: cs_gc_test (no data). ML_GC_POISON=1 makes a missed reference crash instead of reading
// a freed block that happens to be intact.

#include <cstdio>
#include <string>

#include "cs/Runtime.h"

namespace {

// An object that counts its live instances.
class Probe : public cs::Object {
public:
    static int alive;
    Probe() { alive++; }
    ~Probe() override { alive--; }
    const char* cs_typename() const override { return "Probe"; }
    bool cs_traced() const override { return true; }
    void Touch() { touched++; }
    int touched = 0;
};
int Probe::alive = 0;

// A translated class's fields, as the generated cs_trace traces them (TraceField per field).
class Holder : public cs::Object {
public:
    const char* cs_typename() const override { return "Holder"; }
    bool cs_traced() const override { return true; }
    void cs_trace(cs::Tracer& t) override
    {
        cs::TraceField(t, ref);
        cs::TraceField(t, listEnum);
        cs::TraceField(t, dictEnum);
        cs::TraceField(t, valuesEnum);
        cs::TraceField(t, keysEnum);
        cs::TraceField(t, setEnum);
        cs::TraceField(t, boxed);
        cs::TraceField(t, pair);
        cs::TraceField(t, nullable);
        cs::TraceField(t, action);
    }

    Probe* ref = nullptr;
    cs::List<Probe*>::Enumerator listEnum;
    cs::Dictionary<int32_t, Probe*>::Enumerator dictEnum;
    cs::Dictionary<int32_t, Probe*>::ValueCollection::Enumerator valuesEnum;
    cs::Dictionary<Probe*, int32_t>::KeyCollection::Enumerator keysEnum;
    cs::HashSet<Probe*>::Enumerator setEnum;
    cs::IEnumerator_1<Probe*>* boxed = nullptr;
    cs::KeyValuePair<int32_t, Probe*> pair;
    cs::Nullable<cs::KeyValuePair<int32_t, Probe*>> nullable;
    cs::Delegate<void()>* action = nullptr;
};

int failures = 0;
void Check(bool ok, const std::string& what)
{
    std::printf("%-72s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) failures++;
}

cs::List<Probe*>* ListOf(int n)
{
    auto* l = cs::New<cs::List<Probe*>>();
    for (int i = 0; i < n; i++) l->Add(cs::gcnew<Probe>());
    return l;
}

// A registered root source's objects (what generated code registers for static fields).
Probe* gStatic = nullptr;
void TraceStatic(cs::Tracer& t) { t.Mark(gStatic); }

} // namespace

int main()
{
    cs::Heap heap;
    cs::SetCurrentHeap(&heap);
    cs::RegisterRoots(&TraceStatic);
    cs::Root<Holder> h(cs::gcnew<Holder>());

    // Garbage goes.
    for (int i = 0; i < 10; i++) cs::gcnew<Probe>();
    heap.Collect();
    Check(Probe::alive == 0, "unreferenced objects are freed");

    // A reference field; a registered root source; a cs::Root.
    h->ref = cs::gcnew<Probe>();
    gStatic = cs::gcnew<Probe>();
    {
        cs::Root<Probe> held(cs::gcnew<Probe>());
        heap.Collect();
        Check(Probe::alive == 3, "a field, a root source, a cs::Root keep their objects");
    }
    heap.Collect();
    Check(Probe::alive == 2, "a cs::Root gone: its object is freed");
    h->ref = nullptr;
    gStatic = nullptr;
    heap.Collect();
    Check(Probe::alive == 0, "references cleared: freed");

    // Each struct enumerator alone keeps its collection (and so the collection's elements).
    {
        cs::List<Probe*>* l = ListOf(3);
        h->listEnum = l->GetEnumerator();
        h->listEnum.MoveNext();
        heap.Collect();
        Check(Probe::alive == 3, "List<T>.Enumerator keeps its list");
        int n = 1;
        while (h->listEnum.MoveNext()) {
            h->listEnum.get_Current()->Touch();
            n++;
        }
        Check(n == 3, "  ... and enumerates it after the collection");
        h->listEnum = cs::List<Probe*>::Enumerator{};
        heap.Collect();
        Check(Probe::alive == 0, "  ... and lets it go when reset");
    }
    {
        auto* d = cs::New<cs::Dictionary<int32_t, Probe*>>();
        for (int i = 0; i < 3; i++) d->Add(i, cs::gcnew<Probe>());
        h->dictEnum = d->GetEnumerator();
        heap.Collect();
        Check(Probe::alive == 3, "Dictionary<K,V>.Enumerator keeps its dictionary");
        h->dictEnum = {};
        h->valuesEnum = d->get_Values()->GetEnumerator();
        heap.Collect();
        Check(Probe::alive == 3, "Dictionary<K,V>.ValueCollection.Enumerator keeps its dictionary");
        h->valuesEnum = {};
        heap.Collect();
        Check(Probe::alive == 0, "  ... and lets it go when reset");
    }
    {
        auto* d = cs::New<cs::Dictionary<Probe*, int32_t>>();
        for (int i = 0; i < 2; i++) d->Add(cs::gcnew<Probe>(), i);
        h->keysEnum = d->get_Keys()->GetEnumerator();
        heap.Collect();
        Check(Probe::alive == 2, "Dictionary<K,V>.KeyCollection.Enumerator keeps its dictionary");
        h->keysEnum = {};
    }
    {
        auto* s = cs::New<cs::HashSet<Probe*>>();
        for (int i = 0; i < 2; i++) s->Add(cs::gcnew<Probe>());
        h->setEnum = s->GetEnumerator();
        heap.Collect();
        Check(Probe::alive == 2, "HashSet<T>.Enumerator keeps its set");
        h->setEnum = {};
    }
    heap.Collect();
    Check(Probe::alive == 0, "enumerators reset: everything freed");
    {
        // foreach over an IEnumerable<T>: the boxed enumerator (an iterator's IEnumerator<T> field).
        cs::List<Probe*>* l = ListOf(2);
        h->boxed = static_cast<cs::IEnumerable_1<Probe*>*>(l)->IEnumerable_1_GetEnumerator();
        heap.Collect();
        Check(Probe::alive == 2, "a boxed List<T>.Enumerator (IEnumerator<T>) keeps its list");
        h->boxed = nullptr;
        heap.Collect();
    }
    {
        h->pair = cs::KeyValuePair<int32_t, Probe*>(1, cs::gcnew<Probe>());
        h->nullable = cs::Nullable<cs::KeyValuePair<int32_t, Probe*>>(cs::KeyValuePair<int32_t, Probe*>(2, cs::gcnew<Probe>()));
        auto* target = cs::gcnew<Probe>();
        h->action = cs::Delegate<void()>::Bind<Probe, &Probe::Touch>(target);
        heap.Collect();
        Check(Probe::alive == 3, "a KeyValuePair, a Nullable, a delegate's target are kept");
        h->action->Invoke();
        Check(target->touched == 1, "  ... and the delegate still calls its target");
        h->pair = {};
        h->nullable = {};
        h->action = nullptr;
    }
    heap.Collect();
    Check(Probe::alive == 0, "all cleared: everything freed");

    cs::SetCurrentHeap(nullptr);
    std::printf("\n%s\n", failures ? "FAILURES" : "ALL PASS");
    return failures ? 1 : 0;
}
