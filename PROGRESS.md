# Project Progress & Session Handoff

**Last updated:** 2026-10-29
**Current phase:** Phase 5 IN PROGRESS — jet, shot, entities, throttle done; bridge, respawn-at-bridge, audio-free polish next
**Phase 1:** COMPLETE (reverse engineering + asset extraction)
**Phase 2:** COMPLETE (scaffolding, build pipeline, boot ROM, Mesen smoke test)
**Phase 3:** COMPLETE (HUD, deferred LCD split, playfield ring, 1-px scroll test)
**Phase 4:** COMPLETE (validated generator, collision ring, exact per-tile cache, Atari orientation; all tests pass)

> **New session:** read this file first, then `PORTING_PLAN.md` and
> `docs/ATARI_RE_SPEC.md`. Keep **Current status** and **Next step** current;
> append dated entries under **Session log** instead of rewriting history.

---

## Current status

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
  - The HUD is a 4-row content band (32 px) plus 4 px bottom padding; the
    playfield split is at scanline 36/37 (`LYC=35`).  Row 0 is blank top
    padding; rows 1-3 carry the text.
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
| `src/main.c` | Boot sequence, VBL/LCD split at LYC=35 (visible 36/37), frame loop |
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
  vertical scroll mid-frame).  The visible split is 36/37 due to the faked
  delay's one-line jitter.
- **NES HUD:** 4 content rows (32 px) + 4 px bottom padding; row 0 is top
  padding, rows 1-3 are content.  Gray band (`$00`), black and yellow text.
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
   Main loop now runs ~0.98 iterations per PPU frame (was 0.45).  Remaining
   cost: a few frames per minute still exceed one frame (rows with >6 new
   patterns fall back to a blocking `set_bkg_data`).
2. **Fuel/bridge scores (Phase 5):** confirm the 80/500 candidates and how
   `$BB0B` is indexed for those objects.
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
    scanline 36 or 37 (a one-line alternation); the top 36 rows stay HUD/gray
    and there are no black playfield frames. A future option is a fixed
    border row at the boundary or a mapper with a scanline IRQ.
11. **HUD digit width:** score, hi-score and bridge digits use the 8-px
    charset digits, while the source displays 16-px mode-6 digits.  Fitting
    six wide digits for both score and hi-score in 256 px would collide with
    the HI label; this is an intentional width compromise.
12. **HUD glyph source:** do not redraw the HUD labels/gauge.  They are in
    `charset.bin` (`$43-$46` BRIDGE, `$47-$4E` gauge, `$5A` P1, `$5B` HI,
    `$5C` lives plane).  The full table is in
    `assets/atari/extracted/README.md`.

## Next step — Phase 5

0. **Bridge and road visuals belong here (decided 2026-10-29, not yet built).**
   The road (grey with a yellow centre line, across the land) at the start of
   the river and at level transitions is not in the NES river palette and is
   probably produced on the Atari by per-scanline colour changes (DLI); this is
   **unverified**, so capture it with AltirraBridge first (DLI list, `$B2A5`
   VBI colour code, bridge rows of `river_rows_157.bin`).  The bridge itself
   is an object (`type $0F`, shared with fuel; `$A3E9` collision/score/
   advance-section code), and level transitions drive the bridge counter,
   `$2E`, and the odd/even land colour (`BG_COLOR_BRIGHT_GREEN`).  The
   generator already flags those rows (`RIVER_FLAG_BRIDGE` in the collision
   ring; rows currently render as plain river).  Candidate NES approach:
   gray/yellow road tiles in the playfield pattern table with the playfield
   attribute table (or the split handler) switching those rows to HUD palette
   0 (`black, gray, yellow, white`); prototype before committing.
1. Start Phase 5 (player jet, entities, collisions).  Feed the entity slots
   into `river_obj_count/river_obj_aux/river_obj_x/river_obj_flags` so the
   bank-avoidance clamp matches the original.
2. Map the jet's screen scanline to the collision ring.  Remember the ring
   advances **backwards** now (`river_slot` decrements); the bounds are
   sampled at raw row 3, which sits 4 scanlines below the top of the tile
   after the vertical flip.
3. `tests/nes_river_cache.mjs` is the long-run cache/streaming check (needs
   `python tools/gen_river_tiles_golden.py 520`); run it after touching
   `river.c`.  It passed 311 tile rows / 72k row checks with zero visible
   glitches.  The collision ring is now filled **one tile row ahead** of
   the display: the jet must index it with that lead in mind.
4. Visual A/B the exact cache against `river_rows_157.bin` and live Altirra
   frames, especially the bridge flash and island forks.
5. RAM is tight: 36 bytes free (queue 64 B, tile buffer 320 B, ring arrays
   150 B, debug hooks 64 B).  Phase 5 will need to reclaim some of it
   (e.g. gate `river_debug_*` behind a macro, shrink `RIVER_Q_MAX`).
6. Remaining perf headroom: `river_convert_row` (~4.3k cycles/frame) and
   `river_render_row` (~3k) dominate; `hud_update` costs ~2k/frame.

## Handy commands

```powershell
./build.ps1                             # compile + link + header patch + RAM asserts
node tests/nes_smoke.mjs                # Phase 3 boot/HUD regression (50 checks)
node tests/nes_scroll_hud.mjs           # scroll/split/HUD regression (14 checks)
node tests/nes_river.mjs                # Phase 4 row/state/collision validation (397/397 sampled)

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