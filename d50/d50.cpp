// SPDX-License-Identifier: GPL-3.0-or-later
// The glue between the FM-1 firmware and the PicoFaceD5 engine (third_party/picofaced5, GPL-3.0-or-later, Michi71). d50.h says what each call does.
// On fm1-base the firmware links no closed library and is GPL-3.0 throughout; the engine's author agreed to this use
// (Michi71/PicoVintageSynthCollection#194).
#include "d50.h"
// On fm1-base (fm1base/, fm1.toml data_lo): the data at the start of the data area, the second core through plat_cpu2_*, the big buffers in the
// platform's pool. plat.h is C; the few calls are declared here.
extern "C" {
#include "d50_inflate.h"
#include <string.h>
const uint8_t *app_d50_data(uint32_t *len);      // fm1base/app.c: the resource region's payload, or 0
int plat_cpu2_on(void);
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg);
int plat_cpu2_wait(void);
uint32_t plat_ticks24(void);
}
#if FM1_CPU1                                     // fm1.toml cpu2 = true
#define FM1B_SPLIT 1
#endif
#define D50_POOL __attribute__((section(".pool")))
// the data: the resource region (fm1.toml resources, app.h APP_RES_BASE), which the update loader writes from the package; app.c finds it
// (a build without the region, as GitHub's, reads it as it lies: issue #36)
static const uint8_t *fm1_flash_map(uint32_t off, uint32_t n)
{
    uint32_t len;
    const uint8_t *p = app_d50_data(&len);
    return p && off <= len && n <= len - off ? p + off : nullptr;
}

// FM1_D50_PROF: where a block's time goes on the FM-1 (app.c's APP_SYSEX: cmd 11 reads it, cmd 12 leaves stages out). TIMER4, 24 MHz, both cores.
#ifndef FM1_D50_PROF
#define FM1_D50_PROF 1
#endif
#if FM1_D50_PROF
extern "C" {
volatile uint32_t d50_prof_t[8];                 // the last time at each mark (5, 6: CPU1's job; 7: after the LFO steps)
volatile uint32_t d50_prof_sum[9];               // 0 parts, 1 lists, 2 CPU0's voices, 3 the wait for CPU1, 4 the effects and mix, 5 CPU1's job,
                                                 // 6 the LFO steps (part of 1), 7 all of d50_render, 8 app.c's resampling
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
        d50_prof_sum[6] += d50_prof_t[7] - d50_prof_t[0];
        d50_prof_t[5] = d50_prof_t[6] = 0;
    }
    d50_prof_t[k] = t;
}
#define D5_MARK(k) d50_mark(k)
#define D5_SKIP(bit) ((d50_prof_skip & (bit)) != 0U)
#endif

// Blocks of up to 96 samples (app.c asks for 92 or 93), and the effects one such block (3 ms) behind the voices, so that CPU0 runs them
// while CPU1 renders voices (d5_patch.h render_part)
#if !defined(D5_FX_LAG)
#define D5_MAX_BLOCK 96
#define D5_FX_LAG 96
#endif
#include "d5_engine/d5_patch.h"
#include "d5_engine/d5_patch_map.h"

// No libc++ here: placement new by hand. The engine is constructed in d50_bind_flash, not by a static constructor.
inline void *operator new(decltype(sizeof 0), void *where) noexcept { return where; }

namespace {

constexpr unsigned kBlock = static_cast<unsigned>(d5::Tone<16>::kMaxBlock);

// The other core (fm1.toml cpu2: FM1B_SPLIT, plat_cpu2_*). begin hands CPU1 the job or says no (CPU1 not up, or the platform holding it for
// a flash write: the job then runs here). If the platform gives up waiting (cpu2_fallback), the split is switched off for good, so that a
// stuck CPU1 costs one block and not every block.
volatile bool split_on = true;
volatile unsigned split_blocks, split_lost;
#if FM1B_SPLIT
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
    // CPU0 also mixes, runs the effects and resamples, so an even share of the voices leaves CPU1 idle part of the block. The share follows
    // what CPU0 waited: over 40 us, CPU1 had too much and gets a sixteenth less; under 5 us, CPU1 was done first and gets a sixteenth more.
    const uint32_t waited_us = (plat_ticks24() - t0) / 24U;
    if (waited_us > 40U && split.share16 > 4) --split.share16;
    else if (waited_us < 5U && split.share16 < 13) ++split.share16;
}
#else
d5::Patch::Split split;
#endif

// The CPU guard (app.c app_render, after each block): over 90 % of the block on CPU0 it retires the oldest released voice
// (a 20 ms fade, PicoFaceD5's shed_voice). With none to retire and over 93 % -- or a block missed (the caller says 100) -- it fades the voice
// held longest (shed_held_voice) and caps each tone one voice under what it may have, so that no new note takes the room back: a voice
// less is better than the audio dropping out (issue #13: Afterthought missed 190 blocks in 4 s with 16 voices held at 96 %). Under 60 %
// for a quarter of a second the cap comes back up a voice at a time; a new program starts uncapped. After a step it waits 8 blocks for the
// fade to show.
unsigned guard_hold, guard_calm;
int guard_cap = 16;

enum : unsigned { kEvNote = 1, kEvBend = 2, kEvPanic = 3, kEvCc = 4, kEvPressure = 5, kEvTune = 6, kEvTranspose = 7, kEvMode = 8, kEvSep = 9,
                  kEvRpnReset = 10, kEvSensing = 11, kQueue = 64 };
// The mono mode (issue #60): up to 8 parts, each one note at a time with its own bend. The audio interrupt's.
constexpr unsigned kMonoParts = 8;
struct Mono {
    unsigned mode = D50_MODE_NORMAL;
    unsigned parts = 0;                          // 0: not mono
    int key[kMonoParts];                         // the key of the part's note (its note-off's), -1 none
    int note[kMonoParts];                        // the note it sounds (after Key Transpose)
    unsigned bend[kMonoParts];
} mono;
// The patch's SepCH in SEP and SEP-S (issue #62): the Upper tone's own controller set, as the firmware's 0xD308 set. The audio interrupt's.
struct Sep {
    unsigned bend = 8192;
    unsigned rpn = 0x3FFF;
    bool nrpn = false;                           // (as the basic channel's, #66)
    bool hold = false;
    bool porta_switch = false;
    int porta_time = 0;
    uint8_t sounded[128] = {};                   // the note a SepCH key sounded (itself: Key Transpose moves only the keyboard's, #64)
    uint32_t down[4] = {0, 0, 0, 0};
    uint32_t held[4] = {0, 0, 0, 0};
} sep;
constexpr unsigned kChaseCc = 80;                // Chase Play on (>= 64) or off: ours, the D-50 has no CC for it (issue #18)

