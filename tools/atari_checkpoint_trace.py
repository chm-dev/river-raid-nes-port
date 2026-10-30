#!/usr/bin/env python3
"""Truth table of the $B1C0 checkpoint choice, captured from the real game.

For each combination of (level parity flipped since life start, $2E != 0,
$23 bit 7) the jet is flown into the left bank; when the game enters $B1C0 the
inputs are poked to sentinel values and the state is read back at $B1F8, the end of
the choice.  Sentinels make the source of every output byte obvious:

    $66-$69 = 11 12 13 14   (checkpoint of the current section)
    $AD-$B0 = 21 22 23 24   (previous section)
    $AA-$AC, $52 = 31 32 33 34 (live generator state, copied by $B194)

Output (JSON lines) feeds tests/host/restart_main.c via tests/host_restart.ps1.

Usage:  python tools/atari_checkpoint_trace.py --out build/checkpoint_trace.jsonl
"""

from __future__ import annotations

import argparse
import itertools
import json
import sys
import tempfile
from pathlib import Path

SDK = Path(r"F:\Emu\Atari800\Altirra\AlitrraBridge\sdk\python")
if str(SDK) not in sys.path:
    sys.path.insert(0, str(SDK))

from altirra_bridge import AltirraBridge  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
XEX = ROOT / "xex" / "River Raid (1983)(Activision)(US)[a3].xex"
BP_ENTRY = 0xB1C0
BP_DONE = 0xB1F8


def connect(token: str | None) -> AltirraBridge:
    if token:
        return AltirraBridge.from_token_file(token)
    paths = sorted(Path(tempfile.gettempdir()).glob("altirra-bridge-*.token"),
                   key=lambda p: p.stat().st_mtime, reverse=True)
    for path in paths:
        try:
            return AltirraBridge.from_token_file(str(path))
        except Exception:  # noqa: BLE001
            continue
    raise SystemExit("no usable AltirraBridge server")


def pk(a: AltirraBridge, addr: int) -> int:
    return a.peek(addr)[0]


def pc(a: AltirraBridge) -> int:
    return int(a.regs()["PC"].lstrip("$"), 16)


def run_to(a: AltirraBridge, addr: int, crash: bool, limit: int = 4000) -> bool:
    bp = a.bp_set(addr)
    if crash:
        a.joy(0, "left")                # steer into the left bank
    try:
        for _ in range(limit):
            a.cycles(2000)
            a.clock()
            if pc(a) == addr:
                return True
            if crash:
                a.poke(0x6A, 0x63)      # plenty of lives
        return False
    finally:
        a.bp_clear(bp)


def scenario(a: AltirraBridge, flip: int, e2: int, bit7: int, level: int) -> dict:
    a.config("video", "ntsc")
    a.fresh()
    a.boot(str(XEX))
    a.frame(320)
    a.clock()
    a.consol(start=True)
    a.frame(2)
    a.clock()
    a.consol()
    a.frame(4)
    a.clock()
    a.joy(0, "up")
    if not run_to(a, BP_ENTRY, crash=True):
        raise SystemExit("never reached $B1C0")

    f23 = (pk(a, 0x23) & ~0x84) | (0x80 if bit7 else 0) | (0x04 if level & 1 else 0)
    c4c = (f23 & 0x04) ^ (0x04 if flip else 0)
    inputs = {"flip": flip, "e2": e2, "bit7": bit7, "level": level,
              "c4c": c4c, "f23": f23}
    a.poke(0x60, level)
    a.poke(0x23, f23)
    a.poke(0x4C, c4c)
    a.poke(0x2E, e2)
    for i, v in enumerate((0x11, 0x12, 0x13, 0x14)):
        a.poke(0x66 + i, v)
    for i, v in enumerate((0x21, 0x22, 0x23, 0x24)):
        a.poke(0xAD + i, v)
    for addr, v in ((0xAA, 0x31), (0xAB, 0x32), (0xAC, 0x33), (0x52, 0x34)):
        a.poke(addr, v)

    if not run_to(a, BP_DONE, crash=False):
        raise SystemExit("never reached $B1F8")
    out = {
        "level": pk(a, 0x60),
        "seeds": [pk(a, 0x66 + i) for i in range(4)],
        "prev": [pk(a, 0xAD + i) for i in range(4)],
        "f23": pk(a, 0x23),
        "c4c": pk(a, 0x4C),
    }
    a.joy(0, "centre")
    return {"in": inputs, "out": out}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--token")
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "checkpoint_trace.jsonl")
    args = ap.parse_args()
    a = connect(args.token)
    rows = []
    # Odd and even levels both matter: the parity decides $23 bit 2.
    for level, flip, e2, bit7 in itertools.product((5, 6), (0, 1), (0, 1), (0, 1)):
        r = scenario(a, flip, e2, bit7, level)
        print(json.dumps(r))
        rows.append(r)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("".join(json.dumps(r) + "\n" for r in rows), encoding="utf-8")
    print(f"{len(rows)} scenarios -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
