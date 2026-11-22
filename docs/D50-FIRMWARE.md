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
have no D-50 CC to follow. #58's **CC81 (tone balance, the stick's Y) and CC82 (the selected tone's partial balance, X)** are ours, as CC80 (Chase Play) is: 0..127 over the
balances' 0..100, set as K2 and K1 set them (an edit of the current patch). Their linear 0..100 is the stick's own on the play screens (0x3114, below).

## The MIDI receive path (verified in the listing, 2026-11-22)

`tools/d50_dasm/show.py SWEEP ADDR [N]` prints the linear disassembly from any address (the descent does not reach these routines: they are called through RAM vectors and tables).

- **0x4ADF, the serial receive interrupt** (register bank 4): reads `RXB`. Real-time bytes are sorted here: 0xF1..0xF7 and 0xFE are handled or flagged (0xFE, active sensing, sets a flag in 0xFE31.1);
  everything else is queued by 0x527F into a ring buffer at 0xC100 (write index 0xFEAD, read index 0xFEAC). The `RXB` reads at 0x531F and 0x5492 are the SysEx side (to follow).
- **0x4B09, the main loop's parser**: takes bytes from the ring buffer (0x4B22). A status byte (bit 7) goes to 0x4B30, a data byte to 0x4B9F. The running status is 0xFEAB (0 for none), the first data byte 0xFEA4, the second 0xFEA5;
  0xFE45 holds the screen number (0x2A: the Data Transfer screen while it runs, where channel messages are ignored and SysEx takes the handshake: below) and 0xFE42 the basic channel (the channel test at 0x4C72 compares the status's low nibble with it, rejecting a higher one).
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

## The SysEx parser (verified in the listing, 2026-11-22)

The registers of the running bank are memory here: 0xFEF0 is X (R0, the byte being parsed), 0xFEF1 A, 0xFEF2 C, 0xFEF3 B, 0xFEF4 E, 0xFEFE/0xFEFF HL. The addresses below are
SysEx addresses as one number, `a2 << 14 | a1 << 7 | a0` (so 00 03 40 is 0x1C0), as `d50.cpp`'s `addr_of` has them.

- **F0, 0x4B53.** Taken only when the MIDI page's **Exclu** switch is on (0xC5CA bit 0, system byte 10); off, the running status is cleared and the message is ignored to its F7.
  On: status 0xF0, 0xFE31.7 (header done) clear, 0xFE34.6/.7 clear, the previous byte 0xFF.
- **Each data byte, 0x4FC6**: added to the checksum 0xFEA6, then, until the header is done, dispatched by its index 0xFEAA through the table at 0x4FDD (routine 0x4056). Any fault sets
  the running status to 0: the rest of the message is ignored, nothing is answered.

  | index | routine | what it takes |
  | --- | --- | --- |
  | 0 | 0x4FF1 | 0x41 (Roland) |
  | 1 | 0x4FFA | the device ID: **exactly the basic channel** (0xC5CD, system byte 13; 0 = channel 1). Omni does not widen it |
  | 2 | 0x5006 | 0x14 (the D-50) |
  | 3 | 0x500F | the command. Outside Data Transfer only **0x11 RQ1 and 0x12 DT1**; on the Data Transfer screen (0xFE45 = 0x2A) the table at 0x504B: 12, 42 DAT, 40 WSD, 41 RQD go on to an address; 43 ACK, 45 EOD, 4E ERR, 4F RJC are complete (0xFE34.6); **11 RQ1 is refused there** (0xFE3B.7, the error flag). The command goes to 0xFEA4 and the checksum restarts at 0 |
  | 4 | 0x5068 | address byte 2. Outside Data Transfer **0 or 1**; on it **2 to 4** (stored less 2: internal memory starts at 0). ACK, EOD, ERR and RJC may not have one |
  | 5, 6 | 0x5090, 0x5094 | address bytes 1 and 0: the address goes to 0xFE24 |
  | | | DT1 then starts its data; on the Data Transfer screen only when the menu item is Bulk Load.O (0xFE53 = 6) and the address is the one expected next (0xFE20). DAT: only when the item is Bulk Load (0xFE53 = 1) and the address is the expected one |
  | 7..9 | 0x50DA, 0x50EA, 0x50EE | (RQ1, WSD, RQD) the size, byte 2 below 4: to 0xFE2C |

- **Data, 0x51B3.** Bytes are written one behind (the previous one, 0xFEF2), so the last byte, the checksum, is never stored. More than 256 data bytes in one message is a fault
  (0xFE34.7). Data after any command other than DT1 and DAT is a fault.
  - **Outside Data Transfer (0x51FA), byte by byte through the address map 0x5620:**

    | address | SysEx | where | parameter numbers (for 0x5727) |
    | --- | --- | --- | --- |
    | 0x000-0x0BF | 00 00 00 - 00 01 3F | the upper tone, RAM 0xC400: partial 1, partial 2 (bytes 0x00-0x3F of each), common (0x80-0xBF) | 0x00-0x35, 0x37-0x66 |
    | 0x0C0-0x17F | 00 01 40 - 00 02 7F | the lower tone, RAM 0xC4C0 | the same |
    | 0x180-0x1BF | 00 03 00 - 00 03 3F | the patch block, RAM 0xC580 | 0x69-0x8F |
    | 0x1C0-0x1FFF | | nothing: the rest of the message is ignored | |
    | 0x2000-0x22FF | 00 40 00 - 00 45 7F | RAM 0xDC00-0xDEFF (where the vector trampolines are), two nibbles a byte (low first), and only on screen 0x05 | |

    **A value above the parameter's maximum (the range tables at 0x7D20) is skipped, not clamped**: the byte keeps its old value and the address moves on. A stored patch-block
    byte with a parameter number of 0x7B and up calls its handler (the table at 0x5735: 0x7B, 0x7C, 0x83, 0x85, 0x87, 0x8B, 0x8E, 0x8F: key mode and the other bytes that need more
    than a store); the tones' bytes are read where they are.
  - **On the Data Transfer screen (0x5247)**, in order from the expected address, counting down the size the WSD gave, through the internal memory's map (0x56C5):
    0x0000-0x3EFF (02 00 00 -) to 0x8040 in one RAM bank, 0x3F00-0x6FFF to 0x8000 in the other (0x39E1, 0x39E5 select them): **64 patches of 448 bytes, as sent**; then
    0x7000-0x877F (**03 60 00 - 04 0E 7F**) two SysEx bytes a RAM byte, a nibble each (0xFE: the reverb data); 0x8780 and on nothing.
- **F7, 0x50FB**, dispatched by the command (table at 0x5112):

  | command | routine | what it does |
  | --- | --- | --- |
  | 12 DT1, 42 DAT | 0x512F | checksum: wrong sets the error flag, right 0xFE3B.5 (taken). **The data are already written: a wrong checksum does not undo them** |
  | 11 RQ1 | 0x513B | right checksum: answers (0x5474) |
  | 40 WSD | 0x514A | only in Bulk Load (0xFE53 = 1): 0x5164 |
  | 41 RQD | 0x5157 | only in Bulk Dump (0xFE53 = 0): 0x5164 |
  | 43 ACK | 0x5189 | only in Bulk Dump: 0xFE3B.3 and .4 (the next DAT may go) |
  | 45 EOD | 0x5197 | only in Bulk Load, with a transfer under way and all its bytes taken: 0xFE3B.2 (done) |
  | 4E ERR | 0x51AD | the error flag |
  | 4F RJC | 0x51B0 | 0xFE3B.0 (the other side gave up) |

  0x5164 (WSD, RQD): not while a transfer is under way, right checksum, an address in the map: the transfer starts at that address with that size (0xFE20, 0xFE22). A size of 0 starts
  nothing; the test (`CMP 0FE2CH,#00H`) reads only the size's low byte, so a size that is a multiple of 256 starts nothing either (the D-50's own 02 0F 00 is 0x8780: not one).
