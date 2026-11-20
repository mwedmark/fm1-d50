// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// RAM placement for the audio-rate functions, same pattern as cp_hot.h and
// the RD engine: on the Pico they go to .time_critical, on the host the
// macro vanishes. Everything the sample loop touches belongs in RAM -- the
// XIP cache is shared with the sample blob, and a strided PCM read evicting
// the code that reads it is how this instrument spent a day at 167% load.
#pragma once

#if defined(__has_include)
#  if __has_include("pico.h")
#    include "pico.h"
#  endif
#endif

#ifdef __not_in_flash_func
#  define D5_HOT(f) __not_in_flash_func(f)
// Two D5_HOT(next) in different classes land in the same named section and
// the linker rejects mixing a template's comdat with a plain function there.
// Functions that share a name across classes take a unique tag instead.
#  define D5_HOT_TAG(tag, f) __attribute__((section(".time_critical." #tag))) f
#elif defined(FM1_BASE)
// fm1-d50 on fm1-base: its RAM code (.app_ram, plat.h PLAT_RAM_CODE), which may call only RAM code (tools/ramcode.py checks); a section of
// its own for each use, as below
#  define D5_HOT_SEC2(n, name) __attribute__((section(".app_ram.d5_" #name "_" #n))) name
#  define D5_HOT_SEC1(n, name) D5_HOT_SEC2(n, name)
#  define D5_HOT(f) D5_HOT_SEC1(__COUNTER__, f)
#  define D5_HOT_TAG(tag, f) D5_HOT_SEC1(__COUNTER__, f)
#elif defined(__GCC_PI32V2__)
// fm1-d50: the FM-1's JieLi toolchain (sdk/Build.ps1 defines __GCC_PI32V2__) has no pico.h, so until now none of this was in RAM: the engine ran
// from flash through the XIP cache that the 512 KB sample blob also uses. The SDK's own audio code lives in .volatile_ram_code (app/synth/engine.h's
// FM1_DSP_CODE); every use gets a section of its own, so that the comdats of inline members never share one.
#  define D5_HOT_SEC2(n, name) __attribute__((section(".volatile_ram_code.d5_" #name "_" #n))) name
#  define D5_HOT_SEC1(n, name) D5_HOT_SEC2(n, name)
#  define D5_HOT(f) D5_HOT_SEC1(__COUNTER__, f)
#  define D5_HOT_TAG(tag, f) D5_HOT_SEC1(__COUNTER__, f)
#else
#  define D5_HOT(f) f
#  define D5_HOT_TAG(tag, f) f
#endif

// D5_HOT_SHARED: an audio-rate function the cold code (a note's start, configure) calls too. On fm1-base the RAM code and the XIP code are
// more than a call's reach (+-4 MiB) apart, so it is inline (forced) in both instead of one copy in RAM; elsewhere it is D5_HOT.
#if defined(FM1_BASE) && !defined(__not_in_flash_func)
#  define D5_HOT_SHARED(f) __attribute__((always_inline)) f
#else
#  define D5_HOT_SHARED(f) D5_HOT(f)
#endif

// D5_HOT_GATE: the audio function the cold code calls each block (Patch::render_block), and D5_FAR(fn): fn through a pointer the compiler
// cannot see through. On fm1-base the gate stays in the XIP code and reaches the RAM code through D5_FAR (plat.h PLAT_FAR: a direct call
// does not reach); elsewhere the gate is D5_HOT and D5_FAR is the plain function.
#if defined(FM1_BASE) && !defined(__not_in_flash_func)
#  define D5_GATED 1
#  define D5_HOT_GATE(f) f
namespace d5 {
inline __attribute__((always_inline)) void* far_ptr(void* fn) {
    void* volatile held = fn;
    return held;
}
}
#  define D5_FAR(fn) (reinterpret_cast<decltype(&(fn))>(::d5::far_ptr(reinterpret_cast<void*>(&(fn)))))
#else
#  define D5_GATED 0
#  define D5_HOT_GATE(f) D5_HOT(f)
#  define D5_FAR(fn) (&(fn))
#endif
