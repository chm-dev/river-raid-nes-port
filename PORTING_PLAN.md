# Atari 8-Bit *River Raid* to NES - Porting Plan

> **Live status:** see `PROGRESS.md`. This document is the roadmap; `PROGRESS.md`
> is the session handoff (current phase, completed work, blockers, next step).

## 1. Executive Summary & Architectural Decisions

- **Source Target:** `xex/River Raid (1983)(Activision)(US)[a3].xex` (8,220 bytes)
  - Contains a 16-byte XEX loader stub at `$9FF0–$9FFF` (`LDA #$60`, `STA $6A` [RAMTOP], `JSR $F3F6` [OS screen init], `JMP $A000`) and a linear, unbanked **8 KiB Atari 800 Left Cartridge ROM image (`$A000–$BFFF`)**.
- **Target Platform & Toolchain:** **NES** using **GBDK-2020 v4.5.0** (`./gbdk/bin/lcc -mmos6502:nes`, SDCC `mos6502` backend; this build rejects the shorter `-mnes` spelling).
- **Screen Layout & HUD Strategy:** **Top Stationary HUD (Scanlines 0–24 / 3 tile rows, 1 px bottom padding)** + **Vertically Scrolling River Playfield (Scanlines 25–239, 215 px)**.
  - The HUD is drawn in nametable 0 (`$2000`); the 30-row (240-pixel) playfield ring lives in nametable 1 (`$2400`). The iNES header uses vertical mirroring so the two nametables are distinct.
  - GBDK's deferred `add_LCD()` handler (`LYC=24`) switches the playfield scroll/nametable from the NMI path every frame, so river row streaming cannot delay or miss the split. The visible split lands on scanline 25/26; that one-line boundary jitter is inherent to GBDK's faked LCD delay.
  - The playfield scrolls **downward**: `playfield_scroll_y` decrements one pixel per frame, and each new 32-tile row is written just above the visible window. GBDK's split handler adds the current scanline to `bkg_scroll_y`, so the handler subtracts `HUD_SPLIT_SCANLINE` before calling `move_bkg()`.
  - A full 30-row playfield ring means fine scrolling never wraps into the HUD rows; the 26 visible rows need at most 203 exact 8x8 blocks, which fit the 207-slot CHR cache.
- **Reverse Engineering Stack:**
  - **Static Analysis:** **REA / Ghidra** (6502 disassembly & decompilation of the `$A000–$BFFF` cartridge image).
  - **Dynamic Analysis:** **AltirraBridge** (`F:\Emu\Atari800\Altirra\AlitrraBridge\AltirraBridgeServer.exe` + Python SDK) for live memory probing, display list / GTIA / PMG / POKEY inspection, execution profiling, and deterministic behavior verification.
- **NES Verification Stack:**
  - **MesenCE-MCP** (`./MesenCE-MCP/MesenCE-MCP-Windows.exe`) driven by Node.js `.mjs` test scripts parsing GBDK `.noi` symbol tables, PPU state, RAM state, screenshots, and Lua APU probes.

---

## 2. Operational Rules & Lessons Learned (`info/`)

### 2.1 AltirraBridge (`info/ALTIRRA_BRIDGE_NOTES.md`)
1. **Server Binary & SDK Paths:**
   - Server: `F:\Emu\Atari800\Altirra\AlitrraBridge\AltirraBridgeServer.exe` (note folder spelling `AlitrraBridge`).
   - Python SDK: `F:\Emu\Atari800\Altirra\AlitrraBridge\sdk\python`.
2. **Process & Pipe Management:**
   - Always redirect `stdout` and `stderr` to files when launching `AltirraBridgeServer.exe` (never leave an un-drained `stderr=PIPE`, which stalls the server after several commands).
   - Connect via `AltirraBridge.from_token_file(token_path)` using the newest `altirra-bridge-<pid>.token` file in `%TEMP%`.
3. **Frame Gate Semantics:**
   - `a.boot(xex_path)` only arms the loader. `a.frame(n)` is a **gate** that blocks on the *next* command. Always step `a.frame(240)`–`a.frame(300)` after `boot()` before reading game state.
