// SPDX-License-Identifier: GPL-3.0-or-later
// Renders the PicoFaceD5 engine's eight fallback presets to WAV files, so the D-50 engine can be heard on the PC before it runs on the FM-1.
// Each preset plays a C major chord (2.5 s held, 1.5 s release) and then a low C (1 s held, 1.5 s release), at 32 kHz stereo.
//
//   render_presets <d5_pcm.bin> <outdir>
//
// d5_pcm.bin comes from third_party/picofaced5/tools/d5_extract/d5_make_blob.py and the user's own PCM ROMs (never committed).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "d5_engine/d5_patch.h"
#include "d5_presets.h"

namespace {

constexpr int kRate = 32000;

int16_t* load_blob(const char* path, long* words)
{
    FILE* f = std::fopen(path, "rb");
    if (!f) { std::perror(path); std::exit(1); }
    std::fseek(f, 0, SEEK_END);
    const long bytes = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    int16_t* blob = static_cast<int16_t*>(std::malloc(static_cast<size_t>(bytes)));
    if (!blob || std::fread(blob, 1, static_cast<size_t>(bytes), f) != static_cast<size_t>(bytes)) {
        std::fprintf(stderr, "short read on %s\n", path);
        std::exit(1);
    }
    std::fclose(f);
    *words = bytes / 2;
    return blob;
}

struct Wav {
    FILE* f = nullptr;
    uint32_t frames = 0;
    float peak = 0.0f;
    void open(const char* path)
    {
        f = std::fopen(path, "wb");
        if (!f) { std::perror(path); std::exit(1); }
        static const uint8_t zero[44] = {};
        std::fwrite(zero, 1, 44, f);
    }
    void put(float l, float r)
    {
        const float v[2] = {l, r};
        for (float x : v) {
            const float a = x < 0 ? -x : x;
            if (a > peak) peak = a;
            int s = static_cast<int>(x * 32767.0f);
            s = s > 32767 ? 32767 : (s < -32767 ? -32767 : s);
            const int16_t w = static_cast<int16_t>(s);
            std::fwrite(&w, 2, 1, f);
        }
        ++frames;
    }
    void close()
    {
        const uint32_t data = frames * 4;
        uint8_t h[44];
        auto u32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; ++i) h[at + i] = static_cast<uint8_t>(v >> (8 * i)); };
        auto u16 = [&](int at, uint16_t v) { h[at] = static_cast<uint8_t>(v); h[at + 1] = static_cast<uint8_t>(v >> 8); };
        std::memcpy(h, "RIFF", 4); u32(4, 36 + data); std::memcpy(h + 8, "WAVEfmt ", 8);
        u32(16, 16); u16(20, 1); u16(22, 2); u32(24, kRate); u32(28, kRate * 4); u16(32, 4); u16(34, 16);
        std::memcpy(h + 36, "data", 4); u32(40, data);
        std::fseek(f, 0, SEEK_SET);
        std::fwrite(h, 1, 44, f);
        std::fclose(f);
    }
};

void run(d5::Patch& p, Wav& a, Wav& b, float seconds)
{
    const int n = static_cast<int>(seconds * kRate);
    for (int i = 0; i < n; ++i) {
        float l, r;
        p.next_stereo(l, r);
        a.put(l, r);
        b.put(l, r);
    }
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: render_presets <d5_pcm.bin> <outdir>\n");
        return 2;
    }
    long words = 0;
    const int16_t* blob = load_blob(argv[1], &words);
    if (words != 262144)
        std::fprintf(stderr, "warning: %ld samples in the blob, 262144 expected\n", words);

    char path[512];
    Wav all;
    std::snprintf(path, sizeof path, "%s/d50_presets_all.wav", argv[2]);
    all.open(path);
    static d5::Patch p;   // big: keep it off the stack
    for (int i = 0; i < d5::kPresetCount; ++i) {
        d5::Preset pr = d5::preset(i);
        d5::preset_bind(pr.spec, blob, pr.pcm1, pr.pcm2);
        p.configure(pr.spec, static_cast<float>(kRate));

        Wav one;
        std::snprintf(path, sizeof path, "%s/d50_preset_%d.wav", argv[2], i + 1);
        one.open(path);
        const int chord[3] = {60, 64, 67};
        for (int k : chord) p.note_on(k, 100.0f / 127.0f);
        run(p, one, all, 2.5f);
        for (int k : chord) p.note_off(k);
        run(p, one, all, 1.5f);
        p.note_on(48, 100.0f / 127.0f);
        run(p, one, all, 1.0f);
        p.note_off(48);
        run(p, one, all, 1.5f);
        std::printf("%d %-14s peak %.3f\n", i + 1, pr.name, one.peak);
        one.close();
    }
    all.close();
    std::printf("all: %.1f s, peak %.3f\n", all.frames / static_cast<float>(kRate), all.peak);
    return 0;
}
