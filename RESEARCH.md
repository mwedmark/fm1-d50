# Milestone 1: research (2026-11-19)

What is known about the D-50, what exists that emulates it, and what that means for fm1-d50. Every fact has its source. What a source only
claims, and nobody here has checked, is said so. Nothing was built or run for this milestone.

## 1. The D-50's hardware

| | Fact | Source |
|---|---|---|
| Control CPU | **NEC uPD78312** (78K/III), 12 MHz crystal. Not an 8031, as D50.md said before. | MAME `roland_d50.cpp` |
| Its internal ROM | 8 KB mask ROM in the CPU (IC25), e.g. `d78312g-022`. The 1.xx firmware has another one (`-017`), which is not dumped. | MAME |
| Program ROM | 64 KB EPROM (IC22). Versions 1.04, 1.06, 1.10, 2.10, 2.20, 2.21, 2.22. D-550: 1.00-1.02. | MAME |
| LA chip | Old boards **MB87136A** (R15229851, PGA), new boards R15229896 "LA32" (QFP). **The same two part numbers as in the MT-32.** | MAME `roland_d50.cpp` and `roland_mt32.cpp` |
| LA clock | 32.768 MHz (a commented-out line in MAME). 32.768 MHz / 1024 = 32 000 Hz, the MT-32's rate. Not measured on a D-50. | MAME; PicoVintage README says 32 kHz is "the MT-32's figure" |
| Wave ROM | **512 KB** (0x80000). New boards: one TC534000 at IC30. Old boards: two 256 KB TC532000 (IC30 A, IC29 B). | MAME, PicoVintage |
| PCM waves | 100. Waves 1-76 are described by a table in the EPROM (start page and length class), 77-100 are combination waves computed from two tables. So **the wave table needs the EPROM too**, not only the wave ROM. | PicoVintage PROJEKTSTAND.md, not checked here |
| Chorus | A chorus ASIC, MB87137 (IC8 on the old board). | MAME (a comment) |
| Reverb | A reverb chip with 32 types of 188 coefficients each **in the silicon**, not in a ROM. MAME lists no reverb ROM. | PicoVintage README, not checked here |
| Tone RAM | 32 KB SRAM with a battery (HM62256). | MAME |
| Polyphony | 16 voices with one tone, 8 + 8 with both. Each tone has 2 partials, so 32 partials. | PicoVintage |

## 2. What exists

**MAME** (`src/mame/roland/roland_d50.cpp`): a skeleton. `MACHINE_NOT_WORKING | MACHINE_NO_SOUND`, the LA32 commented out, no MIDI UART, no LCD,
no reverb. It has the ROM sets, the CPU and the panel matrix. Its uPD78312 core and debugger are what PicoVintage used to disassemble the firmware.
MAME has no LA32 device at all (the MT-32 driver has no sound either).

**Nuked-MT32** (nukeykt, `la32.cpp`: GPL-2.0-or-later, checked). A low-level MT-32. It runs the original control ROM on an emulated MCS-96, an LA32
"based on the decap of John McMaster" and a reverb chip from a decap. Only the second-generation MT-32. Cost and output rate are not stated. Since
the D-50's LA chip has the same part numbers, **this LA32 core is the closest thing to the D-50's own chip that exists**. That it is the same die is a
guess from the part numbers, not proven.

**gearmulator's 88emu**: has an LA32 (MB87136A) "derived from NukeYKT's core, improved and hardware-verified by TUS" (GPL-2.0-or-later). No D-50.

**Munt** (mt32emu, LGPL-2.1-or-later, checked in `LA32WaveGenerator.h`): the long-standing reverse engineering of the MT-32. The LA32 works in log
space. The square wave is cosine segments with linear parts between them, resonance is an added decaying sine, the saw is the square times a
cosine, the PCM is interpolated in log space, and pairs are added or ring modulated in linear space. Integer and float versions. TVA, TVF and TVP are
the MT-32 firmware's envelopes, not the D-50's.

**PicoVintageSynthCollection / PicoFaceD5** (Michi71, GPL-3.0-or-later, checked in `d5_pcm_voice.h`). **A native D-50 engine for the RP2350,
written from a disassembly of the D-50's firmware** (the EPROM and the uPD78312's internal ROM, with MAME 0.289). According to its README and
PROJEKTSTAND.md, not checked here:
- Byte exact from the firmware: the envelope time law, key follow, the LFO rates, the pulse-width scaler, the TVF bias, the TVA bias, the pitch
  envelope's times and levels, the portamento slew, the pitch law. The wave geometry comes from the EPROM's tables.
- The 7 structures with the ring modulator, 2 partials a tone, 3 LFOs a tone, the pitch envelope.
- Not original: the EQ, the chorus (8 types, the type table not decoded) and the reverb (32 types on a Boss RRV-10 / Munt topology). Whole and Dual
  only. No voice stealing (a new note is dropped when the pool is full). The envelope clock is calibrated to a few percent.
- SysEx: Roland DT1/RQ1 (`F0 41 dev 14 ...`), temporary area at 00 00 00, the internal memory at 02 00 00, 448 bytes a patch, 64 patches a bank.
- Float on the RP2350, 32 kHz. On its heaviest patches 11-12 voices fit the audio block.
- The ROMs are not in the repository: the user's own dumps are converted when the firmware is built ("local build only").
- Nobody in that project has a real D-50. The references are plugin recordings, which the project itself does not trust.

## 3. What it means for fm1-d50

### The ROMs

A faithful D-50 needs three of the user's own dumps: **the wave ROM** (512 KB), **the EPROM** (64 KB, for the wave table and the firmware's
tables) and, for path A, **the CPU's internal ROM** (8 KB). None of them may be in this repository or in a published firmware, nor the factory patches.
So the install page reads the user's files, takes what the firmware needs out of them and uploads it to the flash, as a bank is uploaded today.
The firmware itself has no Roland data, and it says which ROM is missing when one is.

