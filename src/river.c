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
 * The NES renderer at the bottom is a deliberate Phase 4 approximation:
 * every 8x8 tile is classified and replaced by the nearest tile from a small
 * 51-tile edge atlas (solid land/water/AA plus left/right edges at 8 phases
 * and 3 slopes).  The exact per-tile cache is the next refinement; the
 * generator state, 48-byte rows and collision ring are exact.
 */
#include <nes/nes.h>
#include <stdint.h>
#include <string.h>

#include "config.h"
#include "atari_data.h"
#include "hud.h"
#include "river.h"

/* 48-byte mode-E row.  The original's last fill writes one byte past the
 * row (it overwrites the next row's first land byte); keep one spare. */
static uint8_t river_raw_row[49];

/* One 32-byte nametable row: six land tiles, 20 river tiles, six land. */
static uint8_t river_row_buf[RIVER_COLS];

/* Interleaved 2bpp patterns for the 20 tiles of the pending tile row. */
static uint8_t river_tile_pat[20 * 16];

/* Chosen tile ID for each column of the pending row (dedupe scratch). */
static uint8_t river_row_ids[20];

/* Rotating exact-pattern cache slot.  The 26 visible ring rows need at
 * most 203 slots, so TILE_RIVER_COUNT (207) never reuses a visible slot. */
static uint8_t river_alloc;

/* ------------------------------------------------------------------------- */
/* Generator state ($52-$60, $AA-$AC, $B4-$BE, $C3-$C9)                      */
/* ------------------------------------------------------------------------- */

static uint8_t rg_left;           /* $52 */
static uint8_t rg_width;          /* $53 */
static uint8_t rg_phase;          /* $54 */
static uint8_t rg_rowcount;       /* $2F */
static uint8_t rg_island_flags;   /* $2D */
static uint8_t rg_feature;        /* $36 */
static uint8_t rg_section_timer;  /* $5A */
static uint8_t rg_countdown;      /* $5D */
static uint8_t rg_island_width;   /* $5F */
static uint8_t rg_margin;         /* $2C */
static uint8_t rg_margin_flag;    /* $052D */
static uint8_t rg_level;          /* $60 */
static uint8_t rg_flags23;        /* $23 */
static uint8_t rg_lfsr_a;         /* $AA */
static uint8_t rg_lfsr_b;         /* $AB */
static uint8_t rg_lfsr_c;         /* $AC */
static uint8_t rg_b5;             /* $B5 */
static uint8_t rg_c3, rg_c4, rg_c5, rg_c8, rg_c9;
static uint8_t rg_bd, rg_bc, rg_be; /* feature type / width / length */
static uint16_t rg_tpl_off;       /* offset into atari_river_templates */
static uint8_t rg_target;         /* $34 */
static int8_t rg_v35;             /* $35 */
static uint8_t rg_seeds[4];       /* $66-$69 */
static uint8_t rg_b1, rg_b7;
static uint8_t rg_hit_flash;

/* Object-avoidance inputs.  Phase 4 has no objects; Phase 5 fills these. */
uint8_t river_obj_count;
uint8_t river_obj_aux[11];
uint8_t river_obj_x[11];
uint8_t river_obj_flags[11];
uint8_t river_obj_v37;

static uint8_t rg_row_left;
static uint8_t rg_row_right;
static uint8_t rg_row_island_left;
static uint8_t rg_row_island_right;

/* Renderer scratch.  rg_ysave is file-scope on purpose: SDCC can alias a
 * block-local saved row value with the register used by the next call, which
 * silently shifts the island/feature overlay. */
static uint8_t rg_row_pos;
static uint8_t rg_row_b4;
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

/* Test hooks: last generated 48-byte row and the state after it.  These are
 * exported through the .noi file so Mesen tests can compare the C port with
 * tools/river_model.py. */
uint8_t river_debug_row[48];
volatile uint8_t river_debug_state[16];

static uint16_t river_step;
static uint8_t river_slot;
static uint8_t river_block_index;

/* ------------------------------------------------------------------------- */
/* LFSR / level state                                                        */
/* ------------------------------------------------------------------------- */

static void river_lfsr_step(void) {
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
}