// The controllers' state, the audio interrupt's (d50_render's events): what a program change keeps (the lever, the pressure, CC7: the
// voices' and the patch's live controls) and what it resets to the new patch's (the bender range, the portamento). Issue #4.
struct Controls {
    int tune = 50;                               // Master Tune, 0..100 (427.5 .. 452.6 Hz, 50 = 440.0: the firmware's range table, #58), issue #54
    int transpose = 0;                           // Key Transpose, -12..12 semitones (issue #54)
    uint8_t sounded[128] = {};                   // the note a key's note-on sounded (its note-off ends that one, whatever the transpose is now)
    unsigned bend = 8192;                        // the last pitch bend, 0..16383
    int bend_range = -1;                         // RPN 0's semitones, -1: the patch's (pb26)
    unsigned rpn = 0x3FFF;                       // CC101 << 7 | CC100; 0x3FFF: none (RPN null)
    bool nrpn = false;                           // CC 98/99 came after the last CC 100/101: a data entry is not an RPN's (0xFE34.2, #66)
    uint8_t fine_msb = 0xFF, fine_lsb = 0xFF;    // RPN 1's data entry, 0xFF: not received (0xFE62/0xFE63, #66)
    unsigned volume = 127;                       // CC7
    unsigned wheel = 0, pressure = 0;            // CC1, channel pressure (again after a program change: the separate modes route them, #62)
    bool porta_switch = false;
    int porta_time = 0;                          // 0..100
    bool hold = false;                           // CC64
    bool chase = false;                          // Chase Play on (the D-50's CHASE button: the panel's ARP key, or CC80)
    uint32_t down[4] = {0, 0, 0, 0};             // the notes whose key is down, a bit each
    uint32_t held[4] = {0, 0, 0, 0};             // released while the pedal held them
} ctl;
volatile unsigned tune_serial;                   // counts the Master Tunes MIDI set, for the panel (d50_midi_tune)
void after_configure();                          // (below, with the other controls)
void mono_set(unsigned mode, unsigned parts);
void release_note(int note);                     // (below, with Chase Play: a key's note-off)
void chase_clear();

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

// The reverb's delay lines (42 KB of 16-bit words) in the pool, as two segments. Their sizes are those of the two idle effect units of
// M-VAVE's SDK that the first builds used (24 416 bytes each, less a state word at 24 380), kept so that the reverb sounds the same.
enum : unsigned { kFxUnit = 24416, kFxState = 24380 };
bool reverb_attached;

d5::ReverbSample reverb_lines[kFxState / 2U + (kFxUnit - 4U) / 2U] D50_POOL;
void attach_reverb()
{
    reverb_attached = true;
    patch->attach_reverb(reverb_lines, static_cast<int>(kFxState / 2U), reverb_lines + kFxState / 2U, static_cast<int>((kFxUnit - 4U) / 2U));
}

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

// The temporary area (the D-50's edit buffer, SysEx 00 00 00): the current patch's 448 bytes, which a program change copies from the bank
// and SysEx DT1 edits (issue #6). Every spec is built from it; spec_live says the interrupt is to apply it as a live edit (Patch::reconfigure:
// what sounds goes on) instead of a new program (configure).
uint8_t temp_patch[D50_PATCH_BYTES];
bool spec_live[2];
// Chase Play's patch parameters (the patch block's bytes 34..36: mode 0 U-L, 1 U-L-L, 2 U-L-U; level 0..100; time 0..100), with the spec
struct ChaseSpec { uint8_t mode, level, time; } chase_spec[2];
bool temp_dirty;                                 // DT1 changed the temporary area since its spec was built (main loop)

void build_spec(bool live)
{
    const unsigned slot = (spec_serial + 1U) & 1U;
    new (spec_memory[slot]) d5::PatchSpec(d5::patch_from_bytes(temp_patch, pcm));   // (trivially destructible: nothing to end first)
    patch_name(temp_patch, names[slot]);
    spec_live[slot] = live;
    chase_spec[slot] = ChaseSpec{temp_patch[6 * 64 + 34], temp_patch[6 * 64 + 35], temp_patch[6 * 64 + 36]};
    spec_serial = spec_serial + 1U;
}

void build_program(unsigned program)
{
    memcpy(temp_patch, bank + program * D50_PATCH_BYTES, D50_PATCH_BYTES);
    program_now = program;
    temp_dirty = false;
    build_spec(false);
}

// The banks after the samples (tools/d50_pack.js). Version 2: one, its raw size and its raw deflate stream. Version 3 (fm1-base, issue #7):
// the header's last word is the bank section's bytes, the section the banks' count, then each bank's raw size, deflated size and stream.
// One bank is unpacked into RAM at a time (bank[]); a SysEx edit of it (#6) lasts until another bank is chosen.
unsigned bank_count, bank_now;
unsigned unpack_bank(const uint8_t *header, unsigned which)
{
    const uint32_t at = D50_HEADER_BYTES + D50_PCM_BYTES, bytes = u32(header + 12);
    if (bytes == 0) return 0;
    const uint8_t *b;
    uint32_t raw, deflated;
    if (header[4] == 2U) {
        if (which != 0) return 0;
        b = fm1_flash_map(at, 4U + bytes);
        if (!b) return 0;
        raw = u32(b);
        deflated = bytes;
        b += 4;
    } else if (header[4] == 3U) {
        const uint8_t *sec = fm1_flash_map(at, bytes);
        if (!sec || bytes < 4U || which >= u32(sec)) return 0;
        uint32_t off = 4;
        for (unsigned k = 0;; ++k) {
            if (off + 8U > bytes) return 0;
            raw = u32(sec + off);
            deflated = u32(sec + off + 4);
            if (deflated > bytes - off - 8U) return 0;
            if (k == which) break;
            off += 8U + deflated;
        }
        b = sec + off + 8;
    } else {
        return 0;
    }
    if (raw == 0 || raw % D50_PATCH_BYTES || raw > sizeof bank) return 0;
    if (d50_inflate(bank, raw, b, deflated) != static_cast<int>(raw)) return 0;
    return raw / D50_PATCH_BYTES;
}
unsigned bind_bank(const uint8_t *header)
{
    bank_count = 0;
    if (header[4] == 2U) bank_count = 1;
    if (header[4] == 3U) {
        const uint8_t *sec = fm1_flash_map(D50_HEADER_BYTES + D50_PCM_BYTES, 4);
        bank_count = sec ? u32(sec) : 0U;
    }
    if (bank_now >= bank_count) bank_now = 0;
    return bank_count ? unpack_bank(header, bank_now) : 0;
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
    if (!header || memcmp(header, D50_FLASH_MAGIC, 4) || header[4] < 1U || header[4] > 3U) return 0;
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
    mono_set(mono.mode, mono.parts);             // (a new engine: the mode as it was, nothing sounding)
    attach_reverb();                             // before configure, which binds the reverb's lines
    build_program(program_now);
    patch->configure(spec_at(spec_serial & 1U), static_cast<float>(D50_RATE));
    spec_done = spec_serial;
    after_configure();
    queue_tail = queue_head;
    ready = 1;
    return 1;
}

