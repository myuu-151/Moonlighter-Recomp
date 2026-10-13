#pragma once
// C# primitive semantics for translated code (docs/translator_plan.md, "Semantics"):
//  - integer + - * wrap (two's complement, no C++ undefined behaviour); / and % throw
//    DivideByZeroException, and int.MinValue / -1 an OverflowException, as Mono x86-64 does;
//    shift counts are masked (& 31, & 63) as the CLI's are;
//  - floats: Mono evaluates float arithmetic in double and rounds where a float is stored
//    (local, field, element, argument, return, cast). Translated code computes a float
//    expression in double (D widens a float operand) and rounds with F;
//  - float -> integer conversions as Mono x86-64's cvttsd2si: truncation, and the "integer
//    indefinite" value (0x80000000...) for NaN and out-of-range values;
//  - GetHashCode / Equals / CompareTo of the primitives as mscorlib has them.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

#include "cs/Object.h"

namespace cs {

// ---- the float model ------------------------------------------------------------------------

inline double D(float f) { return static_cast<double>(f); }
inline double D(double d) { return d; }
inline float F(double d) { return static_cast<float>(d); }

inline float BitsToFloat(uint32_t u)
{
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
inline uint32_t FloatToBits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
inline double BitsToDouble(uint64_t u)
{
    double d;
    std::memcpy(&d, &u, 8);
    return d;
}
inline uint64_t DoubleToBits(double d)
{
    uint64_t u;
    std::memcpy(&u, &d, 8);
    return u;
}

// ---- integer arithmetic ---------------------------------------------------------------------

inline int32_t add(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b)); }
inline int32_t sub(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b)); }
inline int32_t mul(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b)); }
inline int32_t neg(int32_t a) { return static_cast<int32_t>(0u - static_cast<uint32_t>(a)); }
inline int64_t add(int64_t a, int64_t b) { return static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b)); }
inline int64_t sub(int64_t a, int64_t b) { return static_cast<int64_t>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b)); }
inline int64_t mul(int64_t a, int64_t b) { return static_cast<int64_t>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b)); }
inline int64_t neg(int64_t a) { return static_cast<int64_t>(0ull - static_cast<uint64_t>(a)); }
inline uint32_t add(uint32_t a, uint32_t b) { return a + b; }
inline uint32_t sub(uint32_t a, uint32_t b) { return a - b; }
inline uint32_t mul(uint32_t a, uint32_t b) { return a * b; }
inline uint64_t add(uint64_t a, uint64_t b) { return a + b; }
inline uint64_t sub(uint64_t a, uint64_t b) { return a - b; }
inline uint64_t mul(uint64_t a, uint64_t b) { return a * b; }

template <typename T>
inline T div(T a, T b)
{
    if (b == 0) Throw<DivideByZeroException>();
    if constexpr (std::is_signed<T>::value)
        if (b == -1) {
            if (a == std::numeric_limits<T>::min()) Throw<OverflowException>();
            return static_cast<T>(-a);
        }
    return static_cast<T>(a / b);
}
template <typename T>
inline T rem(T a, T b)
{
    if (b == 0) Throw<DivideByZeroException>();
    if constexpr (std::is_signed<T>::value)
        if (b == -1) {
            if (a == std::numeric_limits<T>::min()) Throw<OverflowException>();
            return 0;
        }
    return static_cast<T>(a % b);
}

inline int32_t shl(int32_t a, int32_t n) { return static_cast<int32_t>(static_cast<uint32_t>(a) << (n & 31)); }
inline int32_t shr(int32_t a, int32_t n) { return a >> (n & 31); } // arithmetic on every target compiler
inline uint32_t shl(uint32_t a, int32_t n) { return a << (n & 31); }
inline uint32_t shr(uint32_t a, int32_t n) { return a >> (n & 31); }
inline int64_t shl(int64_t a, int32_t n) { return static_cast<int64_t>(static_cast<uint64_t>(a) << (n & 63)); }
inline int64_t shr(int64_t a, int32_t n) { return a >> (n & 63); }
inline uint64_t shl(uint64_t a, int32_t n) { return a << (n & 63); }
inline uint64_t shr(uint64_t a, int32_t n) { return a >> (n & 63); }

