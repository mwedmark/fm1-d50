# The D-50 firmware (v1.06): analysis notes

Issue #58. Written 2026-11-22. Status: **first pass**. A full decompilation is not done; this records what is established, how to reproduce it,
and where to continue. Anything not marked *verified* is a reading to check against the listing.

The listing itself is a derivative of Roland's code and is **not** in the repository: `tools/d50_dasm` makes it into `roms/out/dasm/` (git-ignored)
from your own dump. This file cites addresses (file offsets of `D50-v1.06.bin`, 64 KB) and describes behaviour in our words.

## Prior art

None found for the firmware itself (searched 2026-11-22). What exists:
- MAME's skeleton driver `src/mame/roland/roland_d50.cpp`: names the CPU (NEC µPD78312, 12 MHz), the memory map, the key scanner (MB63H149),
  and lists ROM versions 2.10 to 2.22 and the CPU's internal ROM (`d78312g-022`, CRC 9564903f). No firmware analysis.
- MAME's µPD78312 disassembler (`src/devices/cpu/upd78k/upd78k3d.cpp`, BSD-3-Clause, AJR): what `tools/d50_dasm` is built from.
- llamamusic.com hosts firmware binaries (also for the D-110); dxzl/roland-d50 is a patch editor. Neither analyses the code.

## Reproducing

1. Build `tools/d50_dasm/main.cpp` with the WASI SDK (the one the host tests use): `clang++ --target=wasm32-wasip1 ... -I tools/d50_dasm -I tools/d50_dasm/mame main.cpp mame/upd78kd.cpp mame/upd78k3d.cpp`.
2. Run it twice (bank 2 and bank 3, below) with node's WASI: it prints every address of the CPU's space decoded (`ADDR LEN FLAG TEXT`).
3. `tools/d50_dasm/analyze.py FIRMWARE.bin SWEEP2 SWEEP3 roms/out/dasm`: recursive descent from the entry points, into `listing.txt` and `functions.txt`.

`tools/d50_dasm/mame/*` are MAME's files unchanged (BSD-3-Clause, their headers kept); `emu.h` is a stand-in for MAME's `emu.h`.

## Machine

