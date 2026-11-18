// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// The engine playing a packed bank (FM1B_PACKED, fm1b.c with live_cache/ and minimp3) on WebAssembly, the emulator of this repository's tests. A bank is made as BFP8
// (index.html), packed (pack_bank.js), and played two ways: by the packed engine, whose notes ask the cache for their sounds, and by the plain engine on the same bank
// unpacked beforehand by the C decoder. With the cache serviced between blocks, a note's output is the plain engine's, one block later (the voice stands still
// until its sound is granted), bit for bit.
//   node test_packed_engine.js [font.sf2]      (default 1mgm.sf2; ffmpeg with libmp3lame and a clang for wasm32)
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const root = __dirname, fontFile = process.argv[2] || path.join(root, '1mgm.sf2');
const { pack } = require('./pack_bank.js');
const wasiClang = path.join(root, 'fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows/bin/clang.exe');
const clang = process.env.FM1_CLANG || (fs.existsSync(wasiClang) ? wasiClang : 'clang');
const app = path.join(root, 'fm1_open/app'), lc = path.join(root, 'fm1_open/live_cache'), mp3 = path.join(root, 'fm1_open/third_party/mp3'), stubs = path.join(root, 'test_sound_cache/stubs');
function build(out, packed, warm, drop, interp) {
  const srcs = [path.join(root, 'test_mt/wrap.c'), path.join(app, 'fm1b.c'), path.join(root, 'fm1_open/web/synth/sea_decode.c'), path.join(root, 'test_sound_cache/libc_min.c')];
  if (packed) srcs.push(path.join(root, 'test_mt/wrap_cache.c'), path.join(lc, 'mp3_sound.c'), path.join(lc, 'sound_cache.c'));
  const deps = srcs.concat(['fm1b.h', 'fm1b_config.h'].map(h => path.join(app, h)), [path.join(lc, 'sound_cache.h'), path.join(lc, 'mp3_sound.h')]);
  if (!fs.existsSync(out) || deps.some(s => fs.statSync(s).mtimeMs > fs.statSync(out).mtimeMs))
    execFileSync(clang, ['--target=wasm32', '-nostdlib', '-O2', '-ffreestanding', '-fno-builtin', '-Wall', '-Wextra', '-Wno-unused-function', '-Wno-unused-parameter', '-DFM1_HOST_TEST', ...(packed && warm ? ['-DFM1B_WARM=1'] : []), ...(packed && drop ? ['-DFM1B_DROP_HEADS=1'] : []), ...(interp ? ['-DFM1B_INTERP4=1'] : []), ...(packed ? ['-DFM1B_PACKED=1', '-DFM1B_BFP8_ONLY=1'] : []),
      '-I' + stubs, '-I' + app, '-I' + path.join(app, 'synth'), '-I' + path.join(root, 'fm1_open/sdk/include'), '-I' + lc, '-I' + mp3, '-Wl,--no-entry', '-Wl,--export-dynamic',
      '-Wl,-z,stack-size=262144', '-Wl,--initial-memory=16777216', ...srcs, '-o', out], { stdio: 'inherit' });
  return new WebAssembly.Module(fs.readFileSync(out));
}
fs.mkdirSync(path.join(root, 'test_mt'), { recursive: true });
const modPacked = build(path.join(root, 'test_mt/mt_packed.wasm'), true), modPlain = build(path.join(root, 'test_mt/mt_plain.wasm'), false);
const modWarm = build(path.join(root, 'test_mt/mt_packed_warm.wasm'), true, true);   // FM1B_WARM=1: the firmware's build
const modDrop = build(path.join(root, 'test_mt/mt_packed_drop.wasm'), true, false, true);   // FM1B_DROP_HEADS=1
const modInterp = build(path.join(root, 'test_mt/mt_packed_interp4.wasm'), true, false, false, true);   // FM1B_INTERP4=1: the firmware's -Interp4
const modDecode = (() => {                                // the stand-alone C decoder, to unpack the bank for the plain engine
  const wasmPath = path.join(root, 'test_sound_cache/harness.wasm'), dir = path.join(root, 'test_sound_cache');
  const sources = [path.join(dir, 'harness.c'), path.join(lc, 'mp3_sound.c'), path.join(lc, 'sound_cache.c'), path.join(dir, 'libc_min.c')];
  if (!fs.existsSync(wasmPath) || sources.concat(path.join(lc, 'mp3_sound.h'), path.join(lc, 'sound_cache.h')).some(s => fs.statSync(s).mtimeMs > fs.statSync(wasmPath).mtimeMs))
    execFileSync(clang, ['--target=wasm32', '-nostdlib', '-O2', '-Wall', '-Wextra', '-Wno-unused-function', '-ffreestanding', '-fno-builtin', '-I' + stubs, '-I' + lc, '-I' + mp3,
      '-Wl,--no-entry', '-Wl,-z,stack-size=262144', '-Wl,--initial-memory=8388608', ...sources, '-o', wasmPath], { stdio: 'inherit' });
  return new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasmPath)), {}).exports;
})();
let failed = 0;
const check = (ok, what) => { if (!ok) ++failed; console.log(ok ? 'ok  ' : 'FAIL', what); };

