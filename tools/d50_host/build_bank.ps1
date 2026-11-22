# Renders the bank in roms\out\d50_pcm.fmd5 to WAV in roms\out: dry as the FM-1 plays it now (no reverb yet) and with the reverb.
#   .\tools\d50_host\build_bank.ps1                      all 64 patches
#   .\tools\d50_host\build_bank.ps1 -Patches 0,8,27      some (0 = patch 11)
param([int[]]$Patches = @(), [string]$File = 'roms\out\d50_pcm.fmd5',
      [string]$WasiSdk = $(if ($env:WASI_SDK) { $env:WASI_SDK } else { 'D:\git\fm1-sf2\fm1_open\build\tools\wasi-sdk-27.0-x86_64-windows' }))
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path "$PSScriptRoot\..\.."
$out = Join-Path $repo 'roms\out'
$sources = @("$PSScriptRoot\render_bank.cpp", "$repo\d50\d50_inflate.c")
# reverb: the engine's float reverb (the reference); fm1: the reverb as the FM-1 builds it (16-bit lines in two segments, d5_effects.h)
foreach ($variant in @(@{ name = 'dry'; defines = @('-DD5_NO_REVERB') }, @{ name = 'reverb'; defines = @() },
                       @{ name = 'fm1'; defines = @('-DD5_REVERB_INT16', '-DD5_REVERB_EXTERNAL_POOL') })) {
    $wasm = Join-Path $out "render_bank_$($variant.name).wasm"
    $objs = @()
    $c = Join-Path $out 'd50_inflate.o'
    & "$WasiSdk\bin\clang.exe" --target=wasm32-wasip1 --sysroot="$WasiSdk\share\wasi-sysroot" -O2 -c $sources[1] -o $c
    & "$WasiSdk\bin\clang++.exe" --target=wasm32-wasip1 --sysroot="$WasiSdk\share\wasi-sysroot" -std=c++17 -O2 -fno-exceptions @($variant.defines) `
        -I"$repo\third_party\picofaced5\include" -I"$repo\roms\gen" -I"$repo\d50" $sources[0] $c -o $wasm
    if ($LASTEXITCODE) { throw 'compile failed' }
    Push-Location $repo
    try { node "$PSScriptRoot\run.js" $wasm ($File -replace '\\', '/') "roms/out/d50_bank_$($variant.name).wav" @($Patches | ForEach-Object { "$_" }) 2>$null }
    finally { Pop-Location }
}