// x++ / x-- / ++x / --x on any integral, float or enum lvalue (enums step their underlying value).
template <typename T>
inline T step(T v, int d)
{
    if constexpr (std::is_enum<T>::value) {
        using U = typename std::underlying_type<T>::type;
        return static_cast<T>(step(static_cast<U>(v), d));
    } else if constexpr (std::is_floating_point<T>::value) {
        return static_cast<T>(static_cast<double>(v) + d);
    } else if constexpr (std::is_same<T, int32_t>::value || std::is_same<T, int64_t>::value) {
        return add(v, static_cast<T>(d));
    } else {
        using U = typename std::make_unsigned<T>::type;
        return static_cast<T>(static_cast<U>(static_cast<U>(v) + static_cast<U>(d)));
    }
}
template <typename T>
inline T postinc(T& x)
{
    T o = x;
    x = step(o, 1);
    return o;
}
template <typename T>
inline T postdec(T& x)
{
    T o = x;
    x = step(o, -1);
    return o;
}
template <typename T>
inline T preinc(T& x)
{
    x = step(x, 1);
    return x;
}
template <typename T>
inline T predec(T& x)
{
    x = step(x, -1);
    return x;
}

// ---- conversions ------------------------------------------------------------------------------

// conv.i4 / conv.i8 of a float or double (cvttsd2si: NaN and out-of-range give the minimum).
inline int32_t conv_i4(double d)
{
    if (!(d > -2147483649.0 && d < 2147483648.0)) return std::numeric_limits<int32_t>::min();
    return static_cast<int32_t>(d);
}
inline int64_t conv_i8(double d)
{
    if (!(d >= -9223372036854775808.0 && d < 9223372036854775808.0)) return std::numeric_limits<int64_t>::min();
    return static_cast<int64_t>(d);
}
inline int16_t conv_i2(double d) { return static_cast<int16_t>(conv_i4(d)); }
inline int8_t conv_i1(double d) { return static_cast<int8_t>(conv_i4(d)); }
inline uint16_t conv_u2(double d) { return static_cast<uint16_t>(conv_i4(d)); }
inline uint8_t conv_u1(double d) { return static_cast<uint8_t>(conv_i4(d)); }
inline uint32_t conv_u4(double d) { return static_cast<uint32_t>(conv_i8(d)); }
inline char16_t conv_c2(double d) { return static_cast<char16_t>(conv_i4(d)); }
inline uint64_t conv_u8(double d)
{
    if (d >= 9223372036854775808.0 && d < 18446744073709551616.0)
        return static_cast<uint64_t>(conv_i8(d - 9223372036854775808.0)) + 9223372036854775808ull;
    return static_cast<uint64_t>(conv_i8(d));
}
// long -> float goes through double on Mono (cvtsi2sd, then the float store).
inline float conv_r4(int64_t v) { return static_cast<float>(static_cast<double>(v)); }
inline float conv_r4(uint64_t v) { return static_cast<float>(static_cast<double>(v)); }

// ---- System.Math ------------------------------------------------------------------------------

