/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The D-50's math functions on fm1-base, which has no libc headers: d50_math.c's exp2f .. tanf and fabsf, inline in the engine's unit. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#if defined(FM1_BASE) && !defined(D50_MATH_FN)
/* the engine's unit (d50_unit.cpp, FM1_BASE): d50_math.c's functions inline everywhere, forced, so that the RAM code (d5_hot.h) calls none of
 * them in the flash (more than a call's reach from the RAM, and refused by tools/ramcode.py) */
#define D50_MATH_FN static inline __attribute__((always_inline))
#define D50_MATH_HELPER static inline __attribute__((always_inline))
#define D50_MATH_LOCAL static inline __attribute__((always_inline))
#include "../../d50/d50_math.c"
static inline __attribute__((always_inline)) float fabsf(float x) { union { float f; uint32_t u; } v; v.f = x; v.u &= 0x7FFFFFFFu; return v.f; }
#else
float exp2f(float x);
float log10f(float x);
float powf(float a, float b);
float sinf(float x);
float cosf(float x);
float tanf(float x);
float fabsf(float x);
#endif
float expf(float x);
float log2f(float x);
float logf(float x);
float sqrtf(float x);
float floorf(float x);
float ceilf(float x);
float fmodf(float a, float b);
#ifdef __cplusplus
}
#endif
