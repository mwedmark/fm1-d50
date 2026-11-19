// SPDX-License-Identifier: GPL-3.0-or-later
// The glue between the FM-1 firmware and the PicoFaceD5 engine (third_party/picofaced5, GPL-3.0-or-later, Michi71). d50.h says what each call does.
// Built for our own use: the engine's licence has no linking exception for the FM-1's SDK (RESEARCH.md section 3).
#include "d50.h"
extern "C" {
#include "fm1_device.h"
#include <string.h>
}

#include "d5_engine/d5_patch.h"
#include "d5_presets.h"

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

// The program is built in the control task into one of two specs; the interrupt configures the patch from the newest one.
d5::Preset specs[2];
volatile unsigned spec_serial, spec_done;        // spec_serial & 1 is the slot the newest one is in
unsigned program_now;
volatile unsigned sounding;

void push(uint32_t event)
{
    const unsigned next = (queue_head + 1U) % kQueue;
    if (next == queue_tail) return;              // full: drop (64 events between two blocks of 2.9 ms is a lot)
    queue[queue_head] = event;
    queue_head = next;
}

void build_program(unsigned program)
{
    const unsigned slot = (spec_serial + 1U) & 1U;
    specs[slot] = d5::preset(static_cast<int>(program));
    d5::preset_bind(specs[slot].spec, pcm, specs[slot].pcm1, specs[slot].pcm2);
    program_now = program;
    spec_serial = spec_serial + 1U;
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
    if (!header || memcmp(header, D50_FLASH_MAGIC, 4) || header[4] != 1U) return 0;
    const uint32_t bytes = static_cast<uint32_t>(header[8]) | static_cast<uint32_t>(header[9]) << 8 |
                           static_cast<uint32_t>(header[10]) << 16 | static_cast<uint32_t>(header[11]) << 24;
    if (bytes != D50_PCM_BYTES) return 0;
    const uint8_t *image = fm1_flash_map(0, D50_HEADER_BYTES + bytes);
    if (!image) return 0;
    ready = 0;
    pcm = reinterpret_cast<const int16_t *>(image + D50_HEADER_BYTES);
    patch = new (patch_memory) d5::Patch();
    build_program(program_now);
    patch->configure(specs[spec_serial & 1U].spec, static_cast<float>(D50_RATE));
    spec_done = spec_serial;
    queue_tail = queue_head;
    ready = 1;
    return 1;
}

extern "C" unsigned d50_ready(void) { return ready; }
extern "C" void d50_note(unsigned note, unsigned velocity) { push(kEvNote << 24 | (note & 127U) << 8 | (velocity & 127U)); }
extern "C" void d50_bend(unsigned value) { push(kEvBend << 24 | (value & 16383U)); }
extern "C" void d50_panic(void) { push(kEvPanic << 24); }
extern "C" unsigned d50_programs(void) { return static_cast<unsigned>(d5::kPresetCount); }
extern "C" unsigned d50_current_program(void) { return program_now; }
// Only the current program's name: building another preset just for its name would put 1.8 KB on the caller's stack.
extern "C" const char *d50_program_name(unsigned program)
{
    return ready && program == program_now ? specs[spec_serial & 1U].name : "";
}
extern "C" unsigned d50_voices_sounding(void) { return sounding; }

extern "C" void d50_program(unsigned program)
{
    if (!ready) { program_now = program % d5::kPresetCount; return; }
    build_program(program % d5::kPresetCount);
}

extern "C" void d50_render(int32_t *out, unsigned n)
{
    if (!ready) return;
    if (spec_done != spec_serial) {               // a new program: configure stops what sounds
        const unsigned serial = spec_serial;
        patch->configure(specs[serial & 1U].spec, static_cast<float>(D50_RATE));
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
