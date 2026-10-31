# AGENTS.md — Workspace Guidelines & Reference

## Project Status
- **Read `HANDOFF.md` first.** It is short and current: phase status, next steps, open bugs, budgets, decisions not to reopen, and a "where to look" table.
- **Do not read `PROGRESS.md` whole** (2,000+ lines, ~35k tokens). It is the append-only history and rationale log. Grep it for a keyword and read only the matching `### date - title` entry.
- Phases 1-5 are complete; Phase 6 (sound, attract/menus, 2P, HUD polish) is in progress. The cart boots into attract, so tests call `startGame()` from `tests/mcp_client.mjs`.
- `PORTING_PLAN.md` is the roadmap. This file holds stable rules only: update `HANDOFF.md` (rewrite) and `PROGRESS.md` (append, without reading it) as work progresses; the end-of-session steps are at the bottom of `HANDOFF.md`.

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

### 1. REA / Ghidra (Static Analysis)
- Load the `$A000–$BFFF` 8 KiB cartridge slice at base address `$A000` (6502 processor).
- Atari hardware register ranges: `GTIA $D000–$D01F`, `POKEY $D200–$D20F`, `PIA $D300–$D303`, `ANTIC $D400–$D40F`.

### 2. AltirraBridge (Dynamic Atari 8-Bit Analysis)
Full reference: `info/ALTIRRA_BRIDGE_NOTES.md`.
- **Server:** `F:\Emu\Atari800\Altirra\AlitrraBridge\AltirraBridgeServer.exe` (note spelling: `AlitrraBridge`).
- **Python SDK:** `F:\Emu\Atari800\Altirra\AlitrraBridge\sdk\python` (`from altirra_bridge import AltirraBridge`).
- **Critical Rules:**
  - Always redirect `stdout` and `stderr` to files when starting `AltirraBridgeServer.exe` (never use an un-drained `stderr=PIPE`, or the server will hang).
  - Connect via `AltirraBridge.from_token_file(token_path)` from `%TEMP%\altirra-bridge-<pid>.token`.
  - `a.boot(xex)` only arms the loader, and `a.frame(n)` is a **gate** that blocks on the *next* command. Always step `a.frame(240)`–`a.frame(300)` after `boot()` before reading state.
  - Default video mode is `pal`; switch with `a.config("video", "ntsc")` when matching 60 Hz NES frame timing.
  - Use `a.peek()` / `a.memdump()` for side-effect-free reads, and `with a.checkpoint():` for non-destructive RAM probing.
  - Clean up server processes (`a.quit()` or `Stop-Process`) and temporary files when done. `a.close()` leaves the server alive; a prior probe that called `quit()` leaves stale tokens that fail with `WinError 10061`.
- **Breakpoints (learned 2026-10-29 while tracing the river generator):**
  - Altirra expression syntax differs from C: equality is `=`, bitwise/boolean AND is `&` (not `==`/`&&`), and memory deref is `db($addr)` with a `$` prefix. `db(0x...)`, `==`, and `&&` are rejected.
  - A conditional breakpoint is the only reliable way to stop on a rare row; keep the condition simple, e.g. `(db($5d)=3)&(db($60)=2)`.
  - **Frame gates coalesce breakpoint hits.** If a frame generates two rows, `a.frame(1)` may stop only once and silently skip the other row. For per-row capture use `a.cycles(2000)` + `a.clock()` and check `regs()["PC"]` instead of frame gating.
  - After a breakpoint hit, **clear the breakpoint before stepping off the same PC**, otherwise the step retriggers at the same address and the same row is captured twice. Re-set the breakpoint after the step.
  - If using `frame()`, arm the gate **before** `resume()`; resuming first lets the free-running emulator execute the next hit before the gate is active.
  - `regs()` returns uppercase keys (`PC`, `A`, `X`, `Y`) with `$`-prefixed hex strings.
- **River-row capture recipe:** break at `$AB80` (first instruction after the row renderer). Dump zero page `$00-$FF`, object arrays `$0500-$053F`, and the row at the LMS operand `$3F04/$3F05` (the generator writes the new row there; do **not** read zero-page `$04/$05`). `$AB80` also guarantees the row was fully built before the object/spawn code runs. Disable object slots (`$4D/$4E`, `$0500-$053F`, Y/status arrays) and center the jet (`$57`) after each sample to prevent checkpoint resets mid-trace.
- **Reference implementation:** `tools/river_model.py` is a validated Python translation of the generator; `tools/atari_river_trace.py` captures the per-row trace; `tools/river_validate.py` compares model state and rows against the trace. A 2,600-row trace across levels 1–3 matches every generation field and row (only unused `$B1/$B7` temporaries differ).

---

