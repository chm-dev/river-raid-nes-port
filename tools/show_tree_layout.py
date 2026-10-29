#!/usr/bin/env python3
"""Show how the Atari tree is assembled from per-scanline template fragments.

The captured river contains a feature at rows 11..32: an 8-byte (32 px) wide
template copied at row byte offset 4.  This script renders:

  left   the exact captured pixels with the NES 8x8 tile grid
  right  the same blocks rebuilt from the 82 captured tiles (current ROM)

Mismatched tiles are outlined in red and labelled with their pixel error.
Run: python tools/show_tree_layout.py
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
ROWS = ROOT / "assets" / "atari" / "extracted" / "river_rows_157.bin"
PALETTE = ROOT / "assets" / "atari" / "palette_altirra_ntsc.bin"
OUT = ROOT / "build" / "atari_tree_layout.png"

VIS = 4
W = 160
Y0, Y1 = 4, 44          # capture rows (6 tile rows)
X0, X1 = 0, 64          # visible pixels (8 tile columns)

COLORS = {0: 0x00, 1: 0xD6, 2: 0x20, 3: 0x84}


def tiledata(rows, tr, c):
    return tuple(
        (rows[tr * 8 + y][VIS + c * 2 + (x >> 2)] >> (6 - 2 * (x & 3))) & 3
        for y in range(8)
        for x in range(8)
    )


def main() -> None:
    raw = ROWS.read_bytes()
    rows = [raw[i * 48 : (i + 1) * 48] for i in range(len(raw) // 48)]
    pal = PALETTE.read_bytes()

    def rgb(v: int):
        c = COLORS[v] * 3
        return tuple(pal[c : c + 3])

    # Reference atlas: the 82 distinct blocks in the capture.
    atlas = []
    seen = set()
    for tr in range(19):
        for c in range(20):
            t = tiledata(rows, tr, c)
            if t not in seen:
                seen.add(t)
                atlas.append(t)

    exact = Image.new("RGB", (X1 - X0, Y1 - Y0))
    approx = Image.new("RGB", (X1 - X0, Y1 - Y0))
    ep = exact.load()
    ap = approx.load()
    for y in range(Y0, Y1):
        for x in range(X0, X1):
            v = (rows[y][VIS + (x >> 2)] >> (6 - 2 * (x & 3))) & 3
            ep[x - X0, y - Y0] = rgb(v)

    errors = {}
    for tr in range(Y0 // 8, Y1 // 8):
        for c in range(X0 // 8, X1 // 8):
            t = tiledata(rows, tr, c)
            best = min(atlas, key=lambda a: sum(1 for i in range(64) if a[i] != t[i]))
            d = sum(1 for i in range(64) if best[i] != t[i])
            errors[(c, tr)] = d
            for y in range(8):
                for x in range(8):
                    ap[c * 8 + x - X0, tr * 8 + y - Y0] = rgb(best[y * 8 + x])

    scale = 8
    label = 18
    grid_w = (X1 - X0) * scale
    grid_h = (Y1 - Y0) * scale
    canvas = Image.new("RGB", (grid_w * 2 + 12, grid_h + label * 2), (24, 24, 24))
    canvas.paste(exact.resize((grid_w, grid_h), Image.NEAREST), (0, label))
    canvas.paste(approx.resize((grid_w, grid_h), Image.NEAREST), (grid_w + 12, label))
    d = ImageDraw.Draw(canvas)
    d.text((4, 3), "ATARI exact (8x8 grid)", fill=(240, 240, 240))
    d.text((grid_w + 16, 3), "NES nearest of 82 tiles", fill=(240, 240, 240))

    for panel in (0, 1):
        ox = panel * (grid_w + 12)
        for c in range(X0 // 8, X1 // 8):
            for tr in range(Y0 // 8, Y1 // 8):
                x = ox + (c * 8 - X0) * scale
                y = label + (tr * 8 - Y0) * scale
                err = errors[(c, tr)]
                color = (255, 40, 40) if err else (80, 80, 80)
                d.rectangle([x, y, x + 8 * scale, y + 8 * scale], outline=color)
                if panel == 1 and err:
                    d.text((x + 2, y + 2), f"{err}", fill=(255, 80, 80))

    d.text((4, label + grid_h + 2),
           "tree template: bc=8 bytes (32 px) x be=22 rows at row byte offset 4",
           fill=(220, 220, 220))
    canvas.save(OUT)
    print(f"wrote {OUT.relative_to(ROOT)}")
    print("tile errors (col,row):", {k: v for k, v in errors.items() if v})


if __name__ == "__main__":
    main()
