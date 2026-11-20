/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The D-50 on fm1-base: the app's half of the contract with the platform (fm1-base device/unity.c). FM1-BASE.md. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "D-50"
#define APP_SPLASH_RGB RGB(40, 90, 200)
#define APP_BLOCK 128u                   /* frames per audio block: 2.9 ms at 44.1 kHz, as the fm1_open build's */

/* the store: the settings (octave, program) and the panel's calibration (the platform's). The data area holds the D-50's data from
 * data_lo (fm1.toml) up to 0xDD648, so the panel's two sectors are 0xDE000 and 0xDF000; nothing may sit in 0xE0000..0xFBFFF */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDE000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
