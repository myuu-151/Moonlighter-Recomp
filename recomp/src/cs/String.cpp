#include "cs/String.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

#include "cs/Array.h"
#include "cs/Box.h"

namespace cs {

namespace {

struct U16Hash {
    size_t operator()(const std::u16string& s) const
    {
        size_t h = 1469598103934665603ull;
        for (char16_t c : s) h = (h ^ c) * 1099511628211ull;
        return h;
    }
};

std::unordered_map<std::u16string, String*, U16Hash>& InternTable()
{
    static std::unordered_map<std::u16string, String*, U16Hash> t;
    return t;
}

// Interned strings are permanent: allocated outside every heap, never freed.
String* Permanent(const std::u16string& s)
{
    auto& t = InternTable();
    auto it = t.find(s);
    if (it != t.end()) return it->second;
    String* p = new String(s);
    t.emplace(s, p);
    return p;
}

bool IsWhite(char16_t c)
{
    // char.IsWhiteSpace for the BMP characters .NET's Trim removes.
    return c == u' ' || (c >= 0x09 && c <= 0x0D) || c == 0xA0 || c == 0x85 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

char16_t LowerInvariant(char16_t c)
{
    if (c >= u'A' && c <= u'Z') return static_cast<char16_t>(c + 32);
    if ((c >= 0xC0 && c <= 0xDE && c != 0xD7)) return static_cast<char16_t>(c + 32);
    return c;
}
char16_t UpperInvariant(char16_t c)
{
    if (c >= u'a' && c <= u'z') return static_cast<char16_t>(c - 32);
    if ((c >= 0xE0 && c <= 0xFE && c != 0xF7)) return static_cast<char16_t>(c - 32);
    return c;
}

std::u16string Ascii(const char* s)
{
    std::u16string r;
    for (; *s; s++) r.push_back(static_cast<char16_t>(static_cast<unsigned char>(*s)));
    return r;
}

} // namespace

// ---- creating ------------------------------------------------------------------------------------

String* String::New(std::u16string s) { return gcnew<String>(std::move(s)); }

String* String::FromAscii(const char* s) { return New(Ascii(s)); }

String* String::FromUtf8(const std::string& s)
{
    std::u16string r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp;
        int n;
        if (c < 0x80) cp = c, n = 1;
        else if ((c >> 5) == 6) cp = c & 0x1F, n = 2;
        else if ((c >> 4) == 14) cp = c & 0x0F, n = 3;
        else if ((c >> 3) == 30) cp = c & 0x07, n = 4;
        else {
            r.push_back(0xFFFD);
            i++;
            continue;
        }
        if (i + n > s.size()) {
            r.push_back(0xFFFD);
            break;
        }
        for (int k = 1; k < n; k++) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += n;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            r.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            r.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            r.push_back(static_cast<char16_t>(cp));
        }
    }
    return New(std::move(r));
}

