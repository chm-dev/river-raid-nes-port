# River Raid NES: Scrolling Optimisation Summary

Late frames in fast flight fell from 16-25% to 1-3.3%, with no game content cut. Occasional stutter remains and was judged acceptable in play.

## Outcome

| Scenario (400 frames) | Before | After |
| --- | --- | --- |
| Level 1 cruise | 0.0% | 0.0% (last measured after the first merge) |
| Level 50 cruise | 8.8% | 1.3% |
| Level 1 fast | 17.3% | 1.8% |
| Level 5 fast | 15.8% | 1.0% |
| Level 20 fast | 22.8% | 1.5% |
| Level 50 fast | 24.6% | 3.3% |

## Causes

- **Budget:** a main-loop pass has about 23.7k cycles.
- **Visible hitch:** a late pass repeats the whole previous frame, then jumps (`0` px, then `3` px).
- **Self-sustaining:** a late pass owes more rows, which can make the next pass late too (`03 03 03`).
- **Costs above the documented figures:** one row took 7-8.6k cycles, one tile job 9-13k, and 11 entities about 13.5k.
- **Wasted work:** only about 5 of 24 columns in a tile row ever need pattern data.

## What changed

- **Row conversion:** columns that stay solid are skipped; earlier lines are filled in only if a column stops being solid.
- **Plain rows:** a hand-written path covers rows with no island or feature (34% of all rows).
- **Tile job:** hand-written `river_put`, unrolled copies, and a dedupe search that only walks mixed columns.
- **Lookup tables and small fixes:** tables replace per-byte edge recolouring; the land refill and a per-frame multiply are gone; the row clear is unrolled and the LFSR step is hand-written.
- **Dead code:** removed the river flash logic, which could never run.
- **Pacing:** up to 3 rows on the player pass and 1 on the entity pass, rising to 2 with the tile job allowed when `river_owed >= 2`.
- **Tooling:** added `./build.ps1 -Profile`, `tests/perf_pass_trace.mjs`, `tests/perf_exp.ps1`, and start-level support in `compare_tile_rows`.

## Decisions and verification

- **Fast speed stays at 1.5 px/frame:** 2.0 gives even steps on paper but felt worse in play.
- **Dedupe stays:** it removes only 4% of uploads but keeps the peak cache window about 25 slots lower.
- **Pixels unchanged:** 0 of 846 tile rows differ from the earlier ROM across start levels 1, 20 and 50.
- **Tests:** all 13 Mesen suites, `nes_ring_integrity` and `host_river` pass.
- **Known gap:** `tools/object_host_check.py` no longer links, so the spawner has no host check. The spawner code was not changed.
- **Limits of the numbers:** the runs use a scripted, invulnerable jet, so enemy-heavy moments may cost more.

## Remaining options

None of these are needed now; the gains are estimates, and none has been built.

- **Hand-written entity loop:** about 30% of entity cost, mainly at level 20+. `entity_gfx` must first move out of bank 1, which has only 154 bytes free.
- **Use the split wait:** the NMI spins through about 3.2k cycles per frame, which could do bounded work while the main loop is idle. It would end on a sprite-0 hit, which may also remove the 1-line split jitter. This is the riskiest option and needs a hardware test.
- **Cheaper feature rows and state step:** hand-write the river rows with trees and the per-row state step, both still in C.
- **Re-tune pacing:** re-run `tests/perf_pass_trace.mjs` if new features make entity passes heavier, since the best row split may change.
- **Fewer entity updates (last resort):** update less often or split them across frames, at the cost of less accurate movement.
- **Not worth doing:** removing dedupe (cache overflow risk), shrinking the HUD (about 114 cycles per line), or cutting trees.
