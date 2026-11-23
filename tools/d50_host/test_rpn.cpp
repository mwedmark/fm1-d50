// SPDX-License-Identifier: GPL-3.0-or-later
// Checks the RPNs in the glue as the D-50 takes them (d50/d50.cpp control; issue #66, the firmware's 0x4D58..0x4DC8): RPN 0 the bender
// range, RPN 1 Fine Tuning as Master Tune once both data entry bytes have come, an NRPN shutting out the data entries until the next RPN,
// and d50_rpn_reset. The real glue is compiled in, on your own data.
//
//   test_rpn <file.fmd5>            (tools/d50_host/test_sysex.ps1 -Test test_rpn builds and runs it)
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
void run()
{
    memset(out, 0, sizeof out);
    d50_render(out, 96);
}
void cc(unsigned c, unsigned v) { d50_cc(c, v); }
int fine(unsigned msb, unsigned lsb)              // RPN 1 with both bytes: the Master Tune it gives
{
    cc(101, 0);
    cc(100, 1);
    cc(6, msb);
    cc(38, lsb);
    run();
    return ctl.tune;
}
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: test_rpn <file.fmd5>\n"); return 2; }
    FILE *fh = fopen(argv[1], "rb");
    if (!fh) { perror(argv[1]); return 2; }
    data_len = static_cast<uint32_t>(fread(data, 1, sizeof data, fh));
    fclose(fh);
    if (!d50_bind_flash()) { fprintf(stderr, "%s: not D-50 data\n", argv[1]); return 2; }

    printf("RPN 1, Fine Tuning, as Master Tune\n");
    check(ctl.tune == 50, "440.0 Hz at start (50)");
    d50_tune(30);
    run();
    cc(101, 0);
    cc(100, 1);
    cc(6, 64);
    run();
    check(ctl.tune == 30, "the MSB alone does nothing yet");
    const unsigned serial = d50_midi_tune() >> 8;
    cc(38, 0);
    run();
    check(ctl.tune == 50 && (d50_midi_tune() & 255U) == 50U, "with the LSB: the centre, 0x2000, is 50");
    check(d50_midi_tune() >> 8 == serial + 1U, "and the panel's count moved");
    check(fine(32, 0) == 0, "0x1000: 0 (427.5 Hz)");
    check(fine(0, 0) == 0, "below it: still 0");
    check(fine(95, 64) == 100, "0x2FC0: 100 (452.6 Hz)");
    check(fine(127, 127) == 100, "the top: still 100");
    check(fine(48, 0) == 25, "0x1800: 25");
    cc(6, 64);
    run();
    check(ctl.tune == 50, "a later MSB alone applies with the last LSB, as on the D-50");

    printf("The NRPN flag\n");
    cc(101, 0);
    cc(100, 0);
    cc(6, 7);
    run();
    check(bend_semis(16383) > 6.9f && bend_semis(16383) < 7.1f, "RPN 0: the bender range 7");
    cc(99, 1);
    cc(98, 2);
    cc(6, 3);
    cc(38, 5);
    run();
    check(bend_semis(16383) > 6.9f && bend_semis(16383) < 7.1f, "after an NRPN a data entry is not taken");
    cc(101, 0);
    cc(100, 0);
    cc(6, 3);
    run();
    check(bend_semis(16383) > 2.9f && bend_semis(16383) < 3.1f, "the next RPN takes them again");
    cc(6, 20);
    run();
    check(bend_semis(16383) > 11.9f && bend_semis(16383) < 12.1f, "the bender range clamped to 12");

    printf("d50_rpn_reset\n");
    fine(64, 0);
    d50_rpn_reset();
    cc(6, 20);
    run();
    check(ctl.tune == 50 && bend_semis(16383) > 11.9f && bend_semis(16383) < 12.1f, "the RPN null: a data entry does nothing");
    cc(101, 0);
    cc(100, 1);
    cc(6, 48);
    run();
    check(ctl.tune == 50, "Fine Tuning's bytes not received: the MSB alone waits for the LSB again");
    cc(38, 0);
    run();
    check(ctl.tune == 25, "then both: 25");
    d50_tune(50);
    run();

    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
