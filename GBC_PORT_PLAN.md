# Game Boy Color port plan

Status as of 2026-10-31: milestone 1 built and accepted in a playtest (river, camera, jet). **Next step: milestone 2** (see `HANDOFF.md`).

## Decisions
- **GBC-only.** DMG is dropped. Original Game Boy and Game Boy Pocket are not targets. Adding DMG later would mean redoing the art for 4 shades, shrinking the tile cache and writing SM83 assembly.
- **Cheap prototype first.** Use the existing C code with no new assembly, then decide from a playtest whether the port is worth finishing.
- **Fast speed stays 1.5 px/frame**, as on the NES build.
- **Work on the `gbc-port` branch until the go decision.** It was branched from `experiment/gb-feasibility` with this plan merged from `main`. `main` stays NES-only until milestone 5.
- **The user runs the playtests.** Build the ROM and hand over a checklist.

## Current state
- `experiment/gb-feasibility` holds benchmarks, not a port. Its `src/` is patched only as far as they need and **no longer builds the NES ROM**:
  - asm guards narrowed from `__SDCC` to `__SDCC_mos6502`. Confirmed: mos6502 builds define that macro, so NES builds would still select the asm. SDCC defines `__SDCC` on every target, so the old guards hid the C paths from SM83.
  - `<gb/gb.h>` replaces `<nes/nes.h>`.
  - palette calls stubbed and `.attr` changed to `.prop` in `src/entities.c`.
- Benchmarks are in `tests/gb/bench/`; `tests/gb/gearboy_client.mjs` is the emulator client. The full numbers are in the branch's two commit messages.
- View mockups are in `tests/gb/` (on `gbc-port` and `main`): `gb_view_center.png`, `gb_view_3panel.png`, `sim_gb_view.py`. They use the existing same-size tiles at 1:1 pixels with a zoomed-in viewport.

## Measurements
Real river and entity code on the C paths, timed in Gearboy at 1.5 px/frame with 200 warm-up pass pairs. No player, HUD, sound or VRAM writes. The run was in DMG mode, so double speed is calculated from the cycle counts, not run. Budget at double speed: 35,112 M-cycles per frame.

| M-cycles per pass | Level 1 | Level 5 | Level 20 | Level 50 |
|---|---|---|---|---|
| Player pass, avg / max | 19,610 / 54,560 | 19,429 / 56,496 | 20,563 / 62,624 | 20,287 / 55,520 |
| Entity pass, avg / max | 16,685 / 25,520 | 17,059 / 26,048 | 20,020 / 28,464 | 22,209 / 30,976 |
| Passes over 35,112 (of 800), player / entity | 22 / 0 | 15 / 0 | 24 / 0 | 23 / 0 |

- Top cost: `river_convert_row`, 33% of run time, about 6,270 M per call.
- The river-only benchmark peaked at about 25.5k M in `river_run_job`, which is why the overruns look like whole tile jobs landing on one frame. Confirm this in milestone 1.
- River tiles uploaded per tile row (30,000 rows, host generator): average 4.98, peak 15.
- Largest number of river tiles uploaded within a window of rows: 182 (18 rows), 195 (20), 211 (22), 264 (28).

**Not measured:** player, HUD, VRAM upload and sound cost; anything actually running in double speed.

## Technical design
- **Coordinates:** game logic stays in NES coordinates (X = Atari HPOS, Y = NES scanlines 30..239). Only drawing goes through a camera, which protects validated Atari behaviour.
- **Camera:** horizontal scroll follows the jet, clamped as in `sim_gb_view.py`. The 24 river columns map to BG map columns 0..23, so `SCX` runs 0..32 and the NES land margin columns aren't needed.
- **Vertical view:** 128 px playfield showing the bottom of the NES world. Make the view offset a tunable constant, since it also sets the jet's height on screen.
- **Ring:** 32 rows, 256 px, because `SCY` wraps at 256. Change `RIVER_ROWS`, `RIVER_RING_ROWS`, `RIVER_SCROLL_HEIGHT` and recheck `river_slot_at_scanline`, `river_slot_top`.
- **HUD:** 16 px at the bottom in the Window layer. This replaces the NES split-screen handlers. The layout must be redone for 20 columns; reuse the glyph art from `assets/atari/extracted/charset.bin`.
- **Sprites:** add the hardware offsets (OAM Y + 16, X + 8) and hide anything outside the view. Audit the `232u`, `240u` and `250u` limits in `entities.c` and `player.c`. Put sprite tiles in VRAM bank 1.
- **River tile cache:** about 213–228 slots in bank 0 (256 minus 3 solid tiles and 25–40 HUD tiles), against a measured peak of 182. Keep the size a parameter.
- **Uploads:** replace `river_put` and the NES transfer-buffer path, which is a no-op on the C paths. `set_bkg_data` is good enough to start; hardware DMA is available if it's too slow.
- **Tile job:** split `river_run_job` across frames (a columns-per-frame limit).
- **Colour:** set per-row palettes through the BG attribute map, the equivalent of `river_emit_extra`'s NES attributes. Start from colours close to the Atari; `tools/gen_entity_gfx.py` will need GBC palette output.
- **Boot:** call `cpu_fast()`. Set the header to GBC-only (check the `makebin` flag, believed to be `-Wm-yC`) and show a "GBC required" screen when `_cpu != CGB_TYPE`, since an original Game Boy ignores the flag.