4. **NTSC / PAL Timing & Colors:**
   - Server defaults to `video=pal`. Because *River Raid* (Activision US, 1983) was authored for NTSC 60 Hz (matching NES NTSC 60 Hz frame pacing), test and verify behavior in `a.config("video", "ntsc")` as the timing reference, while inspecting both NTSC and PAL palettes.
5. **Non-Destructive Probing:**
   - Use `a.peek()` / `a.memdump()` (debug-safe, no hardware register read side effects) and `with a.checkpoint():` to run isolated experiments on RAM addresses.

### 2.2 MesenCE-MCP (`info/MESEN_MCP_NOTES.md`)
1. **Server Startup:**
   - Launch `./MesenCE-MCP/MesenCE-MCP-Windows.exe` with:
     `--mcp --mcp-port=9100 --enableStdout --doNotSaveSettings --preferences.pauseWhenInBackground=false --preferences.pauseWhenInMenusAndConfig=false --preferences.disableOsd=true --video.videoFilter=None --nes.region=Ntsc`
2. **Streamable HTTP MCP Handshake:**
   - First request must be `initialize` (`protocolVersion: "2024-11-05"`) **without** `Mcp-Session-Id`.
   - Capture the `Mcp-Session-Id` response header and include it on all subsequent requests alongside `Accept: application/json, text/event-stream`.
   - Follow `initialize` with `notifications/initialized` before calling tools.
3. **Reset & Pacing Windows:**
   - Wait `900–1200 ms` after `mesen_load_rom` or `mesen_reset` (confirming `frame=` advances in `mesen_get_status`) before issuing breakpoint, memory, or save-state calls.
   - Use `mesen_step {cpuType: "Nes", count: 1, stepType: "PpuFrame"}` while paused for deterministic frame-by-frame inspection.
4. **Input & Lua Rules:**
   - `mesen_input_override` latches buttons; always clear with `buttons: ""` in a `finally` block.
   - In `mesen_run_lua_script`, always output via `emu.log(...)` (never `print(...)`).

### 2.3 GBDK-2020 (SDCC `mos6502`) Coding Conventions
1. **Static / Global Allocation:** Prefer flat global/static variables and fixed-size arrays over deep stack-allocated structs or pointers to locals, matching 6502 zero-page/RAM addressing and avoiding SDCC software-stack overhead.
2. **ISR Safety:** Any function registered via `add_VBL()`, `add_LCD()`, or `add_TIM()` (and helpers called by them) must be marked `NO_OVERLAY_LOCALS` / wrapped in `#pragma nooverlay` so SDCC does not overwrite main-loop local overlay variables.
3. **VBlank Transfer Budget:** Never redraw the full screen during active gameplay. Only stream **1 newly exposed tile row (32 bytes)** + **attribute row updates (8 bytes every 16 pixels of scroll)** and dirty HUD digits per frame.

---

## 3. Hardware Mapping: Atari 8-Bit to NES

| Subsystem | Atari 8-Bit (*River Raid*) | NES Port (GBDK-2020) |
| :--- | :--- | :--- |
| **ROM / Mapper** | 8 KiB Cartridge (`$A000–$BFFF`) | Default GBDK-NES ROM configuration (NROM / UNROM with CHR-RAM or CHR-ROM) + `.noi` debug symbols. |
| **Screen Layout** | scrolling playfield on top + ANTIC DLI split for HUD at the bottom. | **Top HUD (3 tile rows, 24 px + 1 px padding)** in NT0 (scrolled up 8 px to drop its blank row 0) + **Scrolling Playfield (215 px)** in NT1, split at scanline 25/26 by GBDK's deferred `add_LCD()` handler. |
| **Vertical Scrolling** | ANTIC `VSCROL` fine scroll + Display List LMS row pointer updates. | Full 30-row (240 px) nametable ring in `$2400`; `playfield_scroll_y` **decrements** one pixel/frame (content moves down) and one 32-tile row is written just above the visible window every 8 pixels. |
| **Terrain & Attributes** | 4-color playfield characters (green banks, blue water, black/gray roads, bridges). | 8×8 background CHR tiles uploaded verbatim from the generated scanlines (207-slot cache), so 1-px bank staircases and tree template fragments are exact. Solid land/water use fixed tiles; the playfield uses one 4-color palette. Road/bridge visuals (grey road, level-transition bridge) are deferred to Phase 5; see PROGRESS.md "Next step". |
| **Sprites (Player, Enemies, Shots)** | 4 Players + 4 Missiles (1bpp full-height strips multiplexed vertically by band). | **NES 2bpp Hardware Metasprites (`move_metasprite`)**: Player jet (16×16), Enemy ship/heli/jet (16×16 or 24×16), Fuel depot (16×24), Player shot (8×8). Since *River Raid* places at most one obstacle per horizontal band, the NES 8-sprites-per-scanline limit is naturally respected. |
| **Collision Detection** | Hardware GTIA registers (`P0PF`, `M0PF`, `P0PL`, `M0PL`). | **Deterministic C Collision:**<br>• **Jet / Shot vs. Terrain:** Fast lookup into a 30-row in-RAM river geometry ring buffer (storing left/right bank & island X bounds per tile row).<br>• **Jet / Shot vs. Entities:** AABB bounding-box check against active entity slots. |
| **Audio** | 4-channel POKEY (engine hum, shot, explosion, low fuel beep, refuel chime). | **NES 2A03 APU:**<br>• **Noise / Triangle:** Engine roar pitched by player throttle speed.<br>• **Pulse 1:** Shot fire, refuel rising tone, low-fuel alarm.<br>• **Pulse 2:** Enemy/bridge sound cues.<br>• **Noise:** Enemy/bridge/player explosions. |

