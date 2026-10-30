#!/usr/bin/env python3
"""Compare river_restart() (host build) with the Atari $B1C0 trace.

usage: compare_restart.py trace.jsonl restart.exe
"""
import json
import subprocess
import sys

trace = [json.loads(l) for l in open(sys.argv[1], encoding="utf-8") if l.strip()]
stdin = "".join("{level} {f23} {c4c} {e2}\n".format(**t["in"]) for t in trace)
res = subprocess.run([sys.argv[2]], input=stdin, capture_output=True, text=True, check=True)
lines = res.stdout.strip().splitlines()
assert len(lines) == len(trace), (len(lines), len(trace))

bad = 0
for t, line in zip(trace, lines):
    got = list(map(int, line.split()))
    o = t["out"]
    want = [o["level"], *o["seeds"], *o["prev"]]
    ok = got[:9] == want
    print(("ok  " if ok else "FAIL"), t["in"], "->", want if ok else f"atari {want} port {got[:9]}")
    bad += not ok
print(f"{len(trace) - bad}/{len(trace)} scenarios match")
sys.exit(1 if bad else 0)

