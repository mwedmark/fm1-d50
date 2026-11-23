// SPDX-License-Identifier: GPL-3.0-or-later
// Checks the Separate key modes in the glue (d50/d50.cpp d50_sep, d50_sep_channel; issue #62): in SEP the basic channel plays the Lower
// and the patch's SepCH the Upper, each polyphonically; in SEP-S SepCH plays one note at a time; SepCH has its own hold pedal and bender;
// SepCH OFF or another key mode takes nothing; a SepCH note still sounding after a program change ends with its note-off. No bank of
// fm1-d50's uses SEP, so the test makes one in the temporary area. The real glue is compiled in, on your own data.
//
//   test_sep <file.fmd5>            (tools/d50_host/test_sysex.ps1 -Test test_sep builds and runs it)
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
void run(unsigned blocks = 3)                     // the events, then the lower tone's 128 samples' delay
{
    for (unsigned b = 0; b < blocks; ++b) {
        memset(out, 0, sizeof out);
        d50_render(out, 96);
    }
}
int upper() { run(); return patch->tone_keys_down(0); }
int lower() { run(); return patch->tone_keys_down(1); }
void key_mode(unsigned mode, unsigned sep_ch)     // the current patch with this key mode and SepCH, as a new program
{
    temp_patch[6 * 64 + 18] = static_cast<uint8_t>(mode);
    temp_patch[6 * 64 + 38] = static_cast<uint8_t>(sep_ch);
    build_spec(false);
    run();
}
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: test_sep <file.fmd5>\n"); return 2; }
    FILE *fh = fopen(argv[1], "rb");
    if (!fh) { perror(argv[1]); return 2; }
    data_len = static_cast<uint32_t>(fread(data, 1, sizeof data, fh));
    fclose(fh);
    if (!d50_bind_flash()) { fprintf(stderr, "%s: not D-50 data\n", argv[1]); return 2; }
    const uint8_t sepch = 2;                      // SepCH 2: status 0x?1
    const uint8_t on = 0x91, off = 0x81, cc = 0xB1, bend = 0xE1;

    printf("SEP, SepCH %u\n", sepch);
    key_mode(3, sepch);
    check(patch->separate() && d50_sep_channel() == sepch, "a separate patch, SepCH 2");
    d50_note(60, 100);
    check(lower() == 1 && upper() == 0, "the basic channel plays the Lower only");
    d50_note(62, 100);
    check(lower() == 2, "polyphonically");
    d50_sep(on, 64, 100);
    d50_sep(on, 67, 100);
    check(upper() == 2 && lower() == 2, "SepCH plays the Upper, polyphonically in SEP");
    d50_note(60, 0);
    d50_note(62, 0);
    check(lower() == 0 && upper() == 2, "the basic channel's note-offs end only the Lower's notes");
    d50_sep(off, 64, 0);
    d50_sep(on, 67, 0);                           // (a note-on of velocity 0 is a note-off)
    check(upper() == 0, "SepCH's note-offs end the Upper's");

    printf("SEP: SepCH's own hold pedal and bender\n");
    d50_sep(cc, 64, 127);
    d50_sep(on, 64, 100);
    d50_sep(off, 64, 0);
    d50_note(60, 100);
    d50_note(60, 0);
    check(upper() == 1 && lower() == 0, "SepCH's pedal holds the Upper's note, not the Lower's");
    d50_sep(cc, 64, 0);
    check(upper() == 0, "its pedal up ends it");
    d50_cc(64, 127);
    d50_sep(on, 64, 100);
    d50_sep(off, 64, 0);
    check(upper() == 0, "the basic channel's pedal does not hold SepCH's");
    d50_cc(64, 0);
    d50_sep(bend, 0x7F, 0x7F);
    run();
    check(sep.bend == 16383U && ctl.bend == 8192U, "SepCH's bender is its own");
    d50_sep(bend, 0, 0x40);
    d50_sep(on, 64, 100);
    d50_sep(cc, 123, 0);
    check(upper() == 0, "SepCH's All Notes Off");

    printf("SEP-S\n");
    key_mode(8, sepch);
    d50_sep(on, 64, 100);
    d50_sep(on, 67, 100);
    check(upper() == 1, "SepCH plays one note at a time");
    d50_note(60, 100);
    d50_note(62, 100);
    check(lower() == 2, "the basic channel's Lower still polyphonically");
    d50_note(60, 0);
    d50_note(62, 0);
    d50_sep(off, 67, 0);
    check(upper() == 0 && lower() == 0, "all ended");

    printf("A program change with a SepCH note sounding\n");
    d50_sep(on, 64, 100);
    run();
    key_mode(0, 0);                               // (Whole: not separate)
    check(!patch->separate() && d50_sep_channel() == 0, "Whole: no SepCH");
    check(upper() == 1, "the Upper's note still sounds (configure leaves the keys down)");
    d50_sep(off, 64, 0);
    check(upper() == 0, "SepCH's note-off still ends it: nothing stuck");
    d50_sep(on, 64, 100);
    check(upper() == 0 && lower() == 0, "outside the separate modes SepCH plays nothing");

    printf("Hold Mode (#63)\n");
    static const char *const hold_name[3] = {"U", "L", "UL"};
    temp_patch[6 * 64 + 19] = 24;                 // split point C4 (60)
    for (unsigned h = 0; h < 3; ++h) {
        temp_patch[6 * 64 + 21] = static_cast<uint8_t>(h);
        key_mode(2, 0);                           // Split
        d50_cc(64, 127);
        d50_note(72, 100);                        // the Upper's side
        d50_note(48, 100);                        // the Lower's
        d50_note(72, 0);
        d50_note(48, 0);
        char what[96];
        snprintf(what, sizeof what, "Split, Hold Mode %s: the pedal holds the Upper %s, the Lower %s", hold_name[h], h != 1 ? "yes" : "no",
                 h != 0 ? "yes" : "no");
        check(upper() == (h != 1 ? 1 : 0) && lower() == (h != 0 ? 1 : 0), what);
        d50_cc(64, 0);
        check(upper() == 0 && lower() == 0, "  the pedal up ends them");

        key_mode(3, sepch);                       // SEP
        d50_cc(64, 127);
        d50_sep(cc, 64, 127);
        d50_note(60, 100);
        d50_sep(on, 64, 100);
        d50_note(60, 0);
        d50_sep(off, 64, 0);
        snprintf(what, sizeof what, "SEP, Hold Mode %s: SepCH's pedal holds the Upper %s, the basic channel's the Lower %s", hold_name[h],
                 h != 1 ? "yes" : "no", h != 0 ? "yes" : "no");
        check(upper() == (h != 1 ? 1 : 0) && lower() == (h != 0 ? 1 : 0), what);
        d50_cc(64, 0);
        d50_sep(cc, 64, 0);
        check(upper() == 0 && lower() == 0, "  the pedals up end them");
    }
    temp_patch[6 * 64 + 21] = 0;
    key_mode(1, 0);                               // Dual, Hold Mode U: both tones held anyway
    d50_cc(64, 127);
    d50_note(60, 100);
    d50_note(60, 0);
    check(upper() == 1 && lower() == 1, "Dual: the pedal holds both tones whatever Hold Mode is");
    d50_cc(64, 0);
    check(upper() == 0 && lower() == 0, "  the pedal up ends them");

    printf("SepCH OFF\n");
    key_mode(3, 0);
    check(patch->separate() && d50_sep_channel() == 0, "SEP with SepCH OFF: no channel to take");
    d50_note(60, 100);
    check(lower() == 1 && upper() == 0, "the basic channel's Lower only");
    d50_note(60, 0);

    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
