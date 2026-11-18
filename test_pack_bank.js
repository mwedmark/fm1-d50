// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// pack_bank.js (the converter's writer of the live cache's packed bank) against the C decoder of fm1_open/live_cache on WebAssembly:
// a bank is made as BFP8 (index.html's converter), packed, and every sound is unpacked again from the packed file alone, as the engine would:
// the header and pack table are read independently of pack_bank.js, a codec 2 entry is decoded from the stream by the C code (and its loop
// crossfaded), a codec 0 entry is copied. Then the unpacked sound is compared with the BFP8 sound the bank held.
//   node test_pack_bank.js [font.sf2]      (default 1mgm.sf2; ffmpeg with libmp3lame and a clang for wasm32 are needed, as for test_sound_cache.js)
//   node test_pack_bank.js font.sf2 --packed=bank.fm1b    checks a bank the converter page packed from that font (any bit rate) instead
const fs = require('fs'), path = require('path'), { execFileSync } = require('child_process');
const root = __dirname, fontFile = process.argv.slice(2).find(a => !a.startsWith('--')) || path.join(root, '1mgm.sf2');
const packedFile = (process.argv.find(a => a.startsWith('--packed=')) || '').slice(9);
const { pack, unpackBfp8, encodeBfp8 } = require('./pack_bank.js');
const wasiClang = path.join(root, 'fm1_open/build/tools/wasi-sdk-27.0-x86_64-windows/bin/clang.exe');
const clang = process.env.FM1_CLANG || (fs.existsSync(wasiClang) ? wasiClang : 'clang');
const app = path.join(root, 'fm1_open/live_cache'), dir = path.join(root, 'test_sound_cache'), wasmPath = path.join(dir, 'harness.wasm');
const sources = [path.join(dir, 'harness.c'), path.join(app, 'mp3_sound.c'), path.join(app, 'sound_cache.c'), path.join(dir, 'libc_min.c')];
if (!fs.existsSync(wasmPath) || sources.concat(path.join(app, 'mp3_sound.h'), path.join(app, 'sound_cache.h')).some(s => fs.statSync(s).mtimeMs > fs.statSync(wasmPath).mtimeMs))
  execFileSync(clang, ['--target=wasm32', '-nostdlib', '-O2', '-Wall', '-Wextra', '-Wno-unused-function', '-ffreestanding', '-fno-builtin', '-I' + path.join(dir, 'stubs'), '-I' + app,
    '-I' + path.join(root, 'fm1_open/third_party/mp3'), '-Wl,--no-entry', '-Wl,-z,stack-size=262144', '-Wl,--initial-memory=8388608', ...sources, '-o', wasmPath], { stdio: 'inherit' });
const w = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(wasmPath)), {}).exports;
let failed = 0;
const check = (ok, what) => { if (!ok) ++failed; console.log(ok ? 'ok  ' : 'FAIL', what); };

const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
eval(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] + '\nglobalThis.api = { parseSf2, buildBank, writeFm1, bankInfo };');
const A = globalThis.api, b = fs.readFileSync(fontFile);
const sf = A.parseSf2(b.buffer.slice(b.byteOffset, b.byteOffset + b.length));
// a bank from the page: the same BFP8 bank, with the name and date its info block has (at 92 in the packed file, 32 and 20 bytes)
const given = packedFile ? new Uint8Array(fs.readFileSync(packedFile)) : null, text = (a, n) => String.fromCharCode(...given.subarray(a, a + n)).replace(/\0.*$/s, '');
const src = A.writeFm1(A.buildBank(sf, -2, { rate: 32000, bfp: true }), given ? { name: text(92, 32), date: text(124, 20) } : A.bankInfo('test.fm1b'));
const t0 = Date.now(), r = given ? { bytes: given, sounds: '?', lossless: '?' } : pack(new Uint8Array(src)), P = r.bytes, dv = new DataView(P.buffer, P.byteOffset), sdv = new DataView(src.buffer, src.byteOffset), u32 = o => dv.getUint32(o, true);
console.log(`${path.basename(fontFile)}: ${src.length} bytes as BFP8, ${P.length} packed (${((Date.now() - t0) / 1000).toFixed(0)} s); ${r.sounds} sounds, ${r.sounds - r.lossless} MP3, ${r.lossless} lossless`);

