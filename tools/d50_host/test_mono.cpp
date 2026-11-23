// SPDX-License-Identifier: GPL-3.0-or-later
// Checks the D-50's MIDI modes in the glue (d50/d50.cpp d50_mode, d50_note_ch, d50_bend_ch; issue #60): in mono each part holds one
// note, a part's note ends only its own, a note two parts sound ends with the last, the pedal holds a part's note, a part past the count is
// not played, a program change keeps the mode, and leaving mono plays polyphonically again. The real glue is compiled in, on your own data.
//
//   test_mono <file.fmd5>            (tools/d50_host/test_sysex.ps1 -Test test_mono builds and runs it)
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FM1_D50_PROF 0
namespace {
uint8_t data[700000];
uint32_t data_len;
}
extern "C" {
const uint8_t *app_d50_data(uint32_t *len)
{
    if (len) *len = data_len;
    return data_len ? data : nullptr;
}
int plat_cpu2_on(void) { return 0; }
int plat_cpu2_run(void (*)(uint32_t), uint32_t) { return -1; }
int plat_cpu2_wait(void) { return 0; }
uint32_t plat_ticks24(void) { return 0; }
}
extern "C" {
#include "../../d50/d50_inflate.c"
}
#include "../../d50/d50.cpp"

namespace {
int fails = 0;
void check(bool ok, const char *what)
{
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++fails;
}
int32_t out[2 * 512];
void run(unsigned blocks = 2)                     // the events, then the lower tone's 128 samples' delay
{
    for (unsigned b = 0; b < blocks; ++b) {
        memset(out, 0, sizeof out);
        d50_render(out, 96);
    }
}
int keys() { run(); return patch->keys_down(); }
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: test_mono <file.fmd5>\n"); return 2; }
    FILE *fh = fopen(argv[1], "rb");
    if (!fh) { perror(argv[1]); return 2; }
    data_len = static_cast<uint32_t>(fread(data, 1, sizeof data, fh));
    fclose(fh);
    if (!d50_bind_flash()) { fprintf(stderr, "%s: not D-50 data\n", argv[1]); return 2; }

    // a patch whose key mode is not a Solo one, to count voices a note (Whole 1, Dual 2, ...)
    unsigned p = 0;
    for (; p < d50_programs(); ++p) {
        d50_program(p);
        run();
        if (!spec_at(spec_done & 1U).solo && !spec_at(spec_done & 1U).solo_upper && !spec_at(spec_done & 1U).solo_lower) break;
    }
    d50_note(60, 100);
    const int k = keys();
    d50_note(60, 0);
    printf("patch %u, %d key(s) a note\n", p, k);
    check(k >= 1, "a note takes a voice");

    printf("Normal: polyphonic\n");
    d50_note_ch(0, 60, 100);
    d50_note_ch(0, 64, 100);
    check(keys() == 2 * k, "two notes on part 0 both sound");
    d50_panic();
    check(keys() == 0, "panic: none");

    printf("Mono, 2 parts\n");
    d50_mode(D50_MODE_MONO, 2);
    check(keys() == 0 && mono.parts == 2U, "the mode: 2 parts, nothing sounding");
    d50_note_ch(0, 60, 100);
    d50_note_ch(0, 64, 100);
    check(keys() == k && mono.note[0] == 64, "part 0's second note ends its first");
    d50_note_ch(1, 67, 100);
    check(keys() == 2 * k, "part 1 has a note of its own");
    d50_note_ch(2, 70, 100);
    check(keys() == 2 * k, "part 2, past the count, is not played");
    d50_note_ch(0, 60, 0);
    check(keys() == 2 * k, "the note-off of part 0's earlier key does nothing");
    d50_note_ch(0, 64, 0);
    check(keys() == k && mono.note[0] < 0, "part 0's note-off ends its note");
    d50_note_ch(1, 67, 0);
    check(keys() == 0, "part 1's too");

    printf("Mono: one note on two parts\n");
    d50_note_ch(0, 62, 100);
    d50_note_ch(1, 62, 100);
    d50_note_ch(0, 62, 0);
    check(keys() > 0, "still sounding after the first part's note-off");
    d50_note_ch(1, 62, 0);
    check(keys() == 0, "ended by the last");

    printf("Mono: the bender a part\n");
    d50_bend_ch(1, 16383);
    run();
    check(mono.bend[1] == 16383U && mono.bend[0] == 8192U && ctl.bend == 8192U, "part 1's bend is its own");
    d50_bend_ch(0, 0);
    run();
    check(mono.bend[0] == 0U && ctl.bend == 0U, "part 0's is also the global one");
    d50_bend_ch(0, 8192);
    d50_bend_ch(1, 8192);

