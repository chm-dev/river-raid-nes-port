#!/usr/bin/env python3
"""Render a full 336x224 Atari frame from the captured River Raid RAM dump.

This is a validation tool: it uses only the documented display model (mode E
rows, PMG page mapping, DLI object coordinate conversion) and the extracted
ROM shapes.  Compare the output with runtime/scroll/screen_final.png.
"""

from __future__ import annotations

import json
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
RAM_PATH = ROOT / "assets" / "atari" / "runtime" / "scroll" / "ram_0000_ffff.bin"
ROM_PATH = ROOT / "assets" / "atari" / "river_raid_cart_a000_bfff.bin"
PALETTE_PATH = ROOT / "assets" / "atari" / "palette_altirra_ntsc.bin"
OUT = ROOT / "assets" / "atari" / "extracted" / "full_frame_reference.png"

W, H = 336, 224
PLAYFIELD_Y = 12
PLAYFIELD_X0 = -24  # playfield byte 0 starts here; visible bytes ~3..44
HUD_Y = 173


def main() -> int:
    ram = RAM_PATH.read_bytes()
    rom = ROM_PATH.read_bytes()
    pal = PALETTE_PATH.read_bytes()

    def rb(a: int) -> int:
        return rom[a - 0xA000]

    def p(c: int) -> tuple[int, int, int]:
        c &= 0xFE
        return tuple(pal[c * 3 : c * 3 + 3])

    im = Image.new("RGB", (W, H), (0, 0, 0))
    px = im.load()

    # --- playfield: follow the DL LMS segments -----------------------------
    addr = 0x3F00
    screen = None
    row = 0
    runs = []
    while addr < 0x3FC0:
        b = ram[addr]
        if b == 0x41:
            break
        if b in (0x4E, 0x56, 0x46, 0x5E):
            screen = ram[addr + 1] | (ram[addr + 2] << 8)
            addr += 3
            continue
        if (b & 0x0F) == 0x0E:
            runs.append((screen, b))
            screen += 48
        addr += 1
    colpf0 = ram[0x0B] & 0xFE
    colors = [0x00, colpf0, 0x20, 0x84]
    for base, _ in runs:
        if row >= 157:
            break
        data = ram[base : base + 48]
        y = PLAYFIELD_Y + row
        for bx, byte in enumerate(data):
            x0 = PLAYFIELD_X0 + bx * 8
            if x0 < 0 or x0 >= W:
                continue
            for i in range(4):
                v = (byte >> (6 - i * 2)) & 3
                c = p(colors[v])
                for k in range(2):
                    xx = x0 + i * 2 + k
                    if 0 <= xx < W:
                        px[xx, y] = c
        row += 1

    # --- PMG objects -------------------------------------------------------
    # Object shapes are bottom-up; screen Y = object_y - 75, X = 2*HPOS - 88.
    for slot in range(11):
        otype = ram[0x0500 + slot]
        if otype == 0:
            continue
        aux = ram[0x050B + slot]
        x = ram[0x0516 + slot]
        y = ram[0x0C + slot]
        if y < 0x5F or y > 0xFB:
            continue
        shape_lo = rb(0xBB40 + aux)
        yoff = rb(0xBB60 + aux)
        color_lo = rb(0xBB30 + aux)
        sx = 2 * x - 88
        sy = y - 75
        for i in range(yoff + 1):
            bits = rb(0xB900 + shape_lo + i)
            c = p(rb(0xB700 + color_lo + i))
            yy = sy + (yoff - i)  # top row first
            if not (0 <= yy < H):
                continue
            for bit in range(8):
                if bits & (0x80 >> bit):
                    for k in range(2):
                        xx = sx + bit * 2 + k
                        if 0 <= xx < W:
                            px[xx, yy] = c

    # --- player jet (P2, fixed screen position) ----------------------------
    jet_x = 2 * ram[0x57] - 88
    jet_y = 162 - 8  # PM offset $AA -> scanline $A2; screenshot row = scanline-8
    ptrs = []
    for i in range(10):
        ptrs.append(rb(0xBACD + i * 2) | (rb(0xBACE + i * 2) << 8))
    shape_index = min(9, max(0, ram[0x5E]))
    shape = rom[ptrs[shape_index] - 0xA000 : ptrs[shape_index] - 0xA000 + 14]
    for i, bits in enumerate(shape):
        yy = jet_y + i
        for bit in range(8):
            if bits & (0x80 >> bit):
                for k in range(2):
                    xx = jet_x + bit * 2 + k
                    if 0 <= xx < W and 0 <= yy < H:
                        px[xx, yy] = p(0x1E)

    # --- HUD: 3 mode-6 rows x 20 columns, 16 px per column ---------------
    font = ram[0x0600:0x0A00]
    hud_bg = p(0x06)
    for y in range(HUD_Y + 1, HUD_Y + 26):
        for x in range(W):
            px[x, y] = hud_bg
    for r in range(3):
        for c in range(20):
            code = ram[0x1000 + r * 20 + c] & 0x7F
            gx = 8 + c * 16
            gy = HUD_Y + r * 8
            for yy in range(8):
                bits = font[code * 8 + yy]
                for bit in range(8):
                    xx = gx + bit * 2
                    if 0 <= xx < W - 1 and 0 <= gy + yy < H:
                        color = p(0x0C if bits & (0x80 >> bit) else 0x06)
                        px[xx, gy + yy] = color
                        px[xx + 1, gy + yy] = color

    im.save(OUT)
    print(f"wrote {OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
