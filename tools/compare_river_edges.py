#!/usr/bin/env python3
"""Compare the authentic Atari river edges with the current NES tile atlas.

Renders the same captured 157 rows twice:

  left   exact mode-E pixels (4 px per Atari byte)
  right  the C port's 51-tile classifier: 8x8 NES tiles, then the nearest
         straight/sloped edge tile from the atlas

The comparison isolates the tile-atlas approximation from generator parity.
Run: python tools/compare_river_edges.py
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
ROWS = ROOT / "assets" / "atari" / "extracted" / "river_rows_157.bin"
PALETTE = ROOT / "assets" / "atari" / "palette_altirra_ntsc.bin"
OUT = ROOT / "build" / "river_edge_compare.png"
ZOOM = ROOT / "build" / "river_edge_zoom.png"

VISIBLE_X = 4
VISIBLE_BYTES = 40
PIXEL_W = VISIBLE_BYTES * 4  # 160
TILE_W = VISIBLE_BYTES // 2  # 20
TILE_ROWS = 19               # 152 of the 157 rows; last partial block skipped


def atari_color(value: int, pal: bytes) -> tuple[int, int, int]:
    index = {0: 0x00, 1: 0xD6, 2: 0x20, 3: 0x84}[value]
    return tuple(pal[index * 3 : index * 3 + 3])


def raw_pixel(row: bytes, x: int) -> int:
    return (row[VISIBLE_X + (x >> 2)] >> (6 - 2 * (x & 3))) & 3


def flags_for_tile(block: list[bytes], c: int) -> int:
    flags = 0
    for row in block:
        b0 = row[VISIBLE_X + c * 2]
        b1 = row[VISIBLE_X + c * 2 + 1]
        for x in range(8):
            v = ((b0 if x < 4 else b1) >> (6 - 2 * (x & 3))) & 3
            if v != 1:
                flags |= 1
            if v != 3:
                flags |= 2
    return flags


def edge_x(row: bytes, c: int) -> int:
    b0 = row[VISIBLE_X + c * 2]
    b1 = row[VISIBLE_X + c * 2 + 1]
    prev = None
    for x in range(8):
        v = ((b0 if x < 4 else b1) >> (6 - 2 * (x & 3))) & 3
        if v == 2:
            return x
        if prev is not None and ((prev == 1 and v == 3) or (prev == 3 and v == 1)):
            return x
        prev = v
    return 0xFF


def counts_for_tile(block: list[bytes], c: int) -> tuple[int, int]:
    land = water = 0
    for row in block:
        b0 = row[VISIBLE_X + c * 2]
        b1 = row[VISIBLE_X + c * 2 + 1]
        for x in range(8):
            v = ((b0 if x < 4 else b1) >> (6 - 2 * (x & 3))) & 3
            if v == 1:
                land += 1
            elif v == 3:
                water += 1
    return land, water


def classify(block: list[bytes], c: int) -> int:
    flags = flags_for_tile(block, c)
    land, water = counts_for_tile(block, c)
    if land >= 56:
        return 0
    if water >= 56:
        return 1
    if 64 - land - water >= 56:
        return 2
    if flags == 1:
        return 1
    if flags == 2:
        return 0
    first = block[0]
    b0 = first[VISIBLE_X + c * 2]
    b1 = first[VISIBLE_X + c * 2 + 1]
    if all((((b0 if x < 4 else b1) >> (6 - 2 * (x & 3))) & 3) == 2 for x in range(8)):
        return 2
    x0 = edge_x(block[0], c)
    x1 = edge_x(block[-1], c)
    if x0 == 0xFF:
        x0 = 0
    if x1 == 0xFF:
        x1 = x0
    slope = 2 if x1 > x0 else (0 if x1 < x0 else 1)
    left_v = (b0 >> 6) & 3
    right_v = b1 & 3
    # Atlas side 0 = water->edge->land, side 1 = land->edge->water.
    side = 1 if (left_v != 3 and right_v == 3) else 0
    return 3 + side * 24 + slope * 8 + min(x0, 7)


def atlas_pixel(tile: int, y: int, x: int) -> int:
    if tile == 0:
        return 1
    if tile == 1:
        return 3
    if tile == 2:
        return 2
    t = tile - 3
    side = t // 24
    rem = t % 24
    slope = rem // 8
    phase = rem % 8
    e = phase + int((slope - 1) * (y - 4) / 4)
    if side == 0:
        if x < e:
            return 3
        if x < e + 2:
            return 2
        return 1
    if x < e - 2:
        return 1
    if x < e:
        return 2
    return 3


def main() -> None:
    raw = ROWS.read_bytes()
    rows = [raw[i * 48 : (i + 1) * 48] for i in range(len(raw) // 48)]
    pal = PALETTE.read_bytes()

    atari = Image.new("RGB", (PIXEL_W, len(rows)))
    approx = Image.new("RGB", (PIXEL_W, len(rows)))
    ap = atari.load()
    pp = approx.load()

    for y, row in enumerate(rows):
        for x in range(PIXEL_W):
            ap[x, y] = atari_color(raw_pixel(row, x), pal)

    for tr in range(TILE_ROWS):
        block = rows[tr * 8 : tr * 8 + 8]
        for c in range(TILE_W):
            tile = classify(block, c)
            for ty in range(8):
                for tx in range(8):
                    pp[c * 8 + tx, tr * 8 + ty] = atari_color(
                        atlas_pixel(tile, ty, tx), pal
                    )

    # Mismatch statistics and the worst 40x40 crop.
    worst = (0, 0, -1)
    for y0 in range(0, TILE_ROWS * 8 - 40, 8):
        for x0 in range(0, PIXEL_W - 40, 8):
            bad = 0
            for y in range(y0, y0 + 40):
                for x in range(x0, x0 + 40):
                    if ap[x, y] != pp[x, y]:
                        bad += 1
            if bad > worst[2]:
                worst = (x0, y0, bad)
    total_bad = sum(
        1
        for y in range(TILE_ROWS * 8)
        for x in range(PIXEL_W)
        if ap[x, y] != pp[x, y]
    )
    total = TILE_ROWS * 8 * PIXEL_W
    print(f"pixel mismatch {total_bad}/{total} = {total_bad * 100.0 / total:.1f}%")
    print(f"worst 40x40 crop at x={worst[0]} y={worst[1]} ({worst[2]} px)")

    scale = 3
    label_h = 18
    gap = 12
    canvas = Image.new(
        "RGB",
        (PIXEL_W * 2 * scale + gap, len(rows) * scale + label_h),
        (24, 24, 24),
    )
    canvas.paste(atari.resize((PIXEL_W * scale, len(rows) * scale), Image.NEAREST), (0, label_h))
    canvas.paste(
        approx.resize((PIXEL_W * scale, len(rows) * scale), Image.NEAREST),
        (PIXEL_W * scale + gap, label_h),
    )
    d = ImageDraw.Draw(canvas)
    d.text((4, 3), "ATARI exact mode-E edges", fill=(240, 240, 240))
    d.text((PIXEL_W * scale + gap + 4, 3), "NES 51-tile edge atlas", fill=(240, 240, 240))
    canvas.save(OUT)

    # Zoom the worst region: exact | approximation, 8x.
    x0, y0, _ = worst
    za = atari.crop((x0, y0, x0 + 40, y0 + 40)).resize((320, 320), Image.NEAREST)
    zb = approx.crop((x0, y0, x0 + 40, y0 + 40)).resize((320, 320), Image.NEAREST)
    zoom = Image.new("RGB", (320 * 2 + 8, 320 + label_h), (24, 24, 24))
    zoom.paste(za, (0, label_h))
    zoom.paste(zb, (328, label_h))
    dz = ImageDraw.Draw(zoom)
    dz.text((4, 3), f"ATARI x8 (x={x0}, y={y0})", fill=(240, 240, 240))
    dz.text((332, 3), "NES x8", fill=(240, 240, 240))
    zoom.save(ZOOM)
    print(f"wrote {OUT.relative_to(ROOT)} and {ZOOM.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
