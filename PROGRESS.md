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

### 2026-10-31 - Milestone 2 build: OAM slots, sprite palettes, Window HUD (branch `gbc-port`)

- Found `PLAYER_SHOT_SLOT = 47` and `ENT_OAM_LAST = 43` against a 40-entry `gbc_logic_oam`: the shot was invisible and logic wrote past the array. Now 48 logic slots packed into 40 hardware sprites in `oam_camera()`.
- Sprite palettes set as RGB in `gbc/main.c`; new `src/gbc/hud_gbc.c` (Window HUD, bank 1 tiles 0..23). HUD state moved out of `stubs_m1.c`.
- Build `-Name m2_hud` with `src/gbc/hud_gbc.c` added to the milestone 1 list: all checks pass, bank 0 code ends at $357A. Not run in an emulator; the user playtests.

### 2026-10-31 - Late shot hits: sub-stepped shot (branch `gbc-port`)

- Cause: at 30 Hz logic the shot jumped 12 px per pass and was tested only at the end, so hits registered up to 6 px late. `player_update_shot` now steps 6 px per elapsed tick and runs `player_test_shot` (river banks + `entities_hit_shot`) at each step; NES path unchanged.
- Build: `.\build_gbc.ps1` ok, all checks pass. Not play-tested (user to confirm).

### 2026-10-31 - Entity group starvation on overrun passes (branch `gbc-port`)

- Hypothesis (not measured): when a pass lasts >= 2 frames, `logic_acc` reaches LOGIC_FRAMES every pass, so `entities_update()` never ran (enemies slow/stuttering, stale entities, slow death animation in busy scenes). `gbc/main.c` now runs it after the player group when the previous pass skipped it (`ent_skipped`).
- Build ok. If the symptoms stay, profile the busy-scene pass cost (player + entities + river in one pass) before anything else.

### 2026-10-31 - Jet 4 px lower on GBC (branch `gbc-port`)

- `PLAYER_START_Y` is 222 on SM83 (218 on NES). The view is bottom-anchored, so only the jet's logic Y can move it; 222 is the limit because sprites at y >= 232 are culled. Handoff, `BUGS&FEATS.md` (`[~]`) and the plan's risk note updated.
- `.\build_gbc.ps1` passes all checks; bank 0 code ends at `$35A2`. Not playtested yet.

### 2026-11-01 - Hills/trees glitch closed; host river check revived (branch `gbc-port`)

- The "some side hills and trees glitch" bug no longer reproduces; an earlier change must have fixed it (cause never found). Marked done in `BUGS&FEATS.md`, removed from `HANDOFF.md` next steps.
- `tests/host_river.ps1` no longer built (river.c includes `<gb/gb.h>`, stub existed only as `nes/nes.h`). Added `tests/host/gb/gb.h` forwarding to it. Result: all 19999 rows match `tools/river_model.py` for all four start levels.
