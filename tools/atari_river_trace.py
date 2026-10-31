#!/usr/bin/env python3
"""Capture per-row river generator state and row bytes from a live Atari run.

The breakpoint sits at $AB80, the first instruction after the row renderer.
At that point $3F04/$3F05 hold the address of the row that was just built, so
each hit yields one (pre-step state, generated 48-byte mode-E row) sample.

Usage:
    python tools/atari_river_trace.py --rows 220 --out build/river_trace.jsonl
"""

from __future__ import annotations

import argparse
import json
import sys
import tempfile
from pathlib import Path

SDK = Path(r"F:\Emu\Atari800\Altirra\AlitrraBridge\sdk\python")
if str(SDK) not in sys.path:
    sys.path.insert(0, str(SDK))

from altirra_bridge import AltirraBridge  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_XEX = ROOT / "xex" / "River Raid (1983)(Activision)(US)[a3].xex"
BP_RENDER_DONE = 0xAB80


def newest_token() -> Path:
    candidates = sorted(
        Path(tempfile.gettempdir()).glob("altirra-bridge-*.token"),
        key=lambda p: p.stat().st_mtime,
    )
    if not candidates:
        raise SystemExit("no altirra-bridge-*.token found")
    return candidates[-1]


def connect_any(token: str | None) -> AltirraBridge:
    if token:
        return AltirraBridge.from_token_file(token)
    paths = sorted(
        Path(tempfile.gettempdir()).glob("altirra-bridge-*.token"),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    last: Exception | None = None
    for path in paths:
        try:
            return AltirraBridge.from_token_file(str(path))
        except Exception as exc:  # noqa: BLE001
            last = exc
    raise SystemExit(f"no usable AltirraBridge server ({last})")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--token")
    ap.add_argument("--xex", type=Path, default=DEFAULT_XEX)
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "river_trace.jsonl")
    ap.add_argument("--rows", type=int, default=220)
    ap.add_argument("--warmup", type=int, default=320)
    ap.add_argument("--start", action="store_true", default=True)
    ap.add_argument("--no-start", dest="start", action="store_false")
    ap.add_argument("--max-frames", type=int, default=4000)
    ap.add_argument("--select-wait", type=int, default=900, help="frames to wait after the SELECT presses")
    ap.add_argument("--select", type=int, default=0,
                    help="SELECT presses before START (game n+1; 4 = start level 20)")
    args = ap.parse_args()

    a = connect_any(args.token)
    a.config("video", "ntsc")
    a.fresh()
    a.boot(str(args.xex))
    a.frame(args.warmup)
    a.clock()

    for _ in range(args.select):
        a.consol(select=True)
        a.frame(6)
        a.clock()
        a.consol()
        a.frame(6)
        a.clock()
    if args.select:
        a.frame(args.select_wait)
        a.clock()

    if args.start:
        a.consol(start=True)
        a.frame(2)
        a.clock()
        a.consol()
        a.frame(4)
        a.clock()

    # Force a high throttle so rows are generated quickly.  $76 = 0 is the
    # maximum-speed setting in the original game.
    a.poke(0x76, 0x00)
    # Hold "up" and pin $59 near 0x80 between rows: that yields exactly one
    # generated row per frame, so no row is lost between breakpoint stops.
    a.joy(0, "up")

    bp = a.bp_set(BP_RENDER_DONE)
    print(f"breakpoint id {bp} at ${BP_RENDER_DONE:04X}")

    rows: list[dict] = []
    steps = 0
    try:
        # Advance in exact cycle chunks instead of frame gates.  A frame gate
        # can coalesce two row-generator hits into one stop; CYCLES plus the
        # PC breakpoint stops at every hit.
        while len(rows) < args.rows and steps < args.max_frames * 64:
            a.cycles(2000)
            a.clock()
            steps += 1
            regs = a.regs()
            pc = int(regs["PC"].lstrip("$"), 16)
            if pc != BP_RENDER_DONE:
                continue
            zp = a.memdump(0x0000, 0x100)
            obj = a.memdump(0x0500, 0x40)
            lms = a.memdump(0x3F04, 2)
            row_addr = lms[0] | (lms[1] << 8)
            row = a.memdump(row_addr, 48)
            rows.append(
                {
                    "n": len(rows),
                    "step": steps,
                    "row_addr": row_addr,
                    "zp": zp.hex(),
                    "obj": obj.hex(),
                    "row": row.hex(),
                }
            )
            # Keep the jet centered in the channel and alive so a crash does
            # not inject a checkpoint reset into the trace.  Clearing $4F
            # avoids hit-flash rows.
            mid = zp[0x52] + zp[0x54] + (zp[0x53] >> 1)
            a.poke(0x57, max(0, min(0xD4, mid - 4)))
            a.poke(0x6A, 0x63)
            a.poke(0x4F, 0x00)
            # Disable all object slots so no enemy/bridge collision can reset
            # the generator to a checkpoint mid-trace.
            a.memload(0x000C, bytes(11))
            a.memload(0x0042, bytes(11))
            a.memload(0x0500, bytes(0x40))
            a.poke(0x4D, 0x00)
            a.poke(0x4E, 0x00)
            # Step past the breakpoint address before the next chunk.  The
            # breakpoint must be cleared for the step or it retriggers at the
            # same PC and the row is captured twice.
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
    print(f"captured {len(rows)} rows in {steps} cycle chunks -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
