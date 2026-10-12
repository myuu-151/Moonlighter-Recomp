#pragma once
// Small JSON reader. Accepts what FullSerializer accepts in the game's data files:
// // and /* */ comments and trailing commas. Numbers keep FullSerializer's split
// between integers (IsInt64) and doubles (anything with '.', 'e' or 'E').

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace json {

struct Value {
    enum class Kind { Null, Bool, Int, Double, String, Array, Object };

    Kind kind = Kind::Null;
    bool b = false;
    int64_t i = 0;
    double d = 0.0;
    std::string s;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;

    bool IsNull() const { return kind == Kind::Null; }
    bool IsInt() const { return kind == Kind::Int; }
    bool IsDouble() const { return kind == Kind::Double; }
    bool IsString() const { return kind == Kind::String; }
    bool IsArray() const { return kind == Kind::Array; }
    bool IsObject() const { return kind == Kind::Object; }

    // Object member lookup; nullptr if absent or not an object.
    const Value* Find(const std::string& key) const;
    const Value& operator[](const std::string& key) const; // throws if absent
    const Value& operator[](size_t index) const { return arr.at(index); }
    size_t size() const { return kind == Kind::Array ? arr.size() : obj.size(); }

    int64_t AsInt() const;   // Int, or Double truncated
    double AsDouble() const; // Double, or Int converted

    std::string Dump() const; // compact serialization, for diagnostics
};

// Throws std::runtime_error with an offset on malformed input.
Value Parse(const std::string& text);

// Structural equality (Int and Double compare by kind and value).
bool operator==(const Value& a, const Value& b);
inline bool operator!=(const Value& a, const Value& b) { return !(a == b); }

// Path to the first difference ("" if equal), e.g. "layout.rooms[3].u[0].n".
std::string FirstDifference(const Value& a, const Value& b, const std::string& path = "");

} // namespace json
