// SPDX-License-Identifier: GPL-3.0-or-later
// The glue between the FM-1 firmware and the PicoFaceD5 engine (third_party/picofaced5, GPL-3.0-or-later, Michi71). d50.h says what each call does.
// Built for our own use: the engine's licence has no linking exception for the FM-1's SDK (RESEARCH.md section 3).
#include "d50.h"
#if FM1_BASE
// On fm1-base (fm1base/, fm1.toml data_lo): the data at the start of the data area, the second core through plat_cpu2_*, the big buffers in the
// platform's pool. plat.h is C; the few calls are declared here.
extern "C" {
#include "d50_inflate.h"
#include <string.h>
const uint8_t *plat_data(uint32_t off);
int plat_cpu2_on(void);
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg);
int plat_cpu2_wait(void);
uint32_t plat_ticks24(void);
}
#if FM1_CPU1                                     // fm1.toml cpu2 = true
#define FM1B_SPLIT 1
#endif
#define D50_POOL __attribute__((section(".pool")))
static const uint8_t *fm1_flash_map(uint32_t off, uint32_t n)
{
    return off <= D50_FLASH_BYTES && n <= D50_FLASH_BYTES - off ? plat_data(FM1B_DATA_LO + off) : nullptr;
}
#else
extern "C" {
#include "fm1_device.h"
#include "d50_inflate.h"
#include "fm1b.h"                                        /* FM1B_SPLIT (build_fw.ps1 -DualCore) and the other core's hand-over */
#include <string.h>
/* The engine library's two effect units, which sdk/Build.ps1 places between these two symbols for a D-50 build. Weak: without them, no reverb. */
extern uint8_t fm1_fx_pool_begin[] __attribute__((weak));
extern uint8_t fm1_fx_pool_end[] __attribute__((weak));
}
#define D50_POOL
#endif

// FM1_D50_PROF: where a block's time goes on the FM-1 (sf2_upload.c cmd 11 reads it, cmd 12 leaves stages out). TIMER4, 24 MHz, both cores.
#ifndef FM1_D50_PROF
#define FM1_D50_PROF (!FM1_BASE)
#endif
#if FM1_D50_PROF
extern "C" {
volatile uint32_t d50_prof_t[8];                 // the last time at each mark (5, 6: CPU1's job)
volatile uint32_t d50_prof_sum[8];               // 0 parts, 1 lists, 2 CPU0's voices, 3 the wait for CPU1, 4 the effects and mix, 5 CPU1's job
volatile unsigned d50_prof_skip;
}
#define D50_T4 (*(volatile uint32_t *)0x10804U)
static inline __attribute__((always_inline)) void d50_mark(int k)
{
    const uint32_t t = D50_T4;
    if (k == 4) {
        d50_prof_sum[0] += 1U;
        d50_prof_sum[1] += d50_prof_t[1] - d50_prof_t[0];
        d50_prof_sum[2] += d50_prof_t[2] - d50_prof_t[1];
        d50_prof_sum[3] += d50_prof_t[3] - d50_prof_t[2];
        d50_prof_sum[4] += t - d50_prof_t[3];
        d50_prof_sum[5] += d50_prof_t[6] - d50_prof_t[5];
        d50_prof_t[5] = d50_prof_t[6] = 0;
    }
    d50_prof_t[k] = t;
}
#define D5_MARK(k) d50_mark(k)
#define D5_SKIP(bit) ((d50_prof_skip & (bit)) != 0U)
#endif

// The effects 128 samples (4 ms) behind the voices, so that CPU0 runs them while CPU1 renders voices (d5_patch.h render_part)
#if !FM1_BASE && !defined(D5_FX_LAG)
#define D5_FX_LAG 128
#endif
#include "d5_engine/d5_patch.h"
#include "d5_engine/d5_patch_map.h"

