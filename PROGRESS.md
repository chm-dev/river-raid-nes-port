# Project Progress & Session Handoff

**Last updated:** 2026-10-28
**Current phase:** Phase 2 — Project Scaffolding & Build Pipeline
**Phase 1:** COMPLETE (reverse engineering + asset extraction)

> **New session:** read this file first, then `PORTING_PLAN.md` and
> `docs/ATARI_RE_SPEC.md`. Keep **Current status** and **Next step** current;
> append dated entries under **Session log** instead of rewriting history.

---

## Current status

- Phase 1 is complete. The cart is extracted, fully disassembled, the display
  and PMG model are confirmed, the procedural river data/algorithm is
  documented, and all sprites/tables/charset are extracted into C arrays.
- Phase 2 (project scaffolding, `build.ps1`, Mesen smoke test) has **not**
  started yet.
- The bundled GBDK `lcc` requires `-mmos6502:nes`; `-mnes` is rejected. All
  docs have been updated to the working spelling.
- No known blocker. The only open work is the Phase 4 generator-parity harness
  and a few scoring/HUD details listed below.

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

## Key artifacts

| Path | Contents |
|---|---|
| `docs/ATARI_RE_SPEC.md` | Memory map, display/DLI/PMG model, objects, river generator, HUD, audio, routine catalog, open items |
| `docs/atari_disasm_*.txt` | Complete instruction listings |
| `assets/atari/river_raid_cart_a000_bfff.bin` | Extracted 8 KiB cartridge |
| `assets/atari/palette_altirra_ntsc.bin` | Exact NTSC palette |
| `assets/atari/extracted/` | Charset, HUD, sprites, tables, river fixtures, full-frame reference |
| `assets/atari/extracted/README.md` | Asset file map |
| `assets/atari/runtime/` | Attract, scroll, play-left and play-fire captures |
| `src/atari_data.h` / `.c` | Generated C arrays |
| `tools/README.md` | Tool inventory and run notes |
| `tools/extract_assets.py` | Reproducible extractor (also regenerates sprite PNGs) |
| `tools/render_sprite_sheets.py` | Clean `objects*.png` / `player*.png` from extracted JSON |
| `tools/validate_sprites.py` | PMG-buffer validation |

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
  `(code & 0x7F)*8`; text seed `$BB70`.
- **Score:** 3-byte BCD at `$63-$65`; `$BB0B` values are direct BCD points
  (confirmed +6 and +3 in gameplay).
- **Palette:** `assets/atari/palette_altirra_ntsc.bin`.

## Known issues / deferred items

1. **River fork/island parity (Phase 4):** exact branch conditions in
   `$AA7A-$AB7F` still need row-by-row comparison against
   `assets/atari/extracted/river_rows_157.bin`.
2. **Fuel/bridge scores (Phase 4):** confirm the 80/500 candidates and how
   `$BB0B` is indexed for those objects.
3. **HUD glyph/column layout (Phase 3):** raw buffers/charset are correct,
   but the final NES HUD layout still needs visual polish.
4. **Fuel rate / 16-bit gauge (Phase 4).**
5. **PAL gauge offset (`$BF`) (Phase 4).**
6. **`$24`/`$50` semantics:** `$24` is a 3-frame debounce, `$50` is the shot
   animation counter — not game-state flags.
7. **Stale PMG sample:** the aux 13 ship's PMG bytes do not match its ROM
   shape in the single captured frame; this is DLI multiplex/stale-buffer
   timing, not an asset error (screenshot and full-frame render agree).
8. **Deprecated tools:** `render_mode14.py`, `antic_mode14_probe.py`; use
   `render_mode_e.py` / `pmg_line_probe.py`.

## Next step — Phase 2

Follow `PORTING_PLAN.md` section 3. Concrete order:

1. Create `build.ps1` / `Makefile`, `src/` module stubs and `tests/`.
2. Use `./gbdk/bin/lcc -mmos6502:nes` (not `-mnes`) and `-Wl-j` at link
   time for `.noi` symbols.
3. Build a minimal boot ROM (reset/NMI/IRQ, CHR-RAM load, blank screen).
4. Add `tests/nes_smoke.mjs` and verify boot in `MesenCE-MCP`.
5. Update this file (phase, status, next step) and append a session-log entry.

## Handy commands

```powershell
python tools/extract_assets.py          # regenerate assets + C header + sprite PNGs
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
