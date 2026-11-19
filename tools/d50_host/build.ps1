# Builds and runs the D-50 host render: the eight PicoFaceD5 fallback presets as WAV files in roms\out.
# Needs roms\gen\d5_pcm.bin (third_party\picofaced5\tools\d5_extract\d5_make_blob.py on your own PCM ROMs) and the WASI SDK.
param(
    [string]$WasiSdk = $(if ($env:WASI_SDK) { $env:WASI_SDK } else { 'D:\git\fm1-sf2\fm1_open\build\tools\wasi-sdk-27.0-x86_64-windows' })
)
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path "$PSScriptRoot\..\.."
$gen = Join-Path $repo 'roms\gen'
$out = Join-Path $repo 'roms\out'
New-Item -ItemType Directory -Force $out | Out-Null
$wasm = Join-Path $out 'render_presets.wasm'
& "$WasiSdk\bin\clang++.exe" --target=wasm32-wasip1 --sysroot="$WasiSdk\share\wasi-sysroot" -std=c++17 -O2 -fno-exceptions -DD5_HOST_BUILD `
    -I"$repo\third_party\picofaced5\include" -I"$gen" "$PSScriptRoot\render_presets.cpp" -o $wasm
if ($LASTEXITCODE) { throw "compile failed" }
Push-Location $repo
try { node "$PSScriptRoot\run.js" $wasm 'roms/gen/d5_pcm.bin' 'roms/out' } finally { Pop-Location }
