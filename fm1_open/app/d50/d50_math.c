/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The single-precision math functions the D-50 engine calls: the FM-1 SDK has no libm. exp2f and log2 by bit splitting and short
 * polynomials (relative error about 1e-7), the sine by Taylor series after reduction to [-pi/2, pi/2]. They are used mostly when a
 * program is configured or a note starts; powf also once a block. tools/d50_host/test_math.js checks them against a libm. */
#include <math.h>
#include <stdint.h>

#ifndef D50_MATH_PREFIX
#define D50_MATH_PREFIX(name) name
#endif

static inline float bits_float(uint32_t u) { union { uint32_t u; float f; } v; v.u = u; return v.f; }
static inline uint32_t float_bits(float f) { union { uint32_t u; float f; } v; v.f = f; return v.u; }

float D50_MATH_PREFIX(exp2f)(float x)
{
    if (x != x) return x;
    if (x > 127.99f) return bits_float(0x7F800000U);
    if (x < -149.0f) return 0.0f;
    const float k = x >= 0 ? (float)(int32_t)(x + 0.5f) : -(float)(int32_t)(-x + 0.5f);   /* round to nearest */
    const float t = (x - k) * 0.69314718056f;                                           /* |t| <= 0.347 */
    float p = 1.0f + t * (1.0f + t * (0.5f + t * (1.0f / 6 + t * (1.0f / 24 + t * (1.0f / 120 + t * (1.0f / 720 + t * (1.0f / 5040)))))));
    int32_t e = (int32_t)k;
    if (e < -126) { p *= bits_float((uint32_t)(e + 126 + 127) << 23); e = -126; }      /* subnormal results: two steps */
    return p * bits_float((uint32_t)(e + 127) << 23);
}

static float log2_positive(float x)
{
    uint32_t u = float_bits(x);
    int32_t e = (int32_t)(u >> 23) - 127;
    if (e == -127) {                                     /* subnormal: scale up first */
        u = float_bits(x * 8388608.0f);
        e = (int32_t)(u >> 23) - 127 - 23;
    }
    float m = bits_float((u & 0x007FFFFFU) | 0x3F800000U);   /* [1, 2) */
    if (m > 1.41421356f) { m *= 0.5f; ++e; }
    const float s = (m - 1.0f) / (m + 1.0f), s2 = s * s;
    const float ln = 2.0f * s * (1.0f + s2 * (1.0f / 3 + s2 * (1.0f / 5 + s2 * (1.0f / 7 + s2 * (1.0f / 9)))));
    return (float)e + ln * 1.44269504089f;
}

float D50_MATH_PREFIX(log10f)(float x)
{
    if (x != x || x < 0.0f) return bits_float(0x7FC00000U);
    if (x == 0.0f) return -bits_float(0x7F800000U);
    if (float_bits(x) == 0x7F800000U) return x;
    return log2_positive(x) * 0.30102999566f;
}

float D50_MATH_PREFIX(powf)(float a, float b)
{
    if (b == 0.0f) return 1.0f;
    if (a == 1.0f) return 1.0f;
    if (a == 0.0f) return b > 0.0f ? 0.0f : bits_float(0x7F800000U);
    if (a < 0.0f) {
        const float i = (float)(int32_t)b;
        if (i != b || b > 16777216.0f || b < -16777216.0f) return bits_float(0x7FC00000U);   /* a negative base needs a whole exponent */
        const float r = D50_MATH_PREFIX(exp2f)(b * log2_positive(-a));
        return ((int32_t)b & 1) ? -r : r;
    }
    return D50_MATH_PREFIX(exp2f)(b * log2_positive(a));
}

float D50_MATH_PREFIX(sinf)(float x)
{
    const float two_pi = 6.28318530718f, pi = 3.14159265359f, half_pi = 1.57079632679f;
    if (x != x || x > 1e7f || x < -1e7f) return x != x ? x : 0.0f;
    const float q = x * (1.0f / two_pi);
    const float k = q >= 0 ? (float)(int32_t)(q + 0.5f) : -(float)(int32_t)(-q + 0.5f);
    float r = x - k * 6.28125f - k * 0.0019353071795f;  /* 2 pi in two parts, for the larger arguments */
    if (r > half_pi) r = pi - r;
    else if (r < -half_pi) r = -pi - r;
    const float r2 = r * r;
    return r * (1.0f - r2 * (1.0f / 6 - r2 * (1.0f / 120 - r2 * (1.0f / 5040 - r2 * (1.0f / 362880 - r2 * (1.0f / 39916800))))));
}

float D50_MATH_PREFIX(cosf)(float x)
{
    if (x != x || x > 1e7f || x < -1e7f) return x != x ? x : 1.0f;
    const float q = x * (1.0f / 6.28318530718f);         /* reduce first: x + pi/2 would round for the larger arguments */
    const float k = q >= 0 ? (float)(int32_t)(q + 0.5f) : -(float)(int32_t)(-q + 0.5f);
    return D50_MATH_PREFIX(sinf)(x - k * 6.28125f - k * 0.0019353071795f + 1.57079632679f);
}

float D50_MATH_PREFIX(tanf)(float x) { return D50_MATH_PREFIX(sinf)(x) / D50_MATH_PREFIX(cosf)(x); }
