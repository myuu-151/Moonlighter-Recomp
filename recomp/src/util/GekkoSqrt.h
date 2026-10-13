#pragma once
// Exact square roots for the GameCube's Gekko (a PowerPC 750), which has no fsqrt.
//
// newlib's sqrt and sqrtf (devkitPPC's libm.a: e_sqrt.c, ef_sqrt.c) compute the root a bit at a
// time in integer registers, about 890 and 275 instructions per call; in a Dolphin profile of the
// translated BabySlime replay that was 26% of PolyNav's LateUpdate. The game's own square roots
// (Math.Sqrt, Mathf.Sqrt, Vector2.magnitude on x86, Box2D's sqrtf) are the correctly rounded
// IEEE ones, and so are newlib's. This is the same function, rounded to nearest even, bit for bit,
// from the Gekko's reciprocal square root estimate (frsqrte) and fused multiply-adds (fmadd):
//
//   r = frsqrte(x)                          1/sqrt(x) within 1.8451e-4
//   one cubic step on the estimate          sqrt(x) and 1/(2 sqrt(x)) within 1.6e-11
//   one Newton step on the root             within 2^-71 of sqrt(x) before its rounding: y
//   y, its predecessor or its successor     whichever the signs of two exact residuals pick
//
// GekkoSqrt.cpp makes them the GameCube's sqrt and sqrtf (with the hardware frsqrte); the PC test
// recomp/tests/gekko_sqrt_test.cpp runs the same code with Dolphin's emulation of frsqrte against
// the PC's sqrt. The estimate is a parameter: anything callable as double(double), giving
// 1/sqrt(x) within 1/1000 for every x in [2^-900, 2^900] (see the margin below).
//
// Why the result is exact, for every double. Notation: s = sqrt(x), u = 2^-53, RN = rounding to
// nearest even; in Core, x is in [2^-900, 2^900] (Sqrt scales the rest in by even powers of two),
// where every operation below is a single rounding, RN(v) = v (1 + a) with |a| <= u, of a value
// that neither overflows nor underflows (each nonzero intermediate is at least 2^-1008: no
// subnormals, so the Gekko's non-IEEE mode, which libogc switches on, never applies).
//
// 1. The estimate. frsqrte depends only on the parity of x's exponent and the top 15 bits of its
//    mantissa (and frsqrte(4x) = frsqrte(x)/2): 2^16 classes, within which r sqrt(x) is monotonic,
//    so the ends of the classes bound the error. Over all of them r = (1 + E) / s with
//    |E| < kEstimateError = 1.8451e-4 (the test computes it from Dolphin's emulation of the
//    instruction).
// 2. One cubic step. t = RN(x r) = x r (1 + a1). Let e = 1 - t r (exact; |e| < 2E + E^2 + 2u <
//    3.6906e-4). Since t r = 1 - e, t (1 - e)^(-1/2) = sqrt(t / r) = s sqrt(1 + a1), and
//    (1 - e)^(-1/2) = 1 + e/2 + 3e^2/8 + R with |R| <= (5/16) |e|^3 / (1 - |e|) < 1.572e-11 (the
//    series' coefficients decrease from 5/16 on). The code computes em = RN(t r - 1) = -e (1 + a2),
//    q = RN(3/8 em - 1/2), tm = RN(t em) and g = RN(t + tm q): the roundings of em, q and tm are
//    each relative u on a term below 1.9e-4 t, and g's own is u, so g = s (1 + eg) with
//    |eg| < 1.0002 |R| + 1.6u < 1.6e-11. The same with r/2 for t gives h = (1 + eh) / (2 s),
//    |eh| < 1.6e-11 (the code keeps -h, so that every fma is an fmadd or an fmsub).
// 3. One Newton step. D = RN(g^2 - x) = (g^2 - x)(1 + a3) and z = g - h D, exact inside the last
//    fma, y = RN(z). Algebra: (z - s) / s = eg (1 - (1 + eg/2)(1 + eh)(1 + a3)), so
//    |z - s| < s 1.6e-11 (0.8e-11 + 1.6e-11 + 1.2e-16) < s 2^-71.
// 4. RN(s) is y or a neighbour of y. Let w = ulp(s) (the spacing of the doubles in s's binade, so
//    w > s 2^-53 and |z - s| < w/4), and d0 <= s <= d1 the two doubles of that binade around s.
//    If RN(s) = d0, then s <= d0 + w/2 and d0 - w/4 < z < d0 + 3w/4; if RN(s) = d1, then
//    d0 + w/4 < z < d1 + w/4. The doubles next to d0 and d1 outside [d0, d1] are at least w/2
//    away, so in both cases y = RN(z) is d0 or d1: RN(s) is y, its predecessor or its successor.
// 5. The choice. p = RN(y - y C) and n = RN(y + y C), C = 1.25 * 2^-53, are y's predecessor and
//    successor: with v the spacing above y, y C is in [0.625 v, 1.25 v), so y + y C is within
//    0.375 v of y + v and at least 0.625 v from any other double; the same below y, where the
//    spacing is v, or v/2 if y is a power of two (and then y C = 1.25 v/2). Now let v be n - y.
//    y and n are multiples of v and x of v^2 (x > y^2/4), so x - y n is a multiple of v^2 and
//        RN(s) = n  <=>  s > (y + n)/2  <=>  x > y n + v^2/4  <=>  x - y n > 0;
//    likewise with v = y - p,
//        RN(s) = p  <=>  s < (p + y)/2  <=>  x < p y + v^2/4  <=>  x - y p <= 0.
//    s is never a midpoint (x - y n = v^2/4 isn't a multiple of v^2), and the fma's single
//    rounding keeps the sign of the residual and its zero (it is 0 or at least v^2 >= 2^-1008).
//    So the result is RN(s) for every x in [2^-900, 2^900], hence (by the exact scalings) for
//    every positive double. The test runs every float input, and doubles next to midpoints.
//
// The margin is large: any estimate within 1/1000 gives |eg|, |eh| < 2.6e-9 and
// |z - s| < s 1.1e-17 < s 2^-55, which is all step 4 needs (the test checks it with a perturbed
// estimate).
//
// Sqrt and Sqrtf return exactly what newlib's sqrt and sqrtf return on the Gekko for every input,
// including NaNs, infinities, zeros, negatives (with errno = EDOM) and subnormals.

