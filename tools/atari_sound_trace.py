#!/usr/bin/env python3
"""Capture the live POKEY register stream (and the sound variables) per frame.

The Atari drives four POKEY channels from the deferred VBI at $B2A5.  This probe
logs, once per frame, the four AUDC/AUDF pairs plus the zero-page variables that
gate them, and Altirra's decoded audio state, so the NES port can be tuned
against ground truth instead of guesses.

Usage:
    python tools/atari_sound_trace.py --frames 400 --out build/sound_trace.jsonl
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from atari_river_trace import DEFAULT_XEX, ROOT, connect_any  # noqa: E402

# Zero-page sound state (docs/ATARI_RE_SPEC.md section 2.1).
ZP = {
    "c6": 0xC6,   # 2-frame blip on channel 1
    "50": 0x50,   # shot animation / pitch index
    "76": 0x76,   # throttle
    "7a": 0x7A,   # engine sweep pitch increment
    "7b": 0x7B,   # engine sweep counter
    "7c": 0x7C,   # explosion timer
    "7d": 0x7D,   # shot sweep timer (channel 3)
    "7e": 0x7E,   # beep timer (channel 3)
    "7f": 0x7F,   # hit tone timer (channel 2)
    "5b": 0x5B,   # hit counter low
    "5c": 0x5C,   # hit counter high
    "19": 0x19,   # joystick
    "29": 0x29,   # mode flags
    "59": 0x59,
    "24": 0x24,
    "052c": 0x052C,
}

POKEY_REGS = {
    "audf1": 0xD200,
    "audc1": 0xD201,
    "audf2": 0xD202,
    "audc2": 0xD203,
    "audf3": 0xD204,
    "audc3": 0xD205,
    "audf4": 0xD206,
    "audc4": 0xD207,
    "audctl": 0xD208,
}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--token")
    ap.add_argument("--xex", type=Path, default=DEFAULT_XEX)
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "sound_trace.jsonl")
    ap.add_argument("--frames", type=int, default=400)
    ap.add_argument("--warmup", type=int, default=300)
    ap.add_argument("--start", action="store_true", default=True)
    ap.add_argument("--no-start", dest="start", action="store_false")
    ap.add_argument("--fire-every", type=int, default=0,
                    help="press the trigger every N frames (0 = never)")
    ap.add_argument("--audio-state", action="store_true")
    ap.add_argument("--fast", action="store_true",
                    help="one zero-page dump + AUDIO_STATE per frame")
    args = ap.parse_args()

    a = connect_any(args.token)
    a.config("video", "ntsc")
    a.fresh()
    a.boot(str(args.xex))
    a.frame(args.warmup)
    a.clock()

    if args.start:
        a.consol(start=True)
        a.frame(2)
        a.clock()
        a.consol()
        a.frame(4)
        a.clock()
        # $76 = 0 is the game's own "full throttle" value; it keeps the
        # engine sound path ($B2F9) alive and the river scrolling.
        a.poke(0x76, 0x00)
        a.joy(0, "up")

    out = args.out
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", encoding="utf-8") as fh:
        for frame in range(args.frames):
            if args.fire_every and frame % args.fire_every == 0:
                a.joy(0, "up", fire=True)
            elif args.fire_every and frame % args.fire_every == 2:
                a.joy(0, "up")
            rec: dict = {"f": frame}
            if args.fast:
                # One zero-page dump instead of a PEEK per variable.
                zp = a.memdump(0x0000, 0x80)
                for name, addr in ZP.items():
                    if addr < 0x80:
                        rec[name] = zp[addr]
                audio = a.audio_state()
                rec["audio"] = [
                    {"audf": int(c["audf"].lstrip("$"), 16),
                     "audc": int(c["audc"].lstrip("$"), 16),
                     "volume": c["volume"],
                     "distortion": c["distortion"],
                     "freq_hz": c["freq_hz"]}
                    for c in audio["channels"]
                ]
            else:
                for name, addr in POKEY_REGS.items():
                    rec[name] = a.peek(addr)[0]
                for name, addr in ZP.items():
                    rec[name] = a.peek(addr)[0]
            if args.audio_state and not args.fast:
                rec["audio"] = a.audio_state()
            fh.write(json.dumps(rec) + "\n")
            a.frame(1)
            a.clock()
    a.joy(0, "none")
    print(f"wrote {out} ({args.frames} frames)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
