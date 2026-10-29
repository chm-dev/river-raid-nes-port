#!/usr/bin/env python3
"""Generate golden NES tile patterns for the river cache test.

For every tile row (8 Atari rows) of tools/river_model.py, write the 24 river
tiles exactly as they must appear in CHR RAM: 16 planar bytes each (8 bytes of
plane 0, then 8 of plane 1), top scanline first.  Row s is hex-encoded as
24 * 16 * 2 characters.

Used by tests/nes_river_cache.mjs, which checks that every nametable row that
is on screen references CHR tiles holding exactly these patterns.

Run: python tools/gen_river_tiles_golden.py [tile_rows]
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from river_model import initial_state  # noqa: E402

OUT = Path(__file__).resolve().parent.parent / "tests" / "data" / "river_tiles_golden.json"


def planes(b0: int, b1: int) -> tuple[int, int]:
    p0 = p1 = 0
    for b in (b0, b1):
        for i in range(4):
            v = (b >> (6 - 2 * i)) & 3
            p0 = (p0 << 1) | (v & 1)
            p1 = (p1 << 1) | (v >> 1)
    return p0, p1


def main() -> None:
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 120
    st = initial_state(0)
    rows = []
    for _ in range(count):
        raw = [bytes(st.step()) for _ in range(8)]
        out = bytearray()
        for c in range(24):
            p0 = [0] * 8
            p1 = [0] * 8
            for idx, r in enumerate(raw):
                # raw row idx is tile scanline 7 - idx (see river_convert_row)
                p0[7 - idx], p1[7 - idx] = planes(r[2 * c], r[2 * c + 1])
            out += bytes(p0) + bytes(p1)
        rows.append(out.hex())
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(rows))
    print(f"wrote {len(rows)} tile rows to {OUT}")


if __name__ == "__main__":
    main()