// ---- the bank, packed, and unpacked again
const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
eval(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] + '\nglobalThis.api = { parseSf2, buildBank, writeFm1, bankInfo };');
const A = globalThis.api, fb = fs.readFileSync(fontFile);
const sf = A.parseSf2(fb.buffer.slice(fb.byteOffset, fb.byteOffset + fb.length));
const src = A.writeFm1(A.buildBank(sf, -2, { rate: 32000, bfp: true }), A.bankInfo('test.fm1b'));
const R = pack(new Uint8Array(src)), P = R.bytes, dv = new DataView(P.buffer, P.byteOffset), u32 = o => dv.getUint32(o, true);
const offPack = u32(80), nPack = u32(84), offSmp = u32(36), nz = dv.getUint16(14, true), offZone = u32(32);
const frames = new Map(); for (let i = 0; i < nz; i++) frames.set(u32(offZone + i * 24), u32(offZone + i * 24 + 4));
const loops = new Map(); for (let i = 0; i < nz; i++) if (P[offZone + i * 24 + 17] & 1) loops.set(u32(offZone + i * 24), [u32(offZone + i * 24 + 8), u32(offZone + i * 24 + 12)]);
let streamEnd = 0; for (let i = 0; i < nPack; i++) { const e = offPack + i * 24; if (P[e + 16] === 2) streamEnd = Math.max(streamEnd, u32(e + 8) + u32(e + 12)); }
new Uint8Array(modDecode.memory.buffer).set(P.subarray(offSmp, offSmp + streamEnd), modDecode.stream_ptr()); modDecode.set_stream_bytes(streamEnd);
const U = new Uint8Array(R.unpackedBytes); U.set(P.subarray(0, offSmp));
const uv = new DataView(U.buffer); uv.setUint16(4, 1, true); uv.setUint16(6, 8, true); uv.setUint32(40, U.length, true); uv.setUint32(80, 0, true); uv.setUint32(84, 0, true); uv.setUint32(88, 0, true);
const kinds = new Map();                                    // unpacked offset -> 'mp3' | 'lossless'
for (let i = 0; i < nPack; i++) {
  const e = offPack + i * 24, uOff = u32(e), uBytes = u32(e + 4), codec = P[e + 16], n = frames.get(uOff);
  if (codec === 0) { U.set(P.subarray(offSmp + u32(e + 8), offSmp + u32(e + 8) + uBytes), uOff); kinds.set(uOff, 'lossless'); continue; }
  new Uint32Array(modDecode.memory.buffer, modDecode.index_ptr(), 4).set([u32(e + 8), dv.getUint16(e + 18, true), n, 0]);
  const xf = dv.getUint16(e + 20, true), lp = loops.get(uOff);
  new Uint32Array(modDecode.memory.buffer, modDecode.loop_ptr(), 3).set(xf && lp ? [lp[0], lp[1], xf] : [0, 0, 0]);
  if (modDecode.decode_sound(0, 1000) !== uBytes / 33) throw new Error('decode failed');
  if (xf && lp) modDecode.crossfade(modDecode.out_ptr(), n, lp[0], lp[1], xf);
  U.set(new Uint8Array(modDecode.memory.buffer, modDecode.out_ptr(), uBytes), uOff); kinds.set(uOff, 'mp3');
}
console.log(`${path.basename(fontFile)}: packed ${P.length} bytes (${R.sounds - R.lossless} MP3, ${R.lossless} lossless), unpacked for the plain engine ${U.length}`);

