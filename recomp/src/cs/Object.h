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
#include <type_traits>
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
    void Mark(Object* o)
    {
        if (o && o->gc_mark_ != epoch_) {
            o->gc_mark_ = epoch_;
            stack_.push_back(o);
        }
    }
    void Drain()
    {
        while (!stack_.empty()) {
            Object* o = stack_.back();
            stack_.pop_back();
            o->cs_trace(*this);
        }
    }

private:
    std::vector<Object*> stack_;
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
    // Each object on the heap, the newest first (diagnostics: MLGC's test builds count them by type).
    template <typename F>
    void ForEach(F&& f) const
    {
        for (Object* o = head_; o; o = o->gc_next_) f(o);
    }

private:
    Object* head_ = nullptr;
    size_t count_ = 0;
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

// Any reference (class or interface pointer) as the object it points to. Interfaces don't
// derive from Object (docs/translator_plan.md), so they cross-cast.
template <typename T>
inline Object* AsObject(T* p)
{
    if constexpr (std::is_base_of<Object, T>::value) return static_cast<Object*>(p);
    else return p ? dynamic_cast<Object*>(p) : nullptr;
}
inline Object* AsObject(std::nullptr_t) { return nullptr; }

template <typename T>
Object* Root<T>::RootObject() const
{
    return AsObject(p_);
}

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
    else return dynamic_cast<T*>(p) != nullptr;
}

// `x as T`
template <typename T, typename U>
inline T* As(U* p)
{
    if constexpr (std::is_base_of<T, U>::value) return static_cast<T*>(p);
    else return p ? dynamic_cast<T*>(p) : nullptr;
}

// `(T)x` for references: InvalidCastException unless null or a T.
template <typename T, typename U>
inline T* Cast(U* p)
{
    if constexpr (std::is_base_of<T, U>::value) return static_cast<T*>(p);
    else {
        if (!p) return nullptr;
        T* r = dynamic_cast<T*>(p);
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
