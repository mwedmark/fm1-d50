/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* For test_packed_engine.js, beside wrap.c: the live cache's calls (fm1b.c built with FM1B_PACKED=1) with a pool in memory the test can size. */
#include "fm1b.h"
#include <stdint.h>
#define EXPORT __attribute__((used, visibility("default")))
static uint8_t pool[1 << 20];
EXPORT void mt_cache_attach(unsigned bytes) { fm1b_cache_attach(pool, bytes > sizeof pool ? (unsigned)sizeof pool : bytes); }
EXPORT unsigned mt_cache_service(unsigned frames) { return fm1b_cache_service(frames); }
EXPORT uint32_t mt_cache_stat(unsigned which)   /* 0 hits, 1 misses, 2 evictions, 3 no room, 4 decoded frames, 5 requested, 6 lost, 7 waiting, 8 pins held */
{
    struct fm1b_cache_stats s;
    fm1b_cache_stats(&s);
    const uint32_t v[11] = {s.hits, s.misses, s.evictions, s.no_room, s.decoded_mp3_frames, s.requested, s.lost, s.waiting, s.pinned, s.heads_dropped, s.warmed};
    return which < 11 ? v[which] : 0;
}
uint32_t fm1b_test_entry(unsigned i, unsigned which);
EXPORT uint32_t mt_cache_entry(unsigned i, unsigned which) { return fm1b_test_entry(i, which); }   /* 0 state, 1 offset, 2 bytes, 3 dropped, 4 pins */
EXPORT uint8_t *mt_pool(void) { return pool; }
