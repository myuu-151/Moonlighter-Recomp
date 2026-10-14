#include "cs/Object.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <map>
#include <string>

#if defined(_WIN32)
#include <malloc.h> // _msize (ML_GC_POISON)
#endif

#include "cs/Dictionary.h"
#include "cs/String.h"
#include "cs/Type.h"

#if defined(_WIN32) && defined(ML_CS_STACK_DUMP)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace cs {

namespace detail {
CastSlot gCastCache[kCastSlots];
} // namespace detail

void Tracer::Push(Object* o)
{
    o->gc_mark_ = epoch_;
    stack_.push_back(o);
    if (parents_) parents_->push_back({o, current_});
}

void Tracer::MarkRefs(const void* obj, const uint16_t* offsets, int32_t count)
{
    const char* base = static_cast<const char*>(obj);
    for (int32_t i = 0; i < count; i++) {
        Object* o = *reinterpret_cast<Object* const*>(base + offsets[i]);
        if (o && o->gc_mark_ < epoch_) {
            o->gc_mark_ = epoch_;
            stack_.push_back(o);
            if (parents_) parents_->push_back({o, current_});
        }
    }
}

namespace {
// The program's own heap: never destroyed (at exit, other statics may still point into it).
Heap& DefaultHeap()
{
    static Heap* h = new Heap();
    return *h;
}
Heap* gCurrent = nullptr;

std::vector<RootSource>& Sources()
{
    static auto* s = new std::vector<RootSource>(); // never destroyed (used until exit)
    return *s;
}
RootBase* gRoots = nullptr;
uint32_t gNextHash = 0x2545F491u;
} // namespace

Heap& CurrentHeap() { return gCurrent ? *gCurrent : DefaultHeap(); }

void SetCurrentHeap(Heap* heap) { gCurrent = heap ? heap : &DefaultHeap(); }

StaticScope::StaticScope() : saved_(gCurrent) { gCurrent = &DefaultHeap(); }
StaticScope::~StaticScope() { gCurrent = saved_; }

// ---- Object ------------------------------------------------------------------------------------

String* Object::ToString() { return String::FromAscii(cs_typename()); }

int32_t Object::GetHashCode()
{
    if (gc_hash_ == 0) {
        // A per-object number (RuntimeHelpers.GetHashCode is not observed by the game).
        gNextHash = gNextHash * 1103515245u + 12345u;
        gc_hash_ = (gNextHash >> 1) | 1u;
    }
    return static_cast<int32_t>(gc_hash_ & 0x7FFFFFFFu);
}

Type* Object::GetType() { return Type::ForName(cs_typename()); }

Type* Type::ForName(const char* fullName)
{
    static std::map<std::string, Type*> types;
    auto it = types.find(fullName);
    if (it != types.end()) return it->second;
    Type* t = new Type(fullName); // permanent: never on a heap
    types.emplace(fullName, t);
    return t;
}

// ---- Heap ------------------------------------------------------------------------------------------

namespace {
uint32_t gEpoch = 0;
} // namespace

// (Epochs count up from 1; kFrozen, the largest, is never one.)
Tracer::Tracer() : epoch_(++gEpoch == 0 || gEpoch == Heap::kFrozen ? (gEpoch = 1) : gEpoch) {}

namespace {
// The frozen objects (Heap::Freeze), for ML_GC_FREEZE_CHECK.
std::vector<Object*>& Frozen()
{
    static std::vector<Object*>* v = new std::vector<Object*>();
    return *v;
}
} // namespace

size_t Heap::Freeze(Object* root)
{
    if (!root || root->gc_mark_ == kFrozen) return 0;
    Tracer t;
    t.Mark(root);
    std::vector<Object*> reached;
    while (!t.stack_.empty()) {
        Object* o = t.stack_.back();
        t.stack_.pop_back();
        reached.push_back(o);
        o->cs_trace(t);
    }
    for (Object* o : reached) o->gc_mark_ = kFrozen;
    Frozen().insert(Frozen().end(), reached.begin(), reached.end());
    return reached.size();
}

void Heap::AddPressure(size_t bytes)
{
    CurrentHeap().made_ += bytes;
}

void Heap::Clear()
{
    Object* o = head_;
    head_ = nullptr;
    count_ = 0;
    Heap& program = DefaultHeap();
    if (this == &program) {
        while (o) {
            Object* next = o->gc_next_;
            delete o;
            o = next;
        }
        return;
    }
    // Keep what the roots reach (on the program's heap) and what's frozen; free the rest.
    Tracer t;
    TraceRoots(t);
    t.Drain();
    std::vector<Object*> dead;
    while (o) {
        Object* next = o->gc_next_;
        if (o->gc_mark_ >= t.Epoch()) {
            o->gc_next_ = program.head_;
            program.head_ = o;
            program.count_++;
        } else {
            dead.push_back(o);
        }
        o = next;
    }
    for (Object* d : dead) delete d;
}

