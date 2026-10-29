#!/usr/bin/env python3
"""Validate tools/river_model.py against a captured Atari per-row trace."""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from river_model import RiverState, _s  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
TRACE = ROOT / "build" / "river_trace.jsonl"

FIELDS = {
    "left": 0x52,
    "width": 0x53,
    "phase": 0x54,
    "rowcount": 0x2F,
    "island_flags": 0x2D,
    "feature": 0x36,
    "section_timer": 0x5A,
    "countdown": 0x5D,
    "island_width": 0x5F,
    "margin": 0x2C,
    "level": 0x60,
    "flags23": 0x23,
    "lfsr_a": 0xAA,
    "lfsr_b": 0xAB,
    "lfsr_c": 0xAC,
    "c3": 0xC3,
    "c4": 0xC4,
    "c5": 0xC5,
    "c8": 0xC8,
    "c9": 0xC9,
    "bd": 0xBD,
    "bc": 0xBC,
    "be": 0xBE,
    "ba": 0xBA,
    "bb": 0xBB,
    "b1": 0xB1,
    "b7": 0xB7,
}


def state_from(zp: bytes, obj: bytes) -> RiverState:
    st = RiverState()
    st.left = zp[0x52]
    st.width = zp[0x53]
    st.phase = zp[0x54]
    st.rowcount = zp[0x2F]
    st.island_flags = zp[0x2D]
    st.feature = zp[0x36]
    st.section_timer = zp[0x5A]
    st.countdown = zp[0x5D]
    st.island_width = zp[0x5F]
    st.margin = zp[0x2C]
    st.margin_flag = obj[0x2D]
    st.level = zp[0x60]
    st.flags23 = zp[0x23]
    st.lfsr_a, st.lfsr_b, st.lfsr_c = zp[0xAA], zp[0xAB], zp[0xAC]
    st.seeds = list(zp[0x66:0x6A])
    st.saved = list(zp[0xAD:0xB1])
    st.b5 = zp[0xB5]
    st.c3, st.c4, st.c5 = zp[0xC3], zp[0xC4], zp[0xC5]
    st.c8, st.c9 = zp[0xC8], zp[0xC9]
    st.bd, st.bc, st.be = zp[0xBD], zp[0xBC], zp[0xBE]
    st.ba, st.bb = zp[0xBA], zp[0xBB]
    st.b7, st.b1 = zp[0xB7], zp[0xB1]
    st.v37 = zp[0x37]
    st.obj_count = zp[0x4E]
    st.obj_aux = obj[0x0B:0x16]
    st.obj_x = obj[0x16:0x21]
    st.obj_flags = obj[0x21:0x2C]
    st.hit_flash = zp[0x4F]
    st._target = zp[0x34]
    st._v35 = zp[0x35]
    return st


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else TRACE
    rows = [json.loads(line) for line in path.read_text().splitlines()]
    bad: dict[str, int] = {}
    first: dict[str, tuple] = {}
    total = 0
    for i in range(len(rows) - 1):
        zp = bytes.fromhex(rows[i]["zp"])
        obj = bytes.fromhex(rows[i]["obj"])
        nxt = bytes.fromhex(rows[i + 1]["zp"])
        # Skip a duplicate breakpoint sample of the same row.
        if nxt[0x2F] == zp[0x2F] and nxt[0xAA:0xAD] == zp[0xAA:0xAD]:
            continue
        st = state_from(zp, obj)
        # The capture script pokes $4F=0 and clears $0500-$053F after each
        # sample, so the next row sees no hit flash and $052D=0.
        st.hit_flash = 0
        st.margin_flag = 0
        pred_row = st.step()
        nxt_obj = bytes.fromhex(rows[i + 1]["obj"])
        total += 1
        if st.margin_flag != nxt_obj[0x2D]:
            bad["margin_flag"] = bad.get("margin_flag", 0) + 1
            first.setdefault("margin_flag", (i, st.margin_flag, nxt_obj[0x2D]))
        for name, addr in FIELDS.items():
            got = getattr(st, name) & 0xFF
            want = nxt[addr]
            if got != want:
                bad[name] = bad.get(name, 0) + 1
                first.setdefault(name, (i, got, want))
        # template pointer high byte is $BB and only changes at feature start
        # The original's last fill intentionally writes one byte past the
        # 48-byte row (it overwrites the next row's first land byte).
        if pred_row[:48] != bytes.fromhex(rows[i + 1]["row"]):
            bad["row"] = bad.get("row", 0) + 1
            first.setdefault("row", (i, pred_row[:48].hex(), rows[i + 1]["row"]))
    print(f"compared {total} transitions from {path}")
    for name, count in sorted(bad.items(), key=lambda kv: -kv[1]):
        print(f"  {name:14s} {count:5d}  first @{first[name][0]} got={first[name][1]} want={first[name][2]}")
    if not bad:
        print("all fields and rows match")
    return 0 if not bad else 1


if __name__ == "__main__":
    raise SystemExit(main())
