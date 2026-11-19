# PicoFaceD5 (vendored)

The D-50 engine of [PicoVintageSynthCollection](https://github.com/Michi71/PicoVintageSynthCollection) by Michi71, GPL-3.0-or-later
(`LICENSE`), commit `26260b2162e4e366743c0faee1eacf7b3e54e7a0` (2026-11-15): `include/d5_engine/`, `include/d5_presets.h` and
`tools/d5_extract/`. `README.upstream.md` is the instrument's own README.

**Own use only.** Its licence has no linking exception for the FM-1's closed SDK (`fm1_open/LICENSE.md`), so a firmware with it must not be
given to others (RESEARCH.md section 3).

Changed here:
- `include/d5_engine/d5_effects.h`: `D5_NO_REVERB` puts a pass-through in place of the reverb, whose 84 KB pool does not fit the FM-1's SRAM yet.

No ROM data is here. `tools/d5_extract/d5_make_blob.py <romdir> <outdir>` turns your own PCM ROMs into `d5_pcm.bin` and `d5_pcm_table.h`
(put them in `roms/gen/`, which git ignores).
