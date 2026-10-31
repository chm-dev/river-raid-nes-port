# Host check of the C river generator against tools/river_model.py.
# Needs zig (zig cc) on PATH.  Checks all four start levels (1/5/20/50).  Usage: ./tests/host_river.ps1 [tile_rows]
param([int]$Rows = 20000, [int]$JobCols = 0)   # JobCols > 0: per-pass job budget (the GBC build uses 6)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $env:TEMP 'river_host'
New-Item -ItemType Directory -Force $out | Out-Null
$jobDef = if ($JobCols) { "-DRIVER_JOB_COLS=$JobCols" } else { '-DRIVER_JOB_COLS=255' }
zig cc -std=c11 -O1 -w -DRIVER_NO_START_ROAD $jobDef -I"$PSScriptRoot/host" -I"$root/src" -include stdlib.h `
    -o "$out/harness.exe" "$PSScriptRoot/host/main.c" "$root/src/river.c" "$root/src/atari_data.c"
if ($LASTEXITCODE) { throw 'host build failed' }
foreach ($variant in 0..3) {
    & "$out/harness.exe" $Rows $variant | Set-Content "$out/rows.txt"
    python "$PSScriptRoot/host/compare.py" "$out/rows.txt" $variant
    if ($LASTEXITCODE) { throw "host river check failed (start level variant $variant)" }
}
