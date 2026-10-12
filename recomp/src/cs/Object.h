#pragma once
// Minimal stand-in for the .NET object model used by translated game code.
//
// C# reference types become heap objects owned by a cs::Heap and are passed around
// as raw pointers (C# references). Nothing is freed individually; a Heap is cleared
// as a whole (per dungeon floor, per test run). A proper collector can replace this
// later without touching translated code, since that only uses gcnew<T>().

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cs {

struct Object {
    virtual ~Object() = default;
};

class Heap {
public:
    template <typename T, typename... Args>
    T* New(Args&&... args)
    {
        auto obj = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = obj.get();
        objects_.push_back(std::move(obj));
        return raw;
    }

    void Clear() { objects_.clear(); }
    size_t Count() const { return objects_.size(); }

private:
    std::vector<std::unique_ptr<Object>> objects_;
};

// The heap new objects go to; set by whoever owns the current object lifetime scope.
Heap& CurrentHeap();
void SetCurrentHeap(Heap* heap);

template <typename T, typename... Args>
T* gcnew(Args&&... args)
{
    return CurrentHeap().New<T>(std::forward<Args>(args)...);
}

// .NET exceptions that translated code can raise. The oracle records exception type
// names, so these mirror the .NET names.
struct Exception : std::runtime_error {
    using std::runtime_error::runtime_error;
    virtual const char* TypeName() const { return "Exception"; }
};
struct NullReferenceException : Exception {
    NullReferenceException() : Exception("NullReferenceException") {}
    const char* TypeName() const override { return "NullReferenceException"; }
};
struct KeyNotFoundException : Exception {
    KeyNotFoundException() : Exception("KeyNotFoundException") {}
    const char* TypeName() const override { return "KeyNotFoundException"; }
};
struct ArgumentException : Exception {
    explicit ArgumentException(const char* m = "ArgumentException") : Exception(m) {}
    const char* TypeName() const override { return "ArgumentException"; }
};
struct ArgumentOutOfRangeException : Exception {
    ArgumentOutOfRangeException() : Exception("ArgumentOutOfRangeException") {}
    const char* TypeName() const override { return "ArgumentOutOfRangeException"; }
};

template <typename T>
inline T* NullCheck(T* p)
{
    if (!p) throw NullReferenceException();
    return p;
}

// C# `x is T` for reference types.
template <typename T, typename U>
inline bool Is(U* p)
{
    return dynamic_cast<T*>(p) != nullptr;
}

} // namespace cs
