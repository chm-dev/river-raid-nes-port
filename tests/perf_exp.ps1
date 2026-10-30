<#
  perf_exp.ps1 - build the working tree, keep the ROM as build/exp/<name>.nes and
  measure the scroll cadence on the scenarios that matter.  Restarts MesenCE-MCP
  first (persistent Lua scripts survive reloads) and leaves it stopped.

  Run:  ./tests/perf_exp.ps1 <name> [frames]
#>
param([Parameter(Mandatory)][string]$Name, [int]$Frames = 400,
      [string[]]$Scenarios = @('0:Up', '2:Up', '3:', '3:Up'))
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    ./build.ps1 | Select-String '^(ROM|RAM) ' | ForEach-Object { $_.Line }
    $map = Select-String -Path roms/river_raid.map -Pattern '^_CODE_1 |^_XINIT ' | ForEach-Object { $_.Line -replace '\s+', ' ' }
    $map
    New-Item -ItemType Directory -Force build/exp | Out-Null
    Copy-Item roms/river_raid.nes "build/exp/$Name.nes" -Force
    Copy-Item roms/river_raid.noi "build/exp/$Name.noi" -Force
    Get-Process MesenCE-MCP-Windows -ErrorAction SilentlyContinue | Stop-Process -Force -Confirm:$false
    Start-Sleep 1
    $p = Start-Process -FilePath ./MesenCE-MCP/MesenCE-MCP-Windows.exe -PassThru -ArgumentList '--mcp', '--mcp-port=9100', '--enableStdout', '--doNotSaveSettings', '--preferences.pauseWhenInBackground=false', '--preferences.pauseWhenInMenusAndConfig=false', '--preferences.disableOsd=true', '--video.videoFilter=None', '--nes.region=Ntsc' -RedirectStandardOutput build/exp/mesen.out -RedirectStandardError build/exp/mesen.err
    Start-Sleep 3
    foreach ($s in $Scenarios) {
        $v, $b = $s.Split(':')
        node tests/perf_cadence_levels.mjs "build/exp/$Name.nes" "build/exp/$Name.noi" $v $b $Frames 2>&1 | ForEach-Object { "$_" }
    }
    Stop-Process -Id $p.Id -Force -Confirm:$false -ErrorAction SilentlyContinue
} finally { Pop-Location }
