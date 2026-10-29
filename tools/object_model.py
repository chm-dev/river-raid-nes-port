#!/usr/bin/env python3
"""Python translation of the Atari object spawner ($AB80-$AE96).

`spawn_step(zp, obj)` takes the zero page and the $0500-$053F object arrays as
they are at $AB80 (the first instruction after the row renderer) and returns
the state after the spawn block ran.  tools/object_validate.py replays a live
Altirra trace (tools/atari_object_trace.py) through it.

Object arrays: type $0500+X, aux $050B+X, X-pos $0516+X, size $0521+X
(`$052D` doubles as the generator's margin flag), Y $0C+X, status $42+X.
"""

from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
_CART = (ROOT / "assets" / "atari" / "river_raid_cart_a000_bfff.bin").read_bytes()


def rom(addr: int, n: int = 1) -> bytes:
    return _CART[addr - 0xA000: addr - 0xA000 + n]


SPAWN_TYPES = rom(0xBB1C, 8)
SHAPE_H = rom(0xBB60, 16)      # $BB60: rows - 1 per aux
SHAPE_SIZE = rom(0xBBA2, 16)   # $0521 value per aux (SIZEP3)

TYPE, AUX, XPOS, SIZE = 0x0500, 0x050B, 0x0516, 0x0521
CB = 0x2E                      # NTSC $CB; PAL is $2F


class Done(Exception):
    pass


TRACE: list = []


