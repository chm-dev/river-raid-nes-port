# HANDOFF — read this first

Short, current, rewritten every session. Rules are in `AGENTS.md`; the plan is `GBC_PORT_PLAN.md`; history is `PROGRESS.md` (grep it, do not read whole).

**Last updated:** 2026-10-31 · **Branch:** `gbc-port` · **Phase:** milestone 1 not started; no prototype code yet

## Where we are

- The port is GBC-only. The C prototype comes first, then a playtest go/no-go after milestone 2.
- Benchmarks are done (`GBC_PORT_PLAN.md` → Measurements): river and entity passes fit in the GBC double-speed budget at 1.5 px/frame. Player, HUD, VRAM uploads and sound are **not measured**.
- `src/` has the SM83 patches the benchmarks needed. It does **not** build the NES ROM, and there is no GBC build script yet.
- `main` is NES-only. The NES project and its docs are in `archive/nes/`.

## Next steps

1. **Milestone 1:** river, camera and jet on GBC, with the tile job split across frames. Done when scrolling is smooth, there are no tile glitches, and steering pans the view correctly.
   - First: a GBC build and its checks (ROM size, `.map` bank windows, `-Wl-j` symbols). Confirm the header flag (believed `-Wm-yC`) and the MBC5 link.
   - Confirm the river job overrun (about 25.5k M-cycles in `river_run_job`) on the real build.
2. Measure what is not measured yet: player, HUD, uploads.

## Decisions not to reopen

- **GBC only.** DMG is dropped.
- **Fast flight stays at 1.5 px/frame.** NES play-testing found 2.0 worse.
- **Logic keeps NES coordinates.** Only drawing uses the camera. This protects validated Atari behaviour.
- **Exact river tile cache.** 213–228 slots in bank 0 against a measured peak of 182. No atlas.
- **HUD art comes from `charset.bin`.** Do not redraw it.
- **Sound values come from live Atari captures.** Not estimates.
- **The user runs the playtests.** Claude builds the ROM and hands over a checklist.

## Where to look

| Topic | Where |
|---|---|
| Plan, decisions, milestones, risks, Gearboy notes | `GBC_PORT_PLAN.md` |
| Rules, toolchain, Gearboy MCP | `AGENTS.md` |
| River generator and cache | `tools/river_model.py`, `src/river.c`, `tests/host_river.ps1` |
| Sprite and palette generation | `tools/gen_entity_gfx.py` |
| Atari behaviour | `docs/ATARI_RE_SPEC.md`, `docs/atari_disasm_*.txt` |
| Atari RE tools | `info/LESSONS.md` → "Reverse Engineering Tools", `info/ALTIRRA_BRIDGE_NOTES.md` |
| HUD glyphs | `assets/atari/extracted/README.md` |
| Gearboy benchmarks | `tests/gb/`, `tests/gb/bench/` |
| Bugs and feature ideas | `BUGS&FEATS.md` |
| NES history (only if needed) | `archive/nes/` (see its README) |

## Known gaps

- No GBC build script yet (`build.ps1` is NES-only).
- NES code is still in the tree: `build.ps1`, `tests/nes_*.mjs`, `tests/perf_*`, and NES parts of `src/`. Remove or move it when it gets in the way; `main` keeps it until milestone 5.

## End of session (keep this cheap)

1. **Rewrite and trim this file.** Update the date, move finished items out, and correct the budgets. Delete what is no longer true instead of annotating it. Check the size first: `(Get-Content HANDOFF.md).Count` (target ~100, trim past ~150).
2. **Append one entry to `PROGRESS.md` without reading it.** The closing `'@` must stay at column 0:

```powershell
Add-Content -Path PROGRESS.md -Encoding utf8 -Value @'

### YYYY-MM-DD - Short title (branch `name`)

- What changed and why; ground truth used; build line; tests run and results.
'@
```

3. Do not edit `AGENTS.md` for status.
