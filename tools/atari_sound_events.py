#!/usr/bin/env python3
"""Dump what the Atari sound engine does for each individual sound trigger.

Boots the game, starts a run, then pokes one timer variable at a time and
records the POKEY state per frame for a short window.  The deferred VBI
($B2A5) reads these variables every frame, so a poke reproduces exactly what
the game code does when it fires the corresponding event.

Usage:
    python tools/atari_sound_events.py --out build/sound_events.json
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from atari_river_trace import DEFAULT_XEX, ROOT, connect_any  # noqa: E402

EVENTS = {
    "blip_c6":       (0xC6, 0x02, 8),    # START / option blip
    "shot_7d":       (0x7D, 0x0F, 22),   # fire button
    "hit_7f":        (0x7F, 0x09, 14),   # shot hit something
    "beep_7e":       (0x7E, 0x40, 70),   # refuel / low fuel
    "boom_7c_short": (0x7C, 0x17, 30),   # hit explosion
    "boom_7c_long":  (0x7C, 0x37, 64),   # crash
    "sweep_7a3":     (0x7A, 0x03, 12),   # engine sweep after a bank hit
    "sweep_7a1":     (0x7A, 0x01, 12),
}


def sample(a) -> dict:
    audio = a.audio_state()
    return {
        "ch": [
            {"audf": int(c["audf"].lstrip("$"), 16),
             "audc": int(c["audc"].lstrip("$"), 16),
             "volume": c["volume"],
             "distortion": c["distortion"],
             "clock": c["clock"],
             "freq_hz": c["freq_hz"]}
            for c in audio["channels"]
        ]
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--token")
    ap.add_argument("--xex", type=Path, default=DEFAULT_XEX)
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "sound_events.json")
    args = ap.parse_args()

    a = connect_any(args.token)
    a.config("video", "ntsc")
    a.fresh()
    a.boot(str(args.xex))
    a.frame(300)
    a.clock()
    a.consol(start=True)
    a.frame(2)
    a.clock()
    a.consol()
    a.frame(6)
    a.clock()
    a.poke(0x76, 0x00)
    a.joy(0, "up")
    a.frame(30)
    a.clock()

    out: dict[str, list] = {}
    for name, (addr, value, frames) in EVENTS.items():
        # Clear every timer first so only the event under test is running.
        for t in (0xC6, 0x50, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F):
            a.poke(t, 0x00)
        a.frame(3)
        a.clock()
        a.poke(addr, value)
        seq = []
        for i in range(frames):
            a.frame(1)
            a.clock()
            rec = sample(a)
            rec["f"] = i
            for t, nm in ((0x50, "50"), (0x7A, "7a"), (0x7B, "7b"), (0x7C, "7c"),
                          (0x7D, "7d"), (0x7E, "7e"), (0x7F, "7f"), (0xC6, "c6")):
                rec[nm] = a.peek(t)[0]
            seq.append(rec)
        out[name] = seq
        print(f"{name}: {frames} frames")

    args.out.write_text(json.dumps(out, indent=1), encoding="utf-8")
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