---

## 4. Phased Implementation Plan

### Phase 1: Reverse Engineering & Asset Extraction
**Goal:** Obtain a complete code, data, and behavioral specification of `River Raid (1983)(Activision)(US)[a3].xex`.

1. **ROM Extraction & Static Analysis (REA / Ghidra):**
   - Extract the `$A000–$BFFF` 8 KiB binary slice (`tools/dump_xex.py`).
   - Open in REA/Ghidra with 6502 architecture at base address `$A000`.
   - Map Atari hardware registers (`GTIA $D000`, `POKEY $D200`, `PIA $D300`, `ANTIC $D400`) and OS shadow registers (`VDSLST $0200`, `VVBLKI $0222`, `VVBLKD $0224`, `SDLSTL $0230`, `COLOR0–4 $02C4–$02C8`, `CHBAS $02F4`, `STICK0 $0278`, `STRIG0 $0284`).
   - Decompile and document all subroutines in `$A000–$BFFF`.
2. **Runtime Verification (`tools/altirra_probe.py`):**
   - Boot the XEX in `AltirraBridgeServer`, advance 300 frames, and capture:
     - Display list (`a.dlist()`), GTIA/PMG state (`a.gtia()`, `a.pmg()`), active palette (`a.palette()`), and screenshots.
     - Instruction profile (`a.profile_start(mode="insns")`) during title screen and active gameplay.
   - Identify and verify exact zero-page / RAM addresses for:
     - Procedural river LFSR / seed state, section/bridge counter, scroll fine/coarse counters.
     - Player X, throttle speed, banking frame, missile X/Y.
     - Active object table (X, Y, type: ship/heli/jet/fuel/bridge, direction, patrol bounds).
     - Fuel gauge (16-bit counter, consumption rate, refuel rate), Score (6-digit BCD), Lives.
3. **Asset Extraction (`tools/extract_assets.py`):**
   - Rip the custom character set(s), PMG sprite bitmaps, river generation lookup tables, and sound frequency/control tables directly from the ROM bytes into C headers and PNG sheets.
4. **Deliverables:**
   - `docs/ATARI_RE_SPEC.md` (complete memory map, algorithm spec, and constants).
   - `assets/` & `src/atari_data.h` (extracted bitmaps and lookup tables).

---

### Phase 2: Project Scaffolding & Build Pipeline
**Goal:** Set up a reproducible GBDK-2020 NES build and MesenCE-MCP automated test harness.