extern "C" void d50_note_ch(unsigned part, unsigned note, unsigned velocity)
{
    push(kEvNote << 24 | (part & 15U) << 16 | (note & 127U) << 8 | (velocity & 127U));
}
extern "C" void d50_bend_ch(unsigned part, unsigned value) { push(kEvBend << 24 | (part & 15U) << 16 | (value & 16383U)); }
extern "C" void d50_note(unsigned note, unsigned velocity) { d50_note_ch(0, note, velocity); }
extern "C" void d50_key(unsigned note, unsigned velocity)       // (the keyboard's flag, bit 20: Key Transpose moves it, #64)
{
    push(kEvNote << 24 | 1U << 20 | (note & 127U) << 8 | (velocity & 127U));
}
extern "C" void d50_bend(unsigned value) { d50_bend_ch(0, value); }
extern "C" void d50_sep(unsigned status, unsigned d1, unsigned d2)
{
    push(kEvSep << 24 | (status >> 4 & 15U) << 16 | (d1 & 127U) << 8 | (d2 & 127U));
}
extern "C" unsigned d50_sep_channel(void) { return ready && patch ? static_cast<unsigned>(patch->sep_channel()) : 0U; }
extern "C" void d50_mode(unsigned mode, unsigned channels)
{
    push(kEvMode << 24 | (mode & 3U) << 8 | (channels > kMonoParts ? kMonoParts : channels));
}
extern "C" void d50_panic(void) { push(kEvPanic << 24); }
extern "C" void d50_rpn_reset(void) { push(kEvRpnReset << 24); }
extern "C" void d50_sensing_lost(void) { push(kEvSensing << 24); }
extern "C" void d50_cc(unsigned cc, unsigned value) { push(kEvCc << 24 | (cc & 127U) << 8 | (value & 127U)); }
extern "C" unsigned d50_midi_tune(void) { return tune_serial << 8 | static_cast<unsigned>(ctl.tune); }
extern "C" void d50_tune(unsigned value) { push(kEvTune << 24 | (value > 100U ? 100U : value)); }
extern "C" void d50_transpose(int semitones) { push(kEvTranspose << 24 | static_cast<unsigned>(semitones < -12 ? -12 : semitones > 12 ? 12 : semitones) + 12U); }
extern "C" void d50_pressure(unsigned value) { push(kEvPressure << 24 | (value & 127U)); }

