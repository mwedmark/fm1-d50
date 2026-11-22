/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The D-50 on fm1-base: the app's half of the contract with the platform (fm1-base device/unity.c). FM1-BASE.md. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "D-50"
#define APP_SPLASH_RGB RGB(40, 90, 200)
#define APP_BLOCK 128u                   /* frames per audio block: 2.9 ms at 44.1 kHz */
#define APP_TOUCH                        /* velocity from the keys (lib/touch.h; fm1.toml input_hz = 10000, as it was measured at) */

/* the store: the settings (octave, bank, program) and the panel's calibration (the platform's). The data area holds the D-50's data from
 * data_lo (fm1.toml) up to 0xDE000, so the panel's two sectors are 0xDE000 and 0xDF000; nothing may sit in 0xE0000..0xFBFFF */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDE000u}

/* the D-50's data (roms/out/d50_fm1base.fmd5: the samples and six banks, 580 040 B, tools/d50_pack.js version 3) as the resource region
 * (fm1.toml resources): its 32-byte head and the data in 144 sectors from the data area's start, 0x4E000 .. 0xDE000 (8.7 KB to spare).
 * The package carries it and the update loader writes it after the app. Issue #7: 0x5B000 and 131 sectors held one bank */
#define APP_RES_BASE 0x4E000u
#define APP_RES_SECTORS 144u
/* plat_restart (device/main_fm1.c): the FM-1 starts again on data written over USB (app_frame, res_stamp) */
#define APP_RESTART 1
/* the resources need the backup (the transfer is the restore's): the settings are all there is to back up */
#define APP_BACKUP_OBJS {OBJ_SETTINGS}
#define APP_BACKUP_MAGIC 0x30354444u            /* "DD50" */
#define APP_BACKUP_VERSION 1u
#define APP_BACKUP_WHAT "replaces the settings"
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);

/* the time per block (d50.cpp FM1_D50_PROF), app.c app_sysex: F0 7D S F 02 0B 00 F7 and 0C bits F7 */
#define APP_SYSEX
int app_sysex(const uint8_t *p, uint32_t n);

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