    printf("Mono: the hold pedal\n");
    d50_cc(64, 127);
    d50_note_ch(0, 60, 100);
    d50_note_ch(0, 60, 0);
    check(keys() == k, "held after the note-off");
    d50_note_ch(0, 65, 100);
    check(keys() == k && mono.note[0] == 65, "a new note on the part ends the held one");
    d50_note_ch(0, 65, 0);
    d50_cc(64, 0);
    check(keys() == 0 && mono.note[0] < 0, "the pedal up ends it");

    printf("Mono: a program change keeps the mode and the parts' notes (configure leaves the keys down)\n");
    d50_note_ch(0, 60, 100);
    run();                                        // (d50_render configures a new program before it plays the queue)
    d50_program(p);
    run();
    check(mono.parts == 2U && mono.note[0] == 60 && keys() == k, "still 2 parts, part 0's note held");
    d50_note_ch(0, 62, 100);
    check(keys() == k, "a new note on part 0 ends it");
    d50_note_ch(0, 62, 0);
    check(keys() == 0, "and its note-off ends the new one: nothing stuck");

    printf("Poly\n");
    d50_mode(D50_MODE_POLY, 0);
    check(keys() == 0 && mono.parts == 0U, "the notes off, not mono");
    d50_note_ch(0, 60, 100);
    d50_note_ch(0, 64, 100);
    check(keys() == 2 * k, "two notes sound");
    d50_mode(D50_MODE_NORMAL, 0);
    check(keys() == 0, "normal again: the notes off");

    unsigned s = 0, b = 0;                        // a Solo patch (Whole-S, Dual-S or Split-US), if a bank has one
    for (; b < d50_banks(); ++b) {
        d50_bank(b);
        for (s = 0; s < d50_programs(); ++s) {
            d50_program(s);
            run();
            if (spec_at(spec_done & 1U).solo || spec_at(spec_done & 1U).solo_upper) break;
        }
        if (s < d50_programs()) break;
    }
    if (b < d50_banks()) {
        const d5::PatchSpec &sp = spec_at(spec_done & 1U);
        const int n1 = sp.solo ? 60 : sp.split_point + 12, n2 = n1 + 4;   // (Split-US: on the Upper's side)
        printf("Solo (bank %u patch %u): one note in normal, polyphonic after a Poly\n", b + 1, s);
        d50_note(n1, 100);
        const int ks = keys();
        d50_note(n2, 100);
        check(keys() == ks, "normal: the second note replaces the first");
        d50_mode(D50_MODE_POLY, 0);
        d50_note(n1, 100);
        d50_note(n2, 100);
        check(keys() == 2 * ks, "Poly: both sound");
        d50_mode(D50_MODE_NORMAL, 0);
        d50_note(n1, 100);
        d50_note(n2, 100);
        check(keys() == ks, "normal again: solo");
        d50_panic();
    } else {
        printf("(no Solo patch in the %u bank(s): not checked)\n", d50_banks());
    }

    printf("Key Transpose: the instrument's keys only, as on the D-50 (#64)\n");
    d50_bank(0);
    d50_program(p);
    run();
    d50_mode(D50_MODE_NORMAL, 0);
    d50_transpose(5);
    d50_key(60, 100);
    run();
    check(bit_of(ctl.down, 65) && !bit_of(ctl.down, 60), "a key of 60 sounds 65");
    d50_note(60, 100);
    run();
    check(bit_of(ctl.down, 60), "MIDI's 60 sounds 60");
    d50_transpose(0);
    d50_key(60, 0);
    run();
    check(!bit_of(ctl.down, 65) && bit_of(ctl.down, 60), "the key's note-off ends its 65, after the transpose changed too");
    d50_note(60, 0);
    check(keys() == 0, "MIDI's note-off ends its 60");
    d50_transpose(5);
    d50_mode(D50_MODE_MONO, 2);
    d50_key(60, 100);
    d50_note_ch(1, 60, 100);
    run();
    check(mono.note[0] == 65 && mono.note[1] == 60, "mono: the key's part 0 moved, MIDI's part 1 not");
    d50_key(60, 0);
    d50_note_ch(1, 60, 0);
    check(keys() == 0, "both ended");
    d50_mode(D50_MODE_NORMAL, 0);
    d50_transpose(0);

    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
