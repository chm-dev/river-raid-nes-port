# HANDOFF — read this first

Short, current, rewritten every session. History and rationale live in `PROGRESS.md`
(2,000+ lines): **do not read it whole.** Grep it (`Select-String -Path PROGRESS.md -Pattern 'keyword'`)
and read only the matching `### date - title` entry. `AGENTS.md` has the toolchain rules.

**Last updated:** 2026-10-31 · **Branch:** `main` · **Phase:** 6 (title/menu/sound/HUD polish) in progress

## Where we are

Phases 1-5 are done (reverse engineering, build, HUD/split scroll, river generator and exact tile cache,
jet/entities/scoring/bridge/tank/respawn, 30 Hz logic + 60 Hz river). **Phase 6** (sound, attract, menus, 2P,
HUD polish) is in progress: sound, attract, start-level select, 1P/2P, per-level hi-scores, game select,
`ent_hold`, the HUD polish and the shooters (side tank, fast-helicopter missile) are done and play-tested.

The cart **boots into attract**; every Mesen test calls `startGame()` from `tests/mcp_client.mjs`.

**Performance (2026-10-31):** the river renderer is now hand-written asm for every row kind under SDCC
(`river_render_asm`, with plain rows in `river_render_plain`). The C renderer stays as the host reference
under `#else`. A two-row entity pass no longer happens at 9+ entities (`RIVER_ENT_BUMP_MAX`, SDCC only).
Play-tested: smoother at high levels, but no drastic change at later levels. Level 50 late passes went
from about 5% to about 2% in the trace. Fast flight is still not fully smooth when shooting.

## Next steps (priority order)

1. **Stutter when shooting** (play-tested, still there). Not yet measured: profile a level 20/50 run that
   fires continuously (`perf_pass_trace` with a fire input) and see which group the late passes fall in.
2. **Three suites fail on `main`** (they also fail on `d2ff334`, before the perf work; not investigated):
   `nes_entity_hits` (helicopter and ship hits missed, 8 of 20 cases), `nes_hud_blink` (2P blink, 6 checks),
   `nes_menu` (2P life loss, 2 checks). Start with `nes_entity_hits`: it looks like collision, not the river.
3. Phase 6 polish list in `PORTING_PLAN.md`; feature ideas in `BUGS&FEATS.md`.
4. Possible perf follow-up if (1) points at entities: hand-written entity loop (`ent_move`/`ent_draw`).
   `entity_gfx` must leave bank 1 first (check `_CODE_1` headroom in the map).

Open bugs: `BUGS&FEATS.md` is the source (the "planes replaced by random enemies" bug is stale, see
PROGRESS.md "Plane replaced by random enemies").

## Budgets (check, do not trust these numbers)

- Run `./build.ps1`. It must print `ROM  65552 bytes`; also confirm `_XINIT` in `roms/river_raid.map`
  ends below `$FFFA` (last check: `$FCF9`, about 769 bytes free). The linker does **not** fail on fixed-bank
  overflow, the ROM silently becomes 128 KiB.
- Internal RAM: **4 bytes free** at the last build (`_BSS` ends `$07FB`). `build.ps1` prints the live figure
  and throws if `_BSS` passes `$07FF`. The `-Profile` build has 2-3 bytes less.
- Bank layout: fixed bank = everything else; bank 1 = `entities`, `entity_gfx`, `player`; bank 2 = `hud`
  (`$bankedSources` / `$bank2Sources` in `build.ps1`). Check `_CODE_1` / `_CODE_2` in the map for headroom.
- Zero page `$FC-$FF` is used by the river copy and renderer as pointers (`rt_dst`/`rt_src`). Nothing else
  may use it. Check with the `.noi`: no `DEF` in `$FC-$FF` (see PROGRESS "Asm renderer").

## Decisions not to reopen

- **Fast flight stays at 1.5 px/frame.** 2.0 was play-tested and felt worse; aim perf work at a steadier 1.5.
  Attract and the pre-play scroll-in also use 1.5 (2.0 was too fast / too costly).
- **No Atari screen-saver blank** (`$78`, ~73 min idle): not worth the RAM.
- **Loop shape:** `river_update()` + `sound_update()` at 60 Hz; player/collision and HUD/entities at 30 Hz on
  alternating frames. MMC3 + cc65 was assessed and rejected as too expensive.
- **Exact 207-slot CHR river cache**, no edge/tree atlas. River 24 tile columns at nametable cols 4..27.
- **Owed-row bump off at 9+ entities** (SDCC only). The two-row entity pass was the late pass; the host build
  keeps the old rule (`#ifdef __SDCC` in `river_update`).
