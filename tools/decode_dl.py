#!/usr/bin/env python3
"""Decode the River Raid display list from a RAM dump and summarize segments."""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_RAM = ROOT / "assets" / "atari" / "runtime" / "scroll" / "ram_0000_ffff.bin"

LMS = {0x4E, 0x56, 0x46, 0x5E, 0x4D, 0x45}


def decode(dl: bytes, base: int = 0x3F00) -> list[tuple[int, str]]:
    out: list[tuple[int, str]] = []
    i = 0
    while i < len(dl):
        b = dl[i]
        if b == 0x41:
            out.append((base + i, "JVB"))
            break
        if b in LMS:
            if i + 2 >= len(dl):
                break
            a = dl[i + 1] | (dl[i + 2] << 8)
            out.append((base + i, "LMS %02X -> $%04X" % (b, a)))
            i += 3
            continue
        mode = b & 0x0F
        if mode == 0x0E:
            out.append((base + i, "modeE"))
        elif mode == 0x06:
            out.append((base + i, "mode6"))
        elif mode == 0:
            out.append((base + i, "blank n=%d dli=%d" % ((b >> 4) + 1, 1 if b & 0x80 else 0)))
        else:
            out.append((base + i, "op %02X" % b))
        i += 1
    return out


def main() -> int:
    ram = Path(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_RAM).read_bytes()
    dl = ram[0x3F00:0x3FC0]
    print("raw:", " ".join("%02X" % b for b in dl))
    segs: list[tuple[int, int]] = []
    mode_e = 0
    for addr, text in decode(dl):
        print("%04X: %s" % (addr, text))
    # Summarize contiguous mode-E runs (LMS sets the start of a run).
    run_start = None
    run_count = 0
    for addr, text in decode(dl):
        if text == "modeE":
            if run_start is None:
                run_start = addr
            run_count += 1
        else:
            if run_start is not None:
                segs.append((run_start, run_count))
                run_start = None
                run_count = 0
    if run_start is not None:
        segs.append((run_start, run_count))
    print("mode-E runs:", segs, "total", sum(n for _, n in segs))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
