#pragma once
// Delegates: cs::Delegate<R(A...)>* for every C# delegate type of that signature (Action,
// Func<...>, Predicate<T>, Comparison<T>, UnityAction, TweenCallback, the game's own).
//
// A delegate is an immutable invocation list of (target, method) pairs, as MulticastDelegate:
//  - Combine(a, b) appends b's list to a's (null is the empty list); Remove(a, b) takes out the
//    last occurrence of b's list as a contiguous run (Delegate.Remove), giving null when
//    nothing is left and `a` itself when b is not found;
//  - Invoke calls every entry in order and returns the last one's result;
//  - two delegates are equal when their lists are: the same targets (by reference) and the same
//    methods. A method is identified by the thunk tools/translate instantiates for it
//    (Bind<C, &C::M>): the same method gives the same thunk, so `-=` finds what `+=` added.

#include <cstdint>
#include <vector>

#include "cs/Array.h"
#include "cs/Object.h"

namespace cs {

class DelegateBase : public Object {
public:
    struct Entry {
        Object* target;  // the instance (null for a static method)
        void* self;      // the same instance as the method's class sees it
        void* thunk;     // the method's thunk
        const void* id;  // the method's identity (a variable per thunk: never folded by a linker)
    };
    const char* cs_typename() const override { return "System.MulticastDelegate"; }
    bool cs_traced() const override { return true; }
    void cs_trace(Tracer& t) override
    {
        for (const Entry& e : list_) t.Mark(e.target);
    }
    bool Equals(Object* other) override
    {
        auto* o = dynamic_cast<DelegateBase*>(other);
        if (!o || o->list_.size() != list_.size()) return false;
        for (size_t i = 0; i < list_.size(); i++)
            if (list_[i].target != o->list_[i].target || list_[i].id != o->list_[i].id) return false;
        return true;
    }
    int32_t GetHashCode() override { return static_cast<int32_t>(reinterpret_cast<uintptr_t>(list_.empty() ? nullptr : list_[0].id) >> 2); }
    Object* get_Target() const { return list_.empty() ? nullptr : list_.back().target; }
    const std::vector<Entry>& Entries() const { return list_; }

protected:
    std::vector<Entry> list_;
};

inline bool DelegateEquals(DelegateBase* a, DelegateBase* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    return a->Equals(b);
}

template <typename Sig>
class Delegate;

template <typename R, typename... A>
class Delegate<R(A...)> : public DelegateBase {
public:
    using Thunk = R (*)(void* self, A... args);

    // A delegate to an instance method: Bind<C, &C::M>(obj).
    template <typename C, R (C::*M)(A...)>
    static Delegate* Bind(C* obj)
    {
        static char id;
        Thunk t = [](void* self, A... args) -> R { return (static_cast<C*>(self)->*M)(args...); };
        return Make(AsObject(nc(obj)), static_cast<void*>(obj), t, &id);
    }
    // A delegate to a static method (or a free function): BindStatic<&F>().
    template <R (*Fn)(A...)>
    static Delegate* BindStatic()
    {
        static char id;
        Thunk t = [](void*, A... args) -> R { return Fn(args...); };
        return Make(nullptr, nullptr, t, &id);
    }
    static Delegate* Make(Object* target, void* self, Thunk t, const void* id)
    {
        auto* d = gcnew<Delegate>();
        d->list_.push_back(Entry{target, self, reinterpret_cast<void*>(t), id});
        return d;
    }

    R Invoke(A... args)
    {
        // The list is immutable; a copy keeps a handler that changes the field invoking the old list.
        if constexpr (std::is_void<R>::value) {
            for (const Entry& e : list_) reinterpret_cast<Thunk>(e.thunk)(e.self, args...);
        } else {
            R r{};
            for (const Entry& e : list_) r = reinterpret_cast<Thunk>(e.thunk)(e.self, args...);
            return r;
        }
    }

    static Delegate* Combine(Delegate* a, Delegate* b)
    {
        if (!a) return b;
        if (!b) return a;
        auto* d = gcnew<Delegate>();
        d->list_ = a->list_;
        d->list_.insert(d->list_.end(), b->list_.begin(), b->list_.end());
        return d;
    }

    static Delegate* Remove(Delegate* source, Delegate* value)
    {
        if (!source) return nullptr;
        if (!value) return source;
        const auto& s = source->list_;
        const auto& v = value->list_;
        if (v.size() > s.size()) return source;
        for (size_t i = s.size() - v.size() + 1; i-- > 0;) {
            bool match = true;
            for (size_t j = 0; j < v.size() && match; j++)
                match = s[i + j].target == v[j].target && s[i + j].id == v[j].id;
            if (match) {
                if (s.size() == v.size()) return nullptr;
                auto* d = gcnew<Delegate>();
                d->list_.assign(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(i));
                d->list_.insert(d->list_.end(), s.begin() + static_cast<std::ptrdiff_t>(i + v.size()), s.end());
                return d;
            }
        }
        return source;
    }

    // Delegate.GetInvocationList()
    Array<Delegate*>* GetInvocationList()
    {
        auto* a = Array<Delegate*>::New(static_cast<int32_t>(list_.size()));
        for (size_t i = 0; i < list_.size(); i++) {
            auto* d = gcnew<Delegate>();
            d->list_.push_back(list_[i]);
            a->at(static_cast<int32_t>(i)) = d;
        }
        return a;
    }
};

// d1 == d2 / d1 != d2 on delegates (Delegate.op_Equality).
inline bool DelegateOpEquality(DelegateBase* a, DelegateBase* b) { return DelegateEquals(a, b); }

} // namespace cs
