# Benchmarks the engine (tools\d50_host\bench.cpp) as the FM-1 builds it (16-bit reverb lines) and prints the time and an output fingerprint.
#   .\tools\d50_host\bench.ps1 [-Patches 16] [-Opt O2]
param([int]$Patches = 64, [string]$Opt = 'O2', [string]$File = 'roms\out\d50_pcm.fmd5',
      [string]$WasiSdk = $(if ($env:WASI_SDK) { $env:WASI_SDK } else { 'D:\git\fm1-sf2\fm1_open\build\tools\wasi-sdk-27.0-x86_64-windows' }))
$ErrorActionPreference = "Continue"
$repo = Resolve-Path "$PSScriptRoot\..\.."
$out = Join-Path $repo 'roms\out'
$c = Join-Path $out 'd50_inflate.o'
& "$WasiSdk\bin\clang.exe" --target=wasm32-wasip1 --sysroot="$WasiSdk\share\wasi-sysroot" -O2 -c "$repo\d50\d50_inflate.c" -o $c
$wasm = Join-Path $out 'bench.wasm'
& "$WasiSdk\bin\clang++.exe" --target=wasm32-wasip1 --sysroot="$WasiSdk\share\wasi-sysroot" -std=c++17 "-$Opt" -fno-exceptions -DD5_REVERB_INT16 -DD5_REVERB_EXTERNAL_POOL `
    -I"$repo\third_party\picofaced5\include" -I"$repo\roms\gen" -I"$repo\d50" "$PSScriptRoot\bench.cpp" $c -o $wasm
if ($LASTEXITCODE) { throw 'compile failed' }
Push-Location $repo
try { node "$PSScriptRoot\run.js" $wasm ($File -replace '\\', '/') $Patches } finally { Pop-Location }