### The flash

The wave ROM is 524 288 bytes and the user region is 536 576: **it fits, with 12 288 bytes left over**. The firmware's tables (a few KB) fit there too,
a bank of 64 patches (28 672 bytes) does not. The patches need somewhere else: in the image (room is freed when the SF2 code goes), or in one
of the layout's other regions (`sdk/Package.cs`: 4 096, 32 768 and 16 384 bytes). To be decided in milestone 5.

### The CPU

The pi32v2 has a single-precision FPU (`fm1_open/third_party/mp3/README.md`) and two cores at 240 MHz. The RP2350's two Cortex-M33 at 150 MHz fit
11-12 heavy D-50 voices in PicoFaceD5. That suggests 16 voices are within reach on the FM-1, but it is a guess until milestone 2 measures a partial.
Floats in the audio interrupt: whether the FPU registers are saved there has not been checked (HANDOVER_LIVE_CACHE.md).

### Licences: what may be linked with the FM-1's SDK

The firmware links `libfm1_engine.a`, which is not free. fm1_open's linking exception covers "code marked with this exception" and "does not
grant rights in third-party code beyond its applicable license" (`fm1_open/LICENSE.md`). So:

| Code | Licence | In the device firmware we publish? |
|---|---|---|
| Our own | ours, GPLv3 with the same exception | yes |
| Munt | LGPL-2.1+ | yes (the LGPL allows linking with closed code, if it can be relinked) |
| MAME's CPU cores | mostly BSD-3-Clause (check the file) | yes |
| Nuked-MT32's LA32 | GPL-2.0+ | **no**, unless nukeykt grants the exception |
| PicoFaceD5 | GPL-3.0+ | **no**, unless Michi71 grants the exception (see the fm1-base note below) |

All of them may be used freely on a PC (a reference tool that is not linked with the SDK), and for a firmware nobody else gets. Facts, table
addresses and descriptions in their documentation may be used for our own code (the way PicoFaceD5 itself was written from Munt's description).
Not legal advice: when it matters, ask the authors.

**fm1-base (2026-11-21, issue #38).** The table above is about fm1_open, whose firmware links M-VAVE's closed `libfm1_engine.a`. The D-50
now runs on fm1-base (FM1-BASE.md), which links no closed library while Bluetooth is off (fm1.toml `ble = false`): the firmware there is
GPL-3.0 throughout, our code, PicoFaceD5 and fm1-base's MPL-2.0 platform together, and may be published under the GPL. PicoFaceD5's
author was asked and agreed: "Yes sure, do it. The code is GPL-3.0" ([Michi71/PicoVintageSynthCollection#194](https://github.com/Michi71/PicoVintageSynthCollection/issues/194)).
Turning on JieLi's closed Bluetooth libraries (`ble = true`) would bring the question back. Roland's data (the ROMs, the banks, and any
package built with them) is never published either way.

## 4. A or B

**B on the device, A on the PC as the answer key.**

- **On the device: our own native engine** (path B). It is the only way that is cheap enough with certainty and has no licence problem. The
  firmware's tables come from the user's EPROM at install time. The LA32 wave generator starts from Munt's (LGPL), and is then compared
  with Nuked's.
- **On the PC: a low-level D-50** (path A) as the reference: MAME's uPD78312 core running the user's IC25 + IC22, and Nuked's LA32 for the LA chip.
  It renders what a real D-50's LA chip would give, note by note, without a real D-50. That answers the question PicoFaceD5 could not answer
  (it had only plugin recordings), and every curve on the device can be tested against it bit for bit. It is never shipped, so the licences do not matter.
  What is missing for it: how the D-50's CPU addresses the LA32 (MAME does not have it; the disassembly and Nuked's MT-32 map give it), and the
  chorus and reverb, which nobody has decapped.
- **PicoFaceD5** is the map: its PROJEKTSTAND.md says where in the firmware each table is. If Michi71 grants the FM-1 linking exception, its engine
  could be ported directly, which would save months. Worth asking.

What stays uncertain whatever path is taken: **the reverb** (its coefficients are in the chip, so only measurements of a real D-50 can get it right),
the chorus, and the analog output stage.

## 5. Next

1. Answers from the user: is there a real D-50 (or the dumps of one's own unit), and is a question to Michi71 (and perhaps nukeykt) wanted?
2. Milestone 2 as planned: one synth partial on the device from Munt's wave generator, the TVA, MIDI on/off, 32 kHz, measured in fm1-emu.
3. In parallel, the PC reference (new milestone 1b): the uPD78312 + Nuked LA32 on the PC, first only to find the LA32 writes in the firmware.

## Sources

- MAME, `src/mame/roland/roland_d50.cpp` and `roland_mt32.cpp`: https://github.com/mamedev/mame/blob/master/src/mame/roland/roland_d50.cpp
- Nuked-MT32: https://github.com/nukeykt/Nuked-MT32 (`la32.cpp` header read through the GitHub API)
- gearmulator 88emu: https://github.com/dsp56300/gearmulator/blob/main/doc/88emu_readme.md
- Munt: https://github.com/munt/munt (`mt32emu/src/LA32WaveGenerator.h`)
- PicoVintageSynthCollection: https://github.com/Michi71/PicoVintageSynthCollection (README, `instruments/PicoFaceD5/README.md`, `doc/PROJEKTSTAND.md`, `include/d5_engine/d5_pcm_voice.h`)
- Wave ROM sizes discussed: https://www.fmjsoft.com/fmt/syxrd.htm, https://gearspace.com/threads/roland-d-50-filter-cutoff-slope-series-mode.1357116/
