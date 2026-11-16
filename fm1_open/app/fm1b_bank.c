/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
/* The bank the firmware plays. First choice is an FM1B bank in the user flash (where the stock firmware keeps its
 * samples and projects: FM1B_FLASH_OFFSET, up to FM1B_FLASH_BYTES), used in place through fm1_flash_map(); the whole GM bank of a
 * SoundFont fits there, which the firmware image does not. Without one, the bank compiled into the image
 * (fm1b_bank_data.inc, from fm1-sf2's make_bank_c.js) is used if there is one. The browser simulator gets its bank
 * from the page instead, so it compiles this to nothing. */
#include "fm1b.h"
#include "fm1b_config.h"
#if !defined(FM1_HOST_TEST) && FM1B_IN_FIRMWARE
#include "fm1.h"
#include "fm1_device.h"
#include "fm1b_dqt.inc"
#include <string.h>
#if __has_include("fm1b_bank_data.inc")
#include "fm1b_bank_data.inc"
#define HAVE_BUILT_IN 1
#endif

#if FM1B_PACKED
/* The live cache's pool: static, so the link's runtime heap (firmware-report.json, runtime_heap_bytes) is what is left of the SRAM after it. */
#ifndef FM1B_CACHE_KB
#define FM1B_CACHE_KB 96
#endif
#if FM1B_FX_POOL
/* -FxPool (sdk/Build.ps1): the linker lays this pool right after the engine library's two idle effect units (fm1_fx_states, 48 832 bytes: the SDK's stock groove
 * effects, which this firmware never starts; RUNNING.md 1c), and the cache uses both as one pool. What stays out: the units' state words (-1 = idle: the engine
 * writes them), and what else the library keeps in the same section before this pool. */
extern uint8_t fm1_fx_pool_begin[];
static uint8_t cache_pool[FM1B_CACHE_KB * 1024U] __attribute__((aligned(4), section(".bss.fm1_cache_pool")));
enum { FX_UNIT = 24416, FX_STATE = 24380, FX_UNITS = 2 };
#else
static uint8_t cache_pool[FM1B_CACHE_KB * 1024U] __attribute__((aligned(4)));
#endif
#endif

/* Bind the bank at `offset` in the user flash, at most `limit` bytes: 1 if there is a usable one, else 0. A bank is trusted for its magic,
 * version and size only: fm1b_bind() checks the rest. */
static int bind_at(uint32_t offset, uint32_t limit)
{
    const uint8_t *header = fm1_flash_map(offset, 48);
    if (!header || memcmp(header, "FM1B", 4))
        return 0;
    uint32_t bytes = (uint32_t)header[40] | (uint32_t)header[41] << 8 | (uint32_t)header[42] << 16 | (uint32_t)header[43] << 24;
    if (bytes < 48 || bytes > limit)
        return 0;
    const uint8_t *image = fm1_flash_map(offset, bytes);
    return image && fm1b_bind(image, bytes, fm1b_dqt) == 0;
}

/* Bind the bank at the start of the user partition: 1 if there is a usable one, else 0. The upload uses this one: a bank that was just
 * written must be the one that plays, never an older one. */
int fm1b_bind_flash(void)
{
    return bind_at(FM1B_FLASH_OFFSET, FM1B_FLASH_BYTES);
}

void fm1b_device_init(void)
{
#if FM1B_PACKED
#if FM1B_FX_POOL
    if (cache_pool > fm1_fx_pool_begin && (unsigned)(cache_pool - fm1_fx_pool_begin) >= FX_UNITS * FX_UNIT) {        /* the linker put the two where it was asked to */
        fm1b_cache_attach(fm1_fx_pool_begin, (unsigned)(cache_pool + sizeof cache_pool - fm1_fx_pool_begin));
        for (unsigned u = 0; u < FX_UNITS; ++u) fm1b_cache_reserve(u * FX_UNIT + FX_STATE, 4U);
        fm1b_cache_reserve(FX_UNITS * FX_UNIT, (unsigned)(cache_pool - fm1_fx_pool_begin) - FX_UNITS * FX_UNIT);
    } else
        fm1b_cache_attach(cache_pool, sizeof cache_pool);
#else
    fm1b_cache_attach(cache_pool, sizeof cache_pool);
#endif
#endif
}

/* Control task: the player binds the bank once it knows the FM-1 was not started in safe mode (sf2player.c). */
void fm1b_device_bind(void)
{
    if (fm1b_bind_flash() || bind_at(FM1B_LEGACY_OFFSET, FM1B_LEGACY_BYTES))   /* a bank installed by an earlier firmware lies after the 68 KiB of projects */
        return;
#ifdef HAVE_BUILT_IN
    (void)fm1b_bind(fm1b_bank_data, sizeof fm1b_bank_data, fm1b_dqt);
#endif
}
#else
void fm1b_device_init(void) {}
void fm1b_device_bind(void) {}
int fm1b_bind_flash(void) { return 0; }
#endif
