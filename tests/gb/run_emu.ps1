# Start a fresh headless Gearboy on roms/river_raid.gb, run one node test against it, stop it.
#   .\tests\gb\run_emu.ps1 tests/gb/pass_log.mjs fast
# The scripts press Start themselves, so they need a freshly booted ROM every time.
param([Parameter(Mandatory)][string]$Script, [string]$Mode = 'fast', [string]$Extra = '')
Set-Location (Join-Path $PSScriptRoot '..\..')
Get-Process Gearboy -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Process -FilePath gearboy/Gearboy.exe -WindowStyle Hidden -ArgumentList '--headless', '--mcp-http',
    '--mcp-http-port', '7777', 'roms/river_raid.gb', 'roms/river_raid.noi'
Start-Sleep 3
try { node $Script $Mode $Extra 2>&1 } finally { Get-Process Gearboy -ErrorAction SilentlyContinue | Stop-Process -Force }
