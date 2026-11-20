/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The D-50's math functions on fm1-base, which has no libc headers: d50_math.c defines exp2f .. tanf, d50_unit.cpp the rest. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
float exp2f(float x);
float expf(float x);
float log10f(float x);
float log2f(float x);
float logf(float x);
float powf(float a, float b);
float sinf(float x);
float cosf(float x);
float tanf(float x);
float fabsf(float x);
float sqrtf(float x);
float floorf(float x);
float ceilf(float x);
float fmodf(float a, float b);
#ifdef __cplusplus
}
#endif
