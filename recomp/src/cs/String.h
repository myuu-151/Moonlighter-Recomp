#pragma once
// System.String: an immutable UTF-16 string (a reference type: cs::String*, null is not "").
// Ordinal operations are exact. Culture-sensitive ones (Compare, StartsWith/EndsWith/IndexOf
// with a string, ToLower/ToUpper) follow en-US, the culture OptionsPanel.Load sets for every
// thread: exact for ASCII letters and digits; where punctuation decides (Mono's collator orders
// symbols its own way and ignores the hyphen and the apostrophe at the first level) and past
// ASCII they fall back to code-unit order (docs/translator_plan.md, "Known approximations").

#include <cstdint>
#include <initializer_list>
#include <string>

#include "cs/Interfaces.h"
#include "cs/Object.h"

namespace cs {

class String : public Object, public virtual IEnumerable_1<char16_t> {
public:
    explicit String(std::u16string s) : s_(std::move(s)) {}

    // foreach over a string through IEnumerable<char> (CharEnumerator)
    IEnumerator_1<char16_t>* IEnumerable_1_GetEnumerator() override;
    IEnumerator* IEnumerable_GetEnumerator() override;

    const char* cs_typename() const override { return "System.String"; }
    bool cs_traced() const override { return true; }
    String* ToString() override { return this; }
    bool Equals(Object* other) override;
    int32_t GetHashCode() override;

    // ---- creating ----
    static String* New(std::u16string s);
    static String* FromUtf8(const std::string& s);
    static String* FromAscii(const char* s);
    // A literal: interned, so equal literals are the same object (as C# literals are).
    static String* Literal(const char16_t* s, size_t n);
    // A literal too long for one C++ string literal, in pieces.
    static String* LiteralChunks(std::initializer_list<const char16_t*> chunks);
    static String* Intern(String* s);
    static String* get_Empty();

    // ---- reading ----
    int32_t get_Length() const { return static_cast<int32_t>(s_.size()); }
    char16_t get_Chars(int32_t i) const;
    const std::u16string& str() const { return s_; }
    std::string ToUtf8() const;

    // ---- comparing ----
    static bool op_Equality(String* a, String* b);
    static bool op_Inequality(String* a, String* b) { return !op_Equality(a, b); }
    bool Equals(String* other) { return op_Equality(this, other); }
    static bool Equals(String* a, String* b) { return op_Equality(a, b); }
    static int32_t CompareOrdinal(String* a, String* b);
    static int32_t Compare(String* a, String* b); // culture (en-US)
    int32_t CompareTo(String* other) { return Compare(this, other); }
    static bool IsNullOrEmpty(String* s) { return !s || s->s_.empty(); }
    static bool IsNullOrWhiteSpace(String* s);

    // ---- searching ----
    int32_t IndexOf(char16_t c) const;
    int32_t IndexOf(char16_t c, int32_t start) const;
    int32_t IndexOf(String* value) const;
    int32_t IndexOf(String* value, int32_t start) const;
    int32_t LastIndexOf(char16_t c) const;
    int32_t LastIndexOf(String* value) const;
    int32_t IndexOfAny(Array<char16_t>* chars) const;
    bool Contains(String* value) const;
    bool StartsWith(String* value) const;
    bool EndsWith(String* value) const;

    // ---- making new strings ----
    String* Substring(int32_t start);
    String* Substring(int32_t start, int32_t length);
    String* Replace(String* oldValue, String* newValue);
    String* Replace(char16_t oldChar, char16_t newChar);
    String* ToLower();
    String* ToUpper();
    String* ToLowerInvariant();
    String* ToUpperInvariant();
    String* Trim();
    String* TrimStart();
    String* TrimEnd();
    String* Trim(Array<char16_t>* chars);
    String* Insert(int32_t index, String* value);
    String* Remove(int32_t start);
    String* Remove(int32_t start, int32_t count);
    String* PadLeft(int32_t width);
    String* PadLeft(int32_t width, char16_t c);
    String* PadRight(int32_t width);
    String* PadRight(int32_t width, char16_t c);
    Array<String*>* Split(Array<char16_t>* separators);
    Array<char16_t>* ToCharArray();

    static String* Concat(String* a, String* b);
    static String* Concat(String* a, String* b, String* c);
    static String* Concat(String* a, String* b, String* c, String* d);
    static String* Concat(Array<String*>* values);
    static String* Concat(Object* a);
    static String* Concat(Object* a, Object* b);
    static String* Concat(Object* a, Object* b, Object* c);
    static String* Concat(Object* a, Object* b, Object* c, Object* d);
    static String* Concat(Array<Object*>* values);
    static String* Join(String* separator, Array<String*>* values);
    static String* Format(String* format, Object* a);
    static String* Format(String* format, Object* a, Object* b);
    static String* Format(String* format, Object* a, Object* b, Object* c);
    static String* Format(String* format, Array<Object*>* args);

private:
    std::u16string s_;
};

// x.ToString() for any object reference (null-checked) and "" + x (null is "").
inline String* ToStringOrEmpty(Object* o)
{
    if (!o) return String::get_Empty();
    String* s = o->ToString();
    return s ? s : String::get_Empty();
}

// Formatting of the primitives (en-US / invariant: the same for numbers).
String* ToString(int32_t v);
String* ToString(int64_t v);
String* ToString(uint32_t v);
String* ToString(uint64_t v);
String* ToString(bool v);
String* ToString(char16_t v);
String* ToString(float v);  // "G": 7 significant digits (Mono's NumberFormatter)
String* ToString(double v); // "G": 15 significant digits

// The generated dispatchers' lookup (SendMessage / Invoke / an Animator event by name,
// StartCoroutine(string), a prefab's serialized fields): the index of `name` in a table of names
// (null-terminated), or -1 (a null name too). One shared loop instead of a comparison per name in
// every class.
int32_t NameIndex(String* name, const char16_t* const* names, int32_t count);
int32_t NameIndex(String* name, const char* const* names, int32_t count); // ASCII names
int32_t NameIndex(const char* name, const char* const* names, int32_t count);

// FsJSONManager.RemoveComments (the game's: tools/translate/Overrides.cs) in one pass: the same
// result, the same exceptions, the same object back when there is no comment, but one new string
// instead of one per comment (the game's copies the whole text at each of them: 6 MB of garbage
// for the 34 KB constants file).
String* RemoveJsonComments(String* text);

// Parsing as int.Parse / float.Parse with the invariant (en-US) culture.
int32_t ParseInt32(String* s);
bool TryParseInt32(String* s, int32_t& result);
double ParseDouble(String* s);
bool TryParseDouble(String* s, double& result);

} // namespace cs

// A long literal in pieces: `CS_LIT_CHUNKS(u"part1", u"part2")`, interned once per use site.
#define CS_LIT_CHUNKS(...)          \
    ([]() -> ::cs::String* {          \
        static ::cs::String* s_ = ::cs::String::LiteralChunks({__VA_ARGS__}); \
        return s_;          \
    }())

// A string literal (UTF-16): `CS_LIT(u"text")`, interned once per use site.
#define CS_LIT(lit)                                                                            \
    ([]() -> ::cs::String* {                                                                   \
        static ::cs::String* s_ = ::cs::String::Literal(lit, sizeof(lit) / sizeof(char16_t) - 1); \
        return s_;                                                                             \
    }())
