// SPDX-License-Identifier: GPL-3.0-or-later
// Checks the D-50's SysEx (d50/d50.cpp d50_sysex, d50_sysex_reply) as a computer would speak it: the one-way RQ1 and DT1, and the
// handshake (issue #51: WSD, RQD, DAT, ACK, EOD, ERR, RJC), and the reverb data kept and sent back (#69). The real glue is compiled in, on
// your own data file.
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

    printf("The reverb data of a whole bulk dump (#69): kept, and sent back\n");
    {
        const uint32_t reverb = 3U << 14 | 0x60U << 7, rbytes = 0x1780;   // 03 60 00 - 04 0E 7F, just after the 64 patches
        auto nib = [](uint32_t i) { return static_cast<uint8_t>((i * 7U + i / 16U) & 15U); };
        uint8_t before[448];
        memcpy(before, temp, 448);
        send(msg(kRq1, addr_size(reverb, 16), true));
        check(next().empty() && d50_sysex_error() == D50_ERR_ADDRESS, "before a dump: nothing there to read");
        send(msg(kWsd, addr_size(reverb, rbytes), true));
        check(is_short(next(), kAck), "WSD at 03 60 00: ACK");
        bool acked = true;
        for (uint32_t at = 0; at < rbytes; at += 256) {
            const uint32_t a = reverb + at, len = rbytes - at < 256 ? rbytes - at : 256;
            Bytes body;
            body.put(a >> 14 & 127U).put(a >> 7 & 127U).put(a & 127U);
            for (uint32_t i = 0; i < len; ++i) body.put(nib(at + i));
            send(msg(kDat, body, true));
            acked = acked && is_short(next(), kAck);
        }
        check(acked, "each DAT: ACK");
        send(msg(kEod, none(), false));
        check(is_short(next(), kAck), "EOD: ACK");
        check(!memcmp(temp, before, 448) && d50_sysex_error() == 0, "nothing else changed, no error on the screen");

        send(msg(kRq1, addr_size(reverb, rbytes), true));
        bool same = true;
        uint32_t got = 0;
        for (Bytes f = next(); !f.empty(); f = next()) {
            const unsigned len = f.n - 10U;
            same = same && is_data(f, kDt1, reverb + got, len);
            for (unsigned i = 0; i < len && same; ++i) same = f.b[8 + i] == nib(got + i);
            got += len;
        }
        check(same && got == rbytes, "RQ1 at 03 60 00: the same nibbles back, 0x1780 of them");

        if (d50_bank_patches() == D50_BANK_PATCHES) {
            send(msg(kRqd, addr_size(kInternal, 0x8780), true));   // the D-50's own Bulk Load asks for 02 00 00, 02 0F 00
            uint32_t sent = 0;
            bool right = true;
            for (Bytes f = next(); right && !f.empty() && f.b[4] == kDat; f = next()) {
                const unsigned len = f.n - 10U;
                right = is_data(f, kDat, kInternal + sent, len);
                const uint32_t a = kInternal + sent;
                if (a >= reverb) right = right && f.b[8] == nib(a - reverb);
                sent += len;
                send(msg(kAck, none(), false));
            }
            check(right && sent == 0x8780, "a whole-memory RQD: the bank, then the reverb data, 0x8780 bytes");
            send(msg(kAck, none(), false));       // (the EOD's)
        }

        const unsigned b = d50_current_bank();
        if (d50_banks() > 1) {
            d50_bank(b ? 0 : 1);
            d50_bank(b);
            send(msg(kRq1, addr_size(reverb, 16), true));
            check(next().empty(), "another bank chosen: the reverb data is gone with the dump");
            (void)d50_sysex_error();
        }
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

    printf("The Data Transfer menu (d50_transfer): the FM-1 starts the transfer\n");
    {
        const uint32_t whole = 0x8780U;                          // the D-50's own 02 0F 00: the bank, and the reverb data (zeros when no dump brought them)
        auto same = [](const Bytes &f, uint32_t at, unsigned len) { for (unsigned i = 0; i < len; ++i) if (f.b[8 + i] != value_at(at + i)) return false; return true; };
        check(d50_transfer(7) == 0 && d50_transfer_state() == D50_XFER_IDLE, "an unknown item: not started");

        // Bulk Dump: WSD 02 00 00 and the size, a DAT on each ACK, then EOD, then done on its ACK
        check(d50_transfer(D50_XFER_DUMP) == 1 && d50_transfer_state() == D50_XFER_RUN, "Bulk Dump starts");
        {
            Bytes w = next();
            Bytes want;
            want.put(0xF0).put(0x41).put(dev).put(0x14).put(kWsd);
            const Bytes as = addr_size(2U << 14, whole);
            for (unsigned i = 0; i < as.n; ++i) want.put(as.b[i]);
            want.put(sum7(as.b, as.n)).put(0xF7);
            check(w == want, "a WSD for 02 00 00 and the bank's size, the checksum right");
            check(next().empty(), "nothing more before the computer's ACK");
            uint32_t at = 2U << 14;
            bool bytes_ok = true;
            unsigned dats = 0;
            send(msg(kAck, none(), false));
            for (;;) {
                Bytes f = next();
                if (f.empty() || is_short(f, kEod)) { check(is_short(f, kEod), "EOD after the last DAT"); break; }
                const unsigned len = f.n - 10U;
                if (!is_data(f, kDat, at, len) || !same(f, at, len)) bytes_ok = false;
                at += len;
                ++dats;
                check(next().empty(), "no second DAT before the ACK");
                send(msg(kAck, none(), false));
            }
            check(bytes_ok && at == (2U << 14) + whole, "the DATs carry the whole bank, in order, 256 bytes at most");
            check(d50_transfer_state() == D50_XFER_RUN, "not done before the EOD's ACK");
            send(msg(kAck, none(), false));
            check(d50_transfer_state() == D50_XFER_DONE && next().empty(), "done on the EOD's ACK");
            d50_transfer_stop();
            check(d50_transfer_state() == D50_XFER_IDLE, "leaving the menu: idle");
        }

        // an ERR makes the last DAT go again; an RJC ends it
        d50_transfer(D50_XFER_DUMP);
        next();
        send(msg(kAck, none(), false));
        {
            Bytes a = next();
            send(msg(kErr, none(), false));
            check(next() == a, "ERR: the same DAT again");
            send(msg(kRjc, none(), false));
            check(d50_transfer_state() == D50_XFER_FAIL && next().empty(), "RJC from the computer: failed, nothing more");
            d50_transfer_stop();
        }

        // leaving the menu halfway gives up with RJC
        d50_transfer(D50_XFER_DUMP);
        next();
        d50_transfer_stop();
        check(is_short(next(), kRjc) && d50_transfer_state() == D50_XFER_IDLE, "stop: RJC, idle");

        // Bulk Dump.O: DT1s one-way, no waiting
        check(d50_transfer(D50_XFER_DUMP_O) == 1, "Bulk Dump.O starts");
        {
            uint32_t at = 2U << 14;
            bool ok = true;
            for (;;) {
                Bytes f = next();
                if (f.empty()) break;
                const unsigned len = f.n - 10U;
                if (!is_data(f, kDt1, at, len) || !same(f, at, len)) ok = false;
                at += len;
            }
            check(ok && at == (2U << 14) + whole, "DT1s of the whole bank, in order");
            check(d50_transfer_state() == D50_XFER_DONE, "done with the last DT1");
            d50_transfer_stop();
        }

        // Bulk Load: an RQD for 02 00 00 / 02 0F 00; the computer's WSD, DATs and EOD are taken and answered
        check(d50_transfer(D50_XFER_LOAD) == 1, "Bulk Load starts");
        {
            Bytes w = next();
            Bytes want;
            want.put(0xF0).put(0x41).put(dev).put(0x14).put(kRqd);
            const Bytes as = addr_size(2U << 14, 2U * 16384U + 15U * 128U);
            for (unsigned i = 0; i < as.n; ++i) want.put(as.b[i]);
            want.put(sum7(as.b, as.n)).put(0xF7);
            check(w == want, "an RQD for 02 00 00, size 02 0F 00");
            send(msg(kWsd, addr_size(2U << 14, 16), true));
            check(is_short(next(), kAck) && d50_transfer_state() == D50_XFER_RUN, "its WSD: ACK, still under way");
            {                                     // the 16 bytes its WSD said (as they are: nothing changes); without them the EOD is an RJC (#75)
                Bytes d;
                d.put(2).put(0).put(0);
                for (unsigned i = 0; i < 16U; ++i) d.put(d50_internal(nullptr) ? d50_internal(nullptr)[i] : 0);
                send(msg(kDat, d, true));
                check(is_short(next(), kAck), "its DAT: ACK");
            }
            send(msg(kEod, none(), false));
            check(is_short(next(), kAck) && d50_transfer_state() == D50_XFER_DONE, "its EOD: ACK, done");
            d50_transfer_stop();
        }

        // a computer's own request ends ours
        d50_transfer(D50_XFER_DUMP_O);
        send(msg(kRq1, addr_size(0, 448), true));
        check(d50_transfer_state() == D50_XFER_FAIL, "an RQ1 meanwhile: the transfer failed");
        while (!next().empty()) {}
        d50_transfer_stop();
    }

    printf("The internal memory kept (#22): its serial, its bytes, bank 0 from the app's copy\n");
    if (d50_banks() < 2U) {
        printf("  (skipped: the data has one bank)\n");
    } else {
        static uint8_t saved[D50_BANK_PATCHES * D50_PATCH_BYTES], saved_rev[D50_REVERB_BYTES];
        static unsigned give, asked;
        struct R {
            static unsigned read(uint8_t *p, uint8_t *r)
            {
                ++asked;
                if (give & D50_INT_PATCHES) memcpy(p, saved, sizeof saved);
                if (give & D50_INT_REVERB) memcpy(r, saved_rev, sizeof saved_rev);
                return give;
            }
        };
        d50_bank(0);
        const uint8_t *rev = nullptr, *mem = d50_internal(&rev);
        check(mem != nullptr, "bank 0: its bytes");
        const unsigned s0 = d50_internal_serial();
        const uint8_t factory = mem ? mem[384] : 0;
        const uint8_t other = factory == 5 ? 6 : 5;
        Bytes body;
        body.put(2).put(3).put(0).put(other);     // 02 03 00: byte 384, patch 1's block, its name's first character
        send(msg(kDt1, body, true));
        check(d50_internal_serial() == s0 + 1U && mem && mem[384] == other, "a DT1 into 02 00 00: the serial moves, the byte is there");
        d50_program(0);                           // (#86: the sounding patch is the one written: taken again from the bank)
        d50_edit_poll();
        spec_done = spec_serial;                  // (no interrupt here: the spec taken as applied)
        check(d50_patch_bytes()[384] == other, "the program change: the temporary area has it");
        body.n = 3;
        body.put(other == 7 ? 8 : 7);
        send(msg(kDt1, body, true));
        check(d50_patch_bytes()[384] == other, "a DT1 into the sounding patch's place: the temporary area not yet");
        check(d50_edit_poll() == 1 && d50_patch_bytes()[384] == body.b[3], "d50_edit_poll: the sounding patch taken again from the bank (#86)");
        spec_done = spec_serial;
        check(d50_edit_poll() == 0, "and only once");
        body.n = 3;
        body.put(other);                          // (back as it was, for the checks below)
        send(msg(kDt1, body, true));
        d50_edit_poll();
        spec_done = spec_serial;
        const unsigned s2 = d50_internal_serial();
        send(msg(kDt1, [] { Bytes b; b.put(0).put(0).put(0).put(1); return b; }(), true));
        check(d50_internal_serial() == s2, "a DT1 into the temporary area: the serial stays");
        {                                         // WRITE (#83): the temporary area into the current patch's place
            const unsigned s3 = d50_internal_serial();
            check(d50_patch_bytes()[0] == 1U && d50_current_program() == 0U, "the temporary area edited, patch I-11 current");
            check(d50_write() == 1 && d50_internal_serial() == s3 + 1U && !memcmp(mem, d50_patch_bytes(), D50_PATCH_BYTES),
                  "d50_write: the serial moves, I-11 is the temporary area");
            check(memcmp(mem + D50_PATCH_BYTES, d50_patch_bytes(), D50_PATCH_BYTES) != 0, "and only its own place");
        }

        memcpy(saved, mem, sizeof saved);         // the app's copy: bank 0 with patch 1's first character 9, and reverb data
        saved[384] = factory == 9 ? 10 : 9;
        for (unsigned i = 0; i < sizeof saved_rev; ++i) saved_rev[i] = static_cast<uint8_t>(i * 7U);
        give = D50_INT_PATCHES | D50_INT_REVERB;
        d50_internal_from(&R::read);
        d50_bank(1);
        check(d50_internal(nullptr) == nullptr, "bank 1: no internal memory");
        check(d50_write() == 0, "d50_write on bank 1 (a card): nothing");
        const unsigned s1 = d50_internal_serial();
        send(msg(kDt1, body, true));
        check(d50_internal_serial() == s1, "a DT1 into bank 1: the serial stays");
        asked = 0;
        d50_bank(0);
        mem = d50_internal(&rev);
        check(asked == 1U && mem && !memcmp(mem, saved, sizeof saved), "back to bank 0: the reader asked once, the copy's patches");
        check(rev != nullptr && !memcmp(rev, saved_rev, sizeof saved_rev), "and the copy's reverb data");
        send(msg(kRq1, addr_size(3U << 14 | 0x60U << 7, 2), true));
        {
            Bytes f = next();
            check(is_data(f, kDt1, 3U << 14 | 0x60U << 7, 2) && f.b[8] == (saved_rev[0] & 15U) && f.b[9] == (saved_rev[0] >> 4),
                  "RQ1 of 03 60 00: the copy's reverb nibbles");
            while (!next().empty()) {}
        }
        give = 0;                                 // no copy: the factory bank, no reverb data
        d50_bank(1);
        d50_bank(0);
        mem = d50_internal(&rev);
        check(mem && mem[384] == factory && rev == nullptr, "a reader with no copy: the factory bank 0, no reverb data");
        d50_internal_from(nullptr);
    }

    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
