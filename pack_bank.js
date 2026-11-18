// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// Packs a BFP8 bank (make_bank.js bfp8, or the converter page) for the live cache of FORMAT.md ("Calculation: a live cache of decoded sounds"):
// the sounds go into the flash as ONE constant-bit-rate mono MP3 stream (ffmpeg, bit reservoir off), a few that MP3 handles badly stay BFP8,
// and a pack table (FORMAT.md, "Proposal: packed sample data") says where each sound is and how to decode it. The zone table is unchanged but
// for the 12 or 16 bytes the longer header moves everything by, so a zone still addresses the sound as in the unpacked bank.
//   node pack_bank.js in.fm1b out.fm1b [--kbit 64] [--flash 536576] [--margin 3072] [--fill 1] [--budget-kb 400] [--max-seconds 0.75] [--trim-loop 1]
// --budget-kb N: the MP3 sounds are to be N KB decoded at most (the cache's pool holds a few: a bank of many long sounds thrashes it). The largest are then played at half the
//   rate: resampled 2:1 (windowed sinc, the loop kept seamless), the zone's root key an octave up and its fine tuning corrected, loop and length halved. The cost is the treble
//   above a quarter of the bank's rate on those sounds. Sounds whose zones have a scale tuning other than 100 % or an exclusive class (percussion), or a root above 115, stay.
// --drop-kb N: the same aim by another means: sounds are left out instead of played at half the rate. A zone whose sound goes plays another zone's sound of its preset (the one that
//   costs the fewest keys' extra distance from its root key, at most 6 semitones; same loop or none), and the sound's decoded KB and MP3 bytes are free for others. Percussion, sounds
//   with a scale tuning or exclusive class, and lossless sounds stay. The flash it frees takes more sounds lossless (--fill 1). Can be combined with --budget-kb (halving comes after).
// --mp3 none: no MP3 at all (every sound BFP8: no cache); --mp3 oneshot: only sounds without a loop may be MP3. What is lossless and does not fit in --flash is thinned:
//   sounds that cost little are left out (as --drop-kb), then the biggest are halved (re-encoded as BFP8), then any sound that can be left out.
//   --drop-first 0 skips the first step: a sound whose neighbour can stand in a few semitones away (a piano's middle samples) loses bits instead.
// --min-bits N (4..7, with --mp3 none or oneshot): a lossless sound may also lose mantissa bits, down to N (codec 4); each step, a bit or a halving, is the one that
//   costs the least error per byte saved.
// --keep SEL, --mp3-only SEL: pick sounds by hand (by program, drum kit, key or number; see select() below), e.g. --mp3-only drums:42-46,49,51,57 --keep p0.
//   --list 1 prints every sound: its number, what it became and the presets and keys that play it.
// --max-seconds N: an MP3 sound without a loop is cut after N seconds, with a short fade-out. --trim-loop 1: a looped MP3 sound is cut at its loop end (off by default:
//   SoundFonts usually loop to the end of the sample, so it saves nothing in 1mgm.sf2). Sounds that stay BFP8 are never cut or halved.
// Needs ffmpeg with libmp3lame. The sounds are taken out of the BFP8 bank (its 44 dB noise is far below the MP3's), so no .sf2 is needed.
// Sounds go lossless (codec 0, the BFP8 as it is) when MP3 would be badly wrong for them (SNR under 12 dB, worst first by dB per byte it costs),
// as many as fit in --flash minus the packed file without them minus --margin; sounds with a very short loop (under 16 samples; the renderings listened to had every shorter-than-512 loop as MP3, crossfade clipped to half the loop) or zones that
// disagree about the loop are lossless always. Pack entries (24 bytes): see FORMAT.md; for the live cache an entry is one sound:
//   unpacked offset = the sound's offset in the unpacked image, unpacked bytes = its exact BFP8 size (entries may leave up to 3 bytes between them),
//   codec 2: packed offset = the byte where the MP3 frame to start decoding at begins (one lead-in frame before the sound's first), packed bytes = to the
//   end of the frame holding the sound's last sample, delay (u16 at 18) = samples of that decode to throw away, crossfade (u16 at 20) = loop crossfade;
//   codec 0: the BFP8 stored, packed bytes = unpacked bytes.
// The converter page (index.html) loads this file too, as FM1_PACK: there opt.encode is LAME in JavaScript (lamejs, bit reservoir off) and
// opt.decode the firmware's minimp3 (mp3_decoder_wasm.js). In Node they default to ffmpeg.
(function () {
const FRAME = 1152, LEAD = 1, XFADE = 256, MIN_LOOP = 16, NEW_HEADER = 92, ENTRY = 24, CUT_FADE = 2048, HALF_TAPS = 12;
const bfpBytes = n => Math.ceil(n / 32) * 33, align4 = n => (n + 3) & ~3;

function unpackBfp8(b, off, n) {                 // n samples from a BFP8 run at b[off]
  const out = new Int16Array(Math.ceil(n / 32) * 32);
  for (let g = 0; g * 32 < n; g++) { const sh = b[off + g * 33]; for (let i = 0; i < 32; i++) out[g * 32 + i] = ((b[off + g * 33 + 1 + i] << 24) >> 24) << sh; }
  return out.subarray(0, n);
}
const ffmpeg = (args, input) => require('child_process').execFileSync('ffmpeg', ['-v', 'error', ...args], { input, maxBuffer: 1 << 29 });
function ffmpegEncode(pcm, kbit, rate = 32000) {
  return ffmpeg(['-f', 's16le', '-ar', String(rate), '-ac', '1', '-i', '-', '-c:a', 'libmp3lame', '-b:a', kbit + 'k', '-reservoir', '0', '-write_xing', '0', '-id3v2_version', '0', '-f', 'mp3', '-'], Buffer.from(pcm.buffer, pcm.byteOffset, pcm.byteLength));
}
function ffmpegDecode(stream, rate = 32000) { const d = ffmpeg(['-f', 'mp3', '-i', '-', '-ar', String(rate), '-f', 's16le', '-'], stream); return new Int16Array(d.buffer.slice(d.byteOffset, d.byteOffset + d.length)); }
// BFP with `bits`-bit mantissas (8: BFP8; 4..7: codec 4): per 32 samples a shift byte, then the 32 mantissas, 8-bit ones a byte each, narrower ones packed low bit first
// (sample i is bits i*bits .. i*bits + bits - 1 of the group's 4 * bits bytes). A sample is mantissa << shift; the shift is the smallest that keeps the rounded mantissas in range.
function encodeBfp(pcm, bits = 8) {
  const G = 1 + 4 * bits, out = new Uint8Array(Math.ceil(pcm.length / 32) * G), hi = (1 << (bits - 1)) - 1, lo = -(1 << (bits - 1));
  for (let g = 0; g * 32 < pcm.length; g++) {
    let sh = 0; const at = i => g * 32 + i < pcm.length ? pcm[g * 32 + i] : 0;
    for (; sh < 15; sh++) { let ok = true; for (let i = 0; i < 32; i++) { const m = Math.round(at(i) / (1 << sh)); if (m < lo || m > hi) { ok = false; break; } } if (ok) break; }
    out[g * G] = sh;
    for (let i = 0; i < 32; i++) {
      const m = Math.max(lo, Math.min(hi, Math.round(at(i) / (1 << sh)))) & ((1 << bits) - 1);
      if (bits === 8) { out[g * G + 1 + i] = m; continue; }
      const bit = i * bits, k = g * G + 1 + (bit >> 3), w = m << (bit & 7);
      out[k] |= w & 255; if (w >> 8) out[k + 1] |= w >> 8;
    }
  }
  return out;
}
const encodeBfp8 = pcm => encodeBfp(pcm, 8);
function unpackBfp(b, off, n, bits = 8) {          // n samples from a run of `bits`-bit groups at b[off]
  if (bits === 8) return unpackBfp8(b, off, n);
  const G = 1 + 4 * bits, out = new Int16Array(n);
  for (let i = 0; i < n; i++) {
    const g = off + (i >> 5) * G, bit = (i & 31) * bits, k = g + 1 + (bit >> 3), w = b[k] | ((k + 1 < b.length ? b[k + 1] : 0) << 8);
    out[i] = (((w >> (bit & 7)) << (32 - bits)) >> (32 - bits)) * (1 << (b[g] & 15));
  }
  return out;
}
const snr = (a, c) => { let s = 0, e = 0; for (let i = 0; i < a.length; i++) { s += a[i] * a[i]; e += (a[i] - c[i]) ** 2; } return 10 * Math.log10(Math.max(s, 1e-9) / Math.max(e, 1e-9)); };

// A sound resampled to about half the rate for the cache budget. Looped: the output loop is ceil(L / 2) long and the grid is anchored on the loop start, so the output at the
// loop end is the output at the start again (the input after the loop end is the loop, wrapped); the rate ratio r is then a little over a half and `cents` makes up for it.
function halveSound(x, n, loop) {
  const L = loop ? loop[1] - loop[0] : 0, L2 = loop ? Math.ceil(L / 2) : 0, r = loop ? L2 / L : 0.5;
  const ls2 = loop ? Math.floor(loop[0] * r) : 0, nOut = loop ? ls2 + L2 : Math.ceil(n * r);
  const at = i => i < 0 ? 0 : loop && i >= loop[1] ? x[loop[0] + (i - loop[1]) % L] : i < n ? x[i] : 0;
  const out = new Int16Array(nOut), K = HALF_TAPS, reach = Math.ceil(K / r);
  for (let j = 0; j < nOut; j++) {
    const pos = loop ? loop[0] + (j - ls2) / r : j / r, i0 = Math.ceil(pos - reach);
    let acc = 0, sum = 0;
    for (let i = i0; i <= pos + reach; i++) {
      const d = (i - pos) * r, win = 0.42 + 0.5 * Math.cos(Math.PI * d / K) + 0.08 * Math.cos(2 * Math.PI * d / K), k = d === 0 ? 1 : Math.sin(Math.PI * d) / (Math.PI * d);
      const c = k * win; acc += c * at(i); sum += c;
    }
    out[j] = Math.max(-32768, Math.min(32767, Math.round(acc / sum)));
  }
  return { pcm: out, n: nOut, loop: loop ? [ls2, ls2 + L2] : null, cents: loop ? 1200 * Math.log2(2 * r) : 0, r, ls2 };
}

function pack(src, opt = {}) {
  const encode = opt.encode || ffmpegEncode, decode = opt.decode || ffmpegDecode;
  const MIN = opt["min-loop"] === undefined ? MIN_LOOP : opt["min-loop"], kbit = opt.kbit || 64, flash = opt.flash || 536576, margin = opt.margin === undefined ? 3072 : opt.margin;
  const dv = new DataView(src.buffer, src.byteOffset, src.byteLength), u32 = o => dv.getUint32(o, true);
  if (String.fromCharCode(...src.subarray(0, 4)) !== 'FM1B' || dv.getUint16(4, true) !== 1 || dv.getUint16(6, true) !== 8) throw new Error('not a version 1 BFP8 bank');
  const rate = dv.getUint32(8, true);            // the stream is coded at the bank's rate: 32 kHz for the firmware, 44.1 kHz for the experiment build (RUNNING.md, 2e)
  if (rate !== 32000 && rate !== 44100) throw new Error('the MP3 stream is coded at 32 or 44.1 kHz; the bank is at ' + rate);
  // where each frame starts: at 32 kHz every frame has the same bytes, at 44.1 kHz some carry a padding byte (bit 1 of the third header byte)
  const frameStarts = mp3 => { const at = [0], base = Math.floor(144 * kbit * 1000 / rate); let p = 0; while (p < mp3.length) { if (mp3[p] !== 0xff) throw new Error('the stream is not whole frames from byte 0 (an ID3 tag?)'); p += base + ((mp3[p + 2] >> 1) & 1); at.push(p); } if (p !== mp3.length) throw new Error('the stream does not end on a frame'); return at; };
  const nz = dv.getUint16(14, true), offZone = u32(32), offSmp = u32(36), pitch = u32(16), hasInfo = pitch >= 80 && u32(76) !== 0, oldHdr = hasInfo ? 80 : pitch;
  if (oldHdr < 76 || oldHdr > NEW_HEADER) throw new Error('header of ' + oldHdr + ' bytes');
  const shift = NEW_HEADER - oldHdr, ZS = 24;
  // the sounds: distinct (offset, frames) of the zones, with their loops
  const map = new Map();
  for (let i = 0; i < nz; i++) {
    const o = offZone + i * ZS, off = u32(o), n = u32(o + 4), ls = u32(o + 8), le = u32(o + 12), loop = src[o + 17] & 1;
    let s = map.get(off);
    if (!s) map.set(off, s = { off, n, loop: loop ? [ls, le] : null, mixed: false });
    else { if (s.n !== n) throw new Error('two zones disagree about a sound\'s length at ' + off); if (!!s.loop !== !!loop || (loop && (s.loop[0] !== ls || s.loop[1] !== le))) s.mixed = true; }
  }
  let sounds = [...map.values()].sort((a, b) => a.off - b.off);
  const capN = opt["max-seconds"] ? Math.max(256, Math.floor(opt["max-seconds"] * rate / 32) * 32) : 0;
  for (const s of sounds) {
    s.pcm = unpackBfp8(src, s.off, s.n); s.bytes = bfpBytes(s.n);
    // the samples of it that are decoded when it is MP3; the part after is cut. A looped sound is cut at its loop end: the engine wraps the position there (also in the
    // release), so nothing after it is ever read, and the decoded sound is that much smaller in the cache. Off unless --trim-loop 1.
    const trim = opt["trim-loop"] || 0;
    s.cap = s.n; s.fade = false;
    if (s.loop && trim && s.loop[1] > 0 && s.loop[1] < s.n) s.cap = s.loop[1];
    else if (capN && !s.loop && s.n > capN) { s.cap = capN; s.fade = true; }
    s.mp = s.pcm;
    if (s.cap < s.n) {
      s.mp = Int16Array.from(s.pcm.subarray(0, s.cap));
      const f = s.fade ? Math.min(CUT_FADE, s.cap >> 1) : 0;
      for (let i = 0; i < f; i++) s.mp[s.cap - f + i] = Math.round(s.mp[s.cap - f + i] * (1 - (i + 1) / f));
    }
  }
  // the stream of a set of sounds: each starts at its place in the stream, plus the codec delay found by lining the start up with the source
  function stream(list, capped) {                                // capped: the sounds as they are decoded (cut), else whole
    const pcmOf = s => capped ? s.mp : s.pcm;
    let total = 0; for (const s of list) { s.t = total; total += pcmOf(s).length; }
    const all = new Int16Array(total + 3 * FRAME); for (const s of list) all.set(pcmOf(s), s.t);
    const mp3 = encode(all, kbit, rate);
    frameStarts(mp3);
    const back = decode(mp3, rate), n = Math.min(6000, total);   // the 3 frames of silence after the last sound give the encoder its flush and the decoder its last samples
    let delay = 0, best = -Infinity;
    for (let lag = 0; lag < 2400; lag++) { let c = 0; for (let i = 0; i < n; i++) c += back[lag + i] * all[i]; if (c > best) { best = c; delay = lag; } }
    return { mp3, back, delay };
  }
  let first = stream(sounds);
  for (const s of sounds) { s.snr = snr(s.pcm, first.back.subarray(s.t + first.delay, s.t + first.delay + s.n)); s.why = (s.loop && s.loop[1] - s.loop[0] < MIN) ? "loop" : s.mixed ? "mixed" : s.n < 64 ? "short" : ""; s.always = !!s.why; }
  // lossless: always the ones that must be; then the worst by dB under 12 per byte of extra, while the file fits
  const perSecond = sounds.length ? kbit * 125 : 0;
  const extra = s => s.bytes - s.cap / rate * perSecond;
  const tables = offSmp + shift;
  // --mp3 oneshot: only sounds without a loop may be MP3 (the cache then holds short sounds that are played again and again); --mp3 none: no MP3 at all. The rest is lossless,
  // and when that does not fit in --flash, sounds are left out and halved (below) until it does.
  const mp3Mode = opt.mp3 === 'none' ? 'none' : opt.mp3 === 'oneshot' ? 'oneshot' : 'all';
  const dropped_ = new Set(), repoint = new Map();                              // repoint: zone -> the zone it takes its sample from
  const numbered = sounds.slice();                                              // the sounds as --list and s<N> number them
  // what the zones say: which preset and keys each belongs to, and which sounds may be left out or halved
  const nPre = dv.getUint16(12, true), offPre = u32(24), offKeys = u32(28), offBank = u32(20);
  const extrasAt = pitch >= 52 && u32(48) ? u32(48) : 0, extraSize = pitch >= 56 ? u32(52) : 4;
  const perc = new Set(); for (let k = 128; k < 256; k++) { const pr = dv.getUint16(offBank + k * 2, true); if (pr !== 0xffff) perc.add(pr); }
  const zones = [], preOf = new Int32Array(nz).fill(-1);
  for (let p = 0; p < nPre; p++) {
    const first = dv.getUint16(offPre + p * 24, true), cnt = dv.getUint16(offPre + p * 24 + 2, true), notes = Array.from({ length: cnt }, () => []);
    for (let k = 0; k < 128; k++) { const e = src[offKeys + p * 128 + k]; if (e < cnt) notes[e].push(k); }
    for (let j = 0; j < cnt && first + j < nz; j++) preOf[first + j] = p;
    zones.push({ first, cnt, notes });
  }
  const zonesOf = new Map(); for (let i = 0; i < nz; i++) { const k = u32(offZone + i * ZS); if (!zonesOf.has(k)) zonesOf.set(k, []); zonesOf.get(k).push(i); }
  const scaleOf = i => { const e = extrasAt ? extrasAt + i * extraSize : 0; return e && extraSize >= 4 ? dv.getUint16(e + 2, true) : 100; };
  const exclOf = i => { const e = extrasAt ? extrasAt + i * extraSize : 0; return e ? src[e + 1] & 127 : 0; };
  // may go: not forced, no percussion zone, every zone with a scale tuning of 100 % and no exclusive class
  // --keep SEL: those sounds stay lossless as they are (BFP8, full rate, never left out); --mp3-only SEL: only those sounds may be MP3, the rest are lossless (and thinned
  // to fit as with --mp3 none; with --mp3 oneshot: the sounds without a loop and these). SEL is a comma list of terms: p<N> (melodic program N), d<N> (drum kit N), drums (every kit), s<N> (sound N, as --list 1 numbers them).
  // A preset term may take keys: d0:42 or p0:60-72; bare numbers and ranges after a term add keys to it, so drums:42-46,49,51,57 is the hi-hats and cymbals of every kit.
  // A sound is picked when one of its zones is in a picked preset and plays one of the keys (any key, when the term has none).
  const progs = new Map(); for (let k = 0; k < 256; k++) { const pr = dv.getUint16(offBank + k * 2, true); if (pr !== 0xffff && pr < nPre) { if (!progs.has(pr)) progs.set(pr, []); progs.get(pr).push(k); } }
  const notesOf = i => { const p = preOf[i]; return p < 0 ? [] : zones[p].notes[i - zones[p].first]; };
  const select = (spec, flag) => {
    const terms = [], range = x => { const r = /^(\d+)(?:-(\d+))?$/.exec(x); if (!r) throw new Error(`--${flag}: "${x}" is not a key or key range`); return [+r[1], r[2] === undefined ? +r[1] : +r[2]]; };
    for (const tok of String(spec).split(',').map(x => x.trim()).filter(Boolean)) {
      const m = /^(drums|[pds]\d+)(?::(.+))?$/i.exec(tok);
      if (m) { terms.push({ what: m[1].toLowerCase(), keys: m[2] ? [range(m[2])] : [] }); continue; }
      if (!terms.length) throw new Error(`--${flag}: "${tok}" is not p<N>, d<N>, drums or s<N>`);
      terms[terms.length - 1].keys.push(range(tok));
    }
    const inPreset = (w, p) => w === 'drums' ? perc.has(p) : (progs.get(p) || []).includes((w[0] === 'd' ? 128 : 0) + Number(w.slice(1)));
    const picks = t => terms.some(term => term.what[0] === 's' ? numbered[Number(term.what.slice(1))] === t
      : zonesOf.get(t.off).some(i => preOf[i] >= 0 && inPreset(term.what, preOf[i]) && (!term.keys.length || notesOf(i).some(k => term.keys.some(([a, b]) => k >= a && k <= b)))));
    const set = new Set(sounds.filter(picks));
    if (!set.size) throw new Error(`--${flag} ${spec}: no sound is picked`);
    return set;
  };
  const keepSet = opt.keep !== undefined ? select(opt.keep, 'keep') : null, mp3Set = opt['mp3-only'] !== undefined ? select(opt['mp3-only'], 'mp3-only') : null;
  for (const s of sounds) s.keep = !!(keepSet && keepSet.has(s));
  const forced = s => s.keep || mp3Mode === 'none' || (mp3Set ? !mp3Set.has(s) && (mp3Mode !== 'oneshot' || !!s.loop) : mp3Mode === 'oneshot' && !!s.loop);
  let lossless = new Set(sounds.filter(s => s.always || forced(s)));
  let rest = sounds.filter(s => !lossless.has(s)), pk = rest.length ? stream(rest, true) : { mp3: new Uint8Array(0), back: new Int16Array(0), delay: 0 };
  const sizeNow = () => tables + align4(pk.mp3.length) + [...lossless].filter(s => !dropped_.has(s)).reduce((a, s) => a + align4(s.bytes), 0) + (sounds.length - dropped_.size) * ENTRY;
  const mayGo = t => !t.always && !t.keep && zonesOf.get(t.off).every(i => preOf[i] >= 0 && !perc.has(preOf[i]) && scaleOf(i) === 100 && !exclOf(i));
  const fits = (t, h) => zonesOf.get(t.off).every(i => { const zo = offZone + i * ZS, fine = dv.getInt16(zo + 20, true) + Math.round(h.cents + (t.half ? t.half.cents : 0)); return src[zo + 16] + 12 * ((t.levels || 0) + 1) <= 115 && scaleOf(i) === 100 && !exclOf(i) && fine >= -99 && fine <= 99; });
  const MAXOFF = 6;                                                             // no note is played from a sample more than 6 semitones further from its root than its own
  const best = (i, gone) => {                                                   // the zone to take zone i's sample from, and what it costs (Infinity: none)
    const p = preOf[i], z = zones[p], keys = z.notes[i - z.first], root = src[offZone + i * ZS + 16], loop = src[offZone + i * ZS + 17] & 1;
    let bj = -1, bc = Infinity;
    for (let j = z.first; j < z.first + z.cnt && j < nz; j++) {
      const sj = map.get(u32(offZone + j * ZS));
      if (j === i || gone.has(sj) || sj === map.get(u32(offZone + i * ZS)) || (src[offZone + j * ZS + 17] & 1) !== loop) continue;
      const rj = src[offZone + j * ZS + 16]; let c = 0, bad = false;
      for (const k of keys) { const d = Math.abs(k - rj) - Math.abs(k - root); if (d > MAXOFF) bad = true; c += Math.max(0, d); }
      c += keys.length ? 0.5 * keys.length : 4; if (!bad && c < bc) { bc = c; bj = j; }
    }
    return { j: bj, c: bc };
  };
  // leave sounds out while goOn(): the cheapest loss per byte first, none costing more than maxRatio per byte; onDrop(sound) after each
  const dropWhile = (eligible, goOn, maxRatio, onDrop) => {
    while (goOn()) {
      let pick = null, pickRatio = Infinity, pickMap = null;
      for (const t of sounds) {
        if (dropped_.has(t) || !eligible(t) || !mayGo(t)) continue;
        let cost = 0; const m = new Map();
        for (const i of zonesOf.get(t.off)) { const b = best(i, new Set([...dropped_, t])); cost += b.c; m.set(i, b.j); if (b.j < 0) { cost = Infinity; break; } }
        const ratio = cost / bfpBytes(t.cap); if (ratio < pickRatio) { pick = t; pickRatio = ratio; pickMap = m; }
      }
      if (!pick || pickRatio === Infinity || pickRatio > maxRatio) break;
      dropped_.add(pick); for (const [i, j] of pickMap) repoint.set(i, j);
      if (onDrop) onDrop(pick);
    }
  };
  let halved = 0;
  const halveable = (t, maxLevel) => !t.always && !t.keep && !dropped_.has(t) && (t.levels || 0) < maxLevel && t.cap >= 512 && (!t.loop || t.loop[1] - t.loop[0] >= 64);
  // play the biggest of the eligible sounds at half the rate while goOn() (the zones' root key and fine tuning make up for it); lossless ones are re-encoded as BFP8
  const halveWhile = (eligible, goOn, onHalf, maxLevel = 1) => {
    for (const t of sounds.filter(t => eligible(t) && halveable(t, maxLevel)).sort((a, b) => bfpBytes(b.cap) - bfpBytes(a.cap))) {
      if (!goOn()) break;
      const h = halveSound(t.lpcm || t.pcm, t.cap, t.loop);
      if (h.n < 32 || !fits(t, h)) continue;
      const before = bfpBytes(t.cap);
      h.cents += t.half ? t.half.cents : 0; if (t.half) h.r *= t.half.r; t.levels = (t.levels || 0) + 1; if (!t.half) t.loop0 = t.loop; t.half = h; t.mp = h.pcm; t.cap = h.n; if (h.loop) t.loop = h.loop; ++halved;
      if (lossless.has(t)) { t.bytes = bfpBytes(h.n); t.lpcm = h.pcm; }
      if (onHalf) onHalf(t, before);
    }
  };
  // Lossless sounds made smaller one step at a time, the step that does the least harm per byte first: a mantissa bit less (stored as codec 4, down to minBits) or
  // half the rate (at most twice). Harm is the error power over the sound's power: the noise of the narrower groups (measured), and the treble the halving takes
  // (hfLoss below, weighted towards the treble).
  const power = x => { let e = 0; for (let i = 0; i < x.length; i++) e += x[i] * x[i]; return e / Math.max(1, x.length); };
  const noise = (x, bits) => { const y = unpackBfp(encodeBfp(x, bits), 0, x.length, bits); let e = 0, s = 0; for (let i = 0; i < x.length; i++) { s += x[i] * x[i]; e += (x[i] - y[i]) ** 2; } return s ? e / s : 0; };
  const harm = (t, x, bits, lost) => noise(x, bits) + lost;
  // the treble a halving takes, as the ear weighs it more than its power: the share of the sound's slope (first difference, +6 dB an octave) above the new rate's
  // Nyquist (a quarter of the bank's rate at one halving, an eighth at two), by a windowed-sinc low pass at the bank's rate
  const hfCache = new Map();
  const hfLoss = (t, level) => {
    const key = t.off + ':' + level; if (hfCache.has(key)) return hfCache.get(key);
    const x = t.pcm, n = t.n, fc = 0.5 / (1 << (level + 1)), K = 24;
    const d = new Float64Array(n); for (let i = 1; i < n; i++) d[i] = x[i] - x[i - 1];
    const h = new Float64Array(2 * K + 1); let hs = 0;
    for (let k = -K; k <= K; k++) { const a = 2 * Math.PI * fc * k, win = 0.42 + 0.5 * Math.cos(Math.PI * k / K) + 0.08 * Math.cos(2 * Math.PI * k / K); h[k + K] = (k ? Math.sin(a) / (Math.PI * k) : 2 * fc) * win; hs += h[k + K]; }
    let all = 0, hi = 0;
    for (let i = 0; i < n; i++) { let lp = 0; for (let k = -K; k <= K; k++) { const j = i + k; if (j >= 0 && j < n) lp += h[k + K] / hs * d[j]; } all += d[i] * d[i]; hi += (d[i] - lp) ** 2; }
    const r = all ? hi / all : 0; hfCache.set(key, r); return r;
  };
  const shrinkWhile = (eligible, goOn, minBits) => {
    const p0 = new Map(), opts = new Map();
    const options = t => {                                                      // the next steps for t: [{ kind, bytes saved, harm added, apply }]
      const out = [], x = t.lpcm || t.pcm, bits = t.bits || 8, lost = t.lost || 0, now = harm(t, x, bits, lost), g = Math.ceil(t.cap / 32);
      if (bits > minBits) out.push({ save: g * 4, dh: harm(t, x, bits - 1, lost) - now, apply: () => { t.bits = bits - 1; t.bytes = g * (1 + 4 * t.bits); } });
      if (halveable(t, 2)) {
        const h = halveSound(x, t.cap, t.loop);
        if (h.n >= 32 && fits(t, h)) {
          if (!p0.has(t)) p0.set(t, power(t.pcm));
          const lost2 = hfLoss(t, (t.levels || 0) + 1), save = g * (1 + 4 * bits) - Math.ceil(h.n / 32) * (1 + 4 * bits);
          out.push({ save, dh: harm(t, h.pcm, bits, lost2) - now, apply: () => {
            h.cents += t.half ? t.half.cents : 0; if (t.half) h.r *= t.half.r; t.levels = (t.levels || 0) + 1; if (!t.half) t.loop0 = t.loop; t.half = h; t.mp = h.pcm; t.cap = h.n; if (h.loop) t.loop = h.loop; ++halved;
            t.lpcm = h.pcm; t.lost = lost2; t.bytes = Math.ceil(h.n / 32) * (1 + 4 * bits);
          } });
        }
      }
      return out.filter(o => o.save > 0);
    };
    while (goOn()) {
      let pick = null, score = Infinity;
      for (const t of sounds) {
        if (dropped_.has(t) || !eligible(t) || t.always || t.keep) continue;
        if (!opts.has(t)) opts.set(t, options(t));
        for (const o of opts.get(t)) { const sc = Math.max(o.dh, 0) / o.save; if (sc < score) { score = sc; pick = [t, o]; } }
      }
      if (!pick) break;
      pick[1].apply(); opts.delete(pick[0]);
    }
  };
  if (mp3Mode !== 'all' || mp3Set || keepSet) {                                                   // the lossless sounds must fit: first the cheap losses, then fewer bits or half rate, then any
    const over = () => sizeNow() > flash - margin, mine = t => lossless.has(t);
    if (opt['drop-first'] !== 0) dropWhile(mine, over, 2 / 1024);                                   // --drop-first 0: fewer bits or half rate before any sound is left out
    shrinkWhile(mine, over, opt["min-bits"] || 8);
    dropWhile(mine, over, Infinity);
  }
  const weak = sounds.filter(s => !lossless.has(s) && s.snr < 12).sort((a, b) => (12 - b.snr) / Math.max(extra(b), 1) - (12 - a.snr) / Math.max(extra(a), 1));
  let budget = flash - margin - sizeNow();
  for (const s of weak) { const cost = align4(s.bytes) - s.cap / rate * perSecond; if (cost <= budget) { lossless.add(s); budget -= cost; } }
  // --fill 1: the flash that is left takes more sounds as BFP8, worst MP3 first: a sound that plays in place needs no room in the cache's pool (RUNNING.md, 2f)
  const fillPass = () => { for (const s of sounds.filter(s => !lossless.has(s) && !dropped_.has(s)).sort((a, b) => a.snr - b.snr)) { const cost = align4(s.bytes) - s.cap / rate * perSecond; if (cost <= budget) { lossless.add(s); budget -= cost; } } };
  if (opt.fill) fillPass();
  // --drop-kb: the MP3 sounds are to be that many KB decoded at most, by leaving sounds out: a zone of a sound that is left out plays another sound of its preset (the zone takes
  // that zone's sample, loop, root key and fine tuning; its keys, envelope and pan stay). The cheapest loss per KB freed goes first; the flash it frees takes more sounds lossless.
  const decodedNow = () => sounds.filter(t => !lossless.has(t) && !dropped_.has(t)).reduce((a, t) => a + bfpBytes(t.cap), 0);
  const dropBytes = (opt["drop-kb"] || 0) * 1024;
  if (dropBytes) dropWhile(t => !lossless.has(t), () => decodedNow() > dropBytes, Infinity, pick => { budget += pick.cap / rate * perSecond; if (opt.fill) fillPass(); });
  // a zone that took its sample from a zone whose sound went later: follow it to a zone that stays
  for (const [i, j0] of repoint) { let j = j0; while (repoint.has(j)) j = repoint.get(j); repoint.set(i, j); }
  const dropped = dropped_.size; sounds = sounds.filter(t => !dropped_.has(t));
  // --budget-kb: the MP3 sounds are to be that many KB decoded at most: the largest are played at half the rate
  const budgetBytes = (opt["budget-kb"] || 0) * 1024;
  if (budgetBytes) { let total = decodedNow(); halveWhile(t => !lossless.has(t), () => total > budgetBytes, (t, before) => { total += bfpBytes(t.cap) - before; }); }
  if (lossless.size > sounds.filter(s => s.always).length || halved || dropped) { rest = sounds.filter(s => !lossless.has(s)); pk = rest.length ? stream(rest, true) : { mp3: new Uint8Array(0), back: new Int16Array(0), delay: 0 }; }
  // _wide (tests): the narrow sounds written as the BFP8 of what their narrow groups decode to (exactly the same samples), codec 0
  if (opt._wide) for (const t of sounds) if (lossless.has(t) && (t.bits || 8) < 8) { t.wide = unpackBfp(encodeBfp(t.lpcm || t.pcm, t.bits), 0, t.cap, t.bits); t.bits = 8; t.bytes = bfpBytes(t.cap); }
  // the file
  let end = tables; const mp3At = end; end += align4(pk.mp3.length);
  for (const s of sounds) if (lossless.has(s)) { s.stored = end; end += align4(s.bytes); }
  const offPack = end; end += sounds.length * ENTRY;
  const out = new Uint8Array(end), o = new DataView(out.buffer);
  out.set(src.subarray(0, oldHdr)); out.set(src.subarray(oldHdr, offSmp), NEW_HEADER);
  o.setUint16(4, 2, true); o.setUint16(6, 8 | 0x10, true);
  for (const f of [16, 20, 24, 28, 32, 36, 44, 48, 56, 68]) if (u32(f)) o.setUint32(f, u32(f) + shift, true);
  if (hasInfo) o.setUint32(76, u32(76) + shift, true);
  o.setUint32(40, end, true); o.setUint32(80, offPack, true); o.setUint32(84, sounds.length, true); o.setUint32(88, ENTRY, true);
  for (let i = 0; i < nz; i++) {
    const p = offZone + shift + i * ZS, s = map.get(u32(offZone + i * ZS));
    o.setUint32(p, o.getUint32(p, true) + shift, true);
    if (s.cap < s.n) o.setUint32(p + 4, s.cap, true);                           // the zone ends where the decoded sound does
    if (s.half) {                                           // played at about half the rate: an octave up in root key, the loop and the fine tuning follow
      if (s.half.loop) { o.setUint32(p + 8, s.half.loop[0], true); o.setUint32(p + 12, s.half.loop[1], true); }
      out[p + 16] += 12 * s.levels;
      o.setInt16(p + 20, o.getInt16(p + 20, true) + Math.round(s.half.cents), true);
    }
  }
  for (const [i, j] of repoint) {                                               // the zones of the sounds left out: the sample of another zone of their preset (as patched above)
    const p = offZone + shift + i * ZS, q = offZone + shift + j * ZS;
    out.copyWithin(p, q, q + 17); out[p + 17] = (out[p + 17] & ~1) | (out[q + 17] & 1); out.copyWithin(p + 20, q + 20, q + 22);
  }
  out.set(pk.mp3, mp3At);
  const starts = frameStarts(pk.mp3), mp3Frames = starts.length - 1;
  sounds.forEach((s, i) => {
    const e = offPack + i * ENTRY;
    o.setUint32(e, s.off + shift, true); o.setUint32(e + 4, lossless.has(s) ? s.bytes : bfpBytes(s.cap), true);   // a cut sound is as long as its decode
    if (lossless.has(s)) { const narrow = (s.bits || 8) < 8; out.set(narrow ? encodeBfp(s.lpcm || s.pcm, s.bits) : s.wide ? encodeBfp8(s.wide) : s.lpcm ? encodeBfp8(s.lpcm) : src.subarray(s.off, s.off + s.bytes), s.stored); o.setUint32(e + 8, s.stored - tables, true); o.setUint32(e + 12, s.bytes, true); out[e + 16] = narrow ? 4 : 0; out[e + 17] = narrow ? s.bits : 0; if (narrow) o.setUint32(e + 4, bfpBytes(s.cap), true); return; }
    const t0 = s.t + pk.delay, f0 = Math.max(0, Math.floor(t0 / FRAME) - LEAD), f1 = Math.min(mp3Frames - 1, Math.floor((t0 + s.cap - 1) / FRAME));
    s.discard = t0 - f0 * FRAME;
    o.setUint32(e + 8, mp3At - tables + starts[f0], true); o.setUint32(e + 12, starts[f1 + 1] - starts[f0], true);
    out[e + 16] = 2; o.setUint16(e + 18, s.discard, true); o.setUint16(e + 20, s.loop && s.loop[0] >= 64 ? XFADE : 0, true);
  });
  // heard: what the device plays for each MP3 sound (its unpacked offset and samples), for a preview of the packed bank
  // and for each lossless sound the device plays other than as the bank had it (fewer bits, half rate): what its groups decode to
  const changed = sounds.filter(s => lossless.has(s) && ((s.bits || 8) < 8 || s.half));
  const heard = [...rest, ...changed].map(s => {
    const dec = lossless.has(s) ? (s.wide || unpackBfp(encodeBfp(s.lpcm || s.pcm, s.bits || 8), 0, s.cap, s.bits || 8)) : pk.back.subarray(s.t + pk.delay, s.t + pk.delay + s.cap);
    if (!s.half) return { off: s.off, pcm: Int16Array.from({ length: s.n }, (_, i) => i < s.cap ? dec[i] : 0) };
    const ls = s.loop0 ? s.loop0[0] : 0, r = s.half.r, ls2 = s.half.ls2;    // the preview plays the original zone: the half-rate decode is stretched back (linear)
    return { off: s.off, pcm: Int16Array.from({ length: s.n }, (_, i) => { const j = s.loop0 ? ls2 + (i - ls) * r : i * r, a = Math.max(0, Math.floor(j)), f = j - a; return a + 1 < dec.length ? Math.round(dec[a] * (1 - f) + dec[a + 1] * f) : a < dec.length ? dec[a] : 0; }) };
  });
  for (const t of dropped_) lossless.delete(t);                                // the counts below are of the sounds in the file
  const narrowed = sounds.filter(t => lossless.has(t) && (t.bits || 8) < 8).length;
  const shape = {}; for (const t of sounds) { const k = (lossless.has(t) ? 'bfp' + (t.bits || 8) : 'mp3') + (t.levels ? '/' + (1 << t.levels) : ''); shape[k] = (shape[k] || 0) + 1; }   // e.g. bfp6/2: 6 bits at half rate
  // list: each sound (numbered as s<N> picks it), what it became and which presets and keys play it
  const nameOf = p => { let s = ''; for (let k = 0; k < 20; k++) { const c = src[offPre + p * 24 + 4 + k]; if (!c) break; s += String.fromCharCode(c); } return s; };
  const keyRuns = ks => { const u = [...new Set(ks)].sort((a, b) => a - b), r = []; for (const k of u) { const l = r[r.length - 1]; if (l && k === l[1] + 1) l[1] = k; else r.push([k, k]); } return r.map(([a, b]) => a === b ? a : a + '-' + b).join(','); };
  const list = numbered.map((t, no) => {
    const by = new Map(); for (const i of zonesOf.get(t.off)) if (preOf[i] >= 0) { if (!by.has(preOf[i])) by.set(preOf[i], []); by.get(preOf[i]).push(...notesOf(i)); }
    const used = [...by].map(([p, ks]) => `${(progs.get(p) || []).map(k => k < 128 ? 'p' + k : 'd' + (k - 128)).join('/') || 'preset ' + p} ${nameOf(p)}${ks.length ? ' ' + keyRuns(ks) : ''}`).join('; ');
    const became = dropped_.has(t) ? 'left out' : (lossless.has(t) ? 'bfp' + (t.bits || 8) : 'mp3') + (t.levels ? '/' + (1 << t.levels) : '') + (t.keep ? ' (kept)' : t.always ? ` (${t.why})` : '');
    const presets = [...by].map(([p, ks]) => ({ progs: (progs.get(p) || []).map(k => k < 128 ? 'p' + k : 'd' + (k - 128)), name: nameOf(p), keys: [...new Set(ks)].sort((a, b) => a - b) }));
    return { no, off: t.off, seconds: t.n / rate, loop: !!t.loop, became, used, presets };
  });
  return { bytes: out, heard, narrowed, shape, list, kbit, sounds: sounds.length, lossless: lossless.size, always: sounds.filter(s => s.always).length, why: sounds.filter(s => s.always).reduce((m, s) => (m[s.why] = (m[s.why] || 0) + 1, m), {}), mp3Bytes: pk.mp3.length, shift,
    unpackedBytes: src.length + shift, halved, dropped, cut: sounds.filter(s => !lossless.has(s) && s.cap < s.n).length, meanSnr: sounds.filter(s => !lossless.has(s)).reduce((a, s) => a + s.snr, 0) / Math.max(1, sounds.length - lossless.size),
    lossSnr: sounds.filter(s => lossless.has(s)).map(s => s.snr) };
}
if (typeof module !== 'undefined') module.exports = { pack, unpackBfp8, unpackBfp, halveSound, encodeBfp, encodeBfp8 };
else globalThis.FM1_PACK = { pack, unpackBfp8, unpackBfp, halveSound, encodeBfp, encodeBfp8 };

if (typeof require !== 'undefined' && typeof module !== 'undefined' && require.main === module) {
  const fs = require('fs');
  const a = process.argv.slice(2), pos = a.filter((x, i) => !x.startsWith('--') && !(i && a[i - 1].startsWith('--')));
  const opt = {}; for (let i = 0; i < a.length; i++) if (a[i].startsWith('--')) { const v = a[++i]; opt[a[i - 1].slice(2)] = isNaN(Number(v)) ? v : Number(v); }
  if (pos.length !== 2) { console.error('usage: node pack_bank.js in.fm1b out.fm1b [--kbit 64] [--flash 536576] [--margin 3072] [--fill 1] [--max-seconds 0.75] [--trim-loop 1] [--budget-kb 400] [--drop-kb 300] [--mp3 oneshot|none] [--min-bits 5] [--drop-first 0] [--keep SEL] [--mp3-only SEL] [--list 1]'); process.exit(1); }
  const r = pack(new Uint8Array(fs.readFileSync(pos[0])), opt);
  fs.writeFileSync(pos[1], r.bytes);
  if (opt.list) for (const l of r.list) console.log(`s${l.no}\t${l.seconds.toFixed(2)} s${l.loop ? ' loop' : ''}\t${l.became}\t${l.used}`);
  console.log(`${pos[1]}: ${r.bytes.length} bytes (the unpacked bank would be ${r.unpackedBytes}); ${r.sounds} sounds, ${r.sounds - r.lossless} in a ${r.mp3Bytes}-byte MP3 stream (mean ${r.meanSnr.toFixed(1)} dB), ${r.lossless} lossless (because of: ${JSON.stringify(r.why)})` +
    (r.halved ? `; ${r.halved} halvings (half rate, twice: a quarter)` : '') + (r.dropped ? `; ${r.dropped} sounds left out` : '') + (r.narrowed ? `; ${r.narrowed} lossless sounds with fewer than 8 bits` : '') + `; ${JSON.stringify(r.shape)}` +
    (r.cut ? `; ${r.cut} MP3 sounds cut (at the loop end, or at ${opt['max-seconds'] || 'no'} s)` : '') +
    (r.bytes.length > (opt.flash || 536576) ? ' - TOO BIG' : ''));
}
})();
