// util/GekkoSqrt.h, the GameCube's sqrt and sqrtf, run on the PC against the PC's correctly
// rounded sqrt and sqrtf (SSE2), bit for bit, with model estimates in place of the Gekko's frsqrte.
//
// GekkoSqrt.h's proof needs only a bound on the estimate's error: |r sqrt(x) - 1| < 1.8451e-4,
// the measured accuracy of the Gekko's frsqrte (1.84505e-4 at worst over every input, as Dolphin,
// which emulates the hardware's table bit for bit, computes it). So the models are estimates that
// meet the bound, and estimates at its very ends:
//   truncated   1/sqrt(x) cut to 13 bits after the point: always low, by up to 2^-13 = 1.22e-4
//   +bound      1/sqrt(x) (1 + 1.8451e-4)
//   -bound      1/sqrt(x) (1 - 1.8451e-4)
//   +1/1000     1/sqrt(x) (1 + 1e-3): GekkoSqrt.h's margin
//   -1/1000     1/sqrt(x) (1 - 1e-3)
// where 1/sqrt(x) is the PC's 1/sqrt(x), within 2^-52 of the exact value.
//
// For each model, by default (a few seconds in all): every float in [1, 4) through sqrtf and
// through sqrt of the double, with the estimate's error and the errors of the refined estimates
// against the proof's bounds; every float and double exponent at a sample of mantissas; the
// special values (NaNs, infinities, zeros, negatives with errno, the ends of Core's domain, the
// neighbours step 5 makes); subnormals; millions of random doubles; and doubles whose roots lie
// within 2^-88 of a midpoint (where the last step has to choose).
//
// Usage: gekko_sqrt_test [--all]
//   --all  also every one of the 2^32 float bit patterns, through sqrtf and through sqrt of the
//          double, with each model, on every core.

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

#include "util/GekkoSqrt.h"

using gekko_sqrt::Bits;
using gekko_sqrt::FromBits;

namespace {

// 1/sqrt(x) within 2^-52 (two roundings). Core only asks for normal x.
double InvSqrt(double x) { return 1.0 / std::sqrt(x); }

// Cut to 13 bits after the point (the other 39 of the mantissa cleared).
struct Truncated {
    double operator()(double x) const { return FromBits(Bits(InvSqrt(x)) & ~((1ull << 39) - 1)); }
};

// Off by a constant factor 1 + k.
struct Scaled {
    double k;
    double operator()(double x) const { return InvSqrt(x) * (1.0 + k); }
};

uint64_t Mix(uint64_t z)
{
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

struct Rng {
    uint64_t s;
    uint64_t Next() { return Mix(s++); }
    double Unit() { return static_cast<double>(Next() >> 11) * 0x1p-53; }
};

// ---- Counting ----

std::atomic<uint64_t> g_checks{0};
std::atomic<uint64_t> g_failures{0};
std::mutex g_print;
int g_printed = 0;

void Report(const char* what, uint64_t in, uint64_t got, uint64_t want, bool isFloat)
{
    g_failures++;
    std::lock_guard<std::mutex> lock(g_print);
    if (g_printed++ < 20) {
        if (isFloat)
            std::printf("FAIL %s: in %08llx got %08llx want %08llx\n", what, (unsigned long long)in,
                        (unsigned long long)got, (unsigned long long)want);
        else
            std::printf("FAIL %s: in %016llx got %016llx want %016llx\n", what,
                        (unsigned long long)in, (unsigned long long)got, (unsigned long long)want);
    }
}

uint32_t FBits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, sizeof b);
    return b;
}

float FFromBits(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, sizeof f);
    return f;
}

// Equal bits, or both NaN (x86's NaNs aren't the Gekko's; GekkoSqrt.h makes the Gekko's with the
// same instructions as newlib).
bool SameD(double a, double b) { return (std::isnan(a) && std::isnan(b)) || Bits(a) == Bits(b); }
bool SameF(float a, float b) { return (std::isnan(a) && std::isnan(b)) || FBits(a) == FBits(b); }

// One double through Sqrt; errno must be EDOM for a negative (non-NaN) input, untouched otherwise.
// Returns the number of failures (0 or 1); counts are kept by the caller in bulk.
template <class R>
int CheckD(double x, R rsqrte, const char* what)
{
    errno = 0;
    const double got = gekko_sqrt::Sqrt(x, rsqrte);
    const int err = errno;
    const double want = std::sqrt(x);
    const bool negative = x < 0.0;
    if (!SameD(got, want) || err != (negative ? EDOM : 0)) {
        Report(what, Bits(x), Bits(got), Bits(want), false);
        return 1;
    }
    return 0;
}

