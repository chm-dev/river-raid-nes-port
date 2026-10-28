#!/usr/bin/env python3
"""Parse an Atari 8-bit XEX executable and dump its load segments.

Reference target (River Raid, 1983, Activision, US, [a3]):

    FFFF                        XEX magic (0xFFFF = Atari executable)
    F0 9F  BF FF                segment $9FF0-$BFFF (16-byte loader stub + 8 KiB cart)
    E0 02  E1 02  F0 9F         segment $02E0-$02E1 (RUNAD -> $9FF0)

Usage:
    python tools/dump_xex.py
    python tools/dump_xex.py --xex "xex/River Raid (1983)(Activision)(US)[a3].xex"

Outputs (default --out-dir assets/atari):
    <name>_cart_a000_bfff.bin          the 8 KiB Atari 800 left cartridge image
    <name>_loader_9ff0_9fff.bin        the 16-byte loader stub that runs before it
    <name>_full_9ff0_bfff.bin          contiguous image from the lowest to highest segment
    <name>_segments.json               machine-readable segment map + digests
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

XEX_MAGIC = 0xFFFF
CART_START = 0xA000
CART_END = 0xBFFF
STUB_START = 0x9FF0
STUB_END = 0x9FFF


def read_segments(data: bytes) -> list[dict]:
    """Split an XEX image into its load segments.

    XEX is a sequence of: 0xFFFF, start(lo,hi), end(lo,hi), payload...
    Some files repeat 0xFFFF between blocks; that is accepted here.
    """
    segments: list[dict] = []
    pos = 0
    size = len(data)

    if size < 2 or int.from_bytes(data[0:2], "little") != XEX_MAGIC:
        raise ValueError("not an Atari XEX: missing 0xFFFF magic")

    pos = 2
    while pos < size:
        # Optional repeated magic between segments.
        while pos + 1 < size and int.from_bytes(data[pos : pos + 2], "little") == XEX_MAGIC:
            pos += 2

        if pos >= size:
            break
        if pos + 4 > size:
            raise ValueError(f"truncated segment header at file offset 0x{pos:04X}")

        start = int.from_bytes(data[pos : pos + 2], "little")
        end = int.from_bytes(data[pos + 2 : pos + 4], "little")
        pos += 4

        length = end - start + 1
        if length <= 0:
            raise ValueError(
                f"invalid segment 0x{start:04X}-0x{end:04X} at file offset 0x{pos - 4:04X}"
            )
        if pos + length > size:
            raise ValueError(
                f"segment 0x{start:04X}-0x{end:04X} needs {length} bytes, "
                f"only {size - pos} remain"
            )

        payload = data[pos : pos + length]
        segments.append(
            {
                "start": start,
                "end": end,
                "length": length,
                "file_offset": pos,
                "sha256": hashlib.sha256(payload).hexdigest(),
                "payload": payload,
            }
        )
        pos += length

    if pos != size:
        raise ValueError(f"{size - pos} trailing bytes after the last segment")

    return segments


def slice_segment(segments: list[dict], start: int, end: int) -> bytes:
    """Return the bytes of a single segment covering [start, end] exactly."""
    for seg in segments:
        if seg["start"] <= start and seg["end"] >= end:
            base = seg["start"]
            return seg["payload"][start - base : end - base + 1]
    raise ValueError(f"no segment covers 0x{start:04X}-0x{end:04X}")


def image_for_range(segments: list[dict], start: int, end: int) -> bytes:
    """Assemble a contiguous image for [start, end] from one or more segments.

    Unlike spanning the whole file, this never fabricates the large unmapped gap
    between a cartridge block and a separate RUNAD/init block.
    """
    image = bytearray(end - start + 1)
    covered = bytearray(end - start + 1)
    for seg in segments:
        lo = max(seg["start"], start)
        hi = min(seg["end"], end)
        if lo > hi:
            continue
        src_off = lo - seg["start"]
        dst_off = lo - start
        count = hi - lo + 1
        image[dst_off : dst_off + count] = seg["payload"][src_off : src_off + count]
        covered[dst_off : dst_off + count] = b"\x01" * count
    gaps = [i for i, c in enumerate(covered) if not c]
    if gaps:
        raise ValueError(
            f"0x{start:04X}-0x{end:04X} is not fully covered; "
            f"first unmapped byte at 0x{start + gaps[0]:04X}"
        )
    return bytes(image)


def main() -> int:
    here = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "--xex",
        type=Path,
        default=here / "xex" / "River Raid (1983)(Activision)(US)[a3].xex",
        help="input XEX file",
    )
    parser.add_argument("--out-dir", type=Path, default=here / "assets" / "atari", help="output directory")
    args = parser.parse_args()

    data = args.xex.read_bytes()
    segments = read_segments(data)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    stem = "river_raid"

    cart = slice_segment(segments, CART_START, CART_END)
    stub = slice_segment(segments, STUB_START, STUB_END)
    full = image_for_range(segments, STUB_START, CART_END)

    outputs = {
        f"{stem}_cart_a000_bfff.bin": cart,
        f"{stem}_loader_9ff0_9fff.bin": stub,
        f"{stem}_full_9ff0_bfff.bin": full,
    }
    for seg in segments:
        outputs[f"{stem}_seg_{seg['start']:04x}_{seg['end']:04x}.bin"] = seg["payload"]
    for name, blob in outputs.items():
        (args.out_dir / name).write_bytes(blob)

    manifest = {
        "source": str(args.xex),
        "source_bytes": len(data),
        "source_sha256": hashlib.sha256(data).hexdigest(),
        "segments": [
            {
                "start": f"0x{seg['start']:04X}",
                "end": f"0x{seg['end']:04X}",
                "length": seg["length"],
                "file_offset": f"0x{seg['file_offset']:04X}",
                "sha256": seg["sha256"],
            }
            for seg in segments
        ],
        "outputs": {
            name: {"bytes": len(blob), "sha256": hashlib.sha256(blob).hexdigest()}
            for name, blob in outputs.items()
        },
    }
    (args.out_dir / f"{stem}_segments.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    print(f"source : {args.xex} ({len(data)} bytes)")
    print(f"sha256 : {manifest['source_sha256']}")
    for seg in segments:
        print(
            f"segment: 0x{seg['start']:04X}-0x{seg['end']:04X} "
            f"({seg['length']} bytes) @ file+0x{seg['file_offset']:04X} "
            f"sha256={seg['sha256'][:16]}"
        )
    for name, blob in outputs.items():
        print(f"wrote  : {args.out_dir / name} ({len(blob)} bytes)")

    # Sanity checks specific to the reference target.
    if cart[0] != 0xA9 or cart[1] != 0x00:
        print("warning: cartridge does not begin with LDA #$00 as expected")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