namespace Math {
inline double Sqrt(double d) { return std::sqrt(d); }
inline double Pow(double x, double y) { return std::pow(x, y); }
inline double Floor(double d) { return std::floor(d); }
inline double Ceiling(double d) { return std::ceil(d); }
inline double Truncate(double d) { return std::trunc(d); }
inline double Sin(double d) { return std::sin(d); }
inline double Cos(double d) { return std::cos(d); }
inline double Tan(double d) { return std::tan(d); }
inline double Asin(double d) { return std::asin(d); }
inline double Acos(double d) { return std::acos(d); }
inline double Atan(double d) { return std::atan(d); }
inline double Atan2(double y, double x) { return std::atan2(y, x); }
inline double Exp(double d) { return std::exp(d); }
inline double Log(double d) { return std::log(d); }
inline double Log(double a, double b) { return std::log(a) / std::log(b); }
inline double Log10(double d) { return std::log10(d); }
// Math.Round(double): to even (the default rounding mode).
inline double Round(double d) { return std::nearbyint(d); }
inline double Round(double d, int32_t digits)
{
    if (digits == 0) return std::nearbyint(d);
    double p = std::pow(10.0, digits);
    return std::nearbyint(d * p) / p;
}
inline double Abs(double d) { return std::fabs(d); }
inline float Abs(float f) { return std::fabs(f); }
inline int32_t Abs(int32_t v)
{
    if (v == std::numeric_limits<int32_t>::min()) Throw<OverflowException>();
    return v < 0 ? -v : v;
}
inline int64_t Abs(int64_t v)
{
    if (v == std::numeric_limits<int64_t>::min()) Throw<OverflowException>();
    return v < 0 ? -v : v;
}
inline int32_t Max(int32_t a, int32_t b) { return a > b ? a : b; }
inline int32_t Min(int32_t a, int32_t b) { return a < b ? a : b; }
inline int64_t Max(int64_t a, int64_t b) { return a > b ? a : b; }
inline int64_t Min(int64_t a, int64_t b) { return a < b ? a : b; }
inline uint32_t Max(uint32_t a, uint32_t b) { return a > b ? a : b; }
inline uint32_t Min(uint32_t a, uint32_t b) { return a < b ? a : b; }
// Math.Max(double, double): NaN wins (mscorlib: if (a > b) return a; if (IsNaN(a)) return a; return b).
inline double Max(double a, double b)
{
    if (a > b) return a;
    if (std::isnan(a)) return a;
    return b;
}
inline double Min(double a, double b)
{
    if (a < b) return a;
    if (std::isnan(a)) return a;
    return b;
}
inline float Max(float a, float b)
{
    if (a > b) return a;
    if (std::isnan(a)) return a;
    return b;
}
inline float Min(float a, float b)
{
    if (a < b) return a;
    if (std::isnan(a)) return a;
    return b;
}
inline int32_t Sign(double d)
{
    if (std::isnan(d)) Throw<ArithmeticException>();
    return d > 0 ? 1 : d < 0 ? -1 : 0;
}
inline int32_t Sign(int32_t v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }
constexpr double PI = 3.14159265358979323846;
constexpr double E = 2.7182818284590452354;
} // namespace Math

// ---- GetHashCode / Equals / CompareTo of the primitives ------------------------------------------

inline int32_t HashOf(bool b) { return b ? 1 : 0; }
inline int32_t HashOf(int32_t v) { return v; }
inline int32_t HashOf(uint32_t v) { return static_cast<int32_t>(v); }
inline int32_t HashOf(int64_t v) { return static_cast<int32_t>(v) ^ static_cast<int32_t>(v >> 32); }
inline int32_t HashOf(uint64_t v) { return static_cast<int32_t>(v) ^ static_cast<int32_t>(v >> 32); }
inline int32_t HashOf(int16_t v) { return static_cast<int32_t>(static_cast<uint16_t>(v)) | (static_cast<int32_t>(v) << 16); }
inline int32_t HashOf(uint16_t v) { return v; }
inline int32_t HashOf(int8_t v) { return static_cast<int32_t>(static_cast<uint8_t>(v)) | (static_cast<int32_t>(v) << 8); }
inline int32_t HashOf(uint8_t v) { return v; }
inline int32_t HashOf(char16_t c) { return static_cast<int32_t>(c) | (static_cast<int32_t>(c) << 16); }
// float.GetHashCode: the bits, 0 for both zeros.
inline int32_t HashOf(float f) { return f == 0.0f ? 0 : static_cast<int32_t>(FloatToBits(f)); }
inline int32_t HashOf(double d)
{
    if (d == 0.0) return 0;
    uint64_t u = DoubleToBits(d);
    return static_cast<int32_t>(u) ^ static_cast<int32_t>(u >> 32);
}

// float.Equals(float): == or both NaN.
inline bool FloatEquals(float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); }
inline bool DoubleEquals(double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); }

template <typename T>
inline int32_t CompareTo(T a, T b)
{
    if constexpr (std::is_floating_point<T>::value) {
        // float.CompareTo: NaN is less than everything, equal to itself.
        if (a < b) return -1;
        if (a > b) return 1;
        if (a == b) return 0;
        if (std::isnan(a)) return std::isnan(b) ? 0 : -1;
        return 1;
    } else if constexpr (std::is_same<T, bool>::value) {
        return a == b ? 0 : (a ? 1 : -1);
    } else if constexpr (std::is_enum<T>::value) {
        using U = typename std::underlying_type<T>::type;
        return CompareTo(static_cast<U>(a), static_cast<U>(b));
    } else if constexpr (sizeof(T) < 4) {
        return static_cast<int32_t>(a) - static_cast<int32_t>(b); // byte/short/char: the difference
    } else {
        return a < b ? -1 : (a > b ? 1 : 0);
    }
}

} // namespace cs
