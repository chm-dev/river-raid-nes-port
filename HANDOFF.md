# HANDOFF — read this first

Short, current, rewritten every session. Rules are in `AGENTS.md`; the plan is `GBC_PORT_PLAN.md`; history is `PROGRESS.md` (grep it, do not read whole).

**Last updated:** 2026-10-31 · **Branch:** `gbc-port` · **Phase:** milestone 2 built and mostly playtested; remaining checks, then the go/no-go gate

## Where we are

- The port is GBC-only. The C prototype comes first, then a playtest go/no-go after milestone 2.
- **Build:** `.\build_gbc.ps1` → `roms/river_raid.gb`, the only GB ROM; delete old trial ROMs rather than keeping versions. It checks the header (`$0143=$C0`, MBC5, 4 banks, checksum), every `.map` area against its bank window, and `.noi` symbols.
- **Banks:** player.c is in bank 2 (`-Wf-bo2`, set in `build_gbc.ps1`), entities in bank 1. Bank 0 code ends at `$35A2` (~2.6 KB free). Bank 3 is empty; put the next banked module (sound) in 2 or 3 and keep `BANKED` on its public API.
- **GBC code lives in `src/gbc/`** (`main.c`, `config_gbc.h`, `hud_gbc.c`, `stubs_m1.c`). Shared files carry `#ifdef __SDCC_sm83` branches next to the NES code, never replacements, so the milestone 5 merge stays cheap. `build.ps1` is still the NES build.
- **Main loop** (`gbc/main.c`): river every pass; the player group (`player_update`, `collision_update`) runs at 30 Hz (`LOGIC_FRAMES` 2), the entity group (`entities_update`) on the other passes. On overrun passes `ent_skipped` forces `entities_update` after the player group, so it never starves.
- **Drawing:** river ring is 32 rows in BG map 0 (`$9800`); the river scroll value is also `SCY` (256 px wrap), no split interrupt. `river_live` stays 0 on GBC so uploads take the direct `set_bkg_data` / `set_vram_byte` path.
- **Sprites:** logic files compile with `-Dshadow_OAM=gbc_logic_oam` (48 slots, NES coordinates); `oam_camera()` converts them (camera, +16/+8, hide off-view), packs the visible ones into the 40 hardware sprites and sets the VRAM bank 1 bit. Sprite tiles are uploaded with `VBK_REG = 1`. Sprite palettes 0..3 are RGB in `gbc/main.c` (`spr_palette`); `player.c`/`entities.c` still pass NES colour numbers, so main overrides them last.
- **Shot:** on SM83 it steps 6 px per elapsed tick and hit-tests each step (`player_test_shot`); one 12 px jump tested only at its end registered hits late.
- **HUD:** `gbc/hud_gbc.c`, Window at WY=128, 20x2 cells, tiles in VRAM bank 1 ids 0..23, updated right after `vsync()`. Only sound is still stubbed (`stubs_m1.c`).
- **Logic X stays the Atari HPOS**: `SOURCE_X_OFFSET` is 32 on GBC too; only the camera subtracts it.
- Benchmarks (`GBC_PORT_PLAN.md` → Measurements): river and entity passes fit the double-speed budget. Player, HUD, uploads, sound still **not measured**.
- `main` is NES-only. The NES project and its docs are in `archive/nes/`.

## Next steps

1. **Glitching hills and trees**: check with `tests/host_river.ps1` and `tests/compare_tile_rows.mjs` before guessing.
2. **Split `river_run_job` across frames** (fast-speed stutter is acceptable for now); measure first.
3. Measure player, HUD and uploads once they exist.
4. **Later, separate tasks:** HUD rework, remove 2-player mode (both in `BUGS&FEATS.md`); sound port.
5. Known unfinished drawing: bridge flash colour (`ent_water_color` is a no-op), HI score and blink not shown, per-row river colours (milestone 3).
6. **Finish the m2 playtest** (user): fuel gauge, lives and score, crash/respawn/game over back to attract, pause menu (sprite letters, never checked), the late-shot fix, and the jet now 4 px lower (`PLAYER_START_Y` 222 on GBC, the sprite-cull limit; all `[~]` in `BUGS&FEATS.md`). If 4 px does not fix reaction time, the next lever is a thinner HUD Window (`HUD_WY`, +8 px view).

**Use the advisor for steps 1 and 2.** Call it before committing to a diagnosis or approach, again if a fix does not change the symptom, and once before declaring done. Measure first and pass it the evidence (map and tile rows, profiler numbers, what was ruled out): it only knows what is in the transcript. It is not needed for palettes, the HUD or bank moves, and it does not replace the user's playtest or the build checks.

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
| Gearboy benchmarks | `tests/gb/`, `tests/gb/bench/` |
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