// ---- the header, read on its own
const oldHdr = sdv.getUint32(16, true) >= 80 && sdv.getUint32(76, true) ? 80 : sdv.getUint32(16, true), shift = 92 - oldHdr;
check(String.fromCharCode(...P.subarray(0, 4)) === 'FM1B' && dv.getUint16(4, true) === 2 && dv.getUint16(6, true) === 0x18 && u32(40) === P.length && u32(8) === 32000,
  'FM1B, version 2, flags BFP8 + packed (0x18), the file size and the rate');
const offPack = u32(80), nPack = u32(84), entry = u32(88), offSmp = u32(36);
check(entry === 24 && offPack + nPack * entry === P.length && offSmp === sdv.getUint32(36, true) + shift && offPack > offSmp, `pack table of ${nPack} entries of ${entry} bytes ends the file, the sample data starts at ${offSmp}`);
check([16, 20, 24, 28, 32].every(f => u32(f) === sdv.getUint32(f, true) + shift) && src.subarray(oldHdr, sdv.getUint32(36, true)).every((v, i) => {
  const a = oldHdr + i, zone = sdv.getUint32(32, true), nz = sdv.getUint16(14, true); const inZoneOffset = a >= zone && a < zone + nz * 24 && (a - zone) % 24 < 4;
  return inZoneOffset || P[92 + i] === v; }), 'the tables between the header and the samples are the bank\'s own bytes, the zones\' sample offsets moved by the header\'s growth');

