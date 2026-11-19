// SPDX-License-Identifier: GPL-3.0-or-later
// Benchmark and regression fingerprint of the D-50 engine: every patch of the bank plays a full chord (all 16 voices of each tone)
// for a few seconds through Patch::render_block, as the FM-1 renders it. Prints the time per patch and a fingerprint of the output
// (a checksum of the samples quantised to 16 bits, and the sum of squares) so an optimisation can be checked against the last one.
//
//   bench <file.fmd5> [patches]      patches: how many of the 64 (default 64; they are spread evenly)
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "d50_inflate.h"
}
#include "d5_engine/d5_patch.h"
#include "d5_engine/d5_patch_map.h"

namespace {
uint8_t file[600000];
uint8_t bank[64 * 448];
d5::Patch patch;
d5::PatchSpec spec;
#ifdef D5_REVERB_EXTERNAL_POOL
d5::ReverbSample seg[2][12206];
#endif
uint32_t u32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }
}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) return 2;
    std::fread(file, 1, sizeof file, f);
    std::fclose(f);
    const int16_t* pcm = reinterpret_cast<const int16_t*>(file + 16);
    const uint32_t at = 16 + 524288;
    if (d50_inflate(bank, u32(file + at), file + at + 4, u32(file + 12)) != static_cast<int>(sizeof bank)) return 2;
    const int count = argc > 2 ? std::atoi(argv[2]) : 64;
#ifdef D5_REVERB_EXTERNAL_POOL
    patch.attach_reverb(seg[0], 12190, seg[1], 12206);
#endif
    static float l[128], r[128];
    const int blocks = 32000 * 4 / 93;     // four seconds in blocks of 93, as the FM-1's 128-frame blocks at 44.1 kHz
    uint32_t sum = 2166136261u;
    double energy = 0, total_ms = 0, worst_ms = 0;
    int worst = 0;
    for (int k = 0; k < count; ++k) {
        const int p = count >= 64 ? k : k * 64 / count;
        spec = d5::patch_from_bytes(bank + p * 448, pcm);
        patch.configure(spec, 32000.0f);
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) {
            if (b == 0) for (int n = 0; n < 8; ++n) patch.note_on(36 + n * 5, 0.8f);      // eight keys: both tones, sixteen voices
            if (b == blocks / 2) for (int n = 0; n < 8; ++n) patch.note_off(36 + n * 5);   // release in the second half
            patch.render_block(l, r, 93, nullptr);
            for (int i = 0; i < 93; ++i) {
                const int a = static_cast<int>(l[i] * 32767.0f), c = static_cast<int>(r[i] * 32767.0f);
                sum = (sum ^ static_cast<uint32_t>(a & 0xFFFF)) * 16777619u;
                sum = (sum ^ static_cast<uint32_t>(c & 0xFFFF)) * 16777619u;
                energy += l[i] * l[i] + r[i] * r[i];
            }
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        total_ms += ms;
        if (ms > worst_ms) { worst_ms = ms; worst = p; }
    }
    std::printf("patches %d: %.1f ms total, %.1f ms a patch (4 s of audio), worst %.1f ms (patch %d%d)\n", count, total_ms, total_ms / count,
                worst_ms, worst / 8 + 1, worst % 8 + 1);
    std::printf("fingerprint %08x energy %.6g\n", sum, energy);
    return 0;
}
