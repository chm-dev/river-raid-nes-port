# HANDOFF — read this first

Short, current, rewritten every session. History and rationale live in `PROGRESS.md`
(2,000+ lines): **do not read it whole.** Grep it (`Select-String -Path PROGRESS.md -Pattern 'keyword'`)
and read only the matching `### date - title` entry. `AGENTS.md` has the toolchain rules.

**Last updated:** 2026-10-31 · **Branch:** `main` · **Phase:** 6 (title/menu/sound/HUD polish) in progress

## Where we are

Phases 1-5 are done (reverse engineering, build, HUD/split scroll, river generator and exact tile cache,
jet/entities/scoring/bridge/tank/respawn, 30 Hz logic + 60 Hz river). **Phase 6** (sound, attract, menus, 2P,
HUD polish) is in progress: sound, attract, start-level select, 1P/2P, per-level hi-scores, game select,
`ent_hold` and the HUD polish are done and tested. Enemy shooters (side tank, fast-helicopter missile) are
ported and play-tested; OAM layout: 2-3 tank, 4-5 shell, 6 missile, 7+ entities.

The cart **boots into attract**; every Mesen test calls `startGame()` from `tests/mcp_client.mjs`.

## Next steps (priority order)

1. **Check start levels 5/20/50** against the river model and `tests/nes_ring_integrity.mjs`.
   CHR-cache overflow (more visible 8x8 blocks than the 250 cache slots) is reachable from the first
   screen at high levels. Entry: PROGRESS.md "Known issues" 1d.
1b. **Model vs Atari trace at level 20** still diverges from row 264 (see PROGRESS.md Level 20 river edge); examine with `tools/atari_river_trace.py --select 4`.
2. **Attract extras:** the Atari's idle screen-saver blank (`$78`) and attract scroll speed (`$59`).
3. Re-measure `ENT_WAIT_ROWS` if the playfield height or start scroll changes.

Open bugs: `BUGS&FEATS.md` is the source (the "planes replaced by random enemies" bug is stale, see
PROGRESS.md "Plane replaced by random enemies").

## Budgets (check, do not trust these numbers)

- Run `./build.ps1`. It must print `ROM  65552 bytes`; also confirm `_XINIT` in `roms/river_raid.map`
  ends below `$FFFA`. The linker does **not** fail on fixed-bank overflow, the ROM silently becomes 128 KiB.
- Internal RAM was **4 bytes free** at the last build (2026-10-31); `digit_buf` (16 B) can alias a shared
  scratch if more is needed. `build.ps1` prints the live figure and throws if `_BSS` passes `$07FF`.
- Bank layout: fixed bank = everything else; bank 1 = `entities`, `entity_gfx`, `player`; bank 2 = `hud`
  (`$bankedSources` / `$bank2Sources` in `build.ps1`). Check `_CODE_1` / `_CODE_2` in the map for headroom.

## Decisions not to reopen

- **Fast flight stays at 1.5 px/frame.** 2.0 was play-tested and felt worse; aim perf work at a steadier 1.5.
- **Loop shape:** `river_update()` + `sound_update()` at 60 Hz; player/collision and HUD/entities at 30 Hz on
  alternating frames. MMC3 + cc65 was assessed and rejected as too expensive.
- **Exact 207-slot CHR river cache**, no edge/tree atlas. River 24 tile columns at nametable cols 4..27.
- **HUD art comes from `charset.bin`**; do not redraw it. Digits stay 8 px wide (width compromise).
- **Sound** values come from live Atari captures, not estimates. Verify Atari behaviour by sampling over
  time, not static reads (see memory note on the attract freeze misread).
- **The user playtests gameplay.** Mark a fix `[~]` in `BUGS&FEATS.md` until they confirm it.
- **Work on `main`.** Branch only for special cases (what-if experiments, risky or throwaway changes),
  never a scratch copy of the tree. Merge back and delete the branch when done.

## Where to look (grep PROGRESS.md for the quoted title)

| Topic | Entry / file |
|---|---|
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
./tests/host_river.ps1               # C generator vs Python model (needs zig); run after touching river.c
# Mesen suites need MesenCE-MCP running (launch flags in AGENTS.md)
node tests/nes_smoke.mjs; node tests/nes_scroll_hud.mjs; node tests/nes_river_cache.mjs
node tests/nes_gameplay.mjs; node tests/nes_menu.mjs; node tests/nes_hud_blink.mjs
node tests/nes_hold.mjs; node tests/nes_start_clear.mjs; node tests/nes_sound.mjs
node tests/nes_entity_hits.mjs; node tests/nes_sprite_lock.mjs
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