namespace {
// ML_GC_POISON=1 (a check, not for the game): what Collect frees is destroyed, overwritten with
// 0xDD and never handed back to the allocator, so a reference the roots missed reads garbage (a
// crash at its first virtual call, or a mismatch) instead of a block that happens to be intact.
void Free(Object* o)
{
    static const bool poison = std::getenv("ML_GC_POISON") != nullptr;
    if (!poison) {
        delete o;
        return;
    }
    void* start = dynamic_cast<void*>(o);
#if defined(_WIN32)
    const size_t size = _msize(start);
#else
    const size_t size = sizeof(Object);
#endif
    o->~Object();
    std::memset(start, 0xDD, size);
}
} // namespace

size_t Heap::Collect()
{
    Tracer t;
    TraceRoots(t);
    // Objects without a generated cs_trace (the hand port's) can't say what they reference:
    // they are kept (and so is what they reference only if something traced reaches it too:
    // hand-written code keeping a translated object holds it in a cs::Root).
    for (Object* o = head_; o; o = o->gc_next_)
        if (o->gc_mark_ != kFrozen && !o->cs_traced()) t.Mark(o);
    t.Drain();
    // ML_GC_FREEZE_CHECK=1: what frozen data references, marked through it: anything marked only
    // that way is an object the freeze would have lost (reported; kept).
    static const bool freezeCheck = std::getenv("ML_GC_FREEZE_CHECK") != nullptr;
    if (freezeCheck) {
        for (Object* f : Frozen()) f->cs_trace(t);
        size_t lost = 0;
        while (!t.stack_.empty()) {
            Object* o = t.stack_.back();
            t.stack_.pop_back();
            if (lost < 8) std::fprintf(stderr, "  lost: %s\n", o->cs_typename());
            lost++;
            o->cs_trace(t);
        }
        if (lost) {
            std::fprintf(stderr, "ML_GC_FREEZE_CHECK: frozen data references %zu objects nothing else reaches\n", lost);
            std::exit(3);
        }
    }
    made_ = 0;
    size_t freed = 0;
    Object** link = &head_;
    while (Object* o = *link) {
        if (o->gc_mark_ >= t.Epoch()) {
            link = &o->gc_next_;
        } else {
            *link = o->gc_next_;
            Free(o);
            count_--;
            freed++;
        }
    }
    return freed;
}

std::string Heap::WhyAlive(Object* target)
{
    std::vector<std::pair<Object*, Object*>> parents;
    Tracer t;
    t.parents_ = &parents;
    TraceRoots(t);
    t.Drain();
    std::string chain;
    for (Object* o = target; o;) {
        Object* parent = nullptr;
        bool found = false;
        for (const auto& [child, p] : parents)
            if (child == o) {
                parent = p;
                found = true;
                break;
            }
        if (!found) return "";
        chain = std::string(o->cs_typename()) + (chain.empty() ? "" : " -> " + chain);
        o = parent;
    }
    return chain;
}

// ---- roots -------------------------------------------------------------------------------------------

bool RegisterRoots(RootSource source)
{
    Sources().push_back(source);
    return true;
}

RootBase::RootBase()
{
    next_ = gRoots;
    if (gRoots) gRoots->prev_ = this;
    gRoots = this;
}

RootBase::~RootBase()
{
    if (prev_) prev_->next_ = next_;
    else gRoots = next_;
    if (next_) next_->prev_ = prev_;
}

void TraceRoots(Tracer& t)
{
    for (RootSource s : Sources()) s(t);
    for (RootBase* r = gRoots; r; r = r->next_) t.Mark(r->RootObject());
}

// ---- exceptions ------------------------------------------------------------------------------------

void Exception::cs_trace(Tracer& t) { t.Mark(message_); }

String* Exception::ToString()
{
    return String::Concat(String::FromAscii(cs_typename()), String::Concat(String::FromAscii(": "), get_Message()));
}

String* Exception::get_Message()
{
    if (message_) return message_;
    const char* n = TypeName();
    // The messages .NET gives exceptions created without one.
    if (!std::strcmp(n, "NullReferenceException")) return String::FromAscii("Object reference not set to an instance of an object");
    if (!std::strcmp(n, "IndexOutOfRangeException")) return String::FromAscii("Index was outside the bounds of the array.");
    if (!std::strcmp(n, "ArgumentOutOfRangeException")) return String::FromAscii("Specified argument was out of the range of valid values.");
    if (!std::strcmp(n, "KeyNotFoundException")) return String::FromAscii("The given key was not present in the dictionary.");
    if (!std::strcmp(n, "InvalidCastException")) return String::FromAscii("Specified cast is not valid.");
    if (!std::strcmp(n, "DivideByZeroException")) return String::FromAscii("Attempted to divide by zero.");
    return String::Concat(String::FromAscii("Exception of type '"), String::Concat(String::FromAscii(cs_typename()), String::FromAscii("' was thrown.")));
}

const char* Exception::TypeName() const
{
    const char* full = cs_typename();
    const char* dot = std::strrchr(full, '.');
    return dot ? dot + 1 : full;
}

