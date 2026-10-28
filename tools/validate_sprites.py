#!/usr/bin/env python3
"""Validate extracted PMG sprites against the live DLI-generated PMG buffer.

The DLI writes P3 shape row i at:
    $0EBC + (object_y + 1) + (yoff - i)
which is $0F00 + object_y - $43 + (yoff - i).
Rows can be overwritten by later objects (multiplexing), so the report shows
matched rows out of total rows.  The player jet is a straight 14-byte copy at
$0EAA and must match exactly.
"""

from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RAM = (ROOT / "assets" / "atari" / "runtime" / "scroll" / "ram_0000_ffff.bin").read_bytes()
SPR = ROOT / "assets" / "atari" / "extracted" / "sprites"


def main() -> int:
    objs = json.loads((SPR / "objects.json").read_text(encoding="utf-8"))
    players = json.loads((SPR / "player.json").read_text(encoding="utf-8"))["shapes"]

    print("Player P2 at $0EAA (shape index $5E):")
    shape_idx = RAM[0x5E]
    expected = bytes(players[shape_idx])
    actual = RAM[0x0EAA:0x0EB8]
    print(f"  $5E={shape_idx}, match={expected == actual}")
    if expected != actual:
        print("  expected", expected.hex())
        print("  actual  ", actual.hex())

    print("\nActive P3 objects at the captured frame:")
    for slot in range(11):
        typ = RAM[0x0500 + slot]
        if typ == 0:
            continue
        aux = RAM[0x050B + slot]
        x = RAM[0x0516 + slot]
        y = RAM[0x0C + slot]
        s = objs[aux]
        yoff = s["shape_len"] - 1
        best = (-1, 0, 0)
        # The RAM snapshot is taken after the main-loop scroll, so the DLI's
        # Y can be a few rows behind the sampled object_y.  Search a small
        # window for the alignment that matches the DLI write.
        for delta in range(-12, 13):
            matched = 0
            for i in range(yoff + 1):
                addr = 0x0F00 + (y - 0x43 + delta) + (yoff - i)
                if 0 <= addr < 0x10000 and s["shape_bottom_up"][i] == RAM[addr]:
                    matched += 1
            if matched > best[0]:
                best = (matched, delta, yoff + 1)
        matched, delta, rows = best
        print(
            f"  slot {slot:2d} type {typ:02X} aux {aux:02X} X={x:3d} Y={y:3d} "
            f"shape ${s['shape_addr']:04X} rows {matched}/{rows} (DLI Y offset {delta:+d})"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