// No libc++ here: placement new by hand. The engine is constructed in d50_bind_flash, not by a static constructor, which the SDK may not run.
inline void *operator new(decltype(sizeof 0), void *where) noexcept { return where; }

namespace {

constexpr unsigned kBlock = static_cast<unsigned>(d5::Tone<16>::kMaxBlock);

// The other core (build_fw.ps1 -DualCore: FM1B_SPLIT, app/cpu1_split.c, the SDK's own hand-over to its CPU1). begin hands CPU1 the job
// or says no (CPU1 not up, or the SDK holding it for a flash write: the job then runs here). wait gives CPU1 up to 20 ms; if it is not done
// by then the split is switched off for good, so that a stuck CPU1 costs one block and not every block.
volatile bool split_on = true;
volatile unsigned split_blocks, split_lost;
#if FM1_BASE && FM1B_SPLIT
d5::Patch::Split split;
void (*split_job)(void *);
void *split_arg;
void split_run(uint32_t) { split_job(split_arg); }
bool split_begin(void (*job)(void *), void *arg)
{
    split_job = job;
    split_arg = arg;
    if (!split_on || !plat_cpu2_run(&split_run, 0)) return false;   // (the platform holds it while the flash is written)
    ++split_blocks;
    return true;
}
void split_wait()
{
    const uint32_t t0 = plat_ticks24();
    if (plat_cpu2_wait() < 0) {                  // given up (fm1.toml cpu2_fallback): the job was dropped, so it runs here
        split_job(split_arg);
        split_on = false;
        ++split_lost;
        return;
    }
    const uint32_t waited_us = (plat_ticks24() - t0) / 24U;
    if (waited_us > 40U && split.share16 > 4) --split.share16;
    else if (waited_us < 5U && split.share16 < 13) ++split.share16;
}
#elif FM1B_SPLIT && !FM1_BASE
#define T4 (*(volatile uint32_t *)0x10804U)      /* TIMER4, 24 MHz, free running (board/device.c) */
d5::Patch::Split split;
bool split_begin(void (*job)(void *), void *arg)
{
    if (!split_on || fm1b_split_held() || !fm1b_split_begin(job, arg)) return false;   // not while the flash is written
    ++split_blocks;
    return true;
}
// CPU0 also mixes, runs the effects and resamples, so an even share of the voices leaves CPU1 idle part of the block. The share follows
// what CPU0 waited: over 40 us, CPU1 had too much and gets a sixteenth less; under 5 us, CPU1 was done first and gets a sixteenth more.
void split_wait()
{
    const uint32_t t0 = T4;
    for (int k = 0; k < 4; ++k)
        if (fm1b_split_wait()) {
            const uint32_t waited_us = (T4 - t0) / 24U;
            if (waited_us > 40U && split.share16 > 4) --split.share16;
            else if (waited_us < 5U && split.share16 < 13) ++split.share16;
            return;
        }
    split_on = false;
    ++split_lost;
}
#endif
#if !FM1B_SPLIT
d5::Patch::Split split;
#endif

// The CPU guard (sf2out_load, after each block): over 90 % of the block on CPU0 it retires the oldest released voice (a 20 ms fade,
// PicoFaceD5's shed_voice); with none to retire and over 97 %, a lower cap on each tone's voices (new notes past it find no slot). Under 60 %
// for a quarter of a second the cap comes back up a voice at a time. After a step it waits 8 blocks for the fade to show.
unsigned guard_hold, guard_calm;
int guard_cap = 16;

enum : unsigned { kEvNote = 1, kEvBend = 2, kEvPanic = 3, kQueue = 64 };

alignas(8) unsigned char patch_memory[sizeof(d5::Patch)] D50_POOL;
d5::Patch *patch;
const int16_t *pcm;
volatile unsigned ready;

volatile uint32_t queue[kQueue];
volatile unsigned queue_head, queue_tail;        // head: written by the tasks, tail: by the interrupt

// The patch bank, unpacked from the flash when it is bound: the D-50's own 448 bytes a patch, which patch_from_bytes reads.
uint8_t bank[D50_BANK_PATCHES * D50_PATCH_BYTES] D50_POOL;
unsigned bank_patches;                           // 0: no bank (d50_bind_flash then refuses the flash)

// The program is built in the control task into one of two specs; the interrupt configures the patch from the newest one. Raw storage, as
// patch_memory: a PatchSpec array has default values, which would put its 3.6 KB in the image's .data; a slot is built before it is read.
alignas(d5::PatchSpec) unsigned char spec_memory[2][sizeof(d5::PatchSpec)];
static_assert(__has_trivial_destructor(d5::PatchSpec), "a slot is rebuilt in place without ending the old spec");
const d5::PatchSpec &spec_at(unsigned slot) { return *reinterpret_cast<const d5::PatchSpec *>(spec_memory[slot]); }
char names[2][D50_NAME_CHARS + 1];
volatile unsigned spec_serial, spec_done;        // spec_serial & 1 is the slot the newest one is in
unsigned program_now;
volatile unsigned sounding;

uint32_t u32(const uint8_t *p) { return p[0] | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24; }

// The reverb's delay lines (42 KB of 16-bit words) in the SDK's idle effect memory: two units of 24 416 bytes, each with a state word at 24 380
// that the library writes (-1: idle) and that must stay as it is, and 20 bytes of the library's after them (fm1-sf2 RUNNING.md 1c). The firmware
// never calls the library's instrument (FM1B_SKIP_ENGINE), so the units are never started. Segment 0: [0, 24 380), segment 1: [24 384, 48 796).
enum : unsigned { kFxUnit = 24416, kFxState = 24380, kFxUnits = 2 };
bool reverb_attached;

#if FM1_BASE
// fm1-base: the lines in the pool, the same two segments as in the SDK's units
d5::ReverbSample reverb_lines[kFxState / 2U + (kFxUnit - 4U) / 2U] D50_POOL;
void attach_reverb()
{
    reverb_attached = true;
    patch->attach_reverb(reverb_lines, static_cast<int>(kFxState / 2U), reverb_lines + kFxState / 2U, static_cast<int>((kFxUnit - 4U) / 2U));
}
#else
void attach_reverb()
{
    const uintptr_t begin = reinterpret_cast<uintptr_t>(fm1_fx_pool_begin), end = reinterpret_cast<uintptr_t>(fm1_fx_pool_end);
    reverb_attached = begin && end > begin && end - begin >= kFxUnits * kFxUnit;
    // Only where the state words are what the library leaves in idle units (-1): else the layout is not the one assumed, and the reverb stays dry
    for (unsigned u = 0; reverb_attached && u < kFxUnits; ++u) {
        uint32_t word;
        memcpy(&word, fm1_fx_pool_begin + u * kFxUnit + kFxState, 4);
        reverb_attached = word == 0xFFFFFFFFU;
    }
    if (!reverb_attached) return;
    auto *seg0 = reinterpret_cast<d5::ReverbSample *>(fm1_fx_pool_begin);
    auto *seg1 = reinterpret_cast<d5::ReverbSample *>(fm1_fx_pool_begin + kFxState + 4U);
    patch->attach_reverb(seg0, static_cast<int>(kFxState / 2U), seg1, static_cast<int>((kFxUnit + kFxState - (kFxState + 4U)) / 2U));
}
#endif

void push(uint32_t event)
{
    const unsigned next = (queue_head + 1U) % kQueue;
    if (next == queue_tail) return;              // full: drop (64 events between two blocks of 2.9 ms is a lot)
    queue[queue_head] = event;
    queue_head = next;
}

// The patch name: 18 characters in the panel's character set, at the start of the patch block (the seventh of 64 bytes).
void patch_name(const uint8_t *p, char *out)
{
    static const char chars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890-";
    unsigned n = 0;
    for (unsigned i = 0; i < D50_NAME_CHARS; ++i) {
        const uint8_t c = p[6 * 64 + i];
        out[i] = c < sizeof chars - 1 ? chars[c] : '?';
        if (out[i] != ' ') n = i + 1;
    }
    out[n] = 0;
}

void build_program(unsigned program)
{
    const unsigned slot = (spec_serial + 1U) & 1U;
    const uint8_t *p = bank + program * D50_PATCH_BYTES;
    new (spec_memory[slot]) d5::PatchSpec(d5::patch_from_bytes(p, pcm));   // (trivially destructible: nothing to end first)
    patch_name(p, names[slot]);
    program_now = program;
    spec_serial = spec_serial + 1U;
}

// The bank after the samples (version 2, tools/d50_pack.js): its raw size, then its raw deflate stream.
unsigned bind_bank(const uint8_t *header)
{
    if (header[4] < 2U) return 0;
    const uint32_t deflated = u32(header + 12);
    const uint32_t at = D50_HEADER_BYTES + D50_PCM_BYTES;
    if (deflated == 0 || at + 4U + deflated > D50_FLASH_BYTES) return 0;
    const uint8_t *b = fm1_flash_map(at, 4U + deflated);
    if (!b) return 0;
    const uint32_t raw = u32(b);
    if (raw == 0 || raw % D50_PATCH_BYTES || raw > sizeof bank) return 0;
    if (d50_inflate(bank, raw, b + 4, deflated) != static_cast<int>(raw)) return 0;
    return raw / D50_PATCH_BYTES;
}

inline int32_t to_q23(float x)
{
    float v = x * 8388607.0f;
    if (v > 8388607.0f) v = 8388607.0f;
    if (v < -8388608.0f) v = -8388608.0f;
    return static_cast<int32_t>(v);
}

inline int32_t sat24(int32_t v) { return v > 8388607 ? 8388607 : v < -8388608 ? -8388608 : v; }

}  // namespace