- CPU NEC µPD78312 (78K/III family), little endian, 12 MHz. 16-bit address space.
- The dump is the program EPROM, 64 KB. In the CPU's space (MAME's map, and consistent with the code here):
  - 0x0000-0x7FFF: the dump's first 32 KB direct. The first 8 KB is code and tables as the CPU's internal ROM would have them (MAME's machine has a
    separate 8 KB internal ROM with another CRC; the v1.06 dump's first 8 KB is **not** that one, CRC 74D5AE87). *Read as: v1.06 ran it from the EPROM.*
  - 0x8000-0xBFFF: a bank of the EPROM, file 0x8000 (bank 2) or 0xC000 (bank 3), chosen by a port (not found in the code yet). The listing is by file offset.
  - 0xC000-0xDFFF RAM (the stack starts at 0xDFFF). 0xF400-0xF7FF the key scanner. 0xFE00 and up the CPU's own RAM and SFRs.
  - The tone RAM (battery backed) and the memory card sit in the banked window too (MAME: `eram_map`).
- Interrupt vectors, 0x0000-0x003F, words. Reset is **0x0699**: `MOV MM,#07H` (the memory-mapping register) then `BR !2000H`. The other vectors point at 0xDEC3, 0xDEC6, ...
  (3 bytes apart): RAM. The BR table at 0x40-0x78 (`BR !xxxx`, 3 bytes each, 0x0699 first) is what sits there: code at 0x80 copies it to 0xDEC0 (*verified* in the listing: it loads
  0x0040, 0xDEC0 and a count of 0x39 bytes), so each interrupt goes vector -> RAM `BR` -> handler, and a handler can be replaced at run time. Entries that were never given a handler
  all branch to 0x2000, which is itself a table whose first entry `BR !20D9H` is the start-up routine.
- Start-up at 0x20D9: stack, memory map (`MM`), the refresh mode, then calls to 0x21DA, 0x23EA, 0x2232, 0x22A7, 0x21AF, 0x2302, 0x22DE, 0x233C: the hardware set-up.
  - 0x21F2: the serial port: `SCM`=0x49, `SCC`=0, `BRG`=0x30, then the three serial interrupt controls cleared. (31.25 kbaud from 12 MHz is the MIDI rate; the values fit but I did not compute them: check.)
  - 0x2205: timer 0 (`MD0`=0x01D3, `TMC0`=0xC0) and its interrupt: the firmware's tick.
  - 0x23BB: the A/D converter set-up (`ADM`, `ADMS`=0x97, `ADIC`=0x27). 0x5901 and 0x58F5 start a conversion on a channel (`ADM`=0x15 and 0x13) and compare the result with 0x80
    (the middle): the code shape of reading a centred control. *Which control they read (the joystick, the bender, the modulation lever, a pedal) is not established yet.*
- Key scanning: the handler at 0x065D (vector via the BR table) reads the key scanner's registers at 0xE000.. and stores into 0xE400..: the panel scan. (*reading*, not verified.)

## What the screen texts say (verified, strings in the dump)

Everything the earlier work took from the dump stays valid: the play-screen template at 0x6EAC, the edit page titles at 0xF940, the MIDI pages and the function menu's texts at 0x74ED-0x7565
(`Master Tune`, `Protect`, `PedalSW`, `ExtCont`, `Key Transpose =`), panel labels at 0x6AA1 (Tune, MIDI, Intr, Card, Lcal, Valu, Lowr, Uppr, Tran, Case, Prta).

## The D-50 has no controller for the balances

The D-50's MIDI implementation takes CC1, 5, 6, 7, 64, 65 and the RPNs (`d50.h`). The joystick's X and Y are A/D controls on the instrument, not MIDI messages, so the balances
have no D-50 CC to follow. #58's **CC81 (tone balance) and CC82 (partial balance)** are ours, as CC80 (Chase Play) is: 0..127 over the balances' 0..100, set as K1 and K2 set
them (an edit of the current patch). The firmware's own joystick scaling, when found, may change the curve.

## The MIDI receive path (verified in the listing, 2026-11-22)

`tools/d50_dasm/show.py SWEEP ADDR [N]` prints the linear disassembly from any address (the descent does not reach these routines: they are called through RAM vectors and tables).

- **0x4ADF, the serial receive interrupt** (register bank 4): reads `RXB`. Real-time bytes are sorted here: 0xF1..0xF7 and 0xFE are handled or flagged (0xFE, active sensing, sets a flag in 0xFE31.1);
  everything else is queued by 0x527F into a ring buffer at 0xC100 (write index 0xFEAD, read index 0xFEAC). The `RXB` reads at 0x531F and 0x5492 are the SysEx side (to follow).
- **0x4B09, the main loop's parser**: takes bytes from the ring buffer (0x4B22). A status byte (bit 7) goes to 0x4B30, a data byte to 0x4B9F. The running status is 0xFEAB (0 for none), the first data byte 0xFEA4, the second 0xFEA5;
  0xFE45 holds the receive mode (compared with 0x2A: a mode in which the channel is not tested) and 0xFE42 the basic channel (the channel test at 0x4C72 compares the status's low nibble with it, rejecting a higher one).
- **Status 0x80-0xEF** (0x4C3B): 0xE0 and up the pitch bender (0x4C65: 14 bits, stored at 0xD315/0xD316 and, per tone, 0xD3D2..), 0xB0 and up the control change (0x4D00), 0xA0 and up (polyphonic key pressure) ignored,
  0x90 and up note (0x4CE5), below that note off. Program change (0xC0) and channel pressure (0xD0) are one data byte (0x4BB8, 0x4BCC); program change is only taken when the MIDI page's Prog.C switch (0xC5C9 bit 0) is on.
- **Control change, 0x4D00**: a number of 0x7A and up is a channel mode message (table dispatch at 0x4D3F). Below that the number is looked up in a table by routine 0x4039 (compare-and-find, then call through the address table):

  | CC | handler | meaning |
  | --- | --- | --- |
  | 1 | 0x4DE4 | modulation lever (switch 0xC5C5.0: the MIDI page's Mod) |
  | 5 | 0x4E00 | portamento time |
  | 6 | 0x4D67 | data entry MSB (RPN 0: the bender range) |
  | 7 | 0x4E1B | volume (switch 0xC5C6.0) |
  | 38 | 0x4D96 | data entry LSB |
  | 64 | 0x4E30 | hold pedal (switch 0xC5C7.0) |
  | 65 | 0x4E49 | portamento switch |
  | 98, 99 | 0x4D58 | NRPN: ignored (it only sets a flag so that a following data entry is not taken as an RPN's) |
  | 100, 101 | 0x4D61, 0x4D5B | RPN LSB, MSB |
  | any other | 0x4DC9 | the two controller numbers set on the MIDI page (bytes 0xC5CF + 0x40 and 0xC5D0): they run the 0x4E54 and 0x4E6E tables (the assignable "Control" targets) |

  This is the whole table: **the D-50 takes no controller for the tone or partial balance**, which fits `d50.h`'s list of what the engine implements (issue #4). CC81 and CC82 (#58) are ours.
  The mode messages 0x78-0x7F and the all-notes-off are in the 0x4D3F table (not decoded here; the app's `d50_panic()` covers 120 and 123).

Open: the SysEx parser (0x4FC6 for status 0xF0's data bytes), the joystick and the lever's A/D path (the test mode's page names `Stick-X`, `Stick-Y`, `E.Pedal`, `ExCont`, `P.Hold`, `PedalSW` at 0x764F-0x7690
are the controls the firmware reads; where it reads the stick is not found: only two A/D channels are started by name, ADM=0x13 and 0x15 at 0x58F5 and 0x5901, and the stick may arrive through the key scanner's registers at 0xE000).

## Coverage and what is next

First recursive descent from reset, the vectors and the two tables: 6 122 instructions in 367 functions, about 14.8 KB of the 56 KB above 0x2000/0x80 that are code and data
together. The rest is reached by indirect jumps and by handlers installed into the RAM vectors at run time, which the descent does not follow. To do, in this order:
1. (Done for the serial interrupt: it is at 0x4ADF.) Find where the other RAM vectors are rewritten (the writes into 0xDEC0-0xDEFF are by index, not by absolute address: look for loops over `[DE]`/`[HL+B]`), and add those handlers as entry points.
2. SysEx (0x4FC6) and the channel and omni logic (the MIDI page's bytes at 0xC5C0..0xC5D0).
3. The A/D handling: channel assignment (joystick X/Y, bender, modulation, aftertouch, pedals), scaling, and what the stick does in each key mode. The joystick's real behaviour (K1/K2 here are a guess from the D-50's manual).
4. The function menu and the tune/transpose parameters: ranges and steps (issue #57).
5. Name things: write `tools/d50_dasm/names.txt` (address, name) and have analyze.py use it, so the listing becomes readable.