namespace {
bool bit_of(const uint32_t *set, int note) { return (set[note >> 5] >> (note & 31)) & 1U; }
void bit_set(uint32_t *set, int note, bool on)
{
    if (on) set[note >> 5] |= 1U << (note & 31);
    else set[note >> 5] &= ~(1U << (note & 31));
}
float bend_semis(unsigned bend)
{
    const int range = ctl.bend_range >= 0 ? ctl.bend_range : spec_at(spec_done & 1U).bend_range;
    // (the master tune, 0.01003 semitone a step: 100.3 cents over the 100 steps)
    return (static_cast<int>(bend) - 8192) * (static_cast<float>(range) / 8192.0f) + static_cast<float>(ctl.tune - 50) * (1.0f / 100.0f) * 1.0029f;
}
// In mono each part's note has the part's bend (#60). Again after each block's events: a slot keeps the bend it had, and the lower tone's
// note starts 128 samples late (Patch::lower_later), so a new note would have another part's until then.
void mono_bends()
{
    for (unsigned k = 0; k < mono.parts; ++k)
        if (mono.note[k] >= 0) patch->set_note_bend(mono.note[k], bend_semis(mono.bend[k]));
}
void apply_bend()
{
    if (patch->separate()) {                     // (#62: the basic channel's bender moves the Lower, SepCH's the Upper)
        patch->set_tone_bend(1, bend_semis(ctl.bend));
        patch->set_tone_bend(0, bend_semis(sep.bend));
    } else {
        patch->set_bend_semis(bend_semis(ctl.bend));
    }
    mono_bends();
}
// The patch's Hold Mode (pb[21]: 0 U, 1 L, 2 UL; issue #63, the firmware's 0x249F): the whole and dual modes hold both tones; the split
// modes only the tones it names, a note above the split point being the Upper's; the separate modes the Lower by the basic channel's
// pedal (unless U) and the Upper by SepCH's (unless L)
bool basic_holds(int note)
{
    const d5::PatchSpec &s = spec_at(spec_done & 1U);
    if (s.key_mode != d5::KeyMode::kSplit) return true;   // (the separate modes are kSplit too)
    const bool upper = !s.separate && note >= s.split_point;
    return upper ? s.hold_mode != 1 : s.hold_mode != 0;
}
bool sep_holds() { return spec_at(spec_done & 1U).hold_mode != 1; }
// Another part sounds this note too (mono): the engine's voices are the note's, so it ends with the last part's note-off
bool mono_shared(unsigned part, int note)
{
    for (unsigned k = 0; k < mono.parts; ++k)
        if (k != part && mono.note[k] == note) return true;
    return false;
}
void mono_clear()                                // nothing sounds (a panic, a new program)
{
    for (unsigned k = 0; k < kMonoParts; ++k) mono.key[k] = mono.note[k] = -1;
}
void mono_set(unsigned mode, unsigned parts)
{
    mono.mode = mode;
    mono.parts = mode == D50_MODE_MONO ? (parts == 0 ? 1U : parts) : 0U;
    mono_clear();
    for (unsigned k = 0; k < kMonoParts; ++k) mono.bend[k] = ctl.bend;
    patch->set_solo_off(mode != D50_MODE_NORMAL);
}
// A part's note on or off in mono: one note a part, as the D-50's fixed voice per channel (0x2BC1), whatever the key mode; no Chase Play
void mono_note(unsigned part, int key, int velocity, int shift)
{
    if (part >= mono.parts) return;
    if (velocity) {
        const int old = mono.note[part];
        if (old >= 0) {                          // the part's last note ends
            mono.note[part] = -1;
            bit_set(ctl.held, old, false);
            if (!mono_shared(part, old)) {
                bit_set(ctl.down, old, false);
                patch->note_off(old);
            }
        }
        const int n = key + shift < 0 ? 0 : key + shift > 127 ? 127 : key + shift;   // (Key Transpose: the keyboard's only, #64)
        mono.key[part] = key;
        mono.note[part] = n;
        bit_set(ctl.down, n, true);
        bit_set(ctl.held, n, false);
        patch->note_on(n, static_cast<float>(velocity) / 127.0f);
        patch->set_note_bend(n, bend_semis(mono.bend[part]));
    } else if (mono.key[part] == key) {
        const int n = mono.note[part];
        mono.key[part] = -1;
        if (ctl.hold && basic_holds(n)) {      // the pedal holds it (Hold Mode, #63): hold_release ends it
            bit_set(ctl.held, n, true);
            return;
        }
        mono.note[part] = -1;
        if (!mono_shared(part, n)) {
            bit_set(ctl.down, n, false);
            patch->note_off(n);
        }
    }
}
void apply_volume() { patch->set_volume(spec_at(spec_done & 1U).volume * (static_cast<float>(ctl.volume) / 127.0f)); }
// after configure (a new program, or the first): the new patch's own bender range and portamento; the volume, the bend and the
// pedal's notes as they were (configure stopped what sounded, so nothing is held any more)
void after_configure()
{
    const d5::PatchSpec &s = spec_at(spec_done & 1U);
    ctl.bend_range = -1;
    guard_cap = 16;                              // (the CPU guard's cap was the last patch's)
    guard_hold = guard_calm = 0;
    patch->cap_voices(16);
    ctl.porta_switch = s.upper.voice.porta_switch;
    ctl.porta_time = s.upper.voice.porta_time;
    for (unsigned i = 0; i < 4; ++i) ctl.held[i] = 0;
    chase_clear();                               // (the mono parts stay, mode and notes: configure leaves the keys down, and a part's note-off ends its note)
    // The separate modes (#62): SepCH's set starts at rest when the patch is one (the firmware's 0x5BB7), the pedal up; the basic channel's
    // lever and pressure go to the Lower only, or to both tones again
    for (int n = 0; n < 128; ++n)
        if (bit_of(sep.held, n)) patch->sep_note_off(n);
    for (unsigned i = 0; i < 4; ++i) sep.held[i] = 0;
    sep.hold = false;
    sep.bend = 8192;
    sep.rpn = 0x3FFF;
    sep.nrpn = false;
    sep.porta_switch = s.upper.voice.porta_switch;
    sep.porta_time = s.upper.voice.porta_time;
    if (patch->separate()) {
        patch->set_tone_wheel(0, 0.0f);
        patch->set_tone_aftertouch(0, 0.0f);
        patch->set_tone_wheel(1, static_cast<float>(ctl.wheel) / 127.0f);
        patch->set_tone_aftertouch(1, static_cast<float>(ctl.pressure) / 127.0f);
    } else {
        patch->set_mod_wheel(static_cast<float>(ctl.wheel) / 127.0f);
        patch->set_aftertouch(static_cast<float>(ctl.pressure) / 127.0f);
    }
    apply_bend();
    apply_volume();
}
// The basic channel's lever, pressure and portamento: in the separate modes the Lower's only (#62)
void apply_wheel()
{
    if (patch->separate()) patch->set_tone_wheel(1, static_cast<float>(ctl.wheel) / 127.0f);
    else patch->set_mod_wheel(static_cast<float>(ctl.wheel) / 127.0f);
}
void apply_pressure()
{
    if (patch->separate()) patch->set_tone_aftertouch(1, static_cast<float>(ctl.pressure) / 127.0f);
    else patch->set_aftertouch(static_cast<float>(ctl.pressure) / 127.0f);
}
void apply_porta()
{
    if (patch->separate()) patch->set_tone_porta(1, ctl.porta_switch, ctl.porta_time);
    else patch->set_porta(ctl.porta_switch, ctl.porta_time);
}
// A message on SepCH (#62, the firmware's 0x5DB7): the Upper's notes and its own controllers. Taken only in the separate modes; a note-off
// of a SepCH note still sounding is taken after a program change too, so that none is left on.
void sep_note(int key, int velocity)
{
    if (velocity) {
        if (!patch->separate()) return;
        const int n = key;                       // (MIDI's: Key Transpose does not move it, #64)
        sep.sounded[key] = static_cast<uint8_t>(n);
        bit_set(sep.down, n, true);
        bit_set(sep.held, n, false);
        patch->sep_note_on(n, static_cast<float>(velocity) / 127.0f);
        return;
    }
    const int n = sep.sounded[key];
    if (!bit_of(sep.down, n)) return;
    bit_set(sep.down, n, false);
    if (sep.hold && sep_holds()) bit_set(sep.held, n, true);   // (Hold Mode, #63)
    else patch->sep_note_off(n);
}
void sep_control(unsigned cc, unsigned v)
{
    switch (cc) {
    case 1: patch->set_tone_wheel(0, static_cast<float>(v) / 127.0f); break;
    case 5:
        sep.porta_time = static_cast<int>((v * 100U + 63U) / 127U);
        patch->set_tone_porta(0, sep.porta_switch, sep.porta_time);
        break;
    case 65:
        sep.porta_switch = v >= 64U;
        patch->set_tone_porta(0, sep.porta_switch, sep.porta_time);
        break;
    case 64:
        sep.hold = v >= 64U;
        if (!sep.hold) {
            for (int n = 0; n < 128; ++n)
                if (bit_of(sep.held, n)) patch->sep_note_off(n);
            for (unsigned i = 0; i < 4; ++i) sep.held[i] = 0;
        }
        break;
    case 123:                                    // All Notes Off, SepCH's tone (0x5F3C)
        for (int n = 0; n < 128; ++n)
            if (bit_of(sep.down, n) || bit_of(sep.held, n)) patch->sep_note_off(n);
        for (unsigned i = 0; i < 4; ++i) sep.down[i] = sep.held[i] = 0;
        break;
    case 101: sep.rpn = (sep.rpn & 127U) | v << 7; sep.nrpn = false; break;
    case 100: sep.rpn = (sep.rpn & ~127U) | v; sep.nrpn = false; break;
    case 98: case 99: sep.nrpn = true; break;    // (an NRPN: the data entries after it are not taken)
    case 6:
        if (!sep.nrpn && sep.rpn == 0) {                      // RPN 0: the bender range (patch-common: both tones', as the basic channel's)
            ctl.bend_range = v > 12U ? 12 : static_cast<int>(v);
            apply_bend();
        }
        break;
    }
}
// Active Sensing lost (#68, the firmware's 0x574F): the notes off (released, 0x60D5), the controllers back to the panel's (0x327D: the
// bender centred, the lever and aftertouch at rest, volume full; the pedal up), the RPN null (0x5BEC), SepCH's set too (0x5BB7)
void sensing_lost()
{
    for (unsigned k = 0; k < mono.parts; ++k)
        if (mono.note[k] >= 0) patch->note_off(mono.note[k]);
    mono_clear();
    for (int n = 0; n < 128; ++n) {
        if (bit_of(ctl.down, n) || bit_of(ctl.held, n)) release_note(n);
        if (bit_of(sep.down, n) || bit_of(sep.held, n)) patch->sep_note_off(n);
    }
    for (unsigned i = 0; i < 4; ++i) ctl.down[i] = ctl.held[i] = sep.down[i] = sep.held[i] = 0;
    ctl.hold = sep.hold = false;
    ctl.bend = sep.bend = 8192;
    for (unsigned k = 0; k < kMonoParts; ++k) mono.bend[k] = 8192;
    ctl.wheel = ctl.pressure = 0;
    ctl.volume = 127;
    ctl.rpn = sep.rpn = 0x3FFF;
    ctl.nrpn = sep.nrpn = false;
    ctl.fine_msb = ctl.fine_lsb = 0xFF;
    apply_wheel();
    apply_pressure();
    if (patch->separate()) {
        patch->set_tone_wheel(0, 0.0f);
        patch->set_tone_aftertouch(0, 0.0f);
    }
    apply_bend();
    apply_volume();
}
void sep_event(unsigned type, unsigned d1, unsigned d2)
{
    switch (type) {
    case 0x9: sep_note(static_cast<int>(d1), static_cast<int>(d2)); break;
    case 0x8: sep_note(static_cast<int>(d1), 0); break;
    case 0xE:
        if (!patch->separate()) break;
        sep.bend = d1 | d2 << 7;                 // (the MIDI bytes: the LSB, then the MSB)
        patch->set_tone_bend(0, bend_semis(sep.bend));
        break;
    case 0xD: if (patch->separate()) patch->set_tone_aftertouch(0, static_cast<float>(d1) / 127.0f); break;
    case 0xB: if (patch->separate()) sep_control(d1, d2); break;
    }
}
void hold_release()
{
    for (unsigned k = 0; k < mono.parts; ++k)    // (mono: a part whose note the pedal held has none now)
        if (mono.note[k] >= 0 && mono.key[k] < 0) mono.note[k] = -1;
    for (int note = 0; note < 128; ++note)
        if (bit_of(ctl.held, note)) release_note(note);
    for (unsigned i = 0; i < 4; ++i) ctl.held[i] = 0;
}
// RPN 1, Fine Tuning, as the D-50's Master Tune (0x4DA6, #66): once both bytes have come, the 14-bit value's top 8 bits less 64, clamped
// to 0..127, x 200 / 256 (0x417E): 0..99, the centre 0x2000 giving 50, 440.0 Hz; about +-50 cents over half the RPN's range
void fine_tune()
{
    if (ctl.fine_msb > 127U || ctl.fine_lsb > 127U) return;
    int a = ctl.fine_msb * 2 + ((ctl.fine_lsb >> 6) & 1) - 64;
    a = a < 0 ? 0 : a > 127 ? 127 : a;
    ctl.tune = a - ((a * 56) >> 8);
    apply_bend();
    tune_serial = tune_serial + 1U;
}
void control(unsigned cc, unsigned v)
{
    switch (cc) {
    case 1: ctl.wheel = v; apply_wheel(); break;
    case 5:
        ctl.porta_time = static_cast<int>((v * 100U + 63U) / 127U);
        apply_porta();
        break;
    case 65:
        ctl.porta_switch = v >= 64U;
        apply_porta();
        break;
    case 7: ctl.volume = v; apply_volume(); break;
    case kChaseCc: ctl.chase = v >= 64U; break;
    case 64:
        ctl.hold = v >= 64U;
        if (!ctl.hold) hold_release();
        break;
    case 101: ctl.rpn = (ctl.rpn & 127U) | v << 7; ctl.nrpn = false; break;
    case 100: ctl.rpn = (ctl.rpn & ~127U) | v; ctl.nrpn = false; break;
    case 98: case 99: ctl.nrpn = true; break;    // an NRPN: the data entries after it are not taken (0x4D58, #66)
    case 6:
        if (ctl.nrpn) break;
        if (ctl.rpn == 0) {                      // RPN 0: the bender range, 0..12 semitones as the D-50's pb26
            ctl.bend_range = v > 12U ? 12 : static_cast<int>(v);
            apply_bend();
        } else if (ctl.rpn == 1) {               // RPN 1, Fine Tuning: its MSB (0x4D8F)
            ctl.fine_msb = static_cast<uint8_t>(v);
            fine_tune();
        }
        break;
    case 38:
        if (!ctl.nrpn && ctl.rpn == 1) {         // its LSB (0x4D96)
            ctl.fine_lsb = static_cast<uint8_t>(v);
            fine_tune();
        }
        break;
    }
}
}  // namespace

