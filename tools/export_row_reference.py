#!/usr/bin/env python3
"""Export the live river playfield exactly as the display list shows it.

Parses the runtime display list at $3F00, follows every mode-E line from its
LMS base, and writes:

  river_rows_157.bin     157 x 48 raw mode-E bytes, top row first
  river_reference.png    192 x 157, Atari palette, 1 px per ANTIC pixel
  river_state.json       display list segments and generator variables

This is the ground-truth reference for the procedural generator port.
"""

from __future__ import annotations

import json
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
RAM_PATH = ROOT / "assets" / "atari" / "runtime" / "scroll" / "ram_0000_ffff.bin"
PALETTE_PATH = ROOT / "assets" / "atari" / "palette_altirra_ntsc.bin"
OUT = ROOT / "assets" / "atari" / "extracted"
ROW_BYTES = 48


def parse_dl(ram: bytes) -> list[dict]:
    """Return mode-E runs as {start, count, dl_addr}."""
    runs: list[dict] = []
    addr = 0x3F00
    screen = None
    while addr < 0x3FC0:
        b = ram[addr]
        if b == 0x41:
            break
        if b in (0x4E, 0x56, 0x46, 0x5E):
            screen = ram[addr + 1] | (ram[addr + 2] << 8)
            run = {"start": screen, "count": 0, "dl_addr": addr, "ctl": b}
            runs.append(run)
            addr += 3
            continue
        if (b & 0x0F) == 0x0E:
            if not runs:
                runs.append({"start": screen, "count": 0, "dl_addr": addr, "ctl": b})
            runs[-1]["count"] += 1
            screen += ROW_BYTES
        addr += 1
    return runs


def main() -> int:
    ram = RAM_PATH.read_bytes()
    pal = PALETTE_PATH.read_bytes()
    runs = parse_dl(ram)
    rows = []
    addresses = []
    for run in runs:
        a = run["start"]
        for _ in range(run["count"]):
            rows.append(ram[a : a + ROW_BYTES])
            addresses.append(a)
            a += ROW_BYTES
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "river_rows_157.bin").write_bytes(b"".join(rows))

    im = Image.new("RGB", (ROW_BYTES * 4, len(rows)))
    p = im.load()
    colors = [0x00, ram[0x0B] & 0xFE, 0x20, 0x84]
    for y, row in enumerate(rows):
        for x, byte in enumerate(row):
            for i in range(4):
                value = (byte >> (6 - i * 2)) & 3
                c = colors[value]
                p[x * 4 + i, y] = tuple(pal[c * 3 : c * 3 + 3])
    im.save(OUT / "river_reference.png")

    state = {
        "segments": [
            {"lms_ctl": f"${r['ctl']:02X}", "start": f"${r['start']:04X}", "lines": r["count"]}
            for r in runs
        ],
        "total_lines": len(rows),
        "first_last_row": [f"${addresses[0]:04X}", f"${addresses[-1]:04X}"],
        "lmargn_52": ram[0x52],
        "rmargn_53": ram[0x53],
        "rowcrs_54": ram[0x54],
        "colcrs_56": ram[0x56],
        "dindex_57": ram[0x57],
        "oldrow_5a": ram[0x5A],
        "oldcol_5b": ram[0x5B],
        "oldcol_hi_5c": ram[0x5C],
        "oldchr_5d": ram[0x5D],
        "oldadr_5e": ram[0x5E],
        "oldadr_hi_5f": ram[0x5F],
        "level_60": ram[0x60],
        "counter_61": ram[0x61],
        "counter_62": ram[0x62],
        "seed_66_69": list(ram[0x66:0x6A]),
        "lfsr_aa_ac": list(ram[0xAA:0xAD]),
        "flags_2d": ram[0x2D],
        "flags_36": ram[0x36],
        "colpf0_index": ram[0x0B],
    }
    (OUT / "river_state.json").write_text(json.dumps(state, indent=1), encoding="utf-8")
    print(json.dumps(state, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
