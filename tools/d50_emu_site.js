// A private D-50 emulator page (fm1-emu, MIT, with the D-50 firmware), and the save file with the D-50's data that one imports into it.
//   node tools/d50_emu_site.js [firmware.fwsc] [data.fmd5] [at]     (build_d50.ps1 runs it with the one-core build, dist/d50_emu.fwsc)
//   at: where the data goes in the flash (default 0x7C000, fm1_open's user flash; an fm1-base build: fm1base/build/d50-res.bin at 0x5B000)
// Writes roms/out/emu-site/ (the page: fm1-emu's files from emu/, which start_emu.ps1 fills, a catalog with only the D-50 firmware, and a page
// of its own) and roms/out/d50.fm1save (the firmware, and the D-50's samples and patch bank as flash sectors at the user partition, 0x7C000, as
// make_flash_overlay.js lays them). The page holds no Roland data: the save stays on the computer and is imported with the page's "Import...",
// into that browser only. Both are under roms/, which git ignores.
'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const root = path.join(__dirname, '..');
// the emulator's build (one core: build_d50.ps1), else the last build
const fwsc = process.argv[2] || [path.join(root, 'dist/d50_emu.fwsc'), path.join(root, 'fm1_open/build/dx7/generated/FM1_OFFICIAL_OPEN.fwsc')].find(f => fs.existsSync(f));
const data = process.argv[3] || path.join(root, 'roms/out/d50_pcm.fmd5');
const emu = path.join(root, 'emu');
const site = path.join(root, 'roms/out/emu-site');
const SECTOR = 4096, USER = process.argv[4] ? Number(process.argv[4]) : 0x7C000;

const pkg = fs.readFileSync(fwsc);
const hash = crypto.createHash('sha256').update(pkg).digest('hex');
const built = fs.statSync(fwsc).mtime.toISOString().slice(0, 16).replace('T', ' ');

// ---- the page's files
fs.mkdirSync(site, { recursive: true });
for (const f of fs.readdirSync(site)) fs.rmSync(path.join(site, f), { recursive: true, force: true });   // (the folder may be a test server's)
for (const f of ['worklet.js', 'persistence.js', 'fwsc.js', 'tryout.js', 'style.css', 'fm1_web.wasm']) fs.copyFileSync(path.join(emu, f), path.join(site, f));
// the firmware as base64 in a text file (claude.ai serves no .fwsc): fm1-emu's catalog takes an `embedded` package from an installer page
fs.writeFileSync(path.join(site, 'd50fw.txt'), `D50 = "${pkg.toString('base64')}"\n`);

// main.js, three changes for a page in claude.ai's frame: it shows no dialogs (confirm() is false there: the import would stop), its address
// takes no query string (history.replaceState with one may throw), and it fetches nothing from other sites (the Firmware Hub's list)
let main = fs.readFileSync(path.join(emu, 'main.js'), 'utf8');
const swap = (a, b, n = 1) => {
  const parts = main.split(a);
  if (parts.length - 1 !== n) throw new Error(`main.js: "${a.slice(0, 50)}" found ${parts.length - 1} times, ${n} expected`);
  main = parts.join(b);
};
swap('const CLOCKS = ', '// fm1-d50 (tools/d50_emu_site.js): the frame this page runs in shows no dialogs, and its address takes no query string\n' +
  'const confirm = () => true;\nconst replaceUrl = (u) => { try { history.replaceState(null, "", u); } catch { /* not in this frame */ } };\nconst CLOCKS = ');
swap('history.replaceState(null, "", `?${q}`);', 'replaceUrl(`?${q}`);', 2);
swap('? await Promise.race([hubNewcomers(catalog)', '? await Promise.race([Promise.resolve([])');
fs.writeFileSync(path.join(site, 'main.js'), main);