extern "C" int d50_bind_flash(void)
{
    const uint8_t *header = fm1_flash_map(0, D50_HEADER_BYTES);
    if (!header || memcmp(header, D50_FLASH_MAGIC, 4) || header[4] < 1U || header[4] > 2U) return 0;
    if (u32(header + 8) != D50_PCM_BYTES) return 0;
    const uint8_t *image = fm1_flash_map(0, D50_HEADER_BYTES + D50_PCM_BYTES);
    if (!image) return 0;
    ready = 0;                                   // the interrupt stops playing while the engine and the bank are replaced
    pcm = reinterpret_cast<const int16_t *>(image + D50_HEADER_BYTES);
    bank_patches = bind_bank(header);
    if (!bank_patches) return 0;                 // samples without a bank: nothing to play (the image has no room for built-in presets)
    if (program_now >= d50_programs()) program_now = 0;
    patch = new (patch_memory) d5::Patch();
#if FM1B_SPLIT
    split.begin = &split_begin;
    split.wait = &split_wait;
#endif
    guard_cap = 16;
    guard_hold = guard_calm = 0;
    attach_reverb();                             // before configure, which binds the reverb's lines
    build_program(program_now);
    patch->configure(spec_at(spec_serial & 1U), static_cast<float>(D50_RATE));
    spec_done = spec_serial;
    queue_tail = queue_head;
    ready = 1;
    return 1;
}