## NES Verification & Testing (`./MesenCE-MCP`)
Full reference: `info/MESEN_MCP_NOTES.md`.
- **Executable:** `./MesenCE-MCP/MesenCE-MCP-Windows.exe`.
- **Standard Launch Flags:**
  `--mcp --mcp-port=9100 --enableStdout --doNotSaveSettings --preferences.pauseWhenInBackground=false --preferences.pauseWhenInMenusAndConfig=false --preferences.disableOsd=true --video.videoFilter=None --nes.region=Ntsc --nes.port2.type=NesController` (the last flag only matters for the two-player test, `tests/nes_menu.mjs`)
- **Critical Rules:**
  - **Handshake:** Send `initialize` (`protocolVersion: "2024-11-05"`) *without* `Mcp-Session-Id`, capture the `Mcp-Session-Id` response header for all subsequent requests, send `notifications/initialized`, and always include `Accept: application/json, text/event-stream`.
  - **Reset Window:** Wait `900–1200 ms` (until `frame=` advances in `mesen_get_status`) after `mesen_load_rom` or `mesen_reset` before calling breakpoints, save-states, or debugger tools.
  - **Deterministic Stepping:** Pause playback and use `mesen_step {cpuType: "Nes", count: 1, stepType: "PpuFrame"}` for frame-exact assertions.
  - **Input Latching:** `mesen_input_override` latches buttons; always clear with `buttons: ""` in a `finally` block.
  - **Lua Probes:** Always use `emu.log(...)` (never `print(...)`) in `mesen_run_lua_script`.
  - **Lua limits:** `io`/`os` are disabled and the log only returns at load time. Use `persistent:true` (a non-persistent script is removed after `waitMs`) and write results to free NES RAM with `emu.write`, then read them with `mesen_read_memory`. Cycle counting: `emu.getState()["cpu.cycleCount"]` inside `emu.addMemoryCallback(..., emu.callbackType.exec, addr)`. Keep the mailbox inside the free RAM reported by `build.ps1`.
  - **Cleanup:** Remove breakpoints/Lua scripts and stop `MesenCE-MCP-Windows` when finished if you started it.

### Performance lessons (2026-10-29)
8. **Inline `__asm` must live in its own function.** SDCC keeps values in registers across an inline asm block; a `river_fill` asm in the middle of C corrupted island bounds (raw-row tests still passed). Verify with real tile pixels - raw-row/state tests only see the last row of each block: diff two ROMs with `tests/compare_tile_rows.mjs` and run `tests/host_river.ps1` (`tests/nes_river_cache.mjs` does the same against the model but is stale on the performance branch, see PROGRESS.md).
9. **Frame timing:** the NMI spends ~5.4-6.4k cycles in the faked LCD wait (the later weighted sampler says ~5.4k), so a main-loop pass has ~23-24k cycles. `vsync()` also runs the deferred VBL/LCD handlers and arms OAM DMA - never replace it with a busy-wait. Use `sys_time` (`game_ticks`) for elapsed frames; `vbl_events` only counts vsync calls.
10. **Transfer buffer:** write entries directly with `river_put()` (format documented in `river.c`); `set_bkg_data`/`set_vram_byte` cost 250 cycles/byte. `num_cycles_x8` starts at 171 (unsigned, bit 7 is just a value). Pattern data goes in planar order.
11. **Profiling recipe:** Lua exec callbacks on function entries, accumulate `emu.getState()["cpu.cycleCount"]` deltas, write to CHR-RAM `$1E00+` (`emu.memType.nesChrRam`), read with `mesen_read_memory memoryType:'NesChrRam'`. Remove old persistent scripts (`mesen_remove_lua_script`) between runs and call `mesen_resume_execution` after load/reset. Static functions have no `.noi` symbol; temporarily drop `static` to profile them. Hold Up/Down via `mesen_input_override buttons:'Up'|'Down'`.