fs.writeFileSync(path.join(site, 'firmwares.json'), JSON.stringify({
  default: 'd50',
  firmwares: [{ id: 'd50', name: 'D-50', version: built, author: 'fm1-d50', license: 'own use',
    about: 'Roland D-50 (PicoFaceD5 engine) on the FM-1. Without the D-50 data it says NO D-50 DATA: import d50.fm1save.',
    repo: 'https://github.com/mwedmark/fm1-d50', embedded: 'D50', installer: 'd50fw.txt', package: 'd50fw.txt' }],
}, null, 2));

const page = `<title>FM-1 D-50</title>
<meta name="description" content="Private: the M-VAVE FM-1 emulated, running the fm1-d50 firmware.">
<link rel="stylesheet" href="style.css">
<style>
  .d50 ol { margin: 0; padding-left: 20px; color: var(--text); font-size: 14px; display: grid; gap: 6px; }
  .d50 code { font-size: 12.5px; }
  .d50 .warn { color: var(--dim); font-size: 12.5px; margin: 0; }
  header.site .wrap { justify-content: space-between; }
  .private { font-size: 12px; color: var(--dim); border: 1px solid var(--line); border-radius: 999px; padding: 2px 10px; }
</style>
<header class="site">
  <div class="wrap">
    <span class="brand"><span class="logo" aria-hidden="true"><svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.8"><path d="M2 11C4 3 6 3 8 8s4 5 6-3"/></svg></span>FM-1 · D-50</span>
    <span class="private">Privat · firmware ${built}</span>
    <a class="embed-only" id="fullpage" href="./">full page</a>
  </div>
</header>
<main>
  <div class="player">
    <div class="deck">
      <div class="deck-head">
        <h1 id="title">M-VAVE FM-1 (emulated)</h1>
        <span id="status">click Start to boot</span>
      </div>
      <div id="perfwarn" role="alert" hidden>
        <p id="perfwarn-text"></p>
        <div class="acts">
          <button type="button" id="perfwarn-fix" class="primary"></button>
          <button type="button" id="perfwarn-copy">Copy a report</button>
          <button type="button" id="perfwarn-close" aria-label="Dismiss">✕</button>
        </div>
      </div>
      <div class="stage">
        <canvas id="panel" aria-label="FM-1 front panel"></canvas>
        <button id="start">Start</button>
      </div>
      <p class="rotate">Vänd telefonen på sidan för en större synt.</p>
      <p class="hint">
        <span class="keyb"><kbd>A</kbd>…<kbd>'</kbd> spelar C4–F5</span>
        <span class="keyb"><kbd>Z</kbd> <kbd>X</kbd> oktav</span>
        <span class="keyb"><kbd>←</kbd><kbd>→</kbd> SELECT: patch</span>
        <span class="keyb"><kbd>R</kbd> REC: infosidan</span>
        <span class="touch">Flera fingrar spelar ackord · dra en ratt upp / ner</span>
      </p>
    </div>

    <aside class="side bar" aria-label="D-50, firmware, save and MIDI">
      <section class="box d50" aria-labelledby="h-d50">
        <h2 class="eyebrow" id="h-d50">D-50</h2>
        <ol>
          <li>Tryck <b>Start</b>. Utan D-50-data säger skärmen <code>NO D-50 DATA</code>.</li>
          <li><b>Import…</b> nedan: välj <code>d50.fm1save</code> från din dator (en gång; den sparas i den här webbläsaren).</li>
          <li>Spela med tangentbordet eller en MIDI-keyboard. SELECT (<kbd>←</kbd><kbd>→</kbd>) och program change 0–63 väljer patch 11–88.</li>
        </ol>
        <p class="warn">Fjärde raden på skärmen, <code>CPU …% MAX …% OVR …</code>, är firmwarens egen last. Statusraden ovanför synten är emulatorns last på den här datorn.</p>
        <p class="warn">Sidan har ingen Roland-data: PCM och patchar finns bara i din sparfil.</p>
      </section>

      <section class="box" aria-labelledby="h-now">
        <h2 class="eyebrow" id="h-now">Now running</h2>
        <p id="about"></p>
        <label id="fwlabel">Firmware <select id="fw" aria-label="Firmware"></select></label>
        <label>Clock <select id="mhz" aria-label="Emulated clock"></select></label>
        <div class="row">
          <label class="file">Open .fwsc… <input type="file" id="open"></label>
          <label class="file" hidden>DX7 bank… <input type="file" id="bank"></label>
          <span id="bankname" class="dim"></span>
        </div>
      </section>

      <section class="box save-box" aria-labelledby="h-save">
        <h2 class="eyebrow" id="h-save">Your save</h2>
        <span id="save-status" role="status" aria-live="polite">Start to restore saved settings and patches</span>
        <div class="row save-actions">
          <button type="button" id="save-now">Save now</button>
          <button type="button" id="export-save" hidden>Export…</button>
          <label class="file">Import… <input type="file" id="import-save" accept=".json,.fm1save"></label>
        </div>
        <details class="save-controls">
          <summary>More: versions, updates, reset</summary>
          <div class="save-actions">
            <button type="button" id="browse-saves">Browse saved firmwares</button>
            <select id="saved-versions" aria-label="Saved firmware versions"></select>
            <button type="button" id="open-saved-version">Open saved firmware</button>
            <button type="button" id="update-firmware">Check for firmware updates</button>
            <button type="button" id="reset-save">Reset saved flash</button>
          </div>
        </details>
      </section>

      <section class="box" aria-labelledby="h-play-from">
        <h2 class="eyebrow" id="h-play-from">Play from</h2>
        <label id="midiin">MIDI in <span class="sel"><span id="midiact" aria-hidden="true"></span><select id="midi" aria-label="MIDI input"><option value="">off</option><option value="*">all inputs</option></select></span></label>
        <label id="midichl">Channel <select id="midich" aria-label="MIDI channel"><option value="0">as sent</option></select></label>
      </section>
    </aside>
  </div>
  <ul class="cards" id="fwlist" hidden></ul>
</main>
<footer class="site">
  <div class="wrap">
    <div class="about-site">
      <b>FM-1 · D-50</b>
      <span>The emulator is <a href="https://github.com/joakimeriksson/fm1-emu">fm1-emu</a> by Joakim Eriksson (MIT). The firmware is fm1-d50,
        with the PicoFaceD5 engine by Michi71 (GPL-3.0-or-later), for own use. Not made by or affiliated with M-VAVE or Roland.</span>
    </div>
  </div>
</footer>
<script type="module" src="main.js"></script>
`;
fs.writeFileSync(path.join(site, 'index.html'), page);

// ---- the save: the firmware and the D-50's data as flash sectors
const flash = fs.readFileSync(data);
// (or fm1-base's resource region, build/d50-res.bin: its 32-byte head, RES1, then the data)
const fmd5At = flash.toString('latin1', 0, 4) === 'RES1' ? 32 : 0;
if (flash.toString('latin1', fmd5At, fmd5At + 4) !== 'FMD5') throw new Error(`${data} is not D-50 data (tools/d50_pack.js) or fm1-base's resource region`);
const sectors = [];
for (let at = 0; at < flash.length; at += SECTOR) {
  const s = Buffer.alloc(SECTOR, 0xff);
  flash.copy(s, 0, at, Math.min(flash.length, at + SECTOR));
  sectors.push({ at: USER + at, data: s.toString('base64') });
}
const save = { format: 'fm1-emulator-save', version: 1, hash, name: `D-50 ${built}`, package: pkg.toString('base64'), sectors };
fs.writeFileSync(path.join(root, 'roms/out/d50.fm1save'), JSON.stringify(save));
console.log(`${site}: ${fs.readdirSync(site).length} files; roms/out/d50.fm1save: ${sectors.length} sectors, firmware ${hash.slice(0, 12)}`);
