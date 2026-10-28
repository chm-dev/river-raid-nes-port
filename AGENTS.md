# AGENTS.md — Workspace Guidelines & Reference

## Project Status
- **Read `PROGRESS.md` first.** It is the live session handoff: current phase, completed work, open issues, next step, and command reminders.
- **Phase 1 (reverse engineering + asset extraction) is complete. Phase 2 (project scaffolding & build pipeline) is next.**
- `PORTING_PLAN.md` is the roadmap; `PROGRESS.md` is the status log. Update `PROGRESS.md`, not this file, as work progresses.

## Project Overview
Port of Atari 8-bit **River Raid** (`xex/River Raid (1983)(Activision)(US)[a3].xex`) to the **NES** in C using **GBDK-2020 v4.5.0** (`./gbdk`).
- **Source Binary:** 8,220-byte XEX containing a 16-byte loader stub (`$9FF0–$9FFF`, sets `RAMTOP=$60`, jumps to `$A000`) and an unbanked **8 KiB Atari 800 Left Cartridge ROM (`$A000–$BFFF`)**, with `RUNAD=$9FF0`.
- **NES Display Architecture:** Stationary **Top HUD** (rows 0–2, scanlines 0–23) + **Vertically Scrolling Playfield** (rows 3–29, scanlines 24–239) split via GBDK's `add_LCD()` (`_lcd_scanline` / `LYC_REG = 24`).
- **Full Porting Plan:** See `PORTING_PLAN.md`.

---

## Toolchain & Build (`./gbdk`)
- **Compiler:** `./gbdk/bin/lcc -mmos6502:nes` (SDCC `mos6502` backend). **This GBDK build rejects the shorter `-mnes` spelling** — use the explicit port/platform pair.
- **Symbol Generation:** Always pass `-Wl-j` when linking so `.noi` symbol files (`DEF <symbol> 0x<hex>`) are generated alongside `.nes` ROMs for Mesen MCP tests.
- **Asset Conversion:** `./gbdk/bin/png2asset` and `./gbdk/bin/gbcompress`.

### GBDK-NES / SDCC Coding Rules
1. **Flat Static State:** Use global/static variables and fixed-size arrays instead of deep call stacks or pointers to stack locals to avoid SDCC `mos6502` software-stack overhead.
2. **ISR Overlay Safety:** Mark all `add_VBL()`, `add_LCD()`, and `add_TIM()` handlers (and their callees) with `NO_OVERLAY_LOCALS` or `#pragma nooverlay`.
3. **VBlank Budget:** Never write full screens during gameplay. Stream at most **1 tile row (32 bytes)** + **attribute updates (8 bytes)** and dirty HUD digits per frame.
4. **Top HUD Sprite Clipping:** Hide or clip any metasprite whose Y coordinate is in the HUD region (`Y < 24`).

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
  - Clean up server processes (`a.quit()` or `Stop-Process`) and temporary files when done.

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
