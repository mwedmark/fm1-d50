// SPDX-License-Identifier: GPL-3.0-or-later
// Renders patches of the bank in an .fmd5 file (tools/d50_pack.js --bank) through the PicoFaceD5 engine to one WAV, the way the FM-1 plays them:
// the same unpacking (fm1_open/app/d50/d50_inflate.c) and patch_from_bytes. Each patch: a C major chord, 2.5 s held and 1.5 s of release, at 32 kHz.
//
//   render_bank <file.fmd5> <out.wav> [patch index 0..63 ...]     (no indices: all 64)
//
// Built twice by build_bank.ps1: with D5_NO_REVERB as on the FM-1 now, and with the reverb.
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
constexpr int kRate = 32000;
uint8_t file[600000];
uint8_t bank[64 * 448];

uint32_t u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }

void name_of(const uint8_t *p, char *out)
{
    static const char chars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890-";
    int n = 0;
    for (int i = 0; i < 18; ++i) { out[i] = p[384 + i] < 64 ? chars[p[384 + i]] : '?'; if (out[i] != ' ') n = i + 1; }
    out[n] = 0;
}
}  // namespace

int main(int argc, char **argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: render_bank <file.fmd5> <out.wav> [patch ...]\n"); return 2; }
    FILE *f = std::fopen(argv[1], "rb");
    if (!f) { std::perror(argv[1]); return 1; }
    const size_t n = std::fread(file, 1, sizeof file, f);
    std::fclose(f);
    if (n < 16 || std::memcmp(file, "FMD5", 4) || file[4] != 2) { std::fprintf(stderr, "%s: not an .fmd5 with a bank\n", argv[1]); return 1; }
    const int16_t *pcm = reinterpret_cast<const int16_t *>(file + 16);
    const uint32_t at = 16 + 524288, deflated = u32(file + 12), raw = u32(file + at);
    if (d50_inflate(bank, raw, file + at + 4, deflated) != static_cast<int>(raw) || raw != sizeof bank) { std::fprintf(stderr, "the bank does not unpack\n"); return 1; }

    int list[64], count = 0;
    for (int i = 3; i < argc && count < 64; ++i) list[count++] = std::atoi(argv[i]) & 63;
    if (!count) for (; count < 64; ++count) list[count] = count;

    FILE *w = std::fopen(argv[2], "wb");
    if (!w) { std::perror(argv[2]); return 1; }
    uint8_t header[44] = {};
    std::fwrite(header, 1, 44, w);
    uint32_t frames = 0;
    float peak = 0;
    static d5::Patch patch;
    static d5::PatchSpec spec;
    for (int k = 0; k < count; ++k) {
        const uint8_t *p = bank + list[k] * 448;
        spec = d5::patch_from_bytes(p, pcm);
        patch.configure(spec, static_cast<float>(kRate));
        char name[19];
        name_of(p, name);
        std::printf("%d%d %s\n", list[k] / 8 + 1, list[k] % 8 + 1, name);
        const int chord[3] = {60, 64, 67};
        for (int c : chord) patch.note_on(c, 100.0f / 127.0f);
        for (int i = 0; i < 4 * kRate; ++i) {
            if (i == 5 * kRate / 2) for (int c : chord) patch.note_off(c);
            float l, r;
            patch.next_stereo(l, r);
            const float lr[2] = {l, r};
            for (float x : lr) {
                const float a = x < 0 ? -x : x;
                if (a > peak) peak = a;
                int s = static_cast<int>(x * 32767.0f);
                s = s > 32767 ? 32767 : (s < -32767 ? -32767 : s);
                const int16_t v = static_cast<int16_t>(s);
                std::fwrite(&v, 2, 1, w);
            }
            ++frames;
        }
    }
    const uint32_t data = frames * 4;
    auto put32 = [&](int o, uint32_t v) { for (int i = 0; i < 4; ++i) header[o + i] = static_cast<uint8_t>(v >> 8 * i); };
    std::memcpy(header, "RIFF", 4); put32(4, 36 + data); std::memcpy(header + 8, "WAVEfmt ", 8); put32(16, 16);
    header[20] = 1; header[22] = 2; put32(24, kRate); put32(28, kRate * 4); header[32] = 4; header[34] = 16;
    std::memcpy(header + 36, "data", 4); put32(40, data);
    std::fseek(w, 0, SEEK_SET);
    std::fwrite(header, 1, 44, w);
    std::fclose(w);
    std::printf("%s: %.0f s, peak %.3f\n", argv[2], frames / static_cast<double>(kRate), peak);
    return 0;
}