// ---- every sound, unpacked from the packed file only
const zoneAt = (i, f) => dv.getUint32(u32(32) + i * 24 + f, true), nz = dv.getUint16(14, true);
const loops = new Map();                                   // unpacked offset -> [loop start, loop end] of a zone using it
for (let i = 0; i < nz; i++) if (P[u32(32) + i * 24 + 17] & 1) loops.set(zoneAt(i, 0), [zoneAt(i, 8), zoneAt(i, 12)]);
const frames = new Map(); for (let i = 0; i < nz; i++) frames.set(zoneAt(i, 0), zoneAt(i, 4));
const stream = P.subarray(offSmp, offPack), mem = () => new Uint8Array(w.memory.buffer);
check(stream.length <= 1 << 21, 'the sample data fits the harness\'s stream buffer');
// The decoder is given the stream up to the end of the last MP3 entry, not the sample data to the end of the file: minimp3 finds a frame only when the
// next frames match it or the buffer ends, and BFP8 bytes after the stream make the last 10 frames "not a frame" (found by this test).
let streamEnd = 0; for (let i = 0; i < nPack; i++) { const e = offPack + i * entry; if (P[e + 16] === 2) streamEnd = Math.max(streamEnd, u32(e + 8) + u32(e + 12)); }
mem().set(P.subarray(offSmp, offSmp + streamEnd), w.stream_ptr()); w.set_stream_bytes(streamEnd);
let prevEnd = 0, ordered = true, covered = true, sameLossless = true, nMp3 = 0, nLossless = 0, snrSum = 0, snrWorst = 1e9, loopOk = true, xfaded = 0;
const snr = (a, c) => { let s = 0, e = 0; for (let i = 0; i < a.length; i++) { s += a[i] * a[i]; e += (a[i] - c[i]) ** 2; } return 10 * Math.log10(Math.max(s, 1) / Math.max(e, 1e-9)); };
const idx = () => new Uint32Array(w.memory.buffer, w.index_ptr(), 4 * 1024);
for (let i = 0; i < nPack; i++) {
  const e = offPack + i * entry, uOff = u32(e), uBytes = u32(e + 4), pOff = u32(e + 8), pBytes = u32(e + 12), codec = P[e + 16], delay = dv.getUint16(e + 18, true), xf = dv.getUint16(e + 20, true);
  ordered = ordered && uOff >= prevEnd && uOff % 4 === 0; prevEnd = uOff + uBytes;
  const n = frames.get(uOff); if (n === undefined || (n + 31 >> 5) * 33 !== uBytes) { covered = false; continue; }
  const want = src.subarray(uOff - shift, uOff - shift + uBytes);                       // the bank's own BFP8 for the sound
  let got;
  if (codec === 0) { got = P.subarray(offSmp + pOff, offSmp + pOff + pBytes); sameLossless = sameLossless && pBytes === uBytes && Buffer.compare(Buffer.from(got), Buffer.from(want)) === 0; ++nLossless; continue; }
  if (codec !== 2) { covered = false; continue; }
  idx().set([pOff, delay, n, 0], 0);
  const lp = loops.get(uOff); new Uint32Array(w.memory.buffer, w.loop_ptr(), 3).set(xf && lp ? [lp[0], lp[1], xf] : [0, 0, 0]);
  const g = w.decode_sound(0, 1000);
  if (g !== uBytes / 33) { covered = false; console.log("  entry", i, "n", n, "pOff", pOff, "pBytes", pBytes, "delay", delay, "groups", g, "want", uBytes / 33); continue; }
  got = Uint8Array.from(new Uint8Array(w.memory.buffer, w.out_ptr(), uBytes));
  if (xf && lp) { new Uint8Array(w.memory.buffer, w.out_ptr(), uBytes).set(got); w.crossfade(w.out_ptr(), n, lp[0], lp[1], xf); got = Uint8Array.from(new Uint8Array(w.memory.buffer, w.out_ptr(), uBytes)); ++xfaded; }
  const a = unpackBfp8(want, 0, n), c = unpackBfp8(got, 0, n), q = snr(a, c); snrSum += q; snrWorst = Math.min(snrWorst, q); ++nMp3;
  if (pBytes > 0 && pOff + pBytes > offPack - offSmp) loopOk = false;
}
check(ordered && covered, `${nPack} entries in order of their offsets, each a sound a zone uses, with its exact BFP8 size, and each MP3 sound decodes to its size`);
check(sameLossless, `${nLossless} lossless entries are the bank's bytes exactly`);
check(nMp3 > 0 && snrSum / nMp3 > 10 && snrWorst > 0, `${nMp3} MP3 sounds against the BFP8 of the bank: mean ${(snrSum / Math.max(nMp3, 1)).toFixed(1)} dB, worst ${snrWorst.toFixed(1)} dB (${xfaded} with the loop crossfade)`);
check(loopOk, 'no MP3 entry reaches past the stream');
// ---- the half-rate sounds of --budget-kb: a looped tone keeps its pitch (the loop is a whole number of its periods before and after) and its loop stays seamless
{
  const { halveSound } = require('./pack_bank.js');
  const period = 64.4, n = 4000, loop = [1288, 3220], x = new Int16Array(n);        // a tone of 64.4 samples a cycle; the loop holds 30 cycles exactly
  for (let i = 0; i < n; i++) x[i] = Math.round(9000 * Math.sin(2 * Math.PI * (i < loop[1] ? i : loop[0] + ((i - loop[0]) % (loop[1] - loop[0]))) / period));
  const h = halveSound(x, n, loop), L = loop[1] - loop[0], L2 = h.loop[1] - h.loop[0];
  const cyc = L / period, per2 = L2 / cyc;                                          // the output's samples a cycle
  let worst = 0; for (let i = h.loop[0]; i < h.loop[1] - 1; i++) worst = Math.max(worst, Math.abs(h.pcm[i] - 9000 * Math.sin(2 * Math.PI * (i - h.loop[0]) / per2 + 2 * Math.PI * h.loop[0] / per2)));
  const seam = Math.abs(h.pcm[h.loop[0]] - 9000 * Math.sin(2 * Math.PI * (h.loop[1] / per2)));
  check(L2 === Math.ceil(L / 2) && h.n === h.loop[1] && h.loop[0] === Math.floor(loop[0] * h.r), `the loop is ${L} samples and becomes ${L2} (ceil of half), the sound ends where the loop does (${h.n})`);
  check(worst < 150 && seam < 150, `the tone is the same tone at about half the rate: the output follows the sine at ${per2.toFixed(3)} samples a cycle within ${worst.toFixed(0)} of 9000, and the loop seam is ${seam.toFixed(0)} off`);
  const step = 0.5 * Math.pow(2, h.cents / 1200);                                    // the engine: an octave up in root key halves the step, the fine tuning multiplies it
  check(Math.abs(step - h.r) < 1e-9, `with the root key an octave up and ${h.cents.toFixed(2)} cents of fine tuning the step is ${step.toFixed(6)} of the original, which is the resampling ratio ${h.r.toFixed(6)}`);
  const flat = halveSound(Int16Array.from({ length: 2000 }, (_, i) => Math.round(8000 * Math.sin(2 * Math.PI * i / 100))), 2000, null);
  check(flat.n === 1000 && flat.cents === 0 && !flat.loop && Math.abs(flat.pcm[500] - 8000 * Math.sin(2 * Math.PI * 1000 / 100)) < 150, `a sound without a loop is 2000 samples and becomes ${flat.n}, with no tuning change`);
}
// and a bank packed with a budget: the largest sounds go to half the rate, the zones follow (root key up, loop and length halved), and the pack still checks out
{
  const Q = pack(new Uint8Array(src), { 'budget-kb': 24 });
  const qv = new DataView(Q.bytes.buffer, Q.bytes.byteOffset), nz2 = qv.getUint16(14, true), oz = qv.getUint32(32, true);
  let ok = Q.halved > 0;
  for (let i = 0; i < nz2; i++) { const o = oz + i * 24, len = qv.getUint32(o + 4, true), ls = qv.getUint32(o + 8, true), le = qv.getUint32(o + 12, true), loop = Q.bytes[o + 17] & 1; if (loop && !(le > ls && le <= len)) ok = false; }
  check(ok && Q.halved > 0, `packed with a 24 KB budget: ${Q.halved} sounds at half rate; every zone's loop lies inside its sound`);
}
// and one packed with --drop-kb: sounds are left out, their zones take another zone's sample of the same preset, every zone still points at a sound the pack table has
{
  const total = P.length, D = pack(new Uint8Array(src), { 'drop-kb': 120, fill: 1, flash: 300000 }), E = pack(new Uint8Array(src), { fill: 1, flash: 300000 });
  const qv = new DataView(D.bytes.buffer, D.bytes.byteOffset), nz2 = qv.getUint16(14, true), oz = qv.getUint32(32, true), op = qv.getUint32(80, true), np = qv.getUint32(84, true);
  const known = new Set(); let mp3 = 0;
  for (let i = 0; i < np; i++) { known.add(qv.getUint32(op + i * 24, true)); if (D.bytes[op + i * 24 + 16] === 2) mp3 += qv.getUint32(op + i * 24 + 4, true); }
  let ok = true, loopOk = true, same = 0;
  for (let i = 0; i < nz2; i++) { const o = oz + i * 24, len = qv.getUint32(o + 4, true), ls = qv.getUint32(o + 8, true), le = qv.getUint32(o + 12, true), loop = D.bytes[o + 17] & 1; if (!known.has(qv.getUint32(o, true))) ok = false; if (loop && !(le > ls && le <= len)) loopOk = false; }
  check(D.dropped > 0 && np === D.sounds && np < E.sounds, `packed with --drop-kb 120: ${D.dropped} of ${E.sounds} sounds left out, ${np} in the pack table`);
  check(ok && loopOk, 'every zone still points at a sound with an entry, and every loop lies inside its sound');
  check(mp3 <= 120 * 1024 + 4096, `the MP3 sounds decode to ${(mp3 / 1024).toFixed(0)} KB, within the 120 KB asked (the cost of a whole bank: ${(E.bytes.length / 1024).toFixed(0)} KB file, ${E.sounds - E.lossless} MP3 sounds)`);
}
// --mp3 none: every sound BFP8, thinned (left out, halved, re-encoded) until the file fits; the halved BFP8 sounds decode to what was encoded
{
  const F = 300000, N = pack(new Uint8Array(src), { mp3: 'none', flash: F });
  const qv = new DataView(N.bytes.buffer, N.bytes.byteOffset), nz2 = qv.getUint16(14, true), oz = qv.getUint32(32, true), op = qv.getUint32(80, true), np = qv.getUint32(84, true);
  const len = new Map(); let mp3 = 0; for (let i = 0; i < np; i++) { len.set(qv.getUint32(op + i * 24, true), qv.getUint32(op + i * 24 + 4, true)); if (N.bytes[op + i * 24 + 16] !== 0) mp3++; }
  let ok = true; for (let i = 0; i < nz2; i++) { const o = oz + i * 24, n = qv.getUint32(o + 4, true), ls = qv.getUint32(o + 8, true), le = qv.getUint32(o + 12, true), at = qv.getUint32(o, true); if (!len.has(at) || Math.ceil(n / 32) * 33 > len.get(at) || ((N.bytes[o + 17] & 1) && !(le > ls && le <= n))) ok = false; }
  check(N.bytes.length <= F && mp3 === 0 && N.halved > 0 && ok, `--mp3 none in ${F} bytes: ${N.bytes.length} bytes, no MP3, ${N.halved} halvings, ${N.dropped} left out; every zone inside a sound of the pack table`);
  // --drop-first 0: fewer bits come before leaving sounds out, so with room enough for 5-bit sounds none is left out
  const B = pack(new Uint8Array(src), { mp3: 'none', flash: F, 'min-bits': 5 }), B0 = pack(new Uint8Array(src), { mp3: 'none', flash: F, 'min-bits': 5, 'drop-first': 0 });
  check(B0.bytes.length <= F && B0.dropped <= B.dropped && B0.narrowed >= B.narrowed, `--drop-first 0: ${B0.dropped} left out (${B.dropped} without it), ${B0.narrowed} with fewer bits (${B.narrowed})`);
  const x = Int16Array.from({ length: 100 }, (_, i) => Math.round(20000 * Math.sin(i / 3))), y = unpackBfp8(encodeBfp8(x), 0, 100);
  check(x.every((v, i) => Math.abs(v - y[i]) <= 128), "BFP8 encoding of a loud sine (shift 8) comes back within half a step");
}
// --mp3-only / --keep: only the picked sounds may be MP3, the kept ones stay BFP8 at full rate, a selector that picks nothing or is not one is an error
{
  const F = 300000, S = pack(new Uint8Array(src), { 'mp3-only': 'drums', keep: 'p0,s3', flash: F, 'min-bits': 5 });
  const drums = l => /(^|; )d\d/.test(l.used), kept = l => /(^|; )p0(\/| )/.test(l.used) || l.no === 3;
  const mp3Outside = S.list.filter(l => l.became.startsWith('mp3') && !drums(l)).length, nMp3 = S.list.filter(l => l.became.startsWith('mp3')).length;
  const keptOk = S.list.filter(kept).every(l => l.became === 'bfp8 (kept)') && S.list.filter(l => l.became.includes('kept')).every(kept);
  check(S.bytes.length <= F && nMp3 > 0 && mp3Outside === 0 && keptOk, `--mp3-only drums --keep p0,s3 in ${F} bytes: ${nMp3} MP3 sounds, all of drum kits; ${S.list.filter(kept).length} kept sounds BFP8 at full rate; ${JSON.stringify(S.shape)}`);
  const changed = S.list.filter(l => /^bfp[5-7]|\//.test(l.became)).length;
  check(S.heard.length === nMp3 + changed, `the preview has what the device plays for the ${nMp3} MP3 sounds and the ${changed} lossless ones with fewer bits or a lower rate`);
  const U = pack(new Uint8Array(src), { mp3: 'oneshot', 'mp3-only': 's3', flash: F, 'min-bits': 5 });
  check(U.list.filter(l => l.became.startsWith('mp3')).every(l => !l.loop || l.no === 3), '--mp3 oneshot with --mp3-only: an MP3 sound has no loop or is one of the picked');
  const throws = o => { try { pack(new Uint8Array(src), o); return false; } catch (e) { return true; } };
  check(throws({ keep: 'q1' }) && throws({ keep: 'p0:x' }) && throws({ 'mp3-only': 'd99' }), 'a selector that is not one, or that picks no sound, is an error');
}
const budget = 536576;
console.log(`file ${P.length} bytes against ${budget} (${(P.length / budget * 100).toFixed(0)} %)`);
console.log(failed ? `${failed} FAILED` : 'all passed'); process.exit(failed ? 1 : 0);
