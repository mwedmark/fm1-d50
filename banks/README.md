# Banks the Pages site hosts

Every `*.fm1b` file in this folder, and in `soundfonts/`, is built into the emulator site (`.github/make_site.js`): it is laid in the emulator's flash as `<name>.flash`
and listed on the MIDI player page. **`default.fm1b` is selected when the site starts**; the sine test bank is always last in the list.

- FM1B banks: plain (version 1) or MP3-packed (version 2, from `pack_bank.js`; the site runs the live-cache firmware, which plays both). The firmware is the 44.1 kHz build: a packed bank must be at 44.1 kHz.
- At most 1 MB (the emulator takes 256 flash sectors).
- Everything in this folder is published on the public site. Add only a bank you may redistribute.

## The banks here

- `default.fm1b` (since 2026-11-18; until then `gm_lossless.fm1b`): `1mgm.sf2` at 44.1 kHz without MP3 (no cache): `make_bank.js bfp8 44100 soundfonts/1mgm.sf2`, then
  `pack_bank.js --mp3 none --min-bits 5 --drop-first 0`. Every sound is in it at full rate; 53 of 165 have fewer mantissa bits (28 at 7, 24 at 6, one at 5),
  so it needs the firmware of 2026-11-17 or later (codec 4). Heard on an FM-1 on 2026-11-18: it sounds good. The install page's starter bank.
- `gm_mp3.fm1b`: the default bank until 2026-11-18 (v1.0.0's `gm_44k_fill.fm1b`): the same SoundFont, BFP8 with 16 sounds as MP3 (the live cache),
  `pack_bank.js bank.fm1b banks/gm_mp3.fm1b --fill 1`.

## Made by the workflow instead of committed

The `bank` job of `.github/workflows/firmware.yml` can make `default.fm1b` itself: it takes the SoundFont `soundfonts/1mgm.sf2` of this repo (its result is the site's default bank) (or, if that is not there, fetches one from the repository secret `SF2_URL`:
Settings > Secrets and variables > Actions), converts it with `make_bank.js <format> 32000` (format: the repository variable `BANK_FORMAT`, or `bfp8`;
"Run workflow" takes it as an input too), stores it as the artifact `bank`, and the site build puts it in this folder before building. A bank made
that way replaces a committed `default.fm1b`. Without the secret nothing is made.

`default.fm1b` is committed: the lossless General MIDI bank above (the recipe is there). The `bank` job of the workflow makes a plain 32 kHz bank, which this firmware plays but which would replace this one: leave `SF2_URL` unset.
The SoundFont `soundfonts/1mgm.sf2` itself is not in the repository (it stays on the maintainer's computer; its header names E-mu Systems, 1993);
the tests that need it run only where the file is. The bank is also attached to every release as `gm_44k_lossless.fm1b` (v1.0.0 had the MP3 bank, `gm_44k_fill.fm1b`).
