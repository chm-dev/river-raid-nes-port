# Project Progress & Session Handoff

**Last updated:** 2026-10-30
**Current phase:** Phase 5 COMPLETE (2026-10-30) — jet, shot, entities, scoring, fuel, bridge/road/tank, respawn-at-bridge (checkpoint rule verified against the real game) and the 30 Hz/60 Hz frame work are done and covered by `nes_gameplay`, `nes_respawn`, `nes_river_cache`, `host_restart.ps1`; Phase 6 in progress: APU sound re-derived from the Atari and verified; attract mode, start-level select (Up/Down), 1P/2P alternating play, per-level hi-scores, entity freeze until the player starts and the start clearance around the jet are done (2026-10-30, see the last Session log entries); open: P2 HUD label, high-start-level checks, attract screen-saver (see Next step)
**Branch note:** all work is on `main`; `experiment/30fps`, `experiment/river60-logic30` and `feature/hud-compact` are fully merged (safe to delete).
**Phase 1:** COMPLETE (reverse engineering + asset extraction)
**Phase 2:** COMPLETE (scaffolding, build pipeline, boot ROM, Mesen smoke test)
**Phase 3:** COMPLETE (HUD, deferred LCD split, playfield ring, 1-px scroll test)
**Phase 4:** COMPLETE (validated generator, collision ring, exact per-tile cache, Atari orientation; all tests pass)

> **Archive.** `HANDOFF.md` is the current state. "Current status" and "Next step" below are
> frozen snapshots as of 2026-10-31 and may be stale.

### Topic index (find an entry, then read only that one)

Entries are `### date - title` under **Session log**. Find one with
`Select-String -Path PROGRESS.md -Pattern '<title fragment>' -SimpleMatch`, note the line number, and Read ~80 lines from there
(or use `Grep -A 60`). Titles are stable; line numbers drift. Sections in the first ~590 lines: *Confirmed technical
decisions* (display/PMG/river constants), *Known issues / deferred items*, *Handy commands*.

| Topic | Entry titles (fragments) |
|---|---|
| Phase 1-3 setup, HUD base | "Phase 1 complete", "Phase 2 complete", "Phase 3 complete", "Source-faithful HUD" |
| River generator + collision ring | "Phase 4 generator and collision ring", "Atari river/tree layout", "Playfield orientation", "Out-of-bounds river" |
| Palette, black world at start | "Atari-like palette", "Black world at game start", "Land colour after respawn" |
| Player jet, shot, throttle, death | "Phase 5 slice 1", "Phase 5 slice 2", "Fast throttle", "Death animations", "Throttle ramp" |
| Spawner, entities, helicopters | "Faithful object spawner", "Entity fixes after play-testing", "Start sequence, colours, helicopter", "Entities moved while the river scrolled in", "No enemies near the jet" |
| Bridge, road, tank, shell | "Bridge and road", "Bridge tank and its shell", "Tank priority, bridge debris", "Tank shell scrolled wrongly" |
| Shooting enemies research (helicopter shell, side tanks) | "shooting enemies researched" |
| Respawn / checkpoint, fuel | "Respawn at the last cleared bridge", "Fuel rates checked", "Phase 5 closed out", "Phase 5 finished" |
| Hitboxes, level 50 gaps | "Hitboxes lagged", "Narrow gaps (level 50)" |
| Performance (frame loop, profiling) | "Frame-time profile", "Performance: time-based game", "30 Hz game logic", "Late frames", "Row render and pacing" |
| HUD geometry and tweaks | "Compact HUD", "HUD padding", "HUD: start blink", "HUD: first line raised" |
| Sound (APU) | "RAM freed, first APU sound pass", "sound re-derived", "Fuel tank sound", "Low-fuel warning, enemy shell" |
| Attract, menus, 1P/2P | "attract / SELECT / START read off", "game variants and two-player", "attract mode, start-level select", "Atari-style game select", "attract game select freezes" |

---

## Current status

- **Late frames (2026-10-30, branch `experiment/perf-tuning`):** late frames fell
  to 3-6% in fast flight and 2% at level 50 cruise (was 16-25% and 9%); see the
  session entry.  **Fast flight stays at 1.5 px/frame:** 2.0 was play-tested and felt
  worse.  Newer than the Memory layout bullet below: bank 1 has **154 bytes free**,
  the fixed bank ~1 KB, RAM 67 bytes.  `tools/object_host_check.py` no longer links
  (its stubs lack `sound_play_bonus`, `sound_play_shell`, `hud_lives`), also on `main`.
- **Compact HUD (2026-10-30, branch `feature/hud-compact`):** the HUD is 24
  scanlines of content with 3 px padding above and below (was 32 + 4, plus an 8 px
  blank row at the top); the playfield is **210 scanlines** (was 204), split at **30/31**.  The
  shorter wait for the split recovered ~1.2k cycles per frame (`.delay_to_lcd_scanline`
  5.4k -> 4.1k), fast late passes fell from ~23% to 16-20%, cruise is unchanged
  (0.7-0.8%).  Side effects handled: tall objects (the 24-row fuel depot) have only
  25 px of headroom above the screen, so `entities_row()` clamps the spawn Y at 0;
  the tests' geometry constants were updated.  Cost: the CHR cache window grows by
  one tile row (see Known issues).  `nes_ring_integrity.mjs` shows the ring margin
  is fine (no visible row rewritten up to at least a 223-line window).
- **Frame loop (2026-10-30, final shape):** `river_update()` and `sound_update()`
  run **every frame** (60 Hz scroll, row generation, tile uploads, APU).  The
  sound engine moved out of the 30 Hz group on 2026-10-30 because the Atari's
  sound VBI is 60 Hz and the shot is a 15-step pitch sweep, one step per frame.
  Everything else runs at **30 Hz, in two groups on alternate frames** so their
  costs never land together: the player group (`player_update`,
  `collision_update`, preceded by `entities_follow()`) and the HUD + entity group
  (`hud_update`, `entities_update`).  `entities_follow()` nudges the drawn entity
  sprites by the rows the river scrolled on the in-between frames, so 30 Hz
  entities do not lag the 60 Hz river.  `game_ticks` is the elapsed time of the
  group being run; `entities_update()` measures its own from `sys_time`.
- **Sound (2026-10-30, re-derived from the Atari):** `src/sound.c` now uses the
  register values the original actually writes (captured with
  `tools/atari_sound_trace.py` / `atari_sound_events.py`), converting POKEY's
  `f = 63920/(2*(AUDF+1))` to 2A03 pulse/noise periods.  Shot = pulse 1,
  50% duty, volume 8, **405 -> 196 Hz in 15 one-frame steps** (the old build was
  2728 -> 740 Hz, the "octave or two too high" the user heard).  Engine = **noise**
  (17-bit poly, volume 6/4/3 for fast/cruise/slow, like `$B4AC[$19&3]`) instead of
  the triangle hum, with the **pitch gliding** one noise index per 8 frames
  instead of jumping, because the Atari follows the jet's altitude.  Explosion
  (23 frames) / crash (55), fuel tank (a 499 Hz note, or 999 Hz when the gauge is
  full, repeated while the jet is over it), the low-fuel warning ($B2FD: a
  rising 251 -> 561 Hz tone at volume 15, 35 frames on / 28 off, which **outranks
  every other sound** and cannot be interrupted), the enemy shell ($B4C0 chirp +
  fireball noise) and the extra-life arpeggio (64) and start blip (2) all follow
  the original.  `tests/nes_sound.mjs` (34 checks) samples the live 2A03 state,
  so it verifies pitch and audibility instead of register writes.
- **Measured (per-frame trace, ~5 entities, invulnerable jet, shooting):** slow
  0.50 px/frame with no overruns; cruise 1.00 px/frame, **0.5-0.7% late frames**
  (was 5%); fast 1.50 px/frame (the target), **19-23% late passes** (was 68%),
  scroll steps spread over 1-2 rows instead of 0/3.  Details and the per-group
  cost table are in the performance entry of the Session log.
- **Memory layout (fixed bank / bank 1 / RAM):** ROM is 4 x 16 KiB (mapper 30).
  The **fixed bank has ~2.9 KB free** and **bank 1 ~3 KB free** (`entities.c`,
  `entity_gfx.c`, `player.c` are built with `-Wf-bo1`, `hud.c` with `-Wf-bo2` since 2026-10-31; their public
  functions are `BANKED`).  **Internal RAM: 13 bytes free as of 2026-10-31** (HUD blink/recolour/raise work; was 96 on 2026-10-30 after the menu/2P state and spawn clearance; 140 after the
  sound rewrite; `build.ps1` throws if `_BSS` passes `$07FF`).  The linker does *not*
  fail when the fixed bank overflows into the vectors at `$FFFA`: the ROM silently
  grows to 128 KiB - always check the `ROM` line of `build.ps1` (must be 65,552).
- Test state on this branch: `nes_smoke` (50), `nes_scroll_hud` (14), `nes_river`
  pass; `nes_river_cache` passes again (2026-10-30: it waits for the ring to turn over and uses the 210-line window).  New: `nes_entity_hits` (20 cases),
  `nes_sprite_lock`, the `compare_*` ROM-diff scripts and the `perf_*` profilers
  (see Key artifacts).
- Phase 1 is complete. The cart is extracted, fully disassembled, the display
  and PMG model are confirmed, the procedural river data/algorithm is
  documented, and all sprites/tables/charset are extracted into C arrays.
- Phase 2 is complete. `build.ps1` and `Makefile` produce a 32 KiB mapper-30
  ROM with CHR-RAM, the module stubs are wired into a `vsync()` loop.
- Phase 4 is complete:
  - `tools/river_model.py` is a validated Python translation of `$A6DE-$AB7E`.
    It matches a 2,596-transition live Altirra trace (levels 1-3) for every
    generator field and every 48-byte row (only unused `$B1/$B7` temps differ).
  - `src/river.c` ports the full state machine: LFSR `$B183`, level/section
    timers `$5A/$5D`, bank targets `$34/$35`, island flags/width `$2D/$5F`,
    feature selection `$A96C-$A9D5`, and the `$AA7A-$AB7E` row renderer.
  - The 30-row collision ring stores left/right bank X and island bounds in
    source pixels, plus bridge/island/feature/flash flags.
  - The NES renderer uploads each visible 8x8 block **verbatim** into a
    254-slot CHR cache in the playfield's own pattern table; solids use fixed land/water tiles.
    There is no edge/tree atlas, so 1-px staircases and 32-px tree template
    fragments are exact.
  - Orientation matches the Atari: newest row at the top, playfield scrolls
    down (`playfield_scroll_y` decrements), raw row 7 is the top scanline of
    each NES tile, and new tile rows are written just above the visible
    window.  The split handler subtracts `HUD_SPLIT_SCANLINE` because GBDK's
    deferred ISR adds the current scanline to `bkg_scroll_y`.
  - `tests/nes_river.mjs` compares the live C rows, state and collision ring
    against 400 golden tile rows from `tools/river_model.py`: 397/397 sampled
    rows match (the first 3 complete before the test attaches).
  - `tests/nes_smoke.mjs` (50 checks) and `tests/nes_scroll_hud.mjs`
    (14 checks) pass with the shared-digit CHR layout and downward scroll.
- Phase 3 is complete:
  - The HUD is a 3-row content band (24 px) plus 3 px padding above and below (compact
    since 2026-10-30; it was 4 rows + 4 px); the playfield split is at scanline
    30/31 (`LYC=29`).  Nametable row 0 is blank padding that the HUD pass scrolls
    up (`HUD_SCROLL_Y` = 5); rows 1-3 carry the text.
  - Layout mirrors the Atari HUD: P1/score/HI/hi-score, then
    GAME/fuel box/BRIDGE/bridge number, then game number/gauge/lives.
  - HUD palette 0 is gray/black/yellow/white (`00 0F 28 30`); labels are
    black and the highlighting (P1, score, BRIDGE, bridge number, fuel bar,
    lives) is yellow.
  - Fixed labels are the original charset glyphs, not redrawn text:
    `$43-$46` = BRIDGE, `$5A` = P1, `$5B` = HI, `$5C` = lives plane.
    They are doubled horizontally like the Atari mode-6 text.  GAME uses
    the uppercase glyphs (`G/A/M/E = $07/$01/$0D/$05`) the same way.
  - The fuel gauge is the original 5-cell frame
    `47 48 49 48 4A` / `4B 4C 4D 4C 4E`; E, 1/2 and F are baked into the
    artwork and never change.  The yellow level bar is a two-sprite overlay
    (8+2 px, 10 px tall) that starts just below the top markers and slides
    from the F marker (full) to the E marker (empty) behind the labels.  The
    bar tracks `hud_fuel >> 4` and its 4-px column aligns with the marker
    columns.
  - Score/hi-score suppress leading zeros.  The score field sits at tiles
    11-16 (right-aligned), so a six-digit score centres over the gauge; the
    yellow digits are generated one pixel higher to leave padding above the
    gauge.  The lives icons are sprites on OAM slots 44-46.
  - The playfield is a full 30-row (240-pixel) ring in nametable 1
    (`$2400`). The iNES header is patched to vertical mirroring so the two
    nametables are distinct.
  - The split is applied by GBDK's deferred `add_LCD()` handler from the NMI
    path, so river row streaming cannot delay or miss it.  The visible split
    lands on scanline 36/37; that one-line boundary jitter is inherent to
    GBDK's faked LCD delay (the odd/even PPU frame skip).
  - `river_update()` moves the scroll one pixel **down** per frame and
    writes one 32-tile row every 8 pixels just above the visible window.
  - Each streamed tile row is built from 8 procedural Atari rows. Raw row 7
    is the **top** scanline of the NES tile (Atari's stream is bottom-up
    relative to the display). `river_raw_row` (valid when `river_block_index == 0`) exposes the last raw row and
    `river_debug_state` the generator state for the Mesen test.
  - `river_init()` prefills the ring with a straight channel (not 30 full
    procedural rows) so boot stays under one frame budget; the first
    procedural tile row replaces it as soon as the scroll crosses 8 px.
  - The old demo/atlas tile sources (`river_demo_data.*`, `river_atlas_data.*`,
    `river_tileset_data.*`) and their generators are gone; the river uses the
    exact 254-slot cache (24 river tile columns).
- Build: `roms/river_raid.nes` is 32,784 bytes, mapper 30, 2 x 16 KiB PRG,
  CHR-RAM, vertical mirroring. RAM: `DATA $0300-$030F`,
  `BSS $0310-$079C`, heap at `$079D`, 99 bytes free.
- Tests: `tests/nes_smoke.mjs` passes 50 checks; `tests/nes_scroll_hud.mjs`
  passes 14 checks; `tests/nes_river.mjs` passes 3 checks (397/397 sampled
  rows from the first observed state: exact rows, generator state and
  collision ring).
- No blocker. Phase 4 is complete; Phase 5 starts next.

## Completed — Phase 1 checklist

- [x] Split the XEX into its 16-byte loader and 8 KiB cartridge slice.
- [x] Full 6502 disassembly of `$A000-$BFFF` (three listings in `docs/`).
- [x] Confirm the display list, mode-E geometry, DLI split, ring/seam layout.
- [x] Confirm the PMG memory map and the DLI's multiplexed P3 renderer.
- [x] Confirm the player P2 jet, its X/Y mapping, and flight shapes.
- [x] Decode the river generator state machine, LFSR, bank patterns, feature
      tables and template stream.
- [x] Extract the charset, HUD template, object sprites, player shapes, HUD
      glyphs, score/spawn/sound/river tables and palette.
- [x] Generate `src/atari_data.h` + `src/atari_data.c`; compile-checked with
      the NES SDCC target.
- [x] Capture deterministic attract, gameplay, left-steer and fire traces.
- [x] Validate the sprite sheets against the live PMG buffer (player exact,
      FUEL/plane full-row matches) and the playfield against screenshots.

## Completed — Phase 2 checklist

- [x] `build.ps1` + `Makefile`: compile every `src/*.c`, link with
      `-mmos6502:nes -Wl-j -Wm-yoA`, emit `roms/river_raid.nes`/`.noi`/`.map`,
      assert the iNES header and the RAM layout.
- [x] Module scaffolding: `main.c`, `config.h`, `hud`, `river`, `player`,
      `entities`, `collision`, `sound` with real init/update signatures and
      flat static state.
- [x] Minimal boot ROM: shared BG palette, 128-glyph CHR-RAM charset, five
      scene tiles, boot text, wavy river + bridge, BCD frame digits, and a
      VBL/LCD split at scanline 24 (superseded by the 36/37 HUD split).
- [x] `tests/mcp_client.mjs` (MCP handshake, memory/`.noi` helpers) and
      `tests/png.mjs` (dependency-free screenshot reader).
- [x] `tests/nes_smoke.mjs` verified the boot ROM in MesenCE-MCP.
- [x] Boot verified in MesenCE-MCP.

## Completed — Phase 3 checklist

- [x] BG tile allocation (Phase 3; superseded by the Phase 4 layout):
      0..127 charset, 128..209 river demo tiles, 210..225 gauge frame,
      226..227 P1, 228..229 HI, 230..237 BRIDGE, 238..245 GAME,
      246..255 yellow digits.
- [x] Palettes: HUD `00 0F 28 30` (gray/black/yellow/white); playfield
      `00 1A 06 12`; player sprite `00 28 30 16`.
- [x] Source-faithful HUD: padding row, P1/score/HI/hi-score,
      GAME/gauge/BRIDGE/bridge number, game number/lives.
- [x] Fixed labels generated from the original charset glyphs
      (`$43-$46`, `$5A`, `$5B`) and doubled horizontally.
- [x] Gauge frame assembled from `47 48 49 48 4A` / `4B 4C 4D 4C 4E`
      with E/1/2/F baked in; yellow level bar slides behind the labels.
- [x] Lives use the original `$5C` plane glyph as yellow sprites.
- [x] Playfield ring in NT1 (`$2400`), attributes `0xAA` (palette 2).
- [x] Deferred LCD split: VBL asks for LYC=35; the `add_LCD()` handler sets
      NT1 + the playfield scroll at scanline 36/37.
- [x] `river_update()`: 1-pixel/frame scroll (downward after the Phase 4
      orientation fix), 30-row ring, stream one row every 8 pixels,
      `river_freeze` test hook.
- [x] `tools/gen_river_demo.py` packed the captured 157-row river into
      19 tile rows / 82 distinct tiles (removed; superseded by the exact
      cache).
- [x] `tools/patch_nes_header.py` fixes GBDK's iNES byte 6 (vertical
      mirroring, not four-screen); wired into `build.ps1` and the Makefile.
- [x] `tests/nes_smoke.mjs` updated to 50 Phase 3 checks.
- [x] `tests/nes_scroll_hud.mjs` updated to 14 checks (split 36/37, sprite
      fuel marker and lives).

## Completed / in progress — Phase 4 checklist

- [x] Port the exact Atari LFSR (`$B183`), section/level timers, bank-target
      movement, island/fork logic and feature template selection.
- [x] Port the `$AA7A-$AB7E` row renderer and expose the 48-byte raw row.
- [x] Build the 30-row collision ring (`river_left_x`, `river_right_x`,
      `river_island_left_x`, `river_island_right_x`, `river_flags`).
- [x] Extend `atari_river_widths`/`atari_river_type_margin` to cover the
      original's out-of-range table reads at high levels.
- [x] Validate the Python model against a 2,596-transition live Altirra trace
      (all generator fields and rows; levels 1-3).
- [x] Validate the C port against `tools/river_model.py` with
      `tests/nes_river.mjs` (397/400 sampled tile rows, exact rows/state/
      collision).
- [x] Fix the SDCC local-alias bug in `river_render_row` by using a
      file-scope saved offset (`rg_ysave`).
- [x] Replace the demo tile source with a 51-tile procedural edge atlas
      (solid land/water/AA, left/right edges x 8 phases x 3 slopes;
      superseded by the exact cache below).
- [x] Exact per-tile CHR cache for 1-px bank edges and trees.  Atari has no
      river tile map: each scanline is a 48-byte bitmap and a tree is a
      template fragment copied into consecutive rows.  The NES now uploads
      each visible 8x8 block verbatim (see the 2026-10-29 cache log entry).
- [x] Update the HUD/river test constants for the new CHR layout
      (digits 1..10, HUD 11..46, land 47, water 48, cache 49..255).
      `nes_smoke` and `nes_scroll_hud` pass; `nes_river` covers 397/397 rows
      from the first observed state.
- [x] Invert the playfield to the Atari orientation: raw row 7 at the top,
      ring written above the visible window, `playfield_scroll_y`
      decrements.  Subtracting `HUD_SPLIT_SCANLINE` in the split handler
      removes the 36-px GBDK deferred-ISR offset that put the newest row at
      the bottom.  `build/flip_fixed.png`.
