/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FM1_D50_INFLATE_H
#define FM1_D50_INFLATE_H
#include <stdint.h>
/* Unpack a raw deflate stream (RFC 1951, no zlib header) into out: the bytes written, or -1 if the stream is broken or does not fit.
 * The whole output is the window, so it needs no other buffer. Not reentrant (its code tables are static). */
int d50_inflate(uint8_t *out, unsigned out_bytes, const uint8_t *in, unsigned in_bytes);
#endif
