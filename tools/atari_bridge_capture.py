#!/usr/bin/env python3
"""Capture Atari screenshots/state around game start and the first bridge.

Usage: python tools/atari_bridge_capture.py [--token path] [--outdir build/atari_bridge]

Phase 1 screenshots the start of level 1 (no bridge expected there).  Phase 2
pokes the section counters ($5D countdown = 1) so the generator reaches its
bridge section, then screenshots every few frames together with the state
bytes needed to relate the picture to the generator ($23, $2F, $5A, $5D, $60,
$2E, $0B and the object slot 0..10 type/aux/x/y arrays).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from atari_river_trace import DEFAULT_XEX, ROOT, connect_any  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--token")
    ap.add_argument("--xex", type=Path, default=DEFAULT_XEX)
    ap.add_argument("--outdir", type=Path, default=ROOT / "build" / "atari_bridge")
    ap.add_argument("--start-shots", type=int, default=10)
    ap.add_argument("--bridge-shots", type=int, default=40)
    ap.add_argument("--step", type=int, default=6, help="frames between shots")
    args = ap.parse_args()
    args.outdir.mkdir(parents=True, exist_ok=True)

    a = connect_any(args.token)
    a.config("video", "ntsc")
    a.fresh()
    a.boot(str(args.xex))
    a.frame(320)
    a.clock()
    a.consol(start=True)
    a.frame(2)
    a.clock()
    a.consol()
    a.frame(4)
    a.clock()

    log = []

    def shot(tag: str) -> None:
        zp = a.memdump(0x00, 0x100)
        obj = a.memdump(0x0500, 0x40)
        (args.outdir / f"{tag}.png").write_bytes(a.screenshot())
        log.append({"tag": tag, "zp": zp.hex(), "obj": obj.hex()})

    for i in range(args.start_shots):
        a.frame(args.step * 3)
        a.clock()
        shot(f"start_{i:02d}")

    # Hold the jet centred and let the game run (no obstacles hit while the
    # lives counter is topped up).
    a.joy(0, "up")
    a.frame(30)
    a.clock()
    a.poke(0x6A, 0x63)
    a.poke(0x5D, 0x01)      # last section before the bridge
    a.poke(0x5A, 0x02)      # one boundary: bridge section, then the next level
    for i in range(args.bridge_shots):
        a.frame(args.step)
        a.clock()
        a.poke(0x6A, 0x63)
        a.poke(0x4F, 0x00)
        shot(f"bridge_{i:02d}")
    a.joy(0, "centre")
    (args.outdir / "log.json").write_text(json.dumps(log))
    a.resume()
    print(f"wrote {len(log)} shots to {args.outdir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

