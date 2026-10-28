#!/usr/bin/env python3
"""Locate where HUD character codes are displayed by patching one glyph."""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

SDK = Path(r"F:\Emu\Atari800\Altirra\AlitrraBridge\sdk\python")
if str(SDK) not in sys.path:
    sys.path.insert(0, str(SDK))

from altirra_bridge import AltirraBridge  # noqa: E402
from PIL import Image  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
XEX = ROOT / "xex" / "River Raid (1983)(Activision)(US)[a3].xex"
TOKEN = Path(tempfile.gettempdir()) / "altirra-bridge-12200.token"
OUT = Path(r"C:\Users\chm2\AppData\Local\Temp\opencode\hudmap.png")


def main() -> int:
    a = AltirraBridge.from_token_file(str(TOKEN))
    a.config("video", "ntsc")
    a.fresh()
    a.boot(str(XEX))
    a.frame(340)
    a.clock()
    with a.checkpoint():
        # code E7 -> glyph index 0x67 at $0600 + 0x67*8 = $0938
        a.memload(0x0938, bytes([0xFF] * 8))
        # code CB -> glyph index 0x4B at $0600 + 0x4B*8 = $0858
        a.memload(0x0858, bytes([0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81]))
        a.frame(2)
        a.clock()
        png = a.screenshot()
        OUT.write_bytes(png)
    im = Image.open(OUT).convert("RGB")
    w, h = im.size
    for y in range(160, h):
        xs = [x for x in range(w) if im.getpixel((x, y)) == (255, 255, 255)]
        if xs:
            groups = []
            s = xs[0]
            p = xs[0]
            for x in xs[1:]:
                if x == p + 1:
                    p = x
                else:
                    groups.append((s, p))
                    s = p = x
            groups.append((s, p))
            print(y, groups)
    a.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
