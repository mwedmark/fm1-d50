// SPDX-License-Identifier: GPL-3.0-or-later
// Checks the polyphony cap in the glue (d50/d50.cpp d50_voices; issue #8): the cap reaches the sounding patch, fewer voices sound under
// it, it is kept over a program change, and the CPU guard's coming back up stops at it. The real glue is compiled in, on your own data.
//
//   test_voices <file.fmd5>            (tools/d50_host/test_sysex.ps1 -Test test_voices builds and runs it)
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
void run(unsigned blocks = 1)
{
    for (unsigned i = 0; i < blocks; ++i) {
        memset(out, 0, sizeof out);
        d50_render(out, 96);
    }
}
unsigned chord(unsigned notes)                    // the voices sounding with notes keys held, then all released and let die
{
    for (unsigned n = 0; n < notes; ++n) d50_note(48 + n, 100);
    run(4);
    const unsigned v = d50_voices_sounding();
    for (unsigned n = 0; n < notes; ++n) d50_note(48 + n, 0);
    d50_panic();
    run(400);
    return v;
}
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: test_voices <file.fmd5>\n"); return 2; }
    FILE *fh = fopen(argv[1], "rb");
    if (!fh) { perror(argv[1]); return 2; }
    data_len = static_cast<uint32_t>(fread(data, 1, sizeof data, fh));
    fclose(fh);
    if (!d50_bind_flash()) { fprintf(stderr, "%s: not D-50 data\n", argv[1]); return 2; }
    run(4);

    printf("The polyphony cap (d50_voices)\n");
    check(d50_voice_limit() == 16U && patch->voice_cap() == 16, "16 at start");
    const unsigned all = chord(12);
    d50_voices(6);
    run();
    check(d50_voice_limit() == 6U && patch->voice_cap() == 6, "6: the guard's limit and the patch's cap");
    const unsigned capped = chord(12);
    printf("    twelve keys: %u voices uncapped, %u with the cap at 6\n", all, capped);
    check(capped < all && capped <= 12U, "fewer voices sound, at most 6 a tone");

    d50_program(5);
    run(20);
    check(patch->voice_cap() == 6 && d50_voice_limit() == 6U, "kept over a program change");
    for (unsigned i = 0; i < 400; ++i) d50_guard(10);
    check(d50_voice_limit() == 6U, "the guard, calm, does not go above it");
    for (unsigned i = 0; i < 40; ++i) d50_guard(100);
    check(d50_voice_limit() <= 6U, "and caps under it when loaded");

    d50_voices(0);
    run();
    check(patch->voice_cap() == 1, "0 is taken as 1");
    d50_voices(40);
    run();
    check(patch->voice_cap() == 16 && d50_voice_limit() == 16U, "over 16 is 16, and the guard follows");
    check(chord(12) == all, "uncapped again: as many voices as at first");

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
