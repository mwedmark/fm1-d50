# Builds and runs the D-50 SysEx test (test_sysex.cpp): RQ1 and DT1, and the handshake (issue #51), on the real glue (d50/d50.cpp).
# Needs your own data file (roms\out\d50_fm1base.fmd5: README.md) and the WASI SDK.
param(
    [string]$Data = 'roms\out\d50_fm1base.fmd5',
    [string]$WasiSdk = $(if ($env:WASI_SDK) { $env:WASI_SDK } else { 'D:\git\fm1-sf2\fm1_open\build\tools\wasi-sdk-27.0-x86_64-windows' })
)
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path "$PSScriptRoot\..\.."
$out = Join-Path $repo 'roms\out'
New-Item -ItemType Directory -Force $out | Out-Null
$wasm = Join-Path $out 'test_sysex.wasm'
& "$WasiSdk\bin\clang++.exe" --target=wasm32-wasip1 --sysroot="$WasiSdk\share\wasi-sysroot" -std=c++17 -O1 -fno-exceptions -fno-rtti -nostdinc++ `
    -DD5_REVERB_INT16 -DD5_REVERB_EXTERNAL_POOL -DFM1_BASE=1 `
    -I"$repo\d50" -I"$repo\d50\cxx" -I"$repo\third_party\picofaced5\include" -I"$repo\roms\gen" "$PSScriptRoot\test_sysex.cpp" -o $wasm
if ($LASTEXITCODE) { throw "compile failed" }
Push-Location $repo
try {
    node --no-warnings -e "const {WASI}=require('wasi');const fs=require('fs');const w=new WASI({version:'preview1',args:['test_sysex',process.argv[1]],preopens:{'.':'.'}});WebAssembly.instantiate(fs.readFileSync(process.argv[2]),{wasi_snapshot_preview1:w.wasiImport}).then(({instance})=>process.exit(w.start(instance)))" $Data $wasm
    if ($LASTEXITCODE) { throw "test_sysex failed" }
} finally { Pop-Location }
