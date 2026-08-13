#pragma once

// Yuri's tjsConfig.cpp includes several C compatibility headers from inside
// namespace TJS.  Include them once at global scope so modern libstdc++ does
// not accidentally declare std as TJS::std when their compatibility wrappers
// are reached later.
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <math.h>
#include <time.h>

#ifdef __vita__
// newlib keeps the POSIX timezone object under its ABI name and hides the
// legacy math surface unless feature macros were set before all headers.
extern "C" long _timezone;
#define timezone _timezone
extern "C" int finite(double);
#ifndef M_E
#define M_E 2.7182818284590452354
#endif
#ifndef M_LOG2E
#define M_LOG2E 1.4426950408889634074
#endif
#ifndef M_LOG10E
#define M_LOG10E 0.43429448190325182765
#endif
#ifndef M_LN10
#define M_LN10 2.30258509299404568402
#endif
#ifndef M_LN2
#define M_LN2 0.69314718055994530942
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_SQRT2
#define M_SQRT2 1.41421356237309504880
#endif
#endif

#ifdef TJS_NO_REGEXP
namespace TJS {
void TJSReleaseRegex();
}
#endif
