import json
import sys

path = sys.argv[1] if len(sys.argv) > 1 else 'build/sound_probe.jsonl'
only = sys.argv[2] if len(sys.argv) > 2 else None
rows = [json.loads(l) for l in open(path)]
for r in rows:
    ch = r['audio'] if isinstance(r['audio'], list) else r['audio']['channels']
    s = (f"f={r['f']:4d} 50={r['50']:3d} 76={r['76']:3d} 7a={r['7a']:2d} 7b={r['7b']:2d} "
         f"7c={r['7c']:2d} 7d={r['7d']:2d} 7e={r['7e']:2d} 7f={r['7f']:2d} "
         f"5b={r['5b']:2d} 59={r['59']:3d} 19={r['19']:2d} 29={r['29']:2d} 2a={r['24']:2d}")
    for i, c in enumerate(ch):
        f = c['freq_hz']
        s += (f" |c{i+1} a={c['audf']:02x} v={c['volume']:2d} d={c['distortion']} "
              + (f"{f:7.1f}" if f else "    ---"))
    if only:
        import re
        if not re.search(only, s):
            continue
    print(s)
