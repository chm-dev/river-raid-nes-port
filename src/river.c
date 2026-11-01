/*
 * river.c - Atari-accurate procedural river generator and row streamer.
 *
 * This is a direct port of the original 6502 row generator:
 *
 *   $A6DE-$A9D5  per-row section/feature/LFSR state machine
 *   $AA7A-$AB7E  row renderer (mode-E bytes)
 *
 * The validated Python reference is tools/river_model.py; a 2,600-row live
 * Altirra trace across levels 1-3 matches it exactly.  The C port uses the
 * same variable names ($52, $53, ...) so it can be checked against the
 * disassembly and the trace.
 *
 * The NES renderer at the bottom is exact: every visible 8x8 block is
 * uploaded verbatim into the playfield's own 256-tile pattern table (see
 * "Exact per-tile river cache").  The generator runs one tile row ahead of
 * the display so the pattern uploads can be spread over several frames.
 */
#include <gb/gb.h>
#include <stdint.h>
#include <string.h>

#include "config.h"
#include "atari_data.h"
#include "hud.h"
#include "river.h"
#include "entities.h"
#include "river_lut.h"

#ifdef PROF_EXPORT_STATICS
#define static                  /* ./build.ps1 -Profile: statics get .noi symbols */
#endif

/* 48-byte mode-E row.  The original's last fill writes one byte past the
 * row (it overwrites the next row's first land byte); keep one spare. */
uint8_t river_raw_row[49];

/* Interleaved 2bpp patterns for the 20 tiles of the pending tile row. */
uint8_t river_tile_pat[RIVER_TILE_COLS * 16];

/* Chosen tile ID for each column of the pending row (dedupe scratch). */
static uint8_t river_row_ids[RIVER_TILE_COLS];

/* Per-column solid flags built while converting rows: 1 = all land, 2 = all
 * water (see river_convert_row). */
static uint8_t river_col_kind[RIVER_TILE_COLS];

/* Rotating exact-pattern cache slot (TILE_RIVER_COUNT = 254 slots).  The
 * visible ring rows plus the lead row need at most ~231 (40k-row model
 * measurement, tools/gen_river_tiles_golden.py data), so a slot is never
 * reused while it is still on screen. */
static uint8_t river_alloc;

/* ------------------------------------------------------------------------- */
/* Generator state ($52-$60, $AA-$AC, $B4-$BE, $C3-$C9)                      */
/* ------------------------------------------------------------------------- */

/* The ones the object spawner (entities.c, $AB80-$AE96) reads are exported
 * through river.h. */
uint8_t rg_left;                  /* $52 */
static uint8_t rg_width;          /* $53 */
uint8_t rg_phase;                 /* $54 */
uint8_t rg_rowcount;              /* $2F */
uint8_t rg_island_flags;          /* $2D */
uint8_t rg_feature;               /* $36 */
uint8_t rg_section_timer;         /* $5A */
uint8_t rg_countdown;             /* $5D */
uint8_t rg_island_width;          /* $5F */
uint8_t rg_margin;                /* $2C */
uint8_t rg_margin_flag;           /* $052D */
uint8_t rg_level;                 /* $60 */
uint8_t river_variant;            /* start-level row of atari_level_descriptors (0..3) */
uint8_t rg_flags23;               /* $23 */
uint8_t rg_lfsr_a;                /* $AA */
uint8_t rg_lfsr_b;                /* $AB */
uint8_t rg_lfsr_c;                /* $AC */
uint8_t rg_b5;                    /* $B5 */
static uint8_t rg_c3, rg_c4, rg_c5, rg_c8, rg_c9;
static uint8_t rg_bd, rg_bc, rg_be; /* feature type / width / length */
static uint16_t rg_tpl_off;       /* offset into atari_river_templates */
static uint8_t rg_target;         /* $34 */
static int8_t rg_v35;             /* $35 */
static uint8_t rg_seeds[4];       /* $66-$69: LFSR + rg_left at the start of the current section */
static uint8_t rg_prev[4];        /* $AD-$B0: the same one section back */
static uint8_t rg_life;           /* $4C bit 2 -> bit 7, bridge counter at life start -> bits 0..6 */
static uint8_t rg_b7;

/* Object-avoidance inputs.  Phase 4 has no objects; Phase 5 fills these. */
uint8_t river_obj_count;
uint8_t river_obj_aux[1];
uint8_t river_obj_x[1];
uint8_t river_obj_flags[1];
uint8_t river_obj_v37;

static uint8_t rg_row_left;
static uint8_t rg_row_right;
static uint8_t rg_row_island_left;
static uint8_t rg_row_island_right;

/* Renderer scratch.  rg_ysave is file-scope on purpose: SDCC can alias a
 * block-local saved row value with the register used by the next call, which
 * silently shifts the island/feature overlay. */
static uint8_t rg_row_pos;
static uint8_t rg_ysave;

/* The 32-entry bank-walk table at $BA8F. */
static const uint8_t river_walk[32] = {
    0x00, 0x01, 0xFF, 0xFF, 0x00, 0x01, 0x00, 0x00,
    0x01, 0x00, 0x01, 0xFF, 0x00, 0xFF, 0xFF, 0xFF,
    0x00, 0x01, 0x01, 0x00, 0x01, 0xFF, 0xFF, 0x00,
    0x00, 0xFF, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00,
};

/* Initial values copied from $BAE1 to $4D-$5F. */
static const uint8_t river_defaults[19] = {
    0x0B, 0x0B, 0x02, 0xFF, 0x0B, 0x20, 0x14, 0x36, 0x00,
    0x01, 0x7C, 0x00, 0xFE, 0x01, 0xFF, 0xFF, 0x11, 0x00, 0x0D,
};

/* ------------------------------------------------------------------------- */
/* Collision ring                                                            */
/* ------------------------------------------------------------------------- */

uint8_t river_left_x[RIVER_ROWS];
uint8_t river_right_x[RIVER_ROWS];
uint8_t river_island_left_x[RIVER_ROWS];
uint8_t river_island_right_x[RIVER_ROWS];
uint8_t river_flags[RIVER_ROWS];

uint16_t river_distance;
uint8_t river_freeze;
uint8_t river_speed = 1u;
uint8_t river_scrolled;
uint8_t river_rows_last;                /* rows scrolled by the last river_update() */
/* Current scroll velocity in 1/32 row per frame; it ramps toward the target
 * set by river_speed at RIVER_ACCEL units per frame. */
uint8_t river_vel = RIVER_VEL_CRUISE;
/* Sub-row accumulator (0..31) and rows scrolled but not yet generated (see
 * river_update).  The generator is 8 rows ahead of the display, so up to
 * RIVER_MAX_OWED rows may be outstanding without exposing a stale row. */
static uint8_t river_acc;
static uint16_t ru_sum;
static uint8_t ru_target;
uint8_t river_owed;
#define RIVER_MAX_OWED      6u
#define RIVER_GEN_PER_PASS  4u

/* Test hooks: last generated 48-byte row and the state after it.  These are
 * exported through the .noi file so Mesen tests can compare the C port with
 * tools/river_model.py. */
#ifdef __SDCC_mos6502
#define RIVER_DEBUG_FIELDS  12u         /* what tests/nes_river.mjs reads; RAM is tight */
#else
#define RIVER_DEBUG_FIELDS  16u         /* the host check also compares the feature fields */
#endif
volatile uint8_t river_debug_state[RIVER_DEBUG_FIELDS];

static uint16_t river_step;
static uint8_t river_slot;
uint8_t river_block_index;

/* ------------------------------------------------------------------------- */
/* LFSR / level state                                                        */
/* ------------------------------------------------------------------------- */

static void river_lfsr_step(void) {
#ifdef __SDCC_mos6502
    /* $B183, as on the Atari: bit 7 of (b << 5) ^ b feeds a -> c -> b. */
    __asm
        lda     _rg_lfsr_b
        asl     a
        asl     a
        asl     a
        asl     a
        asl     a
        eor     _rg_lfsr_b
        asl     a
        rol     _rg_lfsr_a
        rol     _rg_lfsr_c
        rol     _rg_lfsr_b
    __endasm;
#else
    uint8_t a = (uint8_t)((uint8_t)(rg_lfsr_b << 5) ^ rg_lfsr_b);
    uint8_t carry = (uint8_t)((a >> 7) & 1u);
    uint8_t na, nc, nb;

    a = (uint8_t)(a << 1);
    /* The carry into ROL $AA is the bit shifted out by the ASL above,
     * already captured before the shift. */
    na = (uint8_t)((rg_lfsr_a << 1) | carry);
    carry = (uint8_t)((rg_lfsr_a >> 7) & 1u);
    nc = (uint8_t)((rg_lfsr_c << 1) | carry);
    carry = (uint8_t)((rg_lfsr_c >> 7) & 1u);
    nb = (uint8_t)((rg_lfsr_b << 1) | carry);
    rg_lfsr_a = na;
    rg_lfsr_b = nb;
    rg_lfsr_c = nc;
#endif
}

static void river_level_advance(void) {
    uint8_t x = rg_level;

    if (x >= 0x50u) {
        x = 0x4Eu;
    }
    /* $B194: the section just left becomes the "previous" checkpoint. */
    rg_prev[0] = rg_seeds[0];
    rg_prev[1] = rg_seeds[1];
    rg_prev[2] = rg_seeds[2];
    rg_prev[3] = rg_seeds[3];
    rg_seeds[0] = rg_lfsr_a;
    rg_seeds[1] = rg_lfsr_b;
    rg_seeds[2] = rg_lfsr_c;
    rg_seeds[3] = rg_left;
    rg_level = (uint8_t)(x + 1u);
}

/* $A7E6-$A7F9: signed step toward the bank target. */
static void river_set_target(uint8_t target) {
    uint8_t diff;

    rg_section_timer = 2u;
    rg_target = target;
    diff = (uint8_t)(target - rg_phase);
    if (diff == 0u) {
        rg_v35 = 0;
    } else if (target >= rg_phase) {
        rg_v35 = (diff >= 0x20u) ? 2 : 1;
    } else {
        rg_v35 = (diff >= 0xE0u) ? -1 : -2;
    }
}

/* $A781-$A7DC: choose the next section's island width / bank target. */
static void river_width_select(void) {
    uint8_t a, y;

    /* $A76A-$A77D: last section before the bridge/level transition. */
    if ((uint8_t)(rg_countdown - 1u) == 0u) {
        rg_island_flags = 0;
        rg_island_width = 0x0Du;
        if (rg_level & 1u) {
            rg_margin_flag = (uint8_t)(rg_margin_flag - 1u);
        }
        river_set_target(0x36u);
        return;
    }

    if (rg_level & 1u) {
        rg_island_width = 7u;
        river_set_target((uint8_t)(rg_island_width << 2));
        return;
    }

    /* $A78A-$A791: DEX runs twice here, so this tests $5D-2. */
    if (rg_countdown == 2u) {
        if (rg_island_flags == 0xC0u) {
            rg_island_flags = 0x80u;
            rg_island_width = 0u;
            river_set_target(0u);
            return;
        }
        rg_island_flags = 0;
    } else {
        a = (uint8_t)((uint8_t)(rg_island_flags << 1) ^ rg_island_flags);
        if (a & 0x80u) {
            a = 0xC0u;
            if (!(rg_island_flags & 0x40u)) {
                a = 0u;
            }
            rg_island_flags = a;
        } else if (rg_lfsr_a & 0x30u) {
            /* keep the current flags */
        } else {
            a = (uint8_t)(rg_island_flags & 0x80u);
            if (!a) {
                a = 0x40u;
            }
            rg_island_flags = a;
            rg_island_width = 0u;
            river_set_target(0u);
            return;
        }
    }

    y = 0x0Fu;
    a = (uint8_t)(rg_lfsr_a & 0x0Fu);
    if (a < 2u) {
        a = (uint8_t)(a + 2u);
    }
    if (rg_island_flags & 0x80u) {
        y--;
    }
    if (rg_level < 0x14u) {
        y--;
    }
    if (rg_b5) {
        y = 8u;
    }
    if (a >= y) {
        a = y;
    }
    rg_island_width = a;
    river_set_target((uint8_t)(rg_island_width << 2));
}

