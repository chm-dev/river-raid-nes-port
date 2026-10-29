# Project Progress & Session Handoff

**Last updated:** 2026-10-28
**Current phase:** Phase 3 — Top HUD, Scanline Split, & CHR Graphics
**Phase 1:** COMPLETE (reverse engineering + asset extraction)
**Phase 2:** COMPLETE (scaffolding, build pipeline, boot ROM, Mesen smoke test)
**Phase 3:** COMPLETE (HUD, deferred LCD split, playfield ring, 1-px scroll test)

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
  - `river_update()` advances the scroll one pixel per frame and streams one
    32-tile row every 8 pixels into the hidden ring slot.
  - The Phase 3 row source is the captured Atari river, converted to 19
    tile rows / 82 distinct 8x8 tiles by `tools/gen_river_demo.py`. It loops
    to fill the 30-row ring; Phase 4 replaces only the row source.
- Build: `roms/river_raid.nes` is 32,784 bytes, mapper 30, 2 x 16 KiB PRG,
  CHR-RAM, vertical mirroring. RAM: `DATA $0300-$030F`,
  `BSS $0310-$0528`, heap at `$0529`, 727 bytes free.
- Tests: `tests/nes_smoke.mjs` passes 50 checks; `tests/nes_scroll_hud.mjs`
  passes 14 checks (120-frame scroll, stable split, exact 1-px shift, live
  HUD fields, fuel marker and lives sprites).
- No known blocker. Phase 4 (procedural river generator) is next.

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

- [x] BG tile allocation: 0..127 charset, 128..209 river demo tiles,
      210..225 gauge frame, 226..227 P1, 228..229 HI, 230..237 BRIDGE,
      238..245 GAME, 246..255 yellow digits.
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
- [x] `river_update()`: 1-pixel/frame scroll, 30-row ring, stream one row
      every 8 pixels, `river_freeze` test hook.
- [x] `tools/gen_river_demo.py` packs the captured 157-row river into
      19 tile rows / 82 distinct tiles; `src/river_demo_data.{h,c}` are
      generated.
- [x] `tools/patch_nes_header.py` fixes GBDK's iNES byte 6 (vertical
      mirroring, not four-screen); wired into `build.ps1` and the Makefile.
- [x] `tests/nes_smoke.mjs` updated to 50 Phase 3 checks.
- [x] `tests/nes_scroll_hud.mjs` updated to 14 checks (split 36/37, sprite
      fuel marker and lives).

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
| `src/river_demo_data.h` / `.c` | Generated Phase 3 river tile set + 19 tile rows |
| `src/main.c` | Boot sequence, VBL/LCD split at LYC=35 (visible 36/37), frame loop |
| `src/hud.c` | Source HUD: charset-glyph labels, 5-cell gauge, sprite fuel bar, lives planes |
| `src/river.c` | 30-row NT1 ring, 1-px scroll, row streamer |
| `src/player.c` | Sprite CHR for the three flight shapes |
| `src/config.h` | Geometry, tile IDs, palettes, nametable bases, HUD layout |
| `build.ps1` / `Makefile` | Reproducible GBDK-NES build + header patch |
| `tests/mcp_client.mjs` | MesenCE-MCP client and memory/`.noi` helpers |
| `tests/png.mjs` | Minimal PNG reader for screenshot assertions |
| `tests/nes_smoke.mjs` | Phase 3 boot/HUD/playfield regression (50 checks) |
| `tests/nes_scroll_hud.mjs` | Scroll/split/HUD regression (14 checks) |
| `tools/gen_river_demo.py` | Regenerates `src/river_demo_data.*` |
| `tools/patch_nes_header.py` | Patches the iNES mirroring byte |
| `roms/river_raid.nes` / `.noi` / `.map` | ROM + symbols + linker map |
| `build/phase3_smoke.png` | Latest HUD/playfield screenshot |
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

1. **River fork/island parity (Phase 4):** exact branch conditions in
   `$AA7A-$AB7F` still need row-by-row comparison against
   `assets/atari/extracted/river_rows_157.bin`.
2. **Fuel/bridge scores (Phase 4):** confirm the 80/500 candidates and how
   `$BB0B` is indexed for those objects.
3. **Demo river loop (Phase 4):** the captured fixture has only 19 full
   tile rows, so the 30-row ring repeats it; this is a demo artifact. Phase 4
   generates continuous rows.
4. **Phase 4 CHR strategy:** the demo uses 82 BG tiles for 19 rows. The
   procedural generator will need either a runtime 8x8 tile cache, more
   precomputed bank/transition tiles, or a different row-to-tile mapping.
   All BG tiles 210..255 are now allocated to the HUD, so Phase 4 must
   either reuse river-demo slots or add a CHR-RAM cache.
5. **Fuel rate / 16-bit gauge (Phase 4).**
6. **PAL gauge offset (`$BF`) (Phase 4).**
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

## Next step — Phase 4

Follow `PORTING_PLAN.md` section 4 (Phase 4). Concrete order:

1. Port the Atari LFSR/table generator from `$B183` and the template stream
   (`$BBF6/$BC00`) into `river_generate_row()`; replace the demo source in
   `river_stream_next()`.
2. Decide the NES tile mapping for procedural rows (tile cache vs. extra
   precomputed patterns) within the remaining BG CHR budget.
3. Build the in-RAM collision ring (`left/right/island` bounds + row flags)
   alongside the streamed tiles.
4. Extend `tests/nes_scroll_hud.mjs` or add `tests/nes_river.mjs` to compare
   generated rows against `river_rows_157.bin` and AltirraBridge RAM dumps.

## Handy commands

```powershell
./build.ps1                             # compile + link + header patch + RAM asserts
node tests/nes_smoke.mjs                # Phase 3 boot/HUD regression (50 checks)
node tests/nes_scroll_hud.mjs           # scroll/split/HUD regression (14 checks)

# MesenCE-MCP (leave running while testing)
./MesenCE-MCP/MesenCE-MCP-Windows.exe --mcp --mcp-port=9100 --enableStdout `
  --doNotSaveSettings --preferences.pauseWhenInBackground=false `
  --preferences.pauseWhenInMenusAndConfig=false --preferences.disableOsd=true `
  --video.videoFilter=None --nes.region=Ntsc

python tools/gen_river_demo.py          # regenerate src/river_demo_data.*
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
