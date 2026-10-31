"""Simulated Game Boy (DMG 160x144, 4 shades) view of River Raid level 1, cropped playfield following the jet."""
from pathlib import Path
from PIL import Image

ROOT = Path(r"C:\dev\AI\river-raid-port")
ram = (ROOT / "assets/atari/runtime/attract/ram_0000_ffff.bin").read_bytes()
rom = (ROOT / "assets/atari/river_raid_cart_a000_bfff.bin").read_bytes()
pal = (ROOT / "assets/atari/palette_altirra_ntsc.bin").read_bytes()
rb = lambda a: rom[a - 0xA000]
def rgb(c):
    c &= 0xFE
    return pal[c * 3:c * 3 + 3]
def lum(c):
    r, g, b = rgb(c)
    return 0.3 * r + 0.59 * g + 0.11 * b

SHADE = [(155, 188, 15), (139, 172, 15), (48, 98, 48), (15, 56, 15)]   # DMG greens, 0 light .. 3 dark
SRC_W, GB_W, GB_H = 192, 160, 144

# --- river: 157 rows x 192 px of source colour index (0 edge,1 land,2,3 water)
addr, screen, runs = 0x3F00, None, []
while addr < 0x3FC0:
    b = ram[addr]
    if b == 0x41:
        break
    if b in (0x4E, 0x56, 0x46, 0x5E):
        screen = ram[addr + 1] | (ram[addr + 2] << 8); addr += 3; continue
    if (b & 0x0F) == 0x0E:
        runs.append(screen); screen += 48
    addr += 1
rows = [ram[a:a + 48] for a in runs[:157]]
idx = [[(byte >> (6 - i * 2)) & 3 for byte in row for i in range(4)] for row in rows]
# colour index -> DMG shade: 0 bank outline = darkest, 1 land = mid-dark, 2 = darkest too, 3 water = lightest
BG_SHADE = {0: 3, 1: 2, 2: 3, 3: 0}

def render(jet_src_x, jet_row=120):
    layer = [[BG_SHADE[v] for v in r] for r in idx[:GB_H]]            # background, source px 0..191
    spr = {}                                                          # (x,y) -> shade, sprites over bg
    for slot in range(11):
        if ram[0x0500 + slot] == 0: continue
        aux, hx, oy = ram[0x050B + slot], ram[0x0516 + slot], ram[0x0C + slot]
        if not (0x5F <= oy <= 0xFB): continue
        sl, yoff, cl = rb(0xBB40 + aux), rb(0xBB60 + aux), rb(0xBB30 + aux)
        for i, k in enumerate(range(yoff, -1, -1)):
            bits, col = rb(0xB900 + sl + k), rb(0xB700 + cl + k)
            L = lum(col)
            sh = 3 if L < 90 else 2 if L < 150 else 1
            for bit in range(8):
                if bits & (0x80 >> bit):
                    spr[(hx - 32 + bit, (oy - 95) + i)] = sh
    ptr = rb(0xBACD + 18) | (rb(0xBACE + 18) << 8)
    for i in range(14):
        for bit in range(8):
            if rom[ptr - 0xA000 + i] & (0x80 >> bit):
                spr[(jet_src_x + bit, jet_row + i)] = 3
    scx = max(0, min(SRC_W - GB_W, jet_src_x + 4 - GB_W // 2))
    im = Image.new("RGB", (GB_W, GB_H))
    px = im.load()
    for y in range(GB_H):
        for x in range(GB_W):
            sx = x + scx
            s = spr.get((sx, y), layer[y][sx])
            px[x, y] = SHADE[s]
    return im, scx

# water extent at the jet row, to put the jet on water
water = [x for x, v in enumerate(idx[120 + 7]) if v == 3]
lo, hi = min(water) + 6, max(water) - 14
views = [render(lo)[0], render((lo + hi) // 2)[0], render(hi)[0]]
S, gap = 3, 12
out = Image.new("RGB", (3 * GB_W * S + 4 * gap, GB_H * S + 2 * gap), (40, 40, 40))
for n, v in enumerate(views):
    out.paste(v.resize((GB_W * S, GB_H * S), Image.NEAREST), (gap + n * (GB_W * S + gap), gap))
out.save(Path(__file__).with_name("gb_view_3panel.png"))
views[1].resize((GB_W * 4, GB_H * 4), Image.NEAREST).save(Path(__file__).with_name("gb_view_center.png"))
print("river width px", max(water) - min(water), "jet x", lo, (lo + hi) // 2, hi)
