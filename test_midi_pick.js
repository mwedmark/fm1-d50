// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// The converter's "Fill from a MIDI file": which instruments and drum kits a MIDI file plays (midiUsage) and which presets of a font that takes (pickForMidi).
// Synthetic files first, then the repository's MIDIs/ against midi_events.js (an independent reading of the same files).
//   node test_midi_pick.js [font.sf2]      (default: 1mgm.sf2, then soundfonts/Roland SC-55 Presets.sf2, for the kits)
const fs = require('fs'), path = require('path');
const html = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');
eval(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] + '\nglobalThis.api = { parseSf2, midiUsage, pickForMidi };');
const A = globalThis.api, { parseMidi } = require('./midi_events.js');
let failed = 0;
const check = (ok, what) => { if (!ok) ++failed; console.log(ok ? 'ok  ' : 'FAIL', what); };
const keys = m => [...m.keys()].sort((a, b) => a - b).join(',');

// A one-track MIDI file from [delta, ...bytes] events; meta and sysex as raw bytes.
const vlq = n => { const o = [n & 127]; while (n >>= 7) o.unshift(n & 127 | 128); return o; };
function smf(...tracks) {
  const out = [0x4d, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 1, 0, tracks.length, 1, 0xe0];
  for (const t of tracks) {
    const body = t.flatMap(([d, ...b]) => [...vlq(d), ...b]).concat([0, 0xff, 0x2f, 0]);
    out.push(0x4d, 0x54, 0x72, 0x6b, body.length >>> 24, body.length >> 16 & 255, body.length >> 8 & 255, body.length & 255, ...body);
  }
  return Uint8Array.from(out);
}
const tempo = [0, 0xff, 0x51, 3, 7, 0xa1, 0x20], text = [0, 0xff, 0x01, 3, 65, 66, 67];
let u = A.midiUsage(smf([tempo, text, [0, 0xc0, 24], [0, 0x90, 60, 100], [10, 0x80, 60, 0], [0, 0xc1, 73], [0, 0x91, 60, 0], [0, 0x99, 38, 100]].map(e => e.flat ? e.flat() : e)));
check(keys(u.melodic) === '24' && keys(u.kits) === '0', 'a note played gives its program; a note-on with velocity 0 is a note off and counts for nothing; channel 10 is the kit 0');
u = A.midiUsage(smf([[0, 0x90, 60, 90]]));
check(keys(u.melodic) === '0', 'a channel that never changed program plays program 0');
// the program change of one track is in force for the notes of another (time order, not track order)
u = A.midiUsage(smf([[0, 0x90, 60, 90]].concat([[100, 0x90, 62, 90]]), [[50, 0xc0, 40]]));
check(keys(u.melodic) === '0,40', 'tracks are merged in time: a program change in another track applies to later notes only');
u = A.midiUsage(smf([[0, 0xc9, 16], [0, 0x99, 36, 100], [0, 0xc9, 8], [0, 0x99, 36, 100]]));
check(keys(u.kits) === '8,16' && !u.melodic.size, 'program changes on channel 10 choose the kits (16, then 8)');
u = A.midiUsage(smf([[0, 0xb2, 0, 120], [0, 0xc2, 24], [0, 0x92, 60, 100], [0, 0xb3, 0, 1], [0, 0xb3, 32, 0], [0, 0xc3, 8], [0, 0x93, 60, 100], [0, 0xb4, 0, 5], [0, 0xc4, 30], [0, 0x94, 60, 100]]));
check(keys(u.kits) === '8,24' && keys(u.melodic) === '30', 'bank select 120, or 1 with 0, before a program change makes a kit on any channel; another bank is melodic');
u = A.midiUsage(smf([[0, 0xc0, 5], [0, 0x90, 60, 100], [0, 0x90, 64, 100], [0, 0xa0, 60, 50], [0, 0xe0, 0, 64], [0, 0xd0, 40], [0, 0xf0, 2, 0x7e, 0xf7], [0, 0x90, 67, 100]]));
check(u.melodic.get(5) === 3, 'aftertouch, bend, channel pressure and sysex between notes do not upset the reading (3 notes)');
check(throws(() => A.midiUsage(Uint8Array.from([1, 2, 3]))), 'a file that is not MIDI is refused');
function throws(f) { try { f(); return false; } catch (e) { return true; } }