def spawn_step(zp_in: bytes, obj_in: bytes) -> tuple[list[int], list[int]]:
    z = list(zp_in)
    o = list(obj_in)          # index = address - $0500

    # ---- $AB80-$AB96: age everything -------------------------------------
    z[0xC2] = (z[0xC2] + 1) & 255
    for i in range(0x0C, 0x17):
        z[i] = (z[i] + 1) & 255
    wrapped = z[0x16] == 0

    # ---- $AB9A-$ABDF: shift when slot 10 wrapped ---------------------------
    if wrapped and z[0x4D] < 0x0B:
        x = 9
        while True:
            z[0x0D + x] = z[0x0C + x]
            o[XPOS - 0x500 + 1 + x] = o[XPOS - 0x500 + x]
            z[0x43 + x] = z[0x42 + x]
            o[TYPE - 0x500 + 1 + x] = o[TYPE - 0x500 + x]
            o[AUX - 0x500 + 1 + x] = o[AUX - 0x500 + x]
            o[SIZE - 0x500 + 1 + x] = o[SIZE - 0x500 + x]
            x -= 1
            if x < 0 or x < z[0x4D]:
                break
        z[0x33] ^= 1
        z[0x4D] += 1
        if z[0x51] < 0x0B:
            z[0x51] += 1
        if z[0x4E] < 0x0B:
            z[0x4E] += 1

    def finish_dec():
        z[0x32] = (z[0x32] - 1) & 255
        raise Done

    try:
        # ---- $ABE1: object under construction ----------------------------
        if z[0x4E] < 0x0B:
            y = (z[0x32] - 1) & 255
            if y & 0x80:
                z[0x4E] = 0x0B
            else:
                finish_dec()
        # ---- $ABF3: spawn attempt every 16 rows --------------------------
        if z[0x2F] & 0x0F:
            raise Done
        x = z[0x4D]
        if x == 0:
            raise Done
        x -= 1
        z[0x4D] = z[0x4E] = x
        z[0xB8] = 0
        z[0x42 + x] = z[0x23]
        a = 0
        c = 0
        y = 0

        def place(y: int, a: int) -> None:
            """$AD37-$AE92: store the object."""
            slot = z[0x4E]
            TRACE.append(("place", y, a))
            a = (a + 0x20) & 255
            z[0xCA] = a
            a = z[0x52]
            if o[0x2D] & 0x80:            # $052D bit 7
                if a < 0x18:
                    a = 0x18
                if a >= 0x28:
                    a = 0x28
            a = (a + z[0xCA]) & 255
            o[XPOS - 0x500 + slot] = a
            o[TYPE - 0x500 + slot] = y
            o[AUX - 0x500 + slot] = y
            if y == 8:
                if (z[0x51] >= 0x0B and z[0x5D] < 0x0E and
                        (z[0xAC] & 0x70) == 0 and z[0x60] >= 0x0D):
                    o[TYPE - 0x500 + slot] = 0x0A
                    o[AUX - 0x500 + slot] = 0x0A
                    z[0xC1] = 0
                    z[0x51] = slot
            z[0x32] = (SHAPE_H[y] + 4) & 255
            o[SIZE - 0x500 + slot] = SHAPE_SIZE[y]
            z[0x0C + slot] = (0x61 - z[0x32]) & 255
            finish_dec()

        level = z[0x60]
        if z[0x23] & 0x80:                       # bridge row: object $0E
            z[0x0B] = (z[0x23] & 4) | 0xD2
            z[0xB8] = 7
            place(0x0E, (z[0x54] - 6) & 255)

        y = 1
        if z[0x2F] != 0:
            a = (level - 8) & 255
            if a & 0x80:
                place(1, a)
            if a < 0x20 and ((a << 3) & 255) < z[0xAB]:
                place(1, a)
            real = True
        else:
            y = 0
            if ((z[0x5D] + z[0x5A]) & 255) >= 0x12:
                place(0, 0)
            m = min(level, CB)
            a = (((0x3F - m) & 255) << 1) & 255
            if a < z[0xAB]:
                real = True
            else:
                if z[0xAA] & 0x40:
                    place(0, 0)
                y = 0x0F
                real = False

        if real:
            y = SPAWN_TYPES[z[0xAB] & 7]
            if y == 7 and level < 5:
                y = 0x0D
            elif y == 7:
                pass
            if y == 0x0C:
                if z[0x2F] != 0 or level < 3:
                    y = 8
            if y == 0x0D:
                z[0xB8] = 5

        # ---- $AC8C: width logic ------------------------------------------
        if z[0x5F] != z[0x2C]:
            b3 = max(z[0x5F], z[0x2C])
        else:
            b3 = z[0x5F]
            if level & 1:
                a = 0x37 if z[0xB8] else 0x3F
                b1 = (a - z[0xB5]) & 255
                a = ((z[0xAA] & 0x3F) + z[0xB5]) & 255
                if a >= b1:
                    a = b1
                a = (a + z[0x54]) & 255
                if z[0xAA] & 0x80 and y != 0x0F:
                    z[0x42 + z[0x4E]] |= 8
                place(y, a)
        z[0xB3] = b3

        island = bool(z[0x2D] & 0x80)
        x = 0x0D if island else 0x0E
        if x < b3 and y != 7:
            place(1, a)
        if z[0x5D] - 1 == 0:
            z[0xB5] = (z[0xB5] + 2) & 255
            ec = True
        else:
            x = 0x0A if island else 0x0D
            ec = x < b3
        if ec and y == 0x0D:
            z[0xB8] = 0
            y = 8

        # ---- $ACF6: X offset ----------------------------------------------
        aa = z[0xAA]
        t = (b3 << 2)
        c = (t >> 8) & 1
        a = t & 255
        side = None   # label to continue at

        def adc(a, v, c):
            t = a + v + c
            return t & 255, t >> 8

        def sbc(a, v, c):
            t = a - v - (1 - c)
            return t & 255, 1 if t >= 0 else 0

        label = "AD10"
        if a != 0 and island:
            a ^= 0xFF
            a, c = adc(a, 0x41, c)
            z[0x37] = aa
            if aa & 0x80:
                a ^= 0xFF
                a, c = adc(a, 0x80, c)
                label = "AD14" if a != 0 else "AD10"
            else:
                label = "AD1D"
        TRACE.append(("acf6", label, b3, island, aa, y, a, c))
        while True:
            if label == "AD10":
                label = "AD19" if aa & 0x80 else "AD14"
            if label == "AD14":
                a, c = adc(a, z[0xB5], 1)
                if a != 0:
                    place(y, a)
                label = "AD19"
            if label == "AD19":
                a ^= 0xFF
                a, c = adc(a, 0x81, c)
                label = "AD1D"
            if label == "AD1D":
                c = 1 if y >= 0x0F else 0
                a, c = sbc(a, 8, c)
                a, c = sbc(a, z[0xB5], c)
                if z[0xB8]:
                    a, c = sbc(a, 8, c)
                if y != 0x0F:
                    z[0x42 + z[0x4E]] |= 8
                place(y, a)
    except Done:
        pass
    return z, o
