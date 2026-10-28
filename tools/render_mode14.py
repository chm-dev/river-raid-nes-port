#!/usr/bin/env python3
"""DEPRECATED: render_mode_e.py supersedes this.

This script assumed ANTIC mode 14 with an 8-line row and a 320-byte stride.
The cartridge actually uses mode E with 48-byte rows; see
docs/ATARI_RE_SPEC.md and tools/render_mode_e.py.
"""
from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image

# Atari NTSC palette, hue 0..15, luminance 0..15 (Altirra default).
NTSC = [
    0x000000, 0x111111, 0x222222, 0x333333, 0x444444, 0x555555, 0x666666, 0x777777,
    0x888888, 0x999999, 0xAAAAAA, 0xBBBBBB, 0xCCCCCC, 0xDDDDDD, 0xEEEEEE, 0xFFFFFF,
]


def atari_color(hue: int, lum: int) -> tuple[int, int, int]:
    # Approximation used only for visual comparison with Altirra screenshots.
    import colorsys

    h = (hue * 360.0 / 16.0 + 20.0) % 360.0
    v = lum / 15.0
    s = 0.0 if lum == 0 else 0.72
    r, g, b = colorsys.hsv_to_rgb(h / 360.0, s, v)
    return int(r * 255), int(g * 255), int(b * 255)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("ram", type=Path)
    ap.add_argument("--start", type=lambda s: int(s, 0), default=0x2C90)
    ap.add_argument("--rows", type=int, default=24)
    ap.add_argument("--stride", type=lambda s: int(s, 0), default=0x30)
    ap.add_argument("--colors", default="0C,8E,1E,00", help="COLPF0..3 as hue/lum bytes")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--scale", type=int, default=2)
    args = ap.parse_args()

    ram = args.ram.read_bytes()
    cols = []
    for c in args.colors.split(","):
        v = int(c, 16)
        cols.append(atari_color(v >> 4, v & 0x0F))

    img = Image.new("RGB", (160 * args.scale, args.rows * 8 * args.scale))
    px = img.load()
    for row in range(args.rows):
        base = args.start + row * args.stride
        for line in range(8):
            addr = base + line * 40
            for byte_i in range(40):
                b = ram[addr + byte_i]
                for pix in range(4):
                    c = (b >> (6 - pix * 2)) & 3
                    x = (byte_i * 4 + pix) * args.scale
                    y = (row * 8 + line) * args.scale
                    for sy in range(args.scale):
                        for sx in range(args.scale):
                            px[x + sx, y + sy] = cols[c]
    img.save(args.out)
    print(f"wrote {args.out} ({img.width}x{img.height})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