// ---- Chase Play (issue #18): the D-50 plays a key's note again after a delay, Upper then Lower (U-L), Upper then Lower repeated (U-L-L),
// or the two alternating (U-L-U); in Whole mode the repeats are the Upper's. Only in Whole and Dual (and their solo forms). The laws are the
// D-50 v1.06 firmware's (issue #19, its code at 0x278F, 0x2E0A and 0x419A; the patch's chase bytes at RAM 0xC5A2..0xC5A4):
//  - time: a timer tick adds (104 - time) x 64 to a 16-bit clock, and a repeat is due each time the clock has gone round once, so a step
//    takes 1024 / (104 - time) ticks (time 0: 9.8, 50: 19.0, 100: 256). The tick is an interrupt the CPU's own timers do not make (timer
//    0's modulus, 116 at 6 MHz, is far too fast), most likely the sound chips' clock: 32.768 MHz / 2^19 = 62.5 Hz, 16 ms. That is an
//    assumption, which a recording of Living Calliope bears out (time 50: 0.25-0.30 s a step measured, 0.30 s with 16 ms). Steps then run
//    from 0.16 s (time 0) to 4.1 s (time 100).
//  - level: the factor 2 x level + (143 x level) / 256 (0..255), and each repeat's velocity (1..127) is the last one's x factor / 256, the
//    first repeat's from the key's; the repeats end when it comes to 0. U-L plays one repeat.
// Each repeat is a note on in its tone, released when the next starts or a step after the last; a repeat in the Upper takes over from the
// held key's note. The repeats go on after the key is lifted, as an echo. Timed to the audio block (3 ms). The audio interrupt's.
namespace {
struct ChaseNote {
    bool on;                                     // the entry is in use
    bool done;                                   // the repeats are over (the entry waits for the key, if it is still down)
    bool upper_key;                              // the key's own Upper note still sounds (a repeat in the Upper ends it)
    uint8_t note, step;
    int8_t last_tone;                            // the tone of the last repeat still sounding, -1 none
    uint8_t vel;                                 // the last repeat's velocity (the key's before the first), 1..127
    uint32_t due;                                // the next step, in samples (chase_clock)
};
constexpr unsigned kChaseNotes = 16;
constexpr float kChaseTick = 524288.0f / 32768000.0f;   // seconds a firmware tick: 16 ms (assumed, above)
ChaseNote chase_notes[kChaseNotes];
uint32_t chase_clock;                            // samples rendered

const ChaseSpec &chase_now() { return chase_spec[spec_done & 1U]; }
bool chase_applies()
{
    if (!ctl.chase) return false;
    const d5::KeyMode m = patch->key_mode();
    return m == d5::KeyMode::kWhole || m == d5::KeyMode::kDual;
}
uint32_t chase_interval()
{
    const unsigned t = chase_now().time > 100 ? 100U : chase_now().time;
    const float ticks = 1024.0f / static_cast<float>(104U - t);
    return static_cast<uint32_t>(ticks * kChaseTick * static_cast<float>(D50_RATE)) + 1U;
}
unsigned chase_factor()                          // the firmware's level factor, 0..255 (0x419A)
{
    const unsigned l = chase_now().level > 100 ? 100U : chase_now().level;
    return 2U * l + ((143U * l) >> 8);
}
int chase_tone(unsigned step)                    // the tone of repeat step (1, 2, ..)
{
    if (patch->key_mode() == d5::KeyMode::kWhole) return 0;
    if (chase_now().mode == 2U) return (step & 1U) ? 1 : 0;   // U-L-U: Lower, Upper, Lower, ..
    return 1;                                    // U-L, U-L-L: the Lower
}
void chase_end(ChaseNote &c)                     // the repeats over: the last one released
{
    if (c.last_tone >= 0) patch->tone_note_off(c.last_tone, c.note);
    c.last_tone = -1;
    c.done = true;
    if (!c.upper_key) c.on = false;
}
ChaseNote *chase_of(int note)
{
    for (ChaseNote &c : chase_notes)
        if (c.on && c.note == note) return &c;
    return nullptr;
}
void chase_start(int note, unsigned vel)
{
    if (ChaseNote *old = chase_of(note)) {       // struck again: the old one's repeats end, its Upper note too
        chase_end(*old);
        if (old->upper_key) patch->tone_note_off(0, note);
        old->on = false;
    }
    ChaseNote *c = nullptr;
    for (ChaseNote &e : chase_notes)
        if (!e.on) { c = &e; break; }
    if (!c) {                                    // all in use: the one due last gives way (its repeats end)
        c = &chase_notes[0];
        for (ChaseNote &e : chase_notes)
            if (static_cast<int32_t>(e.due - c->due) > 0) c = &e;
        chase_end(*c);
        if (c->upper_key) patch->tone_note_off(0, c->note);
    }
    *c = ChaseNote{true, false, true, static_cast<uint8_t>(note), 0, -1, static_cast<uint8_t>(vel), chase_clock + chase_interval()};
    patch->tone_note_on(0, note, static_cast<float>(vel) / 127.0f);
}
void chase_service()
{
    const unsigned f = chase_factor();
    const unsigned max = chase_now().mode == 0U ? 1U : 255U;
    for (ChaseNote &c : chase_notes) {
        if (!c.on || c.done || static_cast<int32_t>(chase_clock - c.due) < 0) continue;
        const unsigned step = c.step + 1U;
        const unsigned v7 = (c.vel * f) >> 8;    // the firmware's: the last velocity x factor / 256, 0 ends
        if (step > max || v7 == 0U) { chase_end(c); continue; }
        c.vel = static_cast<uint8_t>(v7);
        const float v = static_cast<float>(v7) / 127.0f;
        if (c.last_tone >= 0) patch->tone_note_off(c.last_tone, c.note);
        const int tone = chase_tone(step);
        if (tone == 0 && c.upper_key) {          // a repeat in the Upper takes over from the key's own note
            patch->tone_note_off(0, c.note);
            c.upper_key = false;
        }
        patch->tone_note_on(tone, c.note, v);
        c.last_tone = static_cast<int8_t>(tone);
        c.step = static_cast<uint8_t>(step);
        c.due += chase_interval();
    }
}

// a key's note-off (or the hold pedal's): a chased note ends only its own Upper note, the repeats go on
void release_note(int note)
{
    ChaseNote *c = chase_of(note);
    if (!c) { patch->note_off(note); return; }
    if (c->upper_key) { patch->tone_note_off(0, note); c->upper_key = false; }
    if (c->done) c->on = false;
}
void chase_clear()
{
    for (ChaseNote &c : chase_notes) c.on = false;
}
}  // namespace

