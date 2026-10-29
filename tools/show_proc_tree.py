#!/usr/bin/env python3
"""Find a procedural tree feature and compare exact vs nearest-of-82 tiles."""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
from river_model import initial_state

ROOT = Path(__file__).resolve().parent.parent
CAP = ROOT / "assets" / "atari" / "extracted" / "river_rows_157.bin"
PALETTE = ROOT / "assets" / "atari" / "palette_altirra_ntsc.bin"
OUT = ROOT / "build" / "proc_tree_layout.png"

VIS = 4
W = 160
COLORS = {0: 0x00, 1: 0xD6, 2: 0x20, 3: 0x84}


def tiledata(rows, tr, c):
    return tuple(
        (rows[tr * 8 + y][VIS + c * 2 + (x >> 2)] >> (6 - 2 * (x & 3))) & 3
        for y in range(8)
        for x in range(8)
    )


def main() -> None:
    cap = CAP.read_bytes()
    caprows = [cap[i * 48 : (i + 1) * 48] for i in range(len(cap) // 48)]
    atlas = []
    seen = set()
    for tr in range(19):
        for c in range(20):
            t = tiledata(caprows, tr, c)
            if t not in seen:
                seen.add(t)
                atlas.append(t)

    st = initial_state()
    rows = []
    state = []
    for _ in range(4000):
        rows.append(st.step())
        state.append((st.bc, st.be, st.bd, st.feature, st.rowcount))

    # Find a feature with a wide template that lasts at least 16 rows.
    best = None
    run_start = None
    run_bc = 0
    for i, (bc, be, bd, feat, rc) in enumerate(state):
        active = feat != 0 and rc < be and bc >= 6
        if active and run_start is None:
            run_start = i
            run_bc = bc
        elif not active and run_start is not None:
            if i - run_start >= 16 and (best is None or i - run_start > best[1] - best[0]):
                best = (run_start, i, run_bc)
            run_start = None
    if best is None:
        raise SystemExit("no wide feature found")
    start, end, bc = best
    print(f"feature raw rows {start}..{end} ({end - start} rows), bc={bc}")

    # Use two tile rows before the feature through one after, 6 tile rows.
    tr0 = max(0, start // 8 - 1)
    tr1 = tr0 + 6
    # shift so the feature is inside
    if tr1 * 8 < end:
        tr0 = max(0, (end // 8) - 5)
        tr1 = tr0 + 6
    print(f"tile rows {tr0}..{tr1 - 1}")

    y0, y1 = tr0 * 8, tr1 * 8
    x0, x1 = 0, 160

    pal = PALETTE.read_bytes()

    def rgb(v):
        c = COLORS[v] * 3
        return tuple(pal[c : c + 3])

    exact = Image.new("RGB", (x1 - x0, y1 - y0))
    approx = Image.new("RGB", (x1 - x0, y1 - y0))
    ep = exact.load()
    ap = approx.load()
    for y in range(y0, y1):
        for x in range(x0, x1):
            v = (rows[y][VIS + (x >> 2)] >> (6 - 2 * (x & 3))) & 3
            ep[x, y - y0] = rgb(v)

    errors = {}
    for tr in range(tr0, tr1):
        for c in range(20):
            t = tiledata(rows, tr, c)
            best_t = min(atlas, key=lambda a: sum(1 for i in range(64) if a[i] != t[i]))
            d = sum(1 for i in range(64) if best_t[i] != t[i])
            errors[(c, tr)] = d
            for y in range(8):
                for x in range(8):
                    ap[c * 8 + x, tr * 8 + y - y0] = rgb(best_t[y * 8 + x])

    scale = 4
    label = 18
    gw = (x1 - x0) * scale
    gh = (y1 - y0) * scale
    canvas = Image.new("RGB", (gw * 2 + 12, gh + label * 2), (24, 24, 24))
    canvas.paste(exact.resize((gw, gh), Image.NEAREST), (0, label))
    canvas.paste(approx.resize((gw, gh), Image.NEAREST), (gw + 12, label))
    d = ImageDraw.Draw(canvas)
    d.text((4, 3), f"ATARI exact rows {y0}..{y1}", fill=(240, 240, 240))
    d.text((gw + 16, 3), "NES nearest of 82 captured tiles", fill=(240, 240, 240))
    for panel in (0, 1):
        ox = panel * (gw + 12)
        for c in range(20):
            for tr in range(tr0, tr1):
                x = ox + c * 8 * scale
                y = label + (tr * 8 - y0) * scale
                err = errors[(c, tr)]
                d.rectangle([x, y, x + 8 * scale, y + 8 * scale],
                            outline=(255, 40, 40) if err else (70, 70, 70))
                if panel == 1 and err >= 8:
                    d.text((x + 1, y + 1), str(err), fill=(255, 90, 90))
    canvas.save(OUT)
    bad = sum(1 for v in errors.values() if v)
    print(f"wrote {OUT.relative_to(ROOT)}: {bad}/{len(errors)} blocks wrong")


if __name__ == "__main__":
    main()
