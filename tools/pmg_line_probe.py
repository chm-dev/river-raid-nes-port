#!/usr/bin/env python3
"""Calibrate Altirra screenshot row <-> ANTIC scanline using a marker DL line."""
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

# Put one white mode-E line after 20 blank lines, then 40 blanks.
DL = bytes([0x70, 0xF0, 0x30, 0x4E, 0x00, 0x50]) + bytes([0x70] * 5 + [0x41, 0x00, 0x00])
DATA = bytes([0x55] * 48 * 2)


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    a = AltirraBridge.from_token_file(str(TOKEN))
    a.config("video", "ntsc")
    a.fresh()
    a.boot_bare()
    a.memload(0x4000, DL)
    a.memload(0x5000, DATA)
    a.hwpoke(0xD402, 0x00)
    a.hwpoke(0xD403, 0x40)
    a.hwpoke(0xD400, 0x3F)
    a.hwpoke(0xD40E, 0x00)
    a.hwpoke(0xD01A, 0x00)
    a.hwpoke(0xD016, 0x0F)
    a.hwpoke(0xD017, 0x00)
    a.hwpoke(0xD018, 0x00)
    a.hwpoke(0xD019, 0x00)
    a.frame(1)
    a.clock()
    png = a.screenshot()
    (OUT / "marker.png").write_bytes(png)
    print(json.dumps(a.antic()))
    print(json.dumps(a.dlist()))
    from PIL import Image

    im = Image.open(OUT / "marker.png").convert("RGB")
    w, h = im.size
    for y in range(h):
        xs = [x for x in range(w) if im.getpixel((x, y)) != (0, 0, 0)]
        if xs:
            print("row", y, "x", min(xs), max(xs), im.getpixel((xs[0], y)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
