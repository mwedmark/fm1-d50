/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A small raw-deflate decoder (RFC 1951) for the D-50's patch bank in the user flash (tools/d50_pack.js deflates it). Canonical Huffman
 * decoding one bit at a time, as Mark Adler's puff does: slow, but a bank of 28 KB unpacks once, when the flash is bound.
 * tools/d50_host/test_inflate.c checks it against zlib's output. */
#include "d50_inflate.h"

struct stream {
    const uint8_t *in;
    unsigned in_bytes, in_at;
    uint32_t bit_buffer;
    unsigned bit_count;
    uint8_t *out;
    unsigned out_bytes, out_at;
};

struct huffman {
    uint16_t count[16];                                  /* codes of each length */
    uint16_t symbol[288];                                /* the symbols in canonical order */
};

static int bits(struct stream *s, unsigned need, unsigned *value)
{
    uint32_t v = s->bit_buffer;
    while (s->bit_count < need) {
        if (s->in_at >= s->in_bytes) return -1;
        v |= (uint32_t)s->in[s->in_at++] << s->bit_count;
        s->bit_count += 8;
    }
    *value = v & ((1U << need) - 1U);
    s->bit_buffer = v >> need;
    s->bit_count -= need;
    return 0;
}

static int decode(struct stream *s, const struct huffman *h)
{
    int code = 0, first = 0, index = 0;
    for (int length = 1; length < 16; ++length) {
        unsigned b;
        if (bits(s, 1, &b)) return -1;
        code |= (int)b;
        const int count = h->count[length];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

/* 0 for a complete code, more for an incomplete one (allowed, as zlib does for a single distance code), less for an oversubscribed one */
static int construct(struct huffman *h, const uint8_t *length, unsigned n)
{
    uint16_t offsets[16];
    for (unsigned l = 0; l < 16; ++l) h->count[l] = 0;
    for (unsigned i = 0; i < n; ++i) h->count[length[i]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (unsigned l = 1; l < 16; ++l) {
        left = (left << 1) - h->count[l];
        if (left < 0) return left;
    }
    offsets[1] = 0;
    for (unsigned l = 1; l < 15; ++l) offsets[l + 1] = (uint16_t)(offsets[l] + h->count[l]);
    for (unsigned i = 0; i < n; ++i)
        if (length[i]) h->symbol[offsets[length[i]]++] = (uint16_t)i;
    return left;
}

static const uint16_t length_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t distance_base[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                                           4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t distance_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int codes(struct stream *s, const struct huffman *lengths, const struct huffman *distances)
{
    for (;;) {
        int symbol = decode(s, lengths);
        if (symbol < 0) return -1;
        if (symbol < 256) {
            if (s->out_at >= s->out_bytes) return -1;
            s->out[s->out_at++] = (uint8_t)symbol;
        } else if (symbol == 256) {
            return 0;
        } else {
            symbol -= 257;
            if (symbol >= 29) return -1;
            unsigned extra;
            if (bits(s, length_extra[symbol], &extra)) return -1;
            unsigned length = length_base[symbol] + extra;
            const int d = decode(s, distances);
            if (d < 0 || d >= 30) return -1;
            if (bits(s, distance_extra[d], &extra)) return -1;
            const unsigned distance = distance_base[d] + extra;
            if (distance > s->out_at || length > s->out_bytes - s->out_at) return -1;
            for (; length; --length, ++s->out_at) s->out[s->out_at] = s->out[s->out_at - distance];
        }
    }
}

static struct huffman length_code, distance_code;          /* static: 650 bytes off the caller's stack */
static uint8_t code_lengths[320];

static int stored(struct stream *s)
{
    s->bit_buffer = 0;                                     /* to the byte boundary */
    s->bit_count = 0;
    if (s->in_at + 4 > s->in_bytes) return -1;
    const unsigned n = s->in[s->in_at] | (unsigned)s->in[s->in_at + 1] << 8;
    const unsigned check = s->in[s->in_at + 2] | (unsigned)s->in[s->in_at + 3] << 8;
    s->in_at += 4;
    if (n != (~check & 0xFFFFU) || s->in_at + n > s->in_bytes || n > s->out_bytes - s->out_at) return -1;
    for (unsigned i = 0; i < n; ++i) s->out[s->out_at++] = s->in[s->in_at++];
    return 0;
}

static int fixed(struct stream *s)
{
    unsigned i = 0;
    for (; i < 144; ++i) code_lengths[i] = 8;
    for (; i < 256; ++i) code_lengths[i] = 9;
    for (; i < 280; ++i) code_lengths[i] = 7;
    for (; i < 288; ++i) code_lengths[i] = 8;
    construct(&length_code, code_lengths, 288);
    for (i = 0; i < 30; ++i) code_lengths[i] = 5;
    construct(&distance_code, code_lengths, 30);
    return codes(s, &length_code, &distance_code);
}

static int dynamic(struct stream *s)
{
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    unsigned n_length, n_distance, n_code, v;
    if (bits(s, 5, &n_length) || bits(s, 5, &n_distance) || bits(s, 4, &n_code)) return -1;
    n_length += 257;
    n_distance += 1;
    n_code += 4;
    if (n_length > 286 || n_distance > 30) return -1;
    unsigned i = 0;
    for (; i < n_code; ++i) {
        if (bits(s, 3, &v)) return -1;
        code_lengths[order[i]] = (uint8_t)v;
    }
    for (; i < 19; ++i) code_lengths[order[i]] = 0;
    if (construct(&length_code, code_lengths, 19) != 0) return -1;
    for (i = 0; i < n_length + n_distance;) {
        int symbol = decode(s, &length_code);
        if (symbol < 0) return -1;
        if (symbol < 16) {
            code_lengths[i++] = (uint8_t)symbol;
            continue;
        }
        uint8_t value = 0;
        unsigned repeat;
        if (symbol == 16) {
            if (i == 0) return -1;
            value = code_lengths[i - 1];
            if (bits(s, 2, &repeat)) return -1;
            repeat += 3;
        } else if (symbol == 17) {
            if (bits(s, 3, &repeat)) return -1;
            repeat += 3;
        } else {
            if (bits(s, 7, &repeat)) return -1;
            repeat += 11;
        }
        if (i + repeat > n_length + n_distance) return -1;
        while (repeat--) code_lengths[i++] = value;
    }
    if (code_lengths[256] == 0) return -1;                 /* no end-of-block code */
    const int l = construct(&length_code, code_lengths, n_length);
    if (l < 0 || (l > 0 && n_length - length_code.count[0] != 1)) return -1;
    const int d = construct(&distance_code, code_lengths + n_length, n_distance);
    if (d < 0 || (d > 0 && n_distance - distance_code.count[0] != 1)) return -1;
    return codes(s, &length_code, &distance_code);
}

int d50_inflate(uint8_t *out, unsigned out_bytes, const uint8_t *in, unsigned in_bytes)
{
    struct stream s = {in, in_bytes, 0, 0, 0, out, out_bytes, 0};
    unsigned last, type;
    do {
        if (bits(&s, 1, &last) || bits(&s, 2, &type)) return -1;
        const int error = type == 0 ? stored(&s) : type == 1 ? fixed(&s) : type == 2 ? dynamic(&s) : -1;
        if (error) return -1;
    } while (!last);
    return (int)s.out_at;
}