template <class R>
int CheckF(float f, R rsqrte, const char* what)
{
    errno = 0;
    const float got = gekko_sqrt::Sqrtf(f, rsqrte);
    const int err = errno;
    const float want = std::sqrt(f);
    const bool negative = f < 0.0f;
    if (!SameF(got, want) || err != (negative ? EDOM : 0)) {
        Report(what, FBits(f), FBits(got), FBits(want), true);
        return 1;
    }
    return 0;
}

// Runs body(i) for i in [begin, end) on every core, in chunks.
template <class Body>
void Parallel(uint64_t begin, uint64_t end, Body body)
{
    const unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    const uint64_t chunk = 1 << 16;
    std::atomic<uint64_t> next{begin};
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; t++)
        pool.emplace_back([&] {
            for (;;) {
                const uint64_t lo = next.fetch_add(chunk);
                if (lo >= end)
                    return;
                const uint64_t hi = std::min(end, lo + chunk);
                for (uint64_t i = lo; i < hi; i++)
                    body(i);
            }
        });
    for (auto& t : pool)
        t.join();
}

void AtomicMax(std::atomic<double>& m, double v)
{
    double cur = m.load();
    while (v > cur && !m.compare_exchange_weak(cur, v)) {
    }
}

// ---- The checks ----

// Every float in [1, 4) through sqrtf and through sqrt of its double, and for the double the
// estimate's error, the errors of Refine's g and h (GekkoSqrt.h step 2) and how often the last
// step moves y.
struct SweepStats {
    std::atomic<double> est{0.0}, eg{0.0}, eh{0.0};
    std::atomic<uint64_t> up{0}, down{0};
};

template <class R>
void Sweep14(R rsqrte, SweepStats& st, const char* what)
{
    const uint32_t first = 0x3F800000u, last = 0x40800000u; // [1, 4)
    const uint64_t count = last - first;
    Parallel(0, count, [&](uint64_t i) {
        const float f = FFromBits(first + static_cast<uint32_t>(i));
        const double x = f;
        CheckF(f, rsqrte, what);
        CheckD(x, rsqrte, what);
        AtomicMax(st.est, std::fabs(rsqrte(x) * std::sqrt(x) - 1.0));
        const gekko_sqrt::Estimate e = gekko_sqrt::Refine(x, rsqrte);
        // g = sqrt(x)(1 + eg): g^2 - x = x (2 eg + eg^2).
        const double eg = std::fma(e.g, e.g, -x) / (2.0 * x);
        // h = (1 + eh)/(2 sqrt(x)): (2h)^2 x - 1 = 2 eh + eh^2, with (2h)^2 = hh + hl exactly.
        const double h2 = -2.0 * e.h_neg;
        const double hh = h2 * h2, hl = std::fma(h2, h2, -hh);
        const double eh = (std::fma(hh, x, -1.0) + hl * x) / 2.0;
        AtomicMax(st.eg, std::fabs(eg));
        AtomicMax(st.eh, std::fabs(eh));
        const double y = gekko_sqrt::Newton(x, e);
        const double r = gekko_sqrt::Round(x, y);
        if (r > y)
            st.up++;
        else if (r < y)
            st.down++;
    });
    g_checks += 2 * count;
}

// Every float exponent (subnormals too) at a sample of mantissas, both signs; every double
// exponent likewise.
template <class R>
void Exponents(R rsqrte)
{
    std::vector<uint32_t> fm = {0, 1, 2, 3, 0x400000, 0x7FFFFD, 0x7FFFFE, 0x7FFFFF};
    for (uint32_t m = 0; m < 0x800000; m += 0x800)
        fm.push_back(m + 0x3FF);
    Parallel(0, 256, [&](uint64_t e) {
        for (uint32_t sign = 0; sign < 2; sign++)
            for (uint32_t m : fm) {
                const float f = FFromBits(sign << 31 | static_cast<uint32_t>(e) << 23 | m);
                CheckF(f, rsqrte, "float exponents");
                CheckD(f, rsqrte, "float exponents (double)");
            }
    });
    g_checks += 256 * 2 * fm.size() * 2;

    const uint64_t perExp = 1024;
    Parallel(0, 2048, [&](uint64_t e) {
        Rng rng{e * 7919 + 3};
        for (uint64_t i = 0; i < perExp; i++) {
            uint64_t m = rng.Next() & 0xFFFFFFFFFFFFFull;
            if (i < 4)
                m = i;
            else if (i < 8)
                m = 0xFFFFFFFFFFFFFull - (i - 4);
            CheckD(FromBits(e << 52 | m), rsqrte, "double exponents");
            CheckD(FromBits(1ull << 63 | e << 52 | m), rsqrte, "double exponents (negative)");
        }
    });
    g_checks += 2048 * perExp * 2;
}

