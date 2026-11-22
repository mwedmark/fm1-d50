# Installing the D-50 on an FM-1

There are three things you can do, from easiest to most work:

1. **Try the D-50 in your browser**, with no FM-1 at all.
2. **Put the D-50 firmware on your FM-1** from the install page. It starts, but plays nothing until the D-50's data is there.
3. **Put the firmware and the D-50's data on your FM-1.** This needs a D-50 of your own and a build on your computer.

> **The D-50's sound is Roland's.** Its PCM wave ROM and its patch banks are not in this repository, not on the install page and not in any
> release, and never will be. You make the data from your own D-50 (the ROM dumps and the SysEx bank dumps you read out of it).

## 1. In the browser

Open <https://mwedmark.github.io/fm1-d50/>. It is the firmware running in an emulator. Switch it on, then press **Load D-50 data** and pick
your own `d50_fm1base.fmd5` (made in step 3, part A). The data stays in your browser; nothing is uploaded. Without it the screen says
`NO D-50 DATA`.

## 2. The firmware, from the install page

You need an M-VAVE FM-1, a USB cable straight to the computer (no hub), and Chrome or Edge.

1. Open <https://mwedmark.github.io/fm1-d50/install/>.
2. Connect the FM-1 and press **Install**. The browser asks once to allow MIDI: allow it. Do not unplug the cable while it writes.
3. The FM-1 restarts as **D-50 FM-1** (the name of its USB port).

The page checks that the package is the one it was built for, and the FM-1 checks the package again before it writes anything. The page
installs the firmware only: **it does not write the D-50's data**, because the package on the page was built without it. Without the data
the FM-1 shows `NO D-50 DATA`. To change the firmware later, install again: the data that is already in the FM-1 is kept.

**Back to M-VAVE's firmware:** the page has "Back to the stock firmware" at the bottom (you download M-VAVE's `FM-1.fwsc` yourself).

## 3. The firmware and the D-50's data

The data is written into the FM-1 together with a firmware that has been built to expect exactly that data, so for now this step is a
build on your own computer. It takes some time, but you do it once.

### What you need

- A D-50's **PCM ROMs and program EPROM**, dumped, and **one or more bank dumps** (`.syx`, a D-50 bulk dump with 64 patches each, up to six).
  [third_party/picofaced5/tools/d5_extract/README.md](third_party/picofaced5/tools/d5_extract/README.md) lists the chips and their CRC-32s.
- Python 3 with Pillow, Node.js, `git`.
- JieLi's toolchain, which fm1-base fetches. **Linux** runs it natively (also WSL on Windows); **macOS** runs it in Docker or podman.
- For the install: Python with `mido` and `python-rtmidi` (`pip install mido python-rtmidi`).

### A. Make the data from your own ROMs

From the root of this repository:

```bash
python3 third_party/picofaced5/tools/d5_extract/d5_make_blob.py YOUR_ROM_DIR roms/gen
node tools/d50_pack.js roms/gen/d5_pcm.bin roms/out/d50_fm1base.fmd5 --bank bank1.syx --bank bank2.syx
```

The first command writes `roms/gen/d5_pcm.bin` (the 512 KB of samples) and `d5_pcm_table.h`. The second packs them with up to six banks into
`roms/out/d50_fm1base.fmd5`. Both folders are ignored by git; keep it so.

### B. Build the firmware with the data in it

```bash
bash tools/ci/fetch_platform.sh ../fm1-base            # fm1-base and felucca-bsp at the commits this firmware needs, with its flash layout
cd ../fm1-base && tools/get_toolchain.sh               # JieLi's toolchain (fm1-base's README: the pinned way is tools/setup_toolchain.sh)
python3 tools/build.py ../fm1-d50/fm1base --no-ble
```

It makes `fm1base/build/d50.fwsc` (the firmware) and `fm1base/build/d50-res.bin` (your data, in the form the FM-1 keeps it). The `.fwsc` has
your data in its package too, which makes it **for your own use only: never share it or put it on the web.**

### C. Install it

With the FM-1 connected, from `../fm1-base`:

```bash
python3 tools/fm1_install.py ../fm1-d50/fm1base/build/d50.fwsc --yes
python3 tools/fm1_resources.py ../fm1-d50/fm1base/build/d50-res.bin --port "D-50 FM-1"
```

The first writes the firmware (and, when the update loader can, the data with it); the FM-1 restarts as **D-50 FM-1**. The second writes the
data and says so if it is there already. When both are done the FM-1 shows the play screen with patch 1-11, and the keys play.

## If something goes wrong

| What you see | What it means |
| --- | --- |
| `NO D-50 DATA` on the screen | The firmware is there, the data is not (or is another build's). Do step 3, part C again. |
| The install page does not find the FM-1 | Use Chrome or Edge, allow MIDI, connect the cable straight to the computer, and close other programs that hold the MIDI port (a DAW, M-UPGRADE). |
| The FM-1 does not start (twice, then `UBOOT`) | Only felucca-bsp's `fm1_rescue` brings it back, see [FM1-BASE.md](FM1-BASE.md) and [rescue_tools/](rescue_tools/). |
| The sound drops out | The firmware runs the FM-1 at 396 MHz; after two failed starts it falls back to 360 MHz. Try again, and tell us the patch: <https://github.com/mwedmark/fm1-d50/issues>. |

Installing another fm1-base firmware afterwards uses the same flash area and **overwrites the D-50's data**: install the D-50 data again.

## Why not a one-click install with the data?

The FM-1 accepts data only of the length and checksum its firmware was built for, and the data is yours, not ours. A page that takes your own
`.fmd5` and writes it into the FM-1 would need the firmware to accept any D-50 data. That is not done yet; see
[the issues](https://github.com/mwedmark/fm1-d50/issues).
