# HANDOFF — read this first

Short, current, rewritten every session. Rules are in `AGENTS.md`; the plan is `GBC_PORT_PLAN.md`; history is `PROGRESS.md` (grep it, do not read whole).

**Last updated:** 2026-10-31 · **Branch:** `gbc-port` · **Phase:** milestone 1 played and accepted by the user (smooth, jet steers, camera pans); milestone 2 next

## Where we are

- The port is GBC-only. The C prototype comes first, then a playtest go/no-go after milestone 2.
- **Build:** `.\build_gbc.ps1 -Name m1_jet -RequireSymbols _frame_counter,_gbc_logic_oam -Sources src/gbc/main.c,src/gbc/stubs_m1.c,src/atari_data.c,src/collision.c,src/river.c,src/entities.c,src/entity_gfx.c,src/player.c` → `roms/m1_jet.gb`. It checks the header (`-Wm-yC` confirmed: `$0143=$C0`, MBC5, 4 banks, checksum), every `.map` area against its bank window, and `.noi` symbols. Failure paths are tested only for a missing symbol.
- **Banks:** player.c is in bank 2 (-Wf-bo2, set in uild_gbc.ps1), entities in bank 1. Bank 0 code ends at $3102 (~3.8 KB free). Bank 3 is empty; put the next banked module (HUD, sound) in 2 or 3 and keep BANKED on its public API.
- **GBC code lives in `src/gbc/`** (`main.c`, `config_gbc.h`, `stubs_m1.c`). Shared files carry `#ifdef __SDCC_sm83` branches next to the NES code, never replacements, so the milestone 5 merge stays cheap. `build.ps1` is still the NES build.
- **How it draws:** river ring is 32 rows in BG map 0 (`$9800`); the river scroll value is also `SCY` (256 px wrap), no split interrupt. `river_live` stays 0 on GBC so uploads take the direct `set_bkg_data` / `set_vram_byte` path (`river_put()` is a no-op on C paths).
- **Sprites:** logic files compile with `-Dshadow_OAM=gbc_logic_oam` and keep NES coordinates; `oam_camera()` in `gbc/main.c` converts them (camera, +16/+8, hide off-view) and sets the VRAM bank 1 bit. Sprite tiles are uploaded with `VBK_REG = 1`.
- **Logic X stays the Atari HPOS**: `SOURCE_X_OFFSET` is 32 on GBC too; only the camera subtracts it.
- Linked but stubbed: HUD state and all sound (`stubs_m1.c`). `entities.c` is linked, so enemies already spawn.
- Benchmarks (`GBC_PORT_PLAN.md` → Measurements): river and entity passes fit the double-speed budget. Player, HUD, uploads, sound still **not measured**.
- `main` is NES-only. The NES project and its docs are in `archive/nes/`.

## Next steps

1. **Milestone 2:** shot visible, entity colours (`tools/gen_entity_gfx.py` GBC palettes), fuel, collisions, minimal HUD (Window layer, 16 px at the bottom), full game to game over, no sound. Open bugs are in `BUGS&FEATS.md`.
2. Move the jet lower (`GBC_VIEW_TOP_SCANLINE`, `config_gbc.h`); the user found the playfield too short ahead of it. This also decides reaction time, the plan's main risk.
3. Split `river_run_job` across frames (fast-speed stutter is acceptable for now); measure first. Check the glitching hills and trees with `tests/host_river.ps1` before guessing.
4. Measure player, HUD and uploads once they exist.

**Use the advisor for steps 2 and 3** (the glitching hills/trees and the `river_run_job` split). Call it before committing to a diagnosis or approach, again if a fix does not change the symptom, and once before declaring done. Measure first and pass it the evidence (map and tile rows, profiler numbers, what was ruled out): it only knows what is in the transcript. It is not needed for palettes, the HUD or the bank 0 move, and it does not replace the user's playtest or the build checks.

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

- `river.c` now emits harmless `always false` warnings (zero-width margin loops, `RIVER_LEFT_COL` is 0 on GBC).
- The "GBC REQUIRED" screen (`_cpu != CGB_TYPE`) has never been run on a plain Game Boy.
- The pause menu draws with sprites and has not been checked on GBC.
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
