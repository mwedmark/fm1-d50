// Runs a WASI build of a host tool under Node with the current directory preopened: node run.js tool.wasm args...
'use strict';
const { WASI } = require('node:wasi');
const fs = require('node:fs');
const path = require('node:path');

const wasmPath = process.argv[2];
const wasi = new WASI({ version: 'preview1', args: [path.basename(wasmPath), ...process.argv.slice(3)], preopens: { '.': process.cwd() } });
WebAssembly.instantiate(fs.readFileSync(wasmPath), wasi.getImportObject()).then(({ instance }) => {
    process.exitCode = wasi.start(instance);
});