// ---- SysEx: the D-50's exclusive (Roland, model 0x14), main loop. Two ways, as the D-50's own Data Transfer menu has them ("Bulk Dump.O",
// "Bulk Load.O" one-way; "Bulk Dump", "Bulk Load" with the handshake):
// - one-way, DT1 and RQ1, as upstream PicoFaceD5 speaks it (issue #6);
// - the handshake (issue #51): WSD, RQD, DAT, ACK, EOD, ERR, RJC, Roland's protocol of the time (the MT-32's and D-110's manuals: the
//   receiver answers ACK to each WSD, DAT and EOD it takes, ERR to a checksum fault (the sender sends that message again or gives up with
//   RJC) and RJC when it cannot; the sender sends the next DAT only after the ACK of the last). WSD and RQD carry three address and three
//   size bytes as RQ1 does, DAT up to 256 data bytes as DT1 does, the checksum over the address (and size or data). Read from the
//   protocol, not yet checked against a real D-50.
// Addresses are three 7-bit bytes, here one 21-bit number: the temporary area at 00 00 00 (448 bytes: upper partial 1, partial 2, common,
// lower partial 1, partial 2, common, patch, 64 bytes each), internal memory at 02 00 00 (the bank, 448 bytes a patch, held in RAM: a
// power cycle brings back the bank in the flash). Anything else (the reverb data at 03 60 00) is taken and ignored.
namespace {
constexpr uint8_t kRoland = 0x41, kModel = 0x14, kRq1 = 0x11, kDt1 = 0x12;
constexpr uint8_t kWsd = 0x40, kRqd = 0x41, kDat = 0x42, kAck = 0x43, kEod = 0x45, kErr = 0x4E, kRjc = 0x4F;
constexpr uint32_t kInternal = 2U << 14;
constexpr unsigned kChunk = 256;                  // the data bytes of one DT1 or DAT the D-50 sends

uint32_t addr_of(const uint8_t *a) { return static_cast<uint32_t>(a[0]) << 14 | static_cast<uint32_t>(a[1]) << 7 | a[2]; }
// the byte at an address, nullptr where nothing is
uint8_t *byte_at(uint32_t a)
{
    if (a < D50_PATCH_BYTES) return temp_patch + a;
    if (a >= kInternal && a - kInternal < bank_patches * D50_PATCH_BYTES) return bank + (a - kInternal);
    return nullptr;
}
// the end of the area an address is in (the temporary area or the bank)
uint32_t end_of(uint32_t a) { return a < D50_PATCH_BYTES ? D50_PATCH_BYTES : kInternal + bank_patches * D50_PATCH_BYTES; }
uint8_t roland_sum(const uint8_t *p, unsigned n)
{
    unsigned s = 0;
    for (unsigned i = 0; i < n; ++i) s += p[i];
    return static_cast<uint8_t>((128U - (s & 127U)) & 127U);
}
// The temporary area's maxima (0 the minimum), byte by byte: a partial's 64, a tone common's 64, the patch block's 64, as the D-50 v1.06
// firmware's range tables (0x7D20, 0x7D60, 0x7DA0) have them; the bytes past the parameters take only 0. The D-50 skips a DT1 byte above
// its maximum (0x521A: the byte keeps its value, the address moves on), and so does write_at (#58, docs/D50-FIRMWARE.md).
constexpr uint8_t kPartialMax[64] = {
    72, 100, 16, 3, 2, 2, 1, 99, 100, 14, 5, 100, 14, 100, 30, 14, 127, 14, 100, 100, 4, 4, 100, 100, 100, 100, 100, 100, 100, 100, 100, 1,
    5, 100, 14, 100, 100, 127, 12, 100, 100, 100, 100, 100, 100, 100, 100, 100, 1, 4, 4, 5, 100, 14};
constexpr uint8_t kCommonMax[64] = {
    63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 6, 2, 4, 50, 50, 50, 50, 100, 100, 100, 100, 100, 100, 100, 100, 3, 100, 100, 2, 3, 100, 100,
    1, 3, 100, 100, 1, 15, 24, 21, 8, 24, 7, 100, 100, 100, 3, 100};
constexpr uint8_t kPatchMax[64] = {
    63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 8, 60, 2, 2, 48, 48, 100, 100, 12, 24, 100, 3, 31, 100,
    100, 100, 2, 100, 100, 16, 16};
// the maximum of a byte of the temporary area (its 7 blocks: the upper's partial 1, partial 2, common, the lower's, the patch block)
uint8_t temp_max(uint32_t a)
{
    const uint32_t block = a / 64U, i = a % 64U;
    return block == 6U ? kPatchMax[i] : block % 3U == 2U ? kCommonMax[i] : kPartialMax[i];
}
// a DT1's or DAT's data bytes into the areas (nothing where nothing is; in the temporary area, nothing above the byte's maximum)
void write_at(uint32_t a, const uint8_t *d, unsigned len)
{
    for (unsigned i = 0; i < len; ++i) {
        uint8_t *b = byte_at(a + i);
        if (!b) continue;
        if (a + i < D50_PATCH_BYTES) {
            if ((d[i] & 127U) > temp_max(a + i)) continue;
            temp_dirty = true;
        }
        *b = d[i] & 127U;
    }
}

// what goes out, one frame per d50_sysex_reply: a short answer first (ACK, ERR, RJC), then an RQ1's DT1s or an RQD's DATs and EOD
struct Reply {
    uint8_t dev;
    uint8_t cmd;                                  // kDt1 (one-way) or kDat (the handshake)
    uint32_t at, left;
    uint8_t answer;                               // a short answer due (kAck, kErr, kRjc), 0 none
    uint8_t answer_dev;
    // the handshake's sending side: a DAT (or the EOD) may go out (go), or went out and waits for the other side's ACK (sent: its length)
    bool go, eod, waiting;
    unsigned sent;
    // the receiving side: a WSD was taken (DATs and the EOD are expected)
    bool receiving;
} reply;

void answer(uint8_t dev, uint8_t cmd) { reply.answer = cmd; reply.answer_dev = dev; }   // (dev: the frame's, which is ours: d50_sysex)
void hs_stop() { reply.go = reply.eod = reply.waiting = reply.receiving = false; reply.sent = 0; if (reply.cmd == kDat) reply.left = 0; }
}  // namespace

