#pragma once
// System.Collections.Generic.Queue<T> (FIFO: Enqueue at the tail, Dequeue / Peek at the head,
// enumerated head to tail; Dequeue and Peek on an empty queue throw InvalidOperationException
// "Queue empty."), and System.Diagnostics.Stopwatch.
//
// Stopwatch measures wall time, which a translation can't reproduce: it reads 0 elapsed, so code
// that slices work by time (AStar.CalculatePath yields after 30 ms) never slices. The hand port
// does the same (game/PolyNav.cpp); the game's paths take well under that budget.

#include <deque>

#include "cs/Array.h"
#include "cs/Collections.h"
#include "cs/Object.h"

namespace cs {

namespace shared {

template <typename T>
class Queue : public Object {
public:
    const char* cs_typename() const override { return "System.Collections.Generic.Queue`1"; }
    bool cs_traced() const override { return true; }
    void cs_trace(Tracer& t) override
    {
        if constexpr (std::is_pointer<T>::value || detail::HasValueTrace<T>::value)
            for (const T& v : items_) TraceField(t, v);
    }
    void __ctor() {}
    void __ctor(int32_t capacity)
    {
        if (capacity < 0) ThrowArgumentOutOfRange();
    }

    int32_t get_Count() const { return static_cast<int32_t>(items_.size()); }
    void Enqueue(T item) { items_.push_back(item); }
    T Dequeue()
    {
        if (items_.empty()) Throw(gcnew<InvalidOperationException>(String::FromAscii("Queue empty.")));
        T v = items_.front();
        items_.pop_front();
        return v;
    }
    T Peek()
    {
        if (items_.empty()) Throw(gcnew<InvalidOperationException>(String::FromAscii("Queue empty.")));
        return items_.front();
    }
    void Clear() { items_.clear(); }
    bool Contains(T item)
    {
        for (const T& v : items_)
            if (EqualityComparer<T>::Equals(v, item)) return true;
        return false;
    }
    Array<T>* ToArray()
    {
        auto* a = Array<T>::New(get_Count());
        for (size_t i = 0; i < items_.size(); i++) a->at(static_cast<int32_t>(i)) = items_[i];
        return a;
    }
    void TrimExcess() {}

private:
    std::deque<T> items_;
};

} // namespace shared

class Stopwatch : public Object {
public:
    const char* cs_typename() const override { return "System.Diagnostics.Stopwatch"; }
    void __ctor() {}
    void Start() { running_ = true; }
    void Stop() { running_ = false; }
    void Reset() { running_ = false; }
    void Restart() { running_ = true; }
    bool get_IsRunning() const { return running_; }
    int64_t get_ElapsedMilliseconds() const { return 0; }
    int64_t get_ElapsedTicks() const { return 0; }
    static Stopwatch* StartNew()
    {
        auto* s = gcnew<Stopwatch>();
        s->Start();
        return s;
    }

private:
    bool running_ = false;
};

} // namespace cs