static void river_level_advance(void) {
    uint8_t x = rg_level;

    if (x >= 0x50u) {
        x = 0x4Eu;
    }
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
    rg_b1 = y;
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

static void river_fill(uint8_t y, uint8_t value, uint8_t edge) {
    uint8_t i;

    if (y & 0x80u) {
        return;
    }
    if (!edge && rg_hit_flash) {
        value = 0xAAu;
    }
    for (i = 0; i <= y; i++) {
        river_raw_row[rg_row_pos + i] = value;
    }
    rg_row_pos = (uint8_t)(rg_row_pos + y);
    rg_row_b4 = (uint8_t)(rg_row_b4 + y);
}

static void river_copy_template(void) {
    uint8_t i;

    for (i = 0; i < rg_bc; i++) {
        river_raw_row[rg_row_pos + i] = atari_river_templates[rg_tpl_off + i];
    }
    rg_row_pos = (uint8_t)(rg_row_pos + rg_bc);
    rg_row_b4 = (uint8_t)(rg_row_b4 + rg_bc);
    rg_tpl_off = (uint16_t)(rg_tpl_off + rg_bc);
}

static void river_pattern(uint8_t index, uint8_t flash) {
    const unsigned char *table;
    uint8_t off;

    if (flash && rg_hit_flash) {
        index = (uint8_t)(index + 0x10u);
    }
    table = (index < 0x10u) ? atari_bank_patterns
                            : atari_bank_patterns_island;
    off = (uint8_t)(index & 0x0Fu);
    river_raw_row[rg_row_pos] = table[off];
    river_raw_row[rg_row_pos + 1u] = table[off + 1u];
    rg_row_pos = (uint8_t)(rg_row_pos + 2u);
    rg_row_b4 = (uint8_t)(rg_row_b4 + 2u);
}

static void river_render_row(void) {
    uint8_t island = (uint8_t)(rg_island_flags & 0x80u);
    uint8_t x, y, a, b7;

    rg_row_pos = 0;
    rg_row_b4 = 0;
    memset(river_raw_row, 0x55, 48);
    rg_row_island_left = 0;
    rg_row_island_right = 0;

    /* --- left bank and left-side feature ----------------------------- */
    x = island ? rg_left : (uint8_t)(rg_left + rg_phase);
    rg_row_left = x;
    y = (uint8_t)((x >> 2) - 1u);
    if (!island && (rg_feature & 0x80u) && rg_rowcount < rg_be) {
        rg_ysave = y;
        y = rg_bd;
        river_fill(y, 0x55u, 0u);
        river_copy_template();
        y = (uint8_t)(rg_ysave - rg_bc - rg_bd);
    }
    river_fill(y, 0x55u, 0u);
    river_pattern((uint8_t)((x & 3u) * 4u), 1u);

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
            y = (uint8_t)((a >> 2) - rg_row_b4);
            river_fill(y, 0xFFu, 0u);
            a = (uint8_t)((x & 3u) * 4u + 2u);
            if (rg_phase < 8u) {
                a = (uint8_t)(a + 0x10u);
            }
            river_pattern(a, 0u);
            b7 = x;
            a = (uint8_t)((rg_phase << 1) + b7);
            x = a;
            rg_row_island_right = x;
            y = (uint8_t)((a >> 2) - rg_row_b4 - 1u);
            if (rg_phase < 8u) {
                river_fill(y, 0xAAu, 1u);
                river_pattern((uint8_t)((x & 3u) * 4u + 0x10u), 0u);
            } else {
                if (rg_feature && rg_rowcount < rg_be) {
                    b7 = (uint8_t)(rg_bd - rg_row_b4);
                    y = (uint8_t)(y - b7 - rg_bc);
                    rg_ysave = y;
                    y = b7;
                    river_fill(y, 0x55u, 0u);
                    river_copy_template();
                    y = rg_ysave;
                }
                river_fill(y, 0x55u, 0u);
                river_pattern((uint8_t)((x & 3u) * 4u), 0u);
            }
            a = (uint8_t)(rg_width >> 1);
        }
    }

    b7 = x;
    a = (uint8_t)(a + b7);
    x = a;
    rg_row_right = x;
    y = (uint8_t)((a >> 2) - rg_row_b4);
    river_fill(y, 0xFFu, 0u);
    river_pattern((uint8_t)((x & 3u) * 4u + 2u), 1u);

    /* --- right-side feature and right land --------------------------- */
    y = (uint8_t)(0x30u - rg_row_b4);
    if (!island && rg_feature && !(rg_feature & 0x80u) &&
        rg_rowcount < rg_be) {
        y = (uint8_t)(y - rg_bd - rg_bc);
        y++;
        river_fill(y, 0x55u, 0u);
        river_copy_template();
        y = (uint8_t)(rg_bd - 1u);
    }
    river_fill(y, 0x55u, 0u);

    if (rg_hit_flash) {
        rg_hit_flash--;
    }
}

