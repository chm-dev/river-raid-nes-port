#!/usr/bin/env python3
"""Render clean, authoritative sprite sheets from the extracted JSON/binary data.

The previous ``objects.png`` mixed the nine real P3 object sprites (aux 7-15)
with aux 0-6, whose colour pointers intentionally do not point at colour data.
This script renders the real sprites at GTIA aspect ratio (2 screen pixels per
shape bit), on a neutral checkerboard, with readable labels and per-row colour
swatches.

Inputs are the extracted authoritative files:
  sprites/objects.json / objects.bin
  sprites/player.json  / player.bin
  palette_altirra_ntsc.bin

Outputs:
  sprites/objects.png       aux 7-15, 3 x 3 grid (the sprites the DLI draws)
  sprites/objects_all.png   aux 0-15, 4 x 4 grid, data-coloured entries marked
  sprites/player.png        all 10 player/throttle shapes, 2px per bit
"""

from __future__ import annotations

import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
EX = ROOT / "assets" / "atari" / "extracted"
SPR = EX / "sprites"
PALETTE = (ROOT / "assets" / "atari" / "palette_altirra_ntsc.bin").read_bytes()

BG_A = (46, 46, 52)
BG_B = (62, 62, 70)
INK = (235, 235, 245)
DIM = (150, 155, 170)
WARN = (255, 110, 110)
GOOD = (120, 230, 150)

# 2 screen pixels per PMG bit; 8 bits = 16 px wide at 1x.
BIT_W = 2
SCALE = 7
SPRITE_W = 8 * BIT_W * SCALE
ROW_H = SCALE
MAX_ROWS = 24
CELL_W = SPRITE_W + 250
CELL_H = MAX_ROWS * ROW_H + 58


def font(size: int) -> ImageFont.ImageFont:
    for name in ("consola.ttf", "Consolas.ttf", "segoeui.ttf"):
        path = Path(r"C:\Windows\Fonts") / name
        if path.exists():
            try:
                return ImageFont.truetype(str(path), size)
            except OSError:
                pass
    return ImageFont.load_default()


F_TITLE = font(20)
F_LABEL = font(14)
F_SMALL = font(11)


def pal(index: int) -> tuple[int, int, int]:
    c = index & 0xFE
    return tuple(PALETTE[c * 3 : c * 3 + 3])  # type: ignore[return-value]


