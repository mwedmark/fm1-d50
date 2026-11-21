// SPDX-License-Identifier: GPL-3.0-or-later
// The D-50 emulator's own part of the page (issue #40): "Load D-50 data", your own d50_fm1base.fmd5 (tools/d50_pack.js from your own ROM
// and banks, README.md) read in this browser and written into the emulator's data area as the FM-1's resource region holds it (fm1-base
// device/res.c: the commit record "RES1", the payload's length and CRC-32, the record's own CRC; then the payload at APP_RES_BASE + 32).
// fm1-base's "data" part (fm1.toml emu = ["data"]) keeps that area in this browser's IndexedDB, so the data is still there after a
// reload. Nothing is uploaded: the page holds no Roland data of its own, and the firmware without it shows NO D-50 DATA.
import * as data from "./parts/data.js";

const ID = "d50";                         // fm1.toml's name in lower case: the data part's key
const RES_BASE = 0x4e000;                 // app.h APP_RES_BASE
const RES_BYTES = 144 * 4096;             // app.h APP_RES_SECTORS sectors of 4 KB
const RES_MAGIC = 0x31534552;             // "RES1"

function crc32(u8) {                      // zlib's
  let c = ~0;
  for (let i = 0; i < u8.length; i++) {
    c ^= u8[i];
    for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xedb88320 & -(c & 1));
  }
  return ~c >>> 0;
}

async function load(file, say) {
  const payload = new Uint8Array(await file.arrayBuffer());
  if (payload.length < 16 || String.fromCharCode(...payload.subarray(0, 4)) !== "FMD5") {
    say(`${file.name} is not D-50 data: tools/d50_pack.js makes it (d50_fm1base.fmd5), README.md says how.`);
    return;
  }
  if (32 + payload.length > RES_BYTES) {
    say(`${file.name} is ${payload.length} bytes: the region holds ${RES_BYTES - 32}.`);
    return;
  }
  const now = await data.current();
  if (!now) {
    say("Switch the emulator on first, then load the data.");
    return;
  }
  const area = new Uint8Array(now.data), at = RES_BASE - now.base;
  if (at < 0 || at + 32 + payload.length > area.length) {
    say("The emulator's data area does not hold the D-50's region.");
    return;
  }
  const head = new DataView(new ArrayBuffer(32));
  head.setUint32(0, RES_MAGIC, true);
  head.setUint32(4, payload.length, true);
  head.setUint32(8, crc32(payload), true);
  head.setUint32(28, crc32(new Uint8Array(head.buffer, 0, 28)), true);
  area.set(new Uint8Array(head.buffer), at);
  area.set(payload, at + 32);
  data.stop();                            // (what the running emulator writes would replace it)
  await data.keep(ID, area.buffer);
  say(`${file.name} is kept in this browser. Starting again with it.`);
  location.reload();
}

export function attach(ctx) {
  const say = ctx.status || ((s) => console.log(s));
  const bar = document.querySelector(".bar");
  if (!bar || document.getElementById("d50-load")) return;
  const input = document.createElement("input");
  input.type = "file";
  input.accept = ".fmd5";
  input.hidden = true;
  input.id = "d50-file";
  const button = document.createElement("button");
  button.type = "button";
  button.id = "d50-load";
  button.textContent = "Load D-50 data";
  button.title = "Your own d50_fm1base.fmd5 (tools/d50_pack.js): read and kept in this browser, never uploaded";
  button.addEventListener("click", () => input.click());
  input.addEventListener("change", () => {
    const f = input.files && input.files[0];
    input.value = "";
    if (f) load(f, say).catch((e) => say("The data could not be loaded: " + (e && e.message || e)));
  });
  bar.append(button, input);
}
