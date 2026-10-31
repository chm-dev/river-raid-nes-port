"""Sample the Atari's shell/tank state ($38 $39 $3A $50 ...) every frame at a high level."""
import sys
from pathlib import Path

sys.path.insert(0, r"C:\dev\AI\river-raid-port\tools")
from atari_river_trace import DEFAULT_XEX, connect_any  # noqa: E402

LEVEL = int(sys.argv[1]) if len(sys.argv) > 1 else 9
FRAMES = int(sys.argv[2]) if len(sys.argv) > 2 else 3000
OUT = Path(r"C:\Users\chm2\AppData\Local\Temp\claude\C--dev-AI-river-raid-port\9d00cab7-52f8-4869-8c9f-af03ed8c1d8d\scratchpad")

a = connect_any(None)
a.config("video", "ntsc")
a.fresh()
a.boot(str(DEFAULT_XEX))
a.frame(320); a.clock()

a.frame(60); a.clock()
a.poke(0x60, LEVEL)

prev38 = 0
pend = []
log = []
shots = 0
for f in range(FRAMES):
    a.frame(1)
    zp = a.peek(0x00, 0x100)
    obj = a.peek(0x0500, 0x40)
    a.poke(0x6A, 0x63)
    if zp[0x38] != 0 and prev38 == 0:
        log.append("f=%d lvl=%02X 38=%02X 39=%02X 3A=%02X 23=%02X 2D=%02X 36=%02X 4E=%02X 5D=%02X 5F=%02X 2C=%02X AC=%02X 3E=%02X 3F=%02X type4E=%02X" % (
            f, zp[0x60], zp[0x38], zp[0x39], zp[0x3A], zp[0x23], zp[0x2D], zp[0x36], zp[0x4E], zp[0x5D], zp[0x5F], zp[0x2C], zp[0xAC], zp[0x3E], zp[0x3F], obj[zp[0x4E]] if zp[0x4E] < 0x40 else 0xFF))
        if shots < 6:
            (OUT / ("tank_%d_%d.png" % (LEVEL, f))).write_bytes(a.screenshot())
            shots += 1
            pend.append(f + 25)
    if f in pend and shots < 12:
        (OUT / ("tank_%d_%d_late.png" % (LEVEL, f))).write_bytes(a.screenshot())
        shots += 1
    prev38 = zp[0x38]
a.joy(0, "centre")
(OUT / ("starts_%d.txt" % LEVEL)).write_text("\n".join(log))
print(len(log), "tank starts")
a.quit()


