# Project Progress & Session Handoff

**Last updated:** 2026-10-28
**Current phase:** Phase 3 — Top HUD, Scanline Split, & CHR Graphics
**Phase 1:** COMPLETE (reverse engineering + asset extraction)
**Phase 2:** COMPLETE (scaffolding, build pipeline, boot ROM, Mesen smoke test)

> **New session:** read this file first, then `PORTING_PLAN.md` and
> `docs/ATARI_RE_SPEC.md`. Keep **Current status** and **Next step** current;
> append dated entries under **Session log** instead of rewriting history.

---

## Current status

- Phase 1 is complete. The cart is extracted, fully disassembled, the display
  and PMG model are confirmed, the procedural river data/algorithm is
  documented, and all sprites/tables/charset are extracted into C arrays.
- Phase 2 is complete. `build.ps1` and `Makefile` produce a 32 KiB mapper-30
  boot ROM with CHR-RAM, the module stubs are wired into a `vsync()` loop, and
  `tests/nes_smoke.mjs` passes all 46 checks in MesenCE-MCP.
- The boot ROM draws the HUD/playfield split at scanline 24 with two LCD calls
  per frame; the playfield's `playfield_scroll_y` is passed straight to
  `move_bkg()` (subtracting `HUD_SCANLINES` shifts the split one tile).
- The bundled GBDK `lcc` requires `-mmos6502:nes`; `-mnes` is rejected. All
  docs have been updated to the working spelling.
- GBDK 4.5.0's prebuilt `nes.lib` exports `__current_1bpp_colors` but **not**
  `set_1bpp_colors_ex()`. Set `_current_1bpp_colors = 0x0001` (fg=1, bg=0)
  before `set_bkg_1bpp_data()`; the default fg=3 makes plane 1 repeat plane 0.
- No known blocker. Phase 3 (real HUD graphics + scrolling playfield test) is
  next.

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
      VBL/LCD split at scanline 24.
- [x] `tests/mcp_client.mjs` (MCP handshake, memory/`.noi` helpers) and
      `tests/png.mjs` (dependency-free screenshot reader).
- [x] `tests/nes_smoke.mjs`: 46 checks covering header/mapper, palette, CHR,
      nametable, handler cadence, RAM/stack, frame animation, and rendered
      scanline geometry (no top-row tear, blank row 3, status/river/bridge/
      footer positions).
- [x] Boot verified in MesenCE-MCP; screenshot at `build/boot.png`.

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
| `build.ps1` / `Makefile` | Reproducible GBDK-NES build |
| `src/main.c` | Boot sequence, VBL/LCD split, frame loop |
| `src/config.h` | Geometry, tile IDs, palette, boot layout constants |
| `tests/mcp_client.mjs` | MesenCE-MCP client and memory/`.noi` helpers |
| `tests/png.mjs` | Minimal PNG reader for screenshot assertions |
| `tests/nes_smoke.mjs` | Phase 2 boot/split regression suite (46 checks) |
| `roms/river_raid.nes` / `.noi` / `.map` | Boot ROM + symbols + linker map |
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

## Next step — Phase 3

Follow `PORTING_PLAN.md` section 3 (Phase 3). Concrete order:

1. Replace the boot text with the real 3-row HUD: score digits, fuel bar with
   `E ... F` markers, and remaining-lives icon, using the extracted Atari
   templates (`atari_hud_seed`, `atari_score_tens`, HUD glyphs).
2. Load the remaining playfield tiles (water/bank/island/road/bridge) and the
   sprite tiles; keep CHR-RAM usage under the 256-tile budget.
3. Add `tests/nes_scroll_hud.mjs`: drive `playfield_scroll_y` over 120 frames,
   assert the top 24 scanlines stay fixed, the split never shows a seam, and
   the river advances by exactly one pixel per scroll step.
4. Leave the Phase 4 river generator hook in `river_update()`; the boot scene
   and frame counter can then be removed or moved behind a test flag.

## Handy commands

```powershell
./build.ps1                             # compile + link + header/RAM assertions
node tests/nes_smoke.mjs                # Phase 2 Mesen regression (46 checks)

# MesenCE-MCP (leave running while testing)
./MesenCE-MCP/MesenCE-MCP-Windows.exe --mcp --mcp-port=9100 --enableStdout `
  --doNotSaveSettings --preferences.pauseWhenInBackground=false `
  --preferences.pauseWhenInMenusAndConfig=false --preferences.disableOsd=true `
  --video.videoFilter=None --nes.region=Ntsc

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

### 2026-10-28 — Phase 2 complete

- Added `build.ps1` and `Makefile`; both compile every `src/*.c` and link
  `roms/river_raid.nes` (32,784 bytes, mapper 30, 2x16 KiB PRG, CHR-RAM) with
  `.noi`/`.map` output. Current RAM: `DATA $0300-$030F`, `BSS $0310-$0439`,
  heap at `$043A`, 966 bytes free.
- Added the module stubs (`river`, `player`, `entities`, `collision`, `sound`)
  with real init/update signatures and flat static state.
- Built the boot screen: shared palette `0F 30 1A 12`, 128-glyph 1bpp charset,
  five scene tiles, status text, wavy river, bridge, BCD frame digits and a
  footer.
- Fixed 1bpp glyph expansion: GBDK 4.5.0's `nes.lib` does not export
  `set_1bpp_colors_ex()`, so `hud_init_graphics()` writes
  `_current_1bpp_colors = 0x0001` directly. Without it the default fg=3 made
  plane 1 duplicate plane 0 (colour 3 instead of white).
- Fixed the scanline-24 split: the VBL and HUD passes use scroll 0 and the
  playfield passes `playfield_scroll_y` directly. Subtracting
  `HUD_SCANLINES` shifted the playfield one tile low and repeated row 2.
- Added `tests/mcp_client.mjs`, `tests/png.mjs` and `tests/nes_smoke.mjs`.
  The suite now runs 46 checks, including screenshot-level geometry: no
  top-row tear, blank nametable row 3 at scanlines 24-31, status row 4 at
  y=32, river row 8 at y=64, bridge row 16 at y=128-135 and footer row 28 at
  y=224. All pass in MesenCE-MCP; screenshot at `build/boot.png`.
