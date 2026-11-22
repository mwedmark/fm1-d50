# The D-50 on fm1-base? (study, 2026-11-20)

> fm1-base is now the D-50's only platform. The fm1_open build this compares against has been removed from the repository; where this
> file names `fm1_open/app/d50/` and `fm1_open/app/resample_table.inc`, they are now `d50/`. `tools/d50_emu_site.js` is gone with it.

Could fm1-d50 run on [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0, on felucca-bsp) instead of M-VAVE's
fm1_open SDK? Read from fm1-base at 7337375 and felucca-bsp at its pinned commit. The first part below is the study from
reading; **"The fork, built and run"** at the end is what came of trying it.

## Verdict

(The fork described here was then built and run: see the last section.)

**Not as fm1-base is.** It gives far more room for code and RAM, but its flash map has no place for the D-50's data:
534 088 bytes (512 KB of PCM, which the engine reads as one block through one pointer, and the packed bank), against
412 KB of data windows in two pieces. It works only with a fork of fm1-base's and felucca-bsp's flash layout (below), which
fm1-base's installers and the other firmwares on it do not know about. That is worth it only if the room is needed: for
the CPU, which is what limits the D-50 now, fm1-base gains little.

## What each gives

| | fm1_open (now) | fm1-base |
| --- | --- | --- |
| App image | 453 921 B limit; 393 KB of it M-VAVE's closed `libfm1_engine.a` (it must stay linked: `sdk/Build.ps1`); **3.7 KB to spare** | 568 KB slot (0x120 .. 0x8E0DC of the app area [0x4000, 0x93000)); template 77 KB, own Bluetooth MIDI + 41..46 KB; with the D-50 about 185 KB (estimated): **about 380 KB to spare** |
| D-50 data | the user flash, 536 576 B at 0x7C000, contiguous, plain XIP | [0x93000, 0xE0000) 308 KB contiguous, [0xE5000, 0xFC000) 92 KB, [0xFC000, 0xFF000) 12 KB on request: **412 KB, the D-50 needs 522 KB** |
| RAM | heap 119.6 KB after the SDK; reverb lines in the SDK's idle effect memory | 32 KB RAM code, 96 KB .data/.bss, 336 KB pool (or 464 KB as one "packed" budget); template 27 KB + 68 KB pool |
| SRAM code | the SDK's DX7/303/808/PSG/pluck/mixer must sit in SRAM; the D-50's hot code (~11 KB) beside them | 32 KB `.ram_text` (flash driver included); RAM code may call only RAM code (`tools/ramcode.py` checks) |
| CPU | boots at 320 MHz (`sf2clock.c` can raise it to 360 MHz; the D-50 build does not) | 360 MHz stock, 396 optional; second core through `plat_cpu2_run` |
| Licence | GPL-3.0-or-later engine on a closed SDK | MPL-2.0 platform; the firmware stays GPL (MPL-2.0 is GPL-compatible) |

## Why the data does not fit

- The whole 512 KB of PCM is used: the 100 samples of `d5_pcm_table.h` cover all 262 144 words. All 16 bits are used
  (11 382 distinct values), so packing it smaller would mean an index table plus a decode step on every sample read, on
  a CPU that is already the limit.
- The windows are fixed by the update loader (felucca-bsp `loader/ldr_core.c`): it writes only [0x4000, 0x93000), keeps
  its staging and the crash records in [0xE0000, 0xE5000), and looks for update records in every sector of
  [0x93000, 0xFC000) outside the store (a sector whose bytes 0xF06..0xF07 read "AT" with a CRC at 0xF00 is made invalid).
- The SD card (`sd = true`) needs a hardware mod (m-card), is not yet run on an FM-1, and reads 1.35 MB/s: too slow for
  sixteen voices reading anywhere in the PCM.

## The fork that would fit

