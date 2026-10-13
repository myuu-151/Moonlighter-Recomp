#include "cs/Text.h"

#include <cmath>
#include <cstdlib>
#include <string>

namespace cs {

bool Char::IsWhiteSpace(char16_t c)
{
    return c == u' ' || (c >= 0x09 && c <= 0x0D) || c == 0xA0 || c == 0x85 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
           c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

String* Environment::get_NewLine()
{
    static String* nl = String::Literal(u"\r\n", 2);
    return nl;
}

CultureInfo* CultureInfo::get_InvariantCulture()
{
    static CultureInfo* c = new CultureInfo(); // permanent, like the interned strings
    return c;
}
CultureInfo* CultureInfo::get_CurrentCulture() { return get_InvariantCulture(); }

// ---- parsing ---------------------------------------------------------------------------------------

namespace {

bool Has(NumberStyles s, NumberStyles f) { return (static_cast<int32_t>(s) & static_cast<int32_t>(f)) != 0; }

struct Parsed {
    bool negative = false;
    std::string digits; // significant digits, no leading zeros
    int exponent = 0;   // value = 0.digits * 10^exponent
    bool fractionNonZero = false;
};

bool IsWhite(char16_t c) { return c == 0x20 || (c >= 0x09 && c <= 0x0D); }

// Number.ParseNumber with the invariant culture's symbols ("-", "+", ".", ",", "¤").
bool ParseNumber(const std::u16string& s, NumberStyles styles, Parsed& p)
{
    size_t i = 0, n = s.size();
    if (Has(styles, NumberStyles::AllowLeadingWhite))
        while (i < n && IsWhite(s[i])) i++;
    bool paren = false, signSeen = false;
    // leading sign / parenthesis / currency, in any order once
    for (;;) {
        if (i < n && !signSeen && Has(styles, NumberStyles::AllowLeadingSign) && (s[i] == u'-' || s[i] == u'+')) {
            p.negative = s[i] == u'-';
            signSeen = true;
            i++;
            continue;
        }
        if (i < n && !paren && !signSeen && Has(styles, NumberStyles::AllowParentheses) && s[i] == u'(') {
            paren = true;
            p.negative = true;
            i++;
            continue;
        }
        if (i < n && Has(styles, NumberStyles::AllowCurrencySymbol) && s[i] == 0xA4) {
            i++;
            continue;
        }
        break;
    }
    bool anyDigit = false, pointSeen = false;
    int intDigits = 0;
    for (; i < n; i++) {
        char16_t c = s[i];
        if (c >= u'0' && c <= u'9') {
            anyDigit = true;
            if (p.digits.empty() && c == u'0') {
                if (pointSeen) p.exponent--;
                continue;
            }
            if (pointSeen) {
                p.digits.push_back(static_cast<char>(c));
            } else {
                p.digits.push_back(static_cast<char>(c));
                intDigits++;
            }
            continue;
        }
        if (c == u'.' && !pointSeen && Has(styles, NumberStyles::AllowDecimalPoint)) {
            pointSeen = true;
            continue;
        }
        if (c == u',' && !pointSeen && Has(styles, NumberStyles::AllowThousands) && anyDigit) continue;
        break;
    }
    if (!anyDigit) return false;
    p.exponent += intDigits;
    if (i < n && (s[i] == u'e' || s[i] == u'E') && Has(styles, NumberStyles::AllowExponent)) {
        size_t j = i + 1;
        bool eneg = false;
        if (j < n && (s[j] == u'-' || s[j] == u'+')) eneg = s[j++] == u'-';
        if (j < n && s[j] >= u'0' && s[j] <= u'9') {
            long e = 0;
            while (j < n && s[j] >= u'0' && s[j] <= u'9') {
                if (e < 100000) e = e * 10 + (s[j] - u'0');
                j++;
            }
            p.exponent += static_cast<int>(eneg ? -e : e);
            i = j;
        }
    }
    // trailing sign / parenthesis / currency / white
    for (;;) {
        if (i < n && !signSeen && Has(styles, NumberStyles::AllowTrailingSign) && (s[i] == u'-' || s[i] == u'+')) {
            p.negative = s[i] == u'-';
            signSeen = true;
            i++;
            continue;
        }
        if (i < n && paren && s[i] == u')') {
            paren = false;
            i++;
            continue;
        }
        if (i < n && Has(styles, NumberStyles::AllowCurrencySymbol) && s[i] == 0xA4) {
            i++;
            continue;
        }
        break;
    }
    if (Has(styles, NumberStyles::AllowTrailingWhite))
        while (i < n && IsWhite(s[i])) i++;
    if (paren || i != n) return false;
    while (!p.digits.empty() && p.digits.back() == '0') p.digits.pop_back();
    if (p.digits.empty()) p.exponent = 0;
    return true;
}

std::u16string Trimmed(const std::u16string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && IsWhite(s[b])) b++;
    while (e > b && IsWhite(s[e - 1])) e--;
    return s.substr(b, e - b);
}

} // namespace

bool TryParseInt64(String* s, NumberStyles styles, IFormatProvider*, int64_t& result)
{
    result = 0;
    if (!s) return false;
    Parsed p;
    if (!ParseNumber(s->str(), styles, p)) return false;
    if (p.digits.empty()) return true;
    // the value must be an integer: no significant digit after the point
    if (static_cast<int>(p.digits.size()) > p.exponent) return false;
    if (p.exponent > 19) return false;
    unsigned long long v = 0;
    for (int k = 0; k < p.exponent; k++) {
        int d = k < static_cast<int>(p.digits.size()) ? p.digits[static_cast<size_t>(k)] - '0' : 0;
        if (v > (18446744073709551615ull - static_cast<unsigned>(d)) / 10ull) return false;
        v = v * 10 + static_cast<unsigned>(d);
    }
    if (p.negative) {
        if (v > 9223372036854775808ull) return false;
        result = v == 9223372036854775808ull ? INT64_MIN : -static_cast<int64_t>(v);
    } else {
        if (v > 9223372036854775807ull) return false;
        result = static_cast<int64_t>(v);
    }
    return true;
}

bool TryParseInt32(String* s, NumberStyles styles, IFormatProvider* provider, int32_t& result)
{
    int64_t v;
    result = 0;
    if (!TryParseInt64(s, styles, provider, v) || v < INT32_MIN || v > INT32_MAX) return false;
    result = static_cast<int32_t>(v);
    return true;
}

bool TryParseDouble(String* s, NumberStyles styles, IFormatProvider*, double& result)
{
    result = 0.0;
    if (!s) return false;
    Parsed p;
    if (!ParseNumber(s->str(), styles, p)) {
        std::u16string t = Trimmed(s->str());
        if (t == u"Infinity") result = INFINITY;
        else if (t == u"-Infinity") result = -INFINITY;
        else if (t == u"NaN") result = NAN;
        else return false;
        return true;
    }
    if (p.digits.empty()) {
        result = p.negative ? -0.0 : 0.0;
        return true;
    }
    std::string text = std::string(p.negative ? "-" : "") + "0." + p.digits + "e" + std::to_string(p.exponent);
    result = std::strtod(text.c_str(), nullptr);
    return true;
}

bool TryParseSingle(String* s, NumberStyles styles, IFormatProvider* provider, float& result)
{
    double d;
    result = 0.0f;
    if (!TryParseDouble(s, styles, provider, d)) return false;
    result = static_cast<float>(d);
    return true;
}

double ParseDouble(String* s, NumberStyles styles, IFormatProvider* provider)
{
    if (!s) ThrowArgumentNull("s");
    double d;
    if (!TryParseDouble(s, styles, provider, d)) Throw<FormatException>();
    return d;
}

float ParseSingle(String* s) { return static_cast<float>(ParseDouble(s, static_cast<NumberStyles>(231), nullptr)); }
bool TryParseSingle(String* s, float& result) { return TryParseSingle(s, static_cast<NumberStyles>(231), nullptr, result); }
int64_t ParseInt64(String* s)
{
    if (!s) ThrowArgumentNull("s");
    int64_t v;
    if (!TryParseInt64(s, NumberStyles::Integer, nullptr, v)) Throw<FormatException>();
    return v;
}

// ---- StringComparer -------------------------------------------------------------------------------------

StringComparer* StringComparer::get_Ordinal()
{
    static StringComparer* c = new StringComparer(false);
    return c;
}
StringComparer* StringComparer::get_OrdinalIgnoreCase()
{
    static StringComparer* c = new StringComparer(true);
    return c;
}

namespace {
char16_t Up(char16_t c) { return (c >= u'a' && c <= u'z') ? static_cast<char16_t>(c - 32) : c; }
} // namespace

bool StringComparer::Equals(String* a, String* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    if (!ignoreCase_) return a->str() == b->str();
    if (a->str().size() != b->str().size()) return false;
    for (size_t i = 0; i < a->str().size(); i++)
        if (Up(a->str()[i]) != Up(b->str()[i])) return false;
    return true;
}

int32_t StringComparer::GetHashCode(String* s)
{
    if (!s) ThrowArgumentNull("obj");
    if (!ignoreCase_) return s->GetHashCode();
    std::u16string u = s->str();
    for (char16_t& c : u) c = Up(c);
    return String::New(u)->GetHashCode();
}

int32_t StringComparer::Compare(String* a, String* b)
{
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    if (!ignoreCase_) return String::CompareOrdinal(a, b);
    size_t n = std::min(a->str().size(), b->str().size());
    for (size_t i = 0; i < n; i++) {
        char16_t x = Up(a->str()[i]), y = Up(b->str()[i]);
        if (x != y) return static_cast<int32_t>(x) - static_cast<int32_t>(y);
    }
    return static_cast<int32_t>(a->str().size()) - static_cast<int32_t>(b->str().size());
}

} // namespace cs
