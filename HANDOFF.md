# HANDOFF — read this first

Short, current, rewritten every session. Rules are in `AGENTS.md`; the plan is `GBC_PORT_PLAN.md`; history is `PROGRESS.md` (grep it, do not read whole).

**Last updated:** 2026-11-01 · **Branch:** `gbc-port` · **Phase:** milestone 2 passed and the user said go. 2-player mode is removed (play-tested). Sound is ported and play-tested. HUD rework done (awaiting play-test confirmation). Next: optional pacing work (step 5), then milestone 5

## Where we are

- The port is GBC-only. The C prototype comes first, then a playtest go/no-go after milestone 2.
- **Build:** `.\build_gbc.ps1` → `roms/river_raid.gb`, the only GB ROM; delete old trial ROMs rather than keeping versions. It checks the header (`$0143=$C0`, MBC5, 4 banks, checksum), every `.map` area against its bank window, and `.noi` symbols.
- **Banks:** player.c is in bank 2 (`-Wf-bo2`, set in `build_gbc.ps1`), entities in bank 1. Bank 0 code ends at `$33D6` (~3.1 KB free). Sound is in bank 3 ($4000-$44DB, ~1.2 KB of 16 KB used). Keep `BANKED` on banked public APIs.
- **GBC code lives in `src/gbc/`** (`main.c`, `config_gbc.h`, `hud_gbc.c`, `sound_gbc.c`). Shared files carry `#ifdef __SDCC_sm83` branches next to the NES code, never replacements, so the milestone 5 merge stays cheap. `build.ps1` is still the NES build.
- **Main loop** (`gbc/main.c`): river every pass; the player group (`player_update`, `collision_update`) runs at 30 Hz (`LOGIC_FRAMES` 2), the entity group (`entities_update`) on the other passes. On overrun passes `ent_skipped` forces `entities_update` after the player group, so it never starves.
- **Drawing:** river ring is 32 rows in BG map 0 (`$9800`); the river scroll value is also `SCY` (256 px wrap), no split interrupt. `river_live` stays 0 on GBC so uploads take the direct `set_bkg_data` / `set_vram_byte` path.
- **Sprites:** logic files compile with `-Dshadow_OAM=gbc_logic_oam` (48 slots, NES coordinates); `oam_camera()` converts them (camera, +16/+8, hide off-view), packs the visible ones into the 40 hardware sprites and sets the VRAM bank 1 bit. Sprite tiles are uploaded with `VBK_REG = 1`. Sprite palettes 0..3 are RGB in `gbc/main.c` (`spr_palette`); `player.c`/`entities.c` still pass NES colour numbers, so main overrides them last.
- **Shot:** on SM83 it steps 6 px per elapsed tick and hit-tests each step (`player_test_shot`).
- **HUD:** `gbc/hud_gbc.c`, Window at WY=128, 20x2 cells, tiles in VRAM bank 1 ids 0..35, updated right after `vsync()`. Layout follows `tests/gb/gb_hud*.png`: row 0 lives / centred score (`hud_put_score`, centred for 1..6 digits) / `Bnn`; row 1 `E` bar `F` centred. Idle (ATTRACT): `HI` top-left and the white high score in the centre, no lives. Per-cell palettes 1/4/5/6; 4..6 swap to the low-fuel colours (red gauge, yellow E/F/Bnn) while `sound_warn_on` (exported from `sound_gbc.c`) is set. Sound: `gbc/sound_gbc.c` in bank 3 (1.2 KB), `BANKED` API, `sound_update()` called right after `vsync()` with the real `game_ticks`;
- **Logic X stays the Atari HPOS**: `SOURCE_X_OFFSET` is 32 on GBC too; only the camera subtracts it.
- Benchmarks (`GBC_PORT_PLAN.md` → Measurements): the DMG bench said river and entity passes fit the budget; the real ROM at fast speed does not (see next steps). Profiler units: 70,224 per frame, M = units / 2 at double speed. `tests/gb/profile_play.mjs` is the recipe (it finds `_player_invuln` in the `.noi`; build with `-ProfExport`).
- `main` is NES-only. The NES project and its docs are in `archive/nes/`.

## Next steps

