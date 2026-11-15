// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// The converter's "Fill the flash": the instruments it ticks make a BFP8 bank that fits the FM-1's sample flash (the real bank is built and
// measured), start with the first drum kit and the piano, and a smaller flash takes fewer.
//   node test_fill.js [font.sf2 ...]     (default: 1mgm.sf2 and soundfonts/Roland SC-55 Presets.sf2, the ones that exist)
const fs = require('fs'), path = require('path');
const html = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');
eval(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] + '\nglobalThis.api = { parseSf2, buildBank, writeFm1, bankInfo, estimateSoundBytes, fillFlashPick, trimToFit, FM1_SAMPLE_BANK };');
const A = globalThis.api;
let failed = 0;
const check = (ok, what) => { if (!ok) ++failed; console.log(ok ? 'ok  ' : 'FAIL', what); };
const fonts = process.argv.length > 2 ? process.argv.slice(2) : ['1mgm.sf2', 'soundfonts/Roland SC-55 Presets.sf2'].map(f => path.join(__dirname, f)).filter(f => fs.existsSync(f));
for (const f of fonts) {
  const b = fs.readFileSync(f), sf = A.parseSf2(b.buffer.slice(b.byteOffset, b.byteOffset + b.length)), name = path.basename(f);
  const all = sf.presets.map((p, i) => i).filter(i => sf.presets[i].bank === 0 || sf.presets[i].bank === 128);
  for (const rate of [32000, 44100]) {
    const bytesOf = list => A.estimateSoundBytes(sf, list, rate, 33 / 32);
    const make = list => A.writeFm1(A.buildBank(sf, list, { rate, bits: 8, sea: false, seaBits: 5, bfp: true }), A.bankInfo('fill'));
    const take = A.trimToFit(A.fillFlashPick(sf, all, bytesOf, A.FM1_SAMPLE_BANK * 0.96), list => make(list).length, A.FM1_SAMPLE_BANK);
    const bank = make(take);
    const slots = take.map(i => sf.presets[i].bank + ':' + sf.presets[i].preset);
    console.log(`     ${name} at ${rate} Hz: ${take.length} of ${all.length} presets, estimate ${(bytesOf(take) / 1024).toFixed(0)} KB, bank ${(bank.length / 1024).toFixed(0)} KB`);
    check(bank.length <= A.FM1_SAMPLE_BANK, `${name} ${rate} Hz: the bank made from the ticked instruments fits the flash`);
    check(bank.length > A.FM1_SAMPLE_BANK * 0.8 || take.length === all.length, `${name} ${rate} Hz: and fills most of it (or takes every instrument)`);
    check(slots.includes('0:0') && slots.some(s => s.startsWith('128:')), `${name} ${rate} Hz: the piano and a drum kit are in`);
    const half = A.fillFlashPick(sf, all, bytesOf, A.FM1_SAMPLE_BANK * 0.48);
    check(half.length <= take.length && half.every(i => take.includes(i)) || half.length < take.length, `${name} ${rate} Hz: half the flash ticks fewer instruments`);
  }
}
if (!fonts.length) console.log('skipped: no soundfont');
console.log(failed ? failed + ' failed' : 'all passed'); process.exit(failed ? 1 : 0);