1. **Workspace Layout:**
   ```text
   river-raid-port/
   ├── PORTING_PLAN.md
   ├── build.ps1 / Makefile
   ├── docs/
   │   └── ATARI_RE_SPEC.md
   ├── tools/
   │   ├── dump_xex.py
   │   ├── altirra_probe.py
   │   └── extract_assets.py
   ├── assets/
   │   ├── tiles.png
   │   └── sprites.png
   ├── src/
   │   ├── main.c
   │   ├── config.h
   │   ├── atari_data.h
   │   ├── hud.c / hud.h
   │   ├── river.c / river.h
   │   ├── player.c / player.h
   │   ├── entities.c / entities.h
   │   ├── collision.c / collision.h
   │   └── sound.c / sound.h
   ├── roms/
   │   ├── river_raid.nes
   │   └── river_raid.noi
   └── tests/
       ├── mcp_client.mjs
       ├── nes_smoke.mjs
       ├── nes_scroll_hud.mjs
       └── nes_gameplay.mjs
   ```
2. **Build Script (`build.ps1`):**
   - Converts PNG assets via `./gbdk/bin/png2asset` (or compiles extracted 1bpp/2bpp tile arrays directly).
   - Compiles C sources via `./gbdk/bin/lcc -mmos6502:nes -Wl-j -o roms/river_raid.nes src/*.c` to generate both `roms/river_raid.nes` and `roms/river_raid.noi`.
3. **Deliverables:**
   - Working `build.ps1` + `tests/nes_smoke.mjs` verifying ROM boot in `MesenCE-MCP`.

---

### Phase 3: Top HUD, Scanline Split, & CHR Graphics
**Goal:** Render the stationary Top HUD and smoothly scrolling river viewport below scanline 25.

1. **CHR & Palette Setup:**
   - Load the HUD font/digits, fuel gauge tiles, the river tiles, and the three player flight shapes into CHR-RAM.
   - HUD palette `0F 00 28 30` (black/gray/yellow/white); playfield palette `0F 1A 06 12` (black/bank green/outline red/water blue).
2. **Top HUD & Split (`src/hud.c`, `src/main.c`):**
   - Draw the 4-row stationary HUD at nametable rows 0–3 (score, HI, GAME, fuel bar with `E ... F`, lives, BRIDGE count/icons), plus 4 px bottom padding.
   - Keep the HUD in NT0 (`$2000`) and the full 30-row playfield ring in NT1 (`$2400`), enabled by vertical mirroring in the iNES header.
   - At VBlank the NMI sets the HUD scroll `(0, 8)` in NT0 (dropping its blank row 0).  The deferred `add_LCD()` handler then installs the playfield scroll/nametable at scanline 25/26; GBDK's PPUADDR + PPUSCROLL sequence sets both `t` and `v`, so the vertical scroll works mid-frame.  The one-line boundary jitter comes from the faked LCD delay, not the main loop.
3. **Playfield Ring:**
   - `river_update()` moves the fine scroll 1 pixel/frame (downward) and writes the next 32-tile row just above the visible window every 8 pixels.
4. **Verification (`tests/nes_scroll_hud.mjs`):**
   - Confirms 120+ frames of 1-pixel/frame downward scroll with the top 25 scanlines unchanged, a stable split at scanline 25/26, an exact `A[y+1] == B[y]` frozen-scroll shift, and live score/fuel/lives/bridge updates.

---

### Phase 4: Authentic Procedural River & Bridge Streamer
**Goal:** Port the exact Atari procedural river generator and synchronize it with VBlank nametable streaming and an in-RAM collision ring buffer.

**Status: complete.**  The generator, rows, collision ring, orientation and
exact tile cache are implemented and covered by the test suites.

1. **Procedural Generator (`src/river.c`):**
   - Exact 6502 LFSR / table-driven state machine (expanding/contracting banks, straight channels, central islands, feature templates).
   - `tools/river_model.py` matches a 2,596-transition Altirra trace; `src/river.c` matches the model for 400 tile rows (397 sampled before the test attaches).
2. **Row Streamer & Collision Map:**
   - One raw Atari scanline per frame; every 8 rows a 32-tile nametable row is written just above the visible window.  The playfield scrolls **downward** (`playfield_scroll_y` decrements), matching the Atari's newest-row-at-top orientation.
   - 30-row circular geometry ring (`river_left_x`, `river_right_x`, `river_island_left_x`, `river_island_right_x`, `river_flags`), sampled at raw row 3 of each tile row.