// ---- the two players
function player(mod, bank, poolBytes) {
  const w = new WebAssembly.Instance(mod, {}).exports;
  if (w._initialize) w._initialize();
  new Uint8Array(w.memory.buffer, w.mt_bank(), bank.length).set(bank);
  if (w.mt_cache_attach) w.mt_cache_attach(poolBytes);
  const rc = w.mt_bind(bank.length);
  return { w, rc, block() { w.mt_block(); return Int32Array.from(new Int32Array(w.memory.buffer, w.mt_out(), 128)); } };
}
check(player(modPlain, U, 0).rc === 0, 'the plain engine binds the unpacked bank');
check(player(modPlain, P, 0).rc !== 0, 'the plain engine refuses the packed bank');
check(player(modPacked, P, 0).rc !== 0, 'the packed engine refuses a packed bank while no pool is attached');
check(player(modPacked, P, 262144).rc === 0, 'the packed engine binds the packed bank');
check(player(modPacked, U, 262144).rc === 0, 'the packed engine still binds an unpacked bank');
{ const b16 = A.writeFm1(A.buildBank(sf, [0], { rate: 32000, bits: 16 })); check(player(modPlain, b16, 0).rc === 0 && player(modPacked, b16, 262144).rc !== 0, 'built with FM1B_BFP8_ONLY (as the firmware with the live cache is), the engine refuses a 16-bit bank that the plain engine binds'); }
// a packed bank with a damaged pack table must be refused, not played
{ const bad = Uint8Array.from(P); new DataView(bad.buffer).setUint32(offPack + 8, 0x7fffffff, true); check(player(modPacked, bad, 262144).rc !== 0, 'a pack entry that lies outside the file is refused'); }

// ---- 1. every program on a few notes: the packed engine is the plain one, a block later
const PROGRAMS = [0, 4, 11, 19, 24, 33, 40, 48, 56, 61, 73, 80, 90, 104, 118];
const BLOCKS = 400;
function run(pl, program, notes, packed, budget) {
  pl.w.mt_panic(); pl.w.mt_block(); if (packed) pl.w.mt_cache_service(1000);   // nothing left sounding from the last set
  pl.w.mt_program(0, program); pl.w.mt_block();
  const out = [], off = BLOCKS >> 1;
  for (const n of notes) pl.w.mt_note(0, n, 100);
  for (let k = 0; k < BLOCKS + 1; k++) {
    if (packed ? k === off + 1 : k === off) for (const n of notes) pl.w.mt_note(0, n, 0);     // the packed player's release a block later too
    out.push(pl.block());
    if (packed) pl.w.mt_cache_service(budget);
  }
  return out;
}
const plain = player(modPlain, U, 0), pk = player(modPacked, P, 262144);
if (process.env.DEBUG2) {
  const q = player(modPacked, P, 262144); q.w.mt_program(0, 0); q.w.mt_block(); q.w.mt_note(0, 36, 100); q.block(); q.w.mt_cache_service(1000); q.block();
  const pool = new Uint8Array(q.w.memory.buffer, q.w.mt_pool(), 66 * 33);
  let hit = -1; for (let o = 0; o + 33 <= U.length && hit < 0; o += 4) { let ok = true; for (let i = 0; i < 66 && ok; i++) ok = U[o + i] === pool[i]; if (ok) hit = o; }
  console.log('pool prefix found in U at', hit, hit >= 0 ? kinds.get(hit) : '', 'first pool bytes', [...pool.slice(0, 12)].join());
  let near = -1, best = 1e9; for (let o = offSmp; o + 33 <= U.length; o += 4) { let d = 0; for (let i = 0; i < 33; i++) d += Math.abs(U[o + i] - pool[i]); if (d < best) { best = d; near = o; } }
  console.log('closest 33 bytes in U at', near, 'distance', best, 'kind', kinds.get(near));
  if (near >= 0) { let diffs = []; for (let i = 0; i < 20 * 33; i++) if (U[near + i] !== pool[i]) diffs.push(i); console.log('differing bytes in first 20 groups:', diffs.length, diffs.slice(0, 10).join()); }

  const a = run(plain, 118, [36], false, 0), b = run(pk, 118, [36], true, 1000);
  { let n = 0, first = -1; a.forEach((blk, k) => { if (k < a.length - 1) blk.forEach((v, i) => { if (v !== b[k + 1][i]) { if (first < 0) first = k * 128 + i; n++; } }); }); console.log('block diff count', n, 'first at sample', first); }
  for (let k = 0; k < 4; k++) console.log('block', k, 'plain', a[k].slice(0, 6).join(','), '| packed next', b[k + 1].slice(0, 6).join(','), '| packed same', b[k].slice(0, 6).join(','));
}
let exact = 0, shifted = 0, mixed = 0, silent = 0, total = 0, withLossless = 0;
const presetOf = program => dv.getUint16(u32(20) + program * 2, true);          // bank map slot -> preset
const zonesFor = (program, note) => { const p = presetOf(program), off = u32(68);   // the zones a note starts: its key map's, and the stacked layers that cover it
  let n = 1; if (off) { const first = dv.getUint16(off + p * 4, true), count = dv.getUint16(off + p * 4 + 2, true), np = dv.getUint16(12, true);
    for (let e = 0; e < count; e++) { const o = off + 4 * np + 4 * (first + e); if (P[o] <= note && note <= P[o + 1]) n++; } } return n; };
