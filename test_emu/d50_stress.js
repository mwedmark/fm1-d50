// fm1-d50 load test in fm1-emu (paste into the page's console, or run it through the browser tool): Staccato Heaven (patch 61, Dual) with an
// eight-note chord, 16 voices, 32 partials, for 8 s, then the info page's overrun count and the split's times, read off the screen.
//   await d50Stress()   ->   { before, after }: "B<blocks> SHORT <n> OVR <overruns>" and "SPLIT ... W<wait us> J<job us>"
window.d50Stress = async function () {
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const W = 240, l = () => window.__lcd;
  const glyphs = {};
  const cell = (y0, c) => { const rows = []; for (let r = 0; r < 7; r++) { let t = ''; for (let x = 0; x < 5; x++) t += l()[(y0 + r) * W + c * 6 + x] !== l()[W * 239 + 239] ? '#' : '.'; rows.push(t); } return rows.join('|'); };
  const learn = (y0, text) => { for (let c = 0; c < text.length; c++) glyphs[cell(y0, c)] = text[c]; };
  const read = y0 => { let s = ''; for (let c = 0; c < 40; c++) { const k = cell(y0, c); s += glyphs[k] !== undefined ? glyphs[k] : (k.indexOf('#') < 0 ? ' ' : '?'); } return s.trimEnd(); };
  const digits = ['.###.|#...#|#..##|#.#.#|##..#|#...#|.###.', '..#..|.##..|..#..|..#..|..#..|..#..|.###.', '.###.|#...#|....#|...#.|..#..|.#...|#####',
    '#####|...#.|..#..|...#.|....#|#...#|.###.', '...#.|..##.|.#.#.|#..#.|#####|...#.|...#.', '#####|#....|####.|....#|....#|#...#|.###.',
    '..##.|.#...|#....|####.|#...#|#...#|.###.', '#####|....#|...#.|..#..|.#...|.#...|.#...', '.###.|#...#|#...#|.###.|#...#|#...#|.###.', '.###.|#...#|#...#|.####|....#|...#.|.##..'];
  digits.forEach((k, i) => { glyphs[k] = String(i); });
  Object.assign(glyphs, { '##..#|##..#|...#.|..#..|.#...|#..##|#..##': '%', '#...#|#...#|#...#|#...#|#...#|.#.#.|..#..': 'V', '#...#|#...#|#...#|#.#.#|#.#.#|#.#.#|.#.#.': 'W',
    '..###|...#.|...#.|...#.|...#.|#..#.|.##..': 'J', '#...#|#...#|#...#|#####|#...#|#...#|#...#': 'H', '####.|#...#|#...#|####.|#...#|#...#|####.': 'B' });
  const rec = async () => { window.__key(13, true); await sleep(150); window.__key(13, false); await sleep(1500); };
  const onInfo = () => cell(228, 0).indexOf('#') >= 0;                // the info page's last line: "REC BACK ..."
  const info = async () => {
    if (!onInfo()) await rec();
    if (!onInfo()) return ['not on the info page'];
    learn(228, 'REC BACK  ARP SPLIT ON/OFF  SEQ SHARE');
    const r = [read(24), read(140)];
    await rec();                                                          // back to the program page
    return r;
  };
  const before = await info();
  window.__midi([0xC0, 40]);
  await sleep(500);
  const chord = [48, 52, 55, 60, 64, 67, 72, 76];
  for (const n of chord) window.__midi([0x90, n, 100]);
  await sleep(8000);
  for (const n of chord) window.__midi([0x90, n, 0]);
  await sleep(2000);
  const after = await info();
  return { before, after };
};