3. **Exact CHR cache:**
   - Each visible 8x8 block is uploaded verbatim (no nearest-tile atlas), so 1-px bank staircases and 32-px tree template fragments are exact.
   - CHR layout: 0 blank, 1..10 shared digits, 11..26 gauge, 27..30 P1/HI, 31..46 BRIDGE/GAME, 47 land, 48 water, 49..255 cache (207 slots).  The visible 26-row window peaks at 203 distinct non-solid blocks.
4. **Verification:**
   - `tests/nes_river.mjs`: rows/state/collision against `tests/data/river_golden.json` (397/397).
   - `tests/nes_smoke.mjs` (HUD/CHR/RAM) and `tests/nes_scroll_hud.mjs` (split, 1-px downward shift, HUD) pass.
   - Visual: `build/flip_fixed.png` (correct orientation, continuous edges, assembled trees).

---

### Phase 5: Player Jet, Entities, AI, & Collision Detection
**Goal:** Full playable loop with Atari-accurate physics, enemy behavior, fuel mechanics, and scoring.

1. **Player Jet & Missile (`src/player.c`):**
   - Horizontal movement, banking sprite animation, throttle speed control (Up = fast, Down = slow, neutral = cruise), and player shot movement (including guided vs. straight missile behavior if selectable).
   - Fuel depletion timer and continuous refueling + chime when overlapping a `FUEL` depot.
2. **Enemy & Bridge Manager (`src/entities.c`):**
   - Spawn ships, helicopters, jets, fuel depots, and bridges according to the original Atari spawn tables and difficulty progression (stationary enemies on bridge 1, patrolling helicopters/ships on later bridges, fast crossing jets).
   - Render all active entities via GBDK metasprites, hiding any entity whose Y coordinate enters the Top HUD zone (`Y < 24`).
3. **Collision System (`src/collision.c`):**
   - **Player vs. Terrain:** Check player jet hitbox against `river_left_x` / `river_right_x` / `island_*` for the jet's current row.
   - **Shot vs. Terrain:** Destroy player shot if it hits a riverbank or island edge.
   - **Player / Shot vs. Entities:** Handle enemy destruction, fuel depot destruction (80 pts), bridge destruction (500 pts + checkpoint advance + explosion flash), and player crash/respawn at the last cleared bridge.
4. **Frame-time architecture (done, 2026-10-30):**
   - The NES has ~29,780 CPU cycles per NTSC frame and GBDK's faked LCD split spends ~5.4k of them, so the work is split: `river_update()` (scroll, row generation, tile uploads) runs every frame; the player group (jet, shot, hit tests, collision, sound) and the HUD + entity group run at 30 Hz on alternate frames; `entities_follow()` keeps the entity sprites locked to the 60 Hz scroll.
   - Hot paths are hand-written 6502 (row conversion/fill, tile-job duplicate search, entity draw, jet hit test); repeat river rows (33% of all rows) skip the renderer.
   - `hud.c`, `entities.c`, `entity_gfx.c`, `player.c` live in switchable bank 1; see `PROGRESS.md` for the measured results and the remaining limits (fast flight is at the CPU limit).
5. **Verification:**
   - Done: `tests/nes_entity_hits.mjs` (20 hit-box boundary cases), `tests/nes_sprite_lock.mjs`, ROM-diff scripts (`tests/compare_*.mjs`), profilers (`tests/perf_*.mjs`).
   - Still to write (`tests/nes_gameplay.mjs`): automated Mesen MCP test scripts verifying steering, speed changes, shooting enemies/bridges, refueling, crash/respawn, and score increments via `.noi` symbols.

---

### Phase 6: Audio Engine (2A03 APU), Game Modes, & Final Polish
**Goal:** Complete audiovisual authenticity and game-mode parity.

1. **APU Sound Engine (`src/sound.c`):**
   - Direct register writes to `$4000–$4015` driven by a 60 Hz frame tick (`add_TIM` or end-of-frame update):
     - Continuous engine hum modulated by jet speed.
     - Priority SFX channels for firing, explosions, refueling rising pitch, and low-fuel warning klaxon.
2. **Game States & Variations:**
   - Title / attract mode, `SELECT` game variation (1/2 players, guided vs. unguided shots), `START` game reset, and Game Over handling.
3. **Final Verification Suite:**
   - End-to-end automated regression run in `MesenCE-MCP` (including Lua APU channel verification) + side-by-side visual/timing comparison with `AltirraBridge`.
