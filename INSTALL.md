# Installing the D-50 on an FM-1

There are two ways to use the D-50 firmware, and both start by making the D-50's data from your own D-50:

1. **In your browser**, with no FM-1 at all.
2. **On your FM-1**, from the install page: the firmware and your data in one go.

> **The D-50's sound is Roland's.** Its PCM wave ROM and its patch banks are not in this repository, not on the install page and not in any
> release, and never will be. You make the data from your own D-50 (the ROM dumps and the SysEx bank dumps you read out of it), on your own
> computer. The install page reads your file in your browser and writes it to your FM-1; nothing is uploaded.

## Step 1: make your data file

You need:

- Your D-50's **PCM ROMs and program EPROM**, dumped, and **one or more bank dumps** (`.syx`, a D-50 bulk dump of 64 patches each; up to six).
  [third_party/picofaced5/tools/d5_extract/README.md](third_party/picofaced5/tools/d5_extract/README.md) lists the chips and their CRC-32s.
- Python 3 and Node.js, and a checkout of this repository (`git clone https://github.com/mwedmark/fm1-d50`).

From the root of the checkout:

```bash
python3 third_party/picofaced5/tools/d5_extract/d5_make_blob.py YOUR_ROM_DIR roms/gen
node tools/d50_pack.js roms/gen/d5_pcm.bin roms/out/d50_fm1base.fmd5 --bank bank1.syx --bank bank2.syx
```

The first command writes `roms/gen/d5_pcm.bin` (the 512 KB of samples). The second packs them with your banks into `roms/out/d50_fm1base.fmd5`:
**that is your data file.** Both folders are ignored by git; keep it so, and never share the file.

## Step 2a: try it in the browser

Open <https://mwedmark.github.io/fm1-d50/>. It is the firmware running in an emulator. Switch it on, press **Load D-50 data** and pick your
`d50_fm1base.fmd5`. The data stays in your browser. Without it the screen says `NO D-50 DATA`.

## Step 2b: put it on your FM-1

You need an M-VAVE FM-1, a USB cable straight to the computer (no hub), and Chrome or Edge.

1. Open <https://mwedmark.github.io/fm1-d50/install/>.
2. Under **Your D-50 data**, press **Choose your file** and pick your `d50_fm1base.fmd5`. The page checks that it is D-50
   data and says how big it is.
3. Connect the FM-1 and press **Install**. The browser asks once to allow MIDI: allow it. **Do not unplug the cable while it writes.**
   The page writes the firmware, the FM-1 restarts as **D-50 FM-1**, and the page then writes your data (the screen says `RESOURCES`).
4. The FM-1 restarts once more, and shows the play screen with patch 1-11. The keys play.

Good to know:

- **Only the data, or only the firmware.** If the firmware is already there, choose your file and press **Write only the data**; the FM-1 then
  restarts by itself to start on it. To update the firmware alone, install without choosing a file: the data that is in the FM-1 is kept.
- **New data over old.** Writing a different file replaces the data; writing the same file again does nothing.
- **Back to M-VAVE's firmware:** the page has "Back to the stock firmware" at the bottom (you download M-VAVE's `FM-1.fwsc` yourself).

## If something goes wrong

| What you see | What it means |
| --- | --- |
| `NO D-50 DATA` on the screen | The firmware is there, the data is not. Choose your file on the install page and press **Write only the data**. |
| The page says "That file is not D-50 data" | It is not the `.fmd5` that `tools/d50_pack.js` makes. Check step 1. |
| The page says the file is too big | The region holds 589 792 bytes; your file is larger than that. Use fewer banks. |
| The install page does not find the FM-1 | Use Chrome or Edge, allow MIDI, connect the cable straight to the computer, and close other programs that hold the MIDI port (a DAW, M-UPGRADE). |
| "the FM-1 is playing" | Stop the sound (release the keys, stop the MIDI source), then write again. |
| The FM-1 does not start (twice, then `UBOOT`) | Only felucca-bsp's `fm1_rescue` brings it back, see [FM1-BASE.md](FM1-BASE.md) and [rescue_tools/](rescue_tools/). |
| The sound drops out | The firmware runs the FM-1 at 396 MHz; after two failed starts it falls back to 360 MHz. Try again, and tell us the patch: <https://github.com/mwedmark/fm1-d50/issues>. |

Installing another fm1-base firmware afterwards uses the same flash area and **overwrites the D-50's data**: write it again from the page.

## Building the firmware yourself

Not needed for the above. If you want to build the firmware (it takes JieLi's toolchain, which runs natively on Linux, in WSL, or in Docker on
macOS), the steps are in [FM1-BASE.md](FM1-BASE.md): `bash tools/ci/fetch_platform.sh ../fm1-base`, then `python3 tools/build.py
../fm1-d50/fm1base --no-ble` in `../fm1-base`. With your data file at `roms/out/d50_fm1base.fmd5` the package carries it too, which makes
the package **for your own use only: never share it or put it on the web.** Install it with `tools/fm1_install.py` (and
`tools/fm1_resources.py` for the data) from fm1-base.