### Performance lessons (2026-10-30)
12. **Frame loop (`main.c`):** `river_update()` and `sound_update()` run every frame; everything else runs at 30 Hz in two groups on alternate frames (player/collision + `entities_follow()`, and HUD + `entities_update()`), so their costs never land on the same frame. `game_ticks` is the elapsed time of the group being run (`entities_update()` measures its own from `sys_time`). River code must be time-based on `game_ticks`; `river_scrolled` accumulates until `entities_update()` consumes it, `river_rows_last` is one frame's share. Never call `BANKED` functions from an ISR.
13. **Fixed-bank / RAM budget.** The linker does **not** fail when the fixed bank runs past `$FFFA`: the ROM silently becomes 128 KiB and the game misbehaves. After every build check that `build.ps1` prints `ROM  65552 bytes` and that `_XINIT` in `roms/river_raid.map` ends below `$FFFA`. Modules built into bank 1 are listed in `$bankedSources` in `build.ps1` (`entities`, `entity_gfx`, `player`; ~3 KB free, check `_CODE_1` in the map); `hud` builds into bank 2 (`$bank2Sources`, check `_CODE_2`); bank 0 is unused; the fixed bank has ~1 KB free; `build.ps1` prints the free internal RAM.
14. **Profile peaks, not averages.** A frame budget is 29,780 cycles (NMI split spin ~5.4k); cruise needs ~12k, fast ~16k on average, but fast has frames of 20-24k. A pass that misses the frame costs a duplicate frame: the NMI replays the scroll table and skips OAM DMA until `vsync()` has run. Build with `./build.ps1 -Profile` for the perf scripts. Use `tests/perf_pass_trace.mjs` first (per pass with the true cycle counter: what the late passes contain), `tests/perf_frame_trace.mjs` (per-frame), `perf_function_cycles.mjs` (per function) and `perf_scroll_cadence.mjs` (what the player sees). The sampler weights instructions by base cycles (~10-15% low). A bucket of banked code spans every static up to the next *public* function, so a global's cost can really be its statics' (temporarily drop `static`, profile, `git checkout`). **Restart MesenCE-MCP between Lua runs** (persistent scripts survive `mesen_load_rom`) and **read addresses from the `.noi`** - they move whenever the RAM layout changes.
15. **SDCC code size/speed:** array reads can compile to a ~30-cycle `setupDPTR` sequence (as in the old hit test), but not always (`ent_draw` has one); count `setupDPTR` and `jsr __mul` per function in `lcc -mmos6502:nes -S` output before blaming the compiler. Hand-written 6502 in its own function is the fix for hot loops (`ent_scan_player`, `ent_draw_reg`, `river_find_dup`, `river_copy_line`, `entities_follow`); hard-coded constants get a compile-time `typedef char x[(A == 88u) ? 1 : -1];` assert, and a C fallback under `#ifndef __SDCC` keeps the host harnesses (`tests/host_river.ps1`, `tools/object_host_check.py`) building.
16. **Repeat-row skip invariants (`river_step_state`, `river_generate_raw_row`).** A plain row is a pure function of `rg_left`, `rg_phase`, `rg_width` and the island flag; the signature `rs_*` (`rs_isl == 0xFF` = none) is invalidated by `river_init`, flash/feature rows and `river_road_paint`. Anything that writes `river_raw_row` outside the renderer must also invalidate it. `river_copy_line` is only valid for `river_block_index != 0` because `river_emit_extra` stages bytes in `river_tile_pat[0..7]` between tile rows. Re-check with `python tools/river_repeat_rows.py` (0 false positives) and `tests/host_river.ps1`.
17. **Geometry changes (HUD height).** Everything keys off `HUD_SPLIT_SCANLINE`/`HUD_SPLIT_LYC`/`HUD_SCROLL_Y` in `config.h`, except the literal `cmp #25` in `ent_draw_reg()` (guarded by a compile-time assert) and the geometry constants in `nes_smoke`/`nes_scroll_hud`. A bigger window costs CHR-cache slots (see PROGRESS.md Known issues 1d) and shrinks the headroom above the screen, where objects are spawned (`entities_row()` clamps the spawn Y at 0). `tests/nes_ring_integrity.mjs <rom> <noi> <split> <Up|none> <frames>` shows whether any visible ring row is rewritten; run it with a deliberately too-large window as a negative control.
18. **Prove an optimisation by diffing output**, not only with a test: `tests/compare_oam.mjs` and `tests/compare_tile_rows.mjs` dump what a ROM produces so the previous ROM's output can be compared byte for byte (rotor tiles may differ by one animation frame between ROMs of different speed). Boundaries are guarded by `tests/nes_entity_hits.mjs`, sprite/scroll lock by `tests/nes_sprite_lock.mjs`.


19. **Sound verification (2026-10-30):** `emu.getState()` exposes the live APU (`apu.square1.realPeriod`, `apu.square2.*`, `apu.noise.envelope.volume`, `apu.noise.timer.period`, each channel's `envelope.lengthCounter.counter`), which is the only way to test *what is audible*: counting register writes cannot detect a wrong pitch. A **zero length counter silences a channel even with the halt flag set**, so every channel needs `$4003/$4007/$400F = LEN_LOAD` at least once (`sound_init()` does it for the noise channel). Keep a per-channel "still holds a tone" shadow and silence from it: a sound whose countdown ends on a different frame from the flag that requested it otherwise leaves the channel stuck at its last volume (the tank note hung at volume 1 this way). POKEY conversion used by the port: `f = 63920/(2*(AUDF+1))`, 2A03 pulse `p = 111861/f - 1`, `AUDC` = volume in the low nibble / noise select in bits 7-5.