const usesLossless = program => { const p = presetOf(program), first = dv.getUint16(u32(24) + p * 24, true), count = dv.getUint16(u32(24) + p * 24 + 2, true);
  for (let z = first; z < first + count; z++) if (kinds.get(u32(offZone + z * 24)) === 'lossless') return true; return false; };
for (const program of PROGRAMS) for (const notes of [[36], [60], [84], [48, 55, 64, 71]]) {
  ++total;
  if (plain.w.mt_program(0, program) !== 0) { --total; continue; }
  const a = run(plain, program, notes, false, 0), b = run(pk, program, notes, true, 1000);
  const same = (off) => a.every((blk, k) => k + off >= b.length || blk.every((v, i) => v === b[k + off][i]));
  const energy = a.reduce((s, blk) => s + blk.reduce((t, v) => t + Math.abs(v), 0), 0);
  if (!energy) { ++silent; continue; }
  if (usesLossless(program)) { ++withLossless; continue; }     // a stored sound starts at once, the others a block later: the sum is not a shift of the plain one
  const aSame0 = a.every((blk, k) => blk.every((v, i) => v === b[k][i]));
  if (same(1)) ++shifted; else if (aSame0) ++exact; else { ++mixed; if (process.env.DEBUG) { let k = 0; while (k < a.length - 1 && a[k].every((v, i) => v === b[k + 1][i])) k++; const d = a[k].map((v, i) => v - b[k + 1][i]); console.log('  differs: program', program, 'notes', notes.join(), 'first block', k, 'of', a.length, 'max diff', Math.max(...d.map(Math.abs)), 'max level', Math.max(...a[k].map(Math.abs))); } }
}
check(shifted > 0 && mixed === 0, `${total} program/note sets: ${shifted} exactly the plain engine's output one block later, ${exact} (sounds stored lossless) exactly the same block, ${mixed} neither, ${silent} silent, ${withLossless} left out because the preset has a lossless sound (they start a block apart)`);
const st = i => pk.w.mt_cache_stat(i);
check(st(8) === 0, `after all the notes ended, no pin is left (${st(8)} held; ${st(5)} requests, ${st(0)} hits, ${st(1)} misses)`);
check(st(6) === 0, `no note was over before its sound came (${st(6)})`);

// ---- 2. a slow decoder: the voice stands still while its sound is decoded; the note still ends, and the cache lets go
{
  const slow = player(modPacked, P, 262144), w = slow.w;
  w.mt_program(0, 0); w.mt_block();
  w.mt_note(0, 60, 100); w.mt_note(0, 72, 100);
  let firstSound = -1, blocks = 0, e = 0;
  for (let k = 0; k < 3000; k++) {
    if (k === 1500) { w.mt_note(0, 60, 0); w.mt_note(0, 72, 0); }
    const blk = slow.block(); ++blocks;
    if (firstSound < 0 && blk.some(v => v)) firstSound = k;
    for (const v of blk) e += Math.abs(v);
    w.mt_cache_service(1);                                  // one MP3 frame (1152 samples) per block: slower than a voice reads it, at higher pitch
  }
  check(firstSound >= 0 && e > 0 && w.mt_voices() === 0 && w.mt_cache_stat(8) === 0, `with one MP3 frame decoded per block the notes start after ${firstSound} blocks, play, end (voices ${w.mt_voices()}), and no pin is left`);
}

