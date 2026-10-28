#!/usr/bin/env python3
"""DEPRECATED: early mode-14 line-height probe; see pmg_line_probe.py."""
from __future__ import annotations

import sys
from pathlib import Path

SDK = Path(r"F:\Emu\Atari800\Altirra\AlitrraBridge\sdk\python")
if str(SDK) not in sys.path:
    sys.path.insert(0, str(SDK))

from altirra_bridge import AltirraBridge

OUT = Path(r"C:\Users\chm2\AppData\Local\Temp\opencode\mode14_probe2.png")


def main() -> int:
    import tempfile

    token = sorted(
        Path(tempfile.gettempdir()).glob("altirra-bridge-*.token"),
        key=lambda p: p.stat().st_mtime,
    )[-1]
    a = AltirraBridge.from_token_file(str(token))
    a.config("video", "ntsc")
    a.boot_bare()

    # First 320 bytes = colour 3, everything else colour 0.
    screen = bytearray(0x2000)
    for i in range(320):
        screen[i] = 0xFF
    a.memload(0x2000, bytes(screen))

    # 8 blank, one LMS line, 8 mode lines, JVB.
    dl = bytes([0x70, 0x4E, 0x00, 0x20] + [0x0E] * 8 + [0x01, 0x00, 0x3F])
    a.memload(0x3F00, dl)

    for reg, val in (
        (0xD40E, 0x00),
        (0xD400, 0x00),
        (0xD402, 0x00),
        (0xD403, 0x3F),
        (0xD409, 0x00),
        (0xD016, 0x00),
        (0xD017, 0x44),
        (0xD018, 0x94),
        (0xD019, 0xC4),
        (0xD01A, 0x00),
        (0xD400, 0x3E),
    ):
        a.hwpoke(reg, val)

    print("antic after setup:", a.antic())
    a.frame(1)
    a.clock()
    print("antic after frame:", a.antic())
    a.pause()
    a.screenshot(path=str(OUT))
    print(f"wrote {OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
