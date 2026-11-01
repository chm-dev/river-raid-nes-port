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

### 2026-11-01 - Fast-speed pacing: measure, oam_camera, job split (branch `gbc-port`)

- First profile of the real ROM in play (Gearboy, double speed, units of 70,224 per frame; `tests/gb/profile_play.mjs`). Fast speed ran 305 passes in 598 frames (0.51 per frame). `SUB_00_0324` = `oam_camera`, 31k units per pass.
- `oam_camera`: 8-bit early-out on hidden slots plus wrap-compare range tests; exhaustive host check over every y/x/camera (0 mismatches). 31k to 7-12k per pass; bank 0 code shrank about 130 bytes.
- `river_run_job`: budget of `RIVER_JOB_COLS` (10) non-solid columns per `river_update` (stripe worth 3), resumes via `river_job_col`; `river_start` runs it unlimited. `tests/host_river.ps1 -JobCols 10/6/3`: all rows match the model for the four start levels. Added `river_clamps` / `river_owed_max` counters (read by `profile_play.mjs`). Budget sweep at fast, 10 s: 6 = 26 clamps, 10 = 5, unlimited = 3; the split is insurance for heavy jobs, not a measured pacing win.
- Result: fast 0.51 to ~0.75 passes per frame (nearly all from `oam_camera`), cruise 1.00. Player-pass generation budget 3 to 2 was tried and reverted (river_update peak 81k to 66k, pass rate unchanged).
- Build: `.\build_gbc.ps1` passes all checks. Corrected the profiler unit note in AGENTS.md and the plan.

### 2026-11-01 - Milestone 2 playtest passed; docs closed out (branch `gbc-port`)

- The user ran the full m2 checklist at the end of the pacing work (fast speed, edges, crash/respawn, roads and bridges, fuel/lives/score, shot): all OK. Stutter bug marked `[x]`.
- The user added open bugs (camera alignment, bridge flash, road colour, grass alternation, smarter camera) and the pause-menu note; kept as is.
- HANDOFF rewritten: next is the go/no-go call, then milestone 3 colour work. Bank 0 ends at `$3538`. The shipped `roms/river_raid.gb` is the plain (non `-ProfExport`) build, `RIVER_JOB_COLS` 10.

### 2026-11-01 - Remove two-player mode from the GBC build (branch `gbc-port`)

- User decision after the m2 playtest: go; keep bugs and HUD rework for later, remove 2P, then sound. `TWO_PLAYER` (config.h, non-sm83 only) gates the 2P state, player swap, `joypad_ex` path and 2P game-number step in `player.c`; `river_swap_checkpoint` and the `hud_two`/`hud_cur`/`hud_other_bcd` externs use `#ifndef __SDCC_sm83`; the GBC HUD no longer defines them. The attract menu steps only the start level 0..3. NES code kept for the milestone 5 merge.
- Build: `.\build_gbc.ps1`, all checks ok; bank 0 code now ends at `$33D6` (was `$3538`). User play-tested: level cycling, START, death/respawn, game over, SELECT debug start all fine.

### 2026-11-01 - GBC sound port (branch `gbc-port`)

- `src/gbc/sound_gbc.c` (bank 3, `-Wf-bo3`): sound.c ported to the GB APU (pulse1->CH1, pulse2->CH2, noise->CH4). Pitch via `G(p)` = 2048 - 131072/111861*(p+1); noise via `nr43_for[]`; tank/bonus notes use the hardware envelope (1/64 s step), other volume changes retrigger. `sound.h` declares the API `BANKED` on SM83. `gbc/main.c` now calls `sound_init()` and `sound_update()` (after `vsync()`, before `game_ticks` is merged). `stubs_m1.c` emptied.
- Build: `.\build_gbc.ps1` passes all checks; bank 3 `_CODE_3` $4000-$44D6. Not play-tested.

- Follow-up 2026-11-01: explosion and crash noise raised one octave (`NOISE_UP`, a second `nr43_for` half). User play-tested the full sound set and passed it. `stubs_m1.c` removed.

### 2026-11-01 - Fix invisible pause menu (branch `gbc-port`)