template <class R>
void Specials(R rsqrte)
{
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> d = {0.0, -0.0, inf, -inf, 1.0, 2.0, 4.0, 0.25, -1.0, -0x1p-1074,
                             -0x1p-1022, -1.7976931348623157e308, 0x1p-1074, 0x1p-1073,
                             0x0.fffffffffffffp-1022, 0x1p-1022, 1.7976931348623157e308,
                             0x1.fffffffffffffp-1, 0x1.0000000000001p0, 0x1.fffffffffffffp1,
                             0x1.0000000000001p1, 9.0, 1e300, 1e-300, 1e-310};
    for (double edge : {gekko_sqrt::kLo, gekko_sqrt::kHi, 0x1p-1000, 0x1p1000}) {
        d.push_back(edge);
        d.push_back(std::nextafter(edge, 0.0));
        d.push_back(std::nextafter(edge, inf));
    }
    for (uint64_t nan : {0x7FF8000000000000ull, 0xFFF8000000000000ull, 0x7FF0000000000001ull,
                         0xFFF0000000000001ull, 0x7FFFFFFFFFFFFFFFull, 0x7FF4000000000000ull})
        d.push_back(FromBits(nan));
    for (int k = -537; k <= 511; k++) // powers of 4 and their neighbours, below and above
        for (double x : {std::ldexp(1.0, 2 * k), std::ldexp(1.0, 2 * k + 1)}) {
            if (x < 0x1p-1074 || std::isinf(x))
                continue;
            d.push_back(x);
            d.push_back(std::nextafter(x, 0.0));
            d.push_back(std::nextafter(x, inf));
        }
    for (double x : d) {
        g_checks++;
        CheckD(x, rsqrte, "special doubles");
    }
    // GekkoSqrt.h step 5: y -+ y C are y's neighbours, at every power of two (where the spacing
    // below is half the one above) and its neighbours, and at random mantissas.
    std::vector<double> ys;
    for (int k = -460; k <= 460; k++) {
        const double y = std::ldexp(1.0, k);
        ys.insert(ys.end(), {y, std::nextafter(y, 0.0), std::nextafter(y, inf)});
    }
    Rng rng{5};
    for (int i = 0; i < 100000; i++)
        ys.push_back(std::ldexp(1.0 + rng.Unit(), static_cast<int>(rng.Next() % 920) - 460));
    for (double y : ys) {
        g_checks += 2;
        const double p = gekko_sqrt::Fma(y, -gekko_sqrt::kStep, y);
        const double n = gekko_sqrt::Fma(y, gekko_sqrt::kStep, y);
        if (p != std::nextafter(y, 0.0) || n != std::nextafter(y, inf))
            Report("neighbours", Bits(y), Bits(p), Bits(n), false);
    }
    // Zeros keep their sign, +inf stays, NaNs stay NaNs.
    g_checks += 4;
    if (Bits(gekko_sqrt::Sqrt(-0.0, rsqrte)) != Bits(-0.0) || Bits(gekko_sqrt::Sqrt(0.0, rsqrte)) != 0 ||
        gekko_sqrt::Sqrt(inf, rsqrte) != inf || !std::isnan(gekko_sqrt::Sqrt(FromBits(0x7FF0000000000001ull), rsqrte)))
        Report("double zeros/inf/NaN", 0, 0, 0, false);

    const float finf = std::numeric_limits<float>::infinity();
    std::vector<float> f = {0.0f, -0.0f, finf, -finf, 1.0f, 4.0f, -1.0f, 0x1p-149f, 0x1p-148f,
                            -0x1p-149f, 0x1p-126f, 0x0.fffffep-126f, 3.40282347e38f,
                            -3.40282347e38f, 0x1.fffffep1f, 0x1.000002p0f};
    for (uint32_t nan : {0x7FC00000u, 0xFFC00000u, 0x7F800001u, 0xFF800001u, 0x7FFFFFFFu})
        f.push_back(FFromBits(nan));
    for (float x : f) {
        g_checks++;
        CheckF(x, rsqrte, "special floats");
    }
    g_checks += 4;
    if (FBits(gekko_sqrt::Sqrtf(-0.0f, rsqrte)) != FBits(-0.0f) || FBits(gekko_sqrt::Sqrtf(0.0f, rsqrte)) != 0 ||
        gekko_sqrt::Sqrtf(finf, rsqrte) != finf || !std::isnan(gekko_sqrt::Sqrtf(FFromBits(0x7F800001u), rsqrte)))
        Report("float zeros/inf/NaN", 0, 0, 0, true);
}