/* $A96C-$A9D5: select a template/feature for the new section. */
static void river_feature_select(void) {
    uint8_t x;
    uint8_t flip;

    if ((rg_flags23 & 0x80u) || rg_rowcount != 0u || !(rg_lfsr_c & 0x20u)) {
        return;
    }
    if (rg_level & 1u) {
        x = (uint8_t)(rg_lfsr_a & 0x07u);
    } else {
        x = (rg_island_width < rg_margin) ? rg_island_width : rg_margin;
        if (x < 2u) {
            return;
        }
        if (rg_island_flags & 0x80u) {
            if (x < 5u) {
                return;
            }
            /* ADC #$0A with the carry left set by CPX #$05. */
            x = (uint8_t)(x + 0x0Au + (uint8_t)(x >= 5u));
        }
    }
    rg_bd = (rg_level & 1u) ? atari_river_type_lfsr[x]
                            : atari_river_type_margin[x];
    rg_bc = atari_river_widths[x];
    rg_be = atari_river_template_len[rg_bc];
    if (rg_lfsr_c & 0x80u) {
        flip = (uint8_t)(rg_left < 0x18u);
    } else {
        flip = (uint8_t)(rg_left >= 0x29u);
    }
    {
        /* $A9B3-$A9C8 keeps only bit 7, optionally flips it, then adds 1. */
        uint8_t dir = (uint8_t)(rg_lfsr_c & 0x80u);
        if (flip) {
            dir ^= 0x80u;
        }
        rg_feature = (uint8_t)(dir + 1u);
    }
    rg_tpl_off = atari_river_template_off[rg_bc - 3u];
}