App area [0x4000, 0x5D000) (356 KB, room for the ~185 KB firmware), D-50 data [0x5D000, 0xE0000) (524 KB, one block below
the loader's staging). It changes:

- felucca-bsp `loader/ldr_core.c` (`LDR_APP_HI`, `LDR_REC_LO`) and `loader/loader.c` (`FL_RANGE_OK`);
- fm1-base `tools/fm1pkg_make.py` (`APP_SLOT`, the VM entry), `device/app.ld` (the XIP length, `_app_xip_end`), `include/plat.h`
  (`PLAT_DATA_LO`, the windows) and the plain XIP window (`FM1_FLASH_PLAIN_LO`, `fl_plain_window_init`: one window from LO
  up, so it can start at 0x5D000);
- the D-50 install: the data into the data area over USB (`APP_USB_CMD` or the companion's files) instead of `sf2_upload.c`.

Risks: the installers and The Firmware Hub assume fm1-base's map; another fm1-base firmware installed afterwards keeps its
store objects in the same area and overwrites the D-50 data (it is installed again); and the loader's record scan must not
reach the PCM.

## The port itself

`app/d50/d50.h` is already a small C interface, so the engine and `d50.cpp` stay. To write against `include/plat.h`:

- the render: `app_render`, the 32 kHz to output-rate resampler that `sf2out.c` has now, and the CPU guard;
- the second core: `fm1b_split_*` (`cpu1_split.c`) to `plat_cpu2_set` / `plat_cpu2_run` / `plat_cpu2_wait`;
- the reverb's 48 KB of lines: from the SDK's idle effect memory (`fm1_fx_pool_*`) to the pool;
- `D5_HOT` / `D5_IN_RAM`: from `.volatile_ram_code.*` to `PLAT_RAM_CODE` (`.app_ram`), within 32 KB, with no calls into XIP;
- MIDI in, the panel and the screen (patch name, CPU line), now in `sf2player.c` and `fm1b.c`;
- Bluetooth MIDI with `ble_stack = "own"`.

## Cheaper first

- 360 MHz instead of 320 MHz: about 12 % more CPU on fm1_open too (`sf2clock.c`, its self-tests, `sf2clock_toggle`), if
  the 3.7 KB of image now spare hold it. That is what fm1-base's stock clock would give.
- For room, the app's own constructors (`Patch`, `PatchSpec`, `Tone`, `Voice`: about 6 KB) before a new platform.

## The fork, built and run (2026-11-20)

The layout above, made a setting and tried. It runs the D-50 in fm1-emu with its data in one block; not tried on an FM-1.

**The build.** fm1-base's pinned toolchain (`tools/setup_toolchain.sh --only jieli,ac79-sdk`, checked against
`tools/toolchain.lock`) in a Linux container (`python:3.12-slim` with git, curl, xz, libfribidi0, libraqm0, Pillow;
`JIELI_DOCKER=0` inside it), with `D:\git` mounted at `/git`:

    docker exec fm1b sh -c "cd /git/fm1-base && JIELI_DOCKER=0 python3 tools/build.py ../fm1-d50/fm1base --no-ble"

The unchanged template builds to 90 608 B of image, 28 KB of RAM and 74 KB of pool, close to the README's numbers.

**The fork** (`D:\git\fm1-base`, branch `d50-layout`, with felucca-bsp on its own `d50-layout`; local only, not pushed):
`data_lo = 0x5B000` in an app's `fm1.toml` gives

- the package: a CODE region and app slot that end at data_lo (`tools/fm1pkg_make.py set_data_lo`), the VM region from there;
- the linker script: the smaller XIP region and `_app_xip_end` (`build.py data_lo_script`);
- the device: `FM1B_DATA_LO` (plat.h's data area from there, XDATA0 empty), felucca-bsp's `FL_DATA_LO` and XDATA0 (the store
  and write regions) and `FM1_FLASH_PLAIN_LO` (the unencrypted XIP window from there);
- the update loader: `LDR_DATA_LO`, so it writes only [0x4000, data_lo) and looks for update records only above the data area.

Without `data_lo` everything is as before. The template built with `data_lo = 0x5D000` runs in fm1-emu as the plain one does.

**Why 0x5B000.** The store may not use 0xE0000..0xFBFFF (the loader's staging) and the panel's calibration needs two sectors.
The D-50's 534 088 bytes from 0x5B000 end at 0xDD648, so the panel's object is 0xDE000/0xDF000 and the settings 0xFC000/0xFD000.
The app area is [0x4000, 0x5B000): 348 KB, an app slot of 352 188 B.

**The app** (`fm1base/`): `app.c` (keys, OCT, SELECT / PRESET for the patch, SAVE, MIDI in with notes, bend, program change
and all-notes-off; the screen; the 32 kHz to 44.1 kHz resampler of `sf2out.c`; the CPU guard on `plat_cpu_pct`), and
`d50_unit.cpp`, the one C++ unit at -O2: `fm1_open/app/d50/d50.cpp` with its `FM1_BASE` part (the data through `plat_res`,
the patch, the bank and the reverb's lines in the pool, the second core through `plat_cpu2_run` / `plat_cpu2_wait`) and
`d50_inflate.c`. `fm1base/inc` has the few libc headers fm1-base lacks; its `math.h` makes `d50_math.c` inline (below).

**The data: the resource region.** fm1-base's `resources` (`device/res.c`, after Melodee's): `fm1.toml resources =
"../roms/out/d50_pcm.fmd5"`, `app.h` `APP_RES_BASE 0x5B000`, `APP_RES_SECTORS 131`. The build writes the region (a 32-byte
commit record, then the 534 088 bytes) into the package and beside it (`build/d50-res.bin`, 534 120 B); the update loader
writes it after the app, from any host that serves the package, and keeps it when it is already this build's. `plat_res`
answers only when the region checks (magic, length, CRC-32), so a torn install reads as NO D-50 DATA, never as noise.
The resources need `backup = true` (the transfer is the restore's), so the settings are a backup of one object.
**The package holds Roland's data: it is for our own FM-1 and is never published** (`build/` and `roms/` stay out of git).

**The engine's audio code in RAM.** fm1-base's RAM code (`.app_ram`, 32 KB with the platform's) may call only RAM code
(`tools/ramcode.py` checks the disassembly), and RAMTEXT (0x01C00000) is more than a direct call's reach (+-4 MiB) from the
XIP code (0x02000120 on), in both directions. So under `FM1_BASE` (`d5_hot.h`):

- `D5_HOT` puts a function in `.app_ram.d5_*` (a section for each, as on fm1_open);
- `D5_HOT_SHARED` (the envelopes' `arm`, which a note's start calls too) and the math (`fast_sin` .. `fast_log2`, `d50_math.c`'s
  `powf`, `log10f` .. through `inc/math.h` and `cxx/cmath`) are forced inline instead: one copy in each caller;
- `D5_HOT_GATE`: `Patch::render_block` stays in XIP and enters the RAM code through `D5_FAR` (a pointer the compiler cannot
  see through, as `PLAT_FAR`) to `part_entry`; the other core's `run_job` is RAM code;
- `Patch::render_part` hands the other core its job through `split_begin` / `split_wait` (XIP, once a block):
  `fm1.toml ram_code_allow` names it;
- the bit copies in `d5_fastmath.h` are `__builtin_memcpy` (fm1-base builds `-fno-builtin`: `std::memcpy` was a call into XIP);
- `D5_IN_RAM` (the sine and exp2 tables) is `.data`, in the SRAM.

`app.c`'s resampler loop is `PLAT_RAM_CODE` too, called through `PLAT_FAR`. The effects one block behind the voices
(`D5_FX_LAG 96`) and blocks of 96 samples are on as on fm1_open. Without `FM1_BASE` none of this changes
fm1_open: its build is byte for byte the size it was (448 964 B).

**The time per block over USB** (`APP_SYSEX`, `app.c app_sysex`): the same frames as fm1_open's `sf2_upload.c`, so the same
scripts read both: `F0 7D 'S' 'F' 02 0B 00 F7` answers `F0 7D S F 02 4B` and eleven 5 x 7-bit values (v0..v8 `d50_prof_sum`,
then from zero; v9 CPU average | peak << 12 | CPU1's share << 24; v10 voices | limit << 8 | cores << 16), and
`F0 7D S F 02 0C bits F7` leaves out 1 the reverb, 2 the chorus, 4 the EQ. The CPU figure is `plat_cpu_pct`, the peak the
highest the main loop saw since the last ask.

| | fm1_open (now) | fm1-base fork |
| --- | --- | --- |
| Image | 448 964 B of 453 921 (4.9 KB to spare) | 163 544 B of 352 188 (**about 188 KB to spare**) |
| RAM | heap about 2.9 KB over its floor | RAM code 22.9 KB of 32 (the engine's 17.5 KB); .data+.bss 37.7 KB of 96; pool 238 KB of 336 |
| D-50 data | user flash at 0x7C000, installed apart (`install.html`) | the resource region 0x5B000 .. 0xDE000, in the package |
| CPU | 320 MHz, 360 MHz with EDIT on the D-50 page | 360 MHz |

**In fm1-emu** (240 MHz, Fantasia, the eight-key chord, 16 voices; `node tools/d50_emu_site.js fm1base/build/d50.fwsc
fm1base/build/d50-res.bin 0x5B000` puts the region where the loader would): the screen shows PATCH 11, Fantasia, VOICES 16,
REVERB, 2 CORES. The firmware's own load with both cores: **39 to 41 %** (it was 47 to 50 % with the engine in XIP; the
emulator counts instructions, so it does not show what the XIP cache costs on an FM-1, where that was the biggest part). The
sound comes out (RMS 0.046, peak 0.157 in an analyser on the page's output), silence before the chord and after it.

**Installing it, and the way back.** The update protocol stages the loader of the package being installed (felucca-bsp
`chip/ota.c` step 1; the SPL runs it): from fm1_open, M-VAVE's firmware stages this fork's loader, which writes the app
[0x4000, 0x5B000) and the resource region; back to fm1_open, fm1-base stages fm1_open's package's own loader, which writes
its whole app area. fm1_open's D-50 data at 0x7C000 is overwritten by the region, so going back means installing it again
(`install.html` does both). If a start fails twice the FM-1 enters UBOOT, and only felucca-bsp's `fm1_rescue` (macOS: the
stock firmware) brings it back: someone has to be at the FM-1.

**Not done or not known yet:**

- Not checked by ear or by recording the FM-1's USB audio for dropouts; the profiler's peak (66 %) is well under the
  audio interrupt's deadline (87.5 % of a block).

**On the FM-1 (2026-11-20).** `tools/fm1_install.py fm1base/build/d50.fwsc --port USB-Midi --yes` from fm1_open: the fast
install, then FM-1_5000 runs; its USB port is then called "D-50 FM-1", so the installer's resource step did not find it
and `tools/fm1_resources.py fm1base/build/d50-res.bin --port "D-50 FM-1"` wrote the region. Fantasia, eight keys (16
voices), 360 MHz, both cores, cmd 11: **64 % (peak 66 %)**, against fm1_open's 80 % (peak 86 %) at 320 MHz; per block
CPU1's voices 1.55 ms, all of the render 1.70 ms, resampling 0.14 ms; silent 9 %.

**Every factory patch with 16 voices (issue #13, 2026-11-20).** Sixteen keys over five octaves held for 4 s, then released,
on each of PN-D50-00's 64 patches, the FM-1 silenced and settled between them. cmd 11 reports the missed audio blocks
(`plat_xruns`, v11); fm1-base has no margin under 100 %: a render past its half is a dropout.
- **At 360 MHz**, 62 patches had no missed block. Two patches did:
  - Afterthought: 96 %, 190 missed blocks in 4 s with every voice held.
  - Spacious Sweep: 97 %, 5 missed blocks in its release.
  - JX Horns-Strings, PressureMe Strings and Pressure Me Lead were at 88–92 % with none.
- **The CPU guard** used to retire only released tails, and capped only over 97 %. Now, with no tail left and over 93 %
  or after a missed block, it fades the voice held longest (`Patch::shed_held_voice`) and caps each tone a voice under its
  share. A new program starts uncapped.
  - At 360 MHz: Afterthought had 1 missed block (the one that triggers the guard), then ran at 91 % with a voice less.
  - At 360 MHz: Spacious Sweep kept 14 voices, with none missed.
- **At 396 MHz** (`cpu_clock = 396`, JieLi's top):
  - All 64 patches hold all their voices with no missed block and the guard idle.
  - The heaviest: Spacious Sweep 88 % (release 90 %), Afterthought 87 %, JX Horns-Strings and Pianissimo 83 %, PressureMe Strings 81 %.
  - Fantasia: 58 %.
  - fm1-base raises the clock after its boot guard's choice, so two hung starts fall back to 360 MHz in safe mode, with
    USB on to install again.
- Bluetooth MIDI (`ble_stack = "own"`), velocity (`APP_TOUCH`).