template <class R>
void Subnormals(R rsqrte)
{
    for (int k = 0; k < 52; k++) {
        CheckD(FromBits(1ull << k), rsqrte, "subnormal bit");
        CheckD(FromBits((2ull << k) - 1), rsqrte, "subnormal mask");
        CheckD(FromBits((1ull << k) | 1), rsqrte, "subnormal bit+1");
    }
    g_checks += 3 * 52;
    // Squares of integers, exact roots: x = n^2 2^-1074.
    for (uint64_t n = 1; n < (1ull << 26); n += 997) {
        CheckD(FromBits(n * n), rsqrte, "subnormal square");
        g_checks++;
    }
    const uint64_t count = 1000000;
    Parallel(0, count, [&](uint64_t i) {
        const uint64_t b = Mix(i * 31 + 17);
        const int shift = static_cast<int>(b >> 58); // 0..63: every magnitude
        CheckD(FromBits((b & 0xFFFFFFFFFFFFFull) >> std::min(shift, 51)), rsqrte, "subnormal random");
    });
    g_checks += count;
}

template <class R>
void RandomDoubles(R rsqrte, uint64_t count, uint64_t seed)
{
    Parallel(0, count, [&](uint64_t i) {
        Rng rng{seed * 0x100000000ull + i * 4};
        const uint64_t b = rng.Next();
        // Any bit pattern (NaNs, negatives and all); a positive double; [1, 4); an exact square.
        CheckD(FromBits(b), rsqrte, "random bits");
        CheckD(FromBits(b & 0x7FFFFFFFFFFFFFFFull), rsqrte, "random positive");
        CheckD(FromBits((0x3FF0000000000000ull + (b & 1ull << 52)) | (b & 0xFFFFFFFFFFFFFull)), rsqrte,
               "random [1, 4)");
        const uint64_t c = rng.Next();
        const double n = static_cast<double>(c & 0x3FFFFFF) + 1.0; // up to 2^26: n^2 is exact
        CheckD(std::ldexp(n * n, 2 * (static_cast<int>(c >> 40) % 950 - 500)), rsqrte, "random square");
    });
    g_checks += 4 * count;
}

// a * b as 128 bits.
void Mul128(uint64_t a, uint64_t b, uint64_t& hi, uint64_t& lo)
{
    const uint64_t a0 = a & 0xFFFFFFFF, a1 = a >> 32, b0 = b & 0xFFFFFFFF, b1 = b >> 32;
    const uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    const uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFF) + (p10 & 0xFFFFFFFF);
    lo = (mid << 32) | (p00 & 0xFFFFFFFF);
    hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

// Doubles whose roots are within |c| 2^-107 (relative) of a midpoint between two doubles: for
// c = 1 mod 8, A^2 = c mod 2^55 has odd solutions A, and with A in (2^53, 2^54) the double
// x = (A^2 - c) 2^-106 sits just off the square of the midpoint A 2^-53. Scaled by powers of 4
// across the range (Core's domain and both scaled paths). Returns how many there were.
template <class R>
uint64_t HardCases(R rsqrte, const char* what, std::atomic<uint64_t>* moved)
{
    const int scales[] = {-511, -460, -451, -450, -449, -300, -1, 0, 1, 300, 449, 450, 451, 460, 511};
    std::atomic<uint64_t> cases{0};
    const int64_t range = 1 << 16;
    Parallel(0, 2 * range, [&](uint64_t i) {
        const int64_t c = 1 + 8 * (static_cast<int64_t>(i) - range); // ..., -15, -7, 1, 9, ...
        const uint64_t mod = (1ull << 55) - 1;
        uint64_t a = 1;
        for (int k = 3; k < 55; k++) // Hensel: from A^2 = c mod 2^k to mod 2^(k+1)
            if (((a * a - static_cast<uint64_t>(c)) >> k) & 1)
                a += 1ull << (k - 1);
        a &= mod;
        for (uint64_t root : {a, (0 - a) & mod, (a + (1ull << 54)) & mod, ((1ull << 54) - a) & mod}) {
            if (root <= (1ull << 53) || root >= (1ull << 54))
                continue;
            uint64_t hi, lo;
            Mul128(root, root, hi, lo);
            // A^2 - c, then divided by 2^55 (exact).
            const uint64_t clo = static_cast<uint64_t>(c);
            const uint64_t chi = c < 0 ? ~0ull : 0;
            const uint64_t nlo = lo - clo;
            const uint64_t nhi = hi - chi - (lo < clo ? 1 : 0);
            if (nlo & mod) {
                Report("hard case construction", root, nlo, 0, false);
                continue;
            }
            const uint64_t k = (nhi << 9) | (nlo >> 55);
            const double x = std::ldexp(static_cast<double>(k), -51);
            for (int s : scales) {
                const double xs = std::ldexp(x, 2 * s);
                if (std::isinf(xs))
                    continue;
                cases++;
                CheckD(xs, rsqrte, what);
                if (moved && s == 0) {
                    const double y = gekko_sqrt::Newton(xs, gekko_sqrt::Refine(xs, rsqrte));
                    if (gekko_sqrt::Round(xs, y) != y)
                        (*moved)++;
                }
            }
        }
    });
    g_checks += cases.load();
    return cases.load();
}