const char* Thrown::what() const noexcept { return ex ? ex->cs_typename() : "System.NullReferenceException"; }

namespace {
// ML_CS_STACK=1 (a build with ML_CS_STACK_DUMP and PDBs): the C++ stack at every C# exception thrown.
void DumpStack([[maybe_unused]] const char* type)
{
#if defined(_WIN32) && defined(ML_CS_STACK_DUMP)
    static const bool on = std::getenv("ML_CS_STACK") != nullptr;
    if (!on) return;
    HANDLE proc = GetCurrentProcess();
    static bool init = false;
    if (!init) {
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        SymInitialize(proc, nullptr, TRUE);
        init = true;
    }
    void* frames[48];
    const USHORT n = CaptureStackBackTrace(1, 48, frames, nullptr);
    std::fprintf(stderr, "cs: throw %s\n", type);
    alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 512];
    for (USHORT i = 0; i < n; i++) {
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 511;
        DWORD64 disp = 0;
        DWORD ldisp = 0;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof line;
        const DWORD64 a = reinterpret_cast<DWORD64>(frames[i]);
        const bool hasSym = SymFromAddr(proc, a, &disp, sym);
        const bool hasLine = SymGetLineFromAddr64(proc, a, &ldisp, &line);
        std::fprintf(stderr, "  %s  %s:%lu\n", hasSym ? sym->Name : "?", hasLine ? line.FileName : "?", hasLine ? line.LineNumber : 0ul);
    }
#endif
}
} // namespace

void Throw(Exception* e)
{
    if (!e) e = gcnew<NullReferenceException>();
    DumpStack(e->cs_typename());
    throw Thrown(e);
}

void ThrowNullReference() { Throw(gcnew<NullReferenceException>()); }
void ThrowIndexOutOfRange() { Throw(gcnew<IndexOutOfRangeException>()); }
void ThrowInvalidCast() { Throw(gcnew<InvalidCastException>()); }
void ThrowArgumentOutOfRange() { Throw(gcnew<ArgumentOutOfRangeException>()); }
void ThrowArgument(const char* message) { Throw(gcnew<ArgumentException>(String::FromAscii(message))); }
void ThrowArgumentNull(const char* param)
{
    Throw(gcnew<ArgumentNullException>(String::Concat(String::FromAscii("Value cannot be null.\nParameter name: "), String::FromAscii(param))));
}
void ThrowInvalidOperation(const char* message) { Throw(gcnew<InvalidOperationException>(String::FromAscii(message))); }
void ThrowKeyNotFound() { Throw(gcnew<KeyNotFoundException>()); }

// ---- the translator's runtime helpers ----------------------------------------------------------------

void NotTranslated(const char* method)
{
    std::fprintf(stderr, "cs: reached %s, which tools/translate did not translate (recomp/gen/cpp/report.tsv)\n", method);
    std::fflush(stderr);
    Throw(gcnew<NotImplementedException>(String::FromAscii(method)));
}

// ---- HashHelpers -------------------------------------------------------------------------------------

namespace HashHelpers {
namespace {
const int32_t kPrimes[] = {3, 7, 11, 17, 23, 29, 37, 47, 59, 71, 89, 107, 131, 163, 197, 239, 293, 353, 431, 521, 631, 761, 919,
                           1103, 1327, 1597, 1931, 2333, 2801, 3371, 4049, 4861, 5839, 7013, 8419, 10103, 12143, 14591,
                           17519, 21023, 25229, 30293, 36353, 43627, 52361, 62851, 75431, 90523, 108631, 130363, 156437,
                           187751, 225307, 270371, 324449, 389357, 467237, 560689, 672827, 807403, 968897, 1162687, 1395263,
                           1674319, 2009191, 2411033, 2893249, 3471899, 4166287, 4999559, 5999471, 7199369};

bool IsPrime(int32_t candidate)
{
    if ((candidate & 1) != 0) {
        int32_t limit = static_cast<int32_t>(std::sqrt(static_cast<double>(candidate)));
        for (int32_t divisor = 3; divisor <= limit; divisor += 2)
            if ((candidate % divisor) == 0) return false;
        return true;
    }
    return candidate == 2;
}
} // namespace

int32_t GetPrime(int32_t min)
{
    if (min < 0) Throw<ArgumentException>();
    for (int32_t p : kPrimes)
        if (p >= min) return p;
    for (int32_t i = (min | 1); i < 2147483647; i += 2)
        if (IsPrime(i) && ((i - 1) % 101 != 0)) return i;
    return min;
}

int32_t ExpandPrime(int32_t oldSize)
{
    int32_t newSize = static_cast<int32_t>(2u * static_cast<uint32_t>(oldSize));
    if (static_cast<uint32_t>(newSize) > 0x7FEFFFFDu && 0x7FEFFFFD > oldSize) return 0x7FEFFFFD;
    return GetPrime(newSize);
}
} // namespace HashHelpers

} // namespace cs