#include <cerrno>
#include <cfloat>
#include <cstdint>
#include <cstring>
#if !defined(__GNUC__)
#include <cmath>
#endif

#if defined(__GNUC__)
#define GEKKO_SQRT_INLINE inline __attribute__((always_inline))
#define GEKKO_SQRT_NOINLINE __attribute__((noinline))
#else
#define GEKKO_SQRT_INLINE inline
#define GEKKO_SQRT_NOINLINE __declspec(noinline)
#endif

namespace gekko_sqrt {

// The bound of step 1 above on frsqrte's error (Dolphin's table: 1.84505e-4).
constexpr double kEstimateError = 1.8451e-4;
// Core's domain: everything inside is exact without subnormal or overflowing intermediates.
constexpr double kLo = 0x1p-900;
constexpr double kHi = 0x1p900;
// C of step 5.
constexpr double kStep = 0x1.4p-53;

// a * b + c with one rounding: an fmadd (an fmsub for c = -z) on the Gekko, even with
// -ffp-contract=off.
GEKKO_SQRT_INLINE double Fma(double a, double b, double c)
{
#if defined(__GNUC__)
    return __builtin_fma(a, b, c);
#else
    return std::fma(a, b, c);
#endif
}

GEKKO_SQRT_INLINE float Fmaf(float a, float b, float c)
{
#if defined(__GNUC__)
    return __builtin_fmaf(a, b, c);
#else
    return std::fma(a, b, c);
#endif
}

// a >= 0 ? b : c (a is never a NaN here): an fsel on the Gekko, no branch.
GEKKO_SQRT_INLINE double Select(double a, double b, double c)
{
#if defined(GEKKO)
    double d;
    __asm__("fsel %0,%1,%2,%3" : "=f"(d) : "f"(a), "f"(b), "f"(c));
    return d;
#else
    return a >= 0.0 ? b : c;
#endif
}

inline uint64_t Bits(double x)
{
    uint64_t b;
    std::memcpy(&b, &x, sizeof b);
    return b;
}

inline double FromBits(uint64_t b)
{
    double x;
    std::memcpy(&x, &b, sizeof x);
    return x;
}

#if defined(GEKKO)
// The Gekko's reciprocal square root estimate (Dolphin: ApproximateReciprocalSquareRoot).
struct Frsqrte {
    double operator()(double x) const
    {
        double r;
        __asm__("frsqrte %0,%1" : "=f"(r) : "f"(x));
        return r;
    }
};
#endif

// Steps 1 and 2: g ~ sqrt(x) and h_neg ~ -1/(2 sqrt(x)), both within 1.6e-11.
struct Estimate {
    double g;
    double h_neg;
};

template <class Rsqrte>
GEKKO_SQRT_INLINE Estimate Refine(double x, Rsqrte rsqrte)
{
    const double r = rsqrte(x);
    const double t = x * r;
    const double hn = -0.5 * r;              // exact
    const double em = Fma(t, r, -1.0);       // -e
    const double q = Fma(em, 0.375, -0.5);   // -(1/2 + 3e/8)
    const double tm = t * em;                // -t e
    const double hm = hn * em;               // r/2 e
    return {Fma(tm, q, t), Fma(hm, q, hn)};  // t (1 + e/2 + 3e^2/8), -r/2 (1 + e/2 + 3e^2/8)
}

// Step 3: y = RN(g + h (x - g^2)), within 2^-71 of sqrt(x) before its rounding.
GEKKO_SQRT_INLINE double Newton(double x, Estimate e)
{
    const double dm = Fma(e.g, e.g, -x);
    return Fma(e.h_neg, dm, e.g);
}

// Steps 4 and 5: RN(sqrt(x)) from y, which is it or one of its neighbours.
GEKKO_SQRT_INLINE double Round(double x, double y)
{
    const double p = Fma(y, -kStep, y);      // predecessor
    const double n = Fma(y, kStep, y);       // successor
    const double below = Fma(y, p, -x);      // >= 0: sqrt(x) below (p + y)/2
    const double above = Fma(y, n, -x);      // < 0: sqrt(x) above (y + n)/2
    return Select(above, Select(below, p, y), n);
}

// RN(sqrt(x)) for x in [kLo, kHi].
template <class Rsqrte>
GEKKO_SQRT_INLINE double Core(double x, Rsqrte rsqrte)
{
    return Round(x, Newton(x, Refine(x, rsqrte)));
}

// Outside Core's domain. Classified on the bits, so a subnormal is never an operand.
template <class Rsqrte>
GEKKO_SQRT_NOINLINE double SqrtSpecial(double x, Rsqrte rsqrte)
{
    const uint64_t b = Bits(x);
    const uint64_t mag = b & 0x7FFFFFFFFFFFFFFFull;
    if (mag > 0x7FF0000000000000ull)
        return Fma(x, x, x);            // NaN: __ieee754_sqrt's x*x+x (an fmadd in libm.a)
    if (mag == 0)
        return x;                       // +-0
    if ((b >> 63) != 0) {
        // newlib's wrapper: errno = EDOM and 0.0/0.0, the same default NaN as __ieee754_sqrt's
        // (x-x)/(x-x) (also for -inf).
        errno = EDOM;
        return (x - x) / (x - x);
    }
    if (mag == 0x7FF0000000000000ull)
        return x;                       // +inf
    // Into Core's domain by an even power of two, exactly; the root (at least 2^-537) is normal,
    // so scaling it back is exact too.
    double in, back;
    if (mag < 0x0010000000000000ull) {
        // Subnormal: x = m 2^-1074 with the integer m < 2^52, made exactly as (2^52 + m) - 2^52.
        in = FromBits(0x4330000000000000ull | b) - 0x1p52;
        back = 0x1p-537;
    } else if (x < kLo) {
        in = x * 0x1p200;
        back = 0x1p-100;
    } else {
        in = x * 0x1p-200;
        back = 0x1p100;
    }
    return Core(in, rsqrte) * back;
}

// newlib's sqrt, exactly.
template <class Rsqrte>
GEKKO_SQRT_INLINE double Sqrt(double x, Rsqrte rsqrte)
{
    if (x >= kLo && x <= kHi)
        return Core(x, rsqrte);
    return SqrtSpecial(x, rsqrte);
}

GEKKO_SQRT_NOINLINE inline float SqrtfSpecial(float x)
{
    if (x != x)
        return Fmaf(x, x, x);           // NaN: __ieee754_sqrtf's x*x+x (an fmadds in libm.a)
    if (x < 0.0f) {
        errno = EDOM;                   // newlib's wrapper: 0.0f/0.0f, the same default NaN
        return (x - x) / (x - x);
    }
    return x;                           // +-0, +inf
}

// newlib's sqrtf, exactly: the double root rounded to float. The double rounding is innocuous
// because 53 >= 2*24 + 2: for a float x, a float midpoint m (25 bits) has x - m^2 a nonzero
// multiple of ulp(m)^2/4, so |sqrt(x) - m| >= ulp(m) 2^-27, while the double's rounding moves the
// root by at most sqrt(x) 2^-53 < ulp(m) 2^-28: it can neither cross m nor land on it.
// (A float subnormal is a normal double, inside Core's domain.)
template <class Rsqrte>
GEKKO_SQRT_INLINE float Sqrtf(float x, Rsqrte rsqrte)
{
    if (x > 0.0f && x <= FLT_MAX)
        return static_cast<float>(Core(static_cast<double>(x), rsqrte));
    return SqrtfSpecial(x);
}

} // namespace gekko_sqrt
