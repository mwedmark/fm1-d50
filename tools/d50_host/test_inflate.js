// Builds the deflate test vectors for test_inflate.c and runs it: node tools/d50_host/test_inflate.js [bank.fmd5]
'use strict';
const fs = require('fs'), zlib = require('zlib'), path = require('path'), { execFileSync } = require('child_process');
const dir = path.join(__dirname, '../../roms/out/inflate_test');
fs.mkdirSync(dir, { recursive: true });
let seed = 1; const rnd = () => (seed = (seed * 1103515245 + 12345) >>> 0) >>> 16;
const text = Buffer.from('The D-50 plays LA synthesis: PCM attacks and synthesized sustains. '.repeat(400));
const noise = Buffer.from(Array.from({ length: 30000 }, () => rnd() & 255));
const mixed = Buffer.from(Array.from({ length: 40000 }, (_, i) => (i % 448 < 300 ? (i * 7) & 127 : rnd() & 127)));
const cases = [['stored', text, { level: 0 }], ['fixed', text, { strategy: zlib.constants.Z_FIXED }], ['dynamic', mixed, { level: 9 }],
               ['noise', noise, { level: 6 }], ['empty', Buffer.alloc(0), {}], ['one', Buffer.from([65]), {}]];
const bankFile = process.argv[2];
if (bankFile) {
  const f = fs.readFileSync(bankFile);
  if (f.toString('latin1', 0, 4) === 'FMD5' && f[4] === 2) {
    const z = f.subarray(16 + 524288 + 4, 16 + 524288 + 4 + f.readUInt32LE(12));
    cases.push(['bank', zlib.inflateRawSync(z), null, z]);
  }
}
const files = [];
for (const [name, raw, opt, given] of cases) {
  const z = given || zlib.deflateRawSync(raw, opt);
  fs.writeFileSync(path.join(dir, name + '.z'), z);
  fs.writeFileSync(path.join(dir, name + '.raw'), raw);
  files.push(path.join(dir, name + '.z'), path.join(dir, name + '.raw'));
}
const wasi = process.env.WASI_SDK || 'D:/git/fm1-sf2/fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows';
const wasm = path.join(dir, 'test_inflate.wasm');
execFileSync(path.join(wasi, 'bin/clang'), ['--target=wasm32-wasip1', `--sysroot=${wasi}/share/wasi-sysroot`, '-O2', '-Wall', path.join(__dirname, 'test_inflate.c'), '-o', wasm], { stdio: 'inherit' });
const rel = files.map(f => path.relative(process.cwd(), f).split(path.sep).join('/'));
try { execFileSync('node', [path.join(__dirname, 'run.js'), wasm, ...rel], { stdio: ['ignore', 'inherit', 'ignore'] }); }
catch (e) { process.exitCode = 1; }
