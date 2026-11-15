// SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
// A stereo pair is read as the mean of its two channels (not the left one alone); a pair in opposite phase keeps the left one.
//   node test_stereo.js
const fs = require('fs'), path = require('path');
const html = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');
eval(html.match(/<script id="core">([\s\S]*?)<\/script>/)[1] + '\nglobalThis.api = { readSample };');
const read = globalThis.api.readSample;
let failed = 0;
const check = (ok, what) => { if (!ok) ++failed; console.log(ok ? 'ok  ' : 'FAIL', what); };
function font(left, right) {                       // smpl: left then right, 16-bit
  const buf = new ArrayBuffer((left.length + right.length) * 2), dv = new DataView(buf);
  left.concat(right).forEach((v, i) => dv.setInt16(i * 2, v, true));
  return { buf, dv, smplOff: 0, shdr: [
    { start: 0, end: left.length, type: 4, link: 1 }, { start: left.length, end: left.length + right.length, type: 2, link: 0 }, { start: 0, end: 0, type: 1, link: 0 }] };
}
let sf = font([1000, 2000, -3000, 400], [3000, 0, -1000, 400]);
let s = read(sf, sf.shdr[0]);
check([...s].join() === '2000,1000,-2000,400', 'a stereo pair plays as the mean of left and right');
sf = font([1000, -2000, 3000, -4000], [-1000, 2000, -3000, 4000]);
s = read(sf, sf.shdr[0]);
check([...s].join() === '1000,-2000,3000,-4000', 'a pair in opposite phase keeps the left channel');
sf = font([1000, 2000, 3000, 4000], [500, 500]);
s = read(sf, sf.shdr[0]);
check([...s].join() === '750,1250,3000,4000', 'a shorter right channel: the left one alone after its end');
sf = font([5, 6, 7], [9, 9, 9]); sf.shdr[0].type = 1;
check([...read(sf, sf.shdr[0])].join() === '5,6,7', 'a mono sample is unchanged');
console.log(failed ? failed + ' failed' : 'all passed'); process.exit(failed ? 1 : 0);
