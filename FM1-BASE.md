# The D-50 on fm1-base? (study, 2026-11-20)

Could fm1-d50 run on [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0, on felucca-bsp) instead of M-VAVE's
fm1_open SDK? Read from fm1-base at 7337375 and felucca-bsp at its pinned commit. **Nothing was built**: fm1-base's
build needs JieLi's Linux toolchain (downloaded from pkgman.jieliapp.com, run in Docker), which was not fetched. The
template's numbers below are fm1-base's own (its README, October 2026); ours are from `build_d50.ps1`.

## Verdict

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
