// The D-50's sample data as the firmware reads it from the start of the user flash (fm1_open/app/d50/d50.h): a 16-byte header
// ("FMD5", version 1, three zero bytes, the sample bytes as u32 LE, a zero u32) and the 262 144 16-bit samples.
//   node tools/d50_pack.js roms/gen/d5_pcm.bin roms/out/d50_pcm.fmd5
// d5_pcm.bin comes from third_party/picofaced5/tools/d5_extract/d5_make_blob.py on your own PCM ROMs. Neither file is ever committed.
'use strict';
const fs = require('fs');
const [inPath, outPath] = process.argv.slice(2);
if (!inPath || !outPath) { console.error('usage: node tools/d50_pack.js <d5_pcm.bin> <out.fmd5>'); process.exit(2); }
const pcm = fs.readFileSync(inPath);
if (pcm.length !== 524288) throw new Error(`${inPath}: ${pcm.length} bytes, 524288 expected`);
const header = Buffer.alloc(16);
header.write('FMD5', 0, 'latin1');
header[4] = 1;
header.writeUInt32LE(pcm.length, 8);
fs.writeFileSync(outPath, Buffer.concat([header, pcm]));
console.log('wrote', outPath, 16 + pcm.length, 'bytes');
