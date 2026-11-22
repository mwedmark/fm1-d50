// SPDX-License-Identifier: GPL-3.0-or-later
// Checks the D-50's SysEx (d50/d50.cpp d50_sysex, d50_sysex_reply) as a computer would speak it: the one-way RQ1 and DT1, and the
// handshake (issue #51: WSD, RQD, DAT, ACK, EOD, ERR, RJC). The real glue is compiled in, on your own data file.
//
//   test_sysex <file.fmd5>            (tools/d50_host/test_sysex.ps1 builds and runs it)
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

// (no libc++: the glue defines placement new itself, as on the FM-1; d50/cxx's headers)
namespace {
int fails = 0;
uint8_t dev = 0x00;                               // the device ID the computer speaks to (d50_device: the D-50's basic channel less one)
void check(bool ok, const char *what)
{
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++fails;
}
uint8_t sum7(const uint8_t *p, unsigned n)
{
    unsigned s = 0;
    for (unsigned i = 0; i < n; ++i) s += p[i];
    return static_cast<uint8_t>((128U - (s & 127U)) & 127U);
}
struct Bytes {
    uint8_t b[300];
    unsigned n = 0;
    Bytes &put(uint8_t v) { b[n++] = v; return *this; }
    uint8_t &back() { return b[n - 1]; }
    bool operator==(const Bytes &o) const { return n == o.n && !memcmp(b, o.b, n); }
    bool empty() const { return !n; }
};
Bytes addr_size(uint32_t a, uint32_t s)
{
    Bytes r;
    r.put(a >> 14 & 127U).put(a >> 7 & 127U).put(a & 127U).put(s >> 14 & 127U).put(s >> 7 & 127U).put(s & 127U);
    return r;
}
// a frame as d50_sysex takes it: 41 dev 14 cmd body [sum] (no F0, F7)
Bytes msg(uint8_t cmd, const Bytes &body, bool with_sum)
{
    Bytes m;
    m.put(0x41).put(dev).put(0x14).put(cmd);
    for (unsigned i = 0; i < body.n; ++i) m.put(body.b[i]);
    if (with_sum) m.put(sum7(body.b, body.n));
    return m;
}
Bytes none() { return Bytes(); }
void send(const Bytes &m) { d50_sysex(m.b, m.n); }
// the next frame the D-50 sends, F0 .. F7; empty when none
Bytes next()
{
    Bytes f;
    f.n = d50_sysex_reply(f.b);
    return f;
}
bool is_short(const Bytes &f, uint8_t cmd)
{
    return f.n == 6 && f.b[0] == 0xF0 && f.b[1] == 0x41 && f.b[2] == dev && f.b[3] == 0x14 && f.b[4] == cmd && f.b[5] == 0xF7;
}
// a DT1 or DAT: its address, its length and its checksum right
bool is_data(const Bytes &f, uint8_t cmd, uint32_t at, unsigned len)
{
    if (f.n != 10U + len || f.b[0] != 0xF0 || f.b[1] != 0x41 || f.b[2] != dev || f.b[3] != 0x14 || f.b[4] != cmd || f.b[f.n - 1] != 0xF7)
        return false;
    if ((static_cast<uint32_t>(f.b[5]) << 14 | f.b[6] << 7 | f.b[7]) != at) return false;
    return sum7(&f.b[5], 3U + len) == f.b[8 + len];
}
}  // namespace

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: test_sysex <file.fmd5>\n"); return 2; }
    FILE *fh = fopen(argv[1], "rb");
    if (!fh) { perror(argv[1]); return 2; }
    data_len = static_cast<uint32_t>(fread(data, 1, sizeof data, fh));
    fclose(fh);
    if (!d50_bind_flash()) { fprintf(stderr, "%s: not D-50 data\n", argv[1]); return 2; }
    const uint8_t *temp = d50_patch_bytes();
    const unsigned bank_bytes = d50_bank_patches() * D50_PATCH_BYTES;

    printf("RQ1, one-way: the temporary area as DT1s without waiting\n");
    send(msg(kRq1, addr_size(0, 448), true));
    {
        Bytes a = next(), b = next(), c = next();
        check(is_data(a, kDt1, 0, 256) && is_data(b, kDt1, 256, 192) && c.empty(), "DT1 256 + 192, then nothing");
        check(!memcmp(&a.b[8], temp, 256) && !memcmp(&b.b[8], temp + 256, 192), "the bytes are the temporary area's");
    }

    printf("RQD: the temporary area, a DAT at a time after each ACK, then EOD\n");
    send(msg(kRqd, addr_size(0, 448), true));
    {
        Bytes a = next();
        check(is_data(a, kDat, 0, 256), "DAT 256 at 00 00 00");
        check(next().empty(), "nothing more before the ACK");
        send(msg(kAck, none(), false));
        Bytes b = next();
        check(is_data(b, kDat, 256, 192), "DAT 192 at 00 02 00 after the ACK");
        check(!memcmp(&a.b[8], temp, 256) && !memcmp(&b.b[8], temp + 256, 192), "the bytes are the temporary area's");
        send(msg(kErr, none(), false));
        check(next() == b, "ERR: the same DAT again");
        send(msg(kAck, none(), false));
        check(is_short(next(), kEod), "EOD after the last ACK");
        check(next().empty(), "nothing more before the EOD's ACK");
        send(msg(kAck, none(), false));
        check(next().empty(), "the EOD's ACK: done");
    }

    printf("RQD: the whole bank in RAM\n");
    send(msg(kRqd, addr_size(kInternal, bank_bytes), true));
    {
        unsigned dats = 0, bytes = 0;
        bool right = true;
        for (Bytes f; !(f = next()).empty();) {
            if (is_short(f, kEod)) { send(msg(kAck, none(), false)); break; }
            const unsigned len = f.n - 10U;
            right = right && is_data(f, kDat, kInternal + bytes, len) && !memcmp(&f.b[8], bank + bytes, len);
            ++dats;
            bytes += len;
            send(msg(kAck, none(), false));
        }
        check(right && bytes == bank_bytes && dats == (bank_bytes + 255U) / 256U, "every DAT in order, the bank's bytes, then EOD");
        check(next().empty(), "done");
    }

    printf("RQD: stopped by RJC\n");
    send(msg(kRqd, addr_size(kInternal, bank_bytes), true));
    check(is_data(next(), kDat, kInternal, 256), "the first DAT");
    send(msg(kRjc, none(), false));
    send(msg(kAck, none(), false));
    check(next().empty(), "nothing after RJC (an ACK after it does nothing)");

    printf("RQD: refused\n");
    send(msg(kRqd, addr_size(1U << 14, 16), true));
    check(is_short(next(), kRjc), "nothing at 01 00 00: RJC");
    check(d50_sysex_error() == D50_ERR_ADDRESS, "the screen says Invalid Data Address");
    {
        Bytes m = msg(kRqd, addr_size(0, 448), true);
        m.back() ^= 1U;
        send(m);
        check(is_short(next(), kErr), "a wrong checksum: ERR");
        check(d50_sysex_error() == D50_ERR_SUM, "the screen says Sum Check Error");
    }

    printf("WSD, DAT, EOD: the patch's name sent to the temporary area\n");
    {
        const uint32_t name = 384;                // the patch block (6 x 64) starts with its 18-character name
        uint8_t before[18];
        memcpy(before, temp + name, 18);
        send(msg(kWsd, addr_size(name, 18), true));
        check(is_short(next(), kAck), "WSD: ACK");
        Bytes body;
        body.put(name >> 14 & 127U).put(name >> 7 & 127U).put(name & 127U);
        for (unsigned i = 0; i < 18; ++i) body.put(static_cast<uint8_t>(i + 1));
        Bytes bad = msg(kDat, body, true);
        bad.back() ^= 1U;
        send(bad);
        check(is_short(next(), kErr), "a DAT with a wrong checksum: ERR");
        check(!memcmp(temp + name, before, 18), "and nothing written");
        (void)d50_sysex_error();
        send(msg(kDat, body, true));
        check(is_short(next(), kAck), "the DAT again: ACK");
        bool same = true;
        for (unsigned i = 0; i < 18; ++i) same = same && temp[name + i] == i + 1;
        check(same, "the bytes are in the temporary area");
        send(msg(kEod, none(), false));
        check(is_short(next(), kAck), "EOD: ACK");
        check(next().empty(), "done");
    }

    printf("DT1: a byte above its parameter's maximum skipped, as the D-50 does (#58)\n");
    {
        const uint32_t at = 6U * 64U + 32U;       // the patch block's bytes 32 and 33 (maxima 100: the firmware's 0x7DA0)
        const uint8_t was = temp[at], to = temp[at + 1] == 40U ? 41U : 40U;
        Bytes body;
        body.put(at >> 14 & 127U).put(at >> 7 & 127U).put(at & 127U).put(101).put(to);
        send(msg(kDt1, body, true));
        check(temp[at] == was, "101 to a byte of 0..100: not written");
        check(temp[at + 1] == to, "the next byte of the same DT1: written");
        check(d50_sysex_error() == 0, "and no error on the screen");
        Bytes zero;
        const uint32_t past = 54U;                // partial 1's byte 54: past its parameters, only 0
        const uint8_t old = temp[past];
        zero.put(0).put(0).put(past).put(1);
        send(msg(kDt1, zero, true));
        check(temp[past] == old, "1 to a byte past the parameters: not written");
    }

    printf("WSD, DAT, EOD: the reverb data of a whole bulk dump, taken and ignored\n");
    {
        const uint32_t reverb = 3U << 14 | 0x60U << 7;   // 03 60 00, just after the bank (64 x 448 bytes from 02 00 00)
        uint8_t before[448];
        memcpy(before, temp, 448);
        send(msg(kWsd, addr_size(reverb, 16), true));
        check(is_short(next(), kAck), "WSD at 03 60 00: ACK");
        Bytes body;
        body.put(reverb >> 14 & 127U).put(reverb >> 7 & 127U).put(reverb & 127U);
        for (unsigned i = 0; i < 16; ++i) body.put(0x55);
        send(msg(kDat, body, true));
        check(is_short(next(), kAck), "its DAT: ACK");
        check(!memcmp(temp, before, 448), "nothing else changed");
        send(msg(kEod, none(), false));
        check(is_short(next(), kAck), "EOD: ACK");
        check(d50_sysex_error() == 0, "no error on the screen");
    }

    printf("RQ1 after an RQD left halfway: the one-way answer, not the handshake's\n");
    send(msg(kRqd, addr_size(0, 448), true));
    check(is_data(next(), kDat, 0, 256), "the RQD's first DAT");
    send(msg(kRq1, addr_size(0, 448), true));
    {
        Bytes a = next(), b = next(), c = next();
        check(is_data(a, kDt1, 0, 256) && is_data(b, kDt1, 256, 192) && c.empty(), "DT1 256 + 192, then nothing");
    }

    printf("The device ID (issue #51): the basic channel less one\n");
    {
        d50_device(3);                            // basic channel 4
        dev = 0x00;
        send(msg(kRq1, addr_size(0, 448), true));
        check(next().empty(), "an RQ1 to device 0 when the FM-1 is 3: not answered");
        send(msg(kRqd, addr_size(0, 448), true));
        check(next().empty(), "an RQD to device 0: not answered");
        uint8_t before[448];
        memcpy(before, temp, 448);
        Bytes body;
        body.put(0).put(0).put(0).put(0x7F);
        send(msg(kDt1, body, true));
        check(!memcmp(temp, before, 448), "a DT1 to device 0: not written");
        check(d50_sysex_error() == 0, "and no error on the screen");
        dev = 0x03;
        send(msg(kRq1, addr_size(0, 448), true));
        Bytes a = next(), b = next();
        check(is_data(a, kDt1, 0, 256) && is_data(b, kDt1, 256, 192), "to device 3: answered, with ID 3");
        send(msg(kWsd, addr_size(0, 16), true));
        check(is_short(next(), kAck), "a WSD to device 3: ACK with ID 3");
        send(msg(kEod, none(), false));
        check(is_short(next(), kAck), "its EOD: ACK");
        d50_device(0);
        dev = 0x00;
    }

    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
