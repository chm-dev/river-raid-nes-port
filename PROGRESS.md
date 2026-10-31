# PROGRESS — GBC port history (append-only)

Append-only log, one `### date - title` entry per session. Do not read it whole; grep it for a keyword.
The NES history is in `archive/nes/PROGRESS.md`.

### 2026-10-31 - Start GBC branch and reorganise docs (branch `gbc-port`)

- Created `gbc-port` from `experiment/gb-feasibility` and merged `main` (which added `GBC_PORT_PLAN.md`). `main` is unchanged.
- Moved the NES-only docs to `archive/nes/` with `git mv` (history kept): HANDOFF, PORTING_PLAN, PROGRESS, BUGS&FEATS, AGENTS, README, MESEN_MCP_NOTES, PERFORMANCE_SUMMARY, LESSONS.
- New `AGENTS.md`, `HANDOFF.md`, `PROGRESS.md`, `BUGS&FEATS.md`, `README.md` and a trimmed `info/LESSONS.md` (Atari RE section and the lessons that carry over).
- `GBC_PORT_PLAN.md` moved from `docs/` to the root. Gearboy MCP notes added to `AGENTS.md`.
- Code is unchanged: no NES code was moved or deleted. No build was run (there is no GBC build yet).

### 2026-10-31 - GBC milestone 1: build script, river, camera, jet (branch `gbc-port`)

- Added `build_gbc.ps1` (compile, MBC5 4-bank link, checks: header `$0143/$0147/$0148/$014D`, `.map` bank windows, WRAM/HRAM ranges, `.noi` symbols). `makebin -h` confirmed `-yC` = CGB only.
- GBC code in `src/gbc/` (`main.c`, `config_gbc.h`, `stubs_m1.c`); shared files got `#ifdef __SDCC_sm83` branches only (`config.h`, `river.h`, `river.c`, `player.c`).
- River: 32-row ring in BG map 0 at `$9800`, scroll value doubles as `SCY`; `river_live` stays 0 on GBC because `river_put()` is a no-op on C paths (symptom of getting this wrong: one row of river, then black).
- Sprites: logic files built with `-Dshadow_OAM=gbc_logic_oam`; `oam_camera()` translates to hardware OAM; sprite tiles in VRAM bank 1. `SOURCE_X_OFFSET` stays 32 (logic X is the Atari HPOS).
- Build line: `.\build_gbc.ps1 -Name m1_jet -RequireSymbols _frame_counter,_gbc_logic_oam -Sources src/gbc/main.c,src/gbc/stubs_m1.c,src/atari_data.c,src/collision.c,src/river.c,src/entities.c,src/entity_gfx.c,src/player.c`. Bank 0 code ends at `$3F6B` (about 150 bytes free).
- Playtest by the user: smooth scrolling, jet steers, camera pans, shooting destroys enemies. Open: invisible shot, black enemy sprites, some glitching hills/trees, jet too high on screen, occasional stutter at fast speed. All in `BUGS&FEATS.md`.
- Not tested: build-check failure paths other than a missing symbol; the GBC-required screen on a plain Game Boy; the river job overrun measurement.
