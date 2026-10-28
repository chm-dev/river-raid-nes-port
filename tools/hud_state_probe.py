#!/usr/bin/env python3
"""Dump HUD buffer, DL tail, and HSCROL in the attract/gameplay state."""
from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

SDK = Path(r"F:\Emu\Atari800\Altirra\AlitrraBridge\sdk\python")
if str(SDK) not in sys.path:
    sys.path.insert(0, str(SDK))

from altirra_bridge import AltirraBridge  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
XEX = ROOT / "xex" / "River Raid (1983)(Activision)(US)[a3].xex"
TOKEN = Path(tempfile.gettempdir()) / "altirra-bridge-12200.token"


def main() -> int:
    a = AltirraBridge.from_token_file(str(TOKEN))
    a.config("video", "ntsc")
    a.fresh()
    a.boot(str(XEX))
    a.frame(340)
    a.clock()
    for label, start in [("attract", False)]:
        print(label, json.dumps(a.antic()))
        dl = a.peek(0x3F00, 0xC0)
        print("dl", " ".join("%02X" % b for b in dl))
        print("hud1000", " ".join("%02X" % b for b in a.peek(0x1000, 0x78)))
        print("zp", " ".join("%02X" % b for b in a.peek(0x00, 0x80)))
        a.screenshot(path=str(Path(r"C:\Users\chm2\AppData\Local\Temp\opencode") / "hudstate.png"))
    a.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
