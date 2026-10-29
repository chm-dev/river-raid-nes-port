#!/usr/bin/env python3
"""Generate the C-port golden river rows from tools/river_model.py.

Each entry is one NES tile row (8 Atari rows):
  row       the 48-byte mode-E row at raw row 7 (the C debug row)
  slot      ring slot used by river_stream_next() for this tile row
  collision geometry at raw row 3, matching river.c
  state     [left,width,phase,rowcount,island_flags,feature,level,
             lfsr_a,lfsr_b,lfsr_c,countdown,flags23]

Run: python tools/gen_river_golden.py [tile_rows]
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from river_model import initial_state  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "tests" / "data" / "river_golden.json"


def bounds(st) -> dict[str, int]:
    island = st.island_flags & 0x80
    left = st.left if island else (st.left + st.phase) & 0xFF
    il = 0
    ir = 0
    if island and st.phase >= 6:
        il = (left + (st.width >> 1)) & 0xFF
        ir = (il + (st.phase << 1)) & 0xFF
        right = (ir + (st.width >> 1)) & 0xFF
    elif island:
        right = (left + st.width + (st.phase << 1)) & 0xFF
    else:
        right = (left + st.width) & 0xFF
    flags = 0
    if st.flags23 & 0x80:
        flags |= 0x01
    if island:
        flags |= 0x02
    if st.feature:
        flags |= 0x04
    return {"left": left, "right": right, "il": il, "ir": ir, "flags": flags}


def state(st) -> list[int]:
    return [
        st.left,
        st.width,
        st.phase,
        st.rowcount,
        st.island_flags,
        st.feature,
        st.level,
        st.lfsr_a,
        st.lfsr_b,
        st.lfsr_c,
        st.countdown,
        st.flags23,
    ]


def main() -> None:
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 400
    st = initial_state()
    entries = []
    for tile_row in range(n):
        collision = None
        row = None
        for i in range(8):
            row = st.step()
            if i == 3:
                collision = bounds(st)
        entries.append(
            {
                "tile_row": tile_row,
                "slot": (28 - tile_row) % 30,
                "row": row[:48].hex(),
                "collision": collision,
                "state": state(st),
            }
        )
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(entries, separators=(",", ":")), encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)}: {len(entries)} tile rows")


if __name__ == "__main__":
    main()