- Cause: player.c draws the pause letters in NES OAM slots 56..63, but `gbc_logic_oam` had 48 slots (LOGIC_OAM_SLOTS), so those writes went past the array and oam_camera never saw them; only slots 2..4 (cursor, "I", "T") showed. Fix: LOGIC_OAM_SLOTS 64 in src/gbc/main.c (also removes the stray writes past the array).
- `.\build_gbc.ps1` passes all checks. Awaiting user playtest.

### 2026-11-01 - Camera aligned with the jet spawn point before start (branch `gbc-port`)

- Bug: during the scroll-in (PLAYER_STATE_WAIT) the camera followed player.x = PLAYER_START_X; player_center() moves it to the river centre only when the jet appears, so the view panned afterwards. Fix: camera_follow() in src/gbc/main.c targets the river centre at PLAYER_START_Y while in WAIT (same formula as player_center). Marked [~] in BUGS&FEATS.md.
- Build: .\build_gbc.ps1 passes all checks (4 banks, map inside windows). No gameplay test run (user playtests).

### 2026-11-01 - Bridge-hit flash on GBC (branch `gbc-port`)

- Bug: no flash after shooting a bridge with the tank on it. The flash logic (`bridge_flash`, `ent_flash_tick`) was shared, but `ent_water_color` used `set_bkg_palette_entry`, which `entities.c` defines away. Added a `__SDCC_sm83` branch that rewrites BG palette 0 colour 3 (water) with `set_bkg_palette`: dark red RGB(24,5,3) on odd remaining frames for 23 frames, else RGB(6,12,26). NES path unchanged.
- Build: `.\build_gbc.ps1` passes all checks. Not play-tested (marked [~]).

### 2026-11-01 - GBC road and grass colours (branch `gbc-port`)

- Bugs: road green, grass not alternating between bridges. Cause: one BG palette on GBC; the NES per-row palette logic (`river_job_pal`, `river_land_alt`) had no GBC output. Fix: per-map-row BG attribute (VRAM bank 1) written in `river_emit_stripe` only when it changes (`river_slot_attr`), cleared in `river_start`; palettes 0 dark land, 2 bright land, 3 road (grey, yellow line), 1 stays the HUD. `ent_water_color` writes water into all three, only on state change.
- Build: `.\build_gbc.ps1` passes all checks. Not play-tested.

### 2026-11-01 - Player shot drawn 1 px wide (branch `gbc-port`)

- Shot tile in `tools/gen_entity_gfx.py` is now one pixel column (0x10, was 0x18); regenerated `src/entity_gfx.c` (shared with NES). Draw x offset (shot_x - 3) still puts the column at shot_x; hit box unchanged. `.\build_gbc.ps1` passes all checks. Not playtested.

### 2026-11-01 - Pause menu centred on the GBC screen (branch `gbc-port`)

- Bug: the pause menu was horizontally centred only for one camera position and sat too high (rows at screen y -1 and 15, i.e. half off the top). Cause: the menu sprites were NES logic coordinates run through the camera. Fix: while `player_state` is PAUSE, `oam_camera()` copies logic slots 56..63 and 2..4 to hardware OAM untranslated; under `__SDCC_sm83` `src/player.c` uses hardware coordinates (rows at screen y 60 and 76, columns 48..112: centre 72 of 144 and 80 of 160) and the hide band moves to logic scanlines 172..197. NES unchanged.
- Build: `.\build_gbc.ps1` passes all checks. Not playtested (marked [~]).

### 2026-11-01 - Player shot centred on the jet nose (branch `gbc-port`)

- Bug: the 1 px shot appeared about 2 px left of the jet's centre. Cause: the straight jet tile has its nose at column 4 (`0x08`, rows symmetric about column 4) while the shot bar is column 3 of its tile and was drawn at `shot_x - 3`, i.e. at jet x + 3. Fix: draw offset `shot_x - 2` in `src/player.c` (bar at jet x + 4). Tile and 2 px hit box (`shot_x` = jet x + 3) unchanged.
- Build: `.\build_gbc.ps1` passes all checks. Not playtested (marked [~]).
