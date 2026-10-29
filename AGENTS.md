# AGENTS.md — Workspace Guidelines & Reference

## Project Status
- **Read `PROGRESS.md` first.** It is the live session handoff: current phase, completed work, open issues, next step, and command reminders.
- **Phase 1 (reverse engineering + asset extraction), Phase 2 (scaffolding/build), Phase 3 (HUD/split/ring) and Phase 4 (generator, collision ring, exact tile cache, orientation) are complete. Phase 5 (player/entities/collision) is next.**
- `PORTING_PLAN.md` is the roadmap; `PROGRESS.md` is the status log. Update `PROGRESS.md`, not this file, as work progresses.

## Project Overview
Port of Atari 8-bit **River Raid** (`xex/River Raid (1983)(Activision)(US)[a3].xex`) to the **NES** in C using **GBDK-2020 v4.5.0** (`./gbdk`).
- **Source Binary:** 8,220-byte XEX containing a 16-byte loader stub (`$9FF0–$9FFF`, sets `RAMTOP=$60`, jumps to `$A000`) and an unbanked **8 KiB Atari 800 Left Cartridge ROM (`$A000–$BFFF`)**, with `RUNAD=$9FF0`.
- **NES Display Architecture:** Stationary **Top HUD** (rows 0-3, scanlines 0-31, plus 4 px bottom padding) in nametable 0 (`$2000`) + **Vertically Scrolling Playfield** (scanlines 36-239, 204 px) in nametable 1 (`$2400`), split by GBDK's deferred `add_LCD()` handler (`LYC=35`, visible split 36/37) with vertical mirroring in the iNES header. The playfield is a full 30-row ring, so fine scrolling never wraps into the HUD. It scrolls **downward**: `playfield_scroll_y` decrements, new tile rows are written just above the visible window, and each 8x8 tile packs raw row 7 at the top. The river uses an **exact 207-slot CHR cache** (each visible 8x8 block uploaded verbatim); there is no fixed edge/tree atlas. BG tiles: 0 blank, 1..10 shared digits, 11..46 HUD art, 47 land, 48 water, 49..255 cache.
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
4. **VBlank Budget:** Never write full screens during gameplay. Stream at most **1 tile row (32 bytes)** + **attribute updates (8 bytes)** and dirty HUD digits per frame. GBDK's VRAM transfer buffer is only `$100-$187` (136 bytes); `set_bkg_data` blocks if it fills, so keep per-row exact-cache uploads small.
5. **Top HUD Sprite Clipping:** Hide or clip any metasprite whose Y coordinate is in the HUD region (`Y < 36`).
6. **Split Scroll Compensation:** GBDK's deferred LCD ISR adds the **current scanline** to `bkg_scroll_y` when it writes the entry. The playfield handler must subtract `HUD_SPLIT_SCANLINE` (mod 240) before `move_bkg()`, otherwise the playfield is 36 px ahead of the ring and the newest row appears at the bottom. The handler in `main.c` does this.
7. **No Fixed River Atlas:** the Atari river is a per-scanline bitmap; trees are template fragments 4*$BC px wide and $BE scanlines tall, often starting on odd bytes. The 8x8 block depends on vertical phase. Upload exact blocks; do not reintroduce nearest-tile atlases.

---

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
  `--mcp --mcp-port=9100 --enableStdout --doNotSaveSettings --preferences.pauseWhenInBackground=false --preferences.pauseWhenInMenusAndConfig=false --preferences.disableOsd=true --video.videoFilter=None --nes.region=Ntsc`
- **Critical Rules:**
  - **Handshake:** Send `initialize` (`protocolVersion: "2024-11-05"`) *without* `Mcp-Session-Id`, capture the `Mcp-Session-Id` response header for all subsequent requests, send `notifications/initialized`, and always include `Accept: application/json, text/event-stream`.
  - **Reset Window:** Wait `900–1200 ms` (until `frame=` advances in `mesen_get_status`) after `mesen_load_rom` or `mesen_reset` before calling breakpoints, save-states, or debugger tools.
  - **Deterministic Stepping:** Pause playback and use `mesen_step {cpuType: "Nes", count: 1, stepType: "PpuFrame"}` for frame-exact assertions.
  - **Input Latching:** `mesen_input_override` latches buttons; always clear with `buttons: ""` in a `finally` block.
  - **Lua Probes:** Always use `emu.log(...)` (never `print(...)`) in `mesen_run_lua_script`.
  - **Cleanup:** Remove breakpoints/Lua scripts and stop `MesenCE-MCP-Windows` when finished if you started it.