- [ ] Bridge/feature visual comparison against the Atari reference frames.
- [x] Edge comparison (pre-cache baseline): `tools/compare_river_edges.py`
      rendered the captured Atari rows exactly and again through the old
      51-tile atlas classifier.  Result: 9.4% pixel mismatch over 152x160,
      worst 40x40 crop 31%.  Atari has a continuous 2-px outline and 1-px
      staircase; the atlas broke the outline and quantized slopes.  Images:
      `build/river_edge_compare.png`, `build/river_edge_zoom.png`,
      `build/river_atari_vs_nes_d155.png`.  Superseded by the exact cache;
      keep the script for regression comparisons.

## Key artifacts

| Path | Contents |
|---|---|
| `docs/ATARI_RE_SPEC.md` | Memory map, display/DLI/PMG model, objects, river generator, HUD, audio, routine catalog, open items |
| `docs/atari_disasm_*.txt` | Complete instruction listings |
| `assets/atari/river_raid_cart_a000_bfff.bin` | Extracted 8 KiB cartridge |
| `assets/atari/palette_altirra_ntsc.bin` | Exact NTSC palette |
| `assets/atari/extracted/` | Charset, HUD, sprites, tables, river fixtures, full-frame reference |
| `assets/atari/extracted/README.md` | Asset file map + full HUD glyph inventory (`$43-$46`, `$47-$4E`, `$5A-$5C`, `$50-$59`, `$01-$1A`) and their uses |
| `assets/atari/runtime/` | Attract, scroll, play-left and play-fire captures |
| `src/atari_data.h` / `.c` | Generated Atari asset arrays |
| `src/river.c` | Exact generator, 30-row collision ring, exact 254-slot tile cache, 24-column river area |
| `src/river.h` | Collision-ring bounds/flags and generator object-input hooks |
| `tools/river_model.py` | Validated Python reference model of `$A6DE-$AB7E` |
| `tools/atari_river_trace.py` | Live Altirra per-row state/row capture |
| `tools/river_validate.py` | Python-model vs. Altirra trace comparison |
| `tools/gen_river_golden.py` | Generates `tests/data/river_golden.json` |
| `tools/compare_river_edges.py` | Exact Atari vs. old-atlas edge comparison (baseline) |
| `tools/show_tree_layout.py` | Captured 32-px tree feature with 8x8 grid |
| `tools/show_proc_tree.py` | Procedural tree: exact vs. old-atlas comparison |
| `tools/inspect_playfield.py` | NT1+CHR playfield reconstruction / screenshot check |
| `src/main.c` | Boot sequence, VBL/LCD split at LYC=29 (visible 30/31), frame loop |
| `tests/nes_ring_integrity.mjs` | Model-free ring test: no visible river row may change between two frames (guards the ring margin and the CHR cache) |
| `src/hud.c` | Source HUD: charset-glyph labels, 5-cell gauge, sprite fuel bar, lives planes |
| `src/player.c` | Sprite CHR for the three flight shapes |
| `src/config.h` | Geometry, tile IDs, palettes, nametable bases, HUD layout |
| `build.ps1` / `Makefile` | Reproducible GBDK-NES build + header patch |
| `tests/mcp_client.mjs` | MesenCE-MCP client and memory/`.noi` helpers |
| `tests/png.mjs` | Minimal PNG reader for screenshot assertions |
| `tests/nes_smoke.mjs` | Phase 3 boot/HUD/playfield regression (50 checks) |
| `tests/nes_scroll_hud.mjs` | Scroll/split/HUD regression (14 checks) |
| `tests/nes_river.mjs` | Phase 4 row/state/collision validation (3 checks, 397/400 sampled) |
| `tests/data/river_golden.json` | 400 golden tile rows from `river_model.py` |
| `tests/nes_entity_hits.mjs` | 20 jet-vs-entity box boundary cases (guards `ent_scan_player`) |
| `tests/nes_sprite_lock.mjs` | Entity sprite Y + scroll constant per frame (guards `entities_follow`) |
| `tests/compare_oam.mjs`, `tests/compare_tile_rows.mjs` | Dump entity sprites / on-screen river tile pixels so two ROMs can be diffed (proves an optimisation did not change output) |
| `tests/perf_scroll_cadence.mjs` | Frames per pass, rows per frame, px/frame for slow/cruise/fast |
| `tests/perf_function_cycles.mjs` | Estimated cycles per frame by function (Lua exec sampler) |
| `tests/perf_frame_trace.mjs` | Per-frame cost trace; what the heavy / late frames contain |
| `tools/river_repeat_rows.py` | Validates the repeat-row rule used by `river_step_state` against the model |
| `tools/atari_sound_trace.py` | Per-frame POKEY register + decoded `audio_state()` dump (`--fast` for long runs) |
| `tools/atari_sound_events.py` | Plays each sound trigger in isolation and dumps what the VBI writes |
| `tools/sound_trace_show.py`, `tools/sound_events_show.py` | Render those dumps as one line per frame |
| `tools/patch_nes_header.py` | Patches the iNES mirroring byte |
| `roms/river_raid.nes` / `.noi` / `.map` | ROM + symbols + linker map |
| `build/flip_fixed.png` | Correct orientation: newest row at top, exact edges/trees |
| `build/cache_fixed.png` | First exact-cache screenshot (pre-orientation fix) |
| `build/scroll_a.png` / `scroll_b.png` | Frozen-scroll 100/101 shift proof |
| `build/hud_dynamic.png` | Screenshot captured during the HUD field test |

## Confirmed technical decisions

These are documented in full in `docs/ATARI_RE_SPEC.md`; don't re-derive them.

- **Display:** 20 blank lines + **157** mode-E lines (48-byte rows, visible
  40-byte window) + 3 mode-6 HUD rows (20 chars each at
  `$1000/$1014/$1028`) + marquee.
- **PMG:** `PMBASE=$08`; P0–P3 at `$0C00/$0D00/$0E00/$0F00`. Object shapes
  are multiplexed on P3 at `$0EBC+X`; object screen Y = `object_y - 75`.
- **Player:** P2 at `$0EAA-$0EB7`, 14 rows, X=`$57`, shape=`$5E`
  (7/8/9 = bank-left/right/straight; 0/1 blank, 2–6 shared effect fragments).
- **River:** 160-row ring; 3 seam rows hidden; LFSR `$B183`; bank patterns
  `$B800/$B810`; widths `$BBB2`; feature types `$BBCC/$BBD4`; lengths
  `$BBEB`; template pointers `$BBF6`; fragment stream `$BC00`.
- **HUD:** 3 x 20 mode-6 chars; charset base `$06`; glyph index
  `(code & 0x7F)*8`; text seed `$BB70`.  The charset already contains the
  HUD art: `$43-$46` BRIDGE, `$47-$4E` the 5-cell gauge
  (`47 48 49 48 4A` / `4B 4C 4D 4C 4E`), `$5A` P1, `$5B` HI, `$5C` the
  lives plane.  See `assets/atari/extracted/README.md` for the full table.
- **Score:** 3-byte BCD at `$63-$65`; `$BB0B` values are direct BCD points
  (confirmed +6 and +3 in gameplay).
- **Palette:** `assets/atari/palette_altirra_ntsc.bin`.
- **NES split:** top HUD NT0 `$2000`, playfield ring NT1 `$2400`, vertical
  mirroring.  GBDK's deferred `add_LCD()` handler writes the playfield
  scroll/nametable from the NMI path every frame; its PPUADDR + PPUSCROLL
  sequence sets both `t` and `v` (a PPUSCROLL-only write cannot change the
  vertical scroll mid-frame).  The visible split is 30/31 (`HUD_SPLIT_SCANLINE`
  30, `LYC` 29) due to the faked delay's one-line jitter.
