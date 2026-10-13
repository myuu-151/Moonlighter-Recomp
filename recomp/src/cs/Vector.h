#pragma once
// The growable array behind List, Array, Dictionary and HashSet: std::vector without the
// vector<bool> specialization (a C# bool[] or List<bool> hands out real bool references).

#include <cstddef>
#include <initializer_list>
#include <type_traits>
#include <vector>

namespace cs {

namespace detail {
// bool is stored as a struct holding one, so std::vector stays a plain vector.
struct BoolSlot {
    bool v = false;
};
} // namespace detail

template <typename T>
class Vector {
    static constexpr bool kBool = std::is_same<T, bool>::value;
    using Slot = typename std::conditional<kBool, detail::BoolSlot, T>::type;
    static T& Ref(Slot& s)
    {
        if constexpr (kBool) return s.v;
        else return s;
    }
    static const T& Ref(const Slot& s)
    {
        if constexpr (kBool) return s.v;
        else return s;
    }

public:
    Vector() = default;
    explicit Vector(size_t n) : v_(n) {}
    Vector(std::initializer_list<T> items)
    {
        v_.reserve(items.size());
        for (const T& x : items) push_back(x);
    }
    Vector(const std::vector<T>& items)
    {
        v_.reserve(items.size());
        for (const T& x : items) push_back(x);
    }

    size_t size() const { return v_.size(); }
    bool empty() const { return v_.empty(); }
    void reserve(size_t n) { v_.reserve(n); }
    void resize(size_t n) { v_.resize(n); }
    void clear() { v_.clear(); }
    void push_back(const T& x)
    {
        if constexpr (kBool) v_.push_back(detail::BoolSlot{x});
        else v_.push_back(x);
    }
    T& operator[](size_t i) { return Ref(v_[i]); }
    const T& operator[](size_t i) const { return Ref(v_[i]); }
    T& back() { return Ref(v_.back()); }
    T* data()
    {
        if constexpr (kBool) return v_.empty() ? nullptr : &v_[0].v;
        else return v_.data();
    }

    void insert_at(size_t i, const T& x)
    {
        if constexpr (kBool) v_.insert(v_.begin() + static_cast<std::ptrdiff_t>(i), detail::BoolSlot{x});
        else v_.insert(v_.begin() + static_cast<std::ptrdiff_t>(i), x);
    }
    void insert_at(size_t i, const Vector& other)
    {
        v_.insert(v_.begin() + static_cast<std::ptrdiff_t>(i), other.v_.begin(), other.v_.end());
    }
    void erase_at(size_t i, size_t n = 1)
    {
        v_.erase(v_.begin() + static_cast<std::ptrdiff_t>(i), v_.begin() + static_cast<std::ptrdiff_t>(i + n));
    }
    void reverse(size_t i, size_t n)
    {
        for (size_t a = i, b = i + n; a + 1 < b; a++, b--) std::swap(v_[a], v_[b - 1]);
    }

    // Iteration (range-for), as T&.
    class iterator {
    public:
        explicit iterator(Slot* p) : p_(p) {}
        T& operator*() const { return Ref(*p_); }
        iterator& operator++()
        {
            ++p_;
            return *this;
        }
        bool operator!=(const iterator& o) const { return p_ != o.p_; }
        bool operator==(const iterator& o) const { return p_ == o.p_; }

    private:
        Slot* p_;
    };
    class const_iterator {
    public:
        explicit const_iterator(const Slot* p) : p_(p) {}
        const T& operator*() const { return Ref(*p_); }
        const_iterator& operator++()
        {
            ++p_;
            return *this;
        }
        bool operator!=(const const_iterator& o) const { return p_ != o.p_; }
        bool operator==(const const_iterator& o) const { return p_ == o.p_; }

    private:
        const Slot* p_;
    };
    iterator begin() { return iterator(v_.data()); }
    iterator end() { return iterator(v_.data() + v_.size()); }
    const_iterator begin() const { return const_iterator(v_.data()); }
    const_iterator end() const { return const_iterator(v_.data() + v_.size()); }

    std::vector<T> ToStd() const
    {
        std::vector<T> r;
        r.reserve(size());
        for (const T& x : *this) r.push_back(x);
        return r;
    }

private:
    std::vector<Slot> v_;
};

} // namespace cs
