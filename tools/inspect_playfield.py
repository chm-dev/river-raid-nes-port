#!/usr/bin/env python3
"""Reconstruct the NES playfield from NT1+CHR and compare with a screenshot.

Compares by pixel *value* (0..3), inferring the screenshot's RGB palette from
the most common colour at each reconstructed value, so a palette mismatch
does not hide a layout error.
"""

from __future__ import annotations

import sys
from collections import Counter
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SHOT = ROOT / "build" / "flip_check.png"
NT1 = ROOT / "build" / "nt1.bin"
CHR = ROOT / "build" / "chr.bin"
SCROLL = int(sys.argv[1]) if len(sys.argv) > 1 else 235

PLAY_TOP = 37
PLAY_H = 240 - PLAY_TOP
W = 256


def recon_values(nt: bytes, chr_data: bytes, s: int) -> list[list[int]]:
    out = [[0] * W for _ in range(PLAY_H)]
    for y in range(PLAY_H):
        coord = (s + y) % 240
        row = coord >> 3
        py = coord & 7
        for x in range(W):
            tile = nt[row * 32 + (x >> 3)]
            p = chr_data[tile * 16 : tile * 16 + 16]
            bit = 7 - (x & 7)
            out[y][x] = (((p[8 + py] >> bit) & 1) << 1) | ((p[py] >> bit) & 1)
    return out


def main() -> None:
    nt = NT1.read_bytes()
    chr_data = CHR.read_bytes()
    im = Image.open(SHOT).convert("RGB")
    pix = im.load()

    # Try a range of vertical offsets around the expected scroll.
    for delta in range(-8, 9):
        s = (SCROLL + 1 + delta) % 240
        vals = recon_values(nt, chr_data, s)
        # Infer the screenshot palette from the reconstruction.
        colors = [Counter() for _ in range(4)]
        for y in range(PLAY_H):
            for x in range(W):
                colors[vals[y][x]][pix[x, PLAY_TOP + y]] += 1
        palette = [c.most_common(1)[0][0] for c in colors]
        bad = 0
        for y in range(PLAY_H):
            for x in range(W):
                if pix[x, PLAY_TOP + y] != palette[vals[y][x]]:
                    bad += 1
        print(f"delta {delta:+d} s={s} mismatch {bad}/{W*PLAY_H} = {bad*100.0/(W*PLAY_H):.2f}% palette={palette}")

    # Dump the best reconstruction as an image using the inferred palette.
    s = (SCROLL + 1) % 240
    vals = recon_values(nt, chr_data, s)
    colors = [Counter() for _ in range(4)]
    for y in range(PLAY_H):
        for x in range(W):
            colors[vals[y][x]][pix[x, PLAY_TOP + y]] += 1
    palette = [c.most_common(1)[0][0] for c in colors]
    out = Image.new("RGB", (W, PLAY_H))
    op = out.load()
    for y in range(PLAY_H):
        for x in range(W):
            op[x, y] = palette[vals[y][x]]
    out.save(ROOT / "build" / "playfield_recon.png")


if __name__ == "__main__":
    main()
