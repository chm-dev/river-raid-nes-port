# Tools

All scripts are run from the repository root with the workspace Python.
The AltirraBridge SDK lives at `F:\Emu\Atari800\Altirra\AlitrraBridge\sdk\python`.

## Source / static analysis

| Tool | Purpose |
|---|---|
| `dump_xex.py` | Split the XEX into its loader and 8 KiB cartridge slice. |
| `altirra_probe.py` | AltirraBridge CLI: disassemble, capture, dump RAM, profile, search, input. |
| `atari_scroll_probe.py` | Per-frame trace of zero page, object arrays and the display list; optional START/steer/fire. |
| `atari_snapshot.py` | Deterministic fresh-boot state and screenshot capture. |
| `disasm_stats.py` | Summarize a disassembly listing. |
| `decode_dl.py` | Decode a runtime display list and summarize mode-E runs. |

## Evidence / validation

| Tool | Purpose |
|---|---|
| `pmg_map_probe.py` | Bare-machine probe that maps PMBASE pages to player slots. |
| `pmg_map_probe2.py` | Identifies page -> player mapping and HPOSP -> pixel X. |
| `pmg_line_probe.py` | Calibrates screenshot row vs ANTIC scanline with a marker DL line. |
| `dli_probe.py` | Breakpoint experiment for the DLI sprite loop. |
| `hud_map_probe.py` | Patches a HUD glyph to locate its screen column. |
| `hud_state_probe.py` | Dumps HUD buffer, DL tail and zero page in attract/gameplay. |

## Rendering / extraction

| Tool | Purpose |
|---|---|
| `render_mode_e.py` | Correct mode-E playfield renderer for a RAM dump. |
| `render_full_frame.py` | Full 336x224 frame from RAM + extracted ROM shapes (validation). |
| `export_row_reference.py` | Exports the live 157-row playfield and generator state. |
| `extract_assets.py` | Generates `assets/atari/extracted/*` and `src/atari_data.{h,c}`. |

## Deprecated

* `render_mode14.py` - wrong mode/stride assumption; use `render_mode_e.py`.
* `antic_mode14_probe.py` - early mode-14 probe; use `pmg_line_probe.py`.

## Notes

* `AltirraBridge.from_token_file()` fails on stale token files. The newer
  probes retry every `%TEMP%\altirra-bridge-*.token`; prefer passing
  `--token C:\Users\<user>\AppData\Local\Temp\altirra-bridge-<pid>.token`
  when you know the live server.
* `frame(n)` is a gate: the next command waits for `n` emulated frames.
* The bundled GBDK lcc needs `-mmos6502:nes` (not `-mnes`) for NES targets.
