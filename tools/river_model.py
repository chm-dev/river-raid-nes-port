#!/usr/bin/env python3
"""Reference model of River Raid's Atari river row generator.

This is a direct translation of the 6502 code at $A6DE-$AB7E plus the tables
it reads from the cartridge.  It is used to validate the C port and to study
the tile set the procedural rows need.

The state names mirror the original zero-page variables:

    52 left bank X        53 right width      54 row phase
    2D fork/island flags  36 feature flags    2F row counter
    5A section timer      5D level countdown  5F island width
    60 level              AA/AB/AC LFSR       B4 row byte offset
    B5 margin              C3..C9 temps        BD/BC/BE feature

Run `python tools/river_model.py` to print a short generated sample.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CART = ROOT / "assets" / "atari" / "river_raid_cart_a000_bfff.bin"
ROM = CART.read_bytes()


def rom(a: int, n: int) -> bytes:
    return ROM[a - 0xA000 : a - 0xA000 + n]


BANK_MAIN = rom(0xB800, 16)
BANK_ISLAND = rom(0xB810, 16)
WALK_BA8F = rom(0xBA8F, 32)
DEFAULTS_BAE1 = rom(0xBAE1, 0x13)
# The ROM tables are contiguous; the original can index past the nominal
# 22/23 entries when min($5F,$2C) is large at level >= 10, so keep the bytes
# that follow.  This reproduces the original's out-of-range reads instead of
# raising an IndexError.
WIDTHS_BBB2 = rom(0xBBB2, 26)
TYPE_LFSR_BBCC = rom(0xBBCC, 8)
TYPE_MARGIN_BBD4 = rom(0xBBD4, 33)
LENGTH_BBEB = rom(0xBBEB, 11)
TPL_STREAM = rom(0xBC00, 0x2F0)  # templates live in $BC00-$BEEE (width 10 x 30 rows)
LEVEL_DESC = rom(0xB494, 0x18)
SCORE = rom(0xBB0B, 17)


def tpl_ptr(width: int) -> int:
    """16-bit LE pointer at $BBF0 + 2*width (relative to $BC00)."""
    a = 0xBBF0 + 2 * width
    lo, hi = ROM[a - 0xA000], ROM[a - 0xA000 + 1]
    return lo | (hi << 8)


def tpl_bytes(width: int, length: int) -> bytes:
    p = tpl_ptr(width) - 0xBC00
    return TPL_STREAM[p : p + width * length]


# Template widths drawn with black bodies on the NES: the large hills that carry
# blue highlights.  Small hills (3, 6, 7) keep the brown + black shades.
NES_BLACK_HILLS = (4, 5, 8, 9, 10)


def nes_edge_black(b: int) -> int:
    """NES recolour: turn pixel value 2 (outline, near-black on the Atari) into
    value 0 (black backdrop) so value 2 can be the hills' brown.  Used for
    bank patterns and 0xAA edge fills when `nes_edges` is set."""
    hi = b & 0xAA
    lo = (b << 1) & 0xAA
    return b & ~(hi & ~lo) & 0xFF


def _b(x: int) -> int:
    return x & 0xFF


def _s(x: int) -> int:
    x &= 0xFF
    return x - 0x100 if x & 0x80 else x


@dataclass
class RiverState:
    nes_edges: bool = False   # NES recolour of outline pixels (see nes_edge_black)
    left: int = 0x20          # $52
    width: int = 0x14         # $53
    phase: int = 0x36         # $54
    rowcount: int = 0         # $2F
    island_flags: int = 0     # $2D
    feature: int = 0          # $36
    section_timer: int = 1    # $5A
    countdown: int = 0x11     # $5D
    island_width: int = 0x0D  # $5F
    margin: int = 0           # $2C
    margin_flag: int = 0      # $052D
    level: int = 1            # $60
    flags23: int = 0x84       # $23
    lfsr_a: int = 0xE3        # $AA
    lfsr_b: int = 0xD4        # $AB
    lfsr_c: int = 0xF7        # $AC
    seeds: list[int] = field(default_factory=lambda: [0xE3, 0xD4, 0xF7, 0x20])
    saved: list[int] = field(default_factory=lambda: [0, 0, 0, 0])
    b4: int = 0
    b5: int = 6
    c3: int = 0
    c4: int = 0
    c5: int = 0
    c8: int = 0
    c9: int = 0
    bd: int = 0
    bc: int = 0
    be: int = 0
    ba: int = 0
    bb: int = 0xBC
    b7: int = 0
    b1: int = 0
    hit_flash: int = 0
    obj_count: int = 0        # $4E
    obj_aux: bytes = bytes(11)
    obj_x: bytes = bytes(11)
    obj_flags: bytes = bytes(11)
    v37: int = 0
    x3a: int = 0
    x40: int = 0

    # ------------------------------------------------------------------ #
    def lfsr_step(self) -> None:
        a = (((self.lfsr_b << 5) & 0xFF) ^ self.lfsr_b) & 0xFF
        carry = (a >> 7) & 1
        # ROL $AA
        new_a = ((self.lfsr_a << 1) | carry) & 0xFF
        carry = (self.lfsr_a >> 7) & 1
        # ROL $AC
        new_c = ((self.lfsr_c << 1) | carry) & 0xFF
        carry = (self.lfsr_c >> 7) & 1
        # ROL $AB
        new_b = ((self.lfsr_b << 1) | carry) & 0xFF
        self.lfsr_a, self.lfsr_b, self.lfsr_c = new_a, new_b, new_c

    def level_advance(self) -> None:
        x = self.level
        if x >= 0x50:
            x = 0x4E
        self.saved = self.seeds[:]
        self.seeds = [self.lfsr_a, self.lfsr_b, self.lfsr_c, self.left]
        self.level = _b(x + 1)

    # ------------------------------------------------------------------ #
    def _width_select(self) -> None:
        """$A781-$A7DC: choose the island-width target for this section."""
        # $A76A: the last section of a level takes the bridge/fork path
        # before the level-parity width choice.
        if self.countdown - 1 == 0:
            self.island_flags = 0
            self.island_width = 0x0D
            if self.level & 1:
                self.margin_flag = _b(self.margin_flag - 1)
            self.section_timer = 2
            self._set_target(0x36)
            return

        if self.level & 1:
            self.island_width = 7
        else:
            if self.countdown == 2:
                # $A78A-$A791: DEX runs a second time here, so this test is
                # on $5D-2 (the section just before the bridge section).
                if self.island_flags == 0xC0:
                    self.island_flags = 0x80
                    self.island_width = 0
                    self._set_target()
                    return
                self.island_flags = 0
            else:
                a = _b((self.island_flags << 1) ^ self.island_flags)
                if a & 0x80:
                    a = 0xC0
                    if not (self.island_flags & 0x40):
                        a = 0
                    self.island_flags = a
                elif self.lfsr_a & 0x30:
                    pass
                else:
                    a = self.island_flags & 0x80
                    if not a:
                        a = 0x40
                    self.island_flags = a
                    self.island_width = 0
                    self._set_target()
                    return

            y = 0x0F
            a = self.lfsr_a & 0x0F
            if a < 2:
                a += 2
            if self.island_flags & 0x80:
                y -= 1
            if self.level < 0x14:
                y -= 1
            if self.b5 != 0:
                y = 8
            self.b1 = y
            if a >= y:
                a = y
            self.island_width = a
        self._set_target()

    def _set_target(self, target: int | None = None) -> None:
        if target is None:
            target = _b(self.island_width << 2)
        self.section_timer = 2
        self._target = target & 0xFF
        # $A7E6-$A7F9
        diff = (self._target - self.phase) & 0xFF
        if diff == 0:
            self._v35 = 0
        elif self._target >= self.phase:
            self._v35 = 2 if diff >= 0x20 else 1
        else:
            self._v35 = 0xFF if diff >= 0xE0 else 0xFE

    def _feature_select(self) -> None:
        """$A96C-$A9D5."""
        if (self.flags23 & 0x80) or self.rowcount != 0 or not (self.lfsr_c & 0x20):
            return
        if self.level & 1:
            x = self.lfsr_a & 0x07
        else:
            x = min(self.island_width, self.margin)
            if x < 2:
                return
            if self.island_flags & 0x80:
                if x < 5:
                    return
                # ADC #$0A with the carry left set by CPX #$05.
                x += 0x0A + (1 if x >= 5 else 0)
        self.bd = TYPE_LFSR_BBCC[x] if (self.level & 1) else TYPE_MARGIN_BBD4[x]
        self.bc = WIDTHS_BBB2[x]
        self.be = LENGTH_BBEB[self.bc]
        y = self.left
        a = self.lfsr_c & 0x80
        if a:
            flip = y < 0x18
        else:
            flip = y >= 0x29
        if flip:
            a ^= 0x80
        self.feature = _b(a + 1)
        self.ba = tpl_ptr(self.bc) & 0xFF
        self.bb = (tpl_ptr(self.bc) >> 8) & 0xFF

    # ------------------------------------------------------------------ #
    def _render(self) -> bytes:
        """$AA7A-$AB7E."""
        row = bytearray([0x55] * 48)
        p = 0
        b4 = 0
        hit = self.hit_flash != 0
        island = bool(self.island_flags & 0x80)

        dbg = []

        def fill(y: int, value: int, edge: bool = False) -> None:
            # AFF4: write Y+1 bytes but advance the destination by only Y,
            # so a following pattern overwrites the last filled byte.
            nonlocal p, b4
            # Y is an 8-bit register: wrap, then BPL/BMI on bit 7 skips the fill.
            y &= 0xFF
            if y & 0x80:
                return
            if not edge:
                if hit:
                    value = 0xAA
            if self.nes_edges and value == 0xAA:
                value = 0
            row[p : p + y + 1] = bytes([value]) * (y + 1)
            p += y
            b4 += y
            dbg.append(("fill", y, p, b4))

        def copy_template() -> None:
            nonlocal p, b4
            t = tpl_bytes(self.bc, self.be)
            off = (self.bb << 8 | self.ba) - 0xBC00
            chunk = TPL_STREAM[off : off + self.bc]
            if self.nes_edges and self.bc in NES_BLACK_HILLS:
                chunk = bytes(nes_edge_black(v) for v in chunk)
            row[p : p + self.bc] = chunk
            p += self.bc
            b4 += self.bc
            # B01A advances the template pointer by $BC.
            addr = ((self.bb << 8) | self.ba) + self.bc
            self.ba = addr & 0xFF
            self.bb = (addr >> 8) & 0xFF

        def pat(index: int, flash: bool = False) -> None:
            nonlocal p, b4
            if flash and hit:
                index = _b(index + 0x10)
            table = BANK_MAIN if index < 0x10 else BANK_ISLAND
            off = index & 0x0F
            seg = table[off : off + 2]
            if self.nes_edges:
                seg = bytes(nes_edge_black(v) for v in seg)
            row[p : p + 2] = seg
            p += 2
            b4 += 2
            dbg.append(("pat", index, off, p, b4))

        # --- left bank and left-side feature ----------------------------- #
        x = self.left if island else _b(self.left + self.phase)
        y = (x >> 2) - 1
        if (not island) and (self.feature & 0x80) and (self.rowcount < self.be):
            ysave = y
            y = self.bd
            fill(y, 0x55)
            copy_template()
            y = ysave - self.bc - self.bd
        fill(y, 0x55)
        pat((x & 3) * 4, flash=True)

        # --- island / right bank ----------------------------------------- #
        a = self.width
        if island:
            if self.phase < 6:
                a = _b(self.width + self.phase + self.phase)
            else:
                b7 = x
                a = _b((self.width >> 1) + b7)
                x = a
                y = (a >> 2) - b4
                fill(y, 0xFF)
                idx = (x & 3) * 4 + 2
                if self.phase < 8:
                    idx += 0x10
                pat(idx)
                b7 = x
                a = _b((self.phase << 1) + b7)
                x = a
                y = (a >> 2) - b4 - 1
                if self.phase < 8:
                    fill(y, 0xAA, edge=True)
                    pat((x & 3) * 4 + 0x10)
                else:
                    if self.feature and (self.rowcount < self.be):
                        b7 = _b(self.bd - b4)
                        y = y - b7 - self.bc
                        ysave = y
                        y = b7
                        fill(y, 0x55)
                        copy_template()
                        y = ysave
                    fill(y, 0x55)
                    # $AB2E falls into B03B with the value returned by the
                    # last land fill: main-table index, no island offset.
                    pat((x & 3) * 4)
                a = self.width >> 1

        b7 = x
        a = _b(a + b7)
        x = a
        y = (a >> 2) - b4
        fill(y, 0xFF)
        pat((x & 3) * 4 + 2, flash=True)

        # --- right-side feature and right land --------------------------- #
        y = 0x30 - b4
        if (not island) and self.feature and not (self.feature & 0x80) and (self.rowcount < self.be):
            y = y - self.bd - self.bc
            y += 1
            fill(y, 0x55)
            copy_template()
            y = self.bd - 1
        fill(y, 0x55)
        self.debug = dbg + [("end", p, b4, len(row))]
        return bytes(row)

    # ------------------------------------------------------------------ #
    def step(self) -> bytes:
        """One row: state update ($A711-$AA7A) then render."""
        # $A711: $B5 = 6 for levels < 9, else 0
        self.b5 = 6 if self.level < 9 else 0
        self.rowcount = _b(self.rowcount + 1)

        if self.rowcount >= 0x20:
            self.rowcount = 0
            self.feature = 0
            self.margin_flag = 0
            self.flags23 = (self.flags23 & 0x04) | 0x10
            self.margin = self.island_width
            self.section_timer = _b(self.section_timer - 1)
            if self.section_timer == 0:
                self.countdown = _b(self.countdown - 1)
                if self.countdown == 0:
                    self.level_advance()
                    self.countdown = 0x10
                self.lfsr_step()
                self._width_select()
            else:
                if self.countdown - 1 == 0:
                    self.margin_flag = 0
                    self.flags23 = 0x80 if (self.level & 1) else 0x84
                self.lfsr_step()
        else:
            if not (self.flags23 & 0x80):
                self.lfsr_step()

        # $A7FB target movement
        if getattr(self, "_v35", 0):
            v = _s(self._v35)
            # A7FB: move only while the phase is still approaching the target.
            if (v > 0 and self.phase < self._target) or (v < 0 and self.phase > self._target):
                self.phase = _b(self.phase + v)
                self.width = _b(self.width - 2 * v)
            else:
                self._v35 = 0

        # $A826
        if self.flags23 & 0x80:
            self.c3 = 0
            self.left = _b(self.left + WALK_BA8F[self.rowcount])
        else:
            skip_clamp = False
            if self.margin_flag:
                if self.left < 0x18:
                    self.left += 1
                    skip_clamp = True
                elif self.left >= 0x29:
                    self.left -= 1
                    skip_clamp = True
            if not skip_clamp:
                # $A84F-$A887 bank limits
                if self.level < 3:
                    self.c4, self.c5 = 0x1C, 0x24
                elif self.level < 0x0A or not (self.level & 1) or self.margin_flag:
                    # $A85F-$A86D: even levels, odd levels below 10 and rows with
                    # the margin flag set keep the narrow limits.
                    self.c4, self.c5 = 0x19, 0x28
                else:
                    # $A871: odd level >= 10 starts wide (0D/34, 01/40 from 21).
                    self.c4, self.c5 = (0x0D, 0x34) if self.level < 0x14 else (0x01, 0x40)
                    if self.feature:
                        if self.feature & 0x80:
                            self.c4 = 0x19
                        else:
                            self.c5 = 0x28
                # $A889 direction logic
                nib = self.lfsr_c & 0x0F
                if self.c3 == 0:
                    self.c3 = 0x01 if nib >= 8 else 0xFF
                    self._clamp_decision(nib)
                elif nib < 8:
                    pass
                elif nib == 0x0D:
                    self.c3 ^= 0xFE
                    self._clamp_decision(nib)
                elif nib == 0x0F:
                    x = 0x0E - (1 if self.island_flags & 0x80 else 0)
                    if not (x < self.island_width or x < self.margin):
                        self._clamp_decision(nib)
                else:
                    self._clamp_decision(nib)

        self._feature_select()
        row = self._render()
        if self.hit_flash:
            self.hit_flash -= 1
        self.x3a = _b(self.x3a + 1)
        self.x40 = _b(self.x40 + 1)
        return row

    def _clamp_decision(self, nib: int) -> None:
        self.c9 = max(self.phase, self._target)
        self.c8 = _b((0x40 - self.c9) << 1)
        x = self.obj_count
        if self.c3 & 0x80:
            # move left
            if self.left < self.c4:
                self._flip()
                return
            if x >= 0x0B or self.obj_aux[x] < 8:
                self.left = _b(self.left - 1)
                return
            a = self.left
            if self.island_flags & 0x80:
                if self.v37 >= 0:
                    a = _b(a + (self.c8 >> 1))
                else:
                    a = _b(a + 0x7F)
                    if a == 0:
                        a = _b(self.left + (self.c8 >> 1))
            else:
                a = _b(a + self.c8 + self.c9)
            self.b7 = a
            a = _b(self.obj_x[x] - 0x17 + self.b5)
            if self.obj_flags[x]:
                a = _b(a + 8)
            if a >= self.b7:
                self._flip()
            else:
                self.left = _b(self.left - 1)
        else:
            # move right
            if self.left >= self.c5:
                self._flip()
                return
            if x >= 0x0B or self.obj_aux[x] < 8:
                self.left = _b(self.left + 1)
                return
            a = self.left
            if self.island_flags & 0x80:
                if self.v37 >= 0:
                    a = _b(a + self.c9 + 2)
                else:
                    a = _b(a + 0x3F + self.c9)
                    if a == 0:
                        a = _b(self.left + self.c9 + 2)
            else:
                a = _b(a + self.c9 + 2)
            self.b7 = a
            a = _b(self.obj_x[x] - 0x20 - self.b5)
            if a >= self.b7:
                self.left = _b(self.left + 1)
            else:
                self._flip()

    def _flip(self) -> None:
        self.c3 ^= 0xFE
        if self.c3 == 0:
            self.left = _b(self.left + 1)


def initial_state(skill: int = 0) -> RiverState:
    # $B494/$B498/$B49C/$B4A0/$B4A4/$B4A8 are parallel tables indexed by
    # (skill >> 1), not a contiguous 6-byte record.
    d = [LEVEL_DESC[skill + 4 * i] for i in range(6)]
    st = RiverState()
    st.left = DEFAULTS_BAE1[5]
    st.width = DEFAULTS_BAE1[6]
    st.phase = DEFAULTS_BAE1[7]
    st.section_timer = DEFAULTS_BAE1[13]
    st.countdown = DEFAULTS_BAE1[16]
    st.island_width = DEFAULTS_BAE1[18]
    st.level = d[4]
    st.seeds = [d[0], d[1], d[2], d[3]]
    st.lfsr_a, st.lfsr_b, st.lfsr_c = st.seeds[:3]
    st.left = st.seeds[3]
    st.width = 0x14
    st.phase = 0x36
    st.flags23 = 0x84 if st.level & 1 else 0x80     # $B16B-$B178: level parity
    return st


if __name__ == "__main__":
    st = initial_state()
    for i in range(40):
        row = st.step()
        print(i, "L", st.left, "W", st.width, "P", st.phase, "2F", st.rowcount,
              "rows", row.hex())