/* ------------------------------------------------------------------------- */
/* State machine ($A6DE-$A9D5)                                               */
/* ------------------------------------------------------------------------- */

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
            } else if (rg_level < 0x0Au) {
                rg_c4 = 0x19u;
                rg_c5 = 0x28u;
            } else {
                rg_c4 = 0x0Du;
                rg_c5 = 0x34u;
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
    river_render_row();
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
 * cache is a 207-slot ring: the 26 visible nametable rows need at most 203
 * distinct non-solid blocks (measured over 40,000 model rows), so the ring
 * never overwrites a block that is still on screen.
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

static void river_cache_init(void) {
    set_bkg_data(TILE_LAND, 1, river_land_pat);
    set_bkg_data(TILE_WATER, 1, river_water_pat);
    river_alloc = 0u;
}

/* Convert two Atari mode-E bytes into one 8-pixel NES tile row. */
static void river_row_to_tile(uint8_t *dst, uint8_t b0, uint8_t b1) {
    uint8_t plane0 = 0, plane1 = 0;
    uint8_t i, v;

    for (i = 0; i < 4u; i++) {
        v = (uint8_t)((b0 >> (6u - 2u * i)) & 3u);
        plane0 = (uint8_t)((plane0 << 1) | (v & 1u));
        plane1 = (uint8_t)((plane1 << 1) | ((v >> 1) & 1u));
    }
    for (i = 0; i < 4u; i++) {
        v = (uint8_t)((b1 >> (6u - 2u * i)) & 3u);
        plane0 = (uint8_t)((plane0 << 1) | (v & 1u));
        plane1 = (uint8_t)((plane1 << 1) | ((v >> 1) & 1u));
    }
    dst[0] = plane0;
    dst[1] = plane1;
}

static uint8_t river_pat_equal(const uint8_t *a, const uint8_t *b) {
    uint8_t i;

    for (i = 0; i < 16u; i++) {
        if (a[i] != b[i]) {
            return 0u;
        }
    }
    return 1u;
}

/* Return the tile ID for one block, allocating and uploading if needed. */
static uint8_t river_cache_tile(uint8_t c) {
    const uint8_t *pat = &river_tile_pat[(uint16_t)c * 16u];
    uint8_t k;

    if (river_pat_equal(pat, river_land_pat)) {
        return TILE_LAND;
    }
    if (river_pat_equal(pat, river_water_pat)) {
        return TILE_WATER;
    }
    for (k = 0; k < c; k++) {
        if (river_pat_equal(pat, &river_tile_pat[(uint16_t)k * 16u])) {
            return river_row_ids[k];
        }
    }

    {
        uint8_t id = (uint8_t)(TILE_RIVER_BASE + river_alloc);
        set_bkg_data(id, 1, pat);
        river_alloc++;
        if (river_alloc >= TILE_RIVER_COUNT) {
            river_alloc = 0u;
        }
        return id;
    }
}

/* ------------------------------------------------------------------------- */
/* Ring / nametable streaming                                                */
/* ------------------------------------------------------------------------- */

static void river_write_tile_row(uint8_t slot);

/* Generate one raw Atari scanline and add it to the pending 8x8 tile row. */
static void river_generate_raw_row(void) {
    uint8_t c;

    if (river_block_index == 0u) {
        /* New rows enter at the top and scroll down: the ring advances
         * backwards, one row above the current visible top. */
        river_slot = (river_slot == 0u)
            ? (uint8_t)(RIVER_RING_ROWS - 1u)
            : (uint8_t)(river_slot - 1u);
    }

    river_step_state();

    for (c = 0; c < 20u; c++) {
        /* The Atari playfield is stored bottom-up relative to its display:
         * raw row 7 is the top scanline of the NES tile.  Reversing the row
         * order here inverts the whole playfield (trees included). */
        river_row_to_tile(
            &river_tile_pat[c * 16u + (7u - river_block_index) * 2u],
            river_raw_row[4u + c * 2u],
            river_raw_row[5u + c * 2u]);
    }
    if (river_block_index == 3u) {
        river_left_x[river_slot] = rg_row_left;
        river_right_x[river_slot] = rg_row_right;
        river_island_left_x[river_slot] = rg_row_island_left;
        river_island_right_x[river_slot] = rg_row_island_right;
        river_flags[river_slot] = (uint8_t)(
            ((rg_flags23 & 0x80u) ? RIVER_FLAG_BRIDGE : 0u) |
            ((rg_island_flags & 0x80u) ? RIVER_FLAG_ISLAND : 0u) |
            (rg_feature ? RIVER_FLAG_FEATURE : 0u) |
            (rg_hit_flash ? RIVER_FLAG_FLASH : 0u));
    }
    if (river_block_index == 7u) {
        memcpy(river_debug_row, river_raw_row, 48);
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
        river_debug_state[12] = rg_bd;
        river_debug_state[13] = rg_bc;
        river_debug_state[14] = rg_be;
        river_debug_state[15] = (uint8_t)(rg_tpl_off & 0xFF);
    }
    river_block_index++;
}

/* The 8th raw row completed: build and stream the tile row. */
static void river_finish_tile_row(void) {
    river_write_tile_row(river_slot);
    river_block_index = 0;
    river_step++;
    river_distance = river_step;
}

static void river_write_tile_row(uint8_t slot) {
    uint8_t c;
    uint16_t addr = (uint16_t)(PPU_NT1_BASE + ((uint16_t)slot << 5));

    for (c = 0; c < 6u; c++) {
        river_row_buf[c] = TILE_LAND;
    }
    for (c = 0; c < 20u; c++) {
        uint8_t id = river_cache_tile(c);
        river_row_ids[c] = id;
        river_row_buf[6u + c] = id;
    }
    for (c = 0; c < 6u; c++) {
        river_row_buf[26u + c] = TILE_LAND;
    }
    for (c = 0; c < RIVER_COLS; c++) {
        set_vram_byte((uint8_t *)(addr + c), river_row_buf[c]);
    }
}



/* ------------------------------------------------------------------------- */
/* Public interface                                                          */
/* ------------------------------------------------------------------------- */

void river_init_tiles(void) {
    river_cache_init();
}

void river_init(void) {
    uint8_t i;

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
    rg_lfsr_a = atari_level_descriptors[0];
    rg_lfsr_b = atari_level_descriptors[1];
    rg_lfsr_c = atari_level_descriptors[2];
    rg_level = atari_level_descriptors[4];
    rg_seeds[0] = rg_lfsr_a;
    rg_seeds[1] = rg_lfsr_b;
    rg_seeds[2] = rg_lfsr_c;
    rg_seeds[3] = rg_left;
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
    rg_b1 = 0;
    rg_b7 = 0;
    rg_hit_flash = 0;
    river_obj_count = 0;
    river_obj_v37 = 0;
    for (i = 0; i < 11u; i++) {
        river_obj_aux[i] = 0;
        river_obj_x[i] = 0;
        river_obj_flags[i] = 0;
    }

    /* Playfield attributes: palette 2 (all four 16x16 blocks in each byte). */
    for (i = 0; i < 64u; i++) {
        set_vram_byte((uint8_t *)(PPU_NT1_ATTR + i), 0xAAu);
    }

    /* Fill the ring with a straight channel.  Generating 30 full tile rows
     * at boot would take ~240 raw rows; the procedural rows replace this
     * channel as the playfield scrolls (one tile row every 8 pixels). */
    {
        uint8_t c;
        for (c = 0; c < 6u; c++) {
            river_row_buf[c] = TILE_LAND;
            river_row_buf[26u + c] = TILE_LAND;
        }
        for (c = 0; c < 20u; c++) {
            river_row_buf[6u + c] = TILE_WATER;
        }
        for (i = 0; i < RIVER_ROWS; i++) {
            uint16_t addr = (uint16_t)(PPU_NT1_BASE + ((uint16_t)i << 5));
            for (c = 0; c < RIVER_COLS; c++) {
                set_vram_byte((uint8_t *)(addr + c), river_row_buf[c]);
            }
            river_left_x[i] = 3u;
            river_right_x[i] = 28u;
        }
    }

    river_step = 0;
    river_distance = 0;
    river_block_index = 0;
    river_slot = (uint8_t)(RIVER_RING_ROWS - 1u);
    river_freeze = 0;
    hud_set_playfield_scroll(0);
}

void river_update(void) {
    uint8_t next;

    if (river_freeze) {
        return;
    }

    /* Scroll down: the playfield moves in the opposite direction to the
     * generator stream, matching the Atari (new terrain enters at the top). */
    next = (playfield_scroll_y == 0u)
        ? (uint8_t)(RIVER_SCROLL_HEIGHT - 1u)
        : (uint8_t)(playfield_scroll_y - 1u);

    /* One raw Atari scanline per frame keeps the per-frame cost flat.  The
     * 8th row completes one 8x8 tile row; only then do we write 32 bytes of
     * nametable data. */
    river_generate_raw_row();
    if (river_block_index == 8u) {
        river_finish_tile_row();
    }

    hud_set_playfield_scroll(next);
}
