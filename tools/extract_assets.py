#!/usr/bin/env python3
"""Extract River Raid (Atari 800) assets into binary blobs, PNG sheets, and C.

Sources
-------
* ``assets/atari/river_raid_cart_a000_bfff.bin`` - the 8 KiB cartridge.
* ``assets/atari/runtime/scroll/ram_0000_ffff.bin`` - a live RAM dump used for
  the OS-derived character set (the game copies it out of the Atari OS ROM at
  $E000-$E3FF) and for the current HUD buffer.

Everything that exists in the cartridge is read directly from the ROM.  The
runtime-only blobs are marked in the generated README.

Run:  python tools/extract_assets.py
"""

from __future__ import annotations

import json
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
ROM_PATH = ROOT / "assets" / "atari" / "river_raid_cart_a000_bfff.bin"
RAM_PATH = ROOT / "assets" / "atari" / "runtime" / "scroll" / "ram_0000_ffff.bin"
PALETTE_PATH = ROOT / "assets" / "atari" / "palette_altirra_ntsc.bin"
SCREENSHOT_PATH = ROOT / "assets" / "atari" / "runtime" / "scroll" / "screen_final.png"
OUT = ROOT / "assets" / "atari" / "extracted"
HEADER = ROOT / "src" / "atari_data.h"

ROM = ROM_PATH.read_bytes()
RAM = RAM_PATH.read_bytes()
PALETTE = PALETTE_PATH.read_bytes()


def rb(addr: int) -> int:
    """Read one byte from the cartridge image."""
    return ROM[addr - 0xA000]


def rbytes(addr: int, length: int) -> bytes:
    return ROM[addr - 0xA000 : addr - 0xA000 + length]


def pal(index: int) -> tuple[int, int, int]:
    return tuple(PALETTE[(index & 0xFE) * 3 : (index & 0xFE) * 3 + 3])  # type: ignore[return-value]


# --------------------------------------------------------------------------- #
# Constants recovered from the disassembly
# --------------------------------------------------------------------------- #

LEVEL_TABLE = 0xB494          # 6 parallel 4-entry tables (skill pairs)
SCORE_TABLE = 0xBB0B          # 16 entries, tens of points
SPAWN_TABLE = 0xBB1C          # 8 entries, object type chosen per spawn roll
REMAP_TABLE = 0xBB24          # 12 entries, type -> shape/aux
COLOR_LOW = 0xBB30            # 16 entries, low byte into $B7xx colour strips
SHAPE_LOW = 0xBB40            # 16 entries, low byte into $B9xx shapes
YOFF_TABLE = 0xBB60           # 16 entries, bottom-up shape row count - 1
HUD_SEED = 0xBB70             # 60 bytes copied to $1000 at boot
MARQUEE_TEXT = 0xB84B         # marquee source range $B84B-$B8D6 (scrolled)
PLAYER_SHAPE_PTR = 0xBACD     # 10 x 16-bit pointers, 14-byte player/turn shapes
SHOT_FREQ = 0xB4C0            # 18 entries
ENGINE_AUDC = 0xB4AC          # 4 entries
FX_AUDF = 0xB4E1              # 4 entries (explosion / beep)
FLASH_COLORS = 0xB4E5         # 20 entries, COLPF0 on bridge/flash rows
SPAWN_X = 0xBFE4              # per-slot spawn X candidates
BANK_PATTERNS = 0xB800        # 20 x 2-byte bank edge fragments
RIVER_WIDTH_TABLE = 0xBBB2    # 22 entries
RIVER_TYPE_LFSR = 0xBBCC      # 8 entries (LFSR-selected feature types)
RIVER_TYPE_MARGIN = 0xBBD4    # 23 entries (margin-selected feature types)
RIVER_TEMPLATE_LEN = 0xBBEB   # 11 entries, indexed by template width
RIVER_TEMPLATE_PTR = 0xBBF6   # 8 x 16-bit pointers into $BCxx
RIVER_TEMPLATE_DATA = 0xBC00