def checkerboard(w: int, h: int) -> Image.Image:
    im = Image.new("RGB", (w, h), BG_A)
    p = im.load()
    for y in range(h):
        for x in range(w):
            if ((x // 8) + (y // 8)) % 2:
                p[x, y] = BG_B
    return im


def draw_sprite(
    sheet: Image.Image,
    ox: int,
    oy: int,
    shape_bottom_up: list[int],
    colors_bottom_up: list[int],
    *,
    color_is_data: bool = False,
) -> None:
    """Draw one sprite top-down at 2px/bit, with row-colour swatches."""
    rows = list(reversed(shape_bottom_up))
    colors = list(reversed(colors_bottom_up))
    d = ImageDraw.Draw(sheet)

    # sprite area background
    box = (ox, oy, ox + SPRITE_W - 1, oy + len(rows) * ROW_H - 1)
    sheet.paste(checkerboard(SPRITE_W, len(rows) * ROW_H), (ox, oy))
    d.rectangle(box, outline=(90, 90, 105))

    for r, bits in enumerate(rows):
        color = pal(colors[r]) if r < len(colors) else (255, 255, 255)
        y = oy + r * ROW_H
        # per-row colour swatch
        sw_x = ox + SPRITE_W + 10
        d.rectangle([sw_x, y, sw_x + 22, y + ROW_H - 1], fill=color, outline=(20, 20, 20))
        for bit in range(8):
            if bits & (0x80 >> bit):
                x = ox + bit * BIT_W * SCALE
                d.rectangle([x, y, x + BIT_W * SCALE - 1, y + ROW_H - 1], fill=color)

    if color_is_data:
        d.text((ox + 4, oy + len(rows) * ROW_H + 4), "colour pointer is code/data", font=F_SMALL, fill=WARN)


def render_objects(aux_list: list[int], cols: int, path: Path, title: str, note: str) -> None:
    objs = json.loads((SPR / "objects.json").read_text(encoding="utf-8"))
    rows = (len(aux_list) + cols - 1) // cols
    w = cols * CELL_W + 20
    h = 70 + rows * CELL_H
    im = Image.new("RGB", (w, h), (24, 24, 28))
    d = ImageDraw.Draw(im)
    d.text((20, 16), title, font=F_TITLE, fill=INK)
    d.text((20, 44), note, font=F_SMALL, fill=DIM)

    for i, aux in enumerate(aux_list):
        s = objs[aux]
        cx = 20 + (i % cols) * CELL_W
        cy = 70 + (i // cols) * CELL_H
        rows_n = s["shape_len"]
        d.text((cx, cy), f"aux {aux:02X}   {rows_n} rows", font=F_LABEL, fill=GOOD if not s["color_is_data"] else WARN)
        d.text((cx, cy + 18), f"shape ${s['shape_addr']:04X}   colour ${s['color_addr']:04X}", font=F_SMALL, fill=DIM)
        if s["color_is_data"]:
            d.rectangle([cx, cy + 40, cx + SPRITE_W, cy + 40 + rows_n * ROW_H], outline=WARN, width=2)
            d.text((cx + 8, cy + 50), "NOT a P3 sprite", font=F_LABEL, fill=WARN)
            d.text((cx + 8, cy + 68), "colour ptr: code/data", font=F_SMALL, fill=WARN)
        else:
            draw_sprite(
                im,
                cx,
                cy + 40,
                s["shape_bottom_up"],
                s["colors_bottom_up"],
                color_is_data=False,
            )
    im.save(path)
    print(f"wrote {path} ({im.size[0]}x{im.size[1]})")


def render_players() -> None:
    data = json.loads((SPR / "player.json").read_text(encoding="utf-8"))
    shapes = data["shapes"]

    # The usable flight shapes are $5E = 7 (bank left), 8 (bank right),
    # 9 (straight).  Indices 0-6 point at blank or shared enemy/effect data
    # and are not normal flight frames.
    flight = [7, 8, 9]
    cols = 3
    cw, ch = SPRITE_W + 120, 14 * ROW_H + 60
    im = Image.new("RGB", (cols * cw + 20, 70 + ch), (24, 24, 28))
    d = ImageDraw.Draw(im)
    d.text((20, 16), "River Raid player jet - flight shapes ($5E = 7/8/9)", font=F_TITLE, fill=INK)
    d.text((20, 44), "14 rows, top-down, COLPM2=$1E.  2 screen px per bit.", font=F_SMALL, fill=DIM)
    names = {7: "bank left", 8: "bank right", 9: "straight"}
    for i, idx in enumerate(flight):
        ox = 20 + i * cw
        oy = 70
        d.text((ox, oy), f"idx {idx}  -  {names[idx]}", font=F_LABEL, fill=GOOD)
        draw_sprite(im, ox, oy + 22, list(reversed(shapes[idx])), [0x1E] * 14)
    d.text((20, 70 + ch - 24), "Indices 0-6 are blank or shared effect/enemy fragments - not normal flight.", font=F_SMALL, fill=DIM)
    im.save(SPR / "player.png")
    print(f"wrote {SPR / 'player.png'} ({im.size[0]}x{im.size[1]})")

    # Complete 10-entry sheet, for provenance.
    cols = 5
    cw, ch = SPRITE_W + 30, 14 * ROW_H + 58
    rows = (len(shapes) + cols - 1) // cols
    im = Image.new("RGB", (cols * cw + 20, 70 + rows * ch), (24, 24, 28))
    d = ImageDraw.Draw(im)
    d.text((20, 16), "All player-shape table entries (provenance)", font=F_TITLE, fill=INK)
    d.text((20, 44), "Only 7/8/9 are flight frames; 0/1 are blank and 2-6 reuse enemy shapes.", font=F_SMALL, fill=DIM)
    for i, shape in enumerate(shapes):
        ox = 20 + (i % cols) * cw
        oy = 70 + (i // cols) * ch
        d.text((ox, oy), f"idx {i}  ({'flight' if i >= 7 else 'blank/shared'})", font=F_LABEL, fill=GOOD if i >= 7 else WARN)
        draw_sprite(im, ox, oy + 22, list(reversed(shape)), [0x1E] * 14)
    im.save(SPR / "player_all.png")
    print(f"wrote {SPR / 'player_all.png'} ({im.size[0]}x{im.size[1]})")


def main() -> int:
    render_objects(
        list(range(7, 16)),
        3,
        SPR / "objects.png",
        "P3 object sprites (aux 7-15) - ROM $B900 / $B700",
        "Bottom-up shape rows reversed to top-down; 2 screen px per bit; swatch = per-scanline COLPM3.",
    )
    render_objects(
        list(range(0, 16)),
        4,
        SPR / "objects_all.png",
        "All 16 aux entries - only 7-15 are P3 sprites",
        "Red = colour pointer is code/data (aux 0-6); those entries are shared/unused, not drawable P3 sprites.",
    )
    render_players()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
