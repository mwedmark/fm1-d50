/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef FM1B_CONFIG_H
#define FM1B_CONFIG_H
/* 1: the firmware plays an FM1B bank (the hooks in board/audio.c and midi_router.c, the bank in fm1b_bank.c).
 * It is 0 because the stock image has 345 bytes to spare in the FM1 layout (sdk/Package.cs) and the player needs about
 * 3.6 KB plus a bank, so a build with 1 stops at "Application does not fit the FM1 layout." until something else shrinks. */
#define FM1B_IN_FIRMWARE 1
/* 1: board/audio.c does not call fm1_engine_render() (the stock instrument is unused by the SF2 player). */
#define FM1B_SKIP_ENGINE 1
/* 1: the D-50 engine (app/d50, D50.md). With its sample data in the user flash (d50_bind_flash) it plays the keys and MIDI instead of the
 * FM1B player, at 32 kHz through sf2out.c's resampler; without it the FM1B player works as before. */
#ifndef FM1_D50
#ifdef FM1_HOST_TEST
#define FM1_D50 0                                        /* the WebAssembly tests are the FM1B player's */
#else
#define FM1_D50 1
#endif
#endif
/* The image has no room for both players whole: with the D-50 the FM1B player keeps only BFP8 banks, and the MIDI file player goes. */
#if FM1_D50 && !defined(FM1_MIDIPLAY)
#define FM1_MIDIPLAY 0
#endif
#if FM1_D50 && !defined(FM1B_BFP8_ONLY)
#define FM1B_BFP8_ONLY 1
#endif
/* 1: only the D-50 makes sound. The FM1B player's voices and effects are not called (sf2out.c), so the link drops their code: without that the
 * image is 13 KB over the layout (2026-11-19). The panel's FM1B pages stay, and show no bank. */
#if FM1_D50 && !defined(FM1_D50_ONLY)
#define FM1_D50_ONLY 1
#endif
/* Where the FM1B bank lives in the user flash (offsets start at zero in the firmware's user partition). The stock firmware kept its
 * projects in the first 68 KiB (69 632 bytes) and its sample bank after them (GROOVE_SAMPLE_OFFSET and GROOVE_SAMPLE_BYTES in instrument.h,
 * which the browser simulator's stock sampler still models). The SF2 player has no projects, so the bank starts at the beginning of the user
 * partition and may fill all of it: 536 576 bytes, 69 632 more than before. FM1B_LEGACY_* is where banks installed before this change lie:
 * fm1b_device_bind() still plays one if there is no bank at FM1B_FLASH_OFFSET, so a firmware update does not take the sound away. */
#define FM1B_FLASH_OFFSET 0U
#define FM1B_FLASH_BYTES 536576U
#define FM1B_LEGACY_OFFSET 69632U
#define FM1B_LEGACY_BYTES 466944U
/* 1: the MIDI file player (song.c): a song converted from a MIDI file by song.html is uploaded over SysEx into RAM and PLAY
 * plays it. 0 leaves out all of it, its code and its RAM (the song itself is allocated only when one is uploaded). */
#ifndef FM1_MIDIPLAY
#define FM1_MIDIPLAY 1
#endif
/* The largest song the player takes, in bytes (the heap is about 215 KB, part of it used by the firmware). */
#define FM1_MIDIPLAY_MAX_BYTES 131072U
#endif