def extract_tables() -> dict:
    level = {}
    names = ["param_66", "param_67", "param_68", "param_69", "start_level", "init_counter"]
    for i in range(4):
        level[f"skill_pair_{i}"] = {
            names[j]: rb(LEVEL_TABLE + j * 4 + i) for j in range(6)
        }

    return {
        "level_descriptors": level,
        "score_tens": list(rbytes(SCORE_TABLE, 17)),
        "spawn_types": list(rbytes(SPAWN_TABLE, 8)),
        "type_remap": list(rbytes(REMAP_TABLE, 12)),
        "object_color_low": list(rbytes(COLOR_LOW, 16)),
        "object_shape_low": list(rbytes(SHAPE_LOW, 16)),
        "object_y_offset": list(rbytes(YOFF_TABLE, 16)),
        "hud_seed": list(rbytes(HUD_SEED, 60)),
        "shot_frequencies": list(rbytes(SHOT_FREQ, 18)),
        "engine_audc": list(rbytes(ENGINE_AUDC, 4)),
        "fx_auf": list(rbytes(FX_AUDF, 4)),
        "flash_colors": list(rbytes(FLASH_COLORS, 20)),
        "spawn_x": list(rbytes(SPAWN_X, 16)),
        "bank_patterns_main": list(rbytes(BANK_PATTERNS, 16)),
        "bank_patterns_island": list(rbytes(BANK_PATTERNS + 16, 16)),
        "river_width": list(rbytes(RIVER_WIDTH_TABLE, 22)),
        "river_type_lfsr": list(rbytes(RIVER_TYPE_LFSR, 8)),
        "river_type_margin": list(rbytes(RIVER_TYPE_MARGIN, 23)),
        "river_template_len": list(rbytes(RIVER_TEMPLATE_LEN, 11)),
        "river_template_ptrs": [
            rb(RIVER_TEMPLATE_PTR + i) | (rb(RIVER_TEMPLATE_PTR + i + 1) << 8)
            for i in range(0, 16, 2)
        ],
        "river_template_bytes": list(rbytes(RIVER_TEMPLATE_DATA, 0x200)),
        "marquee_range": {"start": 0xB84B, "end": 0xB8D6},
        "marquee_text": decode_atari_text(rbytes(MARQUEE_TEXT, 0xB8D7 - 0xB84B)),
    }


def decode_atari_text(data: bytes) -> str:
    """Decode Atari internal character codes (ATASCII = code + 0x40)."""
    out = []
    for b in data:
        if b == 0:
            out.append(" ")
        elif 1 <= b <= 0x3F:
            out.append(chr(b + 0x40))
        else:
            out.append("?")
    return "".join(out)


# --------------------------------------------------------------------------- #
# Charset
# --------------------------------------------------------------------------- #

