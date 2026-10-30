import json
import sys

data = json.load(open(sys.argv[1] if len(sys.argv) > 1 else 'build/sound_events.json'))
for name, seq in data.items():
    print(f"=== {name}")
    for r in seq:
        s = (f"  f={r['f']:3d} 50={r['50']:3d} 7a={r['7a']:2d} 7b={r['7b']:2d} "
             f"7c={r['7c']:2d} 7d={r['7d']:2d} 7e={r['7e']:2d} 7f={r['7f']:2d} c6={r['c6']:2d}")
        for i, c in enumerate(r['ch']):
            f = c['freq_hz']
            s += (f" |c{i+1} a={c['audf']:02x} v={c['volume']:2d} d={c['distortion']} "
                  + (f"{f:7.1f}" if f else "    ---"))
        print(s)
