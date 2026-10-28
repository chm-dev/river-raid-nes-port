#!/usr/bin/env python3
"""Second PMG mapping probe: identify page→player mapping and HPOSP x mapping."""
from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

SDK = Path(r"F:\Emu\Atari800\Altirra\AlitrraBridge\sdk\python")
if str(SDK) not in sys.path:
    sys.path.insert(0, str(SDK))

from altirra_bridge import AltirraBridge  # noqa: E402

OUT = Path(r"C:\Users\chm2\AppData\Local\Temp\opencode\pmgprobe2")
TOKEN = Path(tempfile.gettempdir()) / "altirra-bridge-12200.token"


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    a = AltirraBridge.from_token_file(str(TOKEN))
    a.config("video", "ntsc")
    a.fresh()
    a.boot_bare()
    # 240 blank lines then JVB (3 bytes).
    a.memload(0x4000, bytes([0x70] * 30 + [0x41, 0x00, 0x00]))
    a.hwpoke(0xD402, 0x00)
    a.hwpoke(0xD403, 0x40)
    a.hwpoke(0xD407, 0x08)  # PMBASE = $0800
    a.hwpoke(0xD01D, 0x03)  # GRACTL players+missiles
    a.hwpoke(0xD400, 0x3F)
    a.hwpoke(0xD40E, 0x00)  # NMIEN
    a.hwpoke(0xD01B, 0x00)  # PRIOR normal
    a.hwpoke(0xD01A, 0x00)
    a.hwpoke(0xD016, 0x0F)
    a.hwpoke(0xD017, 0x0F)
    a.hwpoke(0xD018, 0x0F)
    a.hwpoke(0xD019, 0x0F)
    # Distinct player colours and positions.
    for i, (hpos, col) in enumerate([(60, 0x0F), (80, 0x2A), (100, 0x46), (120, 0x84)]):
        a.hwpoke(0xD000 + i, hpos)
        a.hwpoke(0xD012 + i, col)
    for i in range(4):
        a.hwpoke(0xD008 + i, 100 + i * 10)
    a.hwpoke(0xD00B, 0x00)  # SIZEM = 0 (one missile bit wide)
    # Enabling missile DMA with SIZEM=0; give missiles data too.
    a.memload(0x0800, bytes(0x800))
    for addr in (0x0C32, 0x0D32, 0x0E32, 0x0F32):
        a.poke(addr, 0xFF)
    a.frame(1)
    a.clock()
    png = a.screenshot()
    (OUT / "four.png").write_bytes(png)
    print("antic", json.dumps(a.antic()))
    print("gtia", json.dumps(a.gtia()))
    from PIL import Image
    from collections import Counter

    im = Image.open(OUT / "four.png").convert("RGB")
    w, h = im.size
    pts = {}
    for y in range(h):
        for x in range(w):
            c = im.getpixel((x, y))
            if c != (0, 0, 0):
                pts.setdefault(c, []).append((x, y))
    for c, xy in sorted(pts.items()):
        xs = [p[0] for p in xy]
        ys = [p[1] for p in xy]
        print(c, "n", len(xy), "x", min(xs), max(xs), "y", min(ys), max(ys))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
