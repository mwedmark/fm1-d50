/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The D-50 engine (PicoFaceD5, third_party/picofaced5) behind a C interface for the FM-1 firmware. fm1-d50, D50.md.
 *
 * The sample data lies at the start of the user flash: a 16-byte header ("FMD5", version 1, the sample bytes, 0) and then the
 * D-50's 262 144 PCM words as 16-bit little-endian (tools/d50_pack.js makes the file from d5_make_blob.py's d5_pcm.bin).
 *
 * Threads: d50_bind_flash, d50_note, d50_program and d50_bend run in the control or routing task, under fm1_device_irq_save() as the
 * player's note calls are; they only put events in a queue. d50_render runs in the audio interrupt and plays the queue first. */
#ifndef FM1_D50_H
#define FM1_D50_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define D50_RATE 32000U
#define D50_FLASH_MAGIC "FMD5"
#define D50_HEADER_BYTES 16U
#define D50_PCM_BYTES 524288U

/* Look for the sample data in the user flash and start the engine on it: 1 if it is there (the engine then plays), else 0. */
int d50_bind_flash(void);
unsigned d50_ready(void);
/* velocity 0 is a note off */
void d50_note(unsigned note, unsigned velocity);
/* 0..d50_programs()-1, wraps */
void d50_program(unsigned program);
unsigned d50_programs(void);
unsigned d50_current_program(void);
const char *d50_program_name(unsigned program);   /* the current program's; "" for another */
/* MIDI pitch bend, 0..16383, 8192 = centre; +-2 semitones */
void d50_bend(unsigned value);
void d50_panic(void);
/* Add n frames at D50_RATE to out (interleaved stereo, Q23, saturated). Audio interrupt. */
void d50_render(int32_t *out, unsigned n);
unsigned d50_voices_sounding(void);

#ifdef __cplusplus
}
#endif
#endif
