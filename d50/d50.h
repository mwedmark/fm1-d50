/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The D-50 engine (PicoFaceD5, third_party/picofaced5) behind a C interface for the FM-1 firmware (fm1base/app.c). fm1-d50, D50.md.
 *
 * The sample data lies at the start of the data area (fm1base/app.c app_d50_data): a 16-byte header ("FMD5", version, three zero bytes, the sample bytes, the bank's
 * deflated bytes), the D-50's 262 144 PCM words as 16-bit little-endian, and with version 2 a patch bank: its raw bytes (u32) and a raw deflate
 * stream of the D-50's 448-byte patches; version 3 holds several banks (tools/d50_pack.js makes the file from d5_make_blob.py's d5_pcm.bin and a SysEx bulk dump). The bank
 * is unpacked into RAM when the flash is bound (d50_inflate.c). Without a bank nothing plays: the image has no room for built-in presets.
 *
 * Threads: d50_bind_flash, d50_note, d50_program and d50_bend run in the main loop or the MIDI input; they only put events in a queue.
 * d50_render runs in the audio interrupt and plays the queue first. */
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
#define D50_PATCH_BYTES 448U
#define D50_BANK_PATCHES 64U
#define D50_NAME_CHARS 18U

/* Look for the sample data in the data area and start the engine on it: 1 if it is there (the engine then plays), else 0. */
int d50_bind_flash(void);
/* velocity 0 is a note off. d50_note is MIDI's (the basic channel); d50_key the instrument's own keys, which Key Transpose moves: as on
 * the D-50, it moves only the keyboard's notes, not the ones received over MIDI (issue #64, the firmware's 0x2B46) */
void d50_note(unsigned note, unsigned velocity);
void d50_key(unsigned note, unsigned velocity);
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
/* A control change, as the D-50 receives them: 1 modulation lever, 5 portamento time, 6 and 38 data entry (RPN 0: the bender range;
 * RPN 1: Fine Tuning, issue #66), 7 volume, 64 hold pedal, 65 portamento switch, 100/101 the RPN, 98/99 an NRPN (the data entries after it
 * are not taken); the others are ignored. Issue #4. */
void d50_cc(unsigned cc, unsigned value);
/* The D-50's TUNE/FUNCTION (issue #54): Master Tune 0..100 (427.5 .. 452.6 Hz, 50 = 440.0 Hz, as the D-50's system byte; the pitch's offset, with the bend) and Key
 * Transpose -12..12 semitones (a key of d50_key sounds its note moved, MIDI's notes are not: #64; its note-off ends the one it sounded). Both are kept over a program change. */
void d50_tune(unsigned value);
void d50_transpose(int semitones);
/* RPN 1, Fine Tuning, sets Master Tune as on the D-50 (issue #66): d50_midi_tune is the tune now (the low 8 bits, 0..100) and a count of
 * the times MIDI set it (the bits above), for the panel to follow. d50_rpn_reset: the RPN null and Fine Tuning's bytes not received, as the
 * D-50 does on a basic channel change and when mono starts with Control G.CH. */
unsigned d50_midi_tune(void);
void d50_rpn_reset(void);
/* Active Sensing lost (issue #68, the D-50's 0x574F): the notes off, the bender, lever, aftertouch, volume and pedal back to rest, the RPN
 * null. The caller times it (app.c: an FE arms it, any message resets it). */
void d50_sensing_lost(void);
/* The D-50's MIDI modes (issue #60, its firmware's rows at 0x69EC): D50_MODE_NORMAL the power-on state (the patch's key mode, its Solo too);
 * D50_MODE_POLY after a received Poly (the Solo key modes play polyphonically); D50_MODE_MONO, mode 4: channels 1..8 parts, each one note at
 * a time with its own bender, in any key mode. A change turns the notes off. d50_note_ch and d50_bend_ch take the part, the channel less the
 * basic channel (the caller's channel test): in mono a part's note ends the part's last one, and its bend moves only its note; otherwise the
 * part is not looked at. d50_note and d50_bend are part 0's. */
