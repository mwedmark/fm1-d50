# The D-50 on fm1-base? (study, 2026-11-20)

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
`d50_unit.cpp`, the one C++ unit at -O2: `fm1_open/app/d50/d50.cpp` with its new `FM1_BASE` part (the data through `plat_data`,
the patch, the bank and the reverb's lines in the pool, the second core through `plat_cpu2_run` / `plat_cpu2_wait`) and the two C
files it needs. `fm1base/inc` has the few libc headers fm1-base lacks; `fabsf` was the only function missing at the link.
`D5_HOT` and `D5_IN_RAM` are plain under `FM1_BASE` for now (fm1-base's RAM code may call only RAM code).

| | fm1_open (now) | fm1-base fork |
| --- | --- | --- |
| Image | 450 188 B of 453 921 (3.7 KB to spare) | 150 932 B of 352 188 (**about 200 KB to spare**) |
| RAM | heap 119.6 KB left | .data+.bss 34 KB of 96, pool 234 KB of 336 (patch, bank, reverb lines) |
| D-50 data | user flash at 0x7C000 | data area at 0x5B000 .. 0xDD648 |

**In fm1-emu** (240 MHz, Fantasia, the eight-key chord, 16 voices; `node tools/d50_emu_site.js FIRMWARE roms/out/d50_pcm.fmd5 0x5B000`
puts the data where data_lo says): the screen shows PATCH 11, Fantasia, VOICES 16, REVERB. The firmware's own load, one core:
**78 %** on fm1-base, **79 %** (max 83 %) on fm1_open: the same engine, and the emulator counts instructions. With the second core
on fm1-base: **50 %**. The sound comes out (RMS 0.032, peak 0.094 in an analyser on the page's output, silence before the chord);
about 4 dB under fm1_open's at the same MASTER, from how app.c applies MASTER. (fm1-emu's idle share does not apply: fm1-base's
main loop never idles. The page's own panel did not draw in the browser pane; `window.__lcd` was drawn to a canvas instead.)

**Not done or not known yet:**

- On an FM-1: the loader with the smaller app area, the plain XIP window from 0x5B000 (fm1-emu does not check it), the speed with
  the engine in flash and at fm1-base's 360 MHz.
- Getting the data there on a device: fm1-base has no installer for a 534 KB block; an app's own USB command (`APP_USB_CMD`,
  `plat_host_read`) writing the data area with `plat_data_write` would be the way, with a page that sends the `.fmd5`.
- Bluetooth MIDI (`ble_stack = "own"`), velocity (`APP_TOUCH`), the engine's hot code in fm1-base's RAM code.

