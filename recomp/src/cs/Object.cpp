#include "cs/Object.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <map>
#include <string>

#include "cs/Dictionary.h"
#include "cs/String.h"
#include "cs/Type.h"

namespace cs {

namespace {
Heap gDefaultHeap;
Heap* gCurrent = &gDefaultHeap;

std::vector<RootSource>& Sources()
{
    static std::vector<RootSource> s;
    return s;
}
RootBase* gRoots = nullptr;
uint32_t gNextHash = 0x2545F491u;
} // namespace

Heap& CurrentHeap() { return *gCurrent; }

void SetCurrentHeap(Heap* heap) { gCurrent = heap ? heap : &gDefaultHeap; }

StaticScope::StaticScope() : saved_(gCurrent) { gCurrent = &gDefaultHeap; }
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

Tracer::Tracer() : epoch_(++gEpoch == 0 ? ++gEpoch : gEpoch) {}

void Heap::Clear()
{
    Object* o = head_;
    head_ = nullptr;
    count_ = 0;
    if (this == &gDefaultHeap) {
        while (o) {
            Object* next = o->gc_next_;
            delete o;
            o = next;
        }
        return;
    }
    // Keep what the roots reach (on the program's heap); free the rest.
    Tracer t;
    TraceRoots(t);
    t.Drain();
    std::vector<Object*> dead;
    while (o) {
        Object* next = o->gc_next_;
        if (o->gc_mark_ == t.Epoch()) {
            o->gc_next_ = gDefaultHeap.head_;
            gDefaultHeap.head_ = o;
            gDefaultHeap.count_++;
        } else {
            dead.push_back(o);
        }
        o = next;
    }
    for (Object* d : dead) delete d;
}

size_t Heap::Collect()
{
    Tracer t;
    TraceRoots(t);
    // Objects without a generated cs_trace (the hand port's) can't say what they reference:
    // they are kept, and so is everything they might reach.
    for (Object* o = head_; o; o = o->gc_next_)
        if (!o->cs_traced()) t.Mark(o);
    t.Drain();
    size_t freed = 0;
    Object** link = &head_;
    while (Object* o = *link) {
        if (o->gc_mark_ == t.Epoch()) {
            link = &o->gc_next_;
        } else {
            *link = o->gc_next_;
            delete o;
            count_--;
            freed++;
        }
    }
    return freed;
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

void Throw(Exception* e)
{
    if (!e) e = gcnew<NullReferenceException>();
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
