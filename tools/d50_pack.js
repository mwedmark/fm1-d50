// The D-50's sample data, and a patch bank, as the firmware reads them from the start of the user flash (fm1_open/app/d50/d50.h):
//   header, 16 bytes: "FMD5", version (1: samples only, 2: and a bank), three zero bytes, the sample bytes (u32 LE), the bank's deflated bytes (u32 LE, 0 for none)
//   the 262 144 16-bit samples
//   version 2: the bank's raw bytes (u32 LE, 448 a patch) and its raw deflate stream (app/d50/d50_inflate.c unpacks it into RAM)
//   node tools/d50_pack.js roms/gen/d5_pcm.bin roms/out/d50_pcm.fmd5 [--bank "D50 files/syx files/PND50-00.syx"]
// d5_pcm.bin comes from third_party/picofaced5/tools/d5_extract/d5_make_blob.py on your own PCM ROMs; the bank is a D-50 SysEx bulk dump (DT1 to
// 02-00-00, 64 patches). Neither file, nor what this writes, is ever committed.
'use strict';
const fs = require('fs');
const zlib = require('zlib');

const USER_FLASH = 536576, PCM_BYTES = 524288, PATCH = 448, PATCHES = 64;
const args = process.argv.slice(2);
const bankAt = args.indexOf('--bank');
const bankPath = bankAt >= 0 ? args.splice(bankAt, 2)[1] : null;
const [inPath, outPath] = args;
if (!inPath || !outPath) { console.error('usage: node tools/d50_pack.js <d5_pcm.bin> <out.fmd5> [--bank <bulk dump.syx>]'); process.exit(2); }
const pcm = fs.readFileSync(inPath);
if (pcm.length !== PCM_BYTES) throw new Error(`${inPath}: ${pcm.length} bytes, ${PCM_BYTES} expected`);

// A D-50 bulk dump: Roland DT1 messages F0 41 dev 14 12 a b c data... checksum F7, 7-bit addresses; the patches from 02-00-00 (0x8000).
function bankFromSyx(file) {
  const d = fs.readFileSync(file), mem = new Map();
  let messages = 0;
  for (let i = 0; i < d.length; i++) {
    if (d[i] !== 0xF0) continue;
    const end = d.indexOf(0xF7, i);
    if (end < 0) break;
    const m = d.subarray(i, end + 1);
    i = end;
    if (m.length < 11 || m[1] !== 0x41 || m[3] !== 0x14 || m[4] !== 0x12) continue;
    let sum = 0;
    for (let k = 5; k < m.length - 1; k++) sum += m[k];
    if (sum & 0x7F) throw new Error(`${file}: a DT1 message with a bad checksum at byte ${i}`);
    const at = m[5] << 14 | m[6] << 7 | m[7];
    for (let k = 8; k < m.length - 2; k++) mem.set(at + k - 8, m[k]);
    messages++;
  }
  const bank = Buffer.alloc(PATCHES * PATCH);
  for (let k = 0; k < bank.length; k++) {
    if (!mem.has(0x8000 + k)) throw new Error(`${file}: the dump has no byte for patch ${(k / PATCH | 0) + 1} (${messages} DT1 messages)`);
    bank[k] = mem.get(0x8000 + k);
  }
  return bank;
}

const CHARS = ' ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890-';
const header = Buffer.alloc(16);
header.write('FMD5', 0, 'latin1');
header.writeUInt32LE(PCM_BYTES, 8);
const parts = [header, pcm];
if (bankPath) {
  const bank = bankFromSyx(bankPath);
  const deflated = zlib.deflateRawSync(bank, { level: 9, memLevel: 9 });
  header[4] = 2;
  header.writeUInt32LE(deflated.length, 12);
  const raw = Buffer.alloc(4);
  raw.writeUInt32LE(bank.length, 0);
  parts.push(raw, deflated);
  const names = [];
  for (let p = 0; p < PATCHES; p++) names.push(String.fromCharCode(...bank.subarray(p * PATCH + 384, p * PATCH + 402)).replace(/[\s\S]/g, c => CHARS[c.charCodeAt(0)] || '?').trimEnd());
  console.log(`bank ${bankPath}: ${PATCHES} patches, ${bank.length} bytes, deflated ${deflated.length}; 11 ${names[0]}, 12 ${names[1]}, ... 88 ${names[63]}`);
} else header[4] = 1;
const out = Buffer.concat(parts);
if (out.length > USER_FLASH) throw new Error(`${out.length} bytes: over the ${USER_FLASH} bytes of the user flash`);
fs.writeFileSync(outPath, out);
console.log('wrote', outPath, out.length, 'bytes,', USER_FLASH - out.length, 'of the user flash left');
