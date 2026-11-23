// SPDX-License-Identifier: GPL-3.0-or-later
// Checks the reverb's per-type pre-delay and damping (d5_effects.h, ReverbType::pre_ms / damp, #9).
//
//   test_reverb          prints a hash of the wet output of all 32 types (an impulse, then a burst of noise, 1.5 s each), to compare
//                        before and after a change of the model that must not change the sound
//   test_reverb --check  the pre-delay and damping checks (exit code 1 on a failure)
// With -DD5_REVERB_TABLE_EXPERIMENT (d5_effects.h), and -DD5_REVERB_TABLE_TOTAL_ONSET beside it, the hash is another one, and --check also
// covers the experimental values.
//
// Build (host, 32 kHz, the FM-1's segments):
//   g++ -std=c++17 -O1 -DD5_HOST_BUILD -DD5_REVERB_INT16 -DD5_REVERB_EXTERNAL_POOL -I third_party/picofaced5/include tools/d50_host/test_reverb.cpp -o test_reverb
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "d5_engine/d5_effects.h"

namespace {
constexpr float kSr = 32000.0f;
constexpr int kLen = 48000;
d5::ReverbSample seg0[12190], seg1[12206];

// the wet part only (balance 1, dry factor 0 above 0.5 is not wanted: use balance 0.5, where wet = 1 and dry = 1, and take out the dry)
struct Render { float l[kLen], r[kLen]; };
Render out;

uint32_t rng = 12345;
float noise() { rng = rng * 1664525u + 1013904223u; return static_cast<int32_t>(rng) * (1.0f / 2147483648.0f); }

void render(d5::Reverb &rv, const d5::ReverbType &, bool burst, Render &o) {
    for (int i = 0; i < kLen; ++i) {
        float send = 0.0f;
        if (!burst) send = i == 0 ? 0.5f : 0.0f;
        else if (i < 4000) send = 0.2f * noise();
        float l, r;
        rv.process(send, 0.0f, 0.0f, l, r);   // dry input 0: l and r are the wet part
        o.l[i] = l;
        o.r[i] = r;
    }
}

void make(d5::Reverb &rv, int type) {
    rv.attach(seg0, 12190, seg1, 12206);
    d5::ReverbSpec s;
    s.type = type;
    s.balance = 0.5f;
    rv.configure(s, kSr);
}

uint32_t fnv(uint32_t h, const void *p, size_t n) {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 16777619u;
    return h;
}

int first_above(const float *x, float thr) {
    for (int i = 0; i < kLen; ++i) if (std::fabs(x[i]) > thr) return i;
    return -1;
}
}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        uint32_t h = 2166136261u;
        for (int t = 0; t < 32; ++t) {
            for (int burst = 0; burst < 2; ++burst) {
                rng = 12345;
                d5::Reverb rv;
                make(rv, t);
                render(rv, d5::kReverbTypes[t], burst != 0, out);
                h = fnv(h, out.l, sizeof out.l);
                h = fnv(h, out.r, sizeof out.r);
            }
        }
        printf("hash %08x\n", h);
        return 0;
    }
    int fails = 0;
    auto check = [&](bool ok, const char *what) { printf("%s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; };

    // 1. the table: no type is set to anything the model cannot do
    bool table = true;
    for (int t = 0; t < 32; ++t) {
        const d5::ReverbType &e = d5::kReverbTypes[t];
        if (e.pre_ms < 0.0f || e.pre_ms > 50.0f || e.damp < 0.25f || e.damp > 2.0f) table = false;
    }
    check(table, "every type has pre_ms in 0..50 and damp in 0.25..2");

    // 2. pre-delay: the same type with a pre-delay is the type without it, later by pre_ms (the first wet sample moves by that much)
    for (int t : {1, 2, 9}) {   // a hall, a hall, a room (rows)
        d5::Reverb a, b;
        a.attach(seg0, 12190, seg1, 12206);
        { d5::ReverbSpec s0; s0.type = t; s0.balance = 0.5f; a.configure(s0, kSr, 0.0f); }   // the reference: no pre-delay, whatever the table says
        rng = 1; render(a, d5::kReverbTypes[t], false, out);
        static Render ref;
        ref = out;
        for (float ms : {10.0f, 40.0f}) {
            b.attach(seg0, 12190, seg1, 12206);
            d5::ReverbSpec s; s.type = t; s.balance = 0.5f;
            b.configure(s, kSr, ms);
            render(b, d5::kReverbTypes[t], false, out);
            const int n = static_cast<int>(ms * 0.001f * kSr + 0.5f);
            double err = 0.0, ener = 0.0;
            for (int i = 0; i + n < kLen; ++i) {
                const double d = out.l[i + n] - ref.l[i];
                err += d * d; ener += ref.l[i] * static_cast<double>(ref.l[i]);
            }
            char msg[96];
            snprintf(msg, sizeof msg, "type %d, %.0f ms pre-delay: the wet part is the undelayed one %d samples later", t + 1, ms, n);
            check(ener > 0 && err < 1e-4 * ener, msg);
            for (int i = 0; i < n - 1; ++i) if (out.l[i] != 0.0f) { snprintf(msg, sizeof msg, "type %d: silent before the pre-delay", t + 1); check(false, msg); break; }
        }
    }

    // 3. damping: a larger scale darkens the tail (less energy of the high band: the difference of the samples), a smaller one brightens it
    {
        double hf[3];
        const float scales[3] = {0.7f, 1.0f, 1.3f};
        for (int k = 0; k < 3; ++k) {
            d5::Reverb a;
            a.attach(seg0, 12190, seg1, 12206);
            d5::ReverbSpec s; s.type = 1; s.balance = 0.5f;
            a.configure(s, kSr, 0.0f, scales[k]);
            rng = 7; render(a, d5::kReverbTypes[1], true, out);
            double e = 0.0, tot = 0.0;
            for (int i = 8000; i < kLen; ++i) { const double d = out.l[i] - out.l[i - 1]; e += d * d; tot += out.l[i] * static_cast<double>(out.l[i]); }
            hf[k] = tot > 0 ? e / tot : 0.0;
        }
        printf("     high-band share of the tail at damp 0.7 / 1.0 / 1.3: %.4f %.4f %.4f\n", hf[0], hf[1], hf[2]);
        check(hf[0] > hf[1] && hf[1] > hf[2], "a larger damp makes the tail darker");
    }

    // 4. stability: every type decays (the tail's energy 1.0..1.5 s after the burst under the first 0.5 s)
    bool stable = true;
    for (int t = 0; t < 32; ++t) {
        rng = 99;
        d5::Reverb rv; make(rv, t);
        render(rv, d5::kReverbTypes[t], true, out);
        double early = 0.0, late = 0.0;
        for (int i = 4000; i < 20000; ++i) early += out.l[i] * static_cast<double>(out.l[i]);
        for (int i = 40000; i < kLen; ++i) late += out.l[i] * static_cast<double>(out.l[i]);
        if (!(late <= early) || !std::isfinite(late)) { printf("     type %d does not decay: %g -> %g\n", t + 1, early, late); stable = false; }
    }
    check(stable, "all 32 types decay");
    (void)first_above;
    return fails ? 1 : 0;
}
