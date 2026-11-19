# Builds the D-50 firmware twice, as fm1-sf2's workflow does for its emulator: with both cores for the FM-1, with one core for the emulator.
# Emulating the second core costs the computer more, and the emulator plays better on one; the FM-1 needs both (D50.md).
#   .\build_d50.ps1
#     dist\d50.fwsc       the FM-1's (both cores), packed with the D-50 data into dist\install\ for install.html
#     dist\d50_emu.fwsc   the emulator's (one core): the last build, so start_emu.ps1 runs it, and tools\d50_emu_site.js makes the private
#                         page (roms\out\emu-site\) and the save to import into it (roms\out\d50.fm1save) from it
#   -Data roms\out\d50_pcm.fmd5   the D-50 data (tools\d50_pack.js; your own ROMs, never committed)
param([string]$Data = 'roms\out\d50_pcm.fmd5', [string]$Toolchain = 'D:\JL\pi32')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
Push-Location $root
try {
    if (!(Test-Path $Data)) { throw "No D-50 data at $Data (tools\d50_pack.js makes it)" }
    .\build_fw.ps1 -Name d50 -Rate 32000 -LiveCache:$false -DualCore on -Toolchain $Toolchain
    Copy-Item fm1_open\build\dx7\build-identity.json dist\d50.identity.json -Force   # the install page's manifest needs the FM-1 build's id
    .\build_fw.ps1 -Name d50_emu -Rate 32000 -LiveCache:$false -DualCore off -Toolchain $Toolchain
    node make_install.js $Data --fwsc=dist\d50.fwsc --identity=dist\d50.identity.json
    if ($LASTEXITCODE) { throw 'make_install.js failed' }
    node tools\d50_emu_site.js dist\d50_emu.fwsc $Data
    if ($LASTEXITCODE) { throw 'tools\d50_emu_site.js failed' }
    Write-Host 'FM-1: dist\install\ (install.html), both cores. Emulator: start_emu.ps1 and roms\out\emu-site\ + roms\out\d50.fm1save, one core.'
} finally { Pop-Location }