extern "C" unsigned d50_ready(void) { return ready; }
extern "C" void d50_unbind(void) { ready = 0; }
extern "C" uint32_t d50_flash_bytes(void)
{
    if (!ready) return 0;
    const uint8_t *header = fm1_flash_map(0, D50_HEADER_BYTES);
    if (!header) return 0;
    const uint32_t deflated = u32(header + 12);
    return D50_HEADER_BYTES + D50_PCM_BYTES + (header[4] >= 2U && deflated ? 4U + deflated : 0U);
}
extern "C" void d50_note(unsigned note, unsigned velocity) { push(kEvNote << 24 | (note & 127U) << 8 | (velocity & 127U)); }
extern "C" void d50_bend(unsigned value) { push(kEvBend << 24 | (value & 16383U)); }
extern "C" void d50_panic(void) { push(kEvPanic << 24); }
extern "C" unsigned d50_programs(void) { return bank_patches ? bank_patches : 1U; }
extern "C" unsigned d50_bank_patches(void) { return bank_patches; }
extern "C" unsigned d50_current_program(void) { return program_now; }
extern "C" const char *d50_program_name(unsigned program)
{
    return ready && program == program_now ? names[spec_serial & 1U] : "";
}
extern "C" unsigned d50_voices_sounding(void) { return sounding; }
extern "C" unsigned d50_reverb(void) { return reverb_attached; }
extern "C" unsigned d50_voice_limit(void) { return static_cast<unsigned>(guard_cap); }
extern "C" unsigned d50_split_share(void) { return static_cast<unsigned>(split.share16); }