// What a font takes
const fonts = [process.argv[2] || '1mgm.sf2', 'soundfonts/Roland SC-55 Presets.sf2'].map(f => path.isAbsolute(f) ? f : path.join(__dirname, f)).filter(f => fs.existsSync(f));
for (const f of fonts) {
  const raw = fs.readFileSync(f), sf = A.parseSf2(raw.buffer.slice(raw.byteOffset, raw.byteOffset + raw.length)), name = path.basename(f);
  const all = sf.presets.map((p, i) => i), slots = take => take.map(i => sf.presets[i].bank === 128 ? 128 + sf.presets[i].preset : sf.presets[i].preset).sort((a, b) => a - b).join(',');
  const kits = sf.presets.filter(p => p.bank === 128).map(p => p.preset).sort((a, b) => a - b);
  let r = A.pickForMidi(sf, all, { melodic: new Map([[0, 5], [24, 3]]), kits: new Map([[0, 9]]) });
  check(slots(r.take) === '0,24,' + (128 + kits[0]) && !r.missing.length, `${name}: piano, guitar and the first kit`);
  r = A.pickForMidi(sf, all, { melodic: new Map(), kits: new Map() });
  check(!r.take.length, `${name}: a file with no notes ticks nothing`);
  if (kits.length > 1) {
    const second = kits[1];
    r = A.pickForMidi(sf, all, { melodic: new Map(), kits: new Map([[second + 1, 4]]) });
    check(slots(r.take) === `${128 + kits[0]},${128 + second}`, `${name}: kit ${second + 1} is played by kit ${second} (the nearest below) and the lowest kit comes along`);
  } else {
    r = A.pickForMidi(sf, all, { melodic: new Map(), kits: new Map([[40, 4]]) });
    check(slots(r.take) === String(128 + kits[0]), `${name}: a font with one kit plays it whatever kit the file asks for`);
  }
  r = A.pickForMidi(sf, all, { melodic: new Map([[127, 1]]), kits: new Map() });
  check(slots(r.take) === (sf.presets.some(p => p.bank === 0 && p.preset === 127) ? '127' : '') && (r.missing.length === 0) === sf.presets.some(p => p.bank === 0 && p.preset === 127), `${name}: a program the font lacks is reported`);
}

// The repository's songs: the same as a replay of midi_events.js's messages
const dir = path.join(__dirname, 'MIDIs');
for (const file of fs.existsSync(dir) ? fs.readdirSync(dir).filter(n => /\.mid$/i.test(n)) : []) {
  const buf = fs.readFileSync(path.join(dir, file)), got = A.midiUsage(buf);
  const st = Array.from({ length: 16 }, () => ({ p: 0, m: 0, l: 0, k: false })), mel = new Map(), kit = new Map();
  for (const [, [s, a, b]] of parseMidi(buf)) {
    const c = s & 15, t = s & 0xf0, x = st[c];
    if (t === 0xb0) { if (a === 0) x.m = b; if (a === 32) x.l = b; } else if (t === 0xc0) { x.p = a; x.k = x.m === 120 || (x.m === 1 && x.l === 0); }
    else if (t === 0x90 && b > 0) { const m = c === 9 || x.k ? kit : mel; m.set(x.p, (m.get(x.p) || 0) + 1); }
  }
  const total = m => [...m.values()].reduce((a, v) => a + v, 0);
  check(keys(got.melodic) === keys(mel) && keys(got.kits) === keys(kit) && total(got.melodic) === total(mel) && total(got.kits) === total(kit),
    `${file}: ${got.melodic.size} instruments, ${got.kits.size} kit(s), ${total(got.melodic) + total(got.kits)} notes, as midi_events.js reads it`);
}
console.log(failed ? failed + ' failed' : 'all passed'); process.exit(failed ? 1 : 0);
