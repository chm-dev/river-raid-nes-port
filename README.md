# River Raid Atari 8bit to NES port

No source code. Reverse engineered from xex binary and ported to nes via gbdk by DeepSeek v4.1 FLASH and Sonnet 5.5
Mostly Altirra and AltirraBridge was used. REA with Ghidra for some minor extractions. 

WIP.

## Current state

Playable: the procedural river (exact Atari generator, bridges and roads), the jet,
the shot, ships, helicopters, jets, fuel depots, the bridge tank and its shell, a
source-faithful HUD, scoring and lives all work.  Still open: sound, respawn at the
last bridge, exact fuel rate and some entity-placement details (see `PROGRESS.md`).

Performance: the river scrolls at 60 Hz, the game logic runs at 30 Hz.  On NTSC
the NES has about 29,780 CPU cycles per frame and the faked scanline split spends
~5.4k of them, so the hot paths are hand-written 6502.  Cruise flight is smooth
(about 0.5% late frames), fast flight (1.5 px/frame) is playable.

## Build and test

```powershell
./build.ps1                      # roms/river_raid.nes (must be 65,552 bytes), .noi, .map
node tests/nes_smoke.mjs         # Mesen checks (start MesenCE-MCP first, see AGENTS.md)
./tests/host_river.ps1           # C generator vs. the Python model (needs zig)
```

`AGENTS.md` has the toolchain rules and the lessons learned, `PROGRESS.md` the
status log and next steps, `PORTING_PLAN.md` the roadmap.

<img width="508" height="476" alt="river" src="https://github.com/user-attachments/assets/382baff0-3d52-4f3e-8823-18b9decd1e3b" />