- **NES HUD:** 3 content rows (24 px) with 3 px padding above and below; nametable row 0 is
  blank padding scrolled up by `HUD_SCROLL_Y` = 5 (the HUD sprites'
  `HUD_*_Y` are screen positions, 5 less than the tile-row based ones), rows 1-3
  are content.  Gray band (`$00`), black and yellow text.
  Labels are the original glyphs doubled horizontally; the gauge is the
  original 5-cell frame; E/1-2/F are fixed.  The yellow level bar is a
  two-sprite overlay that slides behind the labels from F (full) to E
  (empty).  Lives are `$5C` plane sprites.  Score fields suppress leading
  zeros.  The score/hi-score digits remain 8 px wide (a width compromise
  vs. the source's 16 px digits).
- **VBlank budget:** at most one 32-byte river row per 8 frames plus dirty HUD
  tiles; the transfer buffer handles the actual PPU writes in the NMI.

## Known issues / deferred items

1. **Exact river cache (Phase 4, done, 60 fps-class):** the renderer uploads
   every visible 8x8 block verbatim into the playfield's **own** 256-tile BG
   pattern table (`$0000`: 0 land, 1 water, 2..255 = 254 cache slots).  HUD
   tiles stay in `$1000` shared with sprites (sprites start at tile 64,
   `SPRITE_TILE_BASE`); the split handlers flip PPUCTRL bit 4 per pass.  The
   generator runs **one tile row ahead** of the display (8 pre-generated
   rows at init), so pattern uploads/stripe writes are spread over frames.
   Superseded by the 2026-10-29/30 performance entries: the upload job streams
   through `river_put()`, the loop is 60 Hz river + 30 Hz logic, and cruise
   overruns only ~0.5% of frames.
1b. **Remaining performance limits (updated 2026-10-30, see the "Late frames"
   session entry):** a late pass shows an exact duplicate of the previous frame, then
   a jump (`0 3`).  The library's NMI replays the scroll table and skips OAM DMA until
   `vsync()` has run, so the scroll and the sprites cannot disagree (the earlier note
   here about a mid-pass desync was wrong; nothing needs committing at the end of the
   pass).  Late frames: fast 3-6%, level 50 cruise 2%.  MMC3 + cc65 was
   assessed (the `MMC3Template` repo) and rejected as too expensive; it would
   recover the ~5.4k-cycle NMI split spin and add 8 KB WRAM.
1c. **Player, shot, hit tests and the HUD run at 30 Hz** (steering runs the
   Atari's 60 Hz ramp twice per pass, see 2026-10-30 "Narrow gaps"; the shot moves 12 px per pass); the 20-case hit-box test guards the
   boundaries, but a fast shot could in principle skip a very small target.
1d. **CHR cache overflow at high levels (compact HUD).**  The 250-slot cache must
   hold the uploads of every visible tile row plus the one being uploaded.  From
   the model (60,000 tile rows, level 80, mean 5.0 / max 14 uploads per row):
   a 26-row window peaks at 251 slots, 27 at 258, 28 at 265, 29 at 274.  The
   compact HUD raises the worst window from 28 to 29 rows and the share of scroll
   phases that need it from 3/8 to 6/8, so windows over 250 go from ~30 to ~67
   per 60,000 rows (~0.1% of positions), first at level 10.  Effect: a few wrong
   tiles in the bottom row for a moment, at level 10+ only.  Not observed in the
   tests (levels 1-3).  Options if it is ever seen: a HUD of 28 px (remove only
   the top 8 px), or stall the upload job instead of overwriting visible slots.
2. **Fuel/bridge scores:** DONE - `atari_score_tens[]` (30/60/100/150/80/500/750 by type) is the original table, applied in `entities_hit_shot()`.
3. **Collision ring is per tile row:** bounds are sampled at raw row 3 of
   each 8-row block.  Phase 5 should interpolate or sample the jet's exact
   scanline within the block.
4. **CHR layout:** HUD table `$1000`: 0 blank, 1..10 shared digits, 11..26
   gauge, 27..28 P1, 29..30 HI, 31..38 BRIDGE, 39..46 GAME; sprites 64+.
   Playfield table `$0000`: 0 land, 1 water, 2..255 exact river cache.  Only 10 charset glyphs remain in CHR; all HUD labels are
   built from `atari_charset` in PRG.  Score/bridge digits use plane-0 art
   plus BG palette 1 (yellow); HI/game digits stay palette 0 (black).
5. **Fuel rate / 16-bit gauge (Phase 5).**
6. **PAL gauge offset (`$BF`) (Phase 5).**
7. **`$24`/`$50` semantics:** `$24` is a 3-frame debounce, `$50` is the shot
   animation counter — not game-state flags.
8. **Stale PMG sample:** the aux 13 ship's PMG bytes do not match its ROM
   shape in the single captured frame; this is DLI multiplex/stale-buffer
   timing, not an asset error (screenshot and full-frame render agree).
9. **Deprecated tools:** `render_mode14.py`, `antic_mode14_probe.py`; use
   `render_mode_e.py` / `pmg_line_probe.py`.
10. **Split boundary jitter:** GBDK's faked LCD delay lands the playfield on
    scanline 30 or 31 (a one-line alternation; it was 36/37 before the compact
    HUD); the top 30 rows stay HUD/gray and there are no black playfield frames. A future option is a fixed
    border row at the boundary or a mapper with a scanline IRQ.
11. **HUD digit width:** score, hi-score and bridge digits use the 8-px
    charset digits, while the source displays 16-px mode-6 digits.  Fitting
    six wide digits for both score and hi-score in 256 px would collide with
    the HI label; this is an intentional width compromise.
12. **HUD glyph source:** do not redraw the HUD labels/gauge.  They are in
    `charset.bin` (`$43-$46` BRIDGE, `$47-$4E` gauge, `$5A` P1, `$5B` HI,
    `$5C` lives plane).  The full table is in
    `assets/atari/extracted/README.md`.
13. **Sound timbre compromises (2026-10-30):** POKEY's poly distortions have no
    exact 2A03 equivalent.  The engine (17-bit poly on ch1) and the explosion
    (poly on ch2) share the single noise channel, so an explosion masks the
    engine hiss until it ends.  The fuel tank note is the one place the original
    uses a plain square - `AUDC1 = $A8..$A1` is POKEY "no poly" - so it is
    ported as a 50% pulse at exactly 499 Hz, volume 8 -> 1, repeated while the
    jet is over the tank.  The engine's noise index steps 11/12/13 for
    fast/cruise/slow, where the Atari's `AUDF1` range (23-27) only spans
    ~0.2 octave: the 2A03 noise steps are too coarse to reproduce it exactly,
    so the port spreads it to ~1 octave on purpose (the Atari also raises the
    volume for "up", which is kept exactly).

## Next step — Phase 6 (Phase 5 is complete)

1. **Sound: DONE (2026-10-30, re-derived from the Atari).**  The first pass
   (`7d4a1ac`) was tuned by estimate and sounded wrong; it has been replaced with
   values read off a live Altirra run.  Ground truth: `tools/atari_sound_trace.py`
   (per-frame POKEY register + `audio_state()` dump, `--fast` for long runs) and
   `tools/atari_sound_events.py` (pokes one trigger variable at a time, e.g.
   `$7D = $0F`, and dumps what the VBI then does); viewers
   `tools/sound_trace_show.py` / `tools/sound_events_show.py`.
   - **Conversion:** AUDCTL is never written, so every POKEY channel runs from the
     64 kHz clock: `f = 63920/(2*(AUDF+1))`; 2A03 pulse `p = 111861/f - 1`, noise
     index from `1789773/noise_period[i]`.  AUDC = volume in the low nibble, noise
     select in bits 7-5 (`$80` = 17-bit poly = white noise, `$A0/$C0/$E0` = tone).
   - **Shot** ($7D = $0F, fire button): ch3 pure tone, volume 8, `AUDF3 =
     6*(27-$7D)` -> **405 Hz falling to 196 Hz in 15 one-frame steps**.  The old
     port did 2728 -> 740 Hz, which is the "octave or two too high" the user heard.
     Now pulse 1, 50% duty (POKEY's square), period 255 -> 549.
   - **Engine** (ch1): **17-bit poly noise**, `AUDC1 = $B4AC[joystick & 3]` =
     volume 6 accelerating / 4 level / 3 slowing (0 silent), `AUDF1 = $1F - (jet Y
     >> 4)` = ~2.3-2.7 kHz.  The old port used a triangle hum (73-125 Hz), i.e. a
     pulse-like drone instead of hiss.  Now the noise channel, volume 6/4/3 and
     index 11/12/13, walked one step per 8 frames so a throttle change glides
     in pitch instead of jumping (the Atari follows the jet's altitude).
   - **Explosion** ($7C = $17 object / $37 jet): ch2 poly noise, volume
     `((t>>1)+4)&$0F`, `AUDF2 = RANDOM|$18` (random pitch every frame); 23 or 55
     frames -> `sound_play_explosion()` / `sound_play_crash()`.
   - **Low fuel** (`$5B < $40`, `$B2FD-$B32A`): ch1 drops the engine and plays a
     **volume-15 pure tone with `AUDF1 = 2 * $7B`** while `$7B` counts `$3E` down
     to `$00`; the tone sounds only while `$7B >= $1C`, so 35 frames rising from
     251 Hz to 561 Hz, then 28 frames of engine, repeating every 63 frames.
     Ported on pulse 1 with **priority over every other sound** (nothing else
     may write that channel while the tone sounds; the engine is muted during it,
     as on POKEY).
   - **Enemy shell** (ch4, `$50` counted by `$A609-$A674`): `AUDF4 = $B4C0[$50]`
     with `AUDC4 = $AC` (pure tone, volume 12) for `$50 = 1..$10` - a four-octave
     dive 2131 -> 143 Hz - then `AUDF4 = RANDOM|$20` with `AUDC4 = $8C` (17-bit
     poly, volume 12) while the fireball swells.  Ported as
     `sound_play_shell()` from `ent_shell_init()`, so it fires and refires with
     the tank: chrome on pulse 2 (periods 52..780), tail on the noise channel.
   - **Fuel tank** ($A40A, object type `$0F`): increments `$5B` (the fuel - the
     DLI at `$B527` positions the gauge missile from `($5B >> 3) + $5C`) and sets
     `$7A = 4, $7B = 8`.  The VBI (`$B2D9`) then writes `AUDF1 = Y` where Y is
     `$3F` (499 Hz) for `$7A = 4` but **`$1F` (999 Hz) for `$7A = 3`** - and the
     carry that selects `$7A = 3` only happens when `$5B` is already `$FF`, i.e.
     the gauge is full (the code saturates instead of wrapping).  A full tank is
     therefore one octave higher, as reported.  `AUDC1 = $7B + $A0 = $A8..$A1`
     is a pure tone whose **volume runs 8 down to 1 over 8 frames**; the VBI
     clears `$7A` when the sweep ends, so the next frame over the tank re-arms
     it: **one note repeated back to back for the whole overlap**.  Ported on
     pulse 2 as `FUEL_PERIOD` 223 (499 Hz) / `FUEL_PERIOD_FULL` 111 (999 Hz),
     re-armed by `sound_play_pickup()` every frame of the overlap and choosing
     the octave from `hud_fuel == 255`.
   - `$7F = 9` (`$A5E0`) is **not** the tank sound: that path tests types
     `$0A`/`$0B`, i.e. helicopters the shot passed.  Not ported.
   - **Extra life** ($7E = $40): ch3 arpeggio 329/394/492/652 Hz, 16 frames each
     with a 15..0 volume ramp = 64 frames; triggered where `$A574-$A587` does it,
     i.e. `entities_add_score()` when the ten-thousands BCD digit changes
     (`hud_lives < 9` -> lives++).
   - **Start blip** ($C6 = 2): ch1 492 Hz pure tone, volume 15, 2 frames; played on
     the START-after-game-over branch (title/attract can reuse it).
   - Channel map: **pulse 1 = the low-fuel warning first, then the shot** (the
     warning cannot be interrupted, so a shot fired during a tone is silent);
     **pulse 2 = tank note > shell chirp > arpeggio > blip**; noise = engine +
     explosion + the shell's fireball (the 2A03 has one noise channel where POKEY
     has three, so the effects take it over and the engine resumes); **triangle
     unused**.  `snd_p2_on` shadows "this channel still holds a tone" so a job
     that ends on a different frame from its hold flag cannot leave it hanging.
   - **Bugs found on the way:** the noise channel's length counter was never
     loaded, so with the halt flag set it stayed 0 and the channel was silent
     until the first effect loaded it (`$400F`; `sound_init()` now writes it once),
     and the tank note hung at volume 1 after the jet left the tank because its
     hold flag outlived the note's last frame (fixed by `snd_p2_on`).
   - Test: `tests/nes_sound.mjs` (30 checks, pass) samples the live 2A03 state via
     a persistent Lua end-frame callback (`apu.square1.realPeriod`,
     `apu.noise.envelope.volume`, length counters) written to CHR-RAM `$1F80`, so
     it checks the pitch the console emits and that each channel is actually
     audible, not just that registers were written.
   - Not ported yet: the `$7A = 1` hit/vibration sweep on ch1.  The ch4 `$50`
     table (`$B4C0`) *is* ported - it turned out to be the enemy shell, driven by
     the tank, not an enemy shot path gated by `$41` (verified live: firing the
     player's gun sets `$7D` only, never `$50`).
2. **Game states (DONE 2026-10-30, see the last Session log entry):** attract + start-level select + 1P/2P.  Old text: START / game-over / respawn
   already work in `player_update()` (WAIT -> READY -> PLAY -> DEAD/OVER; START after game over
   restarts), but there is no title screen, no attract mode and no game selection.
   `sound_play_blip()` is ready for the menu.
   **Atari ground truth for this item (2026-10-30, see the last Session log entry):** no title
   screen and no autopilot; attract is the river scrolling with objects, no jet, HUD with
   `P1 0`, `HI`, `BRIDGE 1`, `GAME n`; SELECT steps `$09` 0..7 (game 1..8); START plays.
   **Still open from this item (priority order):** (a) check start levels 5/20/50 against the
   model and `nes_ring_integrity` (CHR-cache overflow, Known issue 1d, is reachable from the first
   screen); (b) DONE 2026-10-31: P2 label/score (white) in the right field, start blink, game select
   freeze + timer.  Still open: while P2 flies the Atari also turns BRIDGE, the bridge number and the lives
   white (COLPF2 = `$8E`, `$B68E`), which needs 8 more BG tiles + a white life sprite; (c) attract
   screen-saver blank (`$78`) and the attract scroll speed (`$59`);
   (d) re-measure `ENT_WAIT_ROWS` if the playfield or start scroll changes.
3. **Budget:** internal RAM has **13 bytes free** (2026-10-31; 96 before the HUD work, 140 before the menu/2P state) (2026-10-30: the HUD/player boot-time tile buffers
   now alias `river_tile_pat`, see `river.h`; do not call `hud_init_graphics`/`player_init_graphics`
   after `river_init()` without that in mind), the fixed bank ~2.9 KB, bank 1 ~3 KB.
4. Open fidelity items (small): road is 16 scanlines (Atari 20); balloon basket colour
   (`BUGS.md`); PAL `$BF` has no NES counterpart; the spawner uses its own RNG, so objects after
   a restart differ from the Atari's.
5. Perf: finished for now (see the 2026-10-29/30 performance entries).  If more is needed:
   commit the scroll at the end of the pass, cheaper `river_render_row`/`river_convert_row`.
6. Regenerate `tests/data/river_tiles_golden.json` with `python tools/gen_river_tiles_golden.py 520`
   before `tests/nes_river_cache.mjs`.
## Handy commands

```powershell
./build.ps1                             # compile + link + header patch + RAM asserts
node tests/nes_smoke.mjs                # Phase 3 boot/HUD regression (50 checks)
node tests/nes_scroll_hud.mjs           # scroll/split/HUD regression (14 checks)
node tests/nes_river.mjs                # Phase 4 row/state/collision validation (397/397 sampled)

./tests/host_river.ps1                  # C generator vs the Python model, 20,000 tile rows (needs zig)
python tools/object_host_check.py build/object_trace.jsonl build/object_trace_L15.jsonl build/object_trace_L24.jsonl   # spawner vs model
python tools/river_repeat_rows.py       # repeat-row skip rule: 0 false positives
python tools/atari_sound_events.py --out build/sound_events.json   # what each sound trigger does
python tools/atari_sound_trace.py --frames 400 --fast --audio-state --out build/sound_trace.jsonl
python tools/sound_events_show.py build/sound_events.json          # one line per frame
python tools/sound_trace_show.py build/sound_trace.jsonl | Select-String 'v= [1-9]'
node tests/nes_entity_hits.mjs          # 20 hit-box boundary cases
node tests/nes_sound.mjs                # live 2A03 pitch/volume/repeat checks (34)
node tests/nes_gameplay.mjs            # steering, throttle, fuel, refuel, shot score
node tests/nes_menu.mjs                # attract, Up/Down start level, SELECT 1P/2P, START, 2P turn swap (needs --nes.port2.type=NesController)
node tests/nes_hud_blink.mjs           # start blink (16/64 frames), white P2 field, blink after the 2P turn swap
node tests/nes_hold.mjs                # entities stand still until the player starts (with a negative control)
node tests/nes_start_clear.mjs         # no object within 48 px above the jet at the start, on all four start levels
# every Mesen test calls startGame() (tests/mcp_client.mjs): the cart boots into attract, not into play
./tests/host_restart.ps1                 # river_restart() vs the Atari $B1C0 truth table (build/checkpoint_trace.jsonl)
node tests/nes_sprite_lock.mjs          # entity sprites locked to the scroll
node tests/perf_scroll_cadence.mjs      # slow / cruise / fast scroll cadence
node tests/perf_frame_trace.mjs cruise 900   # per-frame trace (restart Mesen between Lua runs)
node tests/perf_function_cycles.mjs named fast   # cycles per function

python tools/gen_river_golden.py 400    # regenerate tests/data/river_golden.json
python tools/river_validate.py build/river_trace.jsonl  # model vs. Altirra trace
python tools/inspect_playfield.py 235   # NT1+CHR reconstruction vs. screenshot

# MesenCE-MCP (leave running while testing)
./MesenCE-MCP/MesenCE-MCP-Windows.exe --mcp --mcp-port=9100 --enableStdout `
  --doNotSaveSettings --preferences.pauseWhenInBackground=false `
  --preferences.pauseWhenInMenusAndConfig=false --preferences.disableOsd=true `
  --video.videoFilter=None --nes.region=Ntsc

python tools/show_tree_layout.py        # captured tree template + 8x8 grid
python tools/show_proc_tree.py          # procedural tree: exact vs. atlas
python tools/patch_nes_header.py roms/river_raid.nes   # vertical mirroring
python tools/extract_assets.py          # regenerate Atari assets + C header
python tools/render_sprite_sheets.py    # only the clean sprite sheets
python tools/export_row_reference.py    # 157-row river fixture
python tools/render_full_frame.py       # full-frame validation image
python tools/validate_sprites.py        # compare sprites to live PMG buffer

./gbdk/bin/lcc -mmos6502:nes -c -Isrc -o build/atari_data.o src/atari_data.c
```

AltirraBridge: reuse the live server token explicitly when known, e.g.
`%TEMP%\altirra-bridge-12200.token`; the newest token may be stale. Use
`--token ...` with `atari_scroll_probe.py`.

## Session log

### 2026-10-28 — Phase 1 complete

- Extracted and disassembled the cart; confirmed the 48-byte mode-E
  playfield, DLI/PMG mapping and ring/seam layout.
- Wrote `docs/ATARI_RE_SPEC.md` and generated all assets/headers.
- Captured `play_left` / `play_fire` traces; confirmed steering, throttle,
  lives, level countdown and BCD scoring.
- Replaced the noisy sprite sheet with `objects.png` (aux 7–15),
  `objects_all.png` (all 16, data entries marked), `player.png` (7/8/9) and
  `player_all.png` (all 10).
- Fixed the build flag mismatch to `-mmos6502:nes`.

### 2026-10-28 — Phase 2 complete

- Added `build.ps1` and `Makefile`; both compile every `src/*.c` and link
  `roms/river_raid.nes` (32,784 bytes, mapper 30, 2x16 KiB PRG, CHR-RAM) with
  `.noi`/`.map` output.
- Added the module stubs (`river`, `player`, `entities`, `collision`, `sound`)
  with real init/update signatures and flat static state.
- Built the boot screen: shared palette `0F 30 1A 12`, 128-glyph 1bpp charset,
  five scene tiles, status text, wavy river, bridge, BCD frame digits and a
  footer.
- Fixed 1bpp glyph expansion (`_current_1bpp_colors = 0x0001`).
- Fixed the scanline-24 split and added `tests/mcp_client.mjs`,
  `tests/png.mjs` and `tests/nes_smoke.mjs` (46 checks).

### 2026-10-28 — Phase 3 complete

- Replaced the boot screen with a real 3-row HUD (score, HI, GAME, fuel gauge,
  lives, bridge count/icons) and a 30-row playfield ring in NT1.
- Tried a hidden sprite-0 hit split for a dot-exact scanline-24 boundary, but
  the main-loop wait could miss the hit when a river row was being streamed;
  the late split blacked the playfield for one frame (visible flashing).
  Reverted to GBDK's deferred `add_LCD()` split, which always runs from the
  NMI path and produced 0 black frames in an 80-frame stepped scan.  The
  trade-off is a one-line boundary jitter (playfield starts at 24 or 25),
  inherent to GBDK's faked LCD delay.
- Found and fixed the mid-frame scroll issue in the deferred handler:
  PPUSCROLL alone updates only the horizontal bits of `v`; the PPUADDR
  sequence sets the vertical scroll.
- Added `tools/gen_river_demo.py` (82 tiles / 19 tile rows from the captured
  river) and `tools/patch_nes_header.py` (vertical mirroring).
- Added `tests/nes_scroll_hud.mjs`: 120-frame 1-px scroll, fixed top 24 rows,
  split on 24/25, exact `A[y+1] == B[y]` shift, and live HUD fields.
- Updated `tests/nes_smoke.mjs` to 50 Phase 3 checks; both suites pass.

### 2026-10-29 — Source-faithful HUD

- Rebuilt the HUD from the original charset art instead of redrawn text:
  `$43-$46` BRIDGE, `$5A` P1, `$5B` HI, `$5C` lives plane, and the 5-cell
  gauge `47 48 49 48 4A` / `4B 4C 4D 4C 4E`.  Labels are doubled
  horizontally like mode-6 text; GAME uses the ordinary uppercase glyphs.
- Confirmed the fraction is baked into the gauge glyphs (`$49` upper,
  `$4D` lower) and is static.  E, 1/2 and F never change.
- Replaced the shrinking green fuel bar with the source-style yellow level
  marker: a two-sprite (8+2 px, 10 px tall) overlay starting just below the
  top markers and sliding from the F marker (full) to the E marker (empty)
  behind the fixed labels.  The labels are duplicated as black sprites so
  the bar passes underneath them; the 4-px column aligns with the marker
  columns.
- Lives now use the original `$5C` plane glyph as yellow sprites on OAM
  44-46, under BRIDGE.
- Gray HUD band (`00 0F 28 30`); P1 aligns with GAME, HI aligns with
  BRIDGE; the score/hi-score fields suppress leading zeros.  The score
  field is right-aligned at tiles 11-16 (six digits centre over the gauge)
  and the yellow digits are raised one pixel for padding.
- HUD is 4 content rows + 4 px bottom padding (top row 0 is padding); the
  playfield split moved to scanline 36/37 (`LYC=35`).
- Updated both test suites (50 smoke / 14 scroll checks); both pass.
- Documented the full HUD glyph inventory in
  `assets/atari/extracted/README.md` and `docs/ATARI_RE_SPEC.md` §8.1.

### 2026-10-29 - Phase 4 generator and collision ring

- Captured a per-row Altirra trace at `$AB80` (2,600 rows) and built
  `tools/river_model.py`, a validated Python translation of `$A6DE-$AB7E`.
  The model matches every generator field and 48-byte row across levels 1-3;
  only the unused `$B1/$B7` temporaries differ.
- Learned/recorded AltirraBridge breakpoint rules in `AGENTS.md`: condition
  syntax is `=`/`&`/`db($addr)`, frame gates coalesce hits, `CYCLES` chunks
  are needed for per-row capture, and the breakpoint must be cleared before
  stepping off the same PC.
- Ported the generator to `src/river.c`: LFSR, level/section timers, bank
  targets, island/fork logic, feature template selection and the full
  renderer.  Added the 30-row collision ring with left/right/island pixel
  bounds and bridge/island/feature/flash flags.
- Extended `atari_river_widths` to 26 bytes and `atari_river_type_margin` to
  33 bytes to reproduce the original's out-of-range table reads at high
  levels.
- Added `tools/gen_river_atlas.py` / `src/river_atlas_data.*` (51-tile edge
  atlas) and removed the Phase 3 demo tile source.
- Added `tests/nes_river.mjs` and `tests/data/river_golden.json` (400 tile
  rows).  398 sampled rows match the C rows, state and collision ring exactly.
- Found and fixed an SDCC local-alias bug: `ysave` in `river_render_row` was
  reused by a callee, shifting island overlays by 11 bytes.  The saved value
  is now file-scope (`rg_ysave`); added the lesson to `AGENTS.md`.
- Boot no longer generates 30 full procedural rows; the ring is prefilled with
  a straight channel and replaced as the scroll advances.
- All three suites pass: 50 smoke, 14 scroll/HUD, 3 river checks.

### 2026-10-29 - Atari river/tree layout + exact tile cache

**What Atari actually stores (this was the missing insight):**

- The river is **not tile-based**.  Each display-list entry has its own LMS;
  one generated row is one 48-byte mode-E **scanline** (4 pixels/byte), not
  an 8-scanline character row.  There is no tile grid to copy.
- A row is assembled as: land fill, 2-byte bank pattern, water fill, 2-byte
  bank pattern, land fill; the island/fork path adds a second
  land/water/edge sequence.  Bank edges are therefore continuous 1-px
  staircases with a 2-px dark outline.
- A tree/forest is a **template fragment streamed into the row buffers**, not
  a sprite or a tile.  `_feature_select()` (`$A96C-$A9D5`) picks a width
  `bc` (bytes), a left offset `bd`, and a lifetime `be` (rows).  For each of
  the next `be` rows, `copy_template()` copies `bc` bytes from `$BC00+` into
  the row at byte offset `p`, then advances the template pointer by `bc`.
  `bc` is in **bytes**, so trees are 4*bc pixels wide and can start on an
  **odd byte** (4-px, not 8-px, alignment).
- Concrete example from `river_rows_157.bin`: the feature at rows 11..32 is
  `bc=8` bytes (32 px) x `be=22` rows, copied at byte offset 4.  Its
  template rows 21..0 match the captured rows 11..32 exactly.  The 11x19
  red connected component found earlier is only the right-hand part of that
  32-px-wide feature.
- Consequence: the NES 8x8 block at a given screen cell contains 8
  consecutive scanlines of that stream.  The same tree produces different
  8x8 fragments depending on its vertical phase, so **no fixed atlas can
  reproduce it**.  Nearest-of-82 measured 2.69% overall pixel error but 33%
  of tree blocks had >10 wrong pixels (mean 9.4, max 32); edges were almost
  perfect.  That is exactly the "random squares/pixels instead of trees".

**Implemented exact cache:**

- `src/river.c` no longer includes `river_tileset_data.h` and no longer
  nearest-matches.  Each completed tile row builds the 20 interleaved
  16-byte patterns in `river_tile_pat[20*16]` as before.
- `river_cache_tile()` returns `TILE_LAND`/`TILE_WATER` for exact solids,
  reuses the slot of an identical pattern earlier in the same row, and
  otherwise calls `set_bkg_data(TILE_RIVER_BASE + river_alloc, 1, pat)` and
  advances the 207-slot ring.
- New CHR layout (config.h): 0 blank, 1..10 digits, 11..26 gauge, 27..28 P1,
  29..30 HI, 31..38 BRIDGE, 39..46 GAME, 47 land, 48 water, 49..255 cache.
  The full 128-glyph charset load is gone; only glyph 0 (blank) and
  `$50-$59` (digits) stay in CHR.  The HUD labels/gauge are still built from
  `atari_charset` in PRG.
- Score and bridge digits now share the plane-0 digit tiles with the HI and
  game digits.  BG palette 1 (`gray, yellow, black, white`) is selected by
  attribute bytes 2 (`0x04`), 3 (`0x05`), 4 (`0x01`) and 7 (`0x50`) so the
  shared digits render yellow in the score/bridge quadrants and black
  elsewhere.  This costs one 1-px vertical shift on the yellow digits (the
  old separate yellow set was raised one pixel).
- Cache budget: with the ring mapping `river_slot=(2+river_step)%30`, the 26
  visible nametable rows are `{b-2,b-1,b} U {b-29..b-7}`.  Over 40,000 model
  rows the peak per-row slot sum for that visible set is **203** (union 195),
  so 207 slots leave four slots of headroom.  A full 30-row ring would need
  229; do not shrink the cache without re-measuring.
- Verified in Mesen at distance 30: `build/cache_fixed.png` (green land, blue
  water, continuous red 1-px staircase, correctly assembled dark-red trees).
- Removed `src/river_tileset_data.*`, `tools/gen_river_tileset.py` and the
  stale object files.
- Build after the fix: 32,784-byte mapper-30 ROM, BSS ends `0x079D`,
  **98 bytes free**.

**Known state at this pause:**

- HUD visually correct in the screenshot; the attribute-shared digits and
  gauge render correctly.
- `tests/nes_smoke.mjs` and `tests/nes_scroll_hud.mjs` are **expected to
  fail** until their hardcoded tile IDs are updated (they assume
  `TILE_DIGIT_YELLOW=246`, `GLYPH_0=80`, full charset at 0..127).
  `tests/nes_river.mjs` validates rows/state/collision and should still pass,
  but its cadence issue (239/400 samples) remains.
- Performance is deferred.  GBDK's VRAM transfer buffer is `$100-$187`
  (136 bytes, 4-byte stripe header + 32 data); `set_bkg_data` blocks when it
  is full, so a row with more than ~6 new patterns can stall the main loop.
  Average is ~4.8 new patterns/row, peak 13.
- Do not confuse the two "solid" encodings: NES pixel value is
  `(plane1<<1)|plane0`, so mode-E land (1) is `0xFF,0x00` interleaved and
  water (3) is `0xFF,0xFF`.  Writing land as `0x00,0xFF` makes it value 2
  (red) and was the first-build border bug.

### 2026-10-29 - Playfield orientation + test suite

- Player-visible bug: the whole playfield was vertically inverted (trees
  upside down) and the river scrolled upward; the newest tile row appeared
  at the bottom.
- Atari layout check: the live screen puts the **newest row at the top** and
  scrolls down.  The captured fixture is generation order (oldest first), so
  the reference render is upside down relative to gameplay.
- Fixes in `src/river.c`:
  - pack raw row 7 at the top of each 8x8 tile (`7 - river_block_index`);
  - advance the ring backwards (`river_slot` decrements; init 29 so block 0
    writes 28);
  - decrement `playfield_scroll_y` (mod 240) so content moves down.
- Fix in `src/main.c`: GBDK's deferred LCD ISR adds the current scanline to
  `bkg_scroll_y`; the handler now subtracts `HUD_SPLIT_SCANLINE` (mod 240)
  before `move_bkg()`.  Without it the playfield was 36 px ahead of the ring,
  which is exactly why the top row showed up at the bottom.
- Tests updated:
  - `nes_river.mjs`: coverage measured from the first state observed
    (397/397; the first 3 rows complete before the test attaches); rows,
    state and collision all match.
  - `nes_smoke.mjs`: new CHR IDs (blank 0, digits 1..10, HUD 11..46, land
    47, water 48, cache 49..255), shared-digit plane-0 CHR check, HUD
    palette-1 attribute check, no full-charset glyph check.
  - `nes_scroll_hud.mjs`: shared digit IDs, new gauge IDs, resume-at-start
    robustness, deterministic scroll pair with an in-flight-update settle;
    all 14 checks pass.
- Verified in Mesen: `build/flip_fixed.png` (newest row at the top,
  continuous edges, right-side-up trees) and `build/scroll_diff.png`.
- All three suites pass: 50 smoke, 14 scroll/HUD, 3 river checks.

### 2026-10-29 - Frame-time profile and own playfield pattern table

- Profiled with Mesen Lua cycle counters (exec callbacks on `river_update`
  entry / `hud_set_playfield_scroll`): main loop was 0.45 iterations/frame;
  normal frames cost >30k cycles, tile-row frames up to 102k.
- `river_row_to_tile` bit loop -> two 256-byte nibble LUTs
  (`src/river_lut.h`, `tools/gen_river_lut.py`) with an all-land/all-water
  fast path; per-column solid flags (`river_col_kind`) replace 16-byte
  compares against the land/water tiles.  Normal frame ~12k cycles.
- Tile-row classification (dedupe + uploads) is split over two frames
  (columns 0-9, then 10-19 before the next row is converted), the nametable
  stripe is written a frame later, and new patterns go through a small
  upload queue (`RIVER_EAGER` direct, `RIVER_Q_MAX` queued, rest blocking).
- To give that slack the generator runs one tile row ahead (8 rows
  pre-generated in `river_init`, display reveals a row 8 frames after the
  generator completes it).  A full 27-row window needs up to ~231 cache
  slots, more than the old 207, so the playfield now has its **own BG
  pattern table** (`$0000`, 254 slots).  HUD BG tiles moved to `$1000` with
  the sprites; sprite tile IDs moved to 64+; `main.c` sets/clears PPUCTRL
  bit 4 in the VBL/LCD handlers.  `hud_init_graphics` selects `$1000` for
  its BG loads.
- Result: ~0.98 loop iterations per PPU frame (8.1-8.2 frames per tile row).
- Added `tests/nes_river_cache.mjs` + `tools/gen_river_tiles_golden.py`
  (pauses and steps frames; every tile row in the visible window must match
  the Atari model's CHR patterns).  Updated `nes_smoke.mjs` for the split
  tables.  All four suites pass.
- Dead end noted: `set_bkg_tiles` only addresses `$2000`-`$23FF`, and
  `set_tiles`/`set_data` are declared in `nes.h` but missing from the
  library, so the playfield nametable stripe still uses 20 `set_vram_byte`
  calls (~550 cycles each).  A 6-bit rolling tile hash for dedupe was slower
  than plain compares and was removed.

### 2026-10-29 - Out-of-bounds river and garbage trees (levels 10+)

- **Garbage trees:** the extracted template stream was only 512 bytes
  (`$BC00-$BDFF`), but width-10 templates (30 rows x 10 bytes from `$BDC2`)
  run to `$BEEE`; the cart's `$BE00-$BEEE` bytes are real template data.
  `atari_river_templates[]` now holds `$BC00-$BEEF` (752 bytes;
  `tools/extract_assets.py`, `tools/river_model.py` `TPL_STREAM`).  Before
  this, level >= 10 forests read whatever followed the array in PRG.
- **Model bug found on the way:** `river_model.py` `fill()` treated Y as an
  unbounded Python int; the 6502 wraps Y to 8 bits and skips when bit 7 is
  set.  Fixed (`y &= 0xFF; if y & 0x80: return`).  The C renderer already
  did this.
- **Out-of-bounds river:** over 40,000 tile rows (levels up to ~79) the
  generator's banks span source bytes 3..46 (px 12..187), i.e. up to 4 px
  outside the original TV window (bytes 4..43).  The NES port used to show
  only that 20-tile window, so water was cut by hard land columns.  The
  river area is now the whole 48-byte row: **24 tile columns at nametable
  columns 4..27** (`RIVER_TILE_COLS`, `RIVER_LEFT_COL`), land in cols 0-3 and
  28-31.  Jet X mapping for Phase 5: screen X = source px + 32.
- RAM paid for it (+64 B tile buffer, +8): dropped the 48-byte
  `river_debug_row` copy (tests read `river_raw_row` when
  `river_block_index == 0`) and `RIVER_Q_MAX` 4 -> 3.  **29 bytes free.**
- New host harness: `tests/host_river.ps1` compiles `river.c` with `zig cc`
  against stub GBDK headers and checks 20,000 tile rows (all levels) against
  `river_model.py` in seconds.  All 19,999 rows match.  `nes_river.mjs` now
  steps frames (deterministic) instead of pause/resume timing.
- Cache capacity with 24 columns: a window of 27-28 rows needs up to 257-265
  slots in the worst case of 40,000 rows (>254 in only 2-13 windows, all at
  very high levels), so extremely rare glitches remain theoretically
  possible; cross-row pattern sharing would remove them if ever seen.
- Seen at the top of the playfield around tile row 1405: a one-tile-tall
  strip of yellow/white/black dashes across the full width.  This is believed
  to be the bridge row at the end of a level (`flags23 & 0x80`); bridge
  visuals are still an open Phase 5 item.

### 2026-10-29 - Atari-like palette

- Playfield colours now follow the Atari registers: land `$D2` -> NES `$09`
  (dark green), water `$84` -> NES `$01` (dark blue), picked by Lab distance
  against the Mesen default and NES-classic palettes.  `BG_COLOR_BRIGHT_GREEN`
  (`$19`) is reserved for the odd-level `$D6` land colour (Phase 5).
- The **backdrop is now black** (Atari COLBK), so pixel value 0 is real black.
  The HUD gray band is therefore pixel value 1 of the HUD tiles (blank tile 0
  is solid gray); black text is value 0, yellow text value 2.  Black digits
  are tiles 1..10 and yellow digits (score, bridge) tiles 48..57; HUD
  attributes are all palette 0.  Sprite palettes are unchanged.
- Bank outlines (Atari value 2, `$20`) are redrawn as value 0 (black) in the
  raw row: `river_edge_black()` on bank patterns and 0xAA edge/flash fills.
  Value 2 is NES `$07` (dark brown) and is used by the small hills
  (templates width 3, 6, 7: brown + black).  The large blue-highlight hills
  (widths 4, 5, 8, 9, 10) are also run through `river_edge_black()` so their
  bodies are black.
- The Python model has `nes_edges` (default off, so Altirra validation is
  untouched) and `NES_BLACK_HILLS`; the golden generators and
  `tests/host/compare.py` turn it on, so NES raw rows differ from the Atari's
  exactly in those pixels.

### 2026-10-29 - Black world at game start

- Like the Atari, the ring now starts solid black (`TILE_BLACK` = playfield
  tile 2, cache slots 253) and the river scrolls in from the top with black
  below it.  The first `RIVER_RING_ROWS` stripes also write the land margin
  columns (`river_black_rows`); collision bounds of the black rows are 0/0.
- Not reproduced yet: the grey road with a yellow centre line across the land
  at the start of the river (and at level transitions).  Needs the bridge
  work below.
- `nes_smoke.mjs` waits 5 s for the river to fill the screen before the colour
  check and accepts an all-black ring row 3.
### 2026-10-29 - Phase 5 slice 1: player jet

- `player.c`: joypad steering (1 px/frame, clamped to source px 8..184), bank
  sprite (shapes 7/8/9 on OAM 0/1), throttle (Down = slow: `river_speed` 0
  scrolls every other frame; fast not done), fuel burn 1 unit / 8 frames,
  terrain crash (jet nose and tail rows vs `river_left_x/right_x/island_*`
  via `river_slot_at_scanline()`; 0/0 black-world rows ignored), 90-frame
  death delay, respawn mid-river, game over at 0 lives (START restarts).
- Test hook `player_disable` (like `river_freeze`); `nes_scroll_hud` and
  `nes_river` set it. All three suites pass. RAM: 11 bytes free.
- Not done: shot, entities, fuel refill, bridge, scoring, respawn at the last
  bridge, fast throttle, fuel rate parity, fine collision within a tile row.
### 2026-10-29 - Phase 5 slice 2: shot, entities, throttle, death animation

- **Banked code.** The fixed bank ($C000-$FFFF) overflowed (the linker silently
  ran past $FFFF and makenes grew the ROM).  `entities.c`, `entity_gfx.c` and
  `player.c` now build with `-Wf-bo1` (`$bankedSources` in `build.ps1`; the
  Makefile still needs the same) and their public functions are `BANKED`.
  Fixed bank now ends ~$FBB6; ROM is 4 x 16 KiB.  Never call banked functions
  from an ISR.  ~1 KB of fixed-bank headroom left.
- **RAM:** `river_obj_*` arrays shrank to 1 slot (generator clamp hook, unused
  for now: `river_obj_count = 0x0B`).  8 bytes free.
- **Entities** (`entities.c`): 8 slots, spawn every 56 scrolled rows just above
  the visible river inside the water (simplified: the Atari pre-places objects
  in the generator so the bank clamp makes room).  Types from
  `atari_spawn_types` (+ ~25% fuel); ships crawl, helicopters walk, jets
  cross and wrap at $2C..$D4; explosions use aux 2..6 debris frames.  Scores
  via `atari_score_tens` (tens, BCD) + hi-score tracking.
- **Atari object sizes (important):** `SIZEP3` per aux is `$BBA2[aux]`:
  aux 13 ship = double width, aux 14 (`$0E` bridge object, 20 rows) = quad
  width, everything else 8 px.  Left-moving objects use the mirror shape
  (`$BB40[type+0x10]`), done with hardware X-flip.  `tools/gen_entity_gfx.py`
  generates `src/entity_gfx.[ch]` (width-expanded tiles, 3-colour palettes).
- **Shot:** one at a time (A), 6 px/frame, dies on banks/islands, entities or
  the HUD.  **Fuel depots** refuel while overlapped, enemies kill the jet.
- **Throttle (revised):** slow = 0.5 px/frame, cruise = fast = 1 px/frame (the 0.75 cruise felt slow and was reverted). Generating a raw row costs ~1 frame of CPU, so 2 px/frame is
  not reachable; fast = 1 px/frame, cruise = 0.75, slow = 0.5.  Real 2x needs
  generator optimisation.
- **Death animation:** the jet becomes two overlapping debris fragments cycling
  aux 2..6 (36 frames), then hidden until respawn (not yet checked visually).
- `player_disable` also clears entities and forces `river_speed = 2` so the
  1 px/frame test cadence holds.  `nes_river_cache` writes the hooks after
  frame 2 (crt0 clears RAM).  All four suites pass.
- Open: bridge object ($0E, quad-wide) + road visuals, respawn at last bridge,
  entity fidelity (Atari spawn timing/positions, bank clamp), fuel rate,
  shot speed/guidance, sound.
### 2026-10-29 - Entity fixes after play-testing

- **Speed:** profiled with Lua cycle markers on the main-loop calls.  River
  ~10k cycles/frame, HUD ~1k; entities were 4-5k *per entity* because
  `entities[e_i].field` multiplies by the 5-byte struct size.  Now separate
  arrays (`ent_type/x/y/flags/aux`), direct `shadow_OAM` writes (jet too),
  no `/` `%` in the draw loop and the patrol-bounds check every 4th tick.
  Loop rate with 3 entities ~0.75-0.9 per frame (was 0.5).  Cruise returned
  to 1 px/frame.  Further headroom: spawn/bounds/draw are still the biggest
  entity costs; the river row generator dominates.
- **Flip:** only moving enemies flip (left = mirror shape via X-flip, with the
  tile columns reversed for the wide ship); fuel depots and debris never flip.
  Verified by pixel dump (left/right ships are mirror images).
- **Palettes** now come from the real Atari row colours (`palette_altirra_ntsc.bin`)
  by nearest classic-NES colour: helicopters sprite palette 2 (black, yellow,
  blue), ships palette 3 (black, dark red, cyan), jet/fuel/debris share the jet's
  palette 0 (yellow, white, red).  Black parts are only visible over water.
### 2026-10-29 - Start sequence, colours, helicopter animation

- **Start / respawn sequence (verified against Altirra):** at game start
  `$5E` = 0 (jet blank) while `$76` counts `$80 -> $2F` as the river scrolls in
  (lives `$6A` 4 -> 3 at that moment); the jet appears (`$5E` = 9) and everything
  holds at `$2F` until the stick moves or fire is pressed (`A329-A339`), which
  sets `$76 = 0` and starts play.  The NES port matches: `PLAYER_STATE_WAIT`
  (jet hidden until the ring row at scanline 239 is non-black) ->
  `PLAYER_STATE_READY` (jet centred on the river, `river_freeze = 1`, no fuel
  burn or collisions) -> `PLAY` on A / any direction.  Respawn re-enters READY.
  The Atari also shows the road/bridge at the bottom here (still open).
- Water is NES `$11` (brighter), helicopter stripe `$02`; helicopters animate
  (aux 8<->9, 10<->11 every 2nd frame).  Tests updated for the new water colour.
- Altirra server note: a stale `AltirraBridgeServer` may hold port 6502; kill
  all of them before starting, and remember `a.quit()` removes the token.
### 2026-10-29 - Death animations matched to the Atari

- **Jet:** `$5E` steps 6,5,4,3,2 (8 frames each) then 1,0 (blank) after a crash;
  COLPM2 stays `$1E`, so the debris is the jet's yellow.  NES: the extracted
  player shapes 6..2 (14 rows, two tiles each, palette 0) at
  `PLAYER_DEBRIS_TILE`, 8 frames per step, then hidden.
- **Enemies / fuel:** an Atari explosion only changes the object's *type*
  (`$0500`: 2->3->4->5->6->0, every 8th frame, `A271` + `$BB24`); the aux
  (`$050B`: colour strip, row count, `$BB60`) and size register (`$0521`) stay.
  So each class bursts differently: the debris frames (shape `$B900+$BB40[type]`)
  are read for the object's full height, in the object's own colours and
  width.  `tools/gen_entity_gfx.py` now builds those per class
  (`ent_exp_first`); the explosion keeps the class in the high nibble of
  `ent_flags`.  Fuel = tall pink/white cloud, ship = wide dark/red/cyan,
  helicopter = small black/orange.  No recentring of the debris.
- Sprite tiles: entities `+24..+108`, jet debris `+112..+121`.  RAM: 3 bytes free
  (tight).
### 2026-10-29 - Fast throttle (forward)

- Up (forward) now scrolls 2 rows per main-loop pass (cruise 1, slow Down = 0.5
  per pass).  The loop itself is CPU/upload-bound, so measured sustained speeds
  are ~0.73 px/frame cruise vs ~1.0 fast (+30%); more would need a faster row
  generator or CHR upload path.  `player_disable` forces cruise for the tests.
  RAM: 2 bytes free.
### 2026-10-29 - Performance: time-based game, direct VRAM buffer, asm hot loops

- **Symptom:** speed changes slowed the whole game.  Profiled with Lua exec
  callbacks (cycle deltas between function entries, results written to unused
  CHR-RAM at $1E00 and read with `mesen_read_memory memoryType NesChrRam`).
  The GBDK NMI spins ~6.4k cycles (faked LCD wait to scanline 36), so a pass
  has only ~23k cycles before it overruns into a second frame.
- **Measured (jet invulnerable, ~3 enemies):** before 0.74 px/frame cruise
  (0.66 passes/frame fast).  After: slow 0.50, cruise 1.00 (1.00 passes/frame,
  max pass 22.8k), fast 1.52 px/frame (0.95 passes/frame, ~4% of passes still
  overrun).  Fast cannot reach 2.0: two rows + a classification slice per pass
  exceed the budget.
- **Hand-written 6502** (all in their own functions, see lessons): row
  conversion (5.7k -> 2.2k), `river_fill`, row clear, entity sprite emit.
  `river_convert_row_c` stays as the host reference.
- **Direct transfer-buffer writes** (`river_put`): entries are
  `[op=$81-4n][0][hi][lo][data][0]`; library vars `valid`($82 bit 7 = flush),
  `num_cycles_x8` (start 171, entry costs n+5, unsigned!), `pos_w`, `pos_old`.
  Pattern data must be **planar** (8 plane-0 bytes then 8 plane-1 bytes);
  `set_bkg_data` de-interleaves, raw puts must do it themselves.  It never
  blocks: a full buffer makes the job retry next pass.  Only used with the
  display on (`river_live`).
- **Streaming job:** tile rows are classified/uploaded in slices
  (`RIVER_JOB_COLS`=12) and generation waits until done; the upload queue is gone.
- **Time-based logic:** `game_ticks` (main.c, from `sys_time`, clamp 4) scales
  scroll (half-row accumulator `river_acc`), enemy patrol (gated by
  `river_freeze`, not by scroll), explosions, rotor animation, steering,
  shot speed, fuel and death timers.  The scroll runs ahead of the generator
  by at most `RIVER_MAX_OWED` rows (generator lead is 8).
- `hud_update` refreshes one field per pass.  `player_invuln` test hook added.
  Makefile now builds the banked modules with `-Wf-bo1`.  RAM: 34 bytes free.

### 2026-10-29 - Faithful object spawner and patrol (placement + moving helicopters)

- **Symptom:** enemies/fuel were dropped at random every 56 scrolled rows with
  random X; patrol ignored the Atari rules.
- **What the Atari does ($AB80-$AE96, all verified on a live trace):**
  - Objects are placed *by the river generator*, every 16th row
    (`$2F & $F == 0`), from its own state: level `$60`, LFSR `$AA-$AC`, width
    `$5F/$2C`, island flag `$2D`, bridge flag `$23`.  `$2F == 0` rows may give
    fuel (`2*(63-min(level,$2E)) >= $AB` fails, and `$AA` bit 6 clear: ~half
    the rest of the time, rarer as levels rise); `$2F == $10` rows are blank
    placeholders (type 1) below level 8.  Type 12 only at level >= 3 on
    `$2F == 0` rows, jets (7) from level 5, ships (13) otherwise.
  - X is derived from the left bank (`$52`), `$54`, island/width state and
    `$B5`; the object is built for `BB60[type]+4` rows during which nothing
    else spawns and the bank clamp steers around it (`river_obj_*`, which
    were previously stubbed off).  Blank/"nothing" objects (types 0/1) still
    hold the spawner (27 / 9 rows), which is what gives the 16/32-row rhythm.
  - **Patrol ($A271-$A2EA):** nothing moves while `$60 < 2`; every 16 frames a
    random slot (0..10, upper slots twice as likely) is *woken* (`status` bit
    6) - until then an object stands still, so some helicopters never move in
    the early levels on the Atari as well.  Awake objects move 1 px on
    alternate frames (slot parity vs frame counter; the big helicopter every
    4th frame), turn when they touch a bank/island outline (one flip per
    contact, new objects start latched), and wrap at `$2C..$D4`.  Only jets
    (7) fly every frame.  The old port moved helicopters 2x too fast and jets
    2x too fast, all unconditionally.
- **Port:** `entities_row()` (entities.c, bank 1) is the `$AB80` block and is
  called by `river_generate_raw_row()` after each row; the generator state it
  reads is exported from river.c (`rg_*`, `river_owed`).  Entities spawn just
  above the newest generated row.  `MAX_ENTITIES` is 11 (Atari slots), wake
  and patrol use `game_ticks`, so they stay time-based.  `player_disable`
  (test hook) also silences the spawner so the object-free golden river still
  applies; `tests/nes_river.mjs` now sets it before generator row 16.
- **Validation:** `tools/atari_object_trace.py` captures zero page + object
  arrays at `$AB80` with objects enabled; `tools/object_model.py` is the
  Python translation and `tools/object_validate.py` replays traces through it
  (level 1: 1196 transitions, 54 spawns, only death resets differ).
  `tools/object_host_check.py` feeds the same Atari states to the real C
  spawner (host build via `tests/host/spawn_main.c`, needs zig) and compares
  with the model (0 mismatches over levels 1/15/24: 2,539 states, 116 spawns;
  run it after touching `entities.c` spawner code).  An end-to-end NES-vs-Atari
  sequence compare is not possible: the trace starts mid-run at a different
  generator offset.
- **Not ported:** the `$AD9D-$AE92` shot/sound side effects, the 11-slot
  shift/eviction (slots are freed when the object scrolls off), bridge object
  `$0E` (the spawner builds it for timing but no entity yet).
- RAM: 13 bytes free (was 34); 11 entity slots cost 15 bytes.

### 2026-10-29 - Bridge and road, redone from the Atari behaviour

The first bridge port was wrong: it spawned a bridge sprite at the start of
level 1, drew the bar in the ship palette (black/dark red/cyan), and had no
road.  Captured the real behaviour with AltirraBridge
(`tools/atari_bridge_capture.py`, screenshots in `build/atari_bridge/`) and the
DLI code at `$B5D3`:

- **The road is not in the river rows.**  The DLI recolours `COLPF0` (the land)
  per scanline from table `$B4E5` (20 values, top first: dark edge x3, grey x5,
  yellow x2, grey x6, dark edge x4) for every object slot whose status byte
  (`$0042+X`, a copy of `$23` at spawn) has bit 7 set.  So any slot spawned
  while `$23` bit 7 is set paints a road over its 20 rows, across both banks.
- **Game start:** the spawner makes a `$0E` object at the first attempt (status
  `$84`), but its type is cleared at once, so the start shows the road only,
  with no bar.  A real bridge (type `$0E`, quad width, shape rows 2..16 are the
  striped bar, rows 17-18 the pillars) exists only in the last section of a
  level (`$5D == 1`).  Colours `$12/$14/$18/$22` are olive greens, not the ship
  palette.
- **Land colour:** `$AFD0` = `($23 & 4) ^ $D6`, so odd levels are `$D2` (dark)
  and even levels `$D6` (bright); the colour changes at the road (old below,
  new above).
- **Capture pitfall:** poking `$5A >= 3` with `$5D = 1` makes consecutive bridge
  sections (two roads, two bars).  A natural bridge needs `$5D = 1`, `$5A = 2`.

NES implementation:

- `river.c`: a road is 16 raw rows (two tile rows = one 16x16 attribute row)
  painted into the generated rows (`river_road_paint`: land pixel 1 -> black
  edge / grey / yellow line, profile `river_road_profile`), starting on an odd
  ring slot; those tile rows use BG palette 3 (black, grey `$00`, yellow `$38`,
  water).  The margin columns 0-3/28-31 get solid road tiles `TILE_ROAD_A/B`
  (253/254; 255 is the "none" sentinel), written through `river_put` kind 3
  (staged in `river_tile_pat`) together with the attribute byte; the slot pair
  is remembered in `river_road_slot` and restored to land one lap later.
  Every tile row also picks BG palette 2 (dark land) or 1 (bright land) from
  `river_land_alt`, which flips when a bridge road ends (`rg_countdown == 1`).
  The game-start road is scheduled by `river_init()` (`-DRIVER_NO_START_ROAD`
  for the host harness).
- `entities.c`: a bridge at `rg_countdown == 1` schedules the road and sets
  `bridge_wait`; `entities_update()` creates the bar (aux 14, palette 1) once
  `river_road_top()` is representable (the generator runs ahead of the ring's
  hidden rows, so the slot's scanline wraps for a few frames).  The game-start
  `$0E` attempt only keeps the spawner timing.  Shooting it scores 500 and
  increments the bridge counter.
- Sprite palette 1 is now the bridge (olive); the HUD label sprites moved to
  sprite palette 2 (entry 1 is black).
- ROM: Atari tables only the Python tools read are now behind
  `ATARI_DATA_TOOLS` (generator `tools/extract_assets.py`), which freed ~660
  bytes of the fixed bank.  **RAM is down to 1 byte free.**
- Tests: `nes_smoke` accepts road attribute bytes, `nes_river_cache` skips the
  start-road tile rows (steps 1-2), host harness defines
  `RIVER_NO_START_ROAD`.  All Mesen suites, `host_river.ps1` (19,999 rows) and
  `object_host_check.py` (0 mismatches) pass.
- Differences from the Atari: the road is 16 scanlines instead of 20 and has
  three tones (black edge, grey, yellow) instead of four; the bar's stripes use
  three NES colours.  Still open: respawn at the last bridge, fuel rate
  parity, sound, a visual check of a natural level-1 bridge in play.
### 2026-10-29 - Bridge tank and its shell

What the Atari does (disassembly `$A21B-$A26A`, `$A4EA-$A545`, `$A609-$A671`,
`$ADA6-$ADCA`, `$AEA3`; captured with `tools/atari_bridge_capture.py`):

- The "tank" is the P1 object (`$38` state, `$39` X, `$3A` Y).  The spawner
  creates it in every bridge section (not at game start), at X =
  `($AA & $7F) + $40` from the LFSR (so its start is fixed by the level) and
  facing right (`$38 = 5`) when `$AA & $7F < $40`, else left (`$38 = 6`).  Its Y
  scrolls with the river, 4 rows below the bridge bar's top; it is removed when
  `$3A >= $BC`.
- Shapes `$B4D1[state]`: 4/5 = facing right, 6/7 = facing left (the two tread
  frames), 3/1 and 2 = explosion.  `$BAAF` is the next-state table
  (4<->5, 6<->7, 3->2->1->0).  Driving frames change every 4th frame, the
  explosion every 16th.  It moves one pixel on those ticks, toward the side it
  faces, only while `$2E == 0` (`$2E` is cleared when the bridge section starts
  and incremented when the bridge is destroyed) and no shell is flying
  (`$50 >= 0`).
- Shooting the bridge (`$A4EA`) with the tank on it (`bridge_x - 8 < tank_x <=
  bridge_x + $1F`) destroys the tank (bit 7 of `$38`) and scores
  `$BB0B[$10]` = **750 instead of the bridge's 500**.
- Otherwise, once the bridge counter (`$61/$62`) is >= 5 and the tank is within
  `$29` pixels on the side it is driving from (`$41` when facing left), it
  fires: `$50` counts 0..`$38` per frame.  The shell (P0, shapes `$BAB7[1..6]`)
  starts 4 px in front of the tank and 6 rows above it, flies 17 frames at
  2 px/frame (`$3E:$3F` = 2.0) with a few pixels of vertical drift (`$B4AF`),
  stays put for 40 frames while it swells (shape 2..6, 4 shapes per 8 frames),
  and **kills the jet only while `$50 >= $11`** (`$A428`).  At `$39` it
  restarts from the tank, so it keeps firing while the tank is on screen.

NES port (`entities.c`, no extra RAM: the tank/shell are entity slots, type
16/17, with the Atari `$38`/`$50` kept in `ent_aux`):

- `ent_tank_spawn()` (bridge spawn) makes a positioned-later tank
  (`ENT_F_WAIT`); `ent_tank_place()` puts it 4 rows below the bar when the bar
  is created.  `ent_tank_tick()` / `ent_shell_tick()` run per game tick;
  `ent_bridge_hit()` applies the 750 bonus / the shell rule; the jet dies to
  the fireball through `ent_shell_hits()` (box per swelling shape).
- Sprite tiles (`tools/gen_entity_gfx.py`): tank frames and explosion from the
  cart (2 tiles each), shell shapes 1..6; the tank uses sprite palette 2
  (black), the shell palette 0.  Entity tiles are now 109, so the jet's debris
  tiles moved to `SPRITE_TILE_BASE + 24 + ENT_GFX_TILES`.
- Verified in Mesen with a forced bridge section: tank drives toward the
  bridge; shooting it with the tank over it scored 750 (860 - 110); with the
  counter at 5 and the tank approaching it fired (shell flew 16 frames then
  swelled, refired every 57 frames) and a plain hit scored 500.
- Not ported: the sound effects (`$7C` = `$37` tank bonus, `$17` hit; the shell's
  fire sound), the shell destroying enemy objects (`$A4C1`), and the same shell
  mechanism for helicopters/ships (`$AE22-$AE92`, from level 7).
### 2026-10-29 - Tank priority, bridge debris, bridge-hit flash

Captured a real bridge hit on the Atari (`tools/atari_bridge_hit.py`: stops at
the collision handler `$A3E9` and sets `$1C` to the bridge slot so the game's
own hit path runs; `--tank-over` pokes the tank under the bridge; frames in
`build/atari_hit_*`).  Confirmed: tank bonus score `$75` (750), bridge counter
+1, `$38` = `$86` -> 3 -> 2 -> 1 -> 0 (16 frames per step), bridge type 2..6
(8 frames each), flash timer `$31` = 22..0.

- **Tank was drawn behind the bar.**  The entity loop allocated OAM in slot
  order, so the bar (lower slot) won.  The Atari draws P1/P0 over P3, so the
  tank and shell now have fixed OAM slots 2-3 / 4-5 (`ENT_OAM_TANK/SHELL`);
  other entities start at `ENT_OAM_FIRST` = 6.
- **Garbled bridge debris.**  The bridge's explosion reused the tiles built
  for another class (1 column wide) but was drawn 4 columns wide.  The
  Atari's debris is the same debris shapes (`$B900 + $BB40[frame]`) over the
  bridge's 20 rows, 4x wide, in the bar's stripe colours: thin horizontal
  speckle dashes (this is what the original shows).  `gen_entity_gfx.py` now
  builds those as 32 shared tiles plus `ent_exp14_map[]` (tile per frame, row,
  column; 0xFF = blank, not drawn) and `ent_draw_bridge_debris()` draws them.
  Entity tiles: 141.
- **Water flash.**  `$A4EE` sets `$31 = $17` on every bridge hit; the VBI
  (`$B41C`) switches `COLPF2` from `$84` to `$42` (a dark red) on every frame
  whose remaining count is odd, for 23 frames.  It is `COLPF2`, so the blue
  highlights on hills flash too.  `ent_flash_tick()` swaps entry 3 of the
  three land palettes (`BRIDGE_FLASH_COLOR` = NES `$07`).  The tank case runs
  the same code (the flash is not specific to it).
- `bridge_wait` was merged into `bridge_x` (0 = none) to pay for the flash
  counter; RAM is still 1 byte free.  Host stubs (`tests/host`) gained
  `set_bkg_palette_entry` and a `prop` alias for the OAM attribute.
### 2026-10-29 - Throttle ramp, tank-only bridge flash, remaining Phase 5 work

- **Throttle acceleration.** `river_speed` (0/1/2) is now only the target;
  `river_vel` (`river.c`/`river.h`, 1/32 row per frame) eases toward it at
  `RIVER_ACCEL` = 1 unit per elapsed frame (slow 16, cruise 32, fast 64), so
  cruise -> fast takes ~32 frames and cruise -> slow ~16.  `river_acc` is a
  5-bit sub-row accumulator.  `river_vel` snaps to cruise on `river_init`,
  respawn, level reset and `player_disable` (tests keep 1 px/frame).  The ramp
  rate is a guess; the Atari's `$76` throttle byte is analogue (128 -> 0 -> 233
  in the play_left trace) but its rate has not been traced.  Build only; not
  yet play-tested or run against the Mesen suites.
- **Bridge flash is now tank-only (deliberate deviation).**  The Atari sets
  `$31` on every bridge hit (see the previous entry), but by request
  `entities_hit_shot` only starts `bridge_flash` when `ent_bridge_hit()`
  returned `TANK_BONUS_TENS` (tank on the bridge).  A plain hit still scores
  500, explodes and advances the counter.  Revert by setting `bridge_flash`
  unconditionally.
- RAM is again **1 byte free** (the throttle state cost 8); any new global
  needs something reclaimed first.
- **Remaining Phase 5:** respawn at the last bridge; sound (engine, shot,
  explosions, tank bonus/hit); fuel rate / 16-bit gauge / PAL `$BF`; shell
  killing enemy objects (`$A4C1`) and helicopter/ship shells from level 7;
  entity fidelity (generator-placed objects + bank clamp, 11-slot eviction,
  spawn timing); collision sampled at the jet's exact scanline instead of once
  per tile row; visual checks (natural level-1 bridge, bridge/feature A/B,
  death animation, 20-row 4-tone road); commit the pending object-model tools.
  (The `Makefile` banked-source list now matches `build.ps1`; `build.ps1` is the
  tested path.)

### 2026-10-29/30 - Performance: 30 Hz game logic, 60 Hz river (branch `experiment/river60-logic30`)

**Symptom.** Game speed was inconsistent: it slowed whenever there were more
entities, shots or explosions, and fast flight (Up) was unplayable.  Considered
and rejected: a rewrite on cc65 + MMC3 (assessed with the `MMC3Template` repo: it
would recover the ~5.4k-cycle NMI split spin and give 8 KB WRAM, but needs a CHR-RAM
variant, a new split/IRQ handler, ca65 rewrites of all asm and a new test harness
- 4-8 sessions) and a black bar at the bottom of the screen (a second split costs
a full frame of NMI spin with GBDK's faked LCD, and does not reduce the per-row
generation cost).

**Method.** Everything was measured in Mesen with frame-stepped runs (invulnerable
jet, shooting every 20 frames, ~5 entities); see `tests/perf_*.mjs`.
`perf_function_cycles.mjs` and `perf_frame_trace.mjs` arm a persistent Lua exec
callback over `$8000-$FFFF`, add each instruction's base cycle cost to the
function (symbol range) it is in and publish the totals through unused CHR-RAM
(`$1F00`).  Numbers are ~10-15% low (taken branches ignored) but good enough to
rank costs; the frame budget is 29,780 cycles of which the NMI split spin
(`.delay_to_lcd_scanline`) takes ~5.4k.

**What the measurements showed (in order).**
1. The 30 Hz experiment (`MAIN_FRAMES_PER_PASS`, commit `c5aefd9`) was *not* CPU
   bound: 299/300 passes took exactly 2 frames, but cruise scrolled only 0.80
   px/frame because the generator could make `RIVER_GEN_PER_PASS` = 2 rows per
   pass, exactly the cruise demand, so every pass blocked by a pending tile job
   lost its scroll rows for good.  `RIVER_GEN_PER_PASS` 4, `RIVER_MAX_OWED` 6,
   `RIVER_VEL_FAST` 48 fixed that (cruise 1.00 px/frame).
2. A pure 30 Hz loop scrolls in 2-row steps and fast became choppy, so the loop
   became a hybrid: `river_update()` every frame, the rest at 30 Hz.
   `river_scrolled` now *accumulates* until `entities_update()` consumes it.
3. A per-frame trace (`perf_frame_trace.mjs`) found the real peaks: the tile-job
   slice cost **10-13.5k cycles** (the duplicate-pattern search was a C loop of
   ~100 cycles per candidate pair) and coincided with entity move/draw (4-7k) on
   the frames that overran.
4. Average cost is not the problem: in fast the idle wait loop still took ~22% of
   the frame, but 36% of frames carried 20-24k of work.  Peaks matter.

**What changed (commits, oldest first).**
- `c5aefd9` 30 Hz loop experiment; `86c1837` generator catch-up constants;
  `0b5f16c` hybrid loop (river 60 Hz / logic 30 Hz).
- `ec0a491` 8-bit wrap-around box tests in `entities_check_player` /
  `entities_hit_shot`; the player group and the HUD+entity group run on
  alternate frames; `entities_update()` measures its own elapsed time from
  `sys_time` (`ent_anim` now holds the previous run's time and doubles as the
  rotor-animation clock).
- `9c3d5e0` `ent_scan_player()` in hand-written 6502 (20-case boundary test
  `nes_entity_hits.mjs`).
- `38d5eb1` `river_find_dup()` in hand-written 6502: the tile job's
  duplicate-pattern search (job spikes 10-13k -> a few k; also frees 4 bytes of
  RAM and ~130 bytes of fixed bank).
- `017e919` `ent_draw_reg()` in hand-written 6502: common draw path for object
  types 7..15 (explosions, bridge debris, tank and shell stay in C).  Verified by
  diffing the OAM of 9 planted scenes against the previous ROM (only rotor
  tiles differ, by one animation frame).
- `b606feb` **repeat-row skip**: a plain row (no feature template, no hit flash)
  is a pure function of `rg_left`, `rg_phase`, `rg_width` and the island flag;
  33.1% of all rows repeat the previous one (`tools/river_repeat_rows.py`: 30,000
  rows, 0 false positives).  Such a row skips `river_render_row()` and, inside a
  tile row, `river_copy_line()` copies the previous converted scanline instead of
  converting again.  The signature (`rs_*`) is invalidated by init, flash/feature
  rows and road painting.  Because that overflowed the fixed bank, **`hud.c` moved
  to bank 1** (`hud_init_palette/graphics/update` are `BANKED`;
  `hud_set_playfield_scroll` is now in `main.c`).
- `2fb94f5` (intermediate) entities at 60 Hz; `9963cf7` (final) entities back at
  30 Hz plus **`entities_follow()`**: on the in-between frames it adds
  `river_rows_last` (this frame's scroll) to every entity sprite's OAM Y (shell
  slots excluded), so sprites stay locked to the river (`nes_sprite_lock.mjs`:
  80/80 frames locked in cruise, vs 40/80 off by 1 px without it).

**Result (same scenarios).**

| | start of the hybrid | final |
|---|---|---|
| cruise: late passes / frames | 29/569 (5.1%) | 3/596 and 6/900 (0.5-0.7%) |
| cruise: mean work, normal frames | ~13.3k | ~12.0k cycles |
| fast: speed | 1.46 px/frame, steps mostly 0 or 3 rows | 1.50, steps 1-2 rows evenly |
| fast: late passes | 68% | 19-23% |
| fast: mean work, normal frames | ~18.1k | ~15.8k cycles |

Cost table (cruise, cycles per frame): convert 1.7k, render 1.6k, job 1.3k,
generator state + misc 1.7k, entity move/draw block 1.6k, entity update loop
1.1k, shot/spawner bucket 0.6k.  The heaviest remaining frames are tile-row
boundaries that generate 2-4 rows at once (render 7-12k + convert 4-6k).

**Lessons (also in AGENTS.md).**
- A profile bucket for banked code covers every static up to the next *public*
  function (banked statics have no `.noi` symbols).  `entities_check_player`
  appeared to cost 2.45k/frame; that bucket was really the whole move/draw block.
  To see statics, temporarily drop `static` (24 functions were exported with a
  sed one-liner, profiled, and reverted with `git checkout`).
- SDCC's `setupDPTR` array pattern is *not* always the cause of slow code: the
  assembly of `ent_draw` has one `setupDPTR`; its cost is plain instruction count.
  Count `setupDPTR` / `jsr __mul` per function in `lcc -S` output before assuming.
- Persistent Lua scripts survive `mesen_load_rom`; restart MesenCE-MCP between
  profiling runs or stale scripts overwrite the mailbox.
- `.noi` addresses move whenever the RAM layout changes: read them from the file,
  never hard-code them in a probe.
- Prove an optimisation with a diff, not just a test: `compare_oam.mjs` /
  `compare_tile_rows.mjs` capture what is on screen for two ROMs.
- Test on hardware-like pacing: Mesen frame-stepping hides mid-pass NMI effects.

**Not done / options.** Commit the scroll at the end of the pass (removes the
mid-pass sprite/scroll desync on late fast frames); cheaper render/convert;
entity update loop; MMC3 + cc65 (rejected, see above); PAL 50 Hz (would need the
NES region, a longer vblank and NMI spin, and no Atari PAL traces).

**Verification state at the end of the performance work (before the compact HUD below):** `nes_smoke`, `nes_scroll_hud`, `nes_river`,
`nes_entity_hits` (20/20), `nes_sprite_lock`, `host_river.ps1` (19,999 rows) and
`object_host_check.py` (2,539 states, 116 spawns, 0 mismatches) pass;
`nes_river_cache` is stale (see Current status).  Play-tested on a MiSTer:
cruise smooth, fast playable.

### 2026-10-30 - Compact HUD (branch `feature/hud-compact`)

The HUD looked heavy: 8 px of blank padding on top and 4 px below the text.  It is
now 24 scanlines of content + 1 padding line, and the playfield grew from 204 to
215 scanlines.

- **How:** the split moved from scanline 36 to 25 (`HUD_SPLIT_SCANLINE` 25,
  `HUD_SPLIT_LYC` 24).  Instead of moving every HUD tile up a row, the HUD pass
  scrolls nametable 0 by `HUD_SCROLL_Y` = 8 (`vbl_handler` in `main.c`), which drops
  the blank row 0; the HUD sprites (gauge labels, fuel bar, lives) got `HUD_*_Y`
  8 lower.  `PLAYFIELD_SCANLINES` 215.  The assembly in `ent_draw_reg()` has the
  split line hard-coded (`cmp #25`) behind a compile-time assert.
- **Spawn height:** objects are placed above the newest generated row, with
  `HUD_SPLIT_SCANLINE` rows of headroom; the 24-row fuel depot no longer fits
  (25 - 10 - 24 < 0 and an 8-bit Y wraps into the culling window), so
  `entities_row()` clamps the spawn Y at 0.  Checked over 800 frames of fast
  flight: every depot spawns at y 0-6 and lives its whole ~155-frame trip.
- **Ring margin:** the new `tests/nes_ring_integrity.mjs` (no visible ring row may
  change between two frames) passes on the old ROM (204-line window, 13,076
  checks) and the new one (215 lines, 13,694 fast / 11,172 cruise checks); as a
  negative control, claiming a 235-line window on the new ROM flags 46 rows; the
  real ROM stays clean up to a 223-line window, so there are at least 8 px of
  spare margin.
- **Cache pressure:** see Known issues 1d (a 29-row worst-case window, ~0.1% of
  positions at level 10+).
- **Performance:** the split spin fell from ~5.4k to ~4.1k cycles per frame;
  cruise 0.7-0.8% late frames (unchanged); fast 16% late frames in the trace and
  19.8% late passes (before: 19% of frames, 23% of passes).
- **Tests:** `nes_smoke` (50), `nes_scroll_hud` (14) and `nes_river` pass with the
  new geometry (split 25/26, HUD rows at y 4/10/18, lives sprites at y 15).
- **For the user:** `BUGS.md` lists "tank shots scroll with the river"; the shell
  (OAM slots 4-5) is deliberately excluded from `entities_follow()`.

### 2026-10-30 - HUD padding (branch eature/hud-compact)

The compact HUD had too little air around the text.  It now has 3 px of padding
above and 3 px below the 24 content scanlines (30 total); the playfield is 210
scanlines.

- **How:** HUD_SCROLL_Y 8 -> 5 (the blank nametable row 0 now shows 3 lines),
  HUD_SPLIT_SCANLINE 25 -> 30, HUD_SPLIT_LYC 24 -> 29, PLAYFIELD_SCANLINES 210,
  and every HUD_*_Y sprite position +3.  nt_draw_reg() has cmp #30 behind its
  compile-time assert.
- **Side effects:** 5 px less playfield eases the CHR-cache pressure of Known issues
  1d (about 1 tile row less in the worst window) and gives the entity spawn code
  30 rows of headroom instead of 25.  The split spin is ~0.6k cycles longer
  per frame than at 25 (not re-profiled).
- **Tests:** 
es_smoke, 
es_scroll_hud (split 30/31, HUD rows sampled at y 7/13/21,
  lives sprites at y 18) and 
es_ring_integrity.mjs (default split now 30; 0 of
  18,828 visible-row checks changed) pass.  ROM is 65,552 bytes.

### 2026-10-30 - Narrow gaps (level 50) were impassable (branch main)

Two causes, both found in the Atari code:

- **Steering is a ramp** (`$A455-$A493`): while Left/Right is held `$2B` grows by 8
  per frame (saturating at 248) and is added to the 8-bit fraction `$58`; every
  carry/borrow moves the jet 1 px.  From rest: 1 px on frame 1, ~1 px per 8 frames,
  full 1 px/frame after 31 frames; releasing clears both.  Right also steps on
  its first frame via `$B9` (previous stick, bit 7 = right not held).  Verified
  frame by frame against `assets/atari/runtime/play_left` (`$57` 124 -> 111 in 28
  frames, `$2B`/`$58` identical).  `player_steer_tick()` runs it once per elapsed
  60 Hz tick.  The port used to move 1 px/frame at once.
- **The hit box was the 8x14 cell.**  The Atari tests the sprite's lit pixels
  (P2PF): the straight jet (shape 9) is columns 1..7, both banked jets (7/8)
  columns 2..6, so a banked jet is 5 px wide.  `player_hits_terrain()` now ORs the
  shape rows that fall in each 8-row tile row (all 2-3 of them; the old code only
  probed the first and last row) and tests the min/max lit column.
- **Tests:** new `nes_steer.mjs` (matches the Atari ramp: dx -13 after 28 frames, tap =
  1 px, full speed 1 px/frame) and `nes_jet_hitbox.mjs` (survives / crashes at
  the exact pixel); `nes_gameplay.mjs` re-centres the jet before the crash-sensitive
  parts.  RAM 83 bytes free (was 96).
- **Not done:** the ring still samples the river bounds at raw row 3 of each tile row
  (Known issue 3), so the edge can be off by a few px against the true per-scanline
  edge.  If a gap is still tight, store per-scanline bounds or sample the jet's rows.

### 2026-10-30 - Hitboxes lagged the sprites (branch main)

Reported: enemies (the ship above all) felt smaller than their tile.  The boxes
were the right size (ship 16x8, jet 8x14); the entity Y they were compared with
was stale.

- **Cause:** `entities_follow()` moves the entity sprites with the river on the
  player's frame, but `ent_y[]` is only advanced by `entities_update()` on the
  other frame.  On the frame the hit tests ran, the true position was
  `ent_y + river_scrolled` while the box used `ent_y`: the box trailed the sprite
  by the pending scroll (grows with speed; worst when flying fast).
- **Fix:** `entities_check_player()` and `entities_hit_shot()` shift the jet/shot
  Y by `-river_scrolled` (the box test only sees the difference, so the asm
  `ent_scan_player` is unchanged).  (`ent_shell_hits()` used to undo the shift
  because the shell was thought not to scroll; see the tank-shell entry below.)
- **Tests:** `nes_entity_hits` 20/20 and `nes_sprite_lock` pass, but both run with
  the river frozen or sprite-only, so **the scrolling case is not covered by a
  test yet** - verify by flying fast into a ship.  ROM 65,552 bytes.

### 2026-10-30 - Tank shell scrolled wrongly (branch main)

Reported in `BUGS.md`: "tank shots are scrolling with the river" - the shell
drifted against the river while the tank moved with it.

- **Cause:** a wrong assumption.  The shell (and `entities_follow()`) were
  written so the shell stayed fixed on screen.  The Atari does not do that: the
  scroll step `$AB7C-$AB7E` runs `inc $3A` (tank Y) and `inc $40` (shell Y)
  together with all other objects after every generated row, so the shell moves
  down with the river, 1 px per row.
- **Fix (`entities.c`):** `entities_update()` scrolls the shell like every
  entity; `entities_follow()` no longer skips OAM slots 4-5 (asm and C
  fallback); `ent_shell_hits()` no longer undoes the `river_scrolled` shift of
  the jet Y; `ent_shell_tick()` adds the pending scroll when it refires from a
  tank in a later slot (not scrolled yet in that pass).  A shell that scrolls
  off the bottom is culled by the normal 232..249 window.
- **Verified:** by eye in Mesen with a temporary ROM that made every tank fire on
  every bridge (bridge counter / range / on-bridge kill gates disabled); that
  debug code is removed again.  No automated test covers the shell yet.  ROM
  65,552 bytes, 4 bytes RAM free.

### 2026-10-30 - Respawn at the last cleared bridge (branch main)

A lost life used to respawn the jet mid-river.  The original restarts the whole
river from a checkpoint; the port now does the same.

- **What the Atari does** (`$A3BC`, `$B0F0`, `$A3C8`, `$B1C0`, `$B194`): on a death with
  lives left, `$B0F0` runs with X=`$12` (reloads only the generator defaults
  `$4D-$5F`; clears the ring, `$2B-$4C` and the objects; keeps level, score and
  lives), then `$A3C8` loads the LFSR (`$AA-$AC`) and `rg_left` from `$66-$69`.
  `$66-$69` are saved by `$B194` every time the generator enters a new section;
  `$B194` also keeps the previous set in `$AD-$B0`.  `$B1C0` decides which set
  the restart uses: if the level parity (`$23` bit 2, against `$4C`, its value
  at life start, `$B178`) flipped during the life, then with a bridge destroyed
  (`$2E != 0`) and the generator still in the bridge approach (`$23` bit 7) it
  advances the checkpoint (`$B194`); with no bridge destroyed and the approach
  already passed it goes back one section (`$AD-$B0`, level - 1).
- **Port:** `river_start(restart, bridges)` (`river.c`) is the old `river_init`
  with a seeded variant; `river_restart(bridges)` applies the `$B1C0` rule using
  `rg_prev[4]` (new), `rg_life` (new: parity in bit 7, bridge counter at life
  start in bits 0-6; `$2E != 0` is "`hud_bridge` changed") and `river_level_advance()`
  now keeps the previous seeds like `$B194`.  `player_respawn(new_game)` calls it,
  clears the entities and re-enters the game-start sequence (`PLAYER_STATE_WAIT`: the
  river scrolls in, then the jet waits for input).  START on game over calls
  `river_init()` (level 1) and resets `hud_bridge`.
- **RAM:** paid for by dropping the write-only `rg_b1` and by shrinking
  `river_debug_state` to 12 bytes on the NES (the host check still gets 16).
  **1 byte free.**  ROM is 65,552 bytes, `_XINIT` ends at `$F524`.
- **Verified:** `nes_respawn.mjs` (new; crash at level 1 -> life lost, level 1 checkpoint,
  ring refilled, jet waits, input resumes) plus `nes_smoke`, `nes_scroll_hud`,
  `nes_river` (390/390), `nes_entity_hits` (20/20), `nes_sprite_lock` and
  `host_river.ps1` (19,999 rows).
- **Not verified:** the `$B1C0` branch itself (advance / rewind) was derived from the
  disassembly, not traced in Altirra: it needs a death before the first bridge
  (no flip), after shooting one and while crashing into one (`$A3F4` also counts a
  crash into a bridge as cleared).  The port's spawner uses its own RNG, not the
  generator LFSR, so objects after a restart differ from the Atari's.

### 2026-10-30 - Fuel rates checked against the disassembly (branch main)

Open item "fuel rate / 16-bit gauge" traced from `$A3E9-$A453` (no Altirra run).

- **Gauge:** 16-bit `$5B:$5C`, start `$FFFF`; the marker is `HPOSM1 = $5C + ($5B >> 3)` (`$B527`).
- **Consumption:** every normal frame `$A43F` does `$5C -= $20`, borrow into `$5B`, so one
  high-byte unit per 8 frames.  Empty (`$5B == 0`, `$A44A`) crashes via `$B1C0`.  The port's
  `PLAYER_FUEL_FRAMES = 8` already matched.
- **Refuel:** on a type >= `$0F` hit `$A40A` runs `lda $5B / adc #1` with carry set by the
  preceding `cmp #$0E`, i.e. **+2 per frame**, saturating at `$FFFF` (`$A412`).  The port added
  3 per 30 Hz pass (1.5/frame); it is now `PLAYER_REFUEL_RATE (2) * pl_ticks`.  The low byte
  is not refilled until saturation, as in the original.
- **Assumption, untraced:** that `$A3E9` runs once per game frame.  Confirm with an Altirra
  breakpoint on `$A43F` if the fuel pace ever feels off.  PAL `$BF` only moves the bar's PMG
  rows on the Atari, so it has no NES counterpart.
- ROM 65,552 bytes, RAM now 0 bytes free.  Not run: Mesen suites (no test covers refuelling).


### 2026-10-30 - Phase 5 closed out: gameplay test (branch main)

- **New `tests/nes_gameplay.mjs`** (8 checks, all pass): steering + banking, throttle
  (`river_speed` 0/1/2), depot refuel (+7 in 4 frames, the +2/frame rule), fuel burn
  (8 units in 64 frames), and a planted ship killed by a shot for exactly 30 points.  It plants
  entities straight into `ent_*` and uses `player_invuln` where fuel must not be forced to full.
- **Scores:** the table already matched the original (ship 30, helicopters 60, jets 100/150, fuel 80,
  bridge 500, tank bonus 750), so the "candidate" note in the spec is resolved.
- Re-ran `nes_respawn`, `nes_entity_hits` (20/20) and `nes_scroll_hud`: pass.
- **Still open from Phase 5:** (1) the `$B1C0` checkpoint advance/rewind branch is derived from
  the disassembly, not traced (needs deaths before / after / into a bridge in Altirra);
  (2) bridge/road visuals (Next step 0); (3) `nes_river_cache.mjs` is stale.


### 2026-10-30 - Phase 5 finished: checkpoint verified, cache test repaired, bridge re-checked

- **`$B1C0` checkpoint rule verified** with the real game.  `tools/atari_checkpoint_trace.py`
  breaks at `$B1C0`, pokes sentinel seeds and inputs (level parity flipped since life start,
  `$2E` zero / non-zero, `$23` bit 7, odd and even levels = 16 cases), and reads `$60`,
  `$66-$69`, `$AD-$B0` at `$B1F8`.  `tests/host_restart.ps1` (harness `tests/host/restart_main.c`
  includes `river.c` for its private state) feeds the same inputs to `river_restart()`:
  **16/16 match**, covering advance (level+1, new seeds, old ones saved), rewind (level-1)
  and keep.  Trace = `build/checkpoint_trace.jsonl` (about 25 min: the bridge round-trips are
  slow).  Caveat: the port models `$2E != 0` as "hud_bridge changed since life start"; a
  bridge count already at `HUD_BRIDGE_DIGIT_MAX` would not register (not reachable in
  normal play).
- **`tests/nes_river_cache.mjs` repaired.**  Not a generator bug: the ring starts as a
  straight-channel prefill that the first procedural rows replace while the game-start road
  scrolls in, so the early frames legitimately differ from the model.  The test now checks
  only from distance 33 on and uses the 210-line window: 12,780 visible rows match exactly
  (the only mismatches are the newest rows, still above the window).
- **Road/bridge visuals:** already built (2026-10-29 entries); the "Next step 0" note was
  stale.  Forced a natural level-1 bridge in Mesen (`rg_countdown = 1`, `rg_section_timer = 2`)
  and compared with the Atari capture: grey road with dark edges across both banks, olive
  striped bar with the tank, land colour change at the road all match.  Remaining known
  difference: the road is 16 scanlines (Atari 20) because it is aligned to the 16x16
  attribute grid.
- **Phase 5 is closed.**  Remaining items are Phase 6: APU sound, title/attract, game
  variations, game over flow.


### 2026-10-30 - Phase 6: RAM freed, first APU sound pass

- Freed 160 bytes of internal RAM: the HUD (`wide_tile_buf`, `gauge_sprite_buf`) and player
  (`player_tile_buf`) boot-time tile buffers now alias `river_tile_pat` (made non-static, declared in
  `river.h`); the unused `hud_row_buf` was removed.  RAM is 152 bytes free after the sound state.
  Commit `339724b`.  Smoke, scroll/HUD, gameplay, entity-hits, respawn, `host_river` pass.
- Added `src/sound.c` (see "Next step" item 1 for the exact design) and `tests/nes_sound.mjs`.
  Commit `7d4a1ac`.  Cadence unchanged (cruise 0.8% late passes, fast 23%).
- The user then listened to it and reported that it sounds "way off"; a different agent is to
  take over the fix.  The register-write test cannot detect that class of problem.


### 2026-10-30 - Phase 6: sound re-derived from the Atari (shots, engine noise)

The user reported two concrete faults: shots an octave or two too high, and the
jet engine sounding like a pulse wave instead of noise.  Both were correct - the
first sound pass was estimated, not derived - so the whole engine was rebuilt
from captured Atari behaviour.

**Ground truth instead of estimates.**  `tools/atari_sound_trace.py` dumps, once
per frame, the four POKEY `AUDF/AUDC` pairs, the zero-page variables that gate
them, and Altirra's decoded `audio_state()` (per-channel volume, distortion,
clock and `freq_hz`).  `tools/atari_sound_events.py` pokes one trigger variable
at a time (`$C6`, `$7D`, `$7F`, `$7E`, `$7C`, `$7A`) and records what the deferred
VBI at `$B2A5` then writes, which isolates each sound from the others.
`tools/sound_trace_show.py` / `tools/sound_events_show.py` render the dumps.

**What the original actually plays** (AUDCTL is never written, so all four
channels run from the 64 kHz clock, `f = 63920/(2*(AUDF+1))`; AUDC = volume in
the low nibble, noise select in bits 7-5, so `$80`-class is 17-bit poly noise and
`$A0/$C0/$E0` are pure tones):

| event | Atari | frames |
|---|---|---|
| shot (`$7D = $0F`) | ch3 pure tone vol 8, `AUDF3 = 6*(27-$7D)` -> **405 -> 196 Hz** | 15 |
| engine (ch1) | **17-bit poly noise**, `AUDC1 = $B4AC[$19&3]` = vol 6/4/3, `AUDF1 = $1F-(jet Y>>4)` (~1.9 kHz) | continuous |
| explosion (`$7C=$17/$37`) | ch2 poly noise, vol `((t>>1)+4)&$0F`, `AUDF2 = RANDOM|$18` | 23 / 55 |
| fuel tank (`$A40A` -> ch1) | pure tone, vol 8 -> 1, re-armed while over the tank; 499 Hz, or 999 Hz when the gauge is full | 8, repeated |
| extra life (`$7E=$40`) | ch3 arpeggio 329/394/492/652 Hz, vol ramp 15..0 per step | 64 |
| start blip (`$C6=2`) | ch1 492 Hz pure tone vol 15 | 2 |

Firing a shot live sets only `$7D`; `$50` (the `$B4C0` pitch table on ch4) stays
`$FF` because that path needs `$41` bit 7, so the ch4 chirp is **not** part of a
normal shot.

**Ported** (`src/sound.c`): pulse 1 = shot (50% duty, volume 8, period
255 -> 549 = 439 -> 204 Hz, 15 one-frame steps - the old build did 2728 -> 740
Hz, the reported fault); noise = engine (vol 6/4/3 and index 11/12/13 for
fast/cruise/slow, replacing the 73-125 Hz triangle hum), explosion (23), crash
(55) and the tank note; pulse 2 = tank note, start blip and extra-life arpeggio;
**triangle is no longer used**.  The 2A03 has one noise channel where POKEY has
three, so the explosion takes it over and the engine resumes afterwards.

**Bug found while measuring:** the noise channel's length counter was never
loaded, so with the halt flag set it stayed zero and the channel emitted nothing
until the first explosion wrote `$400F`.  `sound_init()` now loads it once.

**Frame loop:** `sound_update()` moved from the 30 Hz player group into the
every-frame path (the Atari's sound VBI is 60 Hz and the shot is a per-frame
sweep).  `perf_frame_trace.mjs fast`: 4.5% late, 26.5k cycles/frame; no
regression.

**Extra life rule:** `entities_add_score()` now grants the life and plays the
arpeggio when the ten-thousands BCD digit changes, as `$A574-$A587` does.

**Test:** `tests/nes_sound.mjs` rewritten (21 checks).  An end-frame Lua callback
copies the live 2A03 state (`apu.square1.realPeriod`, `apu.noise.envelope.volume`,
the length counters) into CHR-RAM `$1F80`, so the test asserts the pitch and
volume the console emits and that each channel is audible - the previous version
only counted register writes and could not see a wrong pitch.  Verified: shot
255 -> 549 in 15 steps, engine noise volume 3/4/6 at periods 1015/761/507,
triangle unused, explosion loud then engine restored, tank note repeats.

Smoke, scroll/HUD, river, gameplay (incl. the 30-point kill), entity-hits and
sprite-lock all still pass.  RAM 145 bytes free, ROM 65,552 bytes.

(An earlier attempt at this entry mapped the tank sound to `$7F = 9` on channel 2;
 that was a misreading - see the next entry.)

### 2026-10-30 - Fuel tank sound: two wrong readings corrected

The user's second listen was right again: the tank note is not a warble and not
noise, it is **one short note repeated for as long as the jet is on the tank**.

Two misreadings in the first sound pass:

1. `$7F = 9` (`$A5E0`) is **not** the refuelling sound.  That path (`$A5B7`) tests
   `$0500,X` for types `$0A`/`$0B`, which are helicopters - it is the *shot*
   passing a target.  The port had it wired to `ENT_HIT_FUEL`.
2. The tank sound is the **channel 1** path.  `$A40A` (reached for object type
   `$0F`, the fuel depot) increments `$5B`, which is the fuel level - the DLI
   proves it, `$B527` positions the gauge missile from `($5B >> 3) + $5C` - and
   sets `$7A = 4, $7B = 8`.  The VBI then runs (`$B2D9`):

   ```
   ldy #$1F / cpx #$02 / bcs $B2E6 / beq $B2EA
   ldy #$3F          ; AUDF1 = $3F -> 499 Hz, constant
   adc #$A0          ; AUDC1 = $7B + $A0 = $A8..$A1: pure tone, volume 8 -> 1
   dec $7B           ; 8 frames, then $7A = 0
   ```

   Because the VBI clears `$7A` when the 8-frame sweep ends, the next frame over
   the tank re-arms it, so the note repeats back to back for the whole overlap -
   exactly "repeated single note for the duration the plane is over the fuel
   entity".  It is a 50% square (POKEY "no poly"), 499 Hz, volume 8 down to 1.

Implemented in `src/sound.c` as `FUEL_PERIOD`/`FUEL_FRAMES`: pulse 2, 50% duty,
period 223 (499 Hz), volume 8,7,...,1 and re-armed while
`sound_play_pickup()` keeps being called.  `player.c` calls it **every frame of
the overlap** (the previous edge-trigger was based on the wrong `$7F` reading and
is gone); the fuel itself still refills per pass.

Verified in Mesen: `the tank note is a constant 499 Hz`, `the note volume walks 8
down to 1`, `the note repeats while the jet is over the tank` (volumes
`7,6,5,4,3,2,1,8,7,6,5,4,3,2,1,8,7,6`).  `tests/nes_sound.mjs` is 19 checks;
gameplay (incl. the refuel rate) still passes.

**Also now known (not implemented):** the low-fuel warning is a real Atari sound
and the first port had guessed it.  While `$5B < $40` and no tank note is
running, channel 1 plays a pure tone at volume 15 with `AUDF1 = 2 * $7B`, where
`$7B` is a free-running 0..$3F counter, gated to sound only for `$7B >= $1C`:
a rising 251 -> 561 Hz beep for 36 frames, then a 28-frame gap where the engine
returns, repeating every 64 frames.  The port currently has no warning.

**Pitch depends on the gauge (user report, verified): a full tank is one octave
up.**  `$A40A` increments the fuel and picks `$7A = 4` - except when the
increment carries, which only happens while `$5B` is already `$FF` (the code
saturates `$5B` at `$FF` instead of wrapping) - and then `$7A = 3`.  In the VBI
those two values take different paths: `$7A = 4` falls through to `ldy #$3F`
(AUDF1 = 63 -> **499 Hz**), while `$7A = 3` hits `beq $B2EA` and keeps
`ldy #$1F` (AUDF1 = 31 -> **999 Hz**).  So refuelling with room in the tank
plays 499 Hz and refuelling a full gauge plays the same note an octave higher.
Ported as `FUEL_PERIOD_FULL` 111, selected when `hud_fuel == 255`.

`tests/nes_sound.mjs` is now 21 checks: 8-frame notes repeating at a constant
period, volume 8 -> 1, 499 Hz with a partly full tank and 999 Hz with a full one,
and the noise channel left at the engine level throughout.

### 2026-10-30 - Low-fuel warning, enemy shell, engine glide (user reports)

Three more listening reports, all verified in the source first:

**Low-fuel warning was missing.**  `$B2FD`: below `$40` of fuel (`$5B`), channel 1
drops the engine and plays `AUDC1 = $AF` (pure tone, **volume 15**) with
`AUDF1 = 2 * $7B`, where `$7B` counts `$3E` down to `$00` and the tone only
sounds while `$7B >= $1C`.  That is 35 frames rising 251 -> 561 Hz, then 28
frames where the engine returns, repeating every 63 frames.  Ported on pulse 1
as `snd_warn` (period `7*$7B + 3`), with the engine silenced while it sounds -
POKEY plays it on the engine's own channel.  The user then asked for it to have
**priority over every other sound**, so `sound_play_shot()` no longer writes the
channel while the tone is up (`snd_warn_on`): a shot fired during a tone window
is silent, which is the trade-off for an uninterruptible warning.

**Enemy shell sound.**  The ch4 `$50` path is not an unused enemy-shot branch: it
is the bridge tank's shell (`$A609-$A674`).  `AUDF4 = $B4C0[$50]`, `AUDC4 = $AC`
(pure tone, volume 12) for `$50 = 1..$10`, then `RANDOM|$20` with `$8C` (17-bit
poly, volume 12) while the fireball swells.  Ported as `sound_play_shell()`,
called from `ent_shell_init()` so it fires and refires with the tank: the chirp
runs on pulse 2 (periods 52,62,76,90,108,129,153,185,220,262,314,377,450,542,
650,780 = 2131 -> 143 Hz), the tail on the noise channel.  A temporary
`TANK_FIRE_ALWAYS` hook let every bridge tank shoot for listening and has been
removed again (the Atari's `hud_bridge >= 5` gate is back).

**Engine pitch glide.**  The Atari's engine pitch is `AUDF1 = $1F - (jet Y >> 4)`,
so it slides as the jet climbs; the port snapped between three noise indices.
`sound_update()` now walks the index one step per `ENGINE_STEP_FRAMES` (8) frames,
so slow <-> fast takes ~16 frames instead of one.  The volume still changes
immediately (`AUDC1 = $B4AC[$19 & 3]`), which is what the original does.

**Bug fixed: the tank note used to hang at volume 1.**  Its 8-frame volume walk
ends on volume 1, but the "still over the tank" hold flag expired on a *later*
frame, so the silence write never ran and the pulse was left at volume 1 forever.
Pulse 2 now keeps a `snd_p2_on` shadow ("this channel still holds a tone") and
silences it whenever no job owns it.  Regression check: the note must fall silent
within one note (8 frames) of the jet leaving the tank.

`tests/nes_sound.mjs` is 34 checks; the audio-bearing states are: warning runs
35 tone / 27 gap frames at volume 15 rising 437 -> 199, a shot cannot dim it, the
tank note repeats at 223 (499 Hz) and 111 when full and stops after leaving, the
shell chirp covers the whole `$B4C0` table, the fireball adds noise at volume 12,
and the engine walks 13 -> 12 -> 11 while gliding.  Smoke, gameplay, entity-hits
pass.  RAM 140 bytes free, ROM 65,552 bytes.

### 2026-10-30 - Phase 6: attract / SELECT / START read off the Atari (no code yet)

Disassembly (`$A15D-$A1F5`, `$A675-$A6DC`) plus a live Altirra probe (clean boot, no
input, then SELECT x3, SELECT held, START):

- **There is no title screen and no demo autopilot.**  The joystick `$19` is always read
  from PORTA (`$A31A`).  Boot goes straight to attract: `$29` bit 7 = 1, `$76 = $FF`
  (throttle stopped), the river scrolls with objects, **no player jet**, lives `$6A = 4`
  shown as planes, HUD `P1 0 / HI 0 / BRIDGE 1 / GAME 1`.  The low bits of `$29` count up
  slowly (`$81 -> $86` in ~1200 frames, `$B3DF`) and `$78` blanks the screen after a
  long idle (`$B3F6`, GRACTL/DMACTL off: the screen-saver).
- **SELECT** (`CONSOL` bit 1 low): `$25` is a repeat delay.  Press -> `$09 = ($09+1)&7`,
  `$102B` shows digit `$D1+$09`, **score/lives/bridge cleared** (`$6A = 0` shows no
  planes), `$29 = 0`, `$C6 = 2` blip; the first repeat is `$1E` frames, then `$25` reloads.
  Probe: `$09` 0 -> 1 -> 2 -> 3, held for 100 frames reached 7 and wrapped.
- **START** (bit 0 low, `$A675`): `$C6 = 2` blip, `$29 = 0`, `$2A = 0`, `$77 = $09 & 1`
  (odd game numbers = the second controller/P2 variant: HUD shows `P2`, `PORTA >> 4`),
  then restarts via `$A15D` (lives `$6A = 4` reloaded, so 3 spare, score cleared, throttle
  ramps `$76 = $5B..`).  Game `$09 >> 1` picks the level-descriptor pair (spec line 67).
- Screenshots of the two states are in the session scratchpad only; `GAME 8` showed `P2`,
  bridge 50 (`$09 = 7` starts at a later bridge/skill).

Plan: add `GAME_ATTRACT` state to `player.c` (jet hidden, `river_freeze = 0`, scroll at
cruise, entities live), a `game_select` byte (0..7) with a 30-frame repeat, HUD game digit
from it, START -> `player_respawn(1)`; boot straight to attract.  Still to read off the
Atari: what `$09 >> 1` changes in the river/object tables, and what the `P2` game does.

### 2026-10-30 - Phase 6: game variants and two-player rules (decoded, user decision recorded)

Correction to the entry above: displayed GAME n = `$09 + 1`.  **Even `$09` (GAME 1/3/5/7) is
one player; odd `$09` (GAME 2/4/6/8) is two players alternating** (`$77 = $09 & 1`).

- `$09 >> 1` picks a 6-byte row of `atari_level_descriptors` (`$B494..$B4A8`): LFSR seed
  `$66-$68`, `rg_left` seed `$69` (`$20`/`$21`), start level `$60` = **1 / 5 / 20 / 50**
  (binary) and the BCD bridge number `$62`.  `river_start(0)` currently always uses row 0
  and `river_defaults[5]` for `rg_left` - it must index the row and take byte 3 from it.
- Hi-scores are per variant pair (`$80 + 4*($09>>1)`, compared at `$B261`).
- 2P: `$B247` swaps the 11 bytes `$60-$6A` (level, bridge, score, seeds, lives) with
  `$6B-$75` and toggles `$77` (selects stick/trigger 2).  **Probe (GAME 2):** P1 flies
  first with lives 3 / other 4; on each death the blocks swap and the other player
  continues with their own lives/river; the HUD label at `$100B` changes (`$4F` shows the
  second player), and `$59` is `$80` in attract for variants < 4 and `$FE` otherwise.
- **User decision:** *starting level change is mapped to D-pad Up/Down in attract and
  pre-game mode* (cycles the four start levels 1/5/20/50).  SELECT toggles 1P/2P.
  START begins.
- Risks noted: games 3-8 start at level 5/20/50, so the CHR-cache overflow (Known issue
  1d) is reachable from the first screen - re-run `nes_ring_integrity` and `host_river`
  from those seeds.  Booting into attract changes every test's setup (they must press
  START first); keep variant 0's river identical to today's boot so `nes_river` holds.

### 2026-10-30 - Phase 6: attract mode, start-level select, two players (implemented)

- **Boot goes to attract** (`PLAYER_STATE_ATTRACT`): river scrolls, entities live, no jet, no
  lives, HUD `GAME 1`.  **D-pad Up/Down = start level** (user request): wraps through
  bridge 1 / 5 / 20 / 50 (`river_variant` 0..3, a row of `atari_level_descriptors`: LFSR
  seed, `rg_left`, level), re-initialising the river each press.  **SELECT = 1P/2P**
  (`GAME n = 1 + 2*variant + two`).  **START** begins with 3 lives and `hud_bridge` = the start
  bridge.  `river_init(variant)` now returns the start bridge and takes `rg_left` from the row
  (the old code used `river_defaults[5]` = `$20`, wrong for variants 1/3 = `$21`).
- **Game over returns to attract** (was: wait for START); START there starts a new game.
- **Hi-score per start level** (`pl_hi[4][3]`, saved/loaded on every change of variant).
- **Two players alternate** like `$B247`: `pl_other[10]` holds the idle player's score/lives/
  bridge plus `river_swap_checkpoint()`'s level and 4 seeds; every death swaps, a player with
  no lives is skipped, the game ends when both are out.  **Player 2 flies on controller 2**
  (`joypad_ex`), as the Atari's `$77` selects stick/trigger 2.
- RAM 100 bytes free (was 140: `pl_other` 10, `pl_hi` 12, `pl_pads` 5, scratch), ROM 65,552,
  `_XINIT` ends `$FB03`.
- **Tests:** `tests/nes_menu.mjs` (new, 15 checks: boot state, Up/Down/SELECT, START, 2P swap,
  port-1 input).  Every other suite now calls `startGame()` (`mcp_client.mjs`: START on port 0)
  after pausing, because the cart no longer boots into play; `nes_smoke` starts a game before
  the screenshot (lives icons).  **Launch Mesen with `--nes.port2.type=NesController`** or port
  1 input is ignored (2P test).  All of `nes_smoke`, `nes_scroll_hud`, `nes_gameplay`,
  `nes_entity_hits` (20/20), `nes_sound`, `nes_respawn`, `nes_river` (383/383),
  `nes_river_cache`, `nes_sprite_lock`, `nes_menu` pass; `host_river` (19,999 rows) and
  `host_restart` (16/16) pass.
- **Not done / open:** the HUD still says `P1` while player 2 flies (the Atari shows the
  second player's label via `$4F`); the idle player's score is not shown in the HI field in 2P;
  the attract screen-saver blank (`$78`) and the `$59` attract speed are not ported; levels 5/20/50
  starts were not checked against the model or `nes_ring_integrity` (CHR-cache overflow, Known
  issue 1d, is reachable from the first screen there).

### 2026-10-30 - Entities moved while the river scrolled in (user report)

After a game start the river scrolls in (WAIT) and the jet then waits (READY) - but only READY set
`river_freeze`, so during WAIT patrols, tanks, shells and `ent_wake()` ran.  New `ent_hold`
(`entities.c`): set by `player_respawn()`, cleared when the player first acts (READY -> PLAY); it
gates the wake-ups, tank/shell ticks and `ent_move()` next to `river_freeze`.  Attract is not held
(the Atari's attract world moves).  Note level 1 is static in the original (`ent_move` returns for
`rg_level < 2`), so movement only shows from bridge 2 / start levels 5+.  `tests/nes_hold.mjs`
(5 checks, with a negative control: hold forced off -> the ship moves 120 -> 160).  RAM 99 free.

### 2026-10-30 - No enemies near the jet at game start (user report, level 50)

**Disassembly answer:** the Atari's spawn block has one start-related rule, `$AC3F`: the slot is
a blank when `rg_countdown + section_timer >= $12` (game-start defaults sum to exactly `$12`);
already ported in `sp_choose()`.  The Atari does not need more: its start scroll-in is only
~80 rows (`$76` counts `$7F -> $2F`), so the first objects stop 45-53 px above the jet
(probed on all four start levels).  This port scrolls the whole taller window in (**219 rows**,
measured), which carried the first objects down onto the jet (level 50: a helicopter 20 px away).

Fix: `entities_row()` predicts where an object spawned during the scroll-in would end up
(`ent_scrolled` counts rows while `ent_hold`; `ENT_WAIT_ROWS` = 222) and does not create it if its
bottom would be within `ENT_START_CLEARANCE` (48 px) of the jet (`PLAYER_START_Y` 218), so nothing
is ever seen sliding there and vanishing.  `entities_clear_near()` (called when the jet appears)
stays as a safety net.  Bridges, tanks and shells are exempt.  `tests/nes_start_clear.mjs` watches
every other frame of the scroll-in on all four start levels.  RAM 96 bytes free.
Re-measure `ENT_WAIT_ROWS` (read `ent_scrolled` at READY) if the playfield height or start scroll changes.


### 2026-10-30 - Phase 6: Atari-style game select, P2 scoreboard, debug start, pause menu

- **Game select = the Atari's `$09`** (`$A69D`): Up/Down step the game number 1..8 and wrap
  (bit 0 of `game-1` = two players, bits 1-2 = start level 1/5/20/50), held Up/Down repeats every 30
  frames (`$A6DA`).  Only a change of start level rebuilds the river; 1P <-> 2P just redraws the
  scoreboard.  `nes_hold.mjs` now presses Up twice to reach GAME 3 (level 5); one press is GAME 2
  (level 1, two players, where ships do not patrol).
- **Scoreboard (`$AF80`, `$AF4B`)**: the left field is always P1 (`$5A`) + P1's score.  The right
  field is `HI` (`$5B`) + the level's hi-score for one player, and **`P2` (`$4F`, BG tiles 58/59,
  yellow like P1) + P2's score** for two players - the hi-score is not shown then.  `hud_two`,
  `hud_cur` and `hud_other_bcd` (the idle player's saved score) feed `hud_write_score/hi`.
- **SELECT in attract = debug game**: same as START but `pl_nocol` (static in `player.c`) stops
  terrain/enemy crashes.  Fuel still drains (refuel works), `player_invuln` is the separate test
  hook.  The flag is cleared by `player_attract()`.
- **START in play = pause**: `PLAYER_STATE_PAUSE`, `river_freeze` + `ent_paused` (new, makes
  `entities_update()` return early) freeze the world.  A sprite menu "RESUME / EXIT" (plane icon =
  cursor) is drawn in OAM slots 56..63 and 2..4 (tank slots, idle while paused) using charset glyphs
  uploaded to sprite tiles 240..247 (R E S U M X I T; **X is charset code 120, code 24 is a comma**).
  Other sprites in the menu's scanline band (104..143) are hidden so a scanline never exceeds
  the 8-sprite limit; the entity/jet passes redraw them after RESUME.  Up/Down/SELECT move the
  cursor, START or A confirms; EXIT -> `player_attract()`.
- Not done: the Atari's 2P game-over line in row 2 (`$A399-$A3AE`, P2's bridge counter), and
  checking the hi-score against both players at game over (`$B261`).
- Build: ROM 65552, `_XINIT` ends `$FB03`, **70 bytes** internal RAM free.  `nes_menu.mjs` covers
  game-number stepping, the HI/P2 label tiles, the debug start, pause/resume/exit.

### 2026-10-30 - Late frames: cheaper rows and jobs, paced generator (branch `experiment/perf-tuning`)

**Symptom.** Occasional stutter, choppy fast flight.  A late pass repeats the whole
previous frame, then jumps (`0 3`); it also sustains itself, as a late pass owes more
rows and makes the next one late (`03 03 03`).

**Measured with `tests/perf_pass_trace.mjs`** (per pass, true cycle counter; a pass has
~23.7k cycles).  The old per-row figures were far too low: one row cost 7-8.6k (render
3-4.5k, convert 2.4k, state 1.7k), a tile-row job with 5-7 uploads 9-13k, 11 entities
~13.5k.  Only ~5 of 24 columns of a tile row ever need pattern data.

**Changes.**
- `river_convert_row`: a column that stays solid costs ~34 cycles and writes nothing;
  `river_col_kind` is 3 = nothing yet, 1 = land, 2 = water, 0 = mixed, and earlier lines
  are filled in when a column stops being solid.  `river_copy_line` copies mixed columns.
- Tile job: hand-written `river_put`, unrolled pattern copies (9 cycles a byte),
  `river_find_dup` walks only mixed candidates, `river_job_solids` scans solid columns.
  One job now takes one pass unless the transfer buffer fills.
- Render: no land fill (the row clear wrote it and fills only move forward),
  `river_bank_black` / `river_black_lut` instead of per-byte calls, no multiply in
  `river_update()`.
- Pacing: `river_gen_budget` = 3 rows next to the player group, 1 next to the entity
  group, +1 and the tile job allowed when `river_owed >= 3`.  Pacing alone did nothing
  before the cost cuts; without the owed rule it starved the generator and the scroll
  clamped (`1 2 0 2`, 1.37 px/frame).

**Result (late frames, 400-frame runs, invulnerable jet, shooting).**

| Scenario | before | after |
|---|---|---|
| level 1 cruise | 0.0% | 0.0% |
| level 50 cruise | 8.8% | 2.0% |
| level 1 fast | 17.3% | 5.3% |
| level 5 fast | 15.8% | 3.0% |
| level 20 fast | 22.8% | 5.8% |
| level 50 fast | 24.6% | 5.8% |

**Fast at 2.0 px/frame** gives even steps and was late 9.5-13.5%, but the user
play-tested it and it felt worse than 1.5.  Do not raise it again without new evidence.

**Findings kept for later.**
- Dedupe removes only 4% of uploads but keeps the peak cache window ~25 slots lower, so
  it protects Known issue 1d.  Keep it.
- `rg_hit_flash` is only ever assigned 0: its branches in the renderer are dead code.
- Unused spare time: the NMI spins through scanlines 0-29 (~3.2k cycles).
  `.tim_emulation` is a hook inside the NMI after the vblank work, so that wait could
  do bounded work when the main loop is idle in `vsync()` and end on a sprite-0 hit.
  Not built; needs a hardware test.

**Tooling.** `./build.ps1 -Profile` exports the statics of `river.c` and `entities.c`
(`#define static` was committed unguarded before).  `perf_frame_trace`,
`perf_scroll_cadence` and `perf_cadence_levels` read `river_job`, and `perf_pass_trace`
and `perf_prof_level` need the function symbols, so all need that build.
`tests/perf_exp.ps1 <name>` builds it and measures the standard scenarios.
`compare_tile_rows.mjs` takes a ROM, `.noi` and start level.

**Verified.** `compare_tile_rows` against the `main` ROM over 2,400 frames from start
levels 1, 20 and 50: 0 of 846 rows differ (control: levels differ on 282/282 rows).  All
Mesen suites and `host_river.ps1` pass.  `nes_sound` now tolerates a repeated sweep
value, which is what one late frame produces.  Not yet play-tested on hardware.

### 2026-10-30 - Row render and pacing, part 2 (branch `perf/row-render`)

- `river_render_plain()`: hand-written path for rows with no island and no feature
  (34% of all rows; a third of rows are skipped as repeats).  Unrolled row clear (4
  cycles a byte), asm LFSR.  Removed `rg_hit_flash` (only ever assigned 0) and
  `rg_row_b4` (always equal to `rg_row_pos`).  `RIVER_FLAG_FLASH` is now never set.
- Pacing: after those cuts every late pass was a player pass generating 3-4 rows, while
  entity passes peaked at 18.9k.  So the player pass is capped at 3 rows, and catch-up
  goes to the entity pass (`river_owed >= 2` raises its budget from 1 to 2 and allows
  the tile job).

| Late frames | before all this | part 1 | part 2 |
|---|---|---|---|
| level 1 fast | 17.3% | 5.8% | 1.8% |
| level 5 fast | 15.8% | 3.0% | 1.0% |
| level 20 fast | 22.8% | 6.5% | 1.5% |
| level 50 cruise | 8.8% | 2.0% | 1.3% |
| level 50 fast | 24.6% | 5.8% | 3.3% |

Speed stays 1.50 px/frame with no scroll clamping.  `compare_tile_rows` against the ROM
from before part 1: 0 of 846 rows differ.  All Mesen suites and `host_river.ps1` pass.
`host_river` only runs the C paths, so the asm is covered by `nes_river`,
`nes_river_cache` and the diff.  Not yet play-tested on hardware.  The hand-written
entity loop and the split-wait work are not needed for now; RAM 72 bytes free.

### 2026-10-31 - HUD: start blink and white P2 field (Atari ground truth)

- **Atari logic (read from the disassembly, confirmed live in Altirra):**
  - `$AF80`/`$AF4B` build the scoreboard in screen RAM at `$1000`.  Mode-6 char bits 7-6 pick the
    colour: P1 label `$1A` + P1 digits `$10+d` = COLPF0 (`$1E` yellow); P2 label `$4F` + digits `$50+d` =
    COLPF1 (`$8E`, white on screen); HI `$DB` / `$D0+d` = COLPF3.  The left field is always P1 and the
    right always P2 (2P) or HI (1P), whoever is flying.
  - Blink = DLI at `$B684-$B6AA`: if `$76 != 0` and `($76 | $29)` has bit 7 clear and `($18 & $30) == 0`,
    `COLPF0+Y` is set to `$06` (the band's gray), `Y = $77` (current player).  So the active player's
    label **and** score vanish together for 16 of every 64 frames.
  - `$76` is `$80` after every (re)start (`$B0F1` copies table `$BAE1` over `$4D..$76`), counts down
    to `$2F` while the jet scrolls in, holds at `$2F` until the first stick/fire input, then 0.
    `$FF`/`$FE`/attract (`$29` bit 7) never blink.  Hence it also blinks before every respawn.
  - Also in that DLI (`$B68E`): while P2 is flying COLPF2 becomes `$8E`, so BRIDGE, the bridge number
    and the lives go white.  **Not ported** (needs 8 more BG tiles + a white life sprite); open item.
- **NES:** `hud_blink` (set in `player_update()` for WAIT/READY) + `hud_update()` blank the active
  field (`hid_field`) with `TILE_BLANK` when `(sys_time & $30) == 0`.  The check runs every
  `hud_update()` pass, not through the 6-phase round robin, so the off window stays 16 frames.
  P1 and HI/P2 labels are rewritten too (`shown_p1`, `shown_label`).
- **White P2:** no free palette (1 and 3 are playfield palettes) and no 10 free tile IDs, so the
  right-field digits are tiles 1..10 re-uploaded at runtime (`set_sprite_data`, which addresses
  `$1000` directly) as black for HI / white for P2, one tile per `hud_update()` pass.  The P2 label
  (tiles 58/59) is built white (plane 0 solid, plane 1 = glyph).  The GAME digit moved to its own
  tile `TILE_GAME_DIGIT` (47), uploaded when it changes.
- **Bank:** this grew `_CODE_1` by 558 bytes past the 16 KiB bank, so `hud` now builds into the unused
  bank 2 (`$bank2Sources` in `build.ps1`); bank 1 has ~3.3 KB free again.  Build: ROM 65552, `_XINIT`
  ends `$FC0E`, 46 bytes RAM free.
- Tests: `node tests/nes_hud_blink.mjs` (new, 22 checks: 16-frame runs, phase vs `sys_time`, idle field
  never blinks, white tiles in 2P, black again in 1P, P2 blink after the turn swap); `nes_smoke` now
  expects the GAME digit on tile 47.  smoke, scroll_hud, menu, hold, gameplay, sound, start_clear pass.

### 2026-10-31 - HUD: first line raised 2 px; attract game select freezes the world (Atari timer)

- **First text line (P1 / score / HI / hi-score) is 2 px higher.**  An early scroll split (scanline 10,
  repeating the blank row y=15) was tried and rejected: the NMI's OAM DMA + VRAM-buffer flush before
  it vary, so the entry sometimes landed a line late and scanline 10 flickered (the main split at 30
  has enough margin).  Final version is pure tile art: the labels (two blank top glyph rows) sit 2 rows
  higher in their tiles; the digits (rows 1..6) are shifted 2 up and their first pixel row lives in
  "top" tiles on the blank nametable row 0 (`TILE_TOP_YELLOW` 248..251 left, `TILE_TOP_RIGHT` 252..255
  right; the digits have 4 distinct top rows 3C/18/7E/0C, `top_rep`/`digit_top` in `hud.c`).  IDs 248..255
  are free in the shared pattern table (entities 88..228, debris 229..238, pause 240..247; compile-time
  guard in `player.c`).  The bridge number keeps unraised glyphs in its own runtime tiles 60/61
  (`TILE_BRIDGE_DIGIT`), the GAME digit tile 47.  The blink blanks both rows.
- **Right-field recolouring is a burst, not a trickle:** a 1P/2P change re-uploads tiles 1..10 + 252..255
  (black/white), 7 per `hud_update()` pass, and refreshes GAME/BRIDGE/score/HI in the same passes
  (the first version returned early per tile and left the HUD stale for ~28 frames).
- **Attract game select now works like the Atari** (checked live: `$76=$FF`, `$29=0` after SELECT, `$2A`
  reloaded to `$25` by every press, counted down every 8 frames at `$A2F5`, restart at `$A688`):
  Up/Down only edit the scoreboard (game number, bridge, per-level hi-score), freeze the river and its
  enemies (`river_freeze`, `ent_hold`) and (re)arm a 296-frame timer; the picked level's river is built
  from black only when it runs out (`player_attract(pl_sel)`), or at START.  `pl_sel` is the picked level,
  `river_variant` the one on screen.  Before this, every level change rebuilt the world at once (display
  off + black ring), which looked like a scroll pause.  Not ported: the Atari's idle attract screen saver.
- Build: ROM 65552, `_XINIT` ends `$FC0E`, **13 bytes internal RAM free** (was 70): `digit_buf` 16 B,
  the blink/upload state, `pl_sel`/`pl_frozen`/`pl_wait_end`.  Reclaim before adding state (`digit_buf` can
  alias a shared scratch if needed).
- Tests: `nes_menu` checks freeze, timer reload and delayed rebuild; `nes_smoke` expects the raised digit
  art, the row-0 top tiles and the bridge/game digit tiles; `nes_hud_blink` also covers the top row.
  All Mesen suites pass (smoke, scroll_hud, hud_blink, menu, hold, start_clear, gameplay, sound,
  entity_hits, sprite_lock, jet_hitbox).

### 2026-10-31 - Land colour after respawn; shooting enemies researched (branch `main`)

- **Fixed (play-tested by the user):** after dying at an even level the grass came back dark. `river_start()`
  always set `river_land_alt = 0`. First attempt keyed it to `rg_level & 1`, which failed: the generator
  advances `rg_level` only after the bridge road, but the colour flips at the road, so `rg_level` can still be
  the old level at restart. Now `river_land_alt = (bridges & 1) ^ 1` (bridge number == level in
  `atari_level_descriptors`; odd = dark `$D2`) and `river_job_pal` follows it. Commit `3510a5b`.
- **Research for "white striped helicopters do not shoot" / "no side tanks" (nothing implemented):**
  the Atari shell for helicopters/ships is the spawner block `$ADA6-$AE92` in `docs/atari_disasm_a000_b2a4.txt`.
  Conditions seen at `$ADCD-$AE22`: slot-`$4E` type >= 8, level (`$60`) >= 7, `$5D` in 5..13, `$AC & $C0 == 0`,
  `$5F`/`$2C` in 2..13; it fills `$38/$39/$3A` (shell kind/position/state) and sets `$50 = $FF`. The port only
  has the bridge tank's use of this shell (`ENT_AUX_SHELL`, OAM 4-5, `ent_shell_tick`). The shell kind on the
  Atari is shared, so one enemy shell exists at a time. Side-of-river tanks are probably the `$ADA6` branch
  (`$23` bit 7 clear path, `$38 = 5/6`) but that is unconfirmed.
- Not built or run this session (docs and memory only besides the colour fix).

### 2026-10-31 - Shooting enemies traced on the live Atari (branch `main`)

- `tools/atari_shoot_trace.py <level> <frames>` boots the Atari, stays in attract, pokes `$60` and logs the
  frame on which `$38` goes 0 -> non-zero, with `$23 $2D $36 $4E $5D $5F $2C $AC $3A` and screenshots
  (start and +25 frames). Tip from the user: level 50 in attract gives shooters within seconds (level 9 gave one in 15k frames).
- Findings (level 50, 3000 frames, 7 starts): `$38` = 5/6 is always a tank. `$3A = $0B` + `$23` bit 7 set = the bridge tank
  (already ported); `$3A = $0C` + `$23` bit 7 clear = a **side tank** from the `$ADCD-$AE86` branch (frames 176, 912, 1296, 2192).
  Conditions in the disasm: helicopter in slot `$4E`, level >= 7, `$5D` 5..13, `$AC & $C0 == 0`, `$5F/$2C` 2..13.
  So "no side tanks" is this branch; whether "striped helicopters don't shoot" is a separate mechanism is still unconfirmed.
- Not yet verified: where the side tank sits (X from `$39`, Y from `$3E/$3F`, `$B7F4` table), whether it uses the
  bridge-tank shell rules, and what the light-blue horizontal sprite near the banks in the late screenshots is. No port code changed.

### 2026-10-31 - Side tank ported (branch `main`)

- Sampled the live Atari (level 50, attract): the side tank (`$38`=5/6, `$3A` Y starting `$0C`, `$41`=`$FF`) walks 1 px
  per 4 frames only once `$3A >= $1A`, and fires the shell as soon as `$20` (P1/playfield collision) is set,
  then stands still; the shell cycles 57 frames like the bridge tank's. Shell speed is 8.8 fixed point from
  `$B7F4` (0.5/1/1.5/2/4 px per frame), the bridge tank's is 2. Spawn rules `$ADCD-$AE86` are in `ent_side_tank_spawn`
  (needs rowcount 0, level >= 7, countdown 5..13, `$AC & $C0 == 0`, narrower width 2..13, no tank alive, and a ship/heli/fuel
  just placed). `$36` is `rg_feature` (now exported). Position tables `$BFE4` and the sign rules copied from `$AE3E-$AE86`.
- Port: `ENT_F_SIDE` tanks use `ent_tank_edge()` (overlap with bank/island outline columns, 2 px slack) in place of `$20`;
  `shell_vx_hi/lo/frac` globals make the shell speed variable. The tank starts at its companion's Y.
- Build: ROM 65552, RAM 10 bytes free, `_XINIT` ends `$FC1C`, `_CODE_1` 14303 bytes. Not run in Mesen; user playtests.

### 2026-10-31 - Fast helicopter missile and its sound (branch `main`)

- User description: white striped helicopters shoot a 1 px horizontal short line. It is the Atari's **M0 missile**
  (`$A5B9-$A607`, shooter = the type `$0A/$0B` helicopter the spawner makes at level 13+, slot `$51`), not a shell:
  `$C1` = X (0 = none), `$C0` = dx (-2 left / +2 right, from status bit 3), `$C2` = scanline index. Traced on the live Atari
  (`pm.py`/`heli.py` style: attract at level 50, dump `$0B00-$0FFF`): the missile is **1 scanline** at PM row = helicopter
  top + 4, width 4 px (SIZEM = 1, two bits doubled), X = heli X - 3 (left) or + 7 (right); fires when heli Y17 - `$3C` >= `$1C`
  (top >= 11 rows below the window top), refires the frame after it dies, dies on playfield collision (`M0PF & 3`) or when no
  `$0A/$0B` helicopter exists; the kernel's M0PL & 4 hit on the jet kills it (`$21`). It scrolls with the river.
- Port: `ent_missile_tick`, `msl_x/y/dx` (3 B RAM), sprite tile 239 (`MISSILE_TILE`), OAM slot 6 (`ENT_OAM_MISSILE`; first
  dynamic entity slot is now 7), jet hit in `entities_check_player`; `ent_outline_overlap()` is shared with the side tank.
- Sound: `$7F` is the ch2 missile counter (the earlier note "shot passing a helicopter" was wrong): for n = 9..1,
  AUDC2 = `$46`+n, AUDF2 = 20,16,28,24,36,32,44,40,52; port plays it as a 9-frame pulse-2 job `P2_MISSILE`
  (periods 72,58,100,86,128,114,156,142,184 by n, quarter duty, volume n+6). The Atari is silent for it in attract.
  Pitch is a formula-based approximation (the poly distortion `$40` has no 2A03 equivalent): judge by ear.
- Build: ROM 65552, RAM 7 bytes free, `_XINIT` ends `$FC71`. No tests run (user playtests; OAM slot shift is
  covered by `nes_entity_hits`/`nes_sprite_lock`, not yet run).

### 2026-10-31 - Helicopter missile sound corrected; enemy shots silent in attract (branch `main`)

- The first version played the missile on pulse 2 and sounded like the tank-shell chirp. Captured the **real game** (not
  attract; `$29` = 0): ch2 plays AUDC2/AUDF2 = 4F/14, 4E/10, 4D/1C, 4C/18, 4B/24, 4A/20, 49/2C, 48/28, 47/34 for `$7F` = 9..1
  (registers match the code). Altirra decodes it as distortion 2 (`$40`) with divider period_cycles 588, 476, 812, 700, 1036,
  924, 1260, 1148 (1484 for AUDF 52). A pure-Python DFT of the 465-tick poly5/poly4 pattern shows a rough noise-like buzz
  (strongest line ~ tick/4.9), not a tone, so the port now plays it on the **noise channel, short mode**, indices
  14,13,13,13,13,12,12,11,11 (by n-1), volume n+6, overriding the explosion/fireball/engine for 9 frames (`snd_msl`).
- Enemy shots (tank shell and helicopter missile) no longer make sound in attract (`player_state != PLAYER_STATE_ATTRACT`;
  `player_state` is now exported from `player.c`, the define moved to `player.h`). The Atari gates `$7F` by `$29` bit 7.
- Build: ROM 65552, RAM 5 bytes free, `_XINIT` ends `$FC6C`. Not run in Mesen. Pitch/timbre is a model: judge by ear;
  tweak `missile_noise[]` / `NOISE_SHORT` in `src/sound.c` (try long mode by dropping `NOISE_SHORT` if it is too tonal).

### 2026-10-31 - Helicopter missile sound: long-mode noise (branch `main`)

- Playtest: the short-mode (93-step) noise version still sounded like a high pulse; the original is noise. Dropped `NOISE_SHORT`:
  the missile zap is now normal-mode noise at the same indices (14,13,13,13,13,12,12,11,11 by n-1), volume n+6. Same family
  as the engine hiss (idx 11..13). Build: ROM 65552, RAM 5 free, `_XINIT` ends `$FC68`. Not run in Mesen.

### 2026-10-31 - Shooters confirmed; regression run (branch `main`)

- User play-tested the side tank, the helicopter missile and its sound: all good. Bugs closed in `BUGS&FEATS.md`.
- Regression: nes_smoke, nes_entity_hits (20/20), nes_gameplay, nes_sound, nes_menu, nes_hold, nes_scroll_hud, nes_river_cache,
  nes_hud_blink, nes_start_clear all pass; `tests/host_river.ps1` matches 19999 rows. `nes_sprite_lock` printed a 1 px error
  against the pre-session baseline (0 px): it read OAM slot 6, which is now the missile slot; the test reads slot 7 now
  (locked on 80/80 frames again).
- Build: ROM 65552, RAM 5 bytes free, `_XINIT` ends `$FC68`.

### 2026-10-31 - Plane replaced by random enemies: investigated, not reproducible (branch `main`)

- Bug (BUGS&FEATS.md, now marked `[?]` stale): at high levels white planes (type 7) were reported as replaced by other enemies. User could not reproduce it later; no source changes made.
- Checked in our code: `ent_type` is only written by the spawner, the explosion code (`ent_type` = 2..6, class in the flags high nibble) and the tank/shell code; the only type rewrite on live objects is the helicopter rotor swap (8<->9, 10<->11, `entities.c` ~1625), which excludes 7. Sprite tile ranges do not overlap: HUD/BG 1..64, entities `ENT_TILE_BASE` 88..228, pause glyphs 240..247, HUD top tiles 248..255.
- Mesen probe at level 50 (poked `rg_level`=50 after `startGame()`, held Up, 1500 frames, `ent_type[11]` sampled every frame): only 8<->9 and 10<->11 transitions (~410 and ~108 each); one plane appeared (slot 1, frame 88) and stayed type 7. So no corruption after spawn.
- Not done: Atari comparison. If it comes back, the open question is whether `sp_choose` (`entities.c` ~260, Atari `$AC00-$AC8A`) picks the same types at high levels: `tools/atari_object_trace.py --level 50 --rows 1500 --out build/object_trace50.jsonl`, then `tools/object_validate.py` on it (needs AltirraBridgeServer, see `info/ALTIRRA_BRIDGE_NOTES.md`). Planes come only from `atari_spawn_types[rg_lfsr_b & 7]` (1 in 8 draws), demoted to a ship below level 5. Also capture a screenshot of the wrong sprite and the level/bridge number.

### 2026-10-31 - Extra life every 1000 points (branch `main`)

- `entities_add_score` compared `hud_score_bcd[1] & 0xF0` (thousands digit) instead of the ten-thousands digit (`hud_score_bcd[0] & 0x0F`; score is 6-digit big-endian BCD), so a life and the bonus arpeggio fired every 1000 pts. Now every 10,000 as on the Atari. Not play-tested (marked `[~]`).
- Build: ROM 65552, RAM 5 bytes free.

- Follow-up: the life was granted but the HUD capped lives icons at 3 (`HUD_LIVES_ICON_MAX`), so a 4th was invisible. Cap is now 4; the 4th icon uses OAM slot 56 (`HUD_LIFE_SLOT4`; 47 is the player shot, 48..55 the gauge). The Atari draws up to 8 icons (`$AF5B`); more here would exceed 8 sprites/scanline with the gauge. Build: ROM 65552, RAM 5 free. Untested.

- Lives icon cap raised from 4 to 8 (the Atari's 9-cell field at `$AF5B`): icons 4..8 use OAM slots 56..60. The gauge sprites are all at x<150 and the icons start at x=160, so they never share a scanline with more than the 8 icons. Untested.

- Lives icons moved from sprites to BG tiles (`TILE_LIFE` 62, nametable row 3, cols 20..27). Sprite slots 56..60 were wrong twice over: the gauge already uses 8 sprites on those scanlines (hardware drops sprites past 8/scanline, wherever they sit in x; the MiSTer only looked fine with its sprite limit turned off), and `pause_slot` uses OAM 56..63. Sprite tile 15 stays for the pause cursor. The row is written via `digit_buf` as scratch (RAM was 4 bytes over with a dedicated array). Build: ROM 65552, RAM 5 free. `nes_scroll_hud.mjs` still checks OAM slots 44..46 for the icons and needs updating. Test ROM: `roms/river_raid_9lives.nes` (start lives 9; patch both `hud_lives` and `pl_other[3]` in `player_start_game`).

- Closed: plane tile now uses the gray HUD band (INK_PLANE0/1 like the yellow digits); play-tested by the user with a 9-lives ROM (removed, source back to 3 lives). `tests/nes_scroll_hud.mjs` lives check now reads nametable row 3 cols 20..27 (tile 62) instead of OAM slots 44..46; not re-run (user asked to skip Mesen tests).

### 2026-10-31 - Pause from the READY state

- START was only handled in `PLAYER_STATE_PLAY`, so the menu was unreachable while the jet waited for first input. `player_update` now enters the pause menu from READY too; `pause_from` remembers the state and RESUME restores it (`river_freeze` stays 1 for READY). Build ROM 65552, 4 bytes RAM free. Not Mesen-tested; awaiting user playtest.

### 2026-10-31 - Pause from READY play-tested

- User confirmed the READY-state pause fix (RESUME returns to READY, EXIT goes to attract). Marked `[x]` in `BUGS&FEATS.md`; HANDOFF RAM figure corrected to 4 bytes free.

### 2026-10-31 - Extra-life fix play-tested

- User confirmed the extra life every 10,000 pts fix (ten-thousands nibble) and the BG-tile lives icons. Marked play-tested in `BUGS&FEATS.md`.

### 2026-10-31 - Lives spent when the jet appears

- Atari spends a life when the jet appears (`$6A` 4 -> 3 at WAIT->READY), not at the crash. `player_start_game` now sets `hud_lives` and `pl_other[3]` to 4; `hud_lives--` moved from `player_crash` to the WAIT->READY transition in `player_update`. Game over = crash with 0 in reserve (same test as before). Net effect: 4 planes per game (1 flying + 3 spare), was 3.
- Build: ROM 65552, RAM 4 free. Play-tested by the user (1P and respawn flow); no Mesen run.

### 2026-10-31 - Balloon basket is brown

- Balloon = aux 12 (20 rows; bottom 8 Atari rows `$22` are the basket). A sprite takes one palette per tile and the 8/8/4 tiling split the basket across two tile rows, so `tools/gen_entity_gfx.py` pads the balloon 4 rows at the top (basket = last tile row, colour 2) and `ent_draw_reg` draws type 12 from `ent_y - 4`, switching the last tile row to sprite palette 1 (brown `$08`, flip bit kept). Collision rows stay 20. Debris frames still use palette 0 (not changed).
- Build: ROM 65552, RAM 4 free. Play-tested by the user; no Mesen run.
