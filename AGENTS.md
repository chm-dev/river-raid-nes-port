# AGENTS.md — GBC port workspace rules

Stable rules only. Status lives in `HANDOFF.md`; the roadmap is `GBC_PORT_PLAN.md`.

## Project status
- **Read `HANDOFF.md` first.** Short and current: where we are, next steps, decisions not to reopen, where to look.
- **`GBC_PORT_PLAN.md` is the course of action:** decisions, measurements, technical design, milestones, risks, build and emulator notes.
- **`PROGRESS.md` is append-only history.** Do not read it whole. Grep it for a keyword and read only the matching `### date - title` entry.
- **NES history is in `archive/nes/`** (old HANDOFF, PROGRESS, PORTING_PLAN, BUGS&FEATS, Mesen notes, NES AGENTS and LESSONS). Open it only when a specific NES reference is needed. Nothing there is required reading.
- Keep `HANDOFF.md` ~100 lines (up to ~150 when the extra text is clearly valuable). Trim before adding.

## Project overview
Port of the Atari 8-bit **River Raid** (`xex/River Raid (1983)(Activision)(US)[a3].xex`) to the **Game Boy Color** in C, using GBDK-2020 v4.5.0 (`./gbdk`).
- **GBC only.** DMG and Game Boy Pocket are not targets.
- **Prototype first:** reuse the existing C code, add no new assembly, and decide on a playtest go/no-go after milestone 2.
- **Atari ground truth:** `docs/ATARI_RE_SPEC.md`, `docs/atari_disasm_*.txt`, `assets/atari/extracted/`. The shared river generator is `tools/river_model.py` and `src/river.c`.
- **Logic coordinates:** game logic stays in NES-era coordinates (X = Atari HPOS, Y = NES scanlines 30..239). Only drawing goes through a camera.
- **HUD art:** do not redraw. `assets/atari/extracted/charset.bin` holds the original art; see `assets/atari/extracted/README.md` → "HUD glyph inventory".
- **River:** exact per-scanline bitmap. No fixed atlas: upload the exact 8x8 blocks, which depend on vertical phase.

## Toolchain & build (`./gbdk`)
- **Compile:** `gbdk/bin/lcc.exe -msm83:gb -Isrc` (SDCC `sm83` backend). `-Wf-bo1` for `entities` and `entity_gfx`.
- **Link:** `-Wl-j -Wl-m -Wl-yt0x19 -Wl-yo4` (MBC5, 4 banks). Always pass `-Wl-j` so `.noi` symbols are generated. `-DPROF_EXPORT_STATICS` exposes statics to the profiler.
- **No GBC build script yet.** `build.ps1` is the NES build and no longer builds the NES ROM on this branch. Milestone 1 adds the GBC build and its checks.
- **Asset conversion:** `./gbdk/bin/png2asset`, `./gbdk/bin/gbcompress`, and `tools/gen_entity_gfx.py` (it needs GBC palette output).

### GBC / SDCC rules
1. **Flat static state.** Use globals and fixed arrays, not deep call stacks or pointers to stack locals.
2. **ISR handlers stay short.** Never call a `BANKED` function from an ISR.
3. **SDCC local aliasing.** Do not keep a block-local value live across a call in a complex function. SDCC can alias it with the callee's state and the saved value silently changes. Use a file-scope `static` for values like a saved row offset. (This is the river-renderer `ysave` bug; the code comments cite this rule number.)
4. **Inline `__asm` goes in its own function.** SDCC keeps values in registers across an asm block and corrupts the surrounding C. Raw-row tests can still pass, so verify with real tile pixels (`tests/compare_tile_rows.mjs`, `tests/host_river.ps1`).
5. **Time-based river.** River code runs on `game_ticks`. Benchmarks must advance `sys_time` themselves: `entities_update()` reads it and it only advances with VBlank.
6. **No river atlas.** Trees are template fragments; upload exact blocks.
7. **Banking.** Keep banked modules in a real bank. A single-bank link with `BANKED` functions jumped into unmapped ROM (`RST_38`).
8. **Check the map after every link.** Each bank's code must stay inside its window (`$0000-$3FFF` bank 0, `$4000-$7FFF` for banked code). The linker may not fail on overflow, so treat `.map` as the source of truth until milestone 1 confirms the behaviour.
9. **Profile peaks, not averages** (`LESSONS.md` item 14). Prove an optimisation by diffing output, not only with a test (`LESSONS.md` item 18).

## Emulator and tests: Gearboy (`./gearboy`)
Gearboy is the GB/GBC emulator with an MCP server. The folder is gitignored and local to this machine.
- **Launch (headless):** `gearboy/Gearboy.exe --headless --mcp-http --mcp-http-port 7777 rom.gb rom.noi`. MCP endpoint: `http://127.0.0.1:7777/mcp`.
- **Starts paused:** call `debug_continue`.
- **Protocol:** requests after `initialize` need the header `MCP-Protocol-Version: 2025-11-25`.
- **Memory:** `read_memory` area 6 is WRAM, with offsets from `$C000`.
- **Profiler:** reports units of 70,224 per frame at any CPU speed. M-cycles = units / 4 in single speed, units / 2 in double speed (the ROM runs double). Recipe: `tests/gb/profile_play.mjs`.
- **Speed:** fast-forward did not speed up headless runs, so keep benchmarks short.
- **Shutdown:** stop the emulator process when finished.
- **Client:** `tests/gb/gearboy_client.mjs`. Benchmarks are in `tests/gb/bench/`.
- `tests/host_river.ps1` checks the C generator against `tools/river_model.py` on the host (needs `zig cc`). Run it after touching `river.c` generator code.

## Atari reverse engineering (dynamic analysis)
- AltirraBridge: `F:\Emu\Atari800\Altirra\AlitrraBridge\` (note the spelling). Rules and the river-row capture recipe are in `info/LESSONS.md` → "Reverse Engineering Tools". API reference: `info/ALTIRRA_BRIDGE_NOTES.md`.
- Sound values come from live Atari captures, not estimates. Sample over time and do not trust one static read (see memory on the attract freeze).
- Tool list: `tools/README.md`.

## Workflow
- **The user runs the playtests.** Build the ROM, then hand over a checklist. Do not drive the emulator to confirm gameplay fixes.
- **Bugs and features:** `BUGS&FEATS.md`. Mark a fix `[~]` until the user confirms it in play, then `[x]`.
- **Branches:** work on `gbc-port` until the go decision. `main` stays NES-only until milestone 5, then the two merge. Branch only for risky or throwaway experiments, never a scratch copy of the tree.
- **Session end:** the steps are at the bottom of `HANDOFF.md`. Do not edit `AGENTS.md` for status.
