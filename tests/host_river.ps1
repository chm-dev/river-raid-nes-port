# Host check of the C river generator against tools/river_model.py.
# Needs zig (zig cc) on PATH.  Usage: ./tests/host_river.ps1 [tile_rows]
param([int]$Rows = 20000)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $env:TEMP 'river_host'
New-Item -ItemType Directory -Force $out | Out-Null
zig cc -std=c11 -O1 -w -DRIVER_NO_START_ROAD -I"$PSScriptRoot/host" -I"$root/src" -include stdlib.h `
    -o "$out/harness.exe" "$PSScriptRoot/host/main.c" "$root/src/river.c" "$root/src/atari_data.c"
if ($LASTEXITCODE) { throw 'host build failed' }
& "$out/harness.exe" $Rows | Set-Content "$out/rows.txt"
python "$PSScriptRoot/host/compare.py" "$out/rows.txt"
if ($LASTEXITCODE) { throw 'host river check failed' }