/* $A8DC-$A95D: move the left bank one pixel, with object avoidance. */
static void river_clamp_decision(void) {
    uint8_t x = river_obj_count;
    uint8_t a;

    rg_c9 = (rg_phase > rg_target) ? rg_phase : rg_target;
    rg_c8 = (uint8_t)((0x40u - rg_c9) << 1);

    if (rg_c3 & 0x80u) {
        /* move left */
        if (rg_left < rg_c4) {
            rg_c3 ^= 0xFEu;
            if (rg_c3 == 0u) {
                rg_left++;
            }
            return;
        }
        if (x >= 0x0Bu || river_obj_aux[x] < 8u) {
            rg_left--;
            return;
        }
        a = rg_left;
        if (rg_island_flags & 0x80u) {
            if ((int8_t)river_obj_v37 >= 0) {
                a = (uint8_t)(a + (rg_c8 >> 1));
            } else {
                a = (uint8_t)(a + 0x7Fu);
                if (a == 0u) {
                    a = (uint8_t)(rg_left + (rg_c8 >> 1));
                }
            }
        } else {
            a = (uint8_t)(a + rg_c8 + rg_c9);
        }
        rg_b7 = a;
        a = (uint8_t)(river_obj_x[x] - 0x17u + rg_b5);
        if (river_obj_flags[x]) {
            a = (uint8_t)(a + 8u);
        }
        if (a >= rg_b7) {
            rg_c3 ^= 0xFEu;
            if (rg_c3 == 0u) {
                rg_left++;
            }
        } else {
            rg_left--;
        }
    } else {
        /* move right */
        if (rg_left >= rg_c5) {
            rg_c3 ^= 0xFEu;
            if (rg_c3 == 0u) {
                rg_left++;
            }
            return;
        }
        if (x >= 0x0Bu || river_obj_aux[x] < 8u) {
            rg_left++;
            return;
        }
        a = rg_left;
        if (rg_island_flags & 0x80u) {
            if ((int8_t)river_obj_v37 >= 0) {
                a = (uint8_t)(a + rg_c9 + 2u);
            } else {
                a = (uint8_t)(a + 0x3Fu + rg_c9);
                if (a == 0u) {
                    a = (uint8_t)(rg_left + rg_c9 + 2u);
                }
            }
        } else {
            a = (uint8_t)(a + rg_c9 + 2u);
        }
        rg_b7 = a;
        a = (uint8_t)(river_obj_x[x] - 0x20u - rg_b5);
        if (a >= rg_b7) {
            rg_left++;
        } else {
            rg_c3 ^= 0xFEu;
            if (rg_c3 == 0u) {
                rg_left++;
            }
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Row renderer ($AA7A-$AB7E)                                                */
/* ------------------------------------------------------------------------- */

static uint8_t rf_val, rf_cnt;

#ifdef __SDCC_mos6502
/* Asm kept in its own function so the compiler cannot cache registers across it. */
static void river_fill_core(void) {
    __asm
        ldx     _rg_row_pos
        ldy     _rf_cnt
        lda     _rf_val
    1$:
        sta     _river_raw_row,x
        inx
        dey
        bne     1$
    __endasm;
}

/* Unrolled: 4 cycles a byte instead of 10. */
static void river_clear_row(void) {
    __asm
        lda     #0x55
        sta     _river_raw_row+0
        sta     _river_raw_row+1
        sta     _river_raw_row+2
        sta     _river_raw_row+3
        sta     _river_raw_row+4
        sta     _river_raw_row+5
        sta     _river_raw_row+6
        sta     _river_raw_row+7
        sta     _river_raw_row+8
        sta     _river_raw_row+9
        sta     _river_raw_row+10
        sta     _river_raw_row+11
        sta     _river_raw_row+12
        sta     _river_raw_row+13
        sta     _river_raw_row+14
        sta     _river_raw_row+15
        sta     _river_raw_row+16
        sta     _river_raw_row+17
        sta     _river_raw_row+18
        sta     _river_raw_row+19
        sta     _river_raw_row+20
        sta     _river_raw_row+21
        sta     _river_raw_row+22
        sta     _river_raw_row+23
        sta     _river_raw_row+24
        sta     _river_raw_row+25
        sta     _river_raw_row+26
        sta     _river_raw_row+27
        sta     _river_raw_row+28
        sta     _river_raw_row+29
        sta     _river_raw_row+30
        sta     _river_raw_row+31
        sta     _river_raw_row+32
        sta     _river_raw_row+33
        sta     _river_raw_row+34
        sta     _river_raw_row+35
        sta     _river_raw_row+36
        sta     _river_raw_row+37
        sta     _river_raw_row+38
        sta     _river_raw_row+39
        sta     _river_raw_row+40
        sta     _river_raw_row+41
        sta     _river_raw_row+42
        sta     _river_raw_row+43
        sta     _river_raw_row+44
        sta     _river_raw_row+45
        sta     _river_raw_row+46
        sta     _river_raw_row+47
    __endasm;
}
#endif

/* Fill y+1 bytes of the raw row at rg_row_pos with `value`, then advance the
 * position by y (the original's fills overlap by one byte). */
static void river_fill(uint8_t y, uint8_t value) {
    if (y & 0x80u) {
        return;
    }
    if (value == 0xAAu) {
        value = 0u;
    }
    rf_val = value;
    rf_cnt = (uint8_t)(y + 1u);
#ifdef __SDCC_mos6502
    /* the row clear already wrote land and the fills only move forward */
    if (value != 0x55u) {
        river_fill_core();
    }
#else
    {
        uint8_t i;
        for (i = 0; i <= y; i++) {
            river_raw_row[rg_row_pos + i] = value;
        }
    }
#endif
    rg_row_pos = (uint8_t)(rg_row_pos + y);
}

/* NES recolour: Atari pixel value 2 (outline, near black) becomes value 0
 * (black backdrop) on bank edges, so value 2 is free for the hills' brown.
 * nes_edge_black() in tools/river_model.py, for every byte. */
static const uint8_t river_black_lut[256] = {
    0x00, 0x01, 0x00, 0x03, 0x04, 0x05, 0x04, 0x07, 0x00, 0x01, 0x00, 0x03, 0x0C, 0x0D, 0x0C, 0x0F,
    0x10, 0x11, 0x10, 0x13, 0x14, 0x15, 0x14, 0x17, 0x10, 0x11, 0x10, 0x13, 0x1C, 0x1D, 0x1C, 0x1F,
    0x00, 0x01, 0x00, 0x03, 0x04, 0x05, 0x04, 0x07, 0x00, 0x01, 0x00, 0x03, 0x0C, 0x0D, 0x0C, 0x0F,
    0x30, 0x31, 0x30, 0x33, 0x34, 0x35, 0x34, 0x37, 0x30, 0x31, 0x30, 0x33, 0x3C, 0x3D, 0x3C, 0x3F,
    0x40, 0x41, 0x40, 0x43, 0x44, 0x45, 0x44, 0x47, 0x40, 0x41, 0x40, 0x43, 0x4C, 0x4D, 0x4C, 0x4F,
    0x50, 0x51, 0x50, 0x53, 0x54, 0x55, 0x54, 0x57, 0x50, 0x51, 0x50, 0x53, 0x5C, 0x5D, 0x5C, 0x5F,
    0x40, 0x41, 0x40, 0x43, 0x44, 0x45, 0x44, 0x47, 0x40, 0x41, 0x40, 0x43, 0x4C, 0x4D, 0x4C, 0x4F,
    0x70, 0x71, 0x70, 0x73, 0x74, 0x75, 0x74, 0x77, 0x70, 0x71, 0x70, 0x73, 0x7C, 0x7D, 0x7C, 0x7F,
    0x00, 0x01, 0x00, 0x03, 0x04, 0x05, 0x04, 0x07, 0x00, 0x01, 0x00, 0x03, 0x0C, 0x0D, 0x0C, 0x0F,
    0x10, 0x11, 0x10, 0x13, 0x14, 0x15, 0x14, 0x17, 0x10, 0x11, 0x10, 0x13, 0x1C, 0x1D, 0x1C, 0x1F,
    0x00, 0x01, 0x00, 0x03, 0x04, 0x05, 0x04, 0x07, 0x00, 0x01, 0x00, 0x03, 0x0C, 0x0D, 0x0C, 0x0F,
    0x30, 0x31, 0x30, 0x33, 0x34, 0x35, 0x34, 0x37, 0x30, 0x31, 0x30, 0x33, 0x3C, 0x3D, 0x3C, 0x3F,
    0xC0, 0xC1, 0xC0, 0xC3, 0xC4, 0xC5, 0xC4, 0xC7, 0xC0, 0xC1, 0xC0, 0xC3, 0xCC, 0xCD, 0xCC, 0xCF,
    0xD0, 0xD1, 0xD0, 0xD3, 0xD4, 0xD5, 0xD4, 0xD7, 0xD0, 0xD1, 0xD0, 0xD3, 0xDC, 0xDD, 0xDC, 0xDF,
    0xC0, 0xC1, 0xC0, 0xC3, 0xC4, 0xC5, 0xC4, 0xC7, 0xC0, 0xC1, 0xC0, 0xC3, 0xCC, 0xCD, 0xCC, 0xCF,
    0xF0, 0xF1, 0xF0, 0xF3, 0xF4, 0xF5, 0xF4, 0xF7, 0xF0, 0xF1, 0xF0, 0xF3, 0xFC, 0xFD, 0xFC, 0xFF
};

static const uint8_t *rt_src;
static uint8_t *rt_dst;

#ifdef __SDCC_mos6502
/* The copy loop, through zero-page pointers $FE (source) and $FC (destination),
 * set from rt_src and rt_dst by river_copy_template().  Those bytes are free:
 * the linker places SDCC's zero page at $20-$CE (see the .noi).  Asm kept in
 * its own function, as LESSONS 8 requires. */
static void river_copy_asm(void) {
    __asm
        lda     _rt_src
        sta     0xfe
        lda     _rt_src+1
        sta     0xff
        lda     _rt_dst
        sta     0xfc
        lda     _rt_dst+1
        sta     0xfd
        lda     _rg_bc
        beq     cp_done
        ldy     #0
        cmp     #4
        beq     cp_lut
        cmp     #5
        beq     cp_lut
        cmp     #8
        bcs     cp_lut
    cp_copy:
        .db     0xB1, 0xFE              ; lda (0xfe),y  (the assembler emits absolute)
        .db     0x91, 0xFC              ; sta (0xfc),y
        iny
        cpy     _rg_bc
        bne     cp_copy
        rts
    cp_lut:
        .db     0xB1, 0xFE              ; lda (0xfe),y
        tax
        lda     _river_black_lut,x
        .db     0x91, 0xFC              ; sta (0xfc),y
        iny
        cpy     _rg_bc
        bne     cp_lut
    cp_done:
        rts
    __endasm;
}

static void river_copy_template(void) {
    /* Large hills (widths 4, 5, 8, 9, 10: the ones with blue highlights) are
     * drawn black instead of brown; small hills keep brown + black.  Mirrors
     * NES_BLACK_HILLS in tools/river_model.py. */
    rt_src = atari_river_templates + rg_tpl_off;
    rt_dst = river_raw_row + rg_row_pos;
    river_copy_asm();
    rg_row_pos = (uint8_t)(rg_row_pos + rg_bc);
    rg_tpl_off = (uint16_t)(rg_tpl_off + rg_bc);
}
#else
static void river_copy_template(void) {
    uint8_t i;

    rt_src = atari_river_templates + rg_tpl_off;
    rt_dst = river_raw_row + rg_row_pos;
    if (rg_bc == 4u || rg_bc == 5u || rg_bc >= 8u) {
        for (i = 0; i < rg_bc; i++) {
            rt_dst[i] = river_black_lut[rt_src[i]];
        }
    } else {
        for (i = 0; i < rg_bc; i++) {
            rt_dst[i] = rt_src[i];
        }
    }
    rg_row_pos = (uint8_t)(rg_row_pos + rg_bc);
    rg_tpl_off = (uint16_t)(rg_tpl_off + rg_bc);
}
#endif

/* nes_edge_black() of atari_bank_patterns + atari_bank_patterns_island. */
static const uint8_t river_bank_black[32] = {
    0x00, 0xFF, 0x00, 0x55, 0x40, 0x3F, 0xC0, 0x15, 0x50, 0x0F, 0xF0, 0x05, 0x54, 0x03, 0xFC, 0x01,
    0x00, 0xFF, 0x00, 0x00, 0x00, 0x3F, 0xC0, 0x00, 0x00, 0x0F, 0xF0, 0x00, 0x00, 0x03, 0xFC, 0x00
};

static void river_pattern(uint8_t index) {
    river_raw_row[rg_row_pos] = river_bank_black[index];
    river_raw_row[rg_row_pos + 1u] = river_bank_black[(uint8_t)(index + 1u)];
    rg_row_pos = (uint8_t)(rg_row_pos + 2u);
}

#ifdef __SDCC_mos6502
/* river_render_row() for the commonest rendered row, no island and no feature:
 * land, left edge, water, right edge, land.  The land is the row clear's.  The
 * two y tests are river_fill()'s: a "negative" length writes nothing and does
 * not advance.  Host builds and tests/nes_river check it against the model. */
static void river_render_plain(void) {
    river_clear_row();
    __asm
        lda     #0
        sta     _rg_row_island_left
        sta     _rg_row_island_right
        lda     _rg_left
        clc
        adc     _rg_phase
        sta     _rg_row_left
        tay
        lsr     a
        lsr     a
        sec
        sbc     #1
        bpl     1$
        lda     #0
    1$:
        tax                         ; X = position in the row
        tya
        and     #3
        asl     a
        asl     a
        tay
        lda     _river_bank_black,y
        sta     _river_raw_row,x
        lda     _river_bank_black+1,y
        sta     _river_raw_row+1,x
        inx
        inx
        stx     _rf_cnt
        lda     _rg_row_left
        clc
        adc     _rg_width
        sta     _rg_row_right
        lsr     a
        lsr     a
        sec
        sbc     _rf_cnt
        bmi     3$
        tay                         ; y + 1 water bytes, then back to the last
        lda     #0xFF
    2$:
        sta     _river_raw_row,x
        inx
        dey
        bpl     2$
        dex
    3$:
        lda     _rg_row_right
        and     #3
        asl     a
        asl     a
        ora     #2
        tay
        lda     _river_bank_black,y
        sta     _river_raw_row,x
        lda     _river_bank_black+1,y
        sta     _river_raw_row+1,x
    __endasm;
}
#endif

#ifdef __SDCC_mos6502
static uint8_t isl_x;               /* x of the current edge (island and feature rows) */
static uint8_t rn_f;                /* row flags: 1 feature row, 2 island, 4 left feature, 8 right feature */

static void river_render_asm(void);

/* river_render_row() for SDCC: the clear, then the whole renderer in asm (the C
 * version is under #else and is the host reference).  State is read and written
 * in memory; river_render_asm() calls river_copy_template() for the tree/hill
 * slices and uses the rn_fill and rn_pat subroutines below. */
static void river_render_row(void) {
    river_clear_row();
    river_render_asm();
}

static void river_render_asm(void) {
    __asm
        ; ---- row flags -------------------------------------------------------
        lda     #0
        sta     _rn_f
        ldx     _rg_feature
        beq     rn_flags_i
        lda     _rg_rowcount
        cmp     _rg_be
        bcs     rn_flags_i
        lda     #1
        sta     _rn_f
    rn_flags_i:
        lda     _rg_island_flags
        and     #0x80
        beq     rn_flags_lr
        lda     _rn_f
        ora     #2
        sta     _rn_f
    rn_flags_lr:
        lda     _rn_f               ; feature and not island: left or right side
        and     #3
        cmp     #1
        bne     rn_flags_done
        lda     _rg_feature
        and     #0x80
        bne     rn_flag_left
        lda     _rn_f
        ora     #8
        sta     _rn_f
        jmp     rn_flags_done
    rn_flag_left:
        lda     _rn_f
        ora     #4
        sta     _rn_f
    rn_flags_done:

        ; ---- x, rg_row_left and the land fill before the bank ----------------
        lda     #0
        sta     _rg_row_pos
        sta     _rg_row_island_left
        sta     _rg_row_island_right
        lda     _rn_f
        and     #2
        bne     rn_x_isl
        lda     _rg_left
        clc
        adc     _rg_phase
        jmp     rn_x_set
    rn_x_isl:
        lda     _rg_left
    rn_x_set:
        sta     _rg_row_left
        sta     _isl_x
        lsr     a                   ; y = (x>>2)-1, kept in rf_cnt
        lsr     a
        sec
        sbc     #1
        sta     _rf_cnt

        ; ---- left feature (not island, bit 7 of the feature type) -----------
        lda     _rn_f
        and     #4
        beq     rn_noleft
        lda     _rf_cnt
        sta     _rg_ysave
        ldy     _rg_bd
        lda     #0x55
        jsr     rn_fill
        jsr     _river_copy_template
        lda     _rg_ysave           ; y = ysave - bc - bd
        sec
        sbc     _rg_bc
        sec
        sbc     _rg_bd
        sta     _rf_cnt
    rn_noleft:
        ldy     _rf_cnt
        lda     #0x55
        jsr     rn_fill
        lda     _isl_x
        and     #3
        asl     a
        asl     a
        jsr     rn_pat

        ; ---- island edges -----------------------------------------------------
        lda     _rn_f
        and     #2
        bne     rn_isl_go
        jmp     rn_nonisl
    rn_isl_go:
        lda     _rg_phase
        cmp     #6
        bcs     rn_isl_big
        asl     a                   ; phase < 6: a = width + 2*phase, x = left
        clc
        adc     _rg_width
        jmp     rn_tail
    rn_isl_big:
        lda     _rg_width           ; left island edge: x = (width>>1) + left
        lsr     a
        clc
        adc     _isl_x
        sta     _isl_x
        sta     _rg_row_island_left
        lsr     a
        lsr     a
        sec
        sbc     _rg_row_pos
        tay
        lda     #0xFF
        jsr     rn_fill
        lda     _isl_x
        and     #3
        asl     a
        asl     a
        ora     #2
        ldx     _rg_phase
        cpx     #8
        bcs     rn_isl_p8
        clc
        adc     #0x10
    rn_isl_p8:
        jsr     rn_pat
        lda     _rg_phase           ; right island edge: x = left + 2*phase
        asl     a
        clc
        adc     _isl_x
        sta     _isl_x
        sta     _rg_row_island_right
        lsr     a
        lsr     a
        sec
        sbc     _rg_row_pos
        sec
        sbc     #1
        sta     _rf_cnt
        lda     _rg_phase
        cmp     #8
        bcs     rn_isl_hi
        ldy     _rf_cnt             ; phase < 8: 0xAA fill (writes 0), pattern +0x10
        lda     #0xAA
        jsr     rn_fill
        lda     _isl_x
        and     #3
        asl     a
        asl     a
        ora     #0x10
        jsr     rn_pat
        jmp     rn_isl_half
    rn_isl_hi:
        lda     _rn_f               ; phase >= 8: feature slice, then the 0x55 fill
        and     #1
        beq     rn_isl_plain
        lda     _rg_bd              ; b7 = bd - pos ; y = y - b7 - bc
        sec
        sbc     _rg_row_pos
        sta     _rf_val
        lda     _rf_cnt
        sec
        sbc     _rf_val
        sec
        sbc     _rg_bc
        sta     _rg_ysave
        ldy     _rf_val
        lda     #0x55
        jsr     rn_fill
        jsr     _river_copy_template
        lda     _rg_ysave
        sta     _rf_cnt
    rn_isl_plain:
        ldy     _rf_cnt
        lda     #0x55
        jsr     rn_fill
        lda     _isl_x
        and     #3
        asl     a
        asl     a
        jsr     rn_pat
    rn_isl_half:
        lda     _rg_width           ; right part: width >> 1
        lsr     a
        jmp     rn_tail
    rn_nonisl:
        lda     _rg_width

    rn_tail:                        ; A = width part, x = _isl_x
        clc
        adc     _isl_x
        sta     _isl_x
        sta     _rg_row_right
        lsr     a
        lsr     a
        sec
        sbc     _rg_row_pos
        tay
        lda     #0xFF
        jsr     rn_fill
        lda     _isl_x
        and     #3
        asl     a
        asl     a
        ora     #2
        jsr     rn_pat

        ; ---- right feature (not island, bit 7 clear) --------------------------
        lda     _rn_f
        and     #8
        beq     rn_end
        lda     #0x30               ; y = (0x30 - pos) - bd - bc + 1
        sec
        sbc     _rg_row_pos
        sec
        sbc     _rg_bd
        sec
        sbc     _rg_bc
        clc
        adc     #1
        tay
        lda     #0x55
        jsr     rn_fill
        jsr     _river_copy_template
    rn_end:
        rts

    rn_fill:                        ; A = value, Y = y (bit 7 set: nothing)
        cpy     #0x80
        bcs     rn_fill_ret
        cmp     #0xAA
        bne     rn_fill_v
        lda     #0
    rn_fill_v:
        cmp     #0x55               ; land: the row clear wrote it, only advance
        beq     rn_fill_adv
        ldx     _rg_row_pos
    rn_fill_w:
        sta     _river_raw_row,x
        inx
        dey
        bpl     rn_fill_w
        dex
        stx     _rg_row_pos
    rn_fill_ret:
        rts
    rn_fill_adv:
        tya
        clc
        adc     _rg_row_pos
        sta     _rg_row_pos
        rts

    rn_pat:                         ; A = index: river_pattern(), pos += 2
        tay
        ldx     _rg_row_pos
        lda     _river_bank_black,y
        sta     _river_raw_row,x
        lda     _river_bank_black+1,y
        sta     _river_raw_row+1,x
        inx
        inx
        stx     _rg_row_pos
        rts
    __endasm;
}
#else
static void river_render_row(void) {
    uint8_t island = (uint8_t)(rg_island_flags & 0x80u);
    uint8_t x, y, a, b7;

    rg_row_pos = 0;
#ifdef __SDCC_mos6502
    river_clear_row();
#else
    memset(river_raw_row, 0x55, 48);
#endif
    rg_row_island_left = 0;
    rg_row_island_right = 0;

    /* --- left bank and left-side feature ----------------------------- */
    x = island ? rg_left : (uint8_t)(rg_left + rg_phase);
    rg_row_left = x;
    y = (uint8_t)((x >> 2) - 1u);
    if (!island && (rg_feature & 0x80u) && rg_rowcount < rg_be) {
        rg_ysave = y;
        y = rg_bd;
        river_fill(y, 0x55u);
        river_copy_template();
        y = (uint8_t)(rg_ysave - rg_bc - rg_bd);
    }
    river_fill(y, 0x55u);
    river_pattern((uint8_t)((x & 3u) * 4u));

    /* --- island / right bank ----------------------------------------- */
    a = rg_width;
    if (island) {
        if (rg_phase < 6u) {
            a = (uint8_t)(rg_width + rg_phase + rg_phase);
        } else {
            b7 = x;
            a = (uint8_t)((rg_width >> 1) + b7);
            x = a;
            rg_row_island_left = x;
            y = (uint8_t)((a >> 2) - rg_row_pos);
            river_fill(y, 0xFFu);
            a = (uint8_t)((x & 3u) * 4u + 2u);
            if (rg_phase < 8u) {
                a = (uint8_t)(a + 0x10u);
            }
            river_pattern(a);
            b7 = x;
            a = (uint8_t)((rg_phase << 1) + b7);
            x = a;
            rg_row_island_right = x;
            y = (uint8_t)((a >> 2) - rg_row_pos - 1u);
            if (rg_phase < 8u) {
                river_fill(y, 0xAAu);
                river_pattern((uint8_t)((x & 3u) * 4u + 0x10u));
            } else {
                if (rg_feature && rg_rowcount < rg_be) {
                    b7 = (uint8_t)(rg_bd - rg_row_pos);
                    y = (uint8_t)(y - b7 - rg_bc);
                    rg_ysave = y;
                    y = b7;
                    river_fill(y, 0x55u);
                    river_copy_template();
                    y = rg_ysave;
                }
                river_fill(y, 0x55u);
                river_pattern((uint8_t)((x & 3u) * 4u));
            }
            a = (uint8_t)(rg_width >> 1);
        }
    }

    b7 = x;
    a = (uint8_t)(a + b7);
    x = a;
    rg_row_right = x;
    y = (uint8_t)((a >> 2) - rg_row_pos);
    river_fill(y, 0xFFu);
    river_pattern((uint8_t)((x & 3u) * 4u + 2u));

    /* --- right-side feature and right land --------------------------- */
    y = (uint8_t)(0x30u - rg_row_pos);
    if (!island && rg_feature && !(rg_feature & 0x80u) &&
        rg_rowcount < rg_be) {
        y = (uint8_t)(y - rg_bd - rg_bc);
        y++;
        river_fill(y, 0x55u);
        river_copy_template();
        y = (uint8_t)(rg_bd - 1u);
    }
    river_fill(y, 0x55u);

}
#endif

/* ------------------------------------------------------------------------- */
/* State machine ($A6DE-$A9D5)                                               */
/* ------------------------------------------------------------------------- */

/* Signature of the last plain row that was rendered into river_raw_row (rs_isl
 * is 0xFF when the buffer does not hold one) and whether the current row is a
 * repeat of it. */
static uint8_t rs_left, rs_phase, rs_width, rs_isl;
static uint8_t rg_same;

static void river_step_state(void) {
    rg_b5 = (rg_level < 9u) ? 6u : 0u;
    rg_rowcount++;

    if (rg_rowcount >= 0x20u) {
        rg_rowcount = 0;
        rg_feature = 0;
        rg_margin_flag = 0;
        rg_flags23 = (uint8_t)((rg_flags23 & 0x04u) | 0x10u);
        rg_margin = rg_island_width;
        rg_section_timer--;
        if (rg_section_timer == 0u) {
            rg_countdown--;
            if (rg_countdown == 0u) {
                river_level_advance();
                rg_countdown = 0x10u;
            }
            river_lfsr_step();
            river_width_select();
        } else {
            if ((uint8_t)(rg_countdown - 1u) == 0u) {
                rg_margin_flag = 0;
                rg_flags23 = (rg_level & 1u) ? 0x80u : 0x84u;
            }
            river_lfsr_step();
        }
    } else if (!(rg_flags23 & 0x80u)) {
        river_lfsr_step();
    }

    /* $A7FB target movement. */
    if (rg_v35) {
        int8_t v = rg_v35;
        if ((v > 0 && rg_phase < rg_target) ||
            (v < 0 && rg_phase > rg_target)) {
            rg_phase = (uint8_t)(rg_phase + v);
            rg_width = (uint8_t)(rg_width - (uint8_t)(2 * v));
        } else {
            rg_v35 = 0;
        }
    }

    /* $A826 bank clamp / direction. */
    if (rg_flags23 & 0x80u) {
        rg_c3 = 0;
        rg_left = (uint8_t)(rg_left + river_walk[rg_rowcount]);
    } else {
        uint8_t skip = 0;

        if (rg_margin_flag) {
            if (rg_left < 0x18u) {
                rg_left++;
                skip = 1;
            } else if (rg_left >= 0x29u) {
                rg_left--;
                skip = 1;
            }
        }
        if (!skip) {
            if (rg_level < 3u) {
                rg_c4 = 0x1Cu;
                rg_c5 = 0x24u;
            } else if (rg_level < 0x0Au || !(rg_level & 1u) || rg_margin_flag) {
                /* $A85F-$A86D: even levels, odd levels below 10, and any row
                 * with the margin flag set keep the narrow bank limits. */
                rg_c4 = 0x19u;
                rg_c5 = 0x28u;
            } else {
                /* $A871: odd level >= 10 starts wide (0D/34, or 01/40 from
                 * level 21) and the feature direction narrows one side. */
                if (rg_level < 0x14u) {
                    rg_c4 = 0x0Du;
                    rg_c5 = 0x34u;
                } else {
                    rg_c4 = 0x01u;
                    rg_c5 = 0x40u;
                }
                if (rg_feature) {
                    if (rg_feature & 0x80u) {
                        rg_c4 = 0x19u;
                    } else {
                        rg_c5 = 0x28u;
                    }
                }
            }

            {
                uint8_t nib = (uint8_t)(rg_lfsr_c & 0x0Fu);
                if (rg_c3 == 0u) {
                    rg_c3 = (nib >= 8u) ? 0x01u : 0xFFu;
                    river_clamp_decision();
                } else if (nib < 8u) {
                    /* $A89E: skip the bank move this row */
                } else if (nib == 0x0Du) {
                    rg_c3 ^= 0xFEu;
                    river_clamp_decision();
                } else if (nib == 0x0Fu) {
                    uint8_t xx = (uint8_t)(0x0Eu -
                                 ((rg_island_flags & 0x80u) ? 1u : 0u));
                    if (!(xx < rg_island_width || xx < rg_margin)) {
                        river_clamp_decision();
                    }
                } else {
                    river_clamp_decision();
                }
            }
        }
    }

    river_feature_select();

    /* A plain row (no feature template) is a pure function of
     * left, phase, width and the island flag, and a third of all rows repeat
     * the previous one (checked on 30,000 model rows: the rule below finds
     * 33.1% of the 34.6% identical rows, no false positive).  Then the raw
     * row buffer and the ring fields rg_row_* still hold the right values and
     * the renderer is skipped. */
    rg_same = 0;
    if (rg_feature && rg_rowcount < rg_be) {
        rs_isl = 0xFFu;
        river_render_row();
    } else if (rs_isl == (uint8_t)(rg_island_flags & 0x80u) && rs_left == rg_left &&
               rs_phase == rg_phase && rs_width == rg_width) {
        rg_same = 1u;
    } else {
#ifdef __SDCC_mos6502
        if (rg_island_flags & 0x80u) {
            river_render_row();
        } else {
            river_render_plain();
        }
#else
        river_render_row();
#endif
        rs_left = rg_left;
        rs_phase = rg_phase;
        rs_width = rg_width;
        rs_isl = (uint8_t)(rg_island_flags & 0x80u);
    }
}

/* ------------------------------------------------------------------------- */
/* Exact per-tile river cache                                                */
/* ------------------------------------------------------------------------- */
/*
 * The Atari playfield is a per-scanline bitmap, not a tile map.  A tree is
 * a template fragment copied into consecutive row buffers: width bc bytes
 * (4*bc pixels), height be scanlines, at a byte offset that can be odd.
 * The 8x8 block that lands inside an NES tile therefore depends on the
 * tree's vertical phase, so no fixed atlas can reproduce it.
 *
 * Instead each completed 8x8 block is uploaded verbatim.  Identical blocks
 * within the same row share a slot; solids use TILE_LAND/TILE_WATER.  The
 * cache is a 254-slot ring in the playfield's own pattern table; tests/
 * nes_river_cache.mjs checks every on-screen row against the Atari model.
 */

/* NES pixel value = (plane1 << 1) | plane0, so mode-E value 1 (land) is
 * plane0 set / plane1 clear.  Value 3 (water) sets both planes. */
static const uint8_t river_land_pat[16] = {
    0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00,
    0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00
};
static const uint8_t river_water_pat[16] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static const uint8_t river_black_pat[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* Solid road tiles for the land margins (see TILE_ROAD_A).  Interleaved
 * 2bpp, top to bottom; 0 = dark edge (black), 1 = grey, 2 = yellow line.  A
 * road is 16 raw rows; the first tile row holds rows 1..8 (raw row 8 on top),
 * so its top line is the yellow one. */
static const uint8_t river_road_a_pat[16] = {
    0x00, 0xFF, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00,
    0xFF, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t river_road_b_pat[16] = {
    0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00,
    0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0xFF
};

static void river_cache_init(void) {
    set_bkg_data(TILE_LAND, 1, river_land_pat);
    set_bkg_data(TILE_WATER, 1, river_water_pat);
    set_bkg_data(TILE_BLACK, 1, river_black_pat);
    set_bkg_data(TILE_ROAD_A, 1, river_road_a_pat);
    set_bkg_data(TILE_ROAD_B, 1, river_road_b_pat);
    river_alloc = 0u;
}

/* Convert the 40 visible mode-E bytes of the current raw row into one
 * scanline (two plane bytes) of each of the 20 pending 8x8 tiles.  Each
 * source byte is 4 pixels, so a nibble LUT per plane replaces the bit loop. */
#ifndef __SDCC_mos6502
/* Reference C version of river_convert_row, used by the host harness. */
static void river_convert_row_c(uint8_t line) {
    uint8_t *dst = &river_tile_pat[line * 2u];
    const uint8_t *src = &river_raw_row[0];
    uint8_t c, b0, b1, p0, p1, k;

    for (c = 0; c < RIVER_TILE_COLS; c++) {
        b0 = src[0];
        b1 = src[1];
        if (b0 == b1 && b0 == 0x55u) {
            /* four land pixels twice: the dominant case, no LUT needed */
            p0 = 0xFFu;
            p1 = 0u;
            k = 1u;
        } else if (b0 == b1 && b0 == 0xFFu) {
            p0 = 0xFFu;
            p1 = 0xFFu;
            k = 2u;
        } else {
            p0 = (uint8_t)((river_lut_p0[b0] << 4) | river_lut_p0[b1]);
            p1 = (uint8_t)((river_lut_p1[b0] << 4) | river_lut_p1[b1]);
            k = 0u;
            if (p0 == 0xFFu) {
                k = (p1 == 0u) ? 1u : ((p1 == 0xFFu) ? 2u : 0u);
            }
        }
        dst[0] = p0;
        dst[1] = p1;
        /* river_col_kind starts at 3 for each tile row; AND-ing keeps only
         * the solid kind (land 1 / water 2) shared by all eight lines. */
        river_col_kind[c] &= k;
        dst += 16;
        src += 2;
    }
}
#endif

static uint8_t rc_off, rc_sx, rc_cc, rc_t0, rc_t1;

/* Hand-written 6502.  Only ~5 of the 24 columns of a tile row ever need pattern
 * bytes; the others stay solid land or water for all eight lines and use the fixed
 * tiles.  river_col_kind says what a column has been so far (3 = nothing yet, 1 =
 * land, 2 = water, 0 = mixed), so a solid column that stays solid costs ~34 cycles
 * and writes nothing.  When one stops being solid the lines already done are filled
 * in (lines run 7 down to 0).  X = source byte index, Y = column in the fast path. */
static void river_convert_row(uint8_t line) {
#ifdef __SDCC_mos6502
    rc_off = (uint8_t)(line << 1);
    __asm
        ldx     #0
        ldy     #0
    1$:
        lda     _river_raw_row,x
        cmp     _river_raw_row+1,x
        bne     20$
        cmp     #0x55
        bne     3$
        lda     _river_col_kind,y
        cmp     #1
        beq     8$
        cmp     #3
        bne     20$
        lda     #1
        sta     _river_col_kind,y
        jmp     8$
    3$:
        cmp     #0xFF
        bne     20$
        lda     _river_col_kind,y
        cmp     #2
        beq     8$
        cmp     #3
        bne     20$
        lda     #2
        sta     _river_col_kind,y
    8$:
        inx
        inx
        iny
        cpx     #48
        bne     1$
        jmp     99$
    20$:
        ; this line needs bytes: fill in the earlier lines if it was solid so far
        sty     _rc_cc
        stx     _rc_sx
        lda     _river_col_kind,y
        beq     40$
        cmp     #3
        beq     38$
        ldx     #0
        cmp     #2
        bne     21$
        ldx     #0xFF
    21$:
        stx     _rc_t1
        tya
        asl     a
        asl     a
        asl     a
        asl     a
        ora     _rc_off
        tay
        bcs     25$
    22$:
        iny
        iny
        tya
        and     #15
        beq     37$
        lda     #0xFF
        sta     _river_tile_pat,y
        lda     _rc_t1
        sta     _river_tile_pat+1,y
        jmp     22$
    25$:
        iny
        iny
        tya
        and     #15
        beq     37$
        lda     #0xFF
        sta     _river_tile_pat+256,y
        lda     _rc_t1
        sta     _river_tile_pat+257,y
        jmp     25$
    37$:
        ldy     _rc_cc
        ldx     _rc_sx
    38$:
        lda     #0
        sta     _river_col_kind,y
    40$:
        tya
        asl     a
        asl     a
        asl     a
        asl     a
        ora     _rc_off
        tay
        lda     _river_raw_row,x
        tax
        lda     _river_lut_p0,x
        asl     a
        asl     a
        asl     a
        asl     a
        sta     _rc_t0
        lda     _river_lut_p1,x
        asl     a
        asl     a
        asl     a
        asl     a
        sta     _rc_t1
        ldx     _rc_sx
        lda     _river_raw_row+1,x
        tax
        lda     _rc_cc
        cmp     #16
        bcs     50$
        lda     _river_lut_p0,x
        ora     _rc_t0
        sta     _river_tile_pat,y
        lda     _river_lut_p1,x
        ora     _rc_t1
        sta     _river_tile_pat+1,y
        jmp     60$
    50$:
        lda     _river_lut_p0,x
        ora     _rc_t0
        sta     _river_tile_pat+256,y
        lda     _river_lut_p1,x
        ora     _rc_t1
        sta     _river_tile_pat+257,y
    60$:
        ldy     _rc_cc
        ldx     _rc_sx
        jmp     8$
    99$:
    __endasm;
#else
    river_convert_row_c(line);
#endif
}

/* The current raw row equals the previous one (rg_same), which was converted
 * into line + 1 of the same tile row: copy that scanline of the mixed columns (a
 * solid column has no bytes and stays solid).  line <= 6. */
static void river_copy_line(uint8_t line) {
#ifdef __SDCC_mos6502
    rc_off = (uint8_t)(line << 1);
    __asm
        ldy     _rc_off
        ldx     #0                  ; columns 0-15: first page of the pattern buffer
    1$:
        lda     _river_col_kind,x
        bne     2$
        lda     _river_tile_pat+2,y
        sta     _river_tile_pat,y
        lda     _river_tile_pat+3,y
        sta     _river_tile_pat+1,y
    2$:
        tya
        clc
        adc     #16
        tay
        inx
        cpx     #16
        bne     1$
        ldy     _rc_off             ; columns 16-23: second page
    3$:
        lda     _river_col_kind,x
        bne     4$
        lda     _river_tile_pat+258,y
        sta     _river_tile_pat+256,y
        lda     _river_tile_pat+259,y
        sta     _river_tile_pat+257,y
    4$:
        tya
        clc
        adc     #16
        tay
        inx
        cpx     #24
        bne     3$
    __endasm;
#else
    uint8_t c;

    for (c = 0; c < RIVER_TILE_COLS; c++) {
        river_tile_pat[c * 16u + line * 2u] = river_tile_pat[c * 16u + line * 2u + 2u];
        river_tile_pat[c * 16u + line * 2u + 1u] = river_tile_pat[c * 16u + line * 2u + 3u];
    }
#endif
}

#ifndef __SDCC_mos6502
static uint8_t river_pat_equal(const uint8_t *a, const uint8_t *b) {
    uint8_t i;

    for (i = 0; i < 16u; i++) {
        if (a[i] != b[i]) {
            return 0u;
        }
    }
    return 1u;
}
#endif

/* ------------------------------------------------------------------------- */
/* VRAM transfer buffer                                                      */
/* ------------------------------------------------------------------------- */
/*
 * GBDK-NES flushes a small buffer in page 1 ($0100-$0187) to the PPU during
 * VBlank.  Its API (set_vram_byte, set_bkg_data) costs ~250 CPU cycles per
 * byte and blocks when the buffer is full, so a tile row needed ~17k cycles of
 * uploads.  The entries are simple, so river_put() builds them directly:
 *
 *     [op] [ppuctrl=0] [addr hi] [addr lo] [n data bytes] [0 terminator]
 *
 * with op = $81 - 4n, the low address of a jump into the library's unrolled
 * "PLA / STA $2007" chain (n = 16 gives the $41 seen for set_bkg_data, 24 the
 * $21 of a stripe).  The library keeps four zero-page variables: `valid`
 * (bit 7 = the NMI should flush), `num_cycles_x8` (flush time left this frame,
 * in 8-cycle units: an entry costs n + 5), `pos_w` (next free byte) and
 * `pos_old` (start of the last entry, used to merge adjacent bytes).  Clearing
 * bit 7 of `valid` while editing keeps an NMI from flushing a half-built entry.
 *
 * river_put() never blocks: if the entry does not fit, it returns 0 and the
 * caller retries on a later pass.  Only valid while the display is on; with
 * rendering off the library writes the PPU directly (see river_live).
 */
#ifdef __SDCC_mos6502
extern volatile uint8_t _vram_transfer_buffer_valid;
extern volatile uint8_t _vram_transfer_buffer_num_cycles_x8;
extern volatile uint8_t _vram_transfer_buffer_pos_w;
extern volatile uint8_t _vram_transfer_buffer_pos_old;

#define VTB_BUF ((volatile uint8_t *)0x0100)

/* Parameters / scratch of river_put (file-scope: see AGENTS.md rule 3). */
static uint8_t vp_hi, vp_lo, vp_kind, vp_src, vp_n;
static uint8_t vp_v, vp_pos, vp_cyc, vp_dst, vp_k;

/* Copy loops, one per source array: X = buffer offset, Y = source offset.
 * river_tile_pat holds GBDK-style interleaved 2bpp (p0,p1 per scanline) and
 * set_bkg_data() de-interleaves it for the NES, so the pattern loops write the
 * eight plane-0 bytes first, then the eight plane-1 bytes. */
/* Unrolled: 9 cycles a byte instead of 24.  Pattern loops write the eight
 * plane-0 bytes first, then the eight plane-1 bytes. */
static void vp_copy_pat_lo(void) {
    __asm
        ldx     _vp_dst
        ldy     _vp_src
        lda     _river_tile_pat+0,y
        sta     0x100,x
        lda     _river_tile_pat+2,y
        sta     0x101,x
        lda     _river_tile_pat+4,y
        sta     0x102,x
        lda     _river_tile_pat+6,y
        sta     0x103,x
        lda     _river_tile_pat+8,y
        sta     0x104,x
        lda     _river_tile_pat+10,y
        sta     0x105,x
        lda     _river_tile_pat+12,y
        sta     0x106,x
        lda     _river_tile_pat+14,y
        sta     0x107,x
        lda     _river_tile_pat+1,y
        sta     0x108,x
        lda     _river_tile_pat+3,y
        sta     0x109,x
        lda     _river_tile_pat+5,y
        sta     0x10A,x
        lda     _river_tile_pat+7,y
        sta     0x10B,x
        lda     _river_tile_pat+9,y
        sta     0x10C,x
        lda     _river_tile_pat+11,y
        sta     0x10D,x
        lda     _river_tile_pat+13,y
        sta     0x10E,x
        lda     _river_tile_pat+15,y
        sta     0x10F,x
    __endasm;
}

static void vp_copy_pat_hi(void) {
    __asm
        ldx     _vp_dst
        ldy     _vp_src
        lda     _river_tile_pat+256,y
        sta     0x100,x
        lda     _river_tile_pat+258,y
        sta     0x101,x
        lda     _river_tile_pat+260,y
        sta     0x102,x
        lda     _river_tile_pat+262,y
        sta     0x103,x
        lda     _river_tile_pat+264,y
        sta     0x104,x
        lda     _river_tile_pat+266,y
        sta     0x105,x
        lda     _river_tile_pat+268,y
        sta     0x106,x
        lda     _river_tile_pat+270,y
        sta     0x107,x
        lda     _river_tile_pat+257,y
        sta     0x108,x
        lda     _river_tile_pat+259,y
        sta     0x109,x
        lda     _river_tile_pat+261,y
        sta     0x10A,x
        lda     _river_tile_pat+263,y
        sta     0x10B,x
        lda     _river_tile_pat+265,y
        sta     0x10C,x
        lda     _river_tile_pat+267,y
        sta     0x10D,x
        lda     _river_tile_pat+269,y
        sta     0x10E,x
        lda     _river_tile_pat+271,y
        sta     0x10F,x
    __endasm;
}

static void vp_copy_ids(void) {
    __asm
        ldx     _vp_dst
        ldy     _vp_src
    1$:
        lda     _river_row_ids,y
        sta     0x0100,x
        inx
        iny
        dec     _vp_n
        bne     1$
    __endasm;
}

/* Margin tiles / attribute bytes: staged in river_tile_pat, which is free once a\n * tile row has been classified and only its stripe is left to write. */
static void vp_copy_misc(void) {
    __asm
        ldx     _vp_dst
        ldy     #0
    1$:
        lda     _river_tile_pat,y
        sta     0x0100,x
        inx
        iny
        dec     _vp_n
        bne     1$
    __endasm;
}

static uint8_t vp_ok;

/* Entry header + bookkeeping; the copy loops above fill in the data. */
static void river_put_begin(void) {
    __asm
        lda     __vram_transfer_buffer_valid
        sta     _vp_v
        and     #0x7F
        sta     __vram_transfer_buffer_valid
        lda     #0
        sta     _vp_ok
        lda     #131
        sec
        sbc     _vp_n
        cmp     __vram_transfer_buffer_pos_w
        bcc     8$
        lda     _vp_n
        clc
        adc     #5
        sta     _vp_k
        lda     __vram_transfer_buffer_num_cycles_x8
        cmp     _vp_k
        bcc     8$
        sbc     _vp_k
        sta     _vp_cyc
        ldx     __vram_transfer_buffer_pos_w
        lda     _vp_n
        asl     a
        asl     a
        eor     #0xFF
        sec
        adc     #0x81
        sta     0x100,x
        lda     #0
        sta     0x101,x
        lda     _vp_hi
        sta     0x102,x
        lda     _vp_lo
        sta     0x103,x
        txa
        clc
        adc     #4
        sta     _vp_dst
        adc     _vp_n
        sta     _vp_pos
        inc     _vp_ok
        rts
    8$:
        lda     _vp_v
        sta     __vram_transfer_buffer_valid
    __endasm;
}

static void river_put_end(void) {
    __asm
        ldx     _vp_pos
        lda     #0
        sta     0x100,x
        lda     __vram_transfer_buffer_pos_w
        sta     __vram_transfer_buffer_pos_old
        stx     __vram_transfer_buffer_pos_w
        lda     _vp_cyc
        sta     __vram_transfer_buffer_num_cycles_x8
        lda     __vram_transfer_buffer_valid
        lsr     a
        ora     #0x80
        sta     __vram_transfer_buffer_valid
    __endasm;
}

static uint8_t river_put(void) {
    river_put_begin();
    if (!vp_ok) {
        return 0u;
    }
    if (vp_kind == 0u) {
        vp_copy_pat_lo();
    } else if (vp_kind == 1u) {
        vp_copy_pat_hi();
    } else if (vp_kind == 3u) {
        vp_copy_misc();
    } else {
        vp_copy_ids();
    }
    river_put_end();
    return 1u;
}
#else
/* Host build: nothing is transferred. */
static uint8_t vp_hi, vp_lo, vp_kind, vp_src, vp_n;
static uint8_t river_put(void) { return 1u; }
#endif

/* ------------------------------------------------------------------------- */
/* Tile-row classification and nametable stripe                              */
/* ------------------------------------------------------------------------- */
/*
 * A completed 8x8 tile row (river_tile_pat) is classified column by column:
 * solid columns use the land/water tiles, a pattern identical to an earlier
 * column of the row reuses its slot, anything else is uploaded into the next
 * cache slot.  The nametable stripe is written last, after every pattern it
 * references.  This "job" runs from river_update(); when the transfer buffer is
 * full it simply resumes on the next pass.  Row generation
 * (which overwrites river_tile_pat) waits until the job is done, and the
 * scroll keeps moving meanwhile because the generator runs ahead of the
 * display (see river_owed).
 */

static uint8_t river_black_rows;      /* ring slots still holding black */

/* Roads (the Atari recolours the land of a bridge object's 20 rows per
 * scanline from DLI table $B4E5).  A road is 16 consecutive raw rows painted
 * into the generated rows, starting up to 8 rows after the spawn row
 * so that it fills two whole tile rows (one 16x16 attribute row) and gets
 * BG palette ROAD_PALETTE.  The two margin tiles on each side and the
 * attribute bytes are written with the tile row; the slot pair is remembered
 * so that the next lap restores land. */
static uint8_t river_road_left;       /* raw rows until the road ends (16 + wait at the start) */
static uint8_t river_road_slot;       /* first (odd) slot of the road, or 0xFF */
static uint8_t river_road_stage;      /* margin/attribute writes done for the job */
static uint8_t river_land_alt;        /* land colour: 0 = $D2, 1 = $D6 */
static uint8_t river_job_pal;         /* BG palette of the pending tile row */
#ifdef __SDCC_sm83
static uint8_t river_slot_attr[RIVER_RING_ROWS];   /* BG attribute last written per map row */
#endif
static uint8_t river_flip_age;        /* tile rows since the land colour flipped (255 = long ago) */
static uint8_t river_pending_slot;
static uint8_t river_job;             /* tile row waiting for classification */
static uint8_t river_job_col;
static uint8_t river_job_margin;      /* margins already written for this row */
static uint8_t river_live;            /* display on: use the fast buffer path */
static uint8_t rj_c, rj_k, rj_id, rj_n;
static uint16_t rj_addr;
#ifndef __SDCC_mos6502
static const uint8_t *rj_pc, *rj_pk;
#endif

/* Upload column rj_c's pattern to cache slot rj_id; 0 when the buffer is full. */
static uint8_t river_emit_pattern(void) {
    if (!river_live) {
        set_bkg_data(rj_id, 1, &river_tile_pat[(uint16_t)rj_c * 16u]);
        return 1u;
    }
    vp_hi = (uint8_t)(rj_id >> 4);
    vp_lo = (uint8_t)(rj_id << 4);
    vp_n = 16u;
    if (rj_c < 16u) {
        vp_kind = 0u;
        vp_src = (uint8_t)(rj_c << 4);
    } else {
        vp_kind = 1u;
        vp_src = (uint8_t)((rj_c - 16u) << 4);
    }
    return river_put();
}

/* Tile of the land margins this row needs, or 0xFF when they are already
 * right: road tiles on a road row, land again on the slots a road used. */
static uint8_t river_margin_id(void) {
    if (river_job_pal == ROAD_PALETTE) {
        return (river_pending_slot & 1u) ? TILE_ROAD_A : TILE_ROAD_B;
    }
    if (river_pending_slot == river_road_slot ||
        (uint8_t)(river_pending_slot + 1u) == river_road_slot) {
        return TILE_LAND;
    }
    return 0xFFu;
}

/* Land margins and the 16x16 attribute row (palette) of a tile row.  The
 * palette of a pair of tile rows is written with the first (odd-slot) row; an
 * attribute byte covers four tile rows, so the half belonging to the other
 * pair is derived from the road slot and land colour.  0 when the buffer is
 * full. */
static uint8_t river_emit_extra(void) {
    if (river_road_stage == 0u) {
        rj_c = river_margin_id();
        if (rj_c == 0xFFu) {
            river_road_stage = 2u;
        } else {
            for (rj_n = 0; rj_n < RIVER_LEFT_COL; rj_n++) {
                river_tile_pat[rj_n] = rj_c;
            }
            rj_addr = (uint16_t)(PPU_NT1_BASE + ((uint16_t)river_pending_slot << 5));
            vp_hi = (uint8_t)(rj_addr >> 8);
            vp_lo = (uint8_t)rj_addr;
            vp_n = RIVER_LEFT_COL;
            vp_kind = 3u;
            vp_src = 0u;
            if (!river_put()) {
                return 0u;
            }
            river_road_stage = 1u;
        }
    }
    if (river_road_stage == 1u) {
        rj_addr = (uint16_t)(PPU_NT1_BASE + ((uint16_t)river_pending_slot << 5) +
                             RIVER_LEFT_COL + RIVER_TILE_COLS);
        vp_hi = (uint8_t)(rj_addr >> 8);
        vp_lo = (uint8_t)rj_addr;
        vp_n = RIVER_LEFT_COL;
        vp_kind = 3u;
        vp_src = 0u;
        if (!river_put()) {
            return 0u;
        }
        river_road_stage = 2u;
    }
#ifdef __SDCC_sm83
    /* No attribute table on GBC: colour is the BG attribute map (VRAM bank 1). */
    if (river_road_stage == 2u) {
        river_road_stage = 3u;
    }
#else
    if (river_road_stage == 2u) {
        if (river_pending_slot & 1u) {
            rj_c = (uint8_t)(river_job_pal * 5u);
            if ((river_pending_slot & 3u) == 3u) {
                /* Lower pair; the upper half still holds the previous lap's
                 * rows, which are on screen (the bottom sliver) for 27-28 rows.
                 * Keep their palette: the road, or the land colour from before
                 * a recent flip. */
                rj_n = (river_road_slot == (uint8_t)(river_pending_slot - 2u)) ? ROAD_PALETTE :
                       (river_land_alt ^ (river_flip_age < 28u)) ? PLAYFIELD_ALT_PALETTE : PLAYFIELD_PALETTE;
                rj_c = (uint8_t)((rj_c << 4) | (uint8_t)(rj_n * 5u));
            } else {
                /* The pair below was written just before: the road if it is
                 * the road slot, else the current land colour. */
                rj_n = (river_road_slot == (uint8_t)(river_pending_slot + 2u)) ? ROAD_PALETTE :
                       river_land_alt ? PLAYFIELD_ALT_PALETTE : PLAYFIELD_PALETTE;
                rj_c |= (uint8_t)((rj_n * 5u) << 4);
            }
            for (rj_n = 0; rj_n < 8u; rj_n++) {
                river_tile_pat[rj_n] = rj_c;
            }
            rj_addr = (uint16_t)(PPU_NT1_ATTR + (uint16_t)((river_pending_slot >> 2) << 3));
            vp_hi = (uint8_t)(rj_addr >> 8);
            vp_lo = (uint8_t)rj_addr;
            vp_n = 8u;
            vp_kind = 3u;
            vp_src = 0u;
            if (!river_put()) {
                return 0u;
            }
        }
        river_road_stage = 3u;
    }
#endif
    return 1u;
}

/* Write the 24 river columns of the pending row (and, for the first rows of
 * the game, the land margins); 0 when the buffer is full. */
static uint8_t river_emit_stripe(void) {
    rj_addr = (uint16_t)(PPU_NT1_BASE + ((uint16_t)river_pending_slot << 5));
    if (river_black_rows && !river_job_margin) {
        for (rj_n = 0; rj_n < RIVER_LEFT_COL; rj_n++) {
            set_vram_byte((uint8_t *)(rj_addr + rj_n), TILE_LAND);
            set_vram_byte((uint8_t *)(rj_addr + RIVER_LEFT_COL +
                                      RIVER_TILE_COLS + rj_n), TILE_LAND);
        }
        river_job_margin = 1u;
    }
    if (river_live && river_road_stage != 3u && !river_emit_extra()) {
        return 0u;
    }
    rj_addr = (uint16_t)(PPU_NT1_BASE + ((uint16_t)river_pending_slot << 5));
    rj_addr += RIVER_LEFT_COL;
    if (!river_live) {
        for (rj_n = 0; rj_n < RIVER_TILE_COLS; rj_n++) {
            set_vram_byte((uint8_t *)(rj_addr + rj_n), river_row_ids[rj_n]);
        }
#ifdef __SDCC_sm83
        /* Colour: the BG attribute (VRAM bank 1) of this map row, written only
         * when it changes (land flips and road rows are rare). */
        rj_c = (river_job_pal == ROAD_PALETTE) ? GBC_PAL_ROAD :
               (river_job_pal == PLAYFIELD_ALT_PALETTE) ? GBC_PAL_ALT : GBC_PAL_LAND;
        if (river_slot_attr[river_pending_slot] != rj_c) {
            river_slot_attr[river_pending_slot] = rj_c;
            VBK_REG = 1;
            for (rj_n = 0; rj_n < RIVER_TILE_COLS; rj_n++) {
                set_vram_byte((uint8_t *)(rj_addr + rj_n), rj_c);
            }
            VBK_REG = 0;
        }
#endif
        return 1u;
    }
    vp_hi = (uint8_t)(rj_addr >> 8);
    vp_lo = (uint8_t)rj_addr;
    vp_n = RIVER_TILE_COLS;
    vp_kind = 2u;
    vp_src = 0u;
    return river_put();
}

/* Set rj_id to the cache id of the first earlier, non-solid column whose 16
 * pattern bytes equal column rj_c's, or 0xFF.  This was a C loop that cost
 * 10-13k cycles in one slice (~100 cycles per candidate pair, 12 columns per
 * slice): the frames with a tile-row job were the ones that overran.  The
 * first byte rejects almost every candidate.  Absolute indexing, with X the
 * candidate and Y the current offset inside the 256-byte pages of
 * river_tile_pat (column * 16: columns 0-15 in page 0, 16-23 in page 1).
 * Its own function, as inline asm must be; vp_k is river_put's scratch and is
 * free here. */
static void river_find_dup(void) {
#ifndef __SDCC_mos6502
    rj_id = 0xFFu;
    rj_pc = &river_tile_pat[(uint16_t)rj_c * 16u];
    for (rj_k = 0; rj_k < rj_c; rj_k++) {
        if (river_col_kind[rj_k] & 3u) {
            continue;
        }
        rj_pk = &river_tile_pat[(uint16_t)rj_k * 16u];
        if (river_pat_equal(rj_pc, rj_pk)) {
            rj_id = river_row_ids[rj_k];
            break;
        }
    }
#else
    __asm
        lda     #0xFF
        sta     _rj_id
        ldx     _rj_c
    1$:
        dex
        bpl     2$
        jmp     9$                  ; no candidate left
    2$:
        lda     _river_col_kind,x
        bne     1$                  ; solid column: nothing to share
        stx     _rj_k
    3$:
        txa                         ; X = candidate offset in its page
        asl     a
        asl     a
        asl     a
        asl     a
        tax
        lda     _rj_c               ; Y = current offset in its page
        asl     a
        asl     a
        asl     a
        asl     a
        tay
        lda     #16
        sta     _vp_k
        lda     _rj_c
        cmp     #16
        bcs     4$
        ; current and candidate both in page 0
    10$:
        lda     _river_tile_pat,y
        cmp     _river_tile_pat,x
        bne     8$
        inx
        iny
        dec     _vp_k
        bne     10$
        beq     7$
    4$:
        lda     _rj_k
        cmp     #16
        bcs     5$
        ; current in page 1, candidate in page 0
    11$:
        lda     _river_tile_pat+256,y
        cmp     _river_tile_pat,x
        bne     8$
        inx
        iny
        dec     _vp_k
        bne     11$
        beq     7$
    5$:
        ; both in page 1
    12$:
        lda     _river_tile_pat+256,y
        cmp     _river_tile_pat+256,x
        bne     8$
        inx
        iny
        dec     _vp_k
        bne     12$
    7$:
        ldx     _rj_k               ; identical: reuse its slot
        lda     _river_row_ids,x
        sta     _rj_id
        jmp     9$
    8$:
        ldx     _rj_k
        jmp     1$
    9$:
    __endasm;
#endif
}

static void river_reset_kinds(void) {
#ifdef __SDCC_mos6502
    __asm
        lda     #3
        ldx     #23
    1$:
        sta     _river_col_kind,x
        dex
        bpl     1$
    __endasm;
#else
    memset(river_col_kind, 3, sizeof(river_col_kind));
#endif
}

/* Advance river_job_col over solid columns: kind 1 = TILE_LAND (0), kind 2 =
 * TILE_WATER (1), i.e. the tile is kind - 1. */
typedef char river_solid_tiles_assert[(TILE_LAND == 0u && TILE_WATER == 1u) ? 1 : -1];
static void river_job_solids(void) {
#ifdef __SDCC_mos6502
    __asm
        ldx     _river_job_col
    1$:
        cpx     #24
        bcs     9$
        ldy     _river_col_kind,x
        beq     9$
        dey
        tya
        sta     _river_row_ids,x
        inx
        bne     1$
    9$:
        stx     _river_job_col
    __endasm;
#else
    while (river_job_col < RIVER_TILE_COLS && river_col_kind[river_job_col]) {
        river_row_ids[river_job_col] = (uint8_t)(river_col_kind[river_job_col] - 1u);
        river_job_col++;
    }
#endif
}

/* Non-solid columns one river_update() may classify before the job yields to
 * the next pass (find_dup ~1.7k + upload ~1.4k units each, the stripe ~5.5k, so
 * a whole job peaked at 58k of a 70k frame).  The job resumes where it stopped,
 * as when the buffer is full.  The 6502 keeps its unlimited job. */
#ifndef RIVER_JOB_COLS
#ifdef __SDCC_sm83
#define RIVER_JOB_COLS      10u
#else
#define RIVER_JOB_COLS      255u
#endif
#endif
#define RIVER_STRIPE_COLS   3u          /* budget the stripe write is worth */
static uint8_t river_job_left;

static void river_run_job(void) {
    for (;;) {
        river_job_solids();
        if (river_job_col >= RIVER_TILE_COLS) {
            break;
        }
        if (!river_job_left) {
            return;                     /* budget spent: resume next pass */
        }
        river_job_left--;
        rj_c = river_job_col;
        river_find_dup();
        if (rj_id == 0xFFu) {
            rj_id = (uint8_t)(TILE_RIVER_BASE + river_alloc);
            if (!river_emit_pattern()) {
                return;             /* buffer full: resume next pass */
            }
            river_alloc++;
            if (river_alloc >= TILE_RIVER_COUNT) {
                river_alloc = 0u;
            }
        }
        river_row_ids[rj_c] = rj_id;
        river_job_col++;
    }
    if (river_job_left < RIVER_STRIPE_COLS) {
        return;
    }
    river_job_left -= RIVER_STRIPE_COLS;
    if (!river_emit_stripe()) {
        return;
    }
    if (river_black_rows) {
        river_black_rows--;
    }
    if (river_job_pal == ROAD_PALETTE) {
        if (river_pending_slot & 1u) {
            river_road_slot = river_pending_slot;
        }
    } else if ((uint8_t)(river_pending_slot + 1u) == river_road_slot) {
        river_road_slot = 0xFFu;        /* both road rows are land again */
    }
    river_road_stage = 0u;
    river_job_margin = 0u;
    river_reset_kinds();
    river_job = 0u;
}

/* ------------------------------------------------------------------------- */
/* Ring / row generation                                                     */
/* ------------------------------------------------------------------------- */

/* Road profile by raw rows left (16 = first row): 0 black edge, 1 grey (the
 * land pixels unchanged), 2 yellow centre line.  Symmetric, like the Atari's
 * $B4E5 scanline colours. */
static const uint8_t river_road_profile[17] = {
    1, 0, 0, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 0, 0
};

/* Recolour the land pixels (value 1) of the raw row just rendered, or count
 * down the rows before the road starts.  No calls inside: locals are safe. */
static void river_road_paint(void) {
    uint8_t i, b, m, v;

    if (river_road_left <= 16u) {
        v = river_road_profile[river_road_left];
        if (v != 1u) {
            for (i = 0; i < 48u; i++) {
                b = river_raw_row[i];
                m = (uint8_t)(b & 0x55u & (uint8_t)~(b >> 1));
                b &= (uint8_t)~(m | (uint8_t)(m << 1));
                if (v == 2u) {
                    b |= (uint8_t)(m << 1);
                }
                river_raw_row[i] = b;
            }
        }
        river_job_margin = 2u;          /* the pending tile row holds road */
    }
    if (--river_road_left == 0u && rg_countdown == 1u) {
        river_land_alt ^= 1u;           /* the land changes colour at the bridge */
        river_flip_age = 0u;
    }
}

/* Generate one raw Atari scanline and add it to the pending 8x8 tile row. */
static void river_generate_raw_row(void) {
    if (river_block_index == 0u) {
        /* New rows enter at the top and scroll down: the ring advances
         * backwards, one row above the current visible top. */
        river_slot = (river_slot == 0u)
            ? (uint8_t)(RIVER_RING_ROWS - 1u)
            : (uint8_t)(river_slot - 1u);
    }

    river_step_state();

    if (river_road_left != 0u) {
        river_road_paint();
        rs_isl = 0xFFu;                 /* the road recoloured the buffer */
        rg_same = 0u;
    }

    /* $AB80: the object spawner runs right after the row renderer.  It only
     * has work while an object is being built or every 16th row. */
    if (river_obj_count != 0x0Bu || !(rg_rowcount & 0x0Fu)) {
        entities_row();
    }

    /* The Atari playfield is stored bottom-up relative to its display:
     * raw row 7 is the top scanline of the NES tile.  Reversing the row
     * order here inverts the whole playfield (trees included). */
    if (rg_same && river_block_index != 0u) {
        river_copy_line((uint8_t)(7u - river_block_index));
    } else {
        river_convert_row((uint8_t)(7u - river_block_index));
    }
    if (river_block_index == 3u) {
        river_left_x[river_slot] = rg_row_left;
        river_right_x[river_slot] = rg_row_right;
        river_island_left_x[river_slot] = rg_row_island_left;
        river_island_right_x[river_slot] = rg_row_island_right;
        river_flags[river_slot] = (uint8_t)(
            ((rg_flags23 & 0x80u) ? RIVER_FLAG_BRIDGE : 0u) |
            ((rg_island_flags & 0x80u) ? RIVER_FLAG_ISLAND : 0u) |
            (rg_feature ? RIVER_FLAG_FEATURE : 0u));
    }
    if (river_block_index == 7u) {
        river_debug_state[0] = rg_left;
        river_debug_state[1] = rg_width;
        river_debug_state[2] = rg_phase;
        river_debug_state[3] = rg_rowcount;
        river_debug_state[4] = rg_island_flags;
        river_debug_state[5] = rg_feature;
        river_debug_state[6] = rg_level;
        river_debug_state[7] = rg_lfsr_a;
        river_debug_state[8] = rg_lfsr_b;
        river_debug_state[9] = rg_lfsr_c;
        river_debug_state[10] = rg_countdown;
        river_debug_state[11] = rg_flags23;
#ifndef __SDCC_mos6502
        river_debug_state[12] = rg_bd;
        river_debug_state[13] = rg_bc;
        river_debug_state[14] = rg_be;
        river_debug_state[15] = (uint8_t)(rg_tpl_off & 0xFF);
#endif
    }
    river_block_index++;
}

/* The 8th raw row completed: queue the tile row for classification. */
static void river_finish_tile_row(void) {
    river_pending_slot = river_slot;
    river_job_pal = (river_job_margin == 2u) ? ROAD_PALETTE :
                    river_land_alt ? PLAYFIELD_ALT_PALETTE : PLAYFIELD_PALETTE;
    river_job_margin = 0u;
    if (river_flip_age != 0xFFu) {
        river_flip_age++;
    }
    river_job = 1u;
    river_job_col = 0u;
    river_block_index = 0;
    river_step++;
    river_distance = river_step;
}

/* ------------------------------------------------------------------------- */
/* Public interface                                                          */
/* ------------------------------------------------------------------------- */

void river_init_tiles(void) {
    river_cache_init();
}

/* Start the river.  restart = 0: a new game from the level-1 seeds.  restart =
 * 1: the generator resumes from the checkpoint in rg_seeds/rg_level, as the
 * Atari does after a lost life ($B0F0 with X=$12 reloads only the generator
 * defaults, then $A3C8 loads the LFSR and rg_left from $66-$69; the level,
 * score and lives are kept).  bridges is the HUD bridge counter. */
#ifdef __SDCC_sm83
/* river.h's spawn slot is the third tile row generated below (river_slot
 * starts at RIVER_RING_ROWS - 1 and steps down before each tile row). */
typedef char river_spawn_slot_check[(RIVER_SPAWN_SLOT == RIVER_RING_ROWS - 4u) ? 1 : -1];
#endif

static void river_start(uint8_t restart, uint8_t bridges) {
    uint8_t i;

    rs_isl = 0xFFu;                     /* no rendered row to repeat yet */
    rg_same = 0u;
    river_init_tiles();

    for (i = 0; i < RIVER_ROWS; i++) {
        river_left_x[i] = 3u;
        river_right_x[i] = 28u;
        river_island_left_x[i] = 0u;
        river_island_right_x[i] = 0u;
        river_flags[i] = 0u;
    }

    /* Atari initial state: $BAE1 defaults plus the skill-0 level seeds. */
    rg_left = river_defaults[5];
    rg_width = river_defaults[6];
    rg_phase = river_defaults[7];
    rg_rowcount = 0;
    rg_island_flags = 0;
    rg_feature = 0;
    rg_section_timer = river_defaults[13];
    rg_countdown = river_defaults[16];
    rg_island_width = river_defaults[18];
    rg_margin = 0;
    rg_margin_flag = 0;
    rg_flags23 = 0x84u;
    if (restart) {
        rg_lfsr_a = rg_seeds[0];
        rg_lfsr_b = rg_seeds[1];
        rg_lfsr_c = rg_seeds[2];
        rg_left = rg_seeds[3];
    } else {
        /* One 6-byte row per start level ($B494..$B4A8): LFSR seed, rg_left,
         * level.  Row 0 is the level-1 start the generator was validated on. */
        const uint8_t *row = atari_level_descriptors + (uint8_t)(river_variant * 6u);

        rg_lfsr_a = row[0];
        rg_lfsr_b = row[1];
        rg_lfsr_c = row[2];
        rg_left = row[3];
        rg_level = row[4];
        rg_seeds[0] = rg_lfsr_a;
        rg_seeds[1] = rg_lfsr_b;
        rg_seeds[2] = rg_lfsr_c;
        rg_seeds[3] = rg_left;
    }
    /* $B178: the parity of the level this life starts in. */
    rg_life = (uint8_t)((rg_level << 7) | (bridges & 0x7Fu));
    rg_flags23 = (rg_level & 1u) ? 0x84u : 0x80u;
    rg_b5 = 6u;
    rg_c3 = 0;
    rg_c4 = 0x1Cu;
    rg_c5 = 0x24u;
    rg_c8 = 0;
    rg_c9 = 0;
    rg_bd = 0;
    rg_bc = 0;
    rg_be = 0;
    rg_tpl_off = 0;
    rg_target = 0;
    rg_v35 = 0;
    rg_b7 = 0;
    river_obj_count = 0x0Bu;
    river_obj_v37 = 0;
    river_obj_aux[0] = 0;
    river_obj_x[0] = 0;
    river_obj_flags[0] = 0;

#ifndef __SDCC_sm83
    /* Playfield attributes: palette 2 (all four 16x16 blocks in each byte). */
    for (i = 0; i < 64u; i++) {
        set_vram_byte((uint8_t *)(PPU_NT1_ATTR + i), 0xAAu);
    }
#else
    /* Clear the BG attribute map (VRAM bank 1): palette 0, tile bank 0. */
    VBK_REG = 1;
    fill_bkg_rect(0, 0, 32, 32, 0);
    VBK_REG = 0;
    for (i = 0; i < RIVER_RING_ROWS; i++) {
        river_slot_attr[i] = 0u;
    }
#endif

    /* Start with an empty (black) world, like the Atari: the river scrolls
     * into view from the top and everything below it is black.  The
     * procedural rows replace the black ones as the playfield scrolls. */
    {
        uint8_t c;
        for (i = 0; i < RIVER_ROWS; i++) {
            rj_addr = (uint16_t)(PPU_NT1_BASE + ((uint16_t)i << 5));
            for (c = 0; c < RIVER_COLS; c++) {
                set_vram_byte((uint8_t *)(rj_addr + c), TILE_BLACK);
            }
            river_left_x[i] = 0u;
            river_right_x[i] = 0u;
        }
    }

    river_road_left = 0;
    river_road_slot = 0xFFu;
    river_road_stage = 0;
    river_flip_age = 0xFFu;
    river_land_alt = (uint8_t)((bridges & 1u) ^ 1u);    /* odd bridge: dark $D2 */
    river_job_pal = river_land_alt ? PLAYFIELD_ALT_PALETTE : PLAYFIELD_PALETTE;
    river_step = 0;
    river_distance = 0;
    river_block_index = 0;
    river_slot = (uint8_t)(RIVER_RING_ROWS - 1u);
    river_freeze = 0;
    river_job = 0;
    river_job_margin = 0;
    river_live = 0;
    river_owed = 0;
    river_acc = 0;
    river_vel = RIVER_VEL_CRUISE;
    river_black_rows = RIVER_RING_ROWS;
    memset(river_col_kind, 3, sizeof(river_col_kind));
    hud_set_playfield_scroll(0);

    /* Run the generator one tile row ahead of the display: the display
     * reveals a row eight frames after the generator completes it, which
     * leaves that long to stream its patterns through the small VRAM
     * transfer buffer. */
    for (i = 0; i < 8u; i++) {
        river_generate_raw_row();
    }
    river_finish_tile_row();
    river_job_left = 255u;              /* the display is off: no per-pass limit */
    while (river_job) {
        river_run_job();
    }

    /* The game starts on a road, as on the Atari: the first rows above the
     * very first tile row (no bridge object, only the recoloured land). */
#ifndef RIVER_NO_START_ROAD         /* the host check compares a road-free model */
    river_road_begin();
#endif
}

/* variant: 0..3 = start level 1/5/20/50.  Returns the start bridge number. */
uint8_t river_init(uint8_t variant) {
    river_variant = variant & 3u;
    river_start(0, atari_level_descriptors[river_variant * 6u + 4u]);
    return atari_level_descriptors[river_variant * 6u + 4u];
}

#ifndef __SDCC_sm83
/* Two-player games: exchange the generator's per-player part of $60-$69
 * (level and the LFSR/left checkpoint) with blk[0..4] ($B247). */
void river_swap_checkpoint(uint8_t *blk) {
    uint8_t t = rg_level;
    uint8_t i;

    rg_level = blk[0];
    blk[0] = t;
    for (i = 0; i < 4u; i++) {   /* no calls inside: nothing to alias */
        t = rg_seeds[i];
        rg_seeds[i] = blk[1u + i];
        blk[1u + i] = t;
    }
}
#endif

/* The jet was lost: pick the checkpoint like $B1C0 and restart the river
 * there.  The generator runs ahead of the jet, so rg_seeds may already belong
 * to the next section:
 *  - the level parity ($23 bit 2) flips when the generator enters the bridge
 *    approach, i.e. when it is a section ahead of this life's start;
 *  - if the jet has destroyed a bridge since, the checkpoint moves up to the
 *    generator's present position (only while it is still in the approach);
 *  - if not, and the generator has passed the bridge already, the checkpoint
 *    goes back one section (rg_prev, level - 1). */
void river_restart(uint8_t bridges) {
    uint8_t x;

    if (((rg_life >> 5) ^ rg_flags23) & 0x04u) {
        if (bridges != (uint8_t)(rg_life & 0x7Fu)) {
            if (rg_flags23 & 0x80u) {
                river_level_advance();
            }
        } else if (!(rg_flags23 & 0x80u)) {
            x = (uint8_t)(rg_level - 1u);
            rg_level = (x == 0x4Eu) ? 0x50u : x;
            rg_seeds[0] = rg_prev[0];
            rg_seeds[1] = rg_prev[1];
            rg_seeds[2] = rg_prev[2];
            rg_seeds[3] = rg_prev[3];
        }
    }
    river_start(1, bridges);
}

/* Scanline of the top edge of a ring slot (the slot may be in the hidden rows
 * above the window, which map to 0..35).  Slots that are still further above
 * wrap to the bottom of the window (large values). */
static uint8_t river_slot_top(uint8_t slot) {
    uint16_t bg_y = ((uint16_t)slot * 8u + RIVER_SCROLL_HEIGHT - playfield_scroll_y) %
                    RIVER_SCROLL_HEIGHT;

    return (bg_y >= (uint16_t)(RIVER_SCROLL_HEIGHT - PLAYFIELD_SCANLINE))
        ? (uint8_t)(bg_y - (RIVER_SCROLL_HEIGHT - PLAYFIELD_SCANLINE))
        : (uint8_t)(bg_y + PLAYFIELD_SCANLINE);
}

/* Scanline of the top edge of the road once its first tile row has been
 * written (river_road_slot is set), else 0xFF.  The road's second tile row is
 * the slot above the first. */
uint8_t river_road_top(void) {
    return (river_road_slot == 0xFFu) ? 0xFFu
                                      : river_slot_top((uint8_t)(river_road_slot - 1u));
}

/* Schedule a 16-row road on the next whole pair of tile rows.  Called by the
 * spawner on the last raw row of a tile row, and at the start of the game. */
void river_road_begin(void) {
    /* Slot of the next tile row; the road's first tile row must be an odd
     * slot (the first of its 16x16 attribute row). */
    rj_c = (river_slot == 0u) ? (uint8_t)(RIVER_RING_ROWS - 1u)
                              : (uint8_t)(river_slot - 1u);
    river_road_left = 16u;
    if (!(rj_c & 1u)) {
        river_road_left = 24u;          /* skip one tile row */
        rj_c = (rj_c == 0u) ? (uint8_t)(RIVER_RING_ROWS - 1u)
                            : (uint8_t)(rj_c - 1u);
    }
}

uint8_t river_slot_at_scanline(uint8_t scanline) {
    uint16_t bg_y = (uint16_t)playfield_scroll_y +
                    (uint16_t)(scanline - PLAYFIELD_SCANLINE);

    if (bg_y >= RIVER_SCROLL_HEIGHT) {
        bg_y -= RIVER_SCROLL_HEIGHT;
    }
    return (uint8_t)(bg_y >> 3);
}

/* File-scope: both live across calls (see AGENTS.md rule 3). */
static uint8_t ru_next;
static uint8_t ru_rows;
static uint8_t ru_gen;
uint8_t river_gen_budget = RIVER_GEN_PER_PASS;  /* rows this pass may generate (main.c) */
/* Pacing counters, read by tests/gb/profile_play.mjs through the .noi: scroll
 * steps lost to the owed-rows clamp (each one is a visible hitch) and the
 * deepest the owed rows got. */
uint8_t river_clamps;
uint8_t river_owed_max;

/* Entity count from which the heavy entity pass keeps its one-row budget even
 * when behind: its two-row version ran late in about 4 of 10 passes at level 50. */
#define RIVER_ENT_BUMP_MAX  9u

void river_update(void) {
#ifndef __SDCC_sm83
    /* GBC keeps the direct set_bkg_data / set_vram_byte path: river_put() is a
     * no-op on the C paths, so the "live" buffer path would drop every upload. */
    river_live = 1u;
#endif
    /* Behind schedule: spend more now, a clamped scroll is worse than a late
     * frame.  A budget of 1 marks the heavy entity pass, which leaves the tile
     * job to the next pass unless the generator has fallen behind. */
#ifdef __SDCC_mos6502
    if (river_gen_budget == 1u && river_owed >= 2u && entity_count < RIVER_ENT_BUMP_MAX) {
#else
    /* the host harness does not link the entities; pacing does not change rows */
    if (river_gen_budget == 1u && river_owed >= 2u) {
#endif
        river_gen_budget = 2u;
    }
    river_job_left = RIVER_JOB_COLS;
    if (river_job && river_gen_budget != 1u) {
        river_run_job();
    }

    /* Scroll in time, not in passes.  The jet accelerates and decelerates
     * toward the throttle's target (slow = 0.5 rows/frame, cruise = 1,
     * fast = 2) instead of jumping; fractions accumulate in river_acc.  Each scrolled
     * row is "owed" one generated row; the generator runs up to
     * RIVER_GEN_PER_PASS rows per pass and only while no tile row is waiting
     * for classification, so a slow frame delays generation, never the
     * scroll.  The generator is 8 rows ahead of the display, which is the
     * slack river_owed may use. */
    /* river_scrolled accumulates until entities_update() consumes it;
     * river_rows_last is just this call's share (entities_follow()). */
    river_rows_last = 0;
    if (!river_freeze) {
        ru_target = (river_speed == 0u) ? RIVER_VEL_SLOW :
                    (river_speed == 1u) ? RIVER_VEL_CRUISE : RIVER_VEL_FAST;
        ru_sum = river_acc;
        for (ru_rows = 0; ru_rows < game_ticks; ru_rows++) {
            ru_sum += river_vel;
            if (river_vel < ru_target) {
                river_vel = (ru_target - river_vel > RIVER_ACCEL)
                    ? (uint8_t)(river_vel + RIVER_ACCEL) : ru_target;
            } else if (river_vel > ru_target) {
                river_vel = (river_vel - ru_target > RIVER_ACCEL)
                    ? (uint8_t)(river_vel - RIVER_ACCEL) : ru_target;
            }
        }
        ru_rows = (uint8_t)(ru_sum >> 5);
        river_acc = (uint8_t)(ru_sum & 31u);
        if (ru_rows > (uint8_t)(RIVER_MAX_OWED - river_owed)) {
            ru_rows = (uint8_t)(RIVER_MAX_OWED - river_owed);
            river_clamps++;
        }
        ru_next = playfield_scroll_y;
        for (ru_gen = 0; ru_gen < ru_rows; ru_gen++) {
            ru_next = (ru_next == 0u)
                ? (uint8_t)(RIVER_SCROLL_HEIGHT - 1u)
                : (uint8_t)(ru_next - 1u);
        }
        river_owed = (uint8_t)(river_owed + ru_rows);
        if (river_owed > river_owed_max) {
            river_owed_max = river_owed;
        }
        river_scrolled = (uint8_t)(river_scrolled + ru_rows);
        river_rows_last = ru_rows;
        if (ru_rows) {
            hud_set_playfield_scroll(ru_next);
        }
    }

    ru_gen = river_gen_budget;
    while (river_owed && ru_gen && !river_job) {
        river_generate_raw_row();
        if (river_block_index == 8u) {
            river_finish_tile_row();
            if (river_gen_budget != 1u) {
                river_run_job();
            }
        }
        river_owed--;
        ru_gen--;
    }
}
