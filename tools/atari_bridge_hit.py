#!/usr/bin/env python3
"""Capture the Atari bridge being shot, with or without the tank on it.

Usage: python tools/atari_bridge_hit.py [--tank-over] [--bridges N] [--outdir D]

Drives a real game: forces the bridge section ($5D=1, $5A=2), waits for the
$0E object, parks the jet under it by poking X ($57), fires, and then saves a
screenshot and state (flash timer $31, tank $38/$39/$3A, shell $50, object
types) for every frame of the hit.  --tank-over pokes the tank's X ($39) under
the bridge so the $A4EA bonus path runs.
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
    ap.add_argument("--outdir", type=Path, default=ROOT / "build" / "atari_hit")
    ap.add_argument("--tank-over", action="store_true")
    ap.add_argument("--bridges", type=int, default=1, help="poke the bridge counter ($62) to this")
    ap.add_argument("--frames", type=int, default=50)
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
    a.joy(0, "up")                                  # keep scrolling (the start hold needs input)
    a.frame(120)
    a.clock()
    a.poke(0x6A, 0x63)
    a.poke(0x62, args.bridges)
    a.poke(0x5D, 0x01)
    a.poke(0x5A, 0x02)

    def state():
        zp = a.memdump(0x00, 0x100)
        ob = a.memdump(0x0500, 0x40)
        return zp, ob

    def force_hit(slot):
        """Stop at the collision handler ($A3E9) and report the shot hitting the bridge ($1C)."""
        bp = a.bp_set(0xA3E9)
        try:
            for _ in range(200):
                a.cycles(2000)
                a.clock()
                if int(a.regs()["PC"].lstrip("$"), 16) == 0xA3E9:
                    a.poke(0x1C, slot)
                    a.bp_clear(bp)
                    bp = None
                    while True:                           # step off the breakpoint address
                        a.cycles(64)
                        a.clock()
                        if int(a.regs()["PC"].lstrip("$"), 16) != 0xA3E9:
                            return True
        finally:
            if bp is not None:
                a.bp_clear(bp)
        return False

    log = []
    seen = False
    fired = False
    hit_at = None
    by = bx = 0
    for step in range(500):
        a.frame(1)
        a.clock()
        zp, ob = state()
        a.poke(0x6A, 0x63)
        # The slots are renumbered as objects come and go: find the bridge each time.
        slot = next((i for i in range(11) if ob[i] == 0x0E), None)
        if slot is not None:
            seen = True
            by = zp[0x0C + slot]
            bx = ob[0x16 + slot]
        if not seen:
            continue
        if hit_at is None and zp[0x31] != 0:
            hit_at = step                                # $A4EE: the flash timer starts
        if hit_at is None and slot is not None:
            if args.tank_over and zp[0x38] in (4, 5, 6, 7):
                a.poke(0x39, bx + 8)                      # tank on the bridge
            if not fired and by >= 0x90:
                fired = force_hit(slot)
        if hit_at is not None or fired:
            (args.outdir / f"f{step:03d}.png").write_bytes(a.screenshot())
            log.append({
                "step": step, "hit": hit_at is not None, "by": by, "bx": bx,
                "t31": zp[0x31], "s38": zp[0x38], "s39": zp[0x39], "s3a": zp[0x3A],
                "s50": zp[0x50], "slot": slot, "types": ob[:11].hex(),
                "score": zp[0x63:0x66].hex(), "bridge": zp[0x61:0x63].hex(),
            })
        if hit_at is not None and step - hit_at > args.frames:
            break
    a.joy(0, "centre")
    (args.outdir / "log.json").write_text(json.dumps(log, indent=1))
    a.resume()
    print(f"saved {len(log)} frames to {args.outdir}; hit at step {hit_at}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
