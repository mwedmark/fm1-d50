/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-base has no libc headers: felucca-bsp's chip/libc.c defines memset, memcpy, memcmp; d50_unit.cpp memmove and strlen. */
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);
void *memmove(void *d, const void *s, unsigned n);
unsigned strlen(const char *s);
#ifdef __cplusplus
}
#endif