- **RQ1's answer, 0x5474**: only from the temporary area (below 0x1C0) and only a size above 0. DT1s (the template at 0x6DD0: `F0 41 dev 14 12`, the device ID filled in from
  the basic channel) of **up to 256 data bytes** each (0x533C: the count in C from 0), cut at 0x1C0 (the map gives nothing there).
- **The Data Transfer menu**, the table at 0x3822 by the item 0xFE53 (run from 0x3816):

  | 0xFE53 | routine | the D-50's item | what it sends |
  | --- | --- | --- | --- |
  | 0 | 0x5299 | Bulk Dump (handshake) | WSD 02 00 00, size 02 0F 00 (0x8780 bytes: the whole memory, the template at 0x6DB4), then on each ACK a DAT of 256 bytes, then EOD (0x6DE3) |
  | 1 | 0x5497 | Bulk Load (handshake) | RQD 02 00 00, size 02 0F 00 (0x6DC2), then ACK (0x6DDC) to each DAT |
  | 2, 3, 4 | 0x391F, 0x38E2, 0x3967 | the card (load, save; P0.7 is the card's write line) | |
  | 5 | 0x52CF | Bulk Dump.O (one-way) | DT1s of 256 bytes from 02 00 00, the whole memory, 0x545A between them (not decoded: a pause, by its place) |
  | 6 | 0x54DE | Bulk Load.O (one-way) | nothing: takes DT1s in order from 02 00 00 |

  Both end on screen 0x2B (the message that it is done), or for a fault back on 0x29 (the Data Transfer menu, which the Tran key 0x1B shows: 0x4881) or 0x2C. The templates after 0x6DE3 are ERR and RJC (`F0 41 dev 14 4E F7`, `4F F7`) and the
  all-notes-off `B0 7B 00`.

### Against the engine's `d50_sysex` (issues #6, #51)

| | the D-50 | `d50.cpp` |
| --- | --- | --- |
| device ID | the basic channel exactly | the same (#51) |
| the Exclu switch | off ignores all SysEx | none: always on |
| DT1 to the temporary area | byte by byte; a value above the parameter's maximum skipped | the same since #58 (`temp_max`: the three range tables); before, any 0..127 stored |
| DT1 with a wrong checksum | already written; only flagged | nothing written (`D50_ERR_SUM`) |
| more than 256 data bytes | a fault | taken |
| DT1 / RQ1 to internal memory (02 00 00) | only on the Data Transfer screen (Bulk Load.O: DT1 in order); RQ1 never | any time, also RQ1 |
| the handshake | only on the Data Transfer screen, in the item's direction | any time, both directions |
| internal memory's map | 64 × 448 bytes as sent, then the reverb data at 03 60 00 - 04 0E 7F as nibbles | the bank; 03 60 00 and on taken and ignored |
| RQ1's answer | DT1s of up to 256 bytes, the temporary area only | the same chunk, cut at the area's end |

The engine takes more than the D-50 does (the switch, the screen and the direction it does not ask for), and on a wrong checksum it is stricter. On the values it follows the
D-50 (#58): a byte of the temporary area above its maximum is skipped, wherever it comes from (DT1, or a DAT the D-50 would not take there), and the address moves on;
`test_sysex` checks it. The bank's bytes (02 00 00) are stored as sent, as the D-50's Bulk Load stores them.

## The analogue controls and the joystick (verified in the listing, 2026-11-22)

- **Sampling.** 0x23BB sets the A/D converter to run by *macro service* (the 78K/III's interrupt-driven transfer: `ADIC`=0x27, `ADMS`=0x97, pointer 0xFEEC = 0xC02F, count 0xFEEE = 10,
  SFR 0xFEEF = 0x6A, `ADCR`), so the hardware fills a buffer downward from 0xC02F without code. When a block is done, the interrupt at **0x3413** (register bank 1) steps an external
  multiplexer on P1.0-P1.2 (8 inputs), moves the buffer pointer (wrapping at 0xC010) and restarts the conversion.
- **Averaging.** 0x33D7 sums four samples of each of the 8 inputs (0xC010..0xC02F) and shifts right by 2: the results are 0xC008..0xC00F. Inputs 1 and 2 are then inverted (XOR 0xFF).
  0x33C7 copies them to 0xC000..0xC007, where the control routines read them. 0x3015 runs the eight control routines in turn.
- **Which input is which.** The test mode shows them on two pages (texts at 0x7650 and 0x7678); each routine prints its value at a column of 0xFE3D (0x28 = the lower line's first column):

  | RAM | routine | column | the test page's name |
  | --- | --- | --- | --- |
  | 0xC003 | 0x3173 | 2 | Bender |
  | 0xC005 | 0x31A9 | 10 (page 1) | Mod |
  | 0xC007 | 0x320B | 18 | After |
  | **0xC001** | **0x3031** | **26** | **Stick-X** |
  | **0xC002** | **0x305A** | **34** | **Stick-Y** |
  | 0xC006 | 0x31D2 | 10 (page 2) | ExCont |
  | 0xC004 | 0x3318 | (page 2) | E.Pedal (by elimination) |
  | 0xC000 | 0x3248 | - | the data entry slider (by elimination: the eighth input, not on the test pages) |

- **What the stick moves.** The firmware edits parameters through 6-byte descriptors: +0 the parameter number, +1 its stored value, +2 its maximum, +3 the current value, +5 the value the
  stick moves from. **Stick-X uses the descriptor at 0xC0B6, Stick-Y the one at 0xC0BC.** What they point at is set by panel keys (below): with the balance on, they are given
  - **Stick-X: parameter 0x66 = tone common byte 47, the partial balance, of the tone the Lowr or Uppr key chose** (P1.3: 0x3EB4, below). A tone common parameter's
    RAM is otherwise the edited tone's (0xFE54.6 picks 0xC400, the upper, or 0xC4C0, the lower: 0x3B9C; the partial-select keys set it, 0x4650: the first two the lower, the
    last two the upper), but 0x66 has its own access routine that overrides it.
  - **Stick-Y: parameter 0x8A = patch byte 33, the tone balance.**
  The key at 0x4865 points Stick-X at parameter 0xA3 (system byte 17, Key Transpose), shows screen 0x32 and switches Stick-Y off (0xFF).
- **The keys, and P1.3/P1.4** (verified 2026-11-22). The panel's key codes are dispatched at 0x4590 by range: 0x00-0x07 0x460F, 0x08-0x0F 0x466C, 0x10-0x16 0x45D0, 0x18-0x1F and
  0x2C-0x3A through the table at 0x6BFE (0x45C9: code less 0x18, or less 0x28), 0x20-0x23 0x4726, 0x24 0x45F5, 0x25-0x27 0x472D, 0x29-0x2B 0x4731. The test mode's key names
  (a 4-character table from "Bnk1" at 0x6AC5, by code: "Hit any key") name them: 0x00-0x07 Bnk1-8, 0x08-0x0F Nmb1-8, 0x10 `<-`, 0x11 Keym, 0x12 Splt, 0x13 Bal, 0x14 Lton,
  0x15 Uton, 0x16 `->`, 0x18 Dec, 0x19 Inc, 0x1A Writ, 0x1B Tran, 0x1C Exit, 0x1D Undo, 0x1E Copy, 0x1F Rcal, 0x20-0x2B the number pad (0..9, Entr, Sift), 0x30 Tune, 0x31 MIDI,
  0x32 Intr, 0x33 Card, **0x34 Lcal, 0x35 Valu, 0x36 Lowr, 0x37 Uppr**, 0x38 Tran, 0x39 Case (Chase), 0x3A Prta.
  **P1.3 and P1.4 hold which of the four keys 0x34-0x37 was pressed last**: 0x4AC5, 0x4AC9, 0x4AD0 and 0x4AD7 set them to 00, 01, 10, 11. They are
  outputs (start-up's `MOV PM1,#60H` at 0x221F makes only P1.5 and P1.6 inputs), and they very likely light the four keys' indicators through a decoder: the test mode's
  "Hit any key" (0x43C5, screen 0x3C) prints the pressed key's name and, for 0x34-0x37, sets P1.3/P1.4 by the same table (0x43ED: code less 0x34), and the boot code at
  0x0237-0x042E steps them through all four states during its tests. The schematic would settle it.
  **At power-on they are 01, Valu**: start-up's 0x22A7 (from 0x20EC) clears the run-time state and writes `MOV P1,#08H` (0x22C0), every start, whatever was set before.
  No stick descriptor is given a parameter then, and a screen change (0x345C) clears 0xFE54.3, so **after power-on the stick does nothing on the play screen until Lowr or Uppr
  is pressed**; on a screen that gives the descriptor a parameter it sets that value (Valu). Two more paths leave Lowr/Uppr: every screen change runs 0x495F (with P1.4 set:
  P1 to 01, Valu), so leaving the play screen ends the balance mode, and the key at 0x4737 (Copy, 0x1E, by the table) calls Valu's handler.
  **A patch change ends the balance mode too** (verified 2026-11-22). Every way of choosing a patch goes through 0x25C7, which loads it (from internal memory, or the card
  with 0xFE39 bit 6) and, also when the card fails (0x25DA), ends in `MOV R1,#00H; CALL !345CH`: screen 0 again, so 0x495F sets P1 to Valu and 0xFE54.3 is cleared. The ways:
  - a Bnk or Nmb key on the play screen (the tables at 0x461D/0x4621 and 0x467C/0x4680, by screen 0x0B, 0x28, 0x34, 0x00, else: on screen 0, 0x4644 and 0x46A3 set the
    pending bank or number in 0xFE39 and call 0x25B6). 0x25B6 loads the patch only when system byte 12's bit 0 is set (0xC5CC); clear, it only sends the program change (0x551B,
    when Prog.C, 0xC5C9 bit 0, is on: `C0 n` through 0x5544). After a load it sends the program change as well;
  - a MIDI program change (0x4BCC), taken only on screen 0 and with Prog.C on, straight to 0x25C7.
  So after a patch change on a D-50 the stick is idle until Lowr or Uppr is pressed again. (On screens 0x17-0x27, the tone screens, Nmb1-4 instead toggle the partials on and
  off: 0x46AD flips bits of the tones' common byte 46, 0xC4AE and 0xC56E.) What the code does with them:

  | key | handler | P1.4 P1.3 | the stick |
  | --- | --- | --- | --- |
  | 0x34 Lcal | 0x4919 | 0 0 | relative (0x3083), both axes, on what the screen gave them; coming from Lowr/Uppr both are switched off (0xFF), from Valu the start is the current value |
  | 0x35 Valu | 0x4949 | 0 1 | X absolute on what the screen gave it, Y off; coming from Lowr/Uppr both are switched off |
  | 0x36 Lowr | 0x496A | 1 0 | the balances, absolute: X the **lower** tone's partial balance, Y the tone balance |
  | 0x37 Uppr | 0x49A1 | 1 1 | the same with X the **upper** tone's partial balance |

  Lowr and Uppr are taken only on the play screens (0x498A: screen 0xFE45 below 0x28, not 3, 0x0A or 0x0B), and both end at 0x49BE (Lowr branches there): `OR 0FE47H,#0C0H`,
  `MOV 0FE3FH,#0FFH`, which redraw both LCD lines and drop the field position (0xFE47 below). They differ only in P1.3.
- **Each parameter's access routine** (0x3C4F). A table at 0x79BC gives every parameter number a type (+4 of the descriptor). Below 0x3C the type picks a routine from
  the table at 0x3C7C; 0x3C-0x4F go to 0x3CAA (a second table by type, at 0x3CC8), 0x50 and up to 0x3D42. Each routine finds the parameter's RAM byte (DE) and either loads
  the descriptor from it (0x3FE8, when 0xFE54.5 is clear) or stores the descriptor's value into it and sends it on (0x3C9A, 0xFE54.5 set: 0x5595). Two have a type of their own:
  - **0x66, type 6, 0x3EB4: the upper tone's common byte 47 (0xC4AF) when P1.3 is set, the lower's (0xC56F) when clear.** So the stick's X moves the partial balance of
    the tone the last of Lowr/Uppr chose, whatever tone is being edited.
  - 0x8A, type 7, 0x3EC1: patch byte 33 (0xC5A1); on screen 0 it also redraws the play screen's balance (0x41E6, at column 0x3C: the `Bal` field).
  Types 0x00 (parameters 0x10, 0x25, 0x36, 0x65, 0x67, 0x68, 0x90, 0x91, 0xA4-0xA7) and the rest are not decoded here.
- **0xFE47, the LCD's refresh requests**, served by 0x2619 in the main loop (when 0xFE36.2 is set): bit 7 writes the upper line from the screen buffer (0xC030, 40
  characters, LCD address 0x80), bit 6 the lower (0xC058, 0xC0); bits 4 and 3 write a message's two lines (pointers 0xFE26 and 0xFE28: 0x4104 sets bits 4, 3, 2), bit 2 keeps it
  up until 0xFE55's low nibble reaches 0x0F (a timer, by its use) and then redraws both lines; bit 1 rewrites one field at buffer position **0xFE3F** (0..0x27 the upper line,
  0x28..0x4F the lower; 0xFF none); bit 5 0x26C5 (not decoded). **While a message is up (bit 2) the stick is only tracked** (0x30EB).

  (The
  other P1 tests the first sweep listed, 0x1562, 0x4014 and 0x466D, are misaligned decodes inside other instructions.) The routines at 0x4919..0x49C4 are ordinary code; the
  "screen scripts" the first reading saw were those bytes decoded from a wrong start.
- **How it moves them** (verified 2026-11-22; this corrects the first reading, which had the play screens' stick relative). Two ways, by P1.3 and P1.4:
  - **Reading** (0x335A): the new average is averaged again with the last value taken, and a change of less than 2 (of 0..255) is ignored. 0x3346 then trims the ends: less 4
    (0 below), times 33/32, 255 at most, so the stick's full value is reached about 4 short of each end.
  - **Catching** (0x30E7): while the stick is not in use (0xFE54.3 clear, or 0xFE47.2 set: a message on the LCD) the reading is only tracked. In use, a first movement is taken only when it is small
    (less than 4) or at an end (below 8 or 0xF7 and up); once a value has changed (0xFE55.5) every movement is taken. A jump in the reading does not move the parameter.
  - **Absolute, 0x30BD**: X with P1.3 or P1.4 set, Y with P1.4 set (Y is off with only P1.3), or any parameter with a maximum below 16. The value is the stick's position over
    the parameter's range, through **the table at 0x3114**, which is not a curve but a dispatch: `03 7F 64 63` (3 keys: the maxima 127, 100, 99) and four routines, found by
    0x3440 (`CMPMNE`):

    | maximum | routine | value |
    | --- | --- | --- |
    | 127 | 0x3167 | reading / 2 (0..127) |
    | 100 | 0x3160 | reading / 2, then 0x417E: v - v·56/256 (0..127 to 0..100) |
    | 99 | 0x3159 | reading / 2, then 0x418C: v - v·57/256 (to 0..99) |
    | any other | 0x3120 | the reading in max + 1 equal steps of 256 / (max + 1), centred (less half the remainder), at most max; a change is taken only when it is more than half a step (a quarter for a step of 128 and up): hysteresis |

    **So with Lowr or Uppr the stick sets the balances directly and linearly: 0 at one end, about 50 in the middle, 100 at the other**, whatever the patch had.
  - **Relative, 0x3083**: neither bit set (Lcal) and a maximum of 16 or more. The max is halved, the reading quantised by 0x3120 into
    max/2 + 1 steps, and the step less max/4 added to the descriptor's starting value (+5), clamped to 0..max: the stick moves the parameter by up to a quarter of its range
    either way from where it was. The start is set when the screen gives the descriptor (0x3FE8: +1, +3, +5 from the parameter), and, with P1.3 set and P1.4 not, from the
    current value (0x4930); after each store with either bit set (0x3B60).
  A changed value goes through 0x3784 (store, then 0x3B5A: write to the tone, patch or system byte and update the screen).

### The parameter numbers and their ranges (verified: the code at 0x3B7C and the tables)

| numbers | area | RAM | range table | offset |
| --- | --- | --- | --- | --- |
| 0x00-0x36 | a partial | 0xC400 / 0xC4C0 (+0x40 the second partial) | 0x7D20 | number |
| 0x37-0x68 | a tone's common | the same + 0x80 | 0x7D60 | number - 0x37 |
| 0x69-0x91 | the patch | 0xC580 | 0x7DA0 | number - 0x69 (the patch block's byte) |
| 0x92-0xB3 | the system (MIDI, function) | 0xC5C0 | 0x7DE0 | number - 0x92 |

The range tables hold each parameter's maximum (0 minimum). They agree with what the repository already uses (patch bytes 29-38: 3, 31, 100, 100, 100, 2, 100, 100, 16, 16).
The system table, by byte: 0: **100** (Master Tune: 427.5-452.6 Hz in 100 steps, 0.251 Hz each, 50 = 440.0), 1-2: 3, 3-12: switches (the MIDI pages' Bender, Mod, Volume, Hold, Porta,
Prog.C, Exclu, ...: 0xC5C3-0xC5CC, as the parser tests them), 13: 15 (the basic channel), 14: 2, 15-16: 31 (the two assignable controllers, CC 64 + value), **17: 24 (Key Transpose,
-12..+12 around 12)**, 18: 127, ... The names of bytes 1-2, 14 and 18 on are still to place.

### What this means for the FM-1

- The D-50's stick: **X = partial balance (of the selected tone), Y = tone balance.** #43 had K1 as the tone balance and K2 as the partial balance of both tones; the
  maintainer chose to follow the D-50 (#58): **K1 is now X (the selected tone's partial balance), K2 Y (the tone balance), and K4 selects the tone** (the D-50's UPPER/LOWER
  keys; the Upper at start). CC82 follows K1 and CC81 K2.
- With Lowr or Uppr (on a play screen) the D-50's stick sets the balances absolutely and linearly over 0..100, as CC81/CC82 do (0..127 over 0..100): those two follow the D-50. K1 and K2 are
  encoders without a position, so they step the balances from where they are; that is ours. The D-50 ignores a jump in the reading (0x30E7); MIDI CCs are taken as they come.
- The D-50's stick moves the balances only after Lowr or Uppr, and leaves them on a screen change or a patch change; at power-on it is in Valu. The FM-1's K1/K2 move them on the play
  screen at any time, and K4's Upper at start matches the D-50's power-on P1.3 (set: 0x3EB4 would take the upper). Kept as ours: the FM-1 has no Lcal/Valu.
- Master Tune is 0..100, not 0..126 as PR #56 first had it: corrected (`d50_tune` 0..100, 50 = 440.0 Hz). Key Transpose's -12..+12 is confirmed.

## Coverage and what is next

First recursive descent from reset, the vectors and the two tables: 6 122 instructions in 367 functions, about 14.8 KB of the 56 KB above 0x2000/0x80 that are code and data
together. The rest is reached by indirect jumps and by handlers installed into the RAM vectors at run time, which the descent does not follow. To do, in this order:
1. (Done for the serial interrupt: it is at 0x4ADF.) Find where the other RAM vectors are rewritten (the writes into 0xDEC0-0xDEFF are by index, not by absolute address: look for loops over `[DE]`/`[HL+B]`), and add those handlers as entry points.
2. (Done: SysEx, 0x4FC6.) Still open: the channel and omni logic in full (the MIDI page's bytes at 0xC5C0..0xC5D0; 0x4B81 shows omni as 0xC5CB bit 0), the reverb data's
   layout at 03 60 00, and screen 0x05 (the RAM writes at 00 40 00: a service mode?).
3. (Done: the A/D path, the stick's targets, the table at 0x3114, the key codes, and P1.3/P1.4 as the last of the keys Lcal, Valu, Lowr, Uppr.) (Done: 0x3EB4 is parameter
   0x66's access routine, Lowr/Uppr choose X's tone; 0xFE47/0xFE3F are LCD refresh requests.) (Done: power-on is Valu, 01.)
   Still open: whether P1.3/P1.4 drive indicators (the schematic), and what the four key names mean on the real panel (to check against the owner's manual).
4. The function menu and the tune/transpose parameters: ranges and steps (issue #57).
5. Name things: write `tools/d50_dasm/names.txt` (address, name) and have analyze.py use it, so the listing becomes readable.
