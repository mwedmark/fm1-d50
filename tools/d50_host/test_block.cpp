// SPDX-License-Identifier: GPL-3.0-or-later
// Checks the D-50 engine's block rendering (Patch::render_block, the voices shared between two cores on the FM-1) against its own
// sample-by-sample rendering (next_stereo): every patch of the bank, the same notes at the same samples, blocks of varying size, and a
// pretend other core that runs its job after this core's (the voices must not depend on the order). Only the float rounding of the
// voices' sum may differ.
//
//   test_block <file.fmd5>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
#include "d50_inflate.h"
}
#include "d5_engine/d5_patch.h"
#include "d5_engine/d5_patch_map.h"

namespace {
uint8_t file[600000];
uint8_t bank[64 * 448];
d5::Patch a, b;
d5::PatchSpec spec;
#ifdef D5_REVERB_EXTERNAL_POOL
d5::ReverbSample seg[2][2][12206];
#endif

void (*pending_job)(void*) = nullptr;
void* pending_arg = nullptr;
bool fake_begin(void (*job)(void*), void* arg) { pending_job = job; pending_arg = arg; return true; }
void fake_wait() { pending_job(pending_arg); pending_job = nullptr; }

uint32_t u32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }
}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: test_block <file.fmd5>\n"); return 2; }
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::perror(argv[1]); return 2; }
    const size_t got = std::fread(file, 1, sizeof file, f);
    std::fclose(f);
    if (got < 16 || std::memcmp(file, "FMD5", 4) || file[4] != 2) { std::fprintf(stderr, "not an .fmd5 with a bank\n"); return 2; }
    const int16_t* pcm = reinterpret_cast<const int16_t*>(file + 16);
    const uint32_t at = 16 + 524288;
    if (d50_inflate(bank, u32(file + at), file + at + 4, u32(file + 12)) != static_cast<int>(sizeof bank)) return 2;
#ifdef D5_REVERB_EXTERNAL_POOL
    a.attach_reverb(seg[0][0], 12190, seg[0][1], 12206);
    b.attach_reverb(seg[1][0], 12190, seg[1][1], 12206);
#endif
    const d5::Patch::Split split{&fake_begin, &fake_wait};
    const int sizes[] = {93, 92, 93, 95, 64, 1, 128, 17, 92};
    // notes: (sample, note, velocity; 0 = off) -- a chord, a fifth voice, releases, a re-strike while the tail rings
    const struct { int at, note, vel; } events[] = {{0, 60, 100}, {5, 64, 90}, {9, 67, 110}, {4000, 72, 80}, {20000, 60, 0},
                                                    {20000, 64, 0}, {24000, 48, 120}, {30000, 67, 0}, {30000, 72, 0}, {36000, 48, 0},
                                                    {38000, 60, 70}, {46000, 60, 0}};
    const int total = 56000;
    double worst = 0, worst_rel = 0;
    int worst_patch = -1, bad = 0;
    static float bl[128], br[128];
    for (int p = 0; p < 64; ++p) {
        spec = d5::patch_from_bytes(bank + p * 448, pcm);
        a.configure(spec, 32000.0f);
        b.configure(spec, 32000.0f);
        double peak = 0, diff = 0;
        int s = 0, e = 0, k = 0;
        while (s < total) {
            while (e < static_cast<int>(sizeof events / sizeof events[0]) && events[e].at <= s) {
                d5::Patch* const both[2] = {&a, &b};
                for (d5::Patch* x : both) {
                    if (events[e].vel) x->note_on(events[e].note, events[e].vel / 127.0f);
                    else x->note_off(events[e].note);
                }
                ++e;
            }
            int n = sizes[k++ % (sizeof sizes / sizeof sizes[0])];
            if (e < static_cast<int>(sizeof events / sizeof events[0]) && s + n > events[e].at) n = events[e].at - s;
            if (s + n > total) n = total - s;
            b.render_block(bl, br, n, &split);
            for (int t = 0; t < n; ++t) {
                float l, r;
                a.next_stereo(l, r);
#ifdef D5_FX_LAG
                {   // the block rendering runs the effects D5_FX_LAG samples later: compare with the sample-by-sample output that much earlier
                    static float hl[D5_FX_LAG], hr[D5_FX_LAG];
                    static int hp = 0;
                    if (s == 0 && t == 0) for (int i = 0; i < D5_FX_LAG; ++i) hl[i] = hr[i] = 0.0f;
                    const float ol = hl[hp], orr = hr[hp];
                    hl[hp] = l; hr[hp] = r; hp = (hp + 1) % D5_FX_LAG;
                    l = ol; r = orr;
                }
#endif
                const double dl = l - bl[t], dr = r - br[t];
                const double d = dl < 0 ? -dl : dl, d2 = dr < 0 ? -dr : dr;
                if (d > diff) diff = d;
                if (d2 > diff) diff = d2;
                const double pl = l < 0 ? -l : l;
                if (pl > peak) peak = pl;
            }
            s += n;
        }
        const double rel = peak > 0 ? diff / peak : diff;
        if (diff > worst) { worst = diff; worst_patch = p; }
        if (rel > worst_rel) worst_rel = rel;
        if (diff > 1e-4) { ++bad; std::printf("patch %d%d: differs by %.3g (peak %.3g)\n", p / 8 + 1, p % 8 + 1, diff, peak); }
    }
    std::printf("64 patches: largest difference %.3g (patch %d%d), %.3g of the peak\n", worst, worst_patch / 8 + 1, worst_patch % 8 + 1, worst_rel);
    std::printf(bad ? "FAIL\n" : "pass\n");
    return bad != 0;
}
