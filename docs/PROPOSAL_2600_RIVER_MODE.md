# PROPOSAL — 2600-style river mode (NOT APPROVED)

**Status: proposal, not approved. Drafted 2026-11-01, amended the same day.**

**Agents: do not start, schedule, cite or act on this file** until the user records a decision in the "Decision" section at the bottom. Do not change `HANDOFF.md`, `AGENTS.md` or `GBC_PORT_PLAN.md` because of it. It is deliberately not linked from them.

If approved, this proposal reopens three current rules:
- **"No river atlas"** (`AGENTS.md` rule 6, `HANDOFF.md` decisions). A 2600 river is built from straight and angled edges, so a fixed edge-tile atlas becomes possible.
- **"DMG is dropped."** DMG is the whole motivation, but it is only committed to after the DMG prototype passes (see Gates).
- **"Add no new assembly."** Not part of the prototype. It is a decision for after the DMG gate, and only for routines that still miss the budget.

It also does not come before the milestone 2 go/no-go that is waiting on the user.

## Motivation

The main reason for a 2600 mode is **DMG**. The current exact per-scanline river is too costly for a DMG, which has half the CPU clock of GBC double speed and a single 384-tile VRAM bank. A 2600-style river (straight and angled edges) can use a small fixed tile atlas, so scrolling costs tilemap writes plus `SCY`, with no per-frame tile generation or uploads.

Figures below are from `HANDOFF.md`, not re-measured:
- Fast speed runs ~0.75 passes per frame, cruise 1.00, at GBC double speed.
- `river_update` peak alone exceeds a 70k-unit frame.
- The tile cache needs 213–228 slots (measured peak 182).
- Bank 0 has ~2.7 KB free.

## What would be built, if everything passes

- A **2600 river mode** in the shared generator (`tools/river_model.py`, `src/river.c`): edges as straight and angled segments, with collision and drawing sharing one edge description. Logic stays in NES coordinates.
- A **fixed-atlas renderer** per platform.
- **Availability, only after the DMG prototype passes:**
  - **DMG: 2600 is the only mode.** No exact river on DMG.
  - **GBC: selectable option.**
  - **NES: selectable option. This is required.** If the feature is added, the NES build must have it too. It arrives through the shared generator at the milestone 5 merge, because `main` stays NES-only until then.
- Runtime selection versus a compile-time build flag is decided after the gate. Keeping both the exact cache and the atlas costs ROM and VRAM, and bank 0 is tight.

## Reference

The modded Atari 8-bit XEX is **`xex\River Raid 2600.xex`**. It uses a 2600-style river with straight edges. It is the ground truth for the 2600 mode, as the original `xex\River Raid (1983)(Activision)(US)[a3].xex` is for the current river.

## Plan: two stages, one gate

The GBC build is the testbed: it has the headroom and the profiler setup. The decision that matters is the DMG prototype.

### Stage 1 — 2600 river on GBC (testbed)

1. **Scope the mod.** Byte-diff `xex\River Raid 2600.xex` against the original XEX to see what changed. Then capture river rows over time with AltirraBridge (recipe: `info/LESSONS.md`). Sample across frames, not a single read. Record edge slopes, segment lengths, and any non-river changes (spawns, collision, sound).
2. **Model.** Add a 2600 edge mode to `tools/river_model.py`. Extend `tests/host_river.ps1` to check both modes against the C generator.
3. **C generator.** Add the second mode to `src/river.c` behind a compile-time flag. Same edge interface for collision and drawing.
4. **Atlas renderer on GBC.** Fixed edge tiles in VRAM, tilemap writes only. Check each bank's `.map` window after every link (`AGENTS.md` rule 8).
5. **Measure** against the baseline (table below), including the DMG CPU proxy.
6. **Playtest checklist** handed to the user. The user runs it.

**Stage 1 check (user's call):** the 2600 river removes enough of the CPU cost to make a DMG attempt worth trying, and it plays right.

### Stage 2 — DMG prototype (the gate)

Only if the Stage 1 check passes and the user approves:

7. **DMG build variant.** Strip GBC-only parts: VRAM bank 1, BG attribute maps, RGB palettes, `cpu_fast()`. Add a DMG header and build checks. Rewrite `oam_camera` for the DMG sprite path.
8. **VRAM layout in 384 tiles:** atlas, HUD, sprites, and a single bank.
9. **4-shade look:** tell fuel, bridges and enemies apart by shape and shade. (Asset work is outside this plan's scope but must be proven readable.)
10. **Measure on the DMG build** with real DMG numbers, not the proxy, and re-run the table.
11. **Playtest checklist** handed to the user. The user runs it.

## Gates

The user sets the numbers before measuring. Suggested form, values left to the user:

**Stage 1 check**
- Fast speed reaches **X** passes per frame at GBC double speed.
- The single-speed proxy reaches **Y** passes per frame at fast speed.
- Collision matches the drawn edges, and the playtest feels right.

**DMG gate (commitment point)**
- Fast speed reaches **A** passes per frame on the DMG build.
- Atlas, HUD and sprites fit within **Z** of the 384 tiles.
- Entities remain readable in 4 shades.
- The playtest feels right.

**Only if the DMG gate passes** is the 2600 mode added to the whole build as an option (GBC and the required NES), with DMG using it as its only mode. If it fails, the proposal is dropped and nothing is added to GBC or NES.

## Measurements

Peaks, not averages (`AGENTS.md` rule 9). Take a **baseline first** on the current exact-river build, then repeat the same runs with the 2600 mode (and again on the DMG build in Stage 2). Use `tests/gb/profile_play.mjs` (build with `-ProfExport`). The user's no-tests instruction applies until this is approved; these are future work.

| # | Measurement | Notes |
|---|---|---|
| 1 | Passes per frame, cruise and fast | Baseline ~1.00 and ~0.75 |
| 2 | `river_update` peak, per-row cost, per-job cost, clamp counts | Baseline clamps: see `RIVER_JOB_COLS` note in `HANDOFF.md` |
| 3 | Player and entity group cost | Name them in the profile first; today they appear only as `___sdcc_bcall_ehl` |
| 4 | `oam_camera` cost | Baseline 7–12k units |
| 5 | River tile cache peak | Baseline 182 |
| 6 | Total VRAM tiles used (BG and sprite, both banks) | Needed for the 384-tile DMG estimate |
| 7 | Bank 0 free space, and every bank window | Baseline ~2.7 KB in bank 0 |
| 8 | **DMG CPU proxy** (Stage 1 only) | Build the GBC ROM without `cpu_fast()`. CGB single speed runs at the DMG clock. M-cycles = units / 4 in single speed. It covers CPU cost only, not VRAM or palette limits |
| 9 | 2600-only: atlas tile count (slopes × vertical phases) and tilemap writes per frame | New numbers |

## After the gate (not part of the prototype)

- Runtime-selectable mode versus compile-time only.
- Targeted assembly for any routine that still misses the budget (separate `.s` file; diff its output against the C version).
- NES rollout at the milestone 5 merge (required if the feature is added).
- Mod licensing, if anything derived from the mod is ever shipped.

## Risks

- The mod may differ from the original in more than the river, which would pollute the capture.
- Quantised edges only approximate validated Atari behaviour; collision and drawing must share one source.
- It removes the river cost, not the player, entity, `oam_camera`, HUD or sound costs. Sound is not yet ported and must also fit.
- The test matrix grows with every mode and platform, and playtests are manual.
- A Stage 1 pass does not guarantee a DMG pass. The proxy ignores VRAM, palette and readability limits.

## Decision

_Not decided. The user records approval, rejection or changes here._
