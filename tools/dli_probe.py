#!/usr/bin/env python3
"""Catch the River Raid DLI sprite-write loop and record beam position/registers."""
from __future__ import annotations

import json
import sys
import tempfile
import time
from pathlib import Path

SDK = Path(r"F:\Emu\Atari800\Altirra\AlitrraBridge\sdk\python")
if str(SDK) not in sys.path:
    sys.path.insert(0, str(SDK))

from altirra_bridge import AltirraBridge  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
XEX = ROOT / "xex" / "River Raid (1983)(Activision)(US)[a3].xex"
TOKEN = Path(tempfile.gettempdir()) / "altirra-bridge-12200.token"


def barrier(a: AltirraBridge, frames: int) -> None:
    if frames:
        a.frame(frames)
        a.clock()


def main() -> int:
    a = AltirraBridge.from_token_file(str(TOKEN))
    a.config("video", "ntsc")
    a.fresh()
    a.boot(str(XEX))
    barrier(a, 340)

    bp = a.bp_set(0xB5CD, condition="x = 93")
    print("bp", bp)
    a.frame(1)
    time.sleep(0.5)
    a.pause()
    print("regs", json.dumps(a.regs()))
    print("antic", json.dumps(a.antic()))
    print("bp_list", json.dumps(a.bp_list()))
    print("history", json.dumps(a.history(6), indent=1))
    print("peek 0EBC", a.peek(0x0EBC, 0x40).hex())
    print("peek 0EFC", a.peek(0x0EFC, 0x40).hex())
    print("peek 0F3C", a.peek(0x0F3C, 0x40).hex())
    a.bp_clear_all()
    a.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