### What can be reused
| Code | Lines | Verdict |
|---|---|---|
| `atari_data`, `river_lut`, `collision` | ~355 | Reuse as is |
| `river.c` | 2,446 | Reuse the generator and classifier on the C paths; replace upload code and ring constants. `river_convert_row_c` already writes GB tile byte order |
| `entities.c` | 1,578 | Reuse the logic; change sprite drawing and hide limits |
| `player.c` | 750 | Mostly reuse; change sprite positions, stub two-player mode and the pause menu |
| `entity_gfx` | 181 | Reuse the tiles after checking their byte order; replace palette data |
| `hud.c` | 663 | Rewrite the layout |
| `main.c` | 130 | Rewrite; simpler without the split |
| `sound.c` | 460 | Done: `src/gbc/sound_gbc.c` (bank 3), play-tested 2026-11-01 |
| 6502 assembly (~1,070 lines), NES tests | | Not reusable. `tests/host_river.ps1` still checks the generator |

## Milestones
Session counts are rough guesses.

| # | Goal | Size | Done when |
|---|---|---|---|
| 1 | River, camera and jet on GBC; tile job split across frames | ~1 session | Smooth scrolling, no tile glitches, steering pans the view correctly |
| 2 | Enemies, shooting, collisions, fuel, minimal HUD; no sound | ~1 session | A full game can be played from start to game over |
| **Gate** | **User playtests milestone 2** | | **Go/no-go below** |
| 3 | Sound, game flow, full HUD, GBC colours | 2–3 sessions | Parity with NES, apart from anything explicitly dropped |
| 4 | Profile the full game; SM83 assembly only if it drops frames, `river_convert_row` first | 0–1 session | Holds 1.5 px/frame on most frames |
| 5 | Shared code behind a platform layer; merge | ~1 session | NES and GBC build from one tree; NES ROM checks still pass |

### Go/no-go questions
- Is the reaction time fair at cruise and fast speed?
- Is the panning camera comfortable or disorienting?
- Are hidden enemies ambushing the player unfairly?

If only reaction time fails, try a lower jet position (already 222, the sprite-cull limit), a slower top speed and a thinner HUD before stopping.

## Risks
1. **Reaction time (the main one).** Look-ahead drops from 188 px to about 106 px: 1.2 s instead of 2.1 s at fast speed, 1.8 s instead of 3.1 s at cruise. Enemies also start moving before they're visible.
2. **Unmeasured costs.** About 40% of the frame is left for the player, HUD and uploads. If that's not enough, milestone 4 grows.
3. **Horizontal clipping.** The river can reach bytes 3..46 at high levels (about 176 px), wider than the 160 px screen, so banks and enemies can be off screen while the jet is near the other side.

## Build and test notes
- **Compile:** `gbdk/bin/lcc.exe -msm83:gb -Isrc`, with `-Wf-bo1` for `entities` and `entity_gfx`. **Link:** `-Wl-j -Wl-m -Wl-yt0x19 -Wl-yo4` (MBC5, 4 banks). Add `-DPROF_EXPORT_STATICS` to see statics in the profiler.
- **Banking:** a single-bank link with `BANKED` functions crashed by jumping into unmapped ROM (`RST_38`). Keep banked modules in a real bank, as the NES build does.
- **Time source:** `entities_update()` reads `sys_time`, which only advances with VBlank. With the display off, `game_ticks` becomes 0 and the river stops. Benchmarks must advance `sys_time` themselves.
- **Emulator:** `gearboy/Gearboy.exe --headless --mcp-http --mcp-http-port 7777 rom.gb rom.noi`, MCP at `http://127.0.0.1:7777/mcp`.
  - It starts paused, so call `debug_continue`.
  - Requests after `initialize` need the header `MCP-Protocol-Version: 2025-11-25`.
  - `read_memory` area 6 is WRAM, with offsets from `$C000`.
  - The profiler reports units of 70,224 per frame at any CPU speed: divide by 4 for M-cycles in single speed, by 2 in double speed. Passes per frame (`_wait_vbl_done` calls / frames) is the headline pacing number: 1.0 = 60 Hz, 0.5 = locked at 30 Hz. Recipe and run script: `tests/gb/profile_play.mjs` (build with `-ProfExport`).
  - **First real-ROM profile (2026-11-01, fast speed, 10 s, units):** `oam_camera` cost 31k per pass (int16 math on all 48 slots), now 7-12k. Row generation is ~13.5k per raw row (`river_convert_row` 12.5k). `river_run_job` budget per pass (`RIVER_JOB_COLS`), fast speed, 10 s: 6 gives 26 scroll clamps, 0.72 passes per frame, job peak 26k; 10 gives 5 clamps, 0.75, 38k; unlimited gives 3 clamps, 0.75, 53k (`river_update` peaks 81k / 92k / 104k). Chosen: 10. Passes per frame at fast: 0.51 before the `oam_camera` fix, ~0.75 after; cruise 1.00. The player and entity groups are not yet profiled by name (see HANDOFF).
  - Fast-forward didn't speed up headless runs, so keep benchmarks short.
  - `gearboy/` is gitignored and local to this machine.
- **Stop emulator processes** when finished.
