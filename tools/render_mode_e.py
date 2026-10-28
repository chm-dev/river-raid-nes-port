#!/usr/bin/env python3
"""Render the ANTIC mode-E (GRAPHICS 7 style) playfield from an Atari RAM dump.

Geometry verified against Altirra runtime captures:

* Display list at ``$3F00``: ``70 F0 30`` (20 blank lines), then 160 mode-E
  (``$0E`` / ``$4E``) entries, then the text HUD rows.
* A mode-E scanline displays 4 pixels per byte.  The game runs ANTIC with
  ``DMACTL=$3F`` (wide playfield), so all 48 bytes of a row are visible
  (192 pixels).
* Rows are ``$30`` (48) bytes apart.  The ring lives in ``$2000-$2EFF`` and
  ``$3000-$3EFF``; page ``$2F`` is skipped and ``$3EFF`` wraps to ``$2000``.
* 2-bit pixel values map to ANTIC colours: 0 = COLBK, 1 = COLPF0,
  2 = COLPF1, 3 = COLPF2.

Usage::

    python tools/render_mode_e.py assets/atari/runtime/attract/ram_0000_ffff.bin \
        --out assets/atari/runtime/attract/playfield.png

The default colour bytes come from the River Raid DLI/VBI register writes
(``$D016-$D01A``) and were cross-checked against an Altirra screenshot.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


def atari_rgb(value: int) -> tuple[int, int, int]:
    """Approximate the Altirra NTSC palette entry for one GTIA colour byte."""
    import colorsys

    hue, lum = value >> 4, value & 0x0F
    if hue == 0:
        # Altirra renders hue 0 as a neutral grey ramp.
        v = int(round(lum * 255 / 15.0))
        return v, v, v
    h = (hue * 360.0 / 16.0 + 20.0) % 360.0
    s = 0.0 if lum == 0 else 0.72
    v = lum / 15.0
    r, g, b = colorsys.hsv_to_rgb(h / 360.0, s, v)
    return int(r * 255), int(g * 255), int(b * 255)


def load_palette(path: Path) -> list[tuple[int, int, int]]:
    """Load a 768-byte Atari800/Altirra palette export."""
    data = path.read_bytes()
    if len(data) != 768:
        raise SystemExit(f"{path}: expected 768 bytes, got {len(data)}")
    return [(data[i * 3], data[i * 3 + 1], data[i * 3 + 2]) for i in range(256)]


def next_row(addr: int) -> int:
    addr += 0x30
    if addr == 0x2F00:
        addr = 0x3000
    elif addr == 0x3F00:
        addr = 0x2000
    return addr


def first_playfield_addr(ram: bytes, dl_base: int = 0x3F00) -> int:
    """Walk the display list and return the first mode-E LMS address."""
    i = dl_base
    while i < len(ram):
        b = ram[i]
        if b == 0:
            break
        mode = b & 0x0F
        if mode == 0:
            i += 1
            continue
        addr = None
        if b & 0x40:
            addr = ram[i + 1] | (ram[i + 2] << 8)
            i += 3
        else:
            i += 1
        if mode == 0x0E and addr is not None:
            return addr
    raise SystemExit("no mode-E LMS entry found in display list")


def render(
    ram: bytes,
    start: int,
    rows: int,
    colors: list[int],
    scale: int,
    width_bytes: int = 48,
    palette: list[tuple[int, int, int]] | None = None,
) -> Image.Image:
    rgb = [palette[c] if palette else atari_rgb(c) for c in colors]
    img = Image.new("RGB", (width_bytes * 4 * scale, rows * scale))
    px = img.load()
    addr = start
    for row in range(rows):
        base = addr
        for byte_i in range(width_bytes):
            b = ram[base + byte_i]
            for pix in range(4):
                c = (b >> (6 - pix * 2)) & 3
                x = (byte_i * 4 + pix) * scale
                for sx in range(scale):
                    for sy in range(scale):
                        px[x + sx, row * scale + sy] = rgb[c]
        addr = next_row(addr)
    return img


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("ram", type=Path, help="64 KiB RAM dump (memdump 0x0000 0x10000)")
    ap.add_argument("--start", type=lambda s: int(s, 0), default=None,
                    help="first playfield row address (default: parse DL at $3F00)")
    ap.add_argument("--rows", type=int, default=160)
    ap.add_argument("--width-bytes", type=int, default=48,
                    help="bytes per row to render (48 = wide playfield, 40 = normal)")
    ap.add_argument("--colors", default="00,D2,20,84",
                    help="COLBK,COLPF0,COLPF1,COLPF2 as GTIA hue/lum bytes")
    ap.add_argument("--palette", type=Path, default=None,
                    help="768-byte Altirra palette export (default: built-in approximation)")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--scale", type=int, default=2)
    args = ap.parse_args()

    ram = args.ram.read_bytes()
    start = args.start if args.start is not None else first_playfield_addr(ram)
    colors = [int(c, 16) for c in args.colors.split(",")]
    if len(colors) != 4:
        raise SystemExit("--colors needs exactly four hue/lum bytes")
    palette = load_palette(args.palette) if args.palette else None
    img = render(ram, start, args.rows, colors, args.scale, args.width_bytes, palette)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    img.save(args.out)
    print(f"wrote {args.out} ({img.width}x{img.height}) start=${start:04X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