#define D50_MODE_NORMAL 0U
#define D50_MODE_POLY 1U
#define D50_MODE_MONO 2U
void d50_mode(unsigned mode, unsigned channels);
void d50_note_ch(unsigned part, unsigned note, unsigned velocity);
void d50_bend_ch(unsigned part, unsigned value);
/* The Separate key modes, SEP and SEP-S (issue #62, the firmware's 0x5DB7): the basic channel plays the Lower tone (d50_note and the rest),
 * the patch's SepCH the Upper with a controller set of its own. d50_sep_channel: the current patch's SepCH, 1..16, or 0 when it is not a
 * separate patch or SepCH is OFF (the audio interrupt's, for the channel test). d50_sep: a message on it, as MIDI has it (8x, 9x, Bx, Dx,
 * Ex; the channel is not looked at): notes, the bender, aftertouch, CC 1, 5, 6, 64, 65, 98-101 and 123; nothing else (no volume). */
unsigned d50_sep_channel(void);
void d50_sep(unsigned status, unsigned d1, unsigned d2);
/* Channel pressure (aftertouch), 0..127 */
void d50_pressure(unsigned value);
/* Chase Play (issue #18), on or off with d50_cc(D50_CHASE_CC, >= 64 / < 64): the D-50's CHASE button. 1 while it is on. */
#define D50_CHASE_CC 80U
unsigned d50_chase_on(void);
/* SysEx, the D-50's (Roland 0x41, model 0x14), main loop: one-way DT1 and RQ1 (issue #6), and the handshake (issue #51: WSD, RQD, DAT,
 * ACK, EOD, ERR, RJC). d50_sysex: a frame without F0 and F7; 1 when it is the D-50's. DT1 or DAT writes the temporary area (00 00 00, the
 * current patch: applied while it sounds) or the bank in RAM (02 00 00), and elsewhere is taken and ignored; RQ1 or RQD asks for them back.
 * d50_sysex_reply: the next frame to send, F0 .. F7, into frame (D50_SYSEX_REPLY_MAX bytes): a short answer (ACK, ERR, RJC), an RQ1's next
 * DT1, or an RQD's next DAT once the other side has acknowledged the last (then EOD); its length, 0 when none is due.
 * d50_edit_poll: a DT1 or DAT into the temporary area becomes the sound (each pass of the main loop); 1 when it did. */
#define D50_SYSEX_REPLY_MAX 266U
/* The device ID (issue #51), 0..15: the D-50's is its basic channel less one. A D-50 frame with another is taken (1) but neither done nor
 * answered, as on a D-50 that is not the one addressed; what goes out carries this one. 0 (channel 1) until set. */
void d50_device(unsigned id);
int d50_sysex(const uint8_t *p, uint32_t n);
unsigned d50_sysex_reply(uint8_t *frame);
int d50_edit_poll(void);
/* The fault of the last D-50 frame d50_sysex took, then 0 until the next (main loop). The numbers are the D-50 firmware's error lines'
 * (v1.06, after "Error  Reject"): its screen shows them (issue #25). */
#define D50_ERR_ADDRESS 4U                        /* Invalid Data Address: an RQ1 or RQD of nothing there */
#define D50_ERR_FORMAT 6U                         /* Invalid Data Format: not a frame of the right length */
#define D50_ERR_SUM 7U                            /* Sum Check Error */
unsigned d50_sysex_error(void);
void d50_panic(void);
/* Add n frames at D50_RATE to out (interleaved stereo, Q23, saturated). Audio interrupt. */
void d50_render(int32_t *out, unsigned n);
unsigned d50_voices_sounding(void);
unsigned d50_reverb(void);                               /* 1: the reverb has its lines (in the pool), 0: dry */
unsigned d50_cores(void);                                /* 2: a share of the sounding voices renders on CPU1 (fm1.toml cpu2) */
unsigned d50_voice_limit(void);                          /* the CPU guard's cap on each tone's voices, 4..16 */
unsigned d50_split_share(void);                          /* CPU1's share of the sounding voices, in sixteenths */
/* The CPU guard: the audio interrupt's share of the block just done, in % (app.c, from plat_cpu_pct). Audio interrupt. */
void d50_guard(unsigned percent);

#ifdef __cplusplus
}
#endif
#endif
