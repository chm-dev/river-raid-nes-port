#!/usr/bin/env python3
"""Determine the Atari PMG memory layout empirically on a bare machine.

Builds a blank display list, enables player/missile DMA, zeroes the 2K PM
area at PMBASE=$08, then writes a single $FF byte at candidate offsets and
captures a screenshot.  The resulting white 8-pixel bar reveals which PMG
slot (player/missile index) and which scanline that address maps to.
"""
from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

SDK = Path(r"F:\Emu\Atari800\Altirra\AlitrraBridge\sdk\python")
if str(SDK) not in sys.path:
    sys.path.insert(0, str(SDK))

from altirra_bridge import AltirraBridge  # noqa: E402

OUT = Path(r"C:\Users\chm2\AppData\Local\Temp\opencode\pmgprobe")


def connect() -> AltirraBridge:
    """Connect to the known live server, retrying if a stale client is being evicted."""
    import time

    preferred = Path(tempfile.gettempdir()) / "altirra-bridge-12200.token"
    cands = [preferred] + sorted(
        (p for p in Path(tempfile.gettempdir()).glob("altirra-bridge-*.token") if p != preferred),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    last: Exception | None = None
    for attempt in range(3):
        for path in cands:
            if not path.exists():
                continue
            try:
                return AltirraBridge.from_token_file(str(path))
            except Exception as exc:  # noqa: BLE001 - stale token / busy server
                last = exc
        time.sleep(0.5)
    raise SystemExit(f"no usable AltirraBridge server ({last})")


def setup(a: AltirraBridge) -> None:
    a.config("video", "ntsc")
    a.fresh()
    a.boot_bare()
    # 2K PM area at $0800.
    a.memload(0x0800, bytes(0x0800))
    # Display list: 30 x 8 blank lines + JVB at $2000.
    a.memload(0x2000, bytes([0x70] * 30 + [0x41]))
    a.hwpoke(0xD402, 0x00)  # DLISTL
    a.hwpoke(0xD403, 0x20)  # DLISTH
    a.hwpoke(0xD407, 0x08)  # PMBASE = $0800
    a.hwpoke(0xD01D, 0x03)  # GRACTL = players + missiles
    a.hwpoke(0xD400, 0x3F)  # DMACTL = wide playfield + players + missiles + DL
    a.hwpoke(0xD01B, 0x00)  # PRIOR normal
    a.hwpoke(0xD01A, 0x00)  # COLBK black
    a.hwpoke(0xD016, 0x0F)  # COLPF0 white
    a.hwpoke(0xD017, 0x0F)
    a.hwpoke(0xD018, 0x0F)
    a.hwpoke(0xD019, 0x0F)
    for reg in (0xD000, 0xD001, 0xD002, 0xD003):
        a.hwpoke(reg, 0x50)  # all players at center
    a.hwpoke(0xD008, 0x50)  # HPOSM0
    a.hwpoke(0xD009, 0x50)
    a.hwpoke(0xD00A, 0x50)
    a.hwpoke(0xD00B, 0x50)
    for reg in (0xD012, 0xD013, 0xD014, 0xD015):
        a.hwpoke(reg, 0x0F)  # all PM colors white
    a.hwpoke(0xD40E, 0x00)  # NMIEN off
    a.frame(1)
    a.clock()


def poke_one(a: AltirraBridge, addr: int) -> None:
    a.memload(0x0800, bytes(0x0800))
    a.poke(addr, 0xFF)
    a.frame(1)
    a.clock()


def analyze(png: bytes, path: Path) -> dict:
    path.write_bytes(png)
    from PIL import Image

    im = Image.open(path).convert("RGB")
    w, h = im.size
    rows = []
    for y in range(h):
        xs = [x for x in range(w) if im.getpixel((x, y)) == (255, 255, 255)]
        if xs:
            rows.append((y, min(xs), max(xs)))
    return {"size": [w, h], "rows": rows}


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    a = connect()
    setup(a)
    results = {}
    # Probe every 256-byte page and a few in-page offsets.
    for page in range(0x08, 0x10):
        for off in (0x00, 0x32, 0x80, 0xFF):
            addr = (page << 8) | off
            poke_one(a, addr)
            png = a.screenshot()
            info = analyze(png, OUT / f"p{page:02x}_{off:02x}.png")
            results[f"{addr:04X}"] = info
            print(f"{addr:04X}: {info}")
    (OUT / "results.json").write_text(json.dumps(results, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