- **HUD art comes from `charset.bin`**; do not redraw it. Digits stay 8 px wide (width compromise).
- **Sound** values come from live Atari captures, not estimates. Verify Atari behaviour by sampling over
  time, not static reads (see memory note on the attract freeze misread).
- **The user playtests gameplay.** Mark a fix `[~]` in `BUGS&FEATS.md` until they confirm it.
- **Work on `main`.** Branch only for special cases (what-if experiments, risky or throwaway changes),
  never a scratch copy of the tree. Merge back and delete the branch when done.

## Where to look (grep PROGRESS.md for the quoted title)

| Topic | Entry / file |
|---|---|
| Asm renderer, copy, zero-page pointers, owed-bump cap | "Asm river renderer and entity-pacing cap" |
| River generator, repeat-row skip, tile cache | "Atari river/tree layout + exact tile cache", `tools/river_model.py` |
| Frame loop, 30/60 Hz split, perf numbers | "Performance: 30 Hz game logic", "Late frames", "Row render and pacing" |
| Sound, channel map, POKEY->2A03 conversion | "sound re-derived from the Atari", "Fuel tank sound", `src/sound.c` |
| Attract / SELECT / 2P rules | "attract / SELECT / START read off the Atari", "game variants and two-player rules" |
| HUD geometry, blink, white P2, raised line | "Compact HUD", "HUD: start blink", "HUD: first line raised" |
| Bridge, road, tank, respawn checkpoint | "Bridge and road", "Bridge tank and its shell", "Respawn at the last cleared bridge" |
| Atari reverse-engineering spec | `docs/ATARI_RE_SPEC.md` (disassembly in `docs/atari_disasm_*.txt`; `docs/do_not_read_unless_asked` is exactly that) |
| Roadmap | `PORTING_PLAN.md` |
| Perf work, Altirra tracing, Mesen scripting rules | `info/LESSONS.md` (read the section you need), then `info/MESEN_MCP_NOTES.md`, `info/ALTIRRA_BRIDGE_NOTES.md` |

## Core commands

```powershell
./build.ps1                          # build + RAM asserts; expect ROM 65552
./build.ps1 -Profile                 # same ROM with exported statics, for the perf scripts
./tests/host_river.ps1               # C generator vs Python model (needs zig); run after touching river.c
# Mesen suites need MesenCE-MCP running (launch flags in AGENTS.md)
node tests/nes_smoke.mjs; node tests/nes_scroll_hud.mjs; node tests/nes_river_cache.mjs
node tests/nes_gameplay.mjs; node tests/nes_menu.mjs; node tests/nes_hud_blink.mjs
node tests/nes_hold.mjs; node tests/nes_start_clear.mjs; node tests/nes_sound.mjs
node tests/nes_entity_hits.mjs; node tests/nes_sprite_lock.mjs
# Perf (profile build, start level = variant 0..3 = 1/5/20/50; Up steps the game number)
node tests/perf_pass_trace.mjs roms/river_raid.nes roms/river_raid.noi 3 Up 400
node tests/perf_cadence_levels.mjs roms/river_raid.nes roms/river_raid.noi 3 Up 600
node tests/compare_tile_rows.mjs out.json 800 roms/river_raid.nes roms/river_raid.noi 3   # diff two ROMs' JSON
```

The full command list (profilers, Atari trace tools, asset regeneration) is under "Handy commands" in `PROGRESS.md`.

## End of session (keep this cheap)

1. **Rewrite and trim this file.** Update the date, move finished items out of "Next steps", correct the
   budgets. Target ~100 lines; it may reach ~150 when the extra text is clearly valuable. Remove finished items and anything that is only history;
   detail belongs in a `PROGRESS.md` entry or `BUGS&FEATS.md`, with at most a one-line pointer here.
   Delete what is no longer true instead of annotating it. Check the size and trim *before* adding:

```powershell
(Get-Content HANDOFF.md).Count      # ~100 is the target; past ~150 -> trim first
```

2. **Append one entry to `PROGRESS.md` without reading it** (a Read of that file costs ~35k tokens).
   The closing `'@` must stay at column 0:

```powershell
Add-Content -Path PROGRESS.md -Encoding utf8 -Value @'

### YYYY-MM-DD - Short title (branch `name`)

- What changed and why; ground truth used; build line (ROM size, RAM free); tests run and results.
'@
```

3. Do not edit `AGENTS.md` for status; it holds stable rules only.