// ---- 2b. a decoder that falls behind a voice in the middle of a note: the voice stands still, and what it plays is the plain engine's output, block for block
{
  const slow = player(modPacked, P, 262144), ref = player(modPlain, U, 0);
  let ok = true, stalled = 0, compared = 0;
  for (const [program, note] of [[0, 84], [24, 96], [48, 72], [73, 90], [40, 88], [71, 80], [0, 60], [24, 60], [32, 48], [56, 70], [64, 66], [19, 72], [11, 64]].filter(([p, n]) => !usesLossless(p) && zonesFor(p, n) === 1)) {   // one voice a note: layers wait for their sounds one by one
    for (const pl of [slow, ref]) { pl.w.mt_panic(); pl.block(); if (pl === slow) pl.w.mt_cache_service(1000); pl.w.mt_program(0, program); pl.block(); pl.w.mt_note(0, note, 100); }
    const a = [], b = [];
    for (let k = 0; k < 1400; k++) {
      const x = slow.block(); if (x.some(v => v)) b.push(x); else if (b.length) ++stalled;
      if (k % 8 === 0) slow.w.mt_cache_service(1);            // 1152 samples per 8 blocks (1024 frames): less than a note high above its root reads
      const y = ref.block(); if (y.some(v => v)) a.push(y);
    }
    for (let k = 0; k < Math.min(a.length, b.length); k++) { ++compared; if (!a[k].every((v, i) => v === b[k][i])) { ok = false; if (process.env.DEBUG3) console.log("  mismatch program", program, "note", note, "at compared block", k, "of", a.length, b.length, a[k].slice(0, 4).join(), "|", b[k].slice(0, 4).join()); break; } }
  }
  check(ok && stalled > 3 && compared > 3000, `decoding 1 MP3 frame in 8 blocks: ${stalled} blocks where a voice stood still, ${compared} other blocks exactly the plain engine's`);
}

// ---- 2c. a note over before its sound came, and one over after its sound was granted: nothing is decoded for the first, the pin of the second comes back
{
  const t = player(modPacked, P, 262144), w = t.w;
  w.mt_program(0, 0); t.block(); w.mt_note(0, 60, 100); t.block(); w.mt_panic(); t.block();
  const before = w.mt_cache_stat(1); w.mt_cache_service(1000); t.block(); w.mt_cache_service(1000);
  check(w.mt_cache_stat(6) === 1 && w.mt_cache_stat(1) === before && w.mt_cache_stat(8) === 0, `a note over before the control task answered: counted as lost (${w.mt_cache_stat(6)}), no decode (${w.mt_cache_stat(1) - before}), no pin (${w.mt_cache_stat(8)})`);
  w.mt_note(0, 62, 100); t.block(); w.mt_cache_service(1000); const pinned = w.mt_cache_stat(8); w.mt_panic(); t.block(); w.mt_cache_service(1000); t.block(); w.mt_cache_service(1000);
  check(pinned > 0 && w.mt_cache_stat(8) === 0, `a note over after its sound was granted but before the interrupt took it: ${pinned} pinned, then ${w.mt_cache_stat(8)}`);
}

