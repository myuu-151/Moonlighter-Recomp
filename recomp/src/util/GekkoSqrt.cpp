// The GameCube's sqrt and sqrtf (util/GekkoSqrt.h): this object's definitions take the place of
// newlib's (libm.a's w_sqrt.o and wf_sqrt.o, which are then never pulled into the link) for the
// game's code, Box2D and anything else that calls them; same results bit for bit. libm's own
// functions that call __ieee754_sqrt directly (acos, asin, hypot, pow...) keep newlib's.
// Nothing on the PC, whose sqrt is the hardware's.
//
// Cost (devkitPPC -O2): sqrt and sqrtf are 38 instructions each on the usual path (two compares,
// frsqrte, 14 multiplies and multiply-adds, two fsel, five constants' loads; no branch after the
// range check), a chain of 11 dependent floating-point instructions, so roughly 50 cycles at
// about 4 cycles each. newlib's sqrt runs about 890 instructions (740 to 1050, from the block
// sizes of libm_a-e_sqrt.o and the wrapper over random inputs: 54 iterations of a bit-by-bit loop,
// each with a data-dependent branch), its sqrtf about 275 (25 iterations).
#if defined(GEKKO)

#include "util/GekkoSqrt.h"

// No sqrt calls in here: the bodies are fmadds around frsqrte, so nothing can recurse.
extern "C" double sqrt(double x)
{
    return gekko_sqrt::Sqrt(x, gekko_sqrt::Frsqrte());
}

extern "C" float sqrtf(float x)
{
    return gekko_sqrt::Sqrtf(x, gekko_sqrt::Frsqrte());
}

#endif
