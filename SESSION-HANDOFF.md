# Överlämning: Data Transfer-menyn (Bulk Dump) – test mot D50 Librarian

Klistra in hela filen som första meddelande i en ny Claude Desktop-session på MagnusDesktop, i en lokal klon av `mwedmark/fm1-d50`.

## Mål
D50 Librarian v1.0.25.0 måste få synthen att börja en download (D-50:ans Data Transfer-meny). FM-1:an kunde bara svara på RQ1/RQD. Nu kan den starta själv. Testa det mot riktiga librarianen och rätta det som går fel.

## Läge
- Gren: `claude/friendly-bell-0kodfz`, draft-PR [mwedmark/fm1-d50#71](https://github.com/mwedmark/fm1-d50/pull/71). Senaste commit `2455def`.
- CI (firmware, emulator) är grön på `2455def`. Firmware-paketet `d50.fwsc` finns som artefakt i körning https://github.com/mwedmark/fm1-d50/actions/runs/45171787716 (inga Roland-data i paketet; FM-1:an behåller sin region).
- Engine-delen är testad på värden med `tools/d50_host/test_sysex.cpp` (201 kontroller gröna, syntetisk bank). Själva menysidan (`fm1base/app.c`) är bara byggd, aldrig körd på en FM-1.
- Inget är provat mot en riktig librarian. README: handskakningen är "not yet tried against a real D-50 or a librarian".

## Vad som byggts
- `d50/d50.cpp`, `d50/d50.h`: `d50_transfer(item)`, `d50_transfer_state()`, `d50_transfer_stop()`. Items: `D50_XFER_DUMP` (WSD, sedan DAT à 256 byte på varje ACK, sedan EOD), `_LOAD` (RQD, tar emot WSD/DAT/EOD), `_DUMP_O` (DT1 à 256 enkelriktat), `_LOAD_O` (inget skickas).
- Dumpen är alltid D-50:ans hela storlek `02 0F 00` (0x8780 = 34 688 byte) när banken är hel. Saknas reverbdata skickas den delen som nollor (`value_at`).
- `fm1base/d50_lcd.h`, `fm1base/app.c`: ny EDIT-sida efter MIDI Channel. K1 väljer post, SAVE startar (som D-50:ans Enter, sparar inte inställningar på den sidan), att lämna sidan avbryter. Skärmen säger "Complete ." eller "Error Reject".
- README och `docs/D50-FIRMWARE.md` beskriver det.

## Testplan
1. Installera `d50.fwsc` på FM-1:an (INSTALL.md).
2. FM-1:ans BasicCH 1 = device ID `00`. Samma i librarianen.
3. Starta mottagning i D50 Librarian, gå sedan på FM-1:an: EDIT till Data Transfer, K1 = Bulk Dump, SAVE.
4. Logga trafiken (MIDI-OX eller liknande) om det inte går.
5. Om handskakningen strular: prova Bulk Dump.O (enkelriktat).
6. Provar även: librarianens upload till FM-1:an (Bulk Load / Bulk Load.O), och att läsa tillbaka.

## Vad att titta efter
- Svarar librarianen ACK på WSD (`F0 41 00 14 40 02 00 00 02 0F 00 6D F7`; `0x8780`-storleken, kontrollsumman kan jag ha räknat för hand)?
- Avvisar den storleken, eller tar den emot alla 136 DAT (34 688 / 256)? Skickar den ACK efter varje?
- Vad säger FM-1:ans skärm vid slutet: Complete eller Error Reject? Vid fel: vilket felnummer?
- Reverbdatan är nollor om ingen dump lästs in: en riktig D-50 som får tillbaka filen får nollor i reverbtyp 17–32. FM-1:ans egen reverb påverkas inte.

## Kända skillnader mot D-50:an
- Ingen Tran-knapp: en EDIT-sida i stället. Inga kortposter.
- FM-1:an tar emot SysEx när som helst, inte bara på menysidan.
- Kontrollsumma fel: FM-1:an skriver ingenting, D-50:an har redan skrivit.
- Kodfilerna har CRLF-radslut. Behåll dem (Python-skript som läser/skriver text förstör dem om man inte använder `newline=''`).

## Filer
`d50/d50.cpp` (SysEx-delen ~rad 806 och framåt, `d50_transfer` sist), `d50/d50.h`, `fm1base/app.c` (`transfer_start`, `transfer_stop`, `page_edit`, `app_frame`), `fm1base/d50_lcd.h` (`lcd_transfer_screen`), `tools/d50_host/test_sysex.cpp`, `docs/D50-FIRMWARE.md` (Data Transfer-menyn, tabellen vid 0x3822).