// ---- 3. a small pool and many notes: pins come back, voices end, nothing is overwritten while it plays
{
  const small = player(modPacked, P, 24 * 1024), w = small.w;
  let seed = 7; const rnd = () => (seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296;
  let sound = 0, ok = true;
  for (let k = 0; k < 6000 && ok; k++) {
    if (k % 40 === 0) { w.mt_channel_program(Math.floor(rnd() * 15), 0, Math.floor(rnd() * 128)); }
    if (k % 7 === 0) w.mt_note(Math.floor(rnd() * 15), 36 + Math.floor(rnd() * 48), rnd() < .8 ? 90 : 0);
    const blk = small.block(); for (const v of blk) sound += Math.abs(v);
    w.mt_cache_service(4);
  }
  for (let ch = 0; ch < 16; ch++) w.mt_control(ch, 123, 0);
  for (let k = 0; k < 4000; k++) { small.block(); w.mt_cache_service(40); }
  check(sound > 0 && w.mt_voices() === 0 && w.mt_cache_stat(8) === 0, `24 KB pool, 6000 blocks of random notes on random programs: ${w.mt_cache_stat(1)} decodes, ${w.mt_cache_stat(2)} dropped, ${w.mt_cache_stat(3)} no room, ${w.mt_cache_stat(6)} notes over before their sound; voices ${w.mt_voices()}, pins ${w.mt_cache_stat(8)}`);
}
// ---- 4. FM1B_WARM: a program change has the cache decode the sound's middle sounds before the first note asks
{
  const mp3Programs = []; for (let pr = 0; pr < 128 && mp3Programs.length < 6; pr++) if (!usesLossless(pr) && zonesFor(pr, 60) === 1) mp3Programs.push(pr);
  const t = player(modWarm, P, 262144), w = t.w;
  let warmed = 0, early = 0, baseEarly = 0, sets = 0;
  for (const pr of mp3Programs) {
    const base = player(modPacked, P, 262144);                // a fresh cache each time: sounds the earlier programs share would be there already
    for (const pl of [t, base]) { pl.w.mt_panic(); pl.block(); pl.w.mt_cache_service(1000); pl.w.mt_cache_service(1000); }
    const miss0 = t.w.mt_cache_stat(1);
    t.w.mt_program(0, pr); t.block(); base.w.mt_program(0, pr); base.block();
    for (let k = 0; k < 40; k++) { t.w.mt_cache_service(1000); t.block(); base.w.mt_cache_service(1000); base.block(); }   // nothing plays: only the warming decodes
    warmed += t.w.mt_cache_stat(1) - miss0;
    t.w.mt_note(0, 60, 100); base.w.mt_note(0, 60, 100);
    t.block(); base.block();                                  // the note's request is made in this block
    t.w.mt_cache_service(0); base.w.mt_cache_service(0);      // the control task answers it but decodes nothing
    const a = t.block(), b = base.block();
    ++sets; if (a.some(v => v !== 0)) ++early; if (b.some(v => v !== 0)) ++baseEarly;
    t.w.mt_panic(); base.w.mt_panic(); t.block(); base.block();
  }
  check(warmed > 0, `a program change decodes sounds ahead: ${warmed} decodes on ${sets} programs with no note played`);
  check(early === sets && baseEarly === 0, `with a program change before it, a note sounds in the block after the control task answered, without decoding anything (${early} of ${sets} programs); without the warming none does (${baseEarly})`);
  w.mt_cache_service(1000); t.block(); w.mt_cache_service(1000);
  check(w.mt_cache_stat(8) === 0, `warming leaves no pin (pins held: ${w.mt_cache_stat(8)})`);
  const small = player(modWarm, P, 24576);                    // a small pool: the warming gives way to notes and nothing is overwritten while it plays
  let seed = 7; const rnd = () => (seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff; let sound = 0;
  for (let k = 0; k < 4000; k++) {
    if (k % 25 === 0) small.w.mt_channel_program(Math.floor(rnd() * 15), 0, Math.floor(rnd() * 128));
    if (k % 6 === 0) small.w.mt_note(Math.floor(rnd() * 15), 36 + Math.floor(rnd() * 60), 40 + Math.floor(rnd() * 80));
    for (const v of small.block()) sound += Math.abs(v);
    small.w.mt_cache_service(4);
  }
  for (let ch = 0; ch < 16; ch++) small.w.mt_control(ch, 123, 0);
  for (let k = 0; k < 4000; k++) { small.block(); small.w.mt_cache_service(40); }
  check(sound > 0 && small.w.mt_voices() === 0 && small.w.mt_cache_stat(8) === 0, `24 KB pool with warming, 4000 blocks of random notes and program changes: ${small.w.mt_cache_stat(1)} decodes, ${small.w.mt_cache_stat(3)} no room; voices ${small.w.mt_voices()}, pins ${small.w.mt_cache_stat(8)}`);
}
// ---- 5. a looped sound's first groups go back to the pool once the voices are past them, and nothing reads them again: the same note held on two players, one of which
// scribbles over what was given back after every block, plays bit for bit the same
{
  let dropped = 0, programs = 0, same = true, worst = '';
  for (let pr = 0; pr < 128 && programs < 12; pr++) {
    if (usesLossless(pr) || zonesFor(pr, 60) !== 1) continue;
    const a = player(modDrop, P, 262144), b = player(modDrop, P, 262144), pool = new Uint8Array(b.w.memory.buffer, b.w.mt_pool(), 262144);
    for (const pl of [a, b]) { pl.w.mt_program(0, pr); pl.block(); pl.w.mt_note(0, 60, 100); }
    const seen = new Map(); let freed = []; ++programs;
    for (let k = 0; k < 900 && same; k++) {
      for (const pl of [a, b]) pl.w.mt_cache_service(1000);
      for (let i = 0; i < 160; i++) {
        if (b.w.mt_cache_entry(i, 0) === 2 && !b.w.mt_cache_entry(i, 3)) seen.set(i, b.w.mt_cache_entry(i, 1));
        else if (b.w.mt_cache_entry(i, 3) && seen.has(i) && !freed.some(f => f.i === i)) freed.push({ i, from: seen.get(i), to: b.w.mt_cache_entry(i, 1) });
      }
      for (const f of freed) pool.fill(0xa5, f.from, f.to);
      const x = a.block(), y = b.block();
      if (!x.every((v, j) => v === y[j])) { same = false; worst = `program ${pr}, block ${k}`; }
    }
    dropped += b.w.mt_cache_stat(9);
  }
  check(dropped > 0, `held notes on ${programs} programs: ${dropped} sounds gave their first groups back to the pool`);
  check(same, `and a player that overwrites those groups after every block plays the same as one that does not ${worst}`);
}
// ---- 6. codec 4 (lossless sounds with 4..7-bit mantissas): the engine plays them as the same bank with those samples as BFP8 (codec 0), to the sample
{
  const o = { mp3: 'none', flash: 300000, 'min-bits': 5 }, N = pack(new Uint8Array(src), o), W = pack(new Uint8Array(src), { ...o, _wide: 1 });
  const nv = new DataView(N.bytes.buffer, N.bytes.byteOffset), widths = new Map();
  for (let i = 0; i < nv.getUint32(84, true); i++) { const e = nv.getUint32(80, true) + i * 24; if (N.bytes[e + 16] === 4) widths.set(N.bytes[e + 17], (widths.get(N.bytes[e + 17]) || 0) + 1); }
  for (const [mod, how] of [[modPacked, 'linear'], [modInterp, '4-point']]) {
  const a = player(mod, N.bytes, 262144), b = player(mod, W.bytes, 262144);
  check(a.rc === 0 && b.rc === 0 && N.narrowed > 0, `a bank of ${N.bytes.length} bytes with ${N.narrowed} narrow sounds (${[...widths].map(([k, v]) => v + ' of ' + k + ' bits').join(', ')}) binds, and its wide twin (${W.bytes.length} bytes)`);
  let same = true, loud = 0;
  for (const prog of PROGRAMS) {
    const x = run(a, prog, [36, 48, 60, 67, 79], false), y = run(b, prog, [36, 48, 60, 67, 79], false);
    for (let k = 0; k < x.length; k++) for (let i = 0; i < 128; i++) { if (x[k][i] !== y[k][i]) same = false; loud = Math.max(loud, Math.abs(x[k][i])); }
  }
  check(same && loud > 1000, `${how} interpolation, ${PROGRAMS.length} programs, five notes each: the narrow bank plays sample for sample as its BFP8 twin (peak ${loud})`);
  }
  const bad = Uint8Array.from(N.bytes), e0 = [...Array(nv.getUint32(84, true)).keys()].map(i => nv.getUint32(80, true) + i * 24).find(e => N.bytes[e + 16] === 4);
  bad[e0 + 17] = 3; check(player(modPacked, bad, 262144).rc !== 0, 'a codec 4 entry of 3 bits is refused');
}
console.log(failed ? `${failed} FAILED` : 'all passed'); process.exit(failed ? 1 : 0);
