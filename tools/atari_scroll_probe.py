#!/usr/bin/env python3
"""Trace River Raid's display-list scroll state and game variables per frame.

Boots a fresh machine, optionally presses START, then samples once per frame:
  - zero page $00-$FF          (game variables)
  - object arrays $0500-$053F
  - display list $3F00-$3FBF

Usage:
    python tools/atari_scroll_probe.py --start --frames 240 \
        --outdir assets/atari/runtime/scroll
    python tools/atari_scroll_probe.py --start --frames 240 --hold left --fire \
        --outdir assets/atari/runtime/scroll_left
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

SDK = Path(r"F:\Emu\Atari800\Altirra\AlitrraBridge\sdk\python")
if str(SDK) not in sys.path:
    sys.path.insert(0, str(SDK))

from altirra_bridge import AltirraBridge  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_XEX = ROOT / "xex" / "River Raid (1983)(Activision)(US)[a3].xex"


def newest_token() -> Path:
    import tempfile

    candidates = sorted(
        Path(tempfile.gettempdir()).glob("altirra-bridge-*.token"),
        key=lambda p: p.stat().st_mtime,
    )
    if not candidates:
        raise SystemExit("no altirra-bridge-*.token found; start AltirraBridgeServer first")
    return candidates[-1]


def connect_any(token: str | None) -> AltirraBridge:
    """Use the explicit token, or try every token because stale files persist."""
    if token:
        return AltirraBridge.from_token_file(token)
    import tempfile
    import time

    last: Exception | None = None
    paths = sorted(Path(tempfile.gettempdir()).glob("altirra-bridge-*.token"), key=lambda p: p.stat().st_mtime, reverse=True)
    for _ in range(3):
        for path in paths:
            try:
                return AltirraBridge.from_token_file(str(path))
            except Exception as exc:  # noqa: BLE001 - stale/busy server
                last = exc
        time.sleep(0.5)
    raise SystemExit(f"no usable AltirraBridge server ({last})")


def barrier(a: AltirraBridge, frames: int) -> None:
    if frames > 0:
        a.frame(frames)
        a.clock()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--token")
    ap.add_argument("--xex", type=Path, default=DEFAULT_XEX)
    ap.add_argument("--outdir", type=Path, required=True)
    ap.add_argument("--frames", type=int, default=240)
    ap.add_argument("--warmup", type=int, default=320, help="frames before START")
    ap.add_argument("--start", action="store_true")
    ap.add_argument("--hold", default="", help="joystick direction to hold")
    ap.add_argument("--fire", action="store_true")
    ap.add_argument("--screenshot-every", type=int, default=60)
    args = ap.parse_args()

    a = connect_any(args.token)
    a.config("video", "ntsc")
    a.fresh()
    a.boot(str(args.xex))
    barrier(a, args.warmup)

    if args.start:
        a.consol(start=True)
        barrier(a, 2)
        a.consol()
        barrier(a, 4)

    if args.hold or args.fire:
        a.joy(0, args.hold or "centre", fire=args.fire)

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    samples = []
    try:
        for i in range(args.frames):
            barrier(a, 1)
            zp = a.memdump(0x0000, 0x100)
            obj = a.memdump(0x0500, 0x40)
            dl = a.memdump(0x3F00, 0xC0)
            samples.append(
                {
                    "frame": i,
                    "zp": zp.hex(),
                    "obj": obj.hex(),
                    "dl": dl.hex(),
                }
            )
            if args.screenshot_every and i % args.screenshot_every == 0:
                a.screenshot(path=str(outdir / f"screen_{i:04d}.png"))
    finally:
        a.joy(0, "centre", fire=False)
        a.consol()

    a.screenshot(path=str(outdir / "screen_final.png"))
    (outdir / "ram_0000_ffff.bin").write_bytes(a.memdump(0x0000, 0x10000))
    meta = {
        "timestamp": time.time(),
        "frames": args.frames,
        "start": args.start,
        "hold": args.hold,
        "fire": args.fire,
        "regs": a.regs(),
        "antic": a.antic(),
        "gtia": a.gtia(),
        "dlist": a.dlist(),
    }
    (outdir / "scroll_trace.json").write_text(
        json.dumps({"meta": meta, "samples": samples}, indent=1), encoding="utf-8"
    )
    print(f"captured {len(samples)} frames -> {outdir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
