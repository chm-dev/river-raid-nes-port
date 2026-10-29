#!/usr/bin/env python3
"""Replay tools/atari_object_trace.py samples through tools/object_model.py.

For every consecutive pair of samples the model predicts the spawn block's
effect; the object arrays of the next sample must agree on slot bookkeeping,
object type/size/Y and (for a fresh spawn) X position and direction.

Usage:
    python tools/object_validate.py build/object_trace.jsonl
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import object_model  # noqa: E402
from object_model import spawn_step  # noqa: E402


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/object_trace.jsonl")
    rows = [json.loads(line) for line in path.read_text().splitlines()]
    bad = spawns = checked = 0
    kinds: dict[int, int] = {}
    for a, b in zip(rows, rows[1:]):
        za, oa = bytes.fromhex(a["zp"]), bytes.fromhex(a["obj"])
        zb, ob = bytes.fromhex(b["zp"]), bytes.fromhex(b["obj"])
        # Only consecutive generator rows: Y bytes advance by exactly one.
        if (zb[0xC2] - za[0xC2]) & 255 != 1:
            continue
        object_model.TRACE.clear()
        z, o = spawn_step(za, oa)
        checked += 1
        errs = []
        for name, addr in (("4D", 0x4D), ("4E", 0x4E)):
            if z[addr] != zb[addr]:
                errs.append(f"${name} {z[addr]:02X}!={zb[addr]:02X}")
        for s in range(11):
            if z[0x0C + s] != zb[0x0C + s] and z[0x4D] <= s:
                errs.append(f"Y[{s}] {z[0x0C+s]:02X}!={zb[0x0C+s]:02X}")
            if o[0x0B + s] != ob[0x0B + s] and z[0x4D] <= s:
                errs.append(f"aux[{s}] {o[0x0B+s]:02X}!={ob[0x0B+s]:02X}")
        new = z[0x4E] if z[0x4E] < 11 and z[0x4D] != za[0x4D] else None
        if new is not None and z[0x32] == (zb[0x32] + 1) & 255 or \
                (new is not None and z[0x4D] < za[0x4D]):
            spawns += 1
            s = z[0x4D]
            kinds[o[0x0B + s]] = kinds.get(o[0x0B + s], 0) + 1
            # Blank objects (aux 0/1) keep whatever was left in A as their X.
            # Jets (aux 7) fly every frame, so they have moved once already.
            tol = 1 if o[0x0B + s] == 7 else 0
            if o[0x0B + s] >= 2 and abs(o[0x16 + s] - ob[0x16 + s]) > tol:
                errs.append(f"X[{s}] {o[0x16+s]:02X}!={ob[0x16+s]:02X}")
            if o[0x21 + s] != ob[0x21 + s]:
                errs.append(f"size[{s}] {o[0x21+s]:02X}!={ob[0x21+s]:02X}")
            if (z[0x42 + s] & 0x3C) != (zb[0x42 + s] & 0x3C):
                errs.append(f"status[{s}] {z[0x42+s]:02X}!={zb[0x42+s]:02X}")
        if errs:
            bad += 1
            if bad <= 12:
                print(f"row {a['n']}: " + "; ".join(errs))
                print("   level", za[0x60], "5F/2C", za[0x5F], za[0x2C], "2D", hex(za[0x2D]),
                      "B5", za[0xB5], "5D", za[0x5D], "2F", za[0x2F], "52/54", za[0x52], za[0x54],
                      object_model.TRACE)
    print(f"checked {checked} transitions, {spawns} spawns, {bad} mismatches")
    print("spawned aux histogram:", dict(sorted(kinds.items())))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