std::string String::ToUtf8() const
{
    std::string r;
    for (size_t i = 0; i < s_.size(); i++) {
        uint32_t cp = s_[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s_.size() && s_[i + 1] >= 0xDC00 && s_[i + 1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (s_[i + 1] - 0xDC00);
            i++;
        }
        if (cp < 0x80) r.push_back(static_cast<char>(cp));
        else if (cp < 0x800) {
            r.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            r.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            r.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            r.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            r.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            r.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            r.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            r.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            r.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return r;
}

String* String::Literal(const char16_t* s, size_t n) { return Permanent(std::u16string(s, n)); }

String* String::Intern(String* s) { return Permanent(nc(s)->s_); }

String* String::get_Empty()
{
    static String* e = Permanent(std::u16string());
    return e;
}

// ---- reading and comparing -------------------------------------------------------------------------

char16_t String::get_Chars(int32_t i) const
{
    if (static_cast<uint32_t>(i) >= s_.size()) ThrowIndexOutOfRange();
    return s_[static_cast<size_t>(i)];
}

bool String::Equals(Object* other)
{
    auto* o = dynamic_cast<String*>(other);
    return o && o->s_ == s_;
}

// Mono's String.GetHashCode (unobserved by the game so far; kept deterministic).
int32_t String::GetHashCode()
{
    int32_t h = 0;
    const char16_t* c = s_.data();
    size_t n = s_.size();
    size_t i = 0;
    for (; i + 1 < n; i += 2) {
        h = static_cast<int32_t>((static_cast<uint32_t>(h) << 5) - static_cast<uint32_t>(h) + c[i]);
        h = static_cast<int32_t>((static_cast<uint32_t>(h) << 5) - static_cast<uint32_t>(h) + c[i + 1]);
    }
    if (i < n) h = static_cast<int32_t>((static_cast<uint32_t>(h) << 5) - static_cast<uint32_t>(h) + c[i]);
    return h;
}

bool String::op_Equality(String* a, String* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    return a->s_ == b->s_;
}

int32_t String::CompareOrdinal(String* a, String* b)
{
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    size_t n = std::min(a->s_.size(), b->s_.size());
    for (size_t i = 0; i < n; i++)
        if (a->s_[i] != b->s_[i]) return static_cast<int32_t>(a->s_[i]) - static_cast<int32_t>(b->s_[i]);
    return static_cast<int32_t>(a->s_.size()) - static_cast<int32_t>(b->s_.size());
}

// Culture (en-US) comparison for ASCII letters and digits: case-insensitive first, then
// lowercase before uppercase at the first case difference. Other characters compare
// ordinally (a gap: progress/unity_api_gaps.tsv).
int32_t String::Compare(String* a, String* b)
{
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    const auto& x = a->s_;
    const auto& y = b->s_;
    size_t n = std::min(x.size(), y.size());
    for (size_t i = 0; i < n; i++) {
        char16_t cx = LowerInvariant(x[i]), cy = LowerInvariant(y[i]);
        if (cx != cy) return cx < cy ? -1 : 1;
    }
    if (x.size() != y.size()) return x.size() < y.size() ? -1 : 1;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return x[i] > y[i] ? -1 : 1; // lowercase first
    return 0;
}

bool String::IsNullOrWhiteSpace(String* s)
{
    if (!s) return true;
    for (char16_t c : s->s_)
        if (!IsWhite(c)) return false;
    return true;
}

// ---- searching ----------------------------------------------------------------------------------------

int32_t String::IndexOf(char16_t c) const { return IndexOf(c, 0); }
int32_t String::IndexOf(char16_t c, int32_t start) const
{
    if (start < 0 || start > get_Length()) ThrowArgumentOutOfRange();
    size_t p = s_.find(c, static_cast<size_t>(start));
    return p == std::u16string::npos ? -1 : static_cast<int32_t>(p);
}
int32_t String::IndexOf(String* value) const { return IndexOf(value, 0); }
int32_t String::IndexOf(String* value, int32_t start) const
{
    if (!value) ThrowArgumentNull("value");
    if (start < 0 || start > get_Length()) ThrowArgumentOutOfRange();
    size_t p = s_.find(value->s_, static_cast<size_t>(start));
    return p == std::u16string::npos ? -1 : static_cast<int32_t>(p);
}
int32_t String::LastIndexOf(char16_t c) const
{
    size_t p = s_.rfind(c);
    return p == std::u16string::npos ? -1 : static_cast<int32_t>(p);
}
int32_t String::LastIndexOf(String* value) const
{
    if (!value) ThrowArgumentNull("value");
    size_t p = s_.rfind(value->s_);
    return p == std::u16string::npos ? -1 : static_cast<int32_t>(p);
}
int32_t String::IndexOfAny(Array<char16_t>* chars) const
{
    nc(chars);
    for (size_t i = 0; i < s_.size(); i++)
        for (char16_t c : chars->vec())
            if (s_[i] == c) return static_cast<int32_t>(i);
    return -1;
}
bool String::Contains(String* value) const
{
    if (!value) ThrowArgumentNull("value");
    return s_.find(value->s_) != std::u16string::npos;
}
bool String::StartsWith(String* value) const
{
    if (!value) ThrowArgumentNull("value");
    return s_.size() >= value->s_.size() && s_.compare(0, value->s_.size(), value->s_) == 0;
}
bool String::EndsWith(String* value) const
{
    if (!value) ThrowArgumentNull("value");
    return s_.size() >= value->s_.size() && s_.compare(s_.size() - value->s_.size(), value->s_.size(), value->s_) == 0;
}

// ---- making new strings ---------------------------------------------------------------------------------

String* String::Substring(int32_t start) { return Substring(start, get_Length() - start); }
String* String::Substring(int32_t start, int32_t length)
{
    if (start < 0 || start > get_Length() || length < 0 || start > get_Length() - length) ThrowArgumentOutOfRange();
    if (start == 0 && length == get_Length()) return this;
    return New(s_.substr(static_cast<size_t>(start), static_cast<size_t>(length)));
}
String* String::Replace(String* oldValue, String* newValue)
{
    if (!oldValue) ThrowArgumentNull("oldValue");
    if (oldValue->s_.empty()) ThrowArgument("String cannot be of zero length.");
    const std::u16string& nv = newValue ? newValue->s_ : std::u16string();
    std::u16string r;
    size_t pos = 0;
    bool any = false;
    for (;;) {
        size_t p = s_.find(oldValue->s_, pos);
        if (p == std::u16string::npos) break;
        any = true;
        r.append(s_, pos, p - pos);
        r.append(nv);
        pos = p + oldValue->s_.size();
    }
    if (!any) return this;
    r.append(s_, pos, std::u16string::npos);
    return New(std::move(r));
}
String* String::Replace(char16_t oldChar, char16_t newChar)
{
    if (s_.find(oldChar) == std::u16string::npos) return this;
    std::u16string r = s_;
    for (char16_t& c : r)
        if (c == oldChar) c = newChar;
    return New(std::move(r));
}
String* String::ToLowerInvariant()
{
    std::u16string r = s_;
    for (char16_t& c : r) c = LowerInvariant(c);
    return New(std::move(r));
}
String* String::ToUpperInvariant()
{
    std::u16string r = s_;
    for (char16_t& c : r) c = UpperInvariant(c);
    return New(std::move(r));
}
String* String::ToLower() { return ToLowerInvariant(); } // en-US maps like the invariant culture
String* String::ToUpper() { return ToUpperInvariant(); }
String* String::Trim()
{
    size_t b = 0, e = s_.size();
    while (b < e && IsWhite(s_[b])) b++;
    while (e > b && IsWhite(s_[e - 1])) e--;
    if (b == 0 && e == s_.size()) return this;
    return New(s_.substr(b, e - b));
}
String* String::TrimStart()
{
    size_t b = 0;
    while (b < s_.size() && IsWhite(s_[b])) b++;
    return b == 0 ? this : New(s_.substr(b));
}
String* String::TrimEnd()
{
    size_t e = s_.size();
    while (e > 0 && IsWhite(s_[e - 1])) e--;
    return e == s_.size() ? this : New(s_.substr(0, e));
}
String* String::Trim(Array<char16_t>* chars)
{
    if (!chars || chars->get_Length() == 0) return Trim();
    auto in = [&](char16_t c) {
        for (char16_t x : chars->vec())
            if (x == c) return true;
        return false;
    };
    size_t b = 0, e = s_.size();
    while (b < e && in(s_[b])) b++;
    while (e > b && in(s_[e - 1])) e--;
    if (b == 0 && e == s_.size()) return this;
    return New(s_.substr(b, e - b));
}
String* String::Insert(int32_t index, String* value)
{
    if (!value) ThrowArgumentNull("value");
    if (index < 0 || index > get_Length()) ThrowArgumentOutOfRange();
    std::u16string r = s_;
    r.insert(static_cast<size_t>(index), value->s_);
    return New(std::move(r));
}
String* String::Remove(int32_t start)
{
    if (start < 0 || start > get_Length()) ThrowArgumentOutOfRange();
    return New(s_.substr(0, static_cast<size_t>(start)));
}
String* String::Remove(int32_t start, int32_t count)
{
    if (start < 0 || count < 0 || start > get_Length() - count) ThrowArgumentOutOfRange();
    std::u16string r = s_;
    r.erase(static_cast<size_t>(start), static_cast<size_t>(count));
    return New(std::move(r));
}
String* String::PadLeft(int32_t width) { return PadLeft(width, u' '); }
String* String::PadLeft(int32_t width, char16_t c)
{
    if (width < 0) ThrowArgumentOutOfRange();
    if (width <= get_Length()) return this;
    return New(std::u16string(static_cast<size_t>(width - get_Length()), c) + s_);
}
String* String::PadRight(int32_t width) { return PadRight(width, u' '); }
String* String::PadRight(int32_t width, char16_t c)
{
    if (width < 0) ThrowArgumentOutOfRange();
    if (width <= get_Length()) return this;
    return New(s_ + std::u16string(static_cast<size_t>(width - get_Length()), c));
}
Array<String*>* String::Split(Array<char16_t>* separators)
{
    std::vector<String*> parts;
    auto isSep = [&](char16_t c) {
        if (!separators || separators->get_Length() == 0) return IsWhite(c);
        for (char16_t x : separators->vec())
            if (x == c) return true;
        return false;
    };
    size_t start = 0;
    for (size_t i = 0; i <= s_.size(); i++) {
        if (i == s_.size() || isSep(s_[i])) {
            parts.push_back(New(s_.substr(start, i - start)));
            start = i + 1;
        }
    }
    return Array<String*>::FromVector(std::move(parts));
}
Array<char16_t>* String::ToCharArray() { return Array<char16_t>::FromVector(std::vector<char16_t>(s_.begin(), s_.end())); }

// ---- Concat / Join / Format -------------------------------------------------------------------------

namespace {
const std::u16string& Str(String* s)
{
    static const std::u16string empty;
    return s ? s->str() : empty;
}
} // namespace

String* String::Concat(String* a, String* b)
{
    if (IsNullOrEmpty(a)) return IsNullOrEmpty(b) ? get_Empty() : b;
    if (IsNullOrEmpty(b)) return a;
    return New(a->s_ + b->s_);
}
String* String::Concat(String* a, String* b, String* c) { return New(Str(a) + Str(b) + Str(c)); }
String* String::Concat(String* a, String* b, String* c, String* d) { return New(Str(a) + Str(b) + Str(c) + Str(d)); }
String* String::Concat(Array<String*>* values)
{
    if (!values) ThrowArgumentNull("values");
    std::u16string r;
    for (String* s : values->vec()) r += Str(s);
    return New(std::move(r));
}
String* String::Concat(Object* a) { return ToStringOrEmpty(a); }
String* String::Concat(Object* a, Object* b) { return Concat(ToStringOrEmpty(a), ToStringOrEmpty(b)); }
String* String::Concat(Object* a, Object* b, Object* c) { return Concat(ToStringOrEmpty(a), ToStringOrEmpty(b), ToStringOrEmpty(c)); }
String* String::Concat(Object* a, Object* b, Object* c, Object* d)
{
    return Concat(ToStringOrEmpty(a), ToStringOrEmpty(b), ToStringOrEmpty(c), ToStringOrEmpty(d));
}
String* String::Concat(Array<Object*>* values)
{
    if (!values) ThrowArgumentNull("args");
    std::u16string r;
    for (Object* o : values->vec()) r += Str(ToStringOrEmpty(o));
    return New(std::move(r));
}
String* String::Join(String* separator, Array<String*>* values)
{
    if (!values) ThrowArgumentNull("value");
    std::u16string r;
    for (int32_t i = 0; i < values->get_Length(); i++) {
        if (i > 0) r += Str(separator);
        r += Str(values->at(i));
    }
    return New(std::move(r));
}

// String.Format with {index} and {index,alignment} items (no format strings yet: a gap).
String* String::Format(String* format, Array<Object*>* args)
{
    if (!format || !args) ThrowArgumentNull(format ? "args" : "format");
    const std::u16string& f = format->s_;
    std::u16string r;
    for (size_t i = 0; i < f.size(); i++) {
        char16_t c = f[i];
        if (c == u'{') {
            if (i + 1 < f.size() && f[i + 1] == u'{') {
                r.push_back(u'{');
                i++;
                continue;
            }
            size_t j = i + 1;
            int32_t index = 0;
            if (j >= f.size() || f[j] < u'0' || f[j] > u'9') Throw<FormatException>();
            while (j < f.size() && f[j] >= u'0' && f[j] <= u'9') index = index * 10 + (f[j++] - u'0');
            int32_t align = 0;
            bool left = false;
            if (j < f.size() && f[j] == u',') {
                j++;
                if (j < f.size() && f[j] == u'-') left = true, j++;
                while (j < f.size() && f[j] >= u'0' && f[j] <= u'9') align = align * 10 + (f[j++] - u'0');
            }
            if (j < f.size() && f[j] == u':') Throw<NotImplementedException>(); // format strings: not yet
            if (j >= f.size() || f[j] != u'}') Throw<FormatException>();
            if (index >= args->get_Length()) Throw<FormatException>();
            std::u16string item = Str(ToStringOrEmpty(args->at(index)));
            if (static_cast<int32_t>(item.size()) < align) {
                std::u16string pad(static_cast<size_t>(align) - item.size(), u' ');
                item = left ? item + pad : pad + item;
            }
            r += item;
            i = j;
        } else if (c == u'}') {
            if (i + 1 < f.size() && f[i + 1] == u'}') i++;
            else Throw<FormatException>();
            r.push_back(u'}');
        } else {
            r.push_back(c);
        }
    }
    return New(std::move(r));
}
String* String::Format(String* format, Object* a) { return Format(format, Array<Object*>::Of({a})); }
String* String::Format(String* format, Object* a, Object* b) { return Format(format, Array<Object*>::Of({a, b})); }
String* String::Format(String* format, Object* a, Object* b, Object* c) { return Format(format, Array<Object*>::Of({a, b, c})); }

// ---- numbers -------------------------------------------------------------------------------------------

String* ToString(int32_t v) { return String::FromAscii(std::to_string(v).c_str()); }
String* ToString(int64_t v) { return String::FromAscii(std::to_string(v).c_str()); }
String* ToString(uint32_t v) { return String::FromAscii(std::to_string(v).c_str()); }
String* ToString(uint64_t v) { return String::FromAscii(std::to_string(v).c_str()); }
String* ToString(bool v) { return String::FromAscii(v ? "True" : "False"); }
String* ToString(char16_t v) { return String::New(std::u16string(1, v)); }

namespace {
// "G<precision>" of a finite double, as .NET (Mono's NumberFormatter) writes it: the value
// rounded to `precision` significant digits (half away from zero, on its first 17 digits),
// trailing zeros dropped, scientific ("E+XX") when the exponent is >= precision or < -5.
std::string FormatGeneral(double d, int precision)
{
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
    if (d == 0.0) return std::signbit(d) ? "-0" : "0";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.16e", std::fabs(d)); // d.dddddddddddddddde[+-]XX: 17 digits
    std::string digits;
    digits.push_back(buf[0]);
    for (int i = 2; i < 18; i++) digits.push_back(buf[i]);
    int exp = std::atoi(buf + 19);
    // round to `precision` digits, half away from zero
    if (static_cast<int>(digits.size()) > precision) {
        bool up = digits[static_cast<size_t>(precision)] >= '5';
        digits.resize(static_cast<size_t>(precision));
        if (up) {
            int i = precision - 1;
            for (; i >= 0; i--) {
                if (digits[static_cast<size_t>(i)] == '9') digits[static_cast<size_t>(i)] = '0';
                else {
                    digits[static_cast<size_t>(i)]++;
                    break;
                }
            }
            if (i < 0) {
                digits.insert(digits.begin(), '1');
                digits.pop_back();
                exp++;
            }
        }
    }
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    std::string out = d < 0 ? "-" : "";
    if (exp >= precision || exp < -5) {
        out += digits[0];
        if (digits.size() > 1) out += "." + digits.substr(1);
        char e[16];
        std::snprintf(e, sizeof e, "E%c%02d", exp < 0 ? '-' : '+', exp < 0 ? -exp : exp);
        out += e;
    } else if (exp < 0) {
        out += "0." + std::string(static_cast<size_t>(-exp - 1), '0') + digits;
    } else {
        if (static_cast<int>(digits.size()) <= exp + 1) out += digits + std::string(static_cast<size_t>(exp + 1) - digits.size(), '0');
        else out += digits.substr(0, static_cast<size_t>(exp + 1)) + "." + digits.substr(static_cast<size_t>(exp + 1));
    }
    return out;
}
} // namespace

String* ToString(float v) { return String::FromAscii(FormatGeneral(static_cast<double>(v), 7).c_str()); }
String* ToString(double v) { return String::FromAscii(FormatGeneral(v, 15).c_str()); }

namespace {
bool ParseAsciiNumber(String* s, std::string& out)
{
    if (!s) ThrowArgumentNull("s");
    out.clear();
    for (char16_t c : s->str()) {
        if (c > 127) return false;
        out.push_back(static_cast<char>(c));
    }
    size_t b = out.find_first_not_of(" \t\r\n");
    size_t e = out.find_last_not_of(" \t\r\n");
    if (b == std::string::npos) return false;
    out = out.substr(b, e - b + 1);
    return true;
}
} // namespace

bool TryParseInt32(String* s, int32_t& result)
{
    result = 0;
    std::string a;
    if (!s || !ParseAsciiNumber(s, a)) return false;
    size_t i = 0;
    bool neg = false;
    if (i < a.size() && (a[i] == '-' || a[i] == '+')) neg = a[i++] == '-';
    if (i >= a.size()) return false;
    int64_t v = 0;
    for (; i < a.size(); i++) {
        if (a[i] < '0' || a[i] > '9') return false;
        v = v * 10 + (a[i] - '0');
        if (v > 2147483648ll) return false;
    }
    if (neg) v = -v;
    if (v > 2147483647ll || v < -2147483648ll) return false;
    result = static_cast<int32_t>(v);
    return true;
}

int32_t ParseInt32(String* s)
{
    if (!s) ThrowArgumentNull("s");
    int32_t r;
    if (!TryParseInt32(s, r)) Throw<FormatException>();
    return r;
}

bool TryParseDouble(String* s, double& result)
{
    result = 0.0;
    std::string a;
    if (!s || !ParseAsciiNumber(s, a)) return false;
    char* end = nullptr;
    double v = std::strtod(a.c_str(), &end);
    if (!end || *end != '\0') return false;
    result = v;
    return true;
}

double ParseDouble(String* s)
{
    if (!s) ThrowArgumentNull("s");
    double r;
    if (!TryParseDouble(s, r)) Throw<FormatException>();
    return r;
}

} // namespace cs
