/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The D-50 engine (PicoFaceD5, third_party/picofaced5) behind a C interface for the FM-1 firmware. fm1-d50, D50.md.
 *
 * The sample data lies at the start of the user flash: a 16-byte header ("FMD5", version, three zero bytes, the sample bytes, the bank's
 * deflated bytes), the D-50's 262 144 PCM words as 16-bit little-endian, and with version 2 a patch bank: its raw bytes (u32) and a raw deflate
 * stream of the D-50's 448-byte patches (tools/d50_pack.js makes the file from d5_make_blob.py's d5_pcm.bin and a SysEx bulk dump). The bank
 * is unpacked into RAM when the flash is bound (d50_inflate.c). Without a bank nothing plays: the image has no room for built-in presets.
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
#define D50_FLASH_BYTES 536576U                          /* the user flash (fm1b_config.h FM1B_FLASH_BYTES) */
#define D50_PATCH_BYTES 448U
#define D50_BANK_PATCHES 64U
#define D50_NAME_CHARS 18U

/* Look for the sample data in the user flash and start the engine on it: 1 if it is there (the engine then plays), else 0. */
int d50_bind_flash(void);
unsigned d50_ready(void);
/* Stop playing (the upload is about to erase the flash): d50_bind_flash starts again. */
void d50_unbind(void);
/* The installed data's length in the flash (header, samples, bank), for the install page's check; 0 when none plays. */
uint32_t d50_flash_bytes(void);
/* velocity 0 is a note off */
void d50_note(unsigned note, unsigned velocity);
/* 0..d50_programs()-1, wraps */
void d50_program(unsigned program);
unsigned d50_programs(void);                             /* the bank's patches */
/* Several banks (issue #7, the data's version 3): d50_banks their count (0 without data), d50_bank chooses one (0-based; unpacked into RAM,
 * then d50_program to sound it: 1 done, 0 no such bank), main loop; before the data is bound it is kept for the bind. */
unsigned d50_banks(void);
unsigned d50_current_bank(void);
int d50_bank(unsigned which);
unsigned d50_bank_patches(void);                         /* 0 without a bank */
unsigned d50_current_program(void);
const char *d50_program_name(unsigned program);   /* the current program's; "" for another */
/* The temporary area (the current patch, D50_PATCH_BYTES, with SysEx edits not yet sounding), for the screen; NULL without data. Main loop. */
const uint8_t *d50_patch_bytes(void);
/* MIDI pitch bend, 0..16383, 8192 = centre; over the patch's bender range (pb26, 0..12 semitones), or RPN 0's */
void d50_bend(unsigned value);
/* A control change, as the D-50 receives them: 1 modulation lever, 5 portamento time, 6 data entry (RPN 0: the bender range),
 * 7 volume, 64 hold pedal, 65 portamento switch, 100/101 the RPN; the others are ignored. Issue #4. */
void d50_cc(unsigned cc, unsigned value);
/* Channel pressure (aftertouch), 0..127 */
void d50_pressure(unsigned value);
/* Chase Play (issue #18), on or off with d50_cc(D50_CHASE_CC, >= 64 / < 64): the D-50's CHASE button. 1 while it is on. */
#define D50_CHASE_CC 80U
unsigned d50_chase_on(void);
/* SysEx, the D-50's DT1 and RQ1 (issue #6), main loop. d50_sysex: a frame without F0 and F7; 1 when it is the D-50's (Roland 0x41, model
 * 0x14). DT1 writes the temporary area (00 00 00, the current patch: applied while it sounds) or the bank in RAM (02 00 00); RQ1 asks for
 * them back. d50_sysex_reply: the next DT1 of an RQ1's answer, F0 .. F7, into frame (D50_SYSEX_REPLY_MAX bytes); its length, 0 when none is
 * due. d50_edit_poll: a DT1 into the temporary area becomes the sound (each pass of the main loop); 1 when it did. */
#define D50_SYSEX_REPLY_MAX 266U
int d50_sysex(const uint8_t *p, uint32_t n);
unsigned d50_sysex_reply(uint8_t *frame);
int d50_edit_poll(void);
/* The fault of the last D-50 frame d50_sysex took, then 0 until the next (main loop). The numbers are the D-50 firmware's error lines'
 * (v1.06, after "Error  Reject"): its screen shows them (issue #25). */
#define D50_ERR_ADDRESS 4U                        /* Invalid Data Address: an RQ1 of nothing there */
#define D50_ERR_FORMAT 6U                         /* Invalid Data Format: not a DT1 or RQ1 of the right length */
#define D50_ERR_SUM 7U                            /* Sum Check Error */
unsigned d50_sysex_error(void);
void d50_panic(void);
/* Add n frames at D50_RATE to out (interleaved stereo, Q23, saturated). Audio interrupt. */
void d50_render(int32_t *out, unsigned n);
unsigned d50_voices_sounding(void);
unsigned d50_reverb(void);                               /* 1: the reverb has its lines (the SDK's idle effect memory), 0: dry */
unsigned d50_cores(void);
unsigned d50_voice_limit(void);                          /* the CPU guard's cap on each tone's voices, 4..16 */
unsigned d50_split_share(void);                          /* CPU1's share of the sounding voices, in sixteenths */
/* The CPU guard: the audio interrupt's share of the block just done, in % (sf2out_load). Audio interrupt. */
void d50_guard(unsigned percent);                                /* 2: half the sounding voices render on the SDK's CPU1 (build_fw.ps1 -DualCore) */

#ifdef __cplusplus
}
#endif
#endif