1. **Go decided (2026-11-01).** 2P removal, sound and the HUD rework are done (HUD play-test pending).
2. **2-player mode removed from the GBC build (done, user play-tested 2026-11-01).** `TWO_PLAYER` (defined in `config.h` only when not `__SDCC_sm83`) gates `pl_two`, `pl_cur`, `pl_other`, `pl_pads`, `player_swap_players`, the `joypad_ex` path and the turn swap in `player.c`; `hud_two`/`hud_cur`/`hud_other_bcd` and `river_swap_checkpoint` use `#ifndef __SDCC_sm83`. The NES code stays for the milestone 5 merge. The attract menu now steps only the start level 0..3 (Up/Down wrap); SELECT is still the no-crash debug start.
3. **Sound port done and play-tested (2026-11-01).** Direct map of `src/sound.c` (2A03) onto GB CH1/CH2/CH4 (NES periods via `G(p)`, noise via `nr43_for[]`; tank and extra-life notes use the hardware envelope). Explosion and crash noise are one octave up by choice (`NOISE_UP` in `sound_gbc.c`; set it to 0 to revert). Still to do: re-measure pacing, since sound adds a banked call per frame.
4. **All bugs from `BUGS&FEATS.md` are fixed and play-tested (2026-11-01).** Notable: camera X in ATTRACT/WAIT comes from the fixed spawn slot `RIVER_SPAWN_SLOT` (28), held without easing (`camera_follow`, `player_center` shares the slot); the pause menu bypasses the camera in `oam_camera` and its cursor tile `HUD_LIFE_TILE` is uploaded in `player_init_graphics`; river road and land colours come from per-row BG attributes in `river_emit_stripe` (palettes 0/2/3); bridge flash is `ent_water_color`. Camera has a 12 px dead zone and pans at 1 px/frame (`GBC_CAM_DEAD`, `GBC_CAM_STEP`; play-tested). Open items: sprite Y, save high score.
5. **Pacing (merged into `gbc-port`, user play-tested 2026-11-01: good, slight stutter on busy late levels is acceptable).** Done: sprites are re-enabled by a VBlank handler (`hud_show_sprites_isr`), not the main loop (an overrun pass left a whole frame without sprites = the blinking after the HUD-hide fix); `oam_camera` walks the arrays by pointer with the pause path split out (`oam_camera_slow`, kept as the reference): 33 to 20 scanlines per pass, output identical (`-OamCheck` build: 572 passes, 0 mismatches). Fast speed went from 0.69 to 0.76 passes/frame, clamps 6 to 5, cruise still 1.00. Findings: `vsync()` always waits for the NEXT VBlank, so a pass over 154 lines costs 2 frames (step function), and a pass over after a 2-tick pass tends to stay over (heavy player pass with budget 3 rows, then the entity pass). Per-pass cost (lines of 154): entities ~43, player ~15, camera+OAM ~20, one raw row 15-58, a tile job up to 94. Tried and rejected (runs are deterministic: two runs gave identical numbers): player-pass budget 3 to 2 (no pass gain, clamps 5 to 55); a scanline-deadline governor (`river_room` gating row starts and job columns, limits 112/84/52 per pass kind, ramp +25 lines per owed row above 3): passes 0.83 but clamps 70-95; gating rows only, job ungated: passes 0.79, clamps 66, cruise clamps 2. So withholding rows to save the frame costs more scroll hitches than it gains; generation needs ~all the slack (6 owed rows = ~4 frames at fast). Not measured: sound and HUD cost before the pass (`pl_rec[1]` is read after them). Next ideas: cut real work (`entities_update` 43 lines, `river_generate_raw_row`, bcall overhead); scheduling alone did not pay; `LOGIC_FRAMES` 3 needs a play-test. `-ProfExport` builds carry the pass log (`pl_rec`); `tests/gb/run_emu.ps1 tests/gb/pass_log.mjs fast` prints it.
**Use the advisor before a performance diagnosis or approach**, with measured evidence in the transcript (profiler numbers, what was ruled out). It is not needed for palettes, the HUD or bank moves, and it does not replace the user's playtest or the build checks.

## Decisions not to reopen

- **GBC only.** DMG is dropped.
- **Fast flight stays at 1.5 px/frame.** NES play-testing found 2.0 worse.
- **Logic keeps NES coordinates.** Only drawing uses the camera. This protects validated Atari behaviour.
- **Exact river tile cache.** 213–228 slots in bank 0 against a measured peak of 182. No atlas.
- **HUD art comes from `charset.bin`.** Do not redraw it.
- **Sound values come from live Atari captures.** Not estimates.
- **The user runs the playtests.** Claude builds the ROM and hands over a checklist.

## Where to look

| Topic | Where |
|---|---|
| Plan, decisions, milestones, risks, Gearboy notes | `GBC_PORT_PLAN.md` |
| Rules, toolchain, Gearboy MCP | `AGENTS.md` |
| River generator and cache | `tools/river_model.py`, `src/river.c`, `tests/host_river.ps1` |
| Sprite and palette generation | `tools/gen_entity_gfx.py` |
| Atari behaviour | `docs/ATARI_RE_SPEC.md`, `docs/atari_disasm_*.txt` |
| Atari RE tools | `info/LESSONS.md` → "Reverse Engineering Tools", `info/ALTIRRA_BRIDGE_NOTES.md` |
| HUD glyphs | `assets/atari/extracted/README.md` |
| Gearboy pacing tools | `tests/gb/` (`profile_play.mjs`, `pass_log.mjs`, `run_emu.ps1`) |
| Bugs and feature ideas | `BUGS&FEATS.md` |
| NES history (only if needed) | `archive/nes/` (see its README) |

## Known gaps

- `river.c` emits harmless `always false` warnings (zero-width margin loops, `RIVER_LEFT_COL` is 0 on GBC).
- The "GBC REQUIRED" screen (`_cpu != CGB_TYPE`) has never been run on a plain Game Boy.
- NES code is still in the tree: `build.ps1`, `tests/nes_*.mjs`, `tests/perf_*`, and NES parts of `src/`. Remove or move it when it gets in the way; `main` keeps it until milestone 5.

## End of session (keep this cheap)

1. **Rewrite and trim this file.** Update the date, move finished items out, and correct the budgets. Delete what is no longer true instead of annotating it. Check the size first: `(Get-Content HANDOFF.md).Count` (target ~100, trim past ~150).
2. **Append one entry to `PROGRESS.md` without reading it.** The closing `'@` must stay at column 0:

```powershell
Add-Content -Path PROGRESS.md -Encoding utf8 -Value @'

### YYYY-MM-DD - Short title (branch `name`)

- What changed and why; ground truth used; build line; tests run and results.
'@
```

3. Do not edit `AGENTS.md` for status.