extern "C" void d50_guard(unsigned percent)
{
    if (!ready) return;
    if (guard_hold) { --guard_hold; return; }
    if (percent > 90U) {
        guard_calm = 0;
        if (patch->shed_voice()) { guard_hold = 8; return; }
        if (percent > 97U && guard_cap > 4) { patch->cap_voices(--guard_cap); guard_hold = 8; }
    } else if (percent < 60U && guard_cap < 16) {
        if (++guard_calm >= 86U) { patch->cap_voices(++guard_cap); guard_calm = 0; }
    } else {
        guard_calm = 0;
    }
}
extern "C" unsigned d50_cores(void)
{
#if FM1_BASE && FM1B_SPLIT
    return split_on && plat_cpu2_on() ? 2U : 1U;
#elif FM1B_SPLIT && !FM1_BASE
    return split_on && fm1b_split_up() ? 2U : 1U;
#else
    return 1U;
#endif
}

extern "C" void d50_program(unsigned program)
{
    program %= d50_programs();
    if (!ready) { program_now = program; return; }
    build_program(program);
}

extern "C" void d50_render(int32_t *out, unsigned n)
{
    if (!ready) return;
    if (spec_done != spec_serial) {               // a new program: configure stops what sounds
        const unsigned serial = spec_serial;
        patch->configure(spec_at(serial & 1U), static_cast<float>(D50_RATE));
        spec_done = serial;
    }
    while (queue_tail != queue_head) {
        const uint32_t e = queue[queue_tail];
        queue_tail = (queue_tail + 1U) % kQueue;
        switch (e >> 24) {
        case kEvNote: {
            const int note = static_cast<int>((e >> 8) & 127U), velocity = static_cast<int>(e & 127U);
            if (velocity) patch->note_on(note, velocity / 127.0f);
            else patch->note_off(note);
            break;
        }
        case kEvBend: patch->set_bend_semis((static_cast<int>(e & 16383U) - 8192) * (2.0f / 8192.0f)); break;
        case kEvPanic: patch->silence(); break;
        }
    }
    // A block at a time, every second sounding voice on the SDK's CPU1 (Patch::render_block; FM1B_SPLIT, app/cpu1_split.c)
    static float left[kBlock], right[kBlock];
    for (unsigned s = 0; s < n;) {
        const unsigned m = n - s > kBlock ? kBlock : n - s;
        patch->render_block(left, right, static_cast<int>(m), &split);
        for (unsigned i = 0; i < m; ++i) {
            out[2 * (s + i)] = sat24(out[2 * (s + i)] + to_q23(left[i]));
            out[2 * (s + i) + 1] = sat24(out[2 * (s + i) + 1] + to_q23(right[i]));
        }
        s += m;
    }
    sounding = static_cast<unsigned>(patch->voices_sounding());
}
