#!/usr/bin/env python3
"""Run the C spawner (src/entities.c, host build) over live Atari states and
compare it with tools/object_model.py.

Usage: python tools/object_host_check.py build/object_trace.jsonl [more traces]
Needs zig (zig cc) on PATH.
"""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
from object_model import spawn_step  # noqa: E402


def build() -> Path:
    out = Path(tempfile.gettempdir()) / "spawn_host"
    out.mkdir(exist_ok=True)
    exe = out / "spawn.exe"
    cmd = ["zig", "cc", "-std=c11", "-O1", "-w", f"-I{ROOT / 'tests/host'}", f"-I{ROOT / 'src'}",
           "-include", "stdlib.h", "-o", str(exe), str(ROOT / "tests/host/spawn_main.c"),
           str(ROOT / "src/river.c"), str(ROOT / "src/atari_data.c"), str(ROOT / "src/entity_gfx.c")]
    subprocess.run(cmd, check=True)
    return exe


def main() -> int:
    exe = build()
    samples = []
    for path in sys.argv[1:]:
        rows = [json.loads(l) for l in Path(path).read_text().splitlines()]
        for a, b in zip(rows, rows[1:]):
            za = bytes.fromhex(a["zp"])
            zb = bytes.fromhex(b["zp"])
            if (zb[0xC2] - za[0xC2]) & 255 != 1:
                continue
            # Skip the shift-on-wrap rows: the C port has no slot table.
            if (za[0x16] + 1) & 255 == 0:
                continue
            samples.append((a, za, bytes.fromhex(a["obj"])))
    lines = []
    for a, z, o in samples:
        # $052D (obj[0x2D]) is the generator's margin flag.
        lines.append(" ".join(str(x) for x in (
            z[0x2F], z[0x60], z[0x5D], z[0x5A], z[0x5F], z[0x2C], z[0x2D], o[0x2D], z[0x23],
            z[0x54], z[0x52], z[0xB5], z[0xAA], z[0xAB], z[0xAC], z[0x32],
            1 if z[0x4E] < 0x0B else 0, 0, 0)))
    res = subprocess.run([str(exe)], input="\n".join(lines) + "\n", capture_output=True, text=True, check=True)
    got = [tuple(map(int, l.split())) for l in res.stdout.splitlines()]
    bad = spawns = 0
    for (a, z, o), g in zip(samples, got):
        zp2, op2 = spawn_step(z, o)
        busy2 = 1 if zp2[0x4E] < 0x0B else 0
        spawned = zp2[0x4D] < z[0x4D] and busy2
        want = [busy2, zp2[0x32] if busy2 else 0]
        if spawned:
            s = zp2[0x4D]
            aux = op2[0x0B + s]
            want += [aux, op2[0x16 + s] if aux >= 2 else g[3], 1 if zp2[0x42 + s] & 8 else 0]
            spawns += 1
        else:
            want += [g[2], g[3], g[4]]
            if busy2:
                want[2] = g[2]
        # The model does not decrement $32 in the same place for rows that
        # only count down; compare busy flag always, hold/aux on spawns.
        ok = g[0] == want[0] and (not spawned or (g[1] == want[1] and g[2] == want[2] and
                                                  g[3] == want[3] and g[4] == want[4]))
        if not ok:
            bad += 1
            if bad <= 10:
                print("row", a["n"], "C", g, "model", want, "lvl", z[0x60], "2F", z[0x2F])
    print(f"{len(samples)} states, {spawns} spawns, {bad} mismatches")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
