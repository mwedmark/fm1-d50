# Handoff: K1/K2 joystick test and SEL as TUNE/FUNCTION (fm1-d50)

Run this in a LOCAL Claude Code session on the maintainer's desktop (FM-1 on USB, own D-50 data and firmware dump available).
The cloud session could not reach the desktop (Remote Control listed `MagnusDesktop` folders as "not available"; `fm1-d50` never appeared),
so nothing has been changed, built or tested. Delete this file when done (it is only a handoff, remove it before merging anything).

## Start by reading
README.md, D50.md (history), FM1-BASE.md, issue #43 (https://github.com/mwedmark/fm1-d50/issues/43).

## State (main at ab0009c; the joystick work is PR #44, merged)
- K1 and K2 are the D-50's joystick on the play screen (`fm1base/app.c`, `joystick()` and `patch_edit()`).
  - K1: Upper/Lower tone balance (patch block byte 33, LCD `Bal`).
  - K2: partial balance (each tone's common byte 47, written to both tones).
  - Both are edits of the current patch through the DT1 path (issue #31). The LCD dot height moved to K3.
  - Engine: `Voice::set_partial_balance`, called from `Tone::reconfigure`, so K2 is heard on held notes.
- Only built (CI green) and run in fm1-base's host simulator with synthetic data. Nobody has heard it.
- SEL (`B_SEL` in fm1-base `include/plat.h`) is not read by the app. `EN_SELECT` / `EN_PRESET` still step patches.

## To do
1. Test on the FM-1. Install CI's `d50.fwsc` artifact, or build with fm1-base: `python3 tools/build.py ../fm1-d50/fm1base --no-ble`
   in an fm1-base checkout on branch `d50-layout` (pinned commits: `tools/ci/fetch_platform.sh`).
   Play a Dual or Split patch: does K1 move the tone balance audibly? Does K2 move the partial balance, also on held notes?
   Whole mode: K1 must do nothing audible. Check that held notes do not click when K2 turns. Report what you hear; fix what is wrong.
2. SEL as TUNE/FUNCTION. From the firmware dump (D-50 v1.06) find the tune and function pages: texts, parameters, ranges, order,
   what each press does. The edit pages of #31 took their texts from it (`fm1base/d50_lcd.h`, `lcd_pages`). Make SEL open those pages,
   as EDIT steps through the edit pages (`B_EDIT` in `fm1base/app.c`, `ui.page`). Keep it small: first what the engine already does
   (Master Tune, Key Transpose; check `d50.h` and the engine). The repo has no TUNE/FUNCTION information; the dump is the only source.
3. Update README.md's panel table (add SEL). Decide with the maintainer whether the two balances also get a MIDI CC (as CC80 is for Chase Play);
   suggestion: yes, and put the numbers in the MIDI table.

## Notes
- Never commit or push D-50 data (PCM ROM, banks, firmware dump). `roms/` and `D50 files/` are in .gitignore.
- Host simulator (`tools/build.py fm1base --host`, clang) cannot run the firmware as is: `d50_render` reads the hardware timer at 0x10804
  (`D50_T4` in `d50/d50.cpp`).
- Open a pull request when done (draft); CI builds the firmware and the emulator.
