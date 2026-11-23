# fm1-d50: a Roland D-50 on the M-VAVE FM-1

fm1-d50 turns the M-VAVE FM-1 into a Roland D-50, the 1987 LA synthesizer. It plays the D-50's own patches with the D-50's partial structure,
PCM waves and effects, from the FM-1's keys or over MIDI.

- The sound is [PicoFaceD5](third_party/picofaced5)'s D-50 engine.
- The platform under it is [fm1-base](https://github.com/charlesvestal/fm1-base).

![The D-50's play screen on the FM-1](docs/img/lcd_true_green.png)

It is a work in progress. This README describes what works now and will grow with it.

## What works

- **The D-50's sound:**
  - 16 voices at 32 kHz, resampled to the FM-1's 44.1 kHz.
  - Every key mode (Whole, Dual, Split, Separate and their Solo variants), with the D-50's reverb, chorus and EQ.
  - The CPU runs at 396 MHz. A guard fades a voice out before the audio would drop out.
- **Six banks, 384 patches:** the factory card and the PN-D50 card library.
- **Playing:**
  - **Velocity**, from the FM-1's keys.
  - **Aftertouch**, from how hard a held key is pressed.
  - The **bender** and the **modulation**, from MIDI.
  - **Portamento** and the **hold pedal**, with the patch's Portamento Mode and Hold Mode (U, L or both) as on a D-50: in the split and
    separate modes the pedal holds only the tones Hold Mode names.
- **Chase Play:** the D-50's echo of a note from one tone to the other.
- **SysEx:** the D-50's DT1 and RQ1 for the current patch and the bank in RAM, so D-50 editors and librarians can read and send patches.
- **The D-50's display:** the LCD of the original, its play screen as a real D-50 shows it.
  - It comes in two views and three colours, described below.

## The screen

HOME steps through three views:

1. **The FM-1 screen**, with the patch, its name and the load. Later this becomes a view that uses the FM-1's screen for the D-50 in its own way.
2. **The LCD at true size:** 2 lines of 40 characters, one pixel a dot. K3 sets the dot height.
3. **The LCD folded:** each line split in two at column 24, at a size that is easy to read.

![The folded view](docs/img/lcd_folded_green.png)

Hold HOME for 3 seconds to step through the LCD's colours:
- **Green:** an ordinary LCD.
- **Amber** and **blue:** the backlights of real D-50s.

![Amber](docs/img/lcd_folded_amber.png)
![Blue](docs/img/lcd_folded_blue.png)

The view and the colour are saved with SAVE.

## The panel

| Control | What it does |
| --- | --- |
| Keys | Play; F3 to G5, with velocity and aftertouch |
| OCT − / OCT + | Octave, −2 to +2 |
| SELECT / PRESET encoders | The patch, through all banks: 1-11 to 6-88 |
| ARP | The D-50's CHASE button: Chase Play on or off |
| HOME | The view; held 3 s, the LCD's colour |
| K1 | The joystick's X, as on a D-50: the selected tone's partial balance, partial 1 against partial 2 |
| K2 | The joystick's Y, as on a D-50: the tone balance, the Upper against the Lower tone (the LCD's Bal) |
| K3 | The LCD's dot height (true-size view) |
| K4 | The selected tone for K1, as the D-50's UPPER and LOWER keys: right the Lower, left the Upper (the Upper at start) |
| SEL | The D-50's TUNE/FUNCTION page, and back: K1 Master Tune (427.5 to 452.6 Hz in 100 steps, 440.0 at start), K2 Key Transpose (−12 to +12): the FM-1's keys only, as on a D-50; notes from MIDI are not moved. Not kept over a power cycle |
| EDIT | The edit pages in turn (Output Mode, Chase, MIDI Channel: BasicCH, Omni, Control), then the play screen |
| SAVE | Saves the patch, bank, octave, view, colour and the MIDI settings |

The joystick's balances are edits of the current patch, as the edit pages' are: they last until another patch is chosen. On an edit page (EDIT), K1 to K4 are that page's parameters instead.

## MIDI

It receives over USB. The **MIDI Channel** page (EDIT) has the D-50's two settings, kept with SAVE:

- **BasicCH** (K1), 1 to 16, default 1: the channel it plays on with omni off, and its SysEx device ID, as on a D-50 (the device ID is the
  basic channel less one: channel 1 is `00`).
- **Omni** (K2), default ON: ON plays every channel, as before; OFF only the basic channel.
- **Control** (K3), the D-50's MIDI-1 setting, default B.CH: the channel the program change, aftertouch and controllers come on in the
  mono mode (below). B.CH is the basic channel and G.CH the global channel, the one below it. MdeOff ignores Mono and Poly, and choosing it
  ends the mono mode.

**The mono mode** (CC 126 on the basic channel, with omni off), as on a D-50: the value is the number of channels, from the basic channel
up (0 and 9 to 16 give 8, and no further than channel 16). Each channel is a monophonic part of its own, with its own bender, whatever the
patch's key mode is: a new note on a channel ends that channel's last one. It is for a MIDI guitar, a channel a string, or a sequencer
part a channel. With omni on, Mono plays polyphonically. **CC 127 (Poly)** ends it; from then on, the Solo key modes (Whole-S, Dual-S,
Split-US and so on) play polyphonically too, as on a D-50, until Mono or MdeOff. Each turns the notes off. The mode is not kept over a
power cycle (a D-50's is not either); the screen says MONO or POLY.

**The Separate key modes** (SEP and SEP-S), as on a D-50: the FM-1's keys and the basic channel play the patch's Lower tone,
polyphonically. The patch's own SepCH (a patch setting, OFF or 1 to 16) plays the Upper: polyphonically in SEP, one note at a time in SEP-S. Each
tone has its own bender, modulation, aftertouch, portamento and hold pedal. SepCH takes notes, the bender, aftertouch and CC 1, 5, 6, 64, 65,
98 to 101 and 123, but no volume and no program change. As on a D-50, a message on SepCH also goes through the basic channel's test, so with
omni on (or SepCH the basic channel) it plays both tones. With SepCH OFF only the Lower sounds. None of the six banks has such a patch;
a librarian can make one.

| Message | What it does |
| --- | --- |
| Note on and off | Play, with velocity |
| Program change | The patch, 0 to 63, in the bank CC0 chose |
| CC0 | The bank for the next program change, 0 to 5 |
| Pitch bend | The bender, over the patch's range or RPN 0's |
| Channel pressure | Aftertouch |
| CC1 | Modulation |
| CC5, CC65 | Portamento time and switch |
| CC7 | Volume |
| CC64 | Hold pedal |
| CC80 | Chase Play on (64 and up) or off |
| CC81 | The joystick's tone balance (K2, Y): 0 to 127 over 0 to 100, the Upper against the Lower tone. Ours: the D-50 has no controller for it |
| CC82 | The joystick's partial balance (K1, X): 0 to 127 over 0 to 100, of the selected tone (K4). Ours |
| CC100, CC101, CC6 | RPN 0: the bender range |
| CC120, CC123 | All sound off, all notes off |
| CC124, CC125 | Omni off, omni on, as on a D-50: only on the basic channel and with the value 0. They set the MIDI Channel page's Omni (kept with SAVE) and turn the notes off |
| CC126, CC127 | Mono (the value: the channels) and Poly (value 0), as on a D-50: only on the basic channel, not with Control MdeOff (above) |
| SysEx DT1, RQ1 | Roland, model 14h: the temporary area at 00 00 00 and the internal memory at 02 00 00 (below) |

## SysEx

**The D-50's own** (`F0 41 dev 14 ...`, Roland, model 14h), both ways the D-50's Data Transfer menu has: one-way ("Bulk Dump.O",
"Bulk Load.O") and with the handshake ("Bulk Dump", "Bulk Load").

One-way:

| Message | What it does |
| --- | --- |
| DT1 (`12`) | Writes data at an address; the checksum is checked. |
| RQ1 (`11`) | Reads data from an address; the FM-1 answers with DT1s of up to 256 bytes, one at a time as the computer reads. |

With the handshake (Roland's protocol of the time; not yet tried against a real D-50 or a librarian):

| Message | What the FM-1 does |
| --- | --- |
| WSD (`40`, address, size) | Answers ACK: the computer may send. |
| DAT (`42`, address, data) | Writes the data as a DT1 does and answers ACK; ERR when the checksum is wrong (the computer sends it again). |
| EOD (`45`) | Answers ACK: the transfer is done. |
| RQD (`41`, address, size) | Sends the data as DATs of up to 256 bytes, each after the computer's ACK of the one before, then EOD; RJC when nothing is at that address. |
| ACK (`43`), ERR (`4E`), RJC (`4F`) | From the computer: the next DAT, the last one again, or stop. |

Without D-50 data on the FM-1, WSD and RQD are answered with RJC.

| Address | What it is |
| --- | --- |
| `00 00 00` | The temporary area: the current patch's 448 bytes (upper partial 1 and 2, common, lower partial 1 and 2, common, patch; 64 bytes each). A DT1 or DAT here changes the sound at once. A byte above its parameter's maximum is skipped, as on a D-50 (the rest of the message is written). |
| `02 00 00` | The internal memory: the bank in RAM, 448 bytes a patch (64 patches, up to `03 60 00`). A power cycle brings back the bank from the flash. |
| Anything else | For example the reverb data at `03 60 00`: taken and ignored, so a whole bulk dump goes through. |

The FM-1 takes and answers only the D-50 messages for its own device ID (the basic channel less one, the MIDI Channel page); it answers
with that ID. A faulty message shows its error on the screen (wrong format, checksum or address). Not yet: the system area, memory
protect ([issue 51](https://github.com/mwedmark/fm1-d50/issues/51)).

**For measuring** (`F0 7D 'S' 'F' 02 ... F7`, six bytes):

| Command | What it does |
| --- | --- |
| `0B` | Reads the load: where the time goes, the CPU load and its peak, the voices, the cores and the missed audio blocks. |
| `0C bits` | Leaves out the reverb (1), the chorus (2) or the EQ (4), to see what each costs. |

**From the platform** ([fm1-base](https://github.com/charlesvestal/fm1-base)): the update of the firmware and of the D-50 data, backup and restore, and the companion's services. The
[install page](https://mwedmark.github.io/fm1-d50/install/) and fm1-base's tools use them; music software does not need them.

## The D-50 data

The D-50's sound needs its PCM wave ROM, and its patches come from bank dumps (`.syx`). Both are Roland's, so **they are not in this repository and
are never distributed.** Anyone who builds fm1-d50 makes the data from their own ROM and their own banks:

1. `third_party/picofaced5/tools/d5_extract/d5_make_blob.py` makes `d5_pcm.bin` from the ROM.
2. `tools/d50_pack.js` packs it with up to six banks.

```bash
node tools/d50_pack.js roms/gen/d5_pcm.bin roms/out/d50_fm1base.fmd5 --bank bank1.syx --bank bank2.syx
```

A firmware package built with the data is for your own use only.

## Licence

The firmware is GPL-3.0. The engine is [PicoFaceD5](https://github.com/Michi71/PicoVintageSynthCollection) by Michi71, GPL-3.0, used with
its author's agreement ([Michi71/PicoVintageSynthCollection#194](https://github.com/Michi71/PicoVintageSynthCollection/issues/194)).
The platform, [fm1-base](https://github.com/charlesvestal/fm1-base) with felucca-bsp, is MPL-2.0. The fm1-base build links no closed
library: Bluetooth, which would need JieLi's closed libraries, is off. The D-50's data is Roland's and is never part of the firmware or
of this repository.

## Building and installing

**[INSTALL.md](INSTALL.md) is the guide for anyone else:** the emulator in the browser, the firmware from the
[install page](https://mwedmark.github.io/fm1-d50/install/), and the firmware with your own D-50 data.

The firmware is built with fm1-base's tools from the `fm1base/` folder and installed over USB with fm1-base's installer. The steps, the flash
layout and the measurements are in [FM1-BASE.md](FM1-BASE.md).

## More

- [D50.md](D50.md): the D-50 as the target, and the plan
- [RESEARCH.md](RESEARCH.md): what was found out about the D-50, with sources
- [FM1-BASE.md](FM1-BASE.md): fm1-base, the build, and the load of every patch
- [Issues](https://github.com/mwedmark/fm1-d50/issues): what is being worked on

## What is where

- `fm1base/`: the app on fm1-base (the panel, the screen, MIDI, the resampler), its `fm1.toml` and the emulator page (`web/emu/`).
- `d50/`: the glue between the firmware and the engine (`d50.h`, `d50.cpp`), the bank's unpacking, the libm and std headers the toolchain
  lacks, and the 32 to 44.1 kHz resampling filter (`tools/make_resampler.js` writes it).
- `third_party/picofaced5/`: the engine.
- `tools/`: `d50_pack.js` (the data), `d50_host/` (the engine on a PC: renders and tests), `ci/` (the GitHub build).
- `rescue_tools/`: stock firmware back onto an FM-1 that does not start (`fm1_rescue.py`, with M-VAVE's V15, `FM-1_015.fwsc`).

The repository began as a copy of [fm1-sf2](https://github.com/mwedmark/fm1-sf2), the FM-1's SoundFont player, and the D-50 first ran on
M-VAVE's fm1_open SDK there. That build, the SF2 player and its pages and tools were removed when the D-50 moved to fm1-base; `D50.md` and
`FM1-BASE.md` still describe them where they tell the history.
