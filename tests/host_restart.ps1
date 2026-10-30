# Checks river_restart() (the $B1C0 checkpoint choice) against a trace of the real
# game.  Capture the trace first with AltirraBridge running:
#   python tools/atari_checkpoint_trace.py --out build/checkpoint_trace.jsonl
# Needs zig (zig cc) on PATH.  Usage: ./tests/host_restart.ps1 [trace.jsonl]
param([string]$Trace = 'build/checkpoint_trace.jsonl')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $env:TEMP 'river_host'
New-Item -ItemType Directory -Force $out | Out-Null
zig cc -std=c11 -O1 -w -DRIVER_NO_START_ROAD -I"$PSScriptRoot/host" -I"$root/src" -include stdlib.h `
    -o "$out/restart.exe" "$PSScriptRoot/host/restart_main.c" "$root/src/atari_data.c"
if ($LASTEXITCODE) { throw 'host build failed' }
python "$PSScriptRoot/host/compare_restart.py" (Join-Path $root $Trace) "$out/restart.exe"
if ($LASTEXITCODE) { throw 'host restart check failed' }
