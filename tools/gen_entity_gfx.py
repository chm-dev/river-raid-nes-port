#!/usr/bin/env python3
"""Generate src/entity_gfx.[ch]: NES sprite tiles for the Atari P3 objects.

Source: atari_data.h (bottom-up 1bpp shapes, per-row Atari colours).
aux 2..6 are the explosion/debris fragments (also the jet's death frames),
aux 7..15 are the enemies, fuel depot and bridge marker.  Each shape is cut
into 8x8 tiles, top-down.  Colours are folded to three NES colours per
palette group (enemies / fuel / explosion).
"""
import re
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
hdr = (ROOT / "src" / "atari_data.h").read_text()
src = (ROOT / "src" / "atari_data.c").read_text() if (ROOT / "src" / "atari_data.c").stat().st_size > 200 else ""
text = hdr + src


def arr(name):
    m = re.search(name + r"\[\d*\]\s*=\s*\{(.*?)\};", text, re.S)
    return [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", m.group(1))]


shape = arr("atari_obj_shape")
color = arr("atari_obj_color")
soff = arr("atari_obj_shape_off")
coff = arr("atari_obj_color_off")
yoff = arr("atari_obj_yoff")

# Classic NES (2C02) RGB palette, used to pick the nearest NES colour for an
# Atari colour (palette_altirra_ntsc.bin) by RGB distance.
NES_RGB = """
7C7C7C 0000FC 0000BC 4428BC 940084 A80020 A81000 881400 503000 007800 006800 005800 004058 000000 000000 000000
BCBCBC 0078F8 0058F8 6844FC D800CC E40058 F83800 E45C10 AC7C00 00B800 00A800 00A844 008888 000000 000000 000000
F8F8F8 3CBCFC 6888FC 9878F8 F878F8 F85898 F87858 FCA044 F8B800 B8F818 58D854 58F898 00E8D8 787878 000000 000000
FCFCFC A4E4FC B8B8F8 D8B8F8 F8B8F8 F8A4C0 F0D0B0 FCE0A8 F8D878 D8F878 B8F8B8 B8F8D8 00FCFC F8D8F8 000000 000000
""".split()
NES_RGB = [tuple(int(h[i:i + 2], 16) for i in (0, 2, 4)) for h in NES_RGB]
ATARI_RGB = (ROOT / "assets" / "atari" / "palette_altirra_ntsc.bin").read_bytes()


def nearest(c, candidates):
    r, g, b = ATARI_RGB[c * 3:c * 3 + 3]
    def dist(n):
        nr, ng, nb = NES_RGB[n]
        return 2 * (r - nr) ** 2 + 4 * (g - ng) ** 2 + 3 * (b - nb) ** 2
    return min(range(len(candidates)), key=lambda i: dist(candidates[i])) + 1


# Hardware sprite palette per aux and its three NES colours.  The jet's
# palette 0 (yellow, white, red) also serves the fuel depot and the debris;
# palette 1 belongs to the HUD gauge.
PAL0 = [0x28, 0x30, 0x16]
PAL2 = [0x0F, 0x28, 0x02]      # helicopters: black, yellow, darkest blue ($02; water is the brighter $11)
PAL3 = [0x0F, 0x06, 0x2C]      # ship: black, dark red, cyan
PAL1 = [0x0B, 0x08, 0x18]      # bridge stripes: Atari $12 / $14 + $22 / $18 (dark green, brown, olive)
PALETTES = {0: PAL0, 1: PAL1, 2: PAL2, 3: PAL3}
HW_PAL = {a: 2 for a in (8, 9, 10, 11)}
HW_PAL.update({7: 0, 13: 3, 14: 1, 12: 0, 15: 0})


def value_for(a, r):
    if a in (8, 9, 10, 11) and color[coff[a] + r] == 0x80:
        return 3  # the blue cockpit stripe (nearest-colour would pick black)
    return nearest(color[coff[a] + r], PALETTES[HW_PAL[a]])

tiles = []
first = [0] * 16
count = [0] * 16
rows_of = [0] * 16
cols_of = [1] * 16
exp_first = [0xFF] * 16
# SIZEP3 per aux ($BBA2): 1 = double width (ships), 3 = quad width ($0E).
SIZE_COLS = {13: 2, 14: 4}
ROM = (ROOT / "assets" / "atari" / "river_raid_cart_a000_bfff.bin").read_bytes()
BB40 = ROM[0xBB40 - 0xA000:0xBB40 - 0xA000 + 16]


def build(data, rows, m):
    """data: top-down [(bits, value)]; returns row-major 8x8 tiles, m columns."""
    out = []
    for t in range((rows + 7) // 8):
        for k in range(m):
            buf = []
            for i in range(8):
                idx = t * 8 + i
                bits, v = data[idx] if idx < rows else (0, 0)
                wide = 0
                for bit in range(7, -1, -1):  # each source bit -> m pixels
                    wide = (wide << m) | (((1 << m) - 1) if (bits >> bit) & 1 else 0)
                byte = (wide >> (8 * (m - 1 - k))) & 0xFF
                buf += [byte if v & 1 else 0, byte if v & 2 else 0]
            out.append(buf)
    return out


for a in range(7, 16):
    rows = yoff[a] + 1
    rows_of[a] = rows
    m = cols_of[a] = SIZE_COLS.get(a, 1)
    data = [(shape[soff[a] + r], value_for(a, r)) for r in range(rows)]
    data.reverse()  # bottom-up -> top-down
    first[a] = len(tiles)
    t = build(data, rows, m)
    count[a] = len(t)
    tiles += t

# Enemy explosions.  The Atari keeps the object's aux (colour strip, height,
# SIZEP3 width) and swaps its shape to the debris frames 2..6 (shape pointer
# $B900+$BB40[type]); the object's full height of bytes is read from there,
# so a fuel depot bursts into a tall cloud and a ship into a wide one.
EXP_SRC = {7: 7, 8: 8, 9: 8, 10: 8, 11: 8, 12: 12, 13: 13, 14: 12, 15: 15}
done = {}
for a, src in EXP_SRC.items():
    if src not in done:
        done[src] = len(tiles)
        rows, m = rows_of[src], cols_of[src]
        for frame in range(2, 7):
            base = 0xB900 + BB40[frame] - 0xA000
            data = [(ROM[base + r], value_for(src, r)) for r in range(rows)]
            data.reverse()
            tiles += build(data, rows, m)
    exp_first[a] = done[src]

# Player shot: a 2px wide, 6px tall bar.
shot_tile = len(tiles)
tiles.append(sum([[0x18 if i < 6 else 0, 0] for i in range(8)], []))
flat = [b for t in tiles for b in t]
out_h = f"""/* Generated by tools/gen_entity_gfx.py - do not edit. */
#ifndef ENTITY_GFX_H
#define ENTITY_GFX_H

#include <stdint.h>

#define ENT_GFX_TILES   {len(tiles)}u
#define ENT_GFX_SHOT    {shot_tile}u

/* Per aux 0..15: first tile, tile count, shape rows, width in tiles and\n * hardware sprite palette.  Tiles are row-major, left to right. */
extern const uint8_t ent_gfx_first[16];
extern const uint8_t ent_exp_first[16];  /* explosion frame 2 block; +count per frame */
extern const uint8_t ent_gfx_count[16];
extern const uint8_t ent_gfx_rows[16];
extern const uint8_t ent_gfx_cols[16];  /* width in 8-px tiles (SIZEP3) */
extern const uint8_t ent_gfx_pal[16];
/* Sprite palettes 1 (bridge), 2 (helicopters) and 3 (ships); palette 0 is the jet's. */
extern const uint8_t ent_gfx_palette[3][3];
extern const uint8_t ent_gfx_chr[{len(flat)}];

#endif
"""


def carr(name, vals, per=16):
    lines = []
    for i in range(0, len(vals), per):
        lines.append("    " + ", ".join(f"0x{v:02X}" for v in vals[i:i + per]) + ",")
    return f"{name} = {{\n" + "\n".join(lines) + "\n};\n"


pal = [HW_PAL.get(a, 0) for a in range(16)]
out_c = '/* Generated by tools/gen_entity_gfx.py - do not edit. */\n#include "entity_gfx.h"\n\n'
out_c += carr("const uint8_t ent_gfx_first[16]", first)
out_c += carr("const uint8_t ent_exp_first[16]", exp_first)
out_c += carr("const uint8_t ent_gfx_count[16]", count)
out_c += carr("const uint8_t ent_gfx_rows[16]", rows_of)
out_c += carr("const uint8_t ent_gfx_cols[16]", cols_of)
out_c += carr("const uint8_t ent_gfx_pal[16]", pal)
out_c += "const uint8_t ent_gfx_palette[3][3] = {\n" + "".join(
    "    { " + ", ".join(f"0x{c:02X}" for c in PALETTES[g]) + " },\n" for g in (1, 2, 3)) + "};\n"
out_c += carr(f"const uint8_t ent_gfx_chr[{len(flat)}]", flat)
(ROOT / "src" / "entity_gfx.h").write_text(out_h)
(ROOT / "src" / "entity_gfx.c").write_text(out_c)
print("tiles", len(tiles))
