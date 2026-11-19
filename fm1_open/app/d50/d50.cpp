// SPDX-License-Identifier: GPL-3.0-or-later
// The glue between the FM-1 firmware and the PicoFaceD5 engine (third_party/picofaced5, GPL-3.0-or-later, Michi71). d50.h says what each call does.
// Built for our own use: the engine's licence has no linking exception for the FM-1's SDK (RESEARCH.md section 3).
#include "d50.h"
extern "C" {
#include "fm1_device.h"
#include "d50_inflate.h"
#include <string.h>
}

#include "d5_engine/d5_patch.h"
#include "d5_engine/d5_patch_map.h"

// No libc++ here: placement new by hand. The engine is constructed in d50_bind_flash, not by a static constructor, which the SDK may not run.
inline void *operator new(decltype(sizeof 0), void *where) noexcept { return where; }

namespace {

enum : unsigned { kEvNote = 1, kEvBend = 2, kEvPanic = 3, kQueue = 64 };

alignas(8) unsigned char patch_memory[sizeof(d5::Patch)];
d5::Patch *patch;
const int16_t *pcm;
volatile unsigned ready;

volatile uint32_t queue[kQueue];
volatile unsigned queue_head, queue_tail;        // head: written by the tasks, tail: by the interrupt

// The patch bank, unpacked from the flash when it is bound: the D-50's own 448 bytes a patch, which patch_from_bytes reads.
uint8_t bank[D50_BANK_PATCHES * D50_PATCH_BYTES];
unsigned bank_patches;                           // 0: no bank (d50_bind_flash then refuses the flash)

// The program is built in the control task into one of two specs; the interrupt configures the patch from the newest one.
d5::PatchSpec specs[2];
char names[2][D50_NAME_CHARS + 1];
volatile unsigned spec_serial, spec_done;        // spec_serial & 1 is the slot the newest one is in
unsigned program_now;
volatile unsigned sounding;

uint32_t u32(const uint8_t *p) { return p[0] | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24; }

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
    specs[slot] = d5::patch_from_bytes(p, pcm);
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
    build_program(program_now);
    patch->configure(specs[spec_serial & 1U], static_cast<float>(D50_RATE));
    spec_done = spec_serial;
    queue_tail = queue_head;
    ready = 1;
    return 1;
}

extern "C" unsigned d50_ready(void) { return ready; }
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
        patch->configure(specs[serial & 1U], static_cast<float>(D50_RATE));
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
    for (unsigned i = 0; i < n; ++i) {
        float l, r;
        patch->next_stereo(l, r);
        out[2 * i] = sat24(out[2 * i] + to_q23(l));
        out[2 * i + 1] = sat24(out[2 * i + 1] + to_q23(r));
    }
    sounding = static_cast<unsigned>(patch->voices_sounding());
}