static uint8_t device_id;                       // d50_device: the basic channel less one (issue #51)
extern "C" void d50_device(unsigned id) { device_id = static_cast<uint8_t>(id & 15U); }

static unsigned sysex_error;                     // the last frame's fault (d50.h D50_ERR_*), 0 none; d50_sysex_error reads and clears it

extern "C" unsigned d50_sysex_error(void)
{
    const unsigned e = sysex_error;
    sysex_error = 0;
    return e;
}

// the handshake (issue #51): p is 41 dev 14 cmd ..., n its length
static void d50_handshake(const uint8_t *p, uint32_t n)
{
    const uint8_t dev = p[1], cmd = p[3];
    switch (cmd) {
    case kWsd:                                    // 41 dev 14 40 a a a s s s sum: the other side wants to send
    case kRqd: {                                  // 41 dev 14 41 a a a s s s sum: the other side asks for data
        if (n != 11U) { sysex_error = D50_ERR_FORMAT; answer(dev, kRjc); return; }
        if (roland_sum(p + 4, 6U) != p[10]) { sysex_error = D50_ERR_SUM; answer(dev, kErr); return; }
        hs_stop();
        reply.left = 0;                           // (an RQ1's answer under way ends too)
        const uint32_t a = addr_of(p + 4);
        uint32_t size = addr_of(p + 7);
        if (!ready) { answer(dev, kRjc); return; }
        if (cmd == kWsd) {                        // taken anywhere, as a DT1 is: what is not here (the reverb data at 03 60 00 of a whole
            reply.receiving = true;               // bulk dump) is taken and ignored, so a dump is not stopped halfway
            answer(dev, kAck);
            return;
        }
        if (!byte_at(a) || !size) { sysex_error = D50_ERR_ADDRESS; answer(dev, kRjc); return; }   // (nothing there to send)
        if (size > end_of(a) - a) size = end_of(a) - a;
        reply.dev = dev;
        reply.cmd = kDat;
        reply.at = a;
        reply.left = size;
        reply.go = true;
        return;
    }
    case kDat: {                                  // 41 dev 14 42 a a a data.. sum
        if (n < 9U) { sysex_error = D50_ERR_FORMAT; answer(dev, kErr); return; }
        const unsigned len = n - 8U;
        if (roland_sum(p + 4, 3U + len) != p[n - 1]) { sysex_error = D50_ERR_SUM; answer(dev, kErr); return; }
        if (!ready) { answer(dev, kRjc); return; }
        write_at(addr_of(p + 4), p + 7, len);     // (taken without a WSD too: as a DT1 would be)
        answer(dev, kAck);
        return;
    }
    case kEod:                                    // 41 dev 14 45: the other side's data ends
        reply.receiving = false;
        answer(dev, kAck);
        return;
    case kAck:                                    // our DAT (or EOD) arrived
        if (!reply.waiting) return;
        reply.waiting = false;
        if (reply.eod) { hs_stop(); return; }     // (the EOD's ACK: done)
        reply.at += reply.sent;
        reply.left -= reply.sent;
        reply.sent = 0;
        if (reply.left) reply.go = true;
        else reply.eod = reply.go = true;
        return;
    case kErr:                                    // our last DAT (or EOD) arrived damaged: again
        if (reply.waiting) { reply.waiting = false; reply.go = true; }
        return;
    case kRjc:                                    // the other side ends the transfer
        hs_stop();
        return;
    default:
        sysex_error = D50_ERR_FORMAT;
        return;
    }
}

extern "C" int d50_sysex(const uint8_t *p, uint32_t n)
{
    if (n < 4U || p[0] != kRoland || p[2] != kModel) return 0;
    if (p[1] != device_id) return 1;              // another D-50's (issue #51): not done, not answered
    if (p[3] >= kWsd && p[3] <= kRjc) {           // the handshake: answered even without data (RJC), so the other side does not wait
        d50_handshake(p, n);
        return 1;
    }
    if (!ready) return 1;                         // (ours, but nothing to edit yet)
    if ((p[3] == kDt1 && n < 9U) || (p[3] == kRq1 && n != 11U) || (p[3] != kDt1 && p[3] != kRq1)) {
        sysex_error = D50_ERR_FORMAT;
        return 1;
    }
    if (p[3] == kDt1) {                           // 41 dev 14 12 a a a data.. sum
        const unsigned len = n - 8U;
        if (roland_sum(p + 4, 3U + len) != p[n - 1]) {
            sysex_error = D50_ERR_SUM;
            return 1;
        }
        write_at(addr_of(p + 4), p + 7, len);
    } else {                                      // RQ1: 41 dev 14 11 a a a s s s sum
        if (roland_sum(p + 4, 6U) != p[10]) {
            sysex_error = D50_ERR_SUM;
            return 1;
        }
        const uint32_t a = addr_of(p + 4);
        uint32_t size = addr_of(p + 7);
        // only what is there: the temporary area or the bank, cut at its end
        if (!byte_at(a)) {
            sysex_error = D50_ERR_ADDRESS;
            return 1;
        }
        if (size > end_of(a) - a) size = end_of(a) - a;
        hs_stop();                                // (a handshake under way ends)
        reply.dev = p[1];
        reply.cmd = kDt1;
        reply.at = a;
        reply.left = size;
    }
    return 1;
}

extern "C" unsigned d50_sysex_reply(uint8_t *frame)
{
    frame[0] = 0xF0;
    frame[1] = kRoland;
    frame[3] = kModel;
    if (reply.answer) {                           // ACK, ERR, RJC: F0 41 dev 14 cmd F7
        frame[2] = reply.answer_dev;
        frame[4] = reply.answer;
        frame[5] = 0xF7;
        reply.answer = 0;
        return 6U;
    }
    if (reply.cmd == kDat && !reply.go) return 0; // (the handshake: waiting for the other side's ACK)
    if (reply.cmd == kDat && reply.eod) {         // F0 41 dev 14 45 F7
        frame[2] = reply.dev;
        frame[4] = kEod;
        frame[5] = 0xF7;
        reply.go = false;
        reply.waiting = true;
        return 6U;
    }
    if (!reply.left) return 0;
    const unsigned len = reply.left > kChunk ? kChunk : reply.left;
    frame[2] = reply.dev;
    frame[4] = reply.cmd;
    frame[5] = static_cast<uint8_t>(reply.at >> 14 & 127U);
    frame[6] = static_cast<uint8_t>(reply.at >> 7 & 127U);
    frame[7] = static_cast<uint8_t>(reply.at & 127U);
    for (unsigned i = 0; i < len; ++i) {
        const uint8_t *b = byte_at(reply.at + i);
        frame[8 + i] = b ? *b : 0U;
    }
    frame[8 + len] = roland_sum(frame + 5, 3U + len);
    frame[9 + len] = 0xF7;
    if (reply.cmd == kDat) {                      // the next only after the ACK (kAck moves on, kErr sends this one again)
        reply.go = false;
        reply.waiting = true;
        reply.sent = len;
    } else {
        reply.at += len;
        reply.left -= len;
    }
    return 10U + len;
}

