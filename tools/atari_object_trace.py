#!/usr/bin/env python3
"""Capture the Atari object spawner's state at every generated row.

Like atari_river_trace.py, but object slots are left alive so the spawn block
($AB80-$AE96) runs for real.  Each sample holds zero page and the object
arrays at $AB80; tools/object_model.py replays the spawn decision from sample
n and compares it with the object arrays of sample n+1.

Usage:
    python tools/atari_object_trace.py --rows 900 --out build/object_trace.jsonl
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from atari_river_trace import (  # noqa: E402
    BP_RENDER_DONE,
    DEFAULT_XEX,
    ROOT,
    connect_any,
)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--token")
    ap.add_argument("--xex", type=Path, default=DEFAULT_XEX)
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "object_trace.jsonl")
    ap.add_argument("--rows", type=int, default=900)
    ap.add_argument("--warmup", type=int, default=320)
    ap.add_argument("--level", type=int, default=0, help="poke $60 (level) at start")
    args = ap.parse_args()

    a = connect_any(args.token)
    a.config("video", "ntsc")
    a.fresh()
    a.boot(str(args.xex))
    a.frame(args.warmup)
    a.clock()
    a.consol(start=True)
    a.frame(2)
    a.clock()
    a.consol()
    a.frame(4)
    a.clock()

    a.poke(0x76, 0x00)
    a.joy(0, "up")

    bp = a.bp_set(BP_RENDER_DONE)
    rows: list[dict] = []
    steps = 0
    try:
        while len(rows) < args.rows and steps < 200000:
            a.cycles(2000)
            a.clock()
            steps += 1
            pc = int(a.regs()["PC"].lstrip("$"), 16)
            if pc != BP_RENDER_DONE:
                continue
            zp = a.memdump(0x0000, 0x100)
            obj = a.memdump(0x0500, 0x40)
            rows.append({"n": len(rows), "zp": zp.hex(), "obj": obj.hex()})
            # Stay alive.  The game re-initialises $60 while it starts, so
            # the requested level can only be poked once rows are flowing;
            # each sample is self-contained, so a mid-run jump is fine.
            if args.level and len(rows) == 3:
                a.poke(0x60, args.level)
            a.poke(0x6A, 0x63)
            a.poke(0x4F, 0x00)
            a.bp_clear(bp)
            while True:
                a.cycles(64)
                a.clock()
                if int(a.regs()["PC"].lstrip("$"), 16) != BP_RENDER_DONE:
                    break
            bp = a.bp_set(BP_RENDER_DONE)
    finally:
        a.joy(0, "centre")
        a.bp_clear(bp)
        a.resume()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8") as fh:
        for sample in rows:
            fh.write(json.dumps(sample) + "\n")
    print(f"captured {len(rows)} rows -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
