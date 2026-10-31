# AGENTS.md — Workspace Guidelines & Reference

## Project Status
- **Read `HANDOFF.md` first.** It is short and current: phase status, next steps, open bugs, budgets, decisions not to reopen, and a "where to look" table.
- **Do not read `PROGRESS.md` whole** (2,000+ lines, ~35k tokens). It is the append-only history and rationale log. Grep it for a keyword and read only the matching `### date - title` entry.
- Phases 1-5 are complete; Phase 6 (sound, attract/menus, 2P, HUD polish) is in progress. The cart boots into attract, so tests call `startGame()` from `tests/mcp_client.mjs`.
- `PORTING_PLAN.md` is the roadmap. This file holds stable rules only: update `HANDOFF.md` (rewrite) and `PROGRESS.md` (append, without reading it) as work progresses; the end-of-session steps are at the bottom of `HANDOFF.md`.
- **Keep `HANDOFF.md` small** (target ~100 lines; may grow to ~150 when the extra text is clearly valuable). On every rewrite remove finished items and anything only historical; detail goes in a `PROGRESS.md` entry or `BUGS&FEATS.md`, with at most a one-line pointer in the handoff. Past ~150 lines, trim before adding.

## Project Overview
Port of Atari 8-bit **River Raid** (`xex/River Raid (1983)(Activision)(US)[a3].xex`) to the **NES** in C using **GBDK-2020 v4.5.0** (`./gbdk`).
- **Source Binary:** 8,220-byte XEX containing a 16-byte loader stub (`$9FF0–$9FFF`, sets `RAMTOP=$60`, jumps to `$A000`) and an unbanked **8 KiB Atari 800 Left Cartridge ROM (`$A000–$BFFF`)**, with `RUNAD=$9FF0`.
- **NES Display Architecture:** Stationary **Top HUD** (3 tile rows = scanlines 3-26 with 3 px padding above and below; nametable row 0 is blank and scrolled up by `HUD_SCROLL_Y` = 5 in the HUD pass) in nametable 0 (`$2000`) + **Vertically Scrolling Playfield** (scanlines 30-239, 210 px) in nametable 1 (`$2400`), split by GBDK's deferred `add_LCD()` handler (`LYC=29`, visible split 30/31) with vertical mirroring in the iNES header. The playfield is a full 30-row ring, so fine scrolling never wraps into the HUD. It scrolls **downward**: `playfield_scroll_y` decrements, new tile rows are written just above the visible window, and each 8x8 tile packs raw row 7 at the top. The river uses an **exact 207-slot CHR cache** (each visible 8x8 block uploaded verbatim); there is no fixed edge/tree atlas. BG tiles: 0 blank, 1..10 shared digits, 11..46 HUD art, 47 land, 48 water, 49..255 cache.
- **HUD assets:** do not redraw the HUD. `assets/atari/extracted/charset.bin` already contains the original art: `$43-$46` = BRIDGE, `$47-$4E` = the 5-cell gauge (`47 48 49 48 4A` / `4B 4C 4D 4C 4E`, E/1-2/F baked in), `$5A` = P1, `$5B` = HI, `$5C` = lives plane. Labels are doubled horizontally like mode-6 text. See `assets/atari/extracted/README.md` → "HUD glyph inventory" for codes, sizes and uses.
- **Full Porting Plan:** See `PORTING_PLAN.md`.

---

## Toolchain & Build (`./gbdk`)
- **Compiler:** `./gbdk/bin/lcc -mmos6502:nes` (SDCC `mos6502` backend). **This GBDK build rejects the shorter `-mnes` spelling** — use the explicit port/platform pair.
- **Symbol Generation:** Always pass `-Wl-j` when linking so `.noi` symbol files (`DEF <symbol> 0x<hex>`) are generated alongside `.nes` ROMs for Mesen MCP tests.
- **Asset Conversion:** `./gbdk/bin/png2asset` and `./gbdk/bin/gbcompress`.