extern "C" int d50_edit_poll(void)
{
    if (!temp_dirty || !ready || spec_done != spec_serial) return 0;   // (the last spec not applied yet: next time)
    temp_dirty = false;
    build_spec(true);
    return 1;
}

extern "C" unsigned d50_chase_on(void) { return ctl.chase ? 1U : 0U; }
extern "C" unsigned d50_banks(void) { return ready ? bank_count : 0U; }
extern "C" unsigned d50_current_bank(void) { return bank_now; }
extern "C" int d50_bank(unsigned which)
{
    if (!ready) { bank_now = which; return 1; }  // (before the data: kept for the bind, which checks it)
    if (which >= bank_count) return 0;
    if (which == bank_now) return 1;
    const uint8_t *header = fm1_flash_map(0, D50_HEADER_BYTES);
    if (!header) return 0;
    const unsigned n = unpack_bank(header, which);
    if (!n) {                                    // (a damaged bank: the one before, back)
        bank_patches = unpack_bank(header, bank_now);
        return 0;
    }
    bank_now = which;
    bank_patches = n;
    if (program_now >= n) program_now = 0;
    return 1;                                    // (the caller then chooses the program: d50_program builds it)
}
extern "C" unsigned d50_programs(void) { return bank_patches ? bank_patches : 1U; }
extern "C" unsigned d50_bank_patches(void) { return bank_patches; }
extern "C" unsigned d50_current_program(void) { return program_now; }
extern "C" const char *d50_program_name(unsigned program)
{
    return ready && program == program_now ? names[spec_serial & 1U] : "";
}
extern "C" const uint8_t *d50_patch_bytes(void) { return ready ? temp_patch : nullptr; }
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
        if (percent > 93U && patch->shed_held_voice()) {
            const int tone_max = spec_at(spec_done & 1U).key_mode == d5::KeyMode::kWhole ? 16 : 8;
            if (guard_cap > tone_max) guard_cap = tone_max;
            if (guard_cap > 4) patch->cap_voices(--guard_cap);
            guard_hold = 8;
        }
    } else if (percent < 60U && guard_cap < 16) {
        if (++guard_calm >= 86U) { patch->cap_voices(++guard_cap); guard_calm = 0; }
    } else {
        guard_calm = 0;
    }
}
extern "C" unsigned d50_cores(void)
{
#if FM1B_SPLIT
    return split_on && plat_cpu2_on() ? 2U : 1U;
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
#if FM1_D50_PROF
    const uint32_t render_t0 = D50_T4;
#endif
    if (spec_done != spec_serial) {               // a new program: configure stops what sounds
        const unsigned serial = spec_serial;
        if (spec_live[serial & 1U]) {             // a SysEx edit of the temporary area: what sounds goes on (Patch::reconfigure)
            patch->reconfigure(spec_at(serial & 1U));
            spec_done = serial;
            apply_bend();                        // (the bender range or the level may be what was edited)
            apply_volume();
        } else {
            patch->configure(spec_at(serial & 1U), static_cast<float>(D50_RATE));
            spec_done = serial;
            after_configure();
        }
    }
    while (queue_tail != queue_head) {
        const uint32_t e = queue[queue_tail];
        queue_tail = (queue_tail + 1U) % kQueue;
        switch (e >> 24) {
        case kEvNote: {
            int note = static_cast<int>((e >> 8) & 127U);
            const int velocity = static_cast<int>(e & 127U);
            const bool keyboard = (e >> 20) & 1U;   // Key Transpose moves only the instrument's keys, as on the D-50 (#64, 0x2B46)
            if (mono.parts) {                    // mono (#60): the part's one note
                mono_note((e >> 16) & 15U, note, velocity, keyboard ? ctl.transpose : 0);
                break;
            }
            if (keyboard) {
                if (velocity) {                  // Key Transpose (issue #54): the note the key sounds, kept for its note-off
                    const int n = note + ctl.transpose;
                    ctl.sounded[note] = static_cast<uint8_t>(n < 0 ? 0 : n > 127 ? 127 : n);
                }
                note = ctl.sounded[note];
            }
            bit_set(ctl.down, note, velocity != 0);
            if (velocity) {
                bit_set(ctl.held, note, false);  // (struck again under the pedal: the new note is the one held now)
                if (chase_applies()) chase_start(note, static_cast<unsigned>(velocity));
                else patch->note_on(note, velocity / 127.0f);
            } else if (ctl.hold && basic_holds(note)) {   // (Hold Mode, #63)
                bit_set(ctl.held, note, true);   // CC64: the note-off waits for the pedal
            } else {
                release_note(note);
            }
            break;
        }
        case kEvTune: ctl.tune = static_cast<int>(e & 127U); apply_bend(); break;
        case kEvSensing: sensing_lost(); break;
        case kEvRpnReset:                        // (0x5BEC, 0x4F89: the RPN null, Fine Tuning's bytes not received; #66)
            ctl.rpn = 0x3FFF;
            ctl.fine_msb = ctl.fine_lsb = 0xFF;
            break;
        case kEvTranspose: ctl.transpose = static_cast<int>(e & 127U) - 12; break;
        case kEvBend: {
            const unsigned part = (e >> 16) & 15U;
            if (!mono.parts) {
                ctl.bend = e & 16383U;
                apply_bend();
            } else if (part < mono.parts) {      // mono (#60): the part's bender moves its note only; part 0's is also the global one
                mono.bend[part] = e & 16383U;
                if (part == 0) ctl.bend = e & 16383U;
                if (mono.note[part] >= 0) patch->set_note_bend(mono.note[part], bend_semis(mono.bend[part]));
            }
            break;
        }
        case kEvCc: control((e >> 8) & 127U, e & 127U); break;
        case kEvPressure: ctl.pressure = e & 127U; apply_pressure(); break;
        case kEvSep: sep_event((e >> 16) & 15U, (e >> 8) & 127U, e & 127U); break;
        case kEvPanic:
        case kEvMode:                            // a mode message (#60): the notes off, as every one on the D-50, then the mode
            patch->silence();
            chase_clear();
            mono_clear();
            ctl.hold = sep.hold = false;
            for (unsigned i = 0; i < 4; ++i) ctl.held[i] = ctl.down[i] = sep.held[i] = sep.down[i] = 0;
            if (e >> 24 == kEvMode) {
                mono_set((e >> 8) & 3U, e & 15U);
                apply_bend();
            }
            break;
        }
    }
    if (mono.parts) mono_bends();                // (a new lower note's bend: mono_bends)
    chase_service();                             // Chase Play's repeats that are due (to the block: 3 ms)
    // A block at a time, a share of the sounding voices on CPU1 (Patch::render_block; FM1B_SPLIT, split_begin)
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
    chase_clock += n;
    sounding = static_cast<unsigned>(patch->voices_sounding());
#if FM1_D50_PROF
    d50_prof_sum[7] += D50_T4 - render_t0;
#endif
}
