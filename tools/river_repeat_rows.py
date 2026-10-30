#!/usr/bin/env python3
"""How often is a river row a repeat of the previous one, and can the cheap
state rule used by src/river.c (river_step_state) find them?

A "plain" row (no feature template active, no hit flash) is a pure function of
(left, phase, width, island flag).  The renderer skips such a row when that
signature equals the previous plain row's.  This replays the model, counts the
rows that are really identical to the previous one, the rows the rule skips, and
the false positives (a skipped row that is not identical; must be 0).

Result on 30,000 rows: 34.6% identical, 33.1% skippable, 0 false positives.

Run: python tools/river_repeat_rows.py [rows]
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from river_model import initial_state  # noqa: E402


def main() -> None:
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 30000
    st = initial_state(0)
    st.nes_edges = True
    prev_raw = None
    prev_sig = None
    total = identical = skippable = false_pos = 0
    for _ in range(count):
        flash_before = st.hit_flash
        raw = bytes(st.step())
        plain = (st.feature == 0 or st.rowcount >= st.be) and flash_before == 0
        sig = (st.left, st.phase, st.island_flags & 0x80, st.width) if plain else None
        if prev_raw is not None:
            total += 1
            same = raw == prev_raw
            identical += same
            if sig is not None and sig == prev_sig:
                skippable += 1
                if not same:
                    false_pos += 1
        prev_raw, prev_sig = raw, sig
    print(f"rows {total}: identical {identical} ({100 * identical / total:.1f}%), "
          f"skippable by the state rule {skippable} ({100 * skippable / total:.1f}%), "
          f"false positives {false_pos}")
    sys.exit(1 if false_pos else 0)


if __name__ == "__main__":
    main()