// The default checks with one model: estimateBound is what the model promises, refinedBound what
// GekkoSqrt.h derives from it for g and h.
template <class R>
void Default(R rsqrte, const char* name, double estimateBound, double refinedBound)
{
    const auto start = std::chrono::steady_clock::now();
    const uint64_t before = g_failures.load();
    SweepStats st;
    Sweep14(rsqrte, st, name);
    Exponents(rsqrte);
    Specials(rsqrte);
    Subnormals(rsqrte);
    RandomDoubles(rsqrte, 500000, 1);
    std::atomic<uint64_t> moved{0};
    const uint64_t hard = HardCases(rsqrte, name, &moved);
    g_checks++;
    if (!(st.est.load() <= estimateBound + 1e-15))
        Report("estimate outside its model's bound", 0, Bits(st.est.load()), Bits(estimateBound), false);
    g_checks++;
    if (!(st.eg.load() < refinedBound && st.eh.load() < refinedBound))
        Report("refined estimate outside the proof's bound", 0, Bits(st.eg.load()), Bits(st.eh.load()), false);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("%-9s estimate within %.5e; g within %.3e, h within %.3e (proof: %.1e); last step moved "
                "y %llu+%llu times in [1, 4), %llu of %llu next to midpoints; %llu mismatches, %.1f s\n",
                name, st.est.load(), st.eg.load(), st.eh.load(), refinedBound,
                (unsigned long long)st.up.load(), (unsigned long long)st.down.load(),
                (unsigned long long)moved.load(), (unsigned long long)hard,
                (unsigned long long)(g_failures.load() - before), secs);
}

template <class R>
void All(R rsqrte, const char* name)
{
    const auto start = std::chrono::steady_clock::now();
    const uint64_t before = g_failures.load();
    Parallel(0, 1ull << 32, [&](uint64_t i) {
        const float f = FFromBits(static_cast<uint32_t>(i));
        CheckF(f, rsqrte, name);
        CheckD(f, rsqrte, name);
    });
    g_checks += 2ull << 32;
    const double secs =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("%-9s all 2^32 floats, sqrtf and sqrt of the double: %llu mismatches, %.1f s on %u threads\n",
                name, (unsigned long long)(g_failures.load() - before), secs,
                std::max(1u, std::thread::hardware_concurrency()));
}

} // namespace

int main(int argc, char** argv)
{
    const bool all = argc > 1 && std::strcmp(argv[1], "--all") == 0;
    const auto start = std::chrono::steady_clock::now();
    const double e = gekko_sqrt::kEstimateError;
    const Truncated truncated;
    const Scaled plus{e}, minus{-e}, plus1k{1e-3}, minus1k{-1e-3};

    // The proof's bounds on g and h: 1.6e-11 for an estimate within kEstimateError, 2.6e-9 within
    // 1/1000.
    Default(truncated, "truncated", 0x1p-13 + 0x1p-51, 1.6e-11);
    Default(plus, "+bound", e + 0x1p-51, 1.6e-11);
    Default(minus, "-bound", e + 0x1p-51, 1.6e-11);
    Default(plus1k, "+1/1000", 1e-3 + 0x1p-51, 2.6e-9);
    Default(minus1k, "-1/1000", 1e-3 + 0x1p-51, 2.6e-9);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("default checks: %.1f s\n", secs);

    if (all) {
        All(truncated, "truncated");
        All(plus, "+bound");
        All(minus, "-bound");
        All(plus1k, "+1/1000");
        All(minus1k, "-1/1000");
    }

    std::printf("gekko_sqrt: %llu checks, %llu mismatches%s\n", (unsigned long long)g_checks.load(),
                (unsigned long long)g_failures.load(), all ? " (with all 2^32 floats, each model)" : "");
    return g_failures.load() == 0 ? 0 : 1;
}
