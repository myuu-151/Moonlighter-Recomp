#pragma once
// The .NET object model for C++: what translated game code (tools/translate, recomp/gen/cpp)
// and the hand port share.
//
// C# reference types are cs::Object subclasses allocated with cs::gcnew and passed as raw
// pointers (C# references). Memory (docs/translator_plan.md, "Memory"): every object is linked
// into the current cs::Heap; a heap is freed as a whole (Clear: a test run, a scene) or
// collected by a precise mark-sweep (Collect) at a frame boundary, when no C++ stack frame holds
// a reference: the roots are the static fields of translated classes (registered by the
// generated code), the runtime's interned strings, the Unity layer's live objects and the
// cs::Root handles hand-written code holds. Each translated class traces its reference fields
// in a generated cs_trace; classes that don't (the hand port's) are kept until Clear.
//
// C# exceptions are objects too (cs::Exception and subclasses, GC-allocated); they travel as a
// C++ exception cs::Thrown that carries the pointer (cs::Throw). Translated `catch (T e)` tests
// the object's type the way C# picks a catch clause.

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace cs {

class String;
class Tracer;
class Type;

class Object {
public:
    Object() = default;
    // Copies (the hand port copies some of its objects) never copy the collector header.
    Object(const Object&) {}
    Object& operator=(const Object&) { return *this; }
    virtual ~Object() = default;

    // System.Object's virtuals. Translated classes override what the C# overrides.
    virtual String* ToString();
    virtual bool Equals(Object* other) { return this == other; }
    virtual int32_t GetHashCode();
    // Type.FullName of the object's class ("CaveRoom", "DungeonGenerator+FloorGenerationSaveInfo").
    virtual const char* cs_typename() const { return "System.Object"; }
    static const char* cs_static_typename() { return "System.Object"; }
    // object.GetType(): the canonical Type of cs_typename().
    Type* GetType();
    // new object()
    void __ctor() {}

    // The reference fields, for the collector (generated per translated class).
    virtual void cs_trace(Tracer&) {}
    // False for classes without a generated cs_trace (the hand port): Collect keeps them.
    virtual bool cs_traced() const { return false; }

    // ---- collector header ----
    Object* gc_next_ = nullptr;
    uint32_t gc_hash_ = 0; // RuntimeHelpers.GetHashCode, assigned on first use
    uint32_t gc_mark_ = 0; // the epoch of the last trace that reached it
};

// Marks objects reachable from the roots (Heap::Collect, Heap::Clear). Each trace has a new
// epoch: an object is marked when its gc_mark_ is the tracer's epoch, so nothing is reset.
class Tracer {
public:
    Tracer();
    uint32_t Epoch() const { return epoch_; }
    // (Inline: the test; out of line: the rest, which every cs_trace would otherwise carry.)
    // Marked in an earlier epoch, or never: marked now. (A frozen object's mark, kFrozen, is
    // above every epoch: never marked, never traced.)
    void Mark(Object* o)
    {
        if (o && o->gc_mark_ < epoch_) Push(o);
    }
    void Push(Object* o);
    // The generated cs_trace's: the references at these byte offsets of `obj` (each a pointer to a
    // cs::Object subclass, or null).
    void MarkRefs(const void* obj, const uint16_t* offsets, int32_t count);
    void Drain()
    {
        while (!stack_.empty()) {
            Object* o = stack_.back();
            stack_.pop_back();
            current_ = o;
            o->cs_trace(*this);
        }
        current_ = nullptr;
    }
    // (A diagnosis: who marked what, Heap::WhyAlive's.)
    std::vector<std::pair<Object*, Object*>>* parents_ = nullptr;

private:
    friend class Heap;
    std::vector<Object*> stack_;
    Object* current_ = nullptr;
    uint32_t epoch_;
};

// Every object lives on a heap until the heap is cleared or collects it.
class Heap {
public:
    Heap() = default;
    Heap(const Heap&) = delete;
    Heap& operator=(const Heap&) = delete;
    ~Heap() { Clear(); }

    template <typename T, typename... Args>
    T* New(Args&&... args)
    {
        T* o = new T(std::forward<Args>(args)...);
        o->gc_next_ = head_;
        head_ = o;
        count_++;
        made_ += sizeof(T);
        return o;
    }

    // Frees every object the roots don't reach; what they reach (a static field's list, a
    // cached delegate) moves to the program's own heap, as it outlives this one in C#.
    // The program's own heap (the default) frees everything.
    void Clear();
    // Frees what the roots (static fields, interned strings, registered root sources, cs::Root
    // handles) don't reach. Only at a frame boundary: references held in C++ locals are not
    // seen. Returns the number of objects freed.
    size_t Collect();
    size_t Count() const { return count_; }

    // When to collect (docs/gamecube_translated.md, "The collector"): a game loop collects at a frame
    // end when CollectDue(), not at every one: when the objects made since the last collection
    // (their own sizes, and the strings' and arrays' contents: AddPressure) pass the threshold
    // (default 256 KB). A collection costs about the same whenever it runs; rarer is cheaper.
    bool CollectDue() const { return made_ >= threshold_; }
    void SetCollectThreshold(size_t bytes) { threshold_ = bytes; }
    size_t MadeSinceCollect() const { return made_; }
    static void AddPressure(size_t bytes);

    // Data that never changes again (the constants' tree once read, a stat table): what `root`
    // reaches is kept for good and no longer marked or swept, so collections cost what the
    // changing objects cost. Frozen data must not come to reference newer objects (they would be
    // reachable only through it, which isn't traced): ML_GC_FREEZE_CHECK=1 traces it anyway at
    // each collection and reports any object only it reaches. Returns the objects frozen.
    static size_t Freeze(Object* root);
    static constexpr uint32_t kFrozen = 0xFFFFFFFFu;
    // Each object on the heap, the newest first (diagnostics: MLGC's test builds count them by type).
    template <typename F>
    void ForEach(F&& f) const
    {
        for (Object* o = head_; o; o = o->gc_next_) f(o);
    }

    // A diagnosis (not for the game): the chain of references from a root to `o` ("" if no root
    // reaches it), each object by its type, the root first.
    static std::string WhyAlive(Object* o);

private:
    Object* head_ = nullptr;
    size_t count_ = 0;
    size_t made_ = 0;
    size_t threshold_ = 256 * 1024;
};

// The heap new objects go to; set by whoever owns the current object lifetime scope.
Heap& CurrentHeap();
void SetCurrentHeap(Heap* heap);

template <typename T, typename... Args>
T* gcnew(Args&&... args)
{
    return CurrentHeap().New<T>(std::forward<Args>(args)...);
}

// Static constructors allocate on the program's own heap (never cleared), whichever heap is
// current: what a static field holds outlives a test's or a scene's heap.
class StaticScope {
public:
    StaticScope();
    ~StaticScope();
    StaticScope(const StaticScope&) = delete;
    StaticScope& operator=(const StaticScope&) = delete;

private:
    Heap* saved_;
};

// ---- roots ---------------------------------------------------------------------------------

// A function that marks a group of roots (a class's static fields, a Unity subsystem's objects).
using RootSource = void (*)(Tracer&);
// Registers a root source for the life of the program (generated code does this from static
// initializers; order does not matter).
bool RegisterRoots(RootSource source);
void TraceRoots(Tracer& t);

// A reference held by hand-written code across collections.
class RootBase {
public:
    RootBase();
    RootBase(const RootBase&) = delete;
    RootBase& operator=(const RootBase&) = delete;
    ~RootBase();
    virtual Object* RootObject() const = 0;

private:
    friend void TraceRoots(Tracer& t);
    RootBase* prev_ = nullptr;
    RootBase* next_ = nullptr;
};

template <typename T>
class Root : public RootBase {
public:
    explicit Root(T* p = nullptr) : p_(p) {}
    Root& operator=(T* p)
    {
        p_ = p;
        return *this;
    }
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    operator T*() const { return p_; }
    Object* RootObject() const override;

private:
    T* p_;
};

// ---- references ----------------------------------------------------------------------------

// dynamic_cast<T*>(p), remembered: the result's offset from p depends only on p's dynamic type and
// which of its subobjects p is, and p's vtable pointer names both. So a cast is a lookup in a small
// table keyed by (p's vtable, T) after the first time; the same results as dynamic_cast's. (The
// GameCube's libsupc++ compares type names with strcmp in every failed cast: the translated
// code's Is/As/Cast and the facade's GetComponent were a tenth of its time.)
namespace detail {
struct CastSlot {
    const void* vptr;
    const void* to;
    std::ptrdiff_t delta;
};
constexpr std::ptrdiff_t kCastFails = static_cast<std::ptrdiff_t>(~(~static_cast<std::size_t>(0) >> 1));
constexpr std::size_t kCastSlots = 1024;
extern CastSlot gCastCache[kCastSlots];
} // namespace detail
template <typename T, typename U>
inline T* DynCast(U* p)
{
    static_assert(std::is_polymorphic<U>::value, "DynCast from a polymorphic type");
    if (!p) return nullptr;
    const void* vptr = *reinterpret_cast<const void* const*>(p);
    const void* to = &typeid(T);
    std::size_t h = ((reinterpret_cast<std::uintptr_t>(vptr) >> 3) ^ (reinterpret_cast<std::uintptr_t>(to) >> 4)) & (detail::kCastSlots - 1);
    detail::CastSlot& s = detail::gCastCache[h];
    if (s.vptr == vptr && s.to == to)
        return s.delta == detail::kCastFails ? nullptr : reinterpret_cast<T*>(reinterpret_cast<char*>(p) + s.delta);
    T* r = dynamic_cast<T*>(p);
    s.vptr = vptr;
    s.to = to;
    s.delta = r ? reinterpret_cast<char*>(r) - reinterpret_cast<char*>(p) : detail::kCastFails;
    return r;
}

// Any reference (class or interface pointer) as the object it points to. Interfaces don't
// derive from Object (docs/translator_plan.md), so they cross-cast.
template <typename T>
inline Object* AsObject(T* p)
{
    if constexpr (std::is_base_of<Object, T>::value) return static_cast<Object*>(p);
    else return DynCast<Object>(p);
}
inline Object* AsObject(std::nullptr_t) { return nullptr; }

template <typename T>
Object* Root<T>::RootObject() const
{
    return AsObject(p_);
}

// ---- generic code shared across reference types ----------------------------------------------
//
// .NET compiles one body of List<T> (Dictionary, HashSet, arrays, the collection interfaces) for
// every reference type T; so does this: cs::List<Foo*> *is* cs::shared::List<cs::Object*> when Foo
// is erased (cs::Erase), its elements held as cs::Object*, and what comes out of it is cast back to
// the C# type where the translation reads it (cs::Down: a static_cast, no work at run time; the
// translator writes one wherever a member returns the type parameter). One instantiation's code
// instead of one per element type: the GameCube's code size (tools/gcsize).
//
// Erased: the translated game's classes (game.h specializes ErasedRef for each) and the runtime's
// own generic classes and delegates below. Not erased: interfaces (a pointer into the object, not
// to it), value types, strings (their default comparison is the culture's, an object's is
// IComparable's), the facade's and the hand port's classes; each of those keeps its own instance.
template <typename T>
struct ErasedRef : std::false_type {};
template <typename T>
struct EraseT {
    using type = T;
};
template <typename T>
struct EraseT<T*> {
    using type = typename std::conditional<ErasedRef<T>::value, Object*, T*>::type;
};
template <typename T>
using Erase = typename EraseT<T>::type;

// A value of a shared generic's (cs::Object* for an erased reference) as the C# type it has. Of
// the same type already (a value type, an interface, a string...): the value itself, an lvalue
// staying one (an array's struct element called in place).
template <typename To, typename From>
inline decltype(auto) Down(From&& x)
{
    if constexpr (std::is_same<typename std::remove_cv<typename std::remove_reference<From>::type>::type, To>::value) {
        if constexpr (std::is_lvalue_reference<From>::value) return static_cast<From>(x);
        else return To(std::move(x));
    } else {
        return static_cast<To>(x);
    }
}

// The shared generics (their definitions: cs/Array.h, List.h, Dictionary.h, HashSet.h,
// Queue.h, Collections.h, Interfaces.h), and the names translated code uses.
namespace shared {
template <typename T>
class Array;
template <typename T>
class Array2;
template <typename T>
class List;
template <typename K, typename V>
class Dictionary;
template <typename T>
class HashSet;
template <typename T>
class Queue;
template <typename K, typename V>
struct KeyValuePair;
template <typename T>
struct IEnumerator_1;
template <typename T>
struct IEnumerable_1;
template <typename T>
struct ICollection_1;
template <typename T>
struct IList_1;
template <typename T>
struct IComparer_1;
template <typename T>
struct IEqualityComparer_1;
template <typename T>
struct IComparable_1;
template <typename T>
struct IEquatable_1;
} // namespace shared
template <typename T>
using Array = shared::Array<Erase<T>>;
template <typename T>
using Array2 = shared::Array2<Erase<T>>;
template <typename T>
using List = shared::List<Erase<T>>;
template <typename K, typename V>
using Dictionary = shared::Dictionary<Erase<K>, Erase<V>>;
template <typename T>
using HashSet = shared::HashSet<Erase<T>>;
template <typename T>
using Queue = shared::Queue<Erase<T>>;
template <typename K, typename V>
using KeyValuePair = shared::KeyValuePair<Erase<K>, Erase<V>>;
template <typename T>
using IEnumerator_1 = shared::IEnumerator_1<Erase<T>>;
template <typename T>
using IEnumerable_1 = shared::IEnumerable_1<Erase<T>>;
template <typename T>
using ICollection_1 = shared::ICollection_1<Erase<T>>;
template <typename T>
using IList_1 = shared::IList_1<Erase<T>>;
template <typename T>
using IComparer_1 = shared::IComparer_1<Erase<T>>;
template <typename T>
using IEqualityComparer_1 = shared::IEqualityComparer_1<Erase<T>>;
template <typename T>
using IComparable_1 = shared::IComparable_1<Erase<T>>;
template <typename T>
using IEquatable_1 = shared::IEquatable_1<Erase<T>>;

template <typename T>
struct ErasedRef<shared::Array<T>> : std::true_type {};
template <typename T>
struct ErasedRef<shared::Array2<T>> : std::true_type {};
template <typename T>
struct ErasedRef<shared::List<T>> : std::true_type {};
template <typename K, typename V>
struct ErasedRef<shared::Dictionary<K, V>> : std::true_type {};
template <typename T>
struct ErasedRef<shared::HashSet<T>> : std::true_type {};
template <typename T>
struct ErasedRef<shared::Queue<T>> : std::true_type {};

// Marks a field of any type: references are marked, values with references in them trace
// their fields (found by argument-dependent lookup: cs_trace_value(Tracer&, const S&)), and
// everything else is skipped.
template <typename T>
inline void TraceField(Tracer& t, T* p)
{
    t.Mark(AsObject(p));
}
template <typename T, typename std::enable_if<!std::is_pointer<T>::value, int>::type = 0>
inline void TraceField(Tracer& t, const T& v);

namespace detail {
template <typename T, typename = void>
struct HasValueTrace : std::false_type {};
template <typename T>
struct HasValueTrace<T, decltype(cs_trace_value(std::declval<Tracer&>(), std::declval<const T&>()), void())> : std::true_type {};
} // namespace detail

template <typename T, typename std::enable_if<!std::is_pointer<T>::value, int>::type>
inline void TraceField(Tracer& t, const T& v)
{
    if constexpr (detail::HasValueTrace<T>::value) cs_trace_value(t, v);
    else (void)t, (void)v;
}

// C# reference equality (object == object, ReferenceEquals) across class and interface pointers.
template <typename A, typename B>
inline bool RefEq(A* a, B* b)
{
    if constexpr (std::is_same<A, B>::value) return a == b;
    else return AsObject(a) == AsObject(b);
}
template <typename A>
inline bool RefEq(A* a, std::nullptr_t) { return a == nullptr; }
template <typename B>
inline bool RefEq(std::nullptr_t, B* b) { return b == nullptr; }
inline bool RefEq(std::nullptr_t, std::nullptr_t) { return true; }

// ---- exceptions ----------------------------------------------------------------------------

class Exception : public Object {
public:
    Exception() = default;
    explicit Exception(String* message) : message_(message) {}
    // new XException() / new XException(message) from translated code
    void __ctor() {}
    void __ctor(String* message) { message_ = message; }
    const char* cs_typename() const override { return "System.Exception"; }
    void cs_trace(Tracer& t) override;
    bool cs_traced() const override { return true; }
    String* ToString() override;
    // Exception.Message (the default message when none was given, as .NET has them).
    virtual String* get_Message();
    // The class name without namespace ("NullReferenceException"), as the oracle records it.
    const char* TypeName() const;

    String* message_ = nullptr;
};

#define CS_EXCEPTION(Name, Base, FullName)                                     \
    class Name : public Base {                                                 \
    public:                                                                    \
        Name() = default;                                                      \
        explicit Name(String* message) : Base(message) {}                      \
        const char* cs_typename() const override { return FullName; }          \
    };

CS_EXCEPTION(SystemException, Exception, "System.SystemException")
CS_EXCEPTION(NullReferenceException, SystemException, "System.NullReferenceException")
CS_EXCEPTION(ArgumentException, SystemException, "System.ArgumentException")
CS_EXCEPTION(ArgumentNullException, ArgumentException, "System.ArgumentNullException")
CS_EXCEPTION(ArgumentOutOfRangeException, ArgumentException, "System.ArgumentOutOfRangeException")
CS_EXCEPTION(IndexOutOfRangeException, SystemException, "System.IndexOutOfRangeException")
CS_EXCEPTION(InvalidOperationException, SystemException, "System.InvalidOperationException")
CS_EXCEPTION(InvalidCastException, SystemException, "System.InvalidCastException")
CS_EXCEPTION(NotSupportedException, SystemException, "System.NotSupportedException")
CS_EXCEPTION(NotImplementedException, SystemException, "System.NotImplementedException")
CS_EXCEPTION(ArithmeticException, SystemException, "System.ArithmeticException")
CS_EXCEPTION(DivideByZeroException, ArithmeticException, "System.DivideByZeroException")
CS_EXCEPTION(OverflowException, ArithmeticException, "System.OverflowException")
CS_EXCEPTION(FormatException, SystemException, "System.FormatException")
CS_EXCEPTION(KeyNotFoundException, SystemException, "System.Collections.Generic.KeyNotFoundException")
CS_EXCEPTION(ObjectDisposedException, InvalidOperationException, "System.ObjectDisposedException")
CS_EXCEPTION(ArrayTypeMismatchException, SystemException, "System.ArrayTypeMismatchException")
CS_EXCEPTION(RankException, SystemException, "System.RankException")
CS_EXCEPTION(StackOverflowException, SystemException, "System.StackOverflowException")
CS_EXCEPTION(OutOfMemoryException, SystemException, "System.OutOfMemoryException")

// What C++ throws for a C# exception.
struct Thrown : std::exception {
    explicit Thrown(Exception* e) : ex(e) {}
    const char* what() const noexcept override;
    const char* TypeName() const { return ex ? ex->TypeName() : "NullReferenceException"; }
    Exception* ex;
};

// throw e; (a null e throws a NullReferenceException, as `throw null` does)
[[noreturn]] void Throw(Exception* e);
template <typename E>
[[noreturn]] inline void Throw()
{
    Throw(gcnew<E>());
}
[[noreturn]] void ThrowNullReference();
[[noreturn]] void ThrowIndexOutOfRange();
[[noreturn]] void ThrowInvalidCast();
[[noreturn]] void ThrowArgumentOutOfRange();
[[noreturn]] void ThrowArgument(const char* message);
[[noreturn]] void ThrowArgumentNull(const char* param);
[[noreturn]] void ThrowInvalidOperation(const char* message);
[[noreturn]] void ThrowKeyNotFound();

// p, or a NullReferenceException where C# would dereference null.
template <typename T>
inline T* nc(T* p)
{
    if (!p) ThrowNullReference();
    return p;
}
template <typename T>
inline T* NullCheck(T* p)
{
    return nc(p);
}

// ---- casts ---------------------------------------------------------------------------------

// `x is T` for a reference type T.
template <typename T, typename U>
inline bool Is(U* p)
{
    if (!p) return false;
    if constexpr (std::is_base_of<T, U>::value) return true;
    else return DynCast<T>(p) != nullptr;
}

// `x as T`
template <typename T, typename U>
inline T* As(U* p)
{
    if constexpr (std::is_base_of<T, U>::value) return static_cast<T*>(p);
    else return DynCast<T>(p);
}

// `(T)x` for references: InvalidCastException unless null or a T.
template <typename T, typename U>
inline T* Cast(U* p)
{
    if constexpr (std::is_base_of<T, U>::value) return static_cast<T*>(p);
    else {
        if (!p) return nullptr;
        T* r = DynCast<T>(p);
        if (!r) ThrowInvalidCast();
        return r;
    }
}

// The finally of a try/finally: runs when the block it is declared in is left, whichever way.
template <typename F>
class Finally {
public:
    explicit Finally(F f) : f_(std::move(f)) {}
    Finally(const Finally&) = delete;
    Finally& operator=(const Finally&) = delete;
    ~Finally() noexcept(false) { f_(); }

private:
    F f_;
};
template <typename F>
Finally<F> MakeFinally(F f)
{
    return Finally<F>(std::move(f));
}

} // namespace cs
