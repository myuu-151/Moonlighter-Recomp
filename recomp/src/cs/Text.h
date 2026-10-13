#pragma once
// System.Text.StringBuilder, System.Char, System.Environment, the culture types the game names
// (CultureInfo, NumberStyles, IFormatProvider: the game runs en-US, whose number formats are
// the invariant culture's), StringComparer, and the number parsers with styles.

#include <cstdint>
#include <string>

#include "cs/Array.h"
#include "cs/Collections.h"
#include "cs/Object.h"
#include "cs/String.h"

namespace cs {

class StringBuilder : public Object {
public:
    const char* cs_typename() const override { return "System.Text.StringBuilder"; }
    bool cs_traced() const override { return true; }
    void __ctor() {}
    void __ctor(int32_t capacity)
    {
        if (capacity < 0) ThrowArgumentOutOfRange();
    }
    void __ctor(String* value)
    {
        if (value) s_ = value->str();
    }
    String* ToString() override { return String::New(s_); }
    int32_t get_Length() const { return static_cast<int32_t>(s_.size()); }
    void set_Length(int32_t n)
    {
        if (n < 0) ThrowArgumentOutOfRange();
        s_.resize(static_cast<size_t>(n), u'\0');
    }
    char16_t get_Chars(int32_t i) const
    {
        if (static_cast<uint32_t>(i) >= s_.size()) ThrowIndexOutOfRange();
        return s_[static_cast<size_t>(i)];
    }
    void set_Chars(int32_t i, char16_t c)
    {
        if (static_cast<uint32_t>(i) >= s_.size()) ThrowIndexOutOfRange();
        s_[static_cast<size_t>(i)] = c;
    }
    StringBuilder* Append(char16_t c)
    {
        s_.push_back(c);
        return this;
    }
    StringBuilder* Append(String* s)
    {
        if (s) s_ += s->str();
        return this;
    }
    StringBuilder* Append(Object* o) { return Append(o ? o->ToString() : nullptr); }
    StringBuilder* Append(int32_t v) { return Append(cs::ToString(v)); }
    StringBuilder* Append(int64_t v) { return Append(cs::ToString(v)); }
    StringBuilder* Append(float v) { return Append(cs::ToString(v)); }
    StringBuilder* Append(double v) { return Append(cs::ToString(v)); }
    StringBuilder* Append(bool v) { return Append(cs::ToString(v)); }
    StringBuilder* Append(char16_t c, int32_t repeat)
    {
        if (repeat < 0) ThrowArgumentOutOfRange();
        s_.append(static_cast<size_t>(repeat), c);
        return this;
    }
    StringBuilder* AppendLine() { return Append(String::FromAscii("\r\n")); }
    StringBuilder* AppendLine(String* s) { return Append(s)->AppendLine(); }
    StringBuilder* Insert(int32_t index, String* s)
    {
        if (index < 0 || index > get_Length()) ThrowArgumentOutOfRange();
        if (s) s_.insert(static_cast<size_t>(index), s->str());
        return this;
    }
    StringBuilder* Remove(int32_t start, int32_t length)
    {
        if (start < 0 || length < 0 || start > get_Length() - length) ThrowArgumentOutOfRange();
        s_.erase(static_cast<size_t>(start), static_cast<size_t>(length));
        return this;
    }
    StringBuilder* Clear()
    {
        s_.clear();
        return this;
    }

private:
    std::u16string s_;
};

namespace Char {
bool IsWhiteSpace(char16_t c);
inline bool IsDigit(char16_t c) { return c >= u'0' && c <= u'9'; }
inline bool IsLetter(char16_t c) { return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= 0xC0 && c != 0xD7 && c != 0xF7 && c < 0x250); }
inline bool IsLetterOrDigit(char16_t c) { return IsLetter(c) || IsDigit(c); }
inline bool IsUpper(char16_t c) { return (c >= u'A' && c <= u'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7); }
inline bool IsLower(char16_t c) { return (c >= u'a' && c <= u'z') || (c >= 0xDF && c <= 0xFF && c != 0xF7); }
inline char16_t ToUpper(char16_t c) { return (c >= u'a' && c <= u'z') ? static_cast<char16_t>(c - 32) : c; }
inline char16_t ToLower(char16_t c) { return (c >= u'A' && c <= u'Z') ? static_cast<char16_t>(c + 32) : c; }
inline bool IsWhiteSpace(String* s, int32_t i) { return IsWhiteSpace(nc(s)->get_Chars(i)); }
} // namespace Char

namespace Environment {
// The game runs on Windows: "\r\n".
String* get_NewLine();
} // namespace Environment

// System.Globalization.NumberStyles
enum class NumberStyles : int32_t {
    None = 0,
    AllowLeadingWhite = 1,
    AllowTrailingWhite = 2,
    AllowLeadingSign = 4,
    AllowTrailingSign = 8,
    AllowParentheses = 16,
    AllowDecimalPoint = 32,
    AllowThousands = 64,
    AllowExponent = 128,
    AllowCurrencySymbol = 256,
    AllowHexSpecifier = 512,
    Integer = 7,
    HexNumber = 515,
    Number = 111,
    Float = 167,
    Currency = 383,
    Any = 511,
};

struct IFormatProvider {
    virtual ~IFormatProvider() = default;
};

// CultureInfo: the invariant culture and en-US format numbers alike, which is all the game does with them.
class CultureInfo : public Object, public virtual IFormatProvider {
public:
    const char* cs_typename() const override { return "System.Globalization.CultureInfo"; }
    bool cs_traced() const override { return true; }
    static CultureInfo* get_InvariantCulture();
    static CultureInfo* get_CurrentCulture();
};

// int/long/double.Parse and TryParse with styles (the invariant culture's symbols).
bool TryParseInt64(String* s, NumberStyles styles, IFormatProvider* provider, int64_t& result);
bool TryParseInt32(String* s, NumberStyles styles, IFormatProvider* provider, int32_t& result);
bool TryParseDouble(String* s, NumberStyles styles, IFormatProvider* provider, double& result);
bool TryParseSingle(String* s, NumberStyles styles, IFormatProvider* provider, float& result);
double ParseDouble(String* s, NumberStyles styles, IFormatProvider* provider);
float ParseSingle(String* s);
bool TryParseSingle(String* s, float& result);
int64_t ParseInt64(String* s);

// StringComparer.Ordinal / OrdinalIgnoreCase
class StringComparer : public Object, public virtual IEqualityComparer_1<String*>, public virtual IComparer_1<String*> {
public:
    explicit StringComparer(bool ignoreCase) : ignoreCase_(ignoreCase) {}
    const char* cs_typename() const override { return ignoreCase_ ? "System.OrdinalIgnoreCaseComparer" : "System.OrdinalComparer"; }
    bool cs_traced() const override { return true; }
    static StringComparer* get_Ordinal();
    static StringComparer* get_OrdinalIgnoreCase();
    bool Equals(String* a, String* b);
    int32_t GetHashCode(String* s);
    int32_t Compare(String* a, String* b);
    bool IEqualityComparer_1_Equals(String* a, String* b) override { return Equals(a, b); }
    int32_t IEqualityComparer_1_GetHashCode(String* s) override { return GetHashCode(s); }
    int32_t IComparer_1_Compare(String* a, String* b) override { return Compare(a, b); }

private:
    bool ignoreCase_;
};

} // namespace cs