### GBDK-NES / SDCC Coding Rules
1. **Flat Static State:** Use global/static variables and fixed-size arrays instead of deep call stacks or pointers to stack locals to avoid SDCC `mos6502` software-stack overhead.
2. **ISR Overlay Safety:** Mark all `add_VBL()`, `add_LCD()`, and `add_TIM()` handlers (and their callees) with `NO_OVERLAY_LOCALS` or `#pragma nooverlay`.
3. **SDCC Local Aliasing:** do not keep a block-local value live across a call in a complex function. SDCC may allocate it to the same register/overlay slot as the callee's state even with `#pragma nooverlay`; the saved value silently changes. Use a file-scope `static` for values like a saved row offset. This produced a real river-renderer bug (`ysave` in `river_render_row`) that only disappeared when extra debug stores perturbed code generation.
4. **VBlank Budget / transfer buffer:** GBDK's VRAM transfer buffer is 136 bytes and `set_bkg_data`/`set_vram_byte` are CPU-expensive (~1.5k / ~550 cycles per call). `set_bkg_tiles` only reaches `$2000-$23FF`; `set_tiles`/`set_data` are declared but not in the library. Spread uploads over frames (see `river_stream_pending`).
4b. **VBlank Budget:** Never write full screens during gameplay. Stream at most **1 tile row (32 bytes)** + **attribute updates (8 bytes)** and dirty HUD digits per frame. GBDK's VRAM transfer buffer is only `$100-$187` (136 bytes); `set_bkg_data` blocks if it fills, so keep per-row exact-cache uploads small.
5. **Top HUD Sprite Clipping:** Hide or clip any metasprite whose Y coordinate is in the HUD region (`Y < HUD_SPLIT_SCANLINE`, 30).
6. **Split Scroll Compensation:** GBDK's deferred LCD ISR adds the **current scanline** to `bkg_scroll_y` when it writes the entry. The playfield handler must subtract `HUD_SPLIT_SCANLINE` (mod 240) before `move_bkg()`, otherwise the playfield is `HUD_SPLIT_SCANLINE` (30) px ahead of the ring and the newest row appears at the bottom. The handler in `main.c` does this.
7. **No Fixed River Atlas:** the Atari river is a per-scanline bitmap; trees are template fragments 4*$BC px wide and $BE scanlines tall, often starting on odd bytes. The 8x8 block depends on vertical phase. Upload exact blocks; do not reintroduce nearest-tile atlases.

---

### NES pattern tables and sprite tiles
- The playfield owns BG pattern table `$0000` (0 land, 1 water, 2..255 river cache). HUD BG tiles live in `$1000` with all sprites; sprite tile IDs start at `SPRITE_TILE_BASE` (64) so they never collide with HUD IDs. `main.c` flips PPUCTRL bit 4 in the VBL (HUD) and LCD (playfield) handlers; code that calls `set_bkg_data` outside those handlers must set/restore `shadow_PPUCTRL` bit 4 itself (see `hud_init_graphics`).
- The river shows the whole 48-byte Atari row: 24 tile columns at nametable columns 4..27 (`RIVER_TILE_COLS`, `RIVER_LEFT_COL`); screen X = source px + 32.
- `tests/host_river.ps1` checks the C generator against `tools/river_model.py` on the host (needs `zig cc`); run it after touching `river.c` generator code. `tests/nes_river_cache.mjs` checks the on-screen tiles against the model in Mesen.


## Reverse Engineering Tools
Static analysis: load the `$A000-$BFFF` 8 KiB cart slice at `$A000` (6502) in REA/Ghidra. Dynamic analysis uses AltirraBridge
(`F:\Emu\Atari800\Altirra\AlitrraBridge\`, note the spelling). The rules, breakpoint syntax and the river-row capture recipe are in
`info/LESSONS.md` -> "Reverse Engineering Tools"; the API reference is `info/ALTIRRA_BRIDGE_NOTES.md`. Read them before starting a trace.

---

## NES Verification & Testing (`./MesenCE-MCP`)
Full reference: `info/MESEN_MCP_NOTES.md`; scripting rules in `info/LESSONS.md` -> "Mesen testing rules".
- **Executable:** `./MesenCE-MCP/MesenCE-MCP-Windows.exe`.
- **Standard Launch Flags:**
  `--mcp --mcp-port=9100 --enableStdout --doNotSaveSettings --preferences.pauseWhenInBackground=false --preferences.pauseWhenInMenusAndConfig=false --preferences.disableOsd=true --video.videoFilter=None --nes.region=Ntsc --nes.port2.type=NesController` (the last flag only matters for the two-player test, `tests/nes_menu.mjs`)

## Rules that fail silently (full detail and the rest of the lessons: `info/LESSONS.md`)
- **Fixed-bank overflow:** the linker does not fail past `$FFFA`; the ROM silently becomes 128 KiB. After every build check `build.ps1` prints `ROM  65552 bytes` and `_XINIT` in `roms/river_raid.map` ends below `$FFFA`.
- **Inline `__asm` goes in its own function.** SDCC keeps values in registers across an asm block and corrupts the surrounding C; raw-row tests still pass. Verify with real tile pixels (`tests/compare_tile_rows.mjs`, `tests/host_river.ps1`).
- **Never call `BANKED` functions from an ISR.** Never replace `vsync()` with a busy-wait (it runs the deferred VBL/LCD handlers and arms OAM DMA).
- **Anything that writes `river_raw_row` outside the renderer must invalidate the repeat-row signature** (`rs_*`); see lesson 16.
- **Sound tests read the live APU state** (`emu.getState()`), not register writes; a zero length counter silences a channel (lesson 19).
- **Mesen scripts:** clear `mesen_input_override` with `buttons: ""` in a `finally`; use `emu.log`, never `print`; restart MesenCE-MCP between Lua runs (persistent scripts survive `mesen_load_rom`).
- **Profiling and perf work:** read `info/LESSONS.md` items 9-15 and 17-18 first (frame budget, build with `-Profile`, hand-written asm, geometry constants).