def extract_charset() -> None:
    font = RAM[0x0600:0x0A00]
    (OUT / "charset.bin").write_bytes(font)

    # Patch tables applied to the OS copy, for provenance.
    patches = {
        "06c0_from_beee": rbytes(0xBEEE, 64),
        "0818_from_bf2e": rbytes(0xBF2E, 112),
        "08d0_from_bf9e": rbytes(0xBF9E, 24),
        "07d0_from_bfb6": rbytes(0xBFB6, 48),
    }
    for name, data in patches.items():
        (OUT / f"charset_patch_{name}.bin").write_bytes(data)

    scale = 4
    cols, rows = 16, 8
    im = Image.new("RGB", (cols * 8 * scale, rows * 10 * scale), (24, 24, 24))
    d = ImageDraw.Draw(im)
    for code in range(128):
        gx = (code % cols) * 8 * scale
        gy = (code // cols) * 10 * scale
        glyph = font[(code * 8) : (code * 8 + 8)]
        for y, bits in enumerate(glyph):
            for x in range(8):
                if bits & (0x80 >> x):
                    d.rectangle(
                        [gx + x * scale, gy + y * scale, gx + x * scale + scale - 1, gy + y * scale + scale - 1],
                        fill=(255, 255, 255),
                    )
        d.text((gx + 2, gy + 8 * scale + 1), f"{code:02X}", fill=(120, 200, 255))
    im.save(OUT / "charset.png")

    # HUD buffer: seed, live RAM, and a reference crop from the screenshot.
    (OUT / "hud_seed.bin").write_bytes(rbytes(HUD_SEED, 60))
    (OUT / "hud_runtime.bin").write_bytes(RAM[0x1000:0x103C])
    if SCREENSHOT_PATH.exists():
        shot = Image.open(SCREENSHOT_PATH)
        shot.crop((0, 172, 336, 220)).resize((672, 96), Image.NEAREST).save(OUT / "hud_reference.png")


# --------------------------------------------------------------------------- #
# Player/missile graphics
# --------------------------------------------------------------------------- #

def object_sprite(aux: int) -> dict:
    lo = rb(SHAPE_LOW + aux)
    yoff = rb(YOFF_TABLE + aux)
    shape_addr = 0xB900 + lo
    shape = rbytes(shape_addr, yoff + 1)
    color_lo = rb(COLOR_LOW + aux)
    color_addr = 0xB700 + color_lo
    colors = rbytes(color_addr, yoff + 1)
    return {
        "aux": aux,
        "shape_addr": shape_addr,
        "shape_len": yoff + 1,
        "shape_bottom_up": list(shape),
        "color_addr": color_addr,
        "color_low": color_lo,
        "colors_bottom_up": list(colors),
        "color_is_data": color_addr < 0xB740,
    }


def render_shape(rows_top: list[int], colors_top: list[int], scale: int = 6) -> Image.Image:
    w = 8
    h = max(1, len(rows_top))
    im = Image.new("RGB", (w * scale, h * scale), (0, 0, 0))
    p = im.load()
    for y, bits in enumerate(rows_top):
        c = pal(colors_top[y]) if y < len(colors_top) else (255, 255, 255)
        for x in range(8):
            if bits & (0x80 >> x):
                for yy in range(scale):
                    for xx in range(scale):
                        p[x * scale + xx, y * scale + yy] = c
    return im


def extract_sprites() -> None:
    sprites = [object_sprite(a) for a in range(16)]
    (OUT / "sprites").mkdir(parents=True, exist_ok=True)

    # Flat binary: for each aux, 24 shape bytes + 24 colour bytes (bottom-up).
    blob = bytearray()
    for s in sprites:
        shape = bytes(s["shape_bottom_up"]) + bytes(24 - s["shape_len"])
        colors = bytes(s["colors_bottom_up"]) + bytes(24 - s["shape_len"])
        blob += shape + colors
    (OUT / "sprites" / "objects.bin").write_bytes(bytes(blob))
    (OUT / "sprites" / "objects.json").write_text(json.dumps(sprites, indent=1), encoding="utf-8")

    # Player ship shapes: 10 pointers x 14 bytes.
    ptrs = []
    for i in range(10):
        ptrs.append(rb(PLAYER_SHAPE_PTR + i * 2) | (rb(PLAYER_SHAPE_PTR + i * 2 + 1) << 8))
    player_shapes = [rbytes(ptr, 14) for ptr in ptrs]
    (OUT / "sprites" / "player.bin").write_bytes(b"".join(player_shapes))
    (OUT / "sprites" / "player.json").write_text(
        json.dumps({"pointers": ptrs, "shapes": [list(s) for s in player_shapes]}, indent=1),
        encoding="utf-8",
    )

    # The PNG sheets are rendered by render_sprite_sheets.py from the JSON
    # written above (2 px/bit, readable labels, data-coloured entries marked).

    # Fuel gauge: 7 bytes written to P2/P3 ($FF / $FC), plus missile copy.
    gauge = {
        "p2_0edb": [0xFF] * 7,
        "p3_0fdb": [0xFC] * 7,
        "missile_0bcc": [0x0C] * 7,
        "note": "7 scanlines at PM offsets $DB..$E1 (scanlines 211..217)",
    }
    (OUT / "sprites" / "gauge.json").write_text(json.dumps(gauge, indent=1), encoding="utf-8")

    # Marquee colour strip colours ($B48D..$B493, read by DLI part 2).
    (OUT / "sprites" / "marquee_colors.bin").write_bytes(rbytes(0xB48D, 7))


# --------------------------------------------------------------------------- #
# C header
# --------------------------------------------------------------------------- #

def c_array(name: str, data: list[int] | bytes, per_line: int = 16, comment: str = "") -> str:
    b = list(data)
    lines = [f"/* {comment} */" if comment else "", f"const unsigned char {name}[{len(b)}] = {{"]
    for i in range(0, len(b), per_line):
        chunk = ", ".join(f"0x{x:02X}" for x in b[i : i + per_line])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def c_array16(name: str, data: list[int], per_line: int = 8, comment: str = "") -> str:
    vals = list(data)
    lines = [f"/* {comment} */" if comment else "", f"const unsigned int {name}[{len(vals)}] = {{"]
    for i in range(0, len(vals), per_line):
        chunk = ", ".join(f"0x{x:04X}" for x in vals[i : i + per_line])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def write_header(tables: dict) -> None:
    font = RAM[0x0600:0x0A00]
    sprites = [object_sprite(a) for a in range(16)]
    flat = bytearray()
    offsets = []
    for s in sprites:
        offsets.append(len(flat))
        flat += bytes(s["shape_bottom_up"])
    color_flat = bytearray()
    color_offsets = []
    for s in sprites:
        color_offsets.append(len(color_flat))
        color_flat += bytes(s["colors_bottom_up"])

    ptrs = [rb(PLAYER_SHAPE_PTR + i * 2) | (rb(PLAYER_SHAPE_PTR + i * 2 + 1) << 8) for i in range(10)]
    player = b"".join(rbytes(p, 14) for p in ptrs)

    arrays = [
        ("atari_charset", c_array("atari_charset", font, 16, "Runtime $0600-$09FF; index = (char_code & 0x7F) * 8."), 0),
        ("atari_obj_shape", c_array("atari_obj_shape", flat, 16, "Objects, bottom-up rows, concatenated by atari_obj_shape_off[]."), 0),
        ("atari_obj_shape_off", c_array("atari_obj_shape_off", [x & 0xFF for x in offsets], 16, "Offset into atari_obj_shape[] per aux byte."), 0),
        ("atari_obj_color", c_array("atari_obj_color", color_flat, 16, "Per-row Atari colour index, bottom-up, concatenated."), 0),
        ("atari_obj_color_off", c_array("atari_obj_color_off", [x & 0xFF for x in color_offsets], 16, "Offset into atari_obj_color[] per aux byte."), 0),
        ("atari_obj_yoff", c_array("atari_obj_yoff", [s["shape_len"] - 1 for s in sprites], 16, "Shape rows - 1 per aux byte."), 0),
        ("atari_player_shape", c_array("atari_player_shape", player, 14, "10 shapes x 14 rows, top-down, COLPM2=$1E."), 0),
        ("atari_hud_seed", c_array("atari_hud_seed", tables["hud_seed"], 20, "HUD template copied to $1000 (3 x 20 mode-6 chars)."), 0),
        ("atari_marquee_raw", c_array("atari_marquee_raw", rbytes(MARQUEE_TEXT, 0xB8D7 - 0xB84B), 20, "Marquee character codes at $B84B-$B8D6."), 0),
        ("atari_level_descriptors", c_array("atari_level_descriptors", [v for p in range(4) for v in tables["level_descriptors"][f"skill_pair_{p}"].values()], 6,
                "4 skill pairs x (param_66,param_67,param_68,param_69,start_level,init_counter)."), 0),
        ("atari_score_tens", c_array("atari_score_tens", tables["score_tens"], 17, "BCD score per object aux/type byte."), 0),
        ("atari_spawn_types", c_array("atari_spawn_types", tables["spawn_types"], 8, "Object type per spawn roll."), 0),
        ("atari_type_remap", c_array("atari_type_remap", tables["type_remap"], 12, "Type remap table."), 0),
        ("atari_shot_freq", c_array("atari_shot_freq", tables["shot_frequencies"], 18, "AUDF4 values for shot sound by $50."), 0),
        ("atari_fx_auf", c_array("atari_fx_auf", tables["fx_auf"], 4, "AUDF3 values for $7E effect."), 0),
        ("atari_flash_colors", c_array("atari_flash_colors", tables["flash_colors"], 20, "COLPF0 row flash colours."), 0),
        ("atari_bank_patterns", c_array("atari_bank_patterns", tables["bank_patterns_main"], 16, "Main river bank 2-byte fragments at $B800."), 0),
        ("atari_bank_patterns_island", c_array("atari_bank_patterns_island", tables["bank_patterns_island"], 16, "Island/fork bank fragments at $B810."), 0),
        ("atari_river_widths", c_array("atari_river_widths", tables["river_width"], 22, "Template width by feature index ($BBB2)."), 0),
        ("atari_river_type_lfsr", c_array("atari_river_type_lfsr", tables["river_type_lfsr"], 8, "Feature type by LFSR low bits ($BBCC)."), 0),
        ("atari_river_type_margin", c_array("atari_river_type_margin", tables["river_type_margin"], 23, "Feature type by margin ($BBD4)."), 0),
        ("atari_river_template_len", c_array("atari_river_template_len", tables["river_template_len"], 11, "Feature end row by template width ($BBEB)."), 0),
        ("atari_river_template_off", c_array16("atari_river_template_off", [p - 0xBC00 for p in tables["river_template_ptrs"]], 8, "Offsets into atari_river_templates[] ($BBF6 pointers)."), 1),
        ("atari_river_templates", c_array("atari_river_templates", tables["river_template_bytes"], 48, "Row-fragment stream at $BC00."), 0),
        ("atari_spawn_x", c_array("atari_spawn_x", tables["spawn_x"], 16, "Candidate spawn X per slot."), 0),
    ]

    parts = [
        "/*",
        " * atari_data.h - generated by tools/extract_assets.py. Do not edit.",
        " *",
        " * River Raid (Atari 800) assets. Character set is the runtime copy at",
        " * $0600-$09FF (originally copied from the Atari OS by the game and then",
        " * patched); sprite and table data is read from the cartridge ROM.",
        " *",
        " * Screen coordinates: the Atari playfield is 48 bytes wide; bytes 4..43",
        " * are the visible 40-byte window. One playfield byte is 4 ANTIC pixels.",
        " *",
        " * Define ATARI_DATA_IMPL in exactly one .c file before including this",
        " * header to emit the arrays; all other files see extern declarations.",
        " */",
        "#ifndef ATARI_DATA_H",
        "#define ATARI_DATA_H",
        "",
        f"#define ATARI_CHARSET_SIZE {len(font)}",
        f"#define ATARI_OBJECT_COUNT {len(sprites)}",
        f"#define ATARI_PLAYER_SHAPE_COUNT {len(ptrs)}",
        "#define ATARI_PLAYER_SHAPE_LEN 14",
        "",
        "#ifdef ATARI_DATA_IMPL",
        "",
    ]
    for _, definition, _ in arrays:
        parts.append(definition)
    parts.append("#else")
    parts.append("")
    for name, _, is16 in arrays:
        typ = "unsigned int" if is16 else "unsigned char"
        parts.append(f"extern const {typ} {name}[];")
    parts += [
        "",
        "#endif",
        "#endif",
        "",
    ]
    HEADER.parent.mkdir(parents=True, exist_ok=True)
    HEADER.write_text("\n".join(parts), encoding="utf-8")
    (HEADER.parent / "atari_data.c").write_text(
        '/* Generated by tools/extract_assets.py. */\n#define ATARI_DATA_IMPL\n#include "atari_data.h"\n',
        encoding="utf-8",
    )


# --------------------------------------------------------------------------- #
# Main
# --------------------------------------------------------------------------- #

def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    assert ROM_PATH.stat().st_size == 8192
    assert RAM_PATH.stat().st_size == 0x10000
    tables = extract_tables()
    (OUT / "tables.json").write_text(json.dumps(tables, indent=1), encoding="utf-8")
    (OUT / "palette.json").write_text(
        json.dumps([list(PALETTE[i * 3 : i * 3 + 3]) for i in range(256)], indent=0), encoding="utf-8"
    )
    extract_charset()
    extract_sprites()
    write_header(tables)
    import subprocess
    import sys

    subprocess.run([sys.executable, str(ROOT / "tools" / "render_sprite_sheets.py")], check=True)

    readme = """# Extracted Atari assets

Generated by `tools/extract_assets.py` from:

* cartridge `river_raid_cart_a000_bfff.bin` (sha256 54cd6091...)
* live RAM dump `runtime/scroll/ram_0000_ffff.bin`

| File | Source | Notes |
|---|---|---|
| `charset.bin` | runtime `$0600-$09FF` | 128 glyphs, 8 bytes each; glyph index = `(code & 0x7F) * 8`. |
| `charset.png` | same | 16x8 sheet, labelled by code. |
| `charset_patch_*.bin` | ROM | Patch blocks applied over the OS copy. |
| `hud_seed.bin` | ROM `$BB70` | 60-byte HUD template copied to `$1000` (3 x 20 chars). |
| `hud_runtime.bin` | RAM `$1000-$103B` | Live HUD rows (3 x 20 bytes). |
| `hud_reference.png` | screenshot | Live HUD crop, for layout comparison. |
| `sprites/objects.png` | ROM `$B900+`, `$B700+` | Nine real P3 objects (aux 7-15), top-down, 2 px/bit, per-row colour swatches. |
| `sprites/objects_all.png` | same | All 16 aux entries; aux 0-6 are explicitly marked as not P3 sprites. |
| `sprites/objects.bin/.json` | ROM | Raw 16 x (24 shape + 24 colour) data. |
| `sprites/player.png` | ROM `$BACD` pointer table | The three flight shapes (idx 7/8/9), 14 rows, COLPM2=$1E. |
| `sprites/player_all.png` | same | All 10 player-table entries (0-6 are blank/shared effect fragments). |
| `sprites/player.bin/.json` | same | Raw 10 x 14-byte shapes. |
| `sprites/gauge.json` | code constants | Fuel gauge bars written to P2/P3/missile memory. |
| `tables.json` | ROM | Level, score, spawn, sound, river tables. |
| `river_rows_157.bin` | runtime RAM | 157 x 48 visible mode-E rows (DL order). |
| `river_reference.png` | runtime RAM | Those rows rendered with the Atari palette. |
| `river_state.json` | runtime RAM | DL segments and generator variables. |
| `full_frame_reference.png` | runtime RAM + ROM | Full 336x224 frame; validation of the display model. |

`src/atari_data.h` holds the arrays behind `ATARI_DATA_IMPL`; the generated
`src/atari_data.c` defines the macro and includes it once.

Atari colour indices in the data refer to `assets/atari/palette_altirra_ntsc.bin`
(NTSC, 256 RGB triplets).
"""
    (OUT / "README.md").write_text(readme, encoding="utf-8")
    print(f"wrote assets to {OUT}")
    print(f"wrote {HEADER} ({HEADER.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
