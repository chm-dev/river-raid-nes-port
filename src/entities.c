/*
 * entities.c - enemies, fuel depots and explosions.
 *
 * Objects are placed by a port of the Atari spawner ($AB80-$AE96, called by
 * the river generator after each row) and patrol with the Atari rules: they
 * stand still until a wake-up lottery picks them, then walk on alternate
 * frames and turn at the banks.  Types, shapes and scores come from the
 * extracted Atari tables.
 */
#include <nes/nes.h>
#include <stdint.h>

#include "config.h"
#include "atari_data.h"
#include "entity_gfx.h"
#include "hud.h"
#include "river.h"
#include "entities.h"
#include "player.h"

/* Separate arrays (not an array of structs): SDCC indexes these with plain
 * absolute,X addressing, where a 5-byte struct needs a multiply per access. */
uint8_t ent_type[MAX_ENTITIES];
uint8_t ent_x[MAX_ENTITIES];
uint8_t ent_y[MAX_ENTITIES];
uint8_t ent_flags[MAX_ENTITIES];
uint8_t ent_aux[MAX_ENTITIES];
uint8_t entity_count;

#define ENT_TILE_BASE           (SPRITE_TILE_BASE + 24u)
#define ENT_OAM_FIRST           2u
#define ENT_OAM_LAST            43u
/* Atari SIZEP3: ships are double width, the $0E bridge object quad width. */
#define ent_width(type)         ((uint8_t)(ent_gfx_cols[type] << 3))
#define ENT_EXPLODE_TICKS       8u      /* Atari: type 2..6 advance every 8th frame */

static uint8_t ent_rng = 0x5Au;
static uint8_t ent_frame;       /* $18: free-running frame counter */
static uint8_t ent_wake_acc;    /* frames since the last wake-up draw */
static uint8_t sp_hold;         /* $32: rows left until the new object is built */
static uint8_t sp_slot;         /* entity slot of the object being built */
static uint8_t bridge_wait;     /* a bridge road is scheduled; its bar is not created yet */
static uint8_t bridge_x;        /* X of that bar */
static uint8_t ent_anim;
static uint8_t ent_oam_used;

/* File-scope scratch (SDCC aliases block locals across calls). */
static uint8_t e_i;
static uint8_t e_t;
static uint8_t e_slot;
static uint8_t e_l;
static uint8_t e_r;
static uint8_t e_x;
static uint8_t e_y;
static uint8_t e_type;
static uint8_t e_rows;
static uint8_t e_tile;
static uint8_t e_w;
static uint8_t e_cols;
static uint8_t e_flip;
static uint8_t e_prop;
static uint8_t e_c;
static uint8_t e_src;
static volatile OAM_item_t *e_o;
static uint8_t e_oam;
static uint8_t e_px;
static uint8_t e_n;

static uint8_t ent_random(void) {
    ent_rng ^= (uint8_t)(ent_rng << 7);
    ent_rng ^= (uint8_t)(ent_rng >> 5);
    ent_rng ^= (uint8_t)(ent_rng << 3);
    return ent_rng;
}

void entities_clear(void) BANKED {
    for (e_i = 0; e_i < MAX_ENTITIES; e_i++) {
        ent_type[e_i] = ENTITY_NONE;
        ent_x[e_i] = 0;
        ent_y[e_i] = 0;
        ent_flags[e_i] = 0;
        ent_aux[e_i] = 0;
    }
    entity_count = 0;
    sp_hold = 0;
    bridge_wait = 0;
    river_obj_count = 0x0Bu;
}

void entities_init(void) BANKED {
    entities_clear();
}

void entities_init_graphics(void) BANKED {
    palette_color_t pal[4];

    /* Sprite palettes 1..3 (bridge, helicopters, ships); the HUD label
     * sprites use entry 1 of palette 2, which is black. */
    pal[0] = BG_COLOR_BLACK;
    for (e_i = 0; e_i < 3u; e_i++) {
        pal[1] = ent_gfx_palette[e_i][0];
        pal[2] = ent_gfx_palette[e_i][1];
        pal[3] = ent_gfx_palette[e_i][2];
        set_sprite_palette((uint8_t)(e_i + 1u), 1, pal);
    }
    set_sprite_data(ENT_TILE_BASE, ENT_GFX_TILES, ent_gfx_chr);
}

uint8_t entities_tile(uint8_t aux) BANKED {
    return (uint8_t)(ENT_TILE_BASE + ent_gfx_first[aux]);
}

uint8_t entities_shot_tile(void) BANKED {
    return (uint8_t)(ENT_TILE_BASE + ENT_GFX_SHOT);
}

static uint8_t bcd_carry;

/* a + b in packed BCD; sets bcd_carry. */
static uint8_t bcd_add(uint8_t a, uint8_t b) {
    uint16_t s = (uint16_t)(a & 0x0Fu) + (b & 0x0Fu);

    if (s > 9u) {
        s += 6u;
    }
    s += (uint16_t)(a & 0xF0u) + (b & 0xF0u);
    bcd_carry = 0;
    if (s > 0x99u) {
        s += 0x60u;
        bcd_carry = 1;
    }
    return (uint8_t)s;
}

void entities_add_score(uint8_t bcd_tens) BANKED {
    /* Table values are tens: the low digit lands in the tens column and
     * the high digit in the hundreds column. */
    hud_score_bcd[2] = bcd_add(hud_score_bcd[2], (uint8_t)((bcd_tens & 0x0Fu) << 4));
    hud_score_bcd[1] = bcd_add(hud_score_bcd[1], (uint8_t)((bcd_tens >> 4) + bcd_carry));
    hud_score_bcd[0] = bcd_add(hud_score_bcd[0], bcd_carry);

    /* Track the hi-score. */
    if (hud_score_bcd[0] > hud_hi_bcd[0] ||
        (hud_score_bcd[0] == hud_hi_bcd[0] &&
         (hud_score_bcd[1] > hud_hi_bcd[1] ||
          (hud_score_bcd[1] == hud_hi_bcd[1] &&
           hud_score_bcd[2] > hud_hi_bcd[2])))) {
        hud_hi_bcd[0] = hud_score_bcd[0];
        hud_hi_bcd[1] = hud_score_bcd[1];
        hud_hi_bcd[2] = hud_score_bcd[2];
    }
}

/* ------------------------------------------------------------------------- */
/* Spawner: a port of the Atari object block $AB80-$AE96                      */
/* ------------------------------------------------------------------------- */
/*
 * The Atari does not drop objects at random: every 16th generated row
 * (rg_rowcount & 15 == 0) it picks an object from the generator's own state
 * (level, LFSR bytes, river width/island flags) and places it relative to
 * the bank that was just drawn.  While the object is "under construction"
 * ($32 rows) nothing else spawns and the generator's bank clamp steers
 * around it (river_obj_*).  tools/object_model.py is the Python twin; it
 * replays a live Altirra trace row for row (tools/object_validate.py).
 *
 * The scratch variables are the e_* file statics: entities_row() is only
 * called from the river generator, never from inside an entity loop.
 */
#define sp_a            e_t     /* 6502 accumulator */
#define sp_c            e_c     /* carry */
#define sp_y            e_type  /* object type (the Y register) */
#define sp_b3           e_l     /* $B3 */
#define sp_b5           e_r     /* $B5 (+2 near the bridge) */
#define sp_b8           e_n     /* $B8: 5 = ship, 7 = bridge */
#define sp_left         e_src   /* status bit 3: start moving left */
#define sp_island       e_w     /* island flag, $2D bit 7 */
#define sp_aa           e_cols  /* $AA copy */
#define sp_b1           e_prop  /* $B1 */
#define SP_CB           0x2Eu   /* $CB (NTSC); the PAL ROM uses $2F */

static void sp_adc(uint8_t v) {
    uint16_t s = (uint16_t)sp_a + v + sp_c;

    sp_a = (uint8_t)s;
    sp_c = (uint8_t)(s >> 8);
}

static void sp_sbc(uint8_t v) {
    int16_t s = (int16_t)sp_a - v - (int16_t)(1u - sp_c);

    sp_a = (uint8_t)s;
    sp_c = (s >= 0) ? 1u : 0u;
}

/* $AC00-$AC8A: which object (sp_y, sp_b8) this attempt produces. */
static void sp_choose(void) {
    sp_b8 = 0;
    if (rg_flags23 & 0x80u) {
        sp_y = 0x0Eu;                       /* the bridge itself */
        sp_b8 = 7u;
        sp_a = (uint8_t)(rg_phase - 6u);    /* $AC00: X from the $54 phase */
        return;
    }
    sp_y = 1u;
    if (rg_rowcount != 0u) {
        /* The odd 16-row slot: blank unless the level is high. */
        sp_a = (uint8_t)(rg_level - 8u);
        if (sp_a & 0x80u) {
            return;
        }
        if (sp_a < 0x20u && (uint8_t)(sp_a << 3) < rg_lfsr_b) {
            return;
        }
    } else {
        sp_y = 0;
        if ((uint8_t)(rg_countdown + rg_section_timer) >= 0x12u) {
            return;                         /* just after a bridge */
        }
        sp_a = (rg_level < SP_CB) ? rg_level : SP_CB;
        sp_a = (uint8_t)((uint8_t)(0x3Fu - sp_a) << 1);
        if (sp_a >= rg_lfsr_b) {
            /* Fuel instead of an enemy, ~half the time, rarer as the level
             * rises. */
            if (rg_lfsr_a & 0x40u) {
                return;
            }
            sp_y = ENT_AUX_FUEL;
            return;
        }
    }
    sp_y = atari_spawn_types[rg_lfsr_b & 7u];
    if (sp_y == 7u && rg_level < 5u) {
        sp_y = 0x0Du;
    }
    if (sp_y == 0x0Cu && (rg_rowcount != 0u || rg_level < 3u)) {
        sp_y = 8u;
    }
    if (sp_y == 0x0Du) {
        sp_b8 = 5u;
    }
}

/* $AC8C-$AD29: X offset in sp_a (before +$20 and the bank), and sp_left.
 * May demote the object to a blank (sp_y = 1) when the river is too narrow. */
static void sp_pick_x(void) {
    sp_left = 0;
    sp_b5 = rg_b5;
    sp_aa = rg_lfsr_a;
    if (rg_island_width != rg_margin) {
        sp_b3 = (rg_island_width > rg_margin) ? rg_island_width : rg_margin;
    } else {
        sp_b3 = rg_island_width;
        if (rg_level & 1u) {
            /* Odd level, steady width: anywhere inside the channel. */
            sp_a = sp_b8 ? 0x37u : 0x3Fu;
            sp_b1 = (uint8_t)(sp_a - sp_b5);
            sp_a = (uint8_t)((sp_aa & 0x3Fu) + sp_b5);
            if (sp_a >= sp_b1) {
                sp_a = sp_b1;
            }
            sp_a = (uint8_t)(sp_a + rg_phase);
            if ((sp_aa & 0x80u) && sp_y != ENT_AUX_FUEL) {
                sp_left = 1u;
            }
            return;
        }
    }

    sp_island = rg_island_flags & 0x80u;
    if ((sp_island ? 0x0Du : 0x0Eu) < sp_b3 && sp_y != 7u) {
        sp_y = 1u;
        return;
    }
    if (rg_countdown == 1u) {
        sp_b5 = (uint8_t)(sp_b5 + 2u);
        sp_c = 1u;
    } else {
        sp_c = ((sp_island ? 0x0Au : 0x0Du) < sp_b3) ? 1u : 0u;
    }
    if (sp_c && sp_y == 0x0Du) {
        sp_b8 = 0;
        sp_y = 8u;
    }

    {
        uint16_t t = (uint16_t)sp_b3 << 2;

        sp_c = (uint8_t)((t >> 8) & 1u);
        sp_a = (uint8_t)t;
    }
    if (sp_a != 0u && sp_island) {
        sp_a ^= 0xFFu;
        sp_adc(0x41u);
        river_obj_v37 = sp_aa;
        if (!(sp_aa & 0x80u)) {
            goto ad1d;
        }
        sp_a ^= 0xFFu;
        sp_adc(0x80u);
        if (sp_a != 0u) {
            goto ad14;
        }
    }
    if (sp_aa & 0x80u) {
        goto ad19;
    }
ad14:
    sp_c = 1u;
    sp_adc(sp_b5);
    if (sp_a != 0u) {
        return;
    }
ad19:
    sp_a ^= 0xFFu;
    sp_adc(0x81u);
ad1d:
    sp_c = (sp_y >= ENT_AUX_FUEL) ? 1u : 0u;
    sp_sbc(8u);
    sp_sbc(sp_b5);
    if (sp_b8) {
        sp_sbc(8u);
    }
    if (sp_y != ENT_AUX_FUEL) {
        sp_left = 1u;
    }
}

/* ------------------------------------------------------------------------- */
/* The bridge tank and its shell ($ADA6-$ADCA, $A21B-$A26A, $A4EA-$A545, $A609-$A671) */
/* ------------------------------------------------------------------------- */
/*
 * Every bridge section drives a tank (Atari P1) across the road.  It appears
 * at X = ($AA & $7F) + $40 (the spawner's LFSR, so the start is fixed by the
 * level), faces right when ($AA & $7F) < $40, and creeps one pixel every 4th
 * frame until the bridge is destroyed ($2E != 0) or its shell is flying.  The
 * state is the Atari's $38 (kept in ent_aux): 4/5 and 6/7 are the two tread
 * frames facing right/left, 3,2,1 the explosion; bit 7 = destroyed.
 *
 * Shooting the bridge while the tank is on it destroys the tank for 750
 * points instead of the bridge's 500.  From the fifth bridge on, shooting it
 * with the tank close and facing it makes the tank fire a shell: a dot that
 * flies 17 frames at 2 px/frame, then swells into a fireball (40 frames) that
 * kills the jet; the tank refires every 57 frames while it is on screen.
 */
#define TANK_STATE_MIN_MOVE     4u      /* states 4..7 are the driving frames */
#define TANK_ANIM_MASK          3u      /* driving frames change every 4th frame */
#define TANK_EXPLODE_MASK       15u     /* explosion frames every 16th */
#define TANK_FIRE_RIGHT         0x29u   /* range of the shot when facing right */
#define TANK_FIRE_LEFT          0x41u
#define TANK_BONUS_TENS         0x75u   /* atari_score_tens[16]: 750 */
#define TANK_FIRE_BRIDGE        5u      /* bridge counter from which it shoots back */
#define SHELL_FLY_FRAMES        0x11u
#define SHELL_CYCLE_FRAMES      0x39u

static const uint8_t tank_next[8] = { 0, 0, 1, 2, 5, 4, 7, 6 };         /* $BAAF */
static const uint8_t shell_dy[17] = {                                    /* $B4AF */
    0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1
};
/* Pixel extents (x offset, width, first row, end row) of the fireball
 * shapes 2..6 inside the 8x16 sprite, for the jet collision. */
static const uint8_t shell_box[20] = {
    3, 2, 8, 12,  2, 4, 7, 13,  2, 4, 6, 14,  1, 6, 5, 15,  0, 8, 4, 16
};

/*
 * These helpers take no parameters and use the file-scope e_* scratch
 * variables: SDCC's mos6502 target allocates locals and parameters statically
 * and RAM is almost gone.  Each documents the scratch it uses; none is live
 * across a call into another helper unless noted.
 */

/* Slot of the live (positioned) tank in e_slot, 0xFF when there is none.
 * Scratch: e_t. */
static void ent_find_tank(void) {
    e_slot = 0xFFu;
    for (e_t = 0; e_t < MAX_ENTITIES; e_t++) {
        if (ent_type[e_t] == ENT_AUX_TANK && !(ent_flags[e_t] & ENT_F_WAIT)) {
            e_slot = e_t;
            return;
        }
    }
}

/* (Re)start shell slot e_src at the muzzle of tank slot e_slot: 4 px into the
 * facing direction, 6 rows above the tank's top ($A60F-$A627).  Scratch: e_c. */
static void ent_shell_init(void) {
    e_c = ((ent_aux[e_slot] & 0x7Fu) >= 6u);
    ent_type[e_src] = ENT_AUX_SHELL;
    ent_flags[e_src] = e_c ? ENT_F_LEFT : 0u;
    ent_x[e_src] = e_c ? (uint8_t)(ent_x[e_slot] - 4u) : (uint8_t)(ent_x[e_slot] + 4u);
    ent_y[e_src] = (uint8_t)(ent_y[e_slot] - 6u);
    ent_aux[e_src] = 0;
}

/* The spawner met a bridge section: put the tank in a free slot.  It has no
 * position until the bridge bar is placed (ENT_F_WAIT).  Scratch: e_t, e_c. */
static void ent_tank_spawn(void) {
    for (e_t = 0; e_t < MAX_ENTITIES; e_t++) {
        if (ent_type[e_t] == ENT_AUX_TANK) {
            return;                         /* $2F | $38 must be 0 */
        }
    }
    for (e_t = 0; e_t < MAX_ENTITIES && ent_type[e_t] != ENTITY_NONE; e_t++) {
    }
    if (e_t == MAX_ENTITIES) {
        return;
    }
    e_c = (uint8_t)(rg_lfsr_a & 0x7Fu);
    ent_type[e_t] = ENT_AUX_TANK;
    ent_x[e_t] = (uint8_t)(e_c + 0x40u);
    ent_aux[e_t] = (e_c < 0x40u) ? 5u : 6u;
    ent_flags[e_t] = ENT_F_WAIT;
    ent_y[e_t] = 0;
    entity_count++;
}

/* The bar in slot e_i got its position (e_i == MAX_ENTITIES: it did not get a
 * slot): the tank drives 4 rows below its top.  Scratch: e_t. */
static void ent_tank_place(void) {
    for (e_t = 0; e_t < MAX_ENTITIES; e_t++) {
        if (ent_type[e_t] == ENT_AUX_TANK && (ent_flags[e_t] & ENT_F_WAIT)) {
            if (e_i >= MAX_ENTITIES) {
                ent_type[e_t] = ENTITY_NONE;    /* no slot for the bar: no tank either */
                entity_count--;
            } else {
                ent_y[e_t] = (uint8_t)(ent_y[e_i] + 4u);
                ent_flags[e_t] = 0;
            }
            return;
        }
    }
}

/* The jet's shot destroyed the bridge in slot e_i ($A4EA-$A545).  Returns the
 * score in tens: 750 when the tank is on the bridge, else 500; may make the
 * tank fire back.  Scratch: e_slot (tank), e_l (bridge X), e_r (tank X),
 * e_px (tank state), e_src (shell slot), e_t. */
static uint8_t ent_bridge_hit(void) {
    ent_find_tank();
    if (e_slot == 0xFFu) {
        return 0x50u;
    }
    e_l = ent_x[e_i];
    e_r = ent_x[e_slot];
    e_px = ent_aux[e_slot];
    ent_flags[e_slot] |= ENT_F_STOP;        /* $2E is no longer 0 */
    if (!(e_px & 0x80u) && (uint16_t)e_r + 8u > e_l &&
        (uint16_t)e_r <= (uint16_t)e_l + 0x1Fu) {
        ent_aux[e_slot] = (uint8_t)(e_px | 0x80u);  /* the bridge falls with the tank on it */
        return TANK_BONUS_TENS;
    }
    if (hud_bridge >= TANK_FIRE_BRIDGE && !(ent_flags[e_slot] & ENT_F_SHOOT) &&
        !(e_px & 0x80u) &&
        (((e_px & 0x7Fu) < 6u) ? ((uint8_t)(e_l - e_r) < TANK_FIRE_RIGHT)
                               : ((uint8_t)(e_r - e_l) < TANK_FIRE_LEFT))) {
        for (e_src = 0; e_src < MAX_ENTITIES && ent_type[e_src] != ENTITY_NONE; e_src++) {
        }
        if (e_src < MAX_ENTITIES) {
            ent_flags[e_slot] |= ENT_F_SHOOT;
            entity_count++;
            ent_shell_init();
        }
    }
    return 0x50u;
}

/* Jet box (8x14 at e_px, e_y) against the swelling fireball in slot e_i;
 * entirely on screen only.  Scratch: e_l, e_r, e_slot. */
static uint8_t ent_shell_hits(void) {
    if (ent_y[e_i] >= 232u) {
        return 0;
    }
    e_slot = (uint8_t)(((ent_aux[e_i] - SHELL_FLY_FRAMES) >> 3) << 2);
    e_l = (uint8_t)(ent_x[e_i] + shell_box[e_slot]);                   /* left */
    e_r = (uint8_t)(e_l + shell_box[(uint8_t)(e_slot + 1u)]);          /* right */
    return e_px < e_r && (uint8_t)(e_px + 8u) > e_l &&
           e_y < (uint8_t)(ent_y[e_i] + shell_box[(uint8_t)(e_slot + 3u)]) &&
           (uint8_t)(e_y + 14u) > (uint8_t)(ent_y[e_i] + shell_box[(uint8_t)(e_slot + 2u)]);
}

/* Called by the river generator after every raw row ($AB80). */
void entities_row(void) BANKED {
    if (player_disable) {
        /* Test hook: the object-free river that tools/river_model.py models. */
        river_obj_count = 0x0Bu;
        return;
    }
    if (river_obj_count != 0x0Bu) {
        if (sp_hold != 0u) {
            sp_hold--;
            return;
        }
        river_obj_count = 0x0Bu;
    }
    if (rg_rowcount & 0x0Fu) {
        return;
    }

    sp_choose();
    if (sp_y >= 2u && sp_y != 0x0Eu) {
        sp_pick_x();
    }

    /* $AD37: the offset is relative to the left bank (held inside 0x18..0x28
     * while the generator is pulling the banks back to the centre). */
    sp_a = (uint8_t)(sp_a + 0x20u);
    e_x = rg_left;
    if (rg_margin_flag & 0x80u) {
        if (e_x < 0x18u) {
            e_x = 0x18u;
        }
        if (e_x >= 0x28u) {
            e_x = 0x28u;
        }
    }
    e_x = (uint8_t)(e_x + sp_a);

    /* $AD5E: a few helicopters become the fast type-10 ones from level 13,
     * one at a time. */
    if (sp_y == 8u && rg_countdown < 0x0Eu && !(rg_lfsr_c & 0x70u) &&
        rg_level >= 0x0Du) {
        for (e_i = 0; e_i < MAX_ENTITIES; e_i++) {
            if (ent_type[e_i] == 10u || ent_type[e_i] == 11u) {
                break;
            }
        }
        if (e_i == MAX_ENTITIES) {
            sp_y = 10u;
        }
    }

    /* $AD88: the generator avoids the object for BB60[type]+4 rows. */
    sp_hold = (uint8_t)(atari_obj_yoff[sp_y] + 3u);
    river_obj_count = 0;
    river_obj_aux[0] = sp_y;
    river_obj_x[0] = e_x;
    river_obj_flags[0] = (sp_y == ENT_AUX_SHIP) ? 1u : (sp_y == 0x0Eu) ? 3u : 0u;

    if (sp_y < 2u) {
        return;                             /* blank placeholder */
    }
    if (sp_y == ENT_AUX_BRIDGE) {
        if (rg_countdown == 1u) {
            /* The road is painted into the river rows; the bar is created in
             * entities_update() once the road's tile rows are on the ring
             * (the generator runs ahead of what the display can address). */
            river_road_begin();
            bridge_x = e_x;
            bridge_wait = 1u;
            ent_tank_spawn();
        }                                   /* else the game-start state: no bridge */
        return;
    }
    for (e_i = 0; e_i < MAX_ENTITIES; e_i++) {
        if (ent_type[e_i] == ENTITY_NONE) {
            break;
        }
    }
    if (e_i == MAX_ENTITIES) {
        return;
    }
    sp_slot = e_i;
    e_rows = ent_gfx_rows[sp_y];
    ent_type[e_i] = sp_y;
    ent_x[e_i] = e_x;
    /* The object sits just above the newest generated row (scanline
     * PLAYFIELD_SCANLINE + owed - 9); entities_update() adds this pass's
     * scroll to it again, so take it off here. */
    ent_y[e_i] = (uint8_t)(PLAYFIELD_SCANLINE + river_owed - 10u - e_rows -
                           river_scrolled);
    ent_flags[e_i] = (uint8_t)(ENT_F_LATCH | (sp_left ? ENT_F_LEFT : 0u));
    ent_aux[e_i] = 0;
    entity_count++;
}

/* The explosion keeps the object's own shape class (colours, height, width)
 * in the high nibble of flags and only swaps the pixels for debris frames
 * 2..6, as the Atari does ($0500 type changes, $050B aux stays). */
static void ent_explode(void) {
    ent_flags[e_i] = (uint8_t)((ent_type[e_i] << 4) | (ent_flags[e_i] & 1u));
    ent_type[e_i] = ENT_AUX_EXPLODE_FIRST;
    ent_aux[e_i] = 0;
}

uint8_t entities_hit_shot(uint8_t sx, uint8_t sy) BANKED {
    for (e_i = 0; e_i < MAX_ENTITIES; e_i++) {
        e_type = ent_type[e_i];
        if (e_type < ENT_AUX_JET || e_type > ENT_AUX_FUEL) {
            continue;                       /* explosions, the tank and its shell */
        }
        e_rows = ent_gfx_rows[e_type];
        e_w = ent_width(e_type);
        if ((uint16_t)sx < (uint16_t)ent_x[e_i] + e_w &&
            (uint16_t)sx + 2u > ent_x[e_i] &&
            (uint16_t)sy < (uint16_t)ent_y[e_i] + e_rows &&
            (uint16_t)sy + 6u > ent_y[e_i]) {
            e_c = atari_score_tens[e_type];
            if (e_type == ENT_AUX_BRIDGE) {
                if (hud_bridge < HUD_BRIDGE_DIGIT_MAX) {
                    hud_bridge++;           /* $A3E9: a bridge hit advances the counter */
                }
                e_c = ent_bridge_hit();
            }
            entities_add_score(e_c);
            ent_explode();
            return 1;
        }
    }
    return 0;
}

uint8_t entities_check_player(uint8_t px, uint8_t py) BANKED {
    for (e_i = 0; e_i < MAX_ENTITIES; e_i++) {
        e_type = ent_type[e_i];
        if (e_type == ENT_AUX_SHELL) {
            /* The fireball only kills once it is swelling ($A428: $50 >= $11). */
            e_px = px;
            e_y = py;
            if (ent_aux[e_i] >= SHELL_FLY_FRAMES && ent_shell_hits()) {
                return ENT_HIT_DEADLY;
            }
            continue;
        }
        if (e_type < ENT_AUX_JET || e_type > ENT_AUX_FUEL) {
            continue;
        }
        e_rows = ent_gfx_rows[e_type];
        e_w = ent_width(e_type);
        if ((uint16_t)px < (uint16_t)ent_x[e_i] + e_w &&
            (uint16_t)px + 8u > ent_x[e_i] &&
            (uint16_t)py < (uint16_t)ent_y[e_i] + e_rows &&
            (uint16_t)py + 14u > ent_y[e_i]) {
            if (e_type == ENT_AUX_FUEL) {
                return ENT_HIT_FUEL;
            }
            return ENT_HIT_DEADLY;
        }
    }
    return ENT_HIT_NONE;
}

/* Atari status bit 0 (set by the DLI from the P3/playfield collision): the
 * object overlaps a bank or island outline. */
static uint8_t ent_touching(void) {
    e_slot = river_slot_at_scanline((uint8_t)(ent_y[e_i] + (e_rows >> 1)));
    e_r = river_right_x[e_slot];
    if (e_r == 0u) {
        return 0;
    }
    e_l = river_left_x[e_slot];
    e_px = (uint8_t)(ent_x[e_i] - SOURCE_X_OFFSET);
    if (e_px <= (uint8_t)(e_l + 2u) || (uint8_t)(e_px + e_w + 2u) >= e_r) {
        return 1;
    }
    if ((river_flags[e_slot] & RIVER_FLAG_ISLAND) &&
        river_island_left_x[e_slot] < river_island_right_x[e_slot] &&
        e_px < (uint8_t)(river_island_right_x[e_slot] + 2u) &&
        (uint8_t)(e_px + e_w + 2u) > river_island_left_x[e_slot]) {
        return 1;
    }
    return 0;
}

static uint8_t ent_cur;         /* frame counter value of the tick being run */

/* One frame of the tank ($A21B-$A26A) for slot e_i.  Scratch: e_c. */
static void ent_tank_tick(void) {
    e_c = ent_aux[e_i];
    if (ent_cur & (((e_c & 0x7Fu) >= TANK_STATE_MIN_MOVE) ? TANK_ANIM_MASK
                                                           : TANK_EXPLODE_MASK)) {
        return;
    }
    if (e_c & 0x80u) {
        e_c = 3u;                           /* destroyed: start the explosion */
    } else {
        if (!(ent_flags[e_i] & (ENT_F_STOP | ENT_F_SHOOT))) {
            ent_x[e_i] = (e_c < 6u) ? (uint8_t)(ent_x[e_i] + 1u)
                                    : (uint8_t)(ent_x[e_i] - 1u);
        }
        e_c = tank_next[e_c];
    }
    ent_aux[e_i] = e_c;
    if (e_c == 0u) {
        ent_type[e_i] = ENTITY_NONE;
        entity_count--;
    }
}

/* One frame of the shell ($A609-$A671) for slot e_i: 17 frames of flight at
 * 2 px/frame, then 40 frames of swelling; then it restarts at the tank while
 * the tank is on screen.  Scratch: e_c, then the helpers' (e_slot, e_src). */
static void ent_shell_tick(void) {
    e_c = ent_aux[e_i];
    if (e_c != 0u && e_c < SHELL_FLY_FRAMES) {
        ent_x[e_i] = (ent_flags[e_i] & ENT_F_LEFT) ? (uint8_t)(ent_x[e_i] - 2u)
                                                   : (uint8_t)(ent_x[e_i] + 2u);
        ent_y[e_i] = (uint8_t)(ent_y[e_i] + shell_dy[e_c]);
    }
    e_c++;
    if (e_c < SHELL_CYCLE_FRAMES) {
        ent_aux[e_i] = e_c;
        return;
    }
    ent_find_tank();
    if (e_slot == 0xFFu) {
        ent_type[e_i] = ENTITY_NONE;        /* the tank left the screen */
        entity_count--;
    } else {
        e_src = e_i;
        ent_shell_init();
    }
}

/* Sprite frame of the tank / shell in slot e_i (tiles per frame: 2). */
static uint8_t ent_special_frame(void) {
    e_c = ent_aux[e_i];
    if (ent_type[e_i] == ENT_AUX_SHELL) {
        return (e_c < SHELL_FLY_FRAMES) ? 0u : (uint8_t)(((e_c - SHELL_FLY_FRAMES) >> 3) + 1u);
    }
    e_c &= 0x7Fu;
    if (e_c >= TANK_STATE_MIN_MOVE) {
        return (uint8_t)(e_c - TANK_STATE_MIN_MOVE);    /* driving frames 4..7 -> 0..3 */
    }
    return (e_c == 2u) ? 5u : 4u;                       /* explosion shapes */
}

/* One frame of the Atari patrol pass ($A271-$A2EA) for entity e_i. */
static void ent_move(void) {
    e_type = ent_type[e_i];
    if (e_type >= 14u || rg_level < 2u) {
        return;                 /* fuel/bridge never move; level 1 is static */
    }
    e_rows = ent_gfx_rows[e_type];
    e_w = ent_width(e_type);
    if (e_type != ENT_AUX_JET) {
        /* The big helicopter moves a quarter of the frames, everything else
         * on alternate frames (slot parity against the frame counter), and
         * only once the wake-up lottery has picked it. */
        if ((e_type == 12u && (ent_cur & 2u)) || ((e_i ^ ent_cur) & 1u) ||
            !(ent_flags[e_i] & ENT_F_AWAKE)) {
            return;
        }
        if (ent_touching()) {
            /* Turn round once per contact; a new object starts latched. */
            if (!(ent_flags[e_i] & ENT_F_LATCH)) {
                ent_flags[e_i] ^= ENT_F_LEFT;
                ent_flags[e_i] |= ENT_F_LATCH;
            }
        } else {
            ent_flags[e_i] &= (uint8_t)~ENT_F_LATCH;
        }
    }
    /* Walk one pixel; everything wraps at the patrol limits $2C..$D4. */
    if (ent_flags[e_i] & ENT_F_LEFT) {
        if (--ent_x[e_i] < 0x2Cu) {
            ent_x[e_i] = 0xD4u;
        }
    } else if (++ent_x[e_i] >= 0xD5u) {
        ent_x[e_i] = 0x2Cu;
    }
}

/* $A1FF-$A219: every 16 frames a random slot (0..10, the upper ones twice as
 * likely) is allowed to start patrolling.  Until then it stands still, which
 * is why some of the early helicopters never move. */
static void ent_wake(void) {
    e_t = (uint8_t)(ent_random() & 0x0Fu);
    if (e_t >= 11u) {
        e_t = (uint8_t)(e_t - 5u);
    }
    if (e_t < MAX_ENTITIES && ent_type[e_t] >= ENT_AUX_JET &&
        !(river_obj_count == 0u && e_t == sp_slot)) {
        ent_flags[e_t] |= ENT_F_AWAKE;
    }
}

static uint8_t e_off;
static uint8_t e_ex;
static uint8_t e_dx;

/* Write e_cols consecutive sprites (one 8-pixel row of an entity) at OAM byte
 * offset e_off, stopping at the entity sprite budget.  Hand-written: the C
 * version took ~190 cycles per sprite, this ~45. */
static void ent_emit_row(void) {
#ifndef __SDCC
    /* Host build (tests/host/spawn_main.c): same thing in C. */
    for (e_n = e_cols; e_n != 0u && e_off < 176u; e_n--, e_off = (uint8_t)(e_off + 4u)) {
        shadow_OAM[e_off >> 2].y = e_y;
        shadow_OAM[e_off >> 2].tile = e_tile++;
        shadow_OAM[e_off >> 2].attr = e_prop;
        shadow_OAM[e_off >> 2].x = e_px;
        e_px = (uint8_t)(e_px + e_dx);
    }
#else
    __asm
        ldx     _e_off
        ldy     _e_cols
    1$:
        cpx     #176
        bcs     2$
        lda     _e_y
        sta     _shadow_OAM,x
        lda     _e_tile
        sta     _shadow_OAM+1,x
        inc     _e_tile
        lda     _e_prop
        sta     _shadow_OAM+2,x
        lda     _e_px
        sta     _shadow_OAM+3,x
        clc
        adc     _e_dx
        sta     _e_px
        inx
        inx
        inx
        inx
        dey
        bne     1$
    2$:
        stx     _e_off
    __endasm;
#endif
}
static void ent_draw(void) {
    e_type = ent_type[e_i];
    if (e_type <= ENT_AUX_EXPLODE_LAST) {
        /* Debris frame (e_type - 2) of the original object's class. */
        e_src = (uint8_t)(ent_flags[e_i] >> 4);
        e_tile = (uint8_t)(ENT_TILE_BASE + ent_exp_first[e_src]);
        for (e_t = ENT_AUX_EXPLODE_FIRST; e_t < e_type; e_t++) {
            e_tile = (uint8_t)(e_tile + ent_gfx_count[e_src]);
        }
        e_flip = 0u;
    } else {
        e_src = e_type;
        e_tile = (uint8_t)(ENT_TILE_BASE + ent_gfx_first[e_type]);
        if (e_type >= ENT_AUX_TANK) {
            e_c = ent_special_frame();
            e_tile = (uint8_t)(e_tile + e_c * ent_gfx_count[e_type]);
        }
        /* A left-moving shape is the mirror image: flip every tile and
         * reverse the column order.  Fuel depots, the tank (it has left- and
         * right-facing frames) and the shell never flip. */
        e_flip = ((ent_flags[e_i] & 1u) && e_type != ENT_AUX_FUEL &&
                  e_type < ENT_AUX_TANK) ? S_FLIPX : 0u;
    }
    e_cols = ent_gfx_cols[e_src];
    e_prop = (uint8_t)(ent_gfx_pal[e_src] | e_flip);
    e_rows = ent_gfx_rows[e_src];
    e_y = ent_y[e_i];
    e_ex = ent_x[e_i];
    if (e_flip) {
        e_ex = (uint8_t)(e_ex + ((e_cols - 1u) << 3));
        e_dx = (uint8_t)-8;
    } else {
        e_dx = 8u;
    }
    e_off = (uint8_t)(e_oam << 2);
    for (e_t = 0; e_t < e_rows; e_t += 8u, e_y = (uint8_t)(e_y + 8u)) {
        if (e_y < HUD_SPLIT_SCANLINE || e_y >= 232u || e_off >= 176u) {
            e_tile = (uint8_t)(e_tile + e_cols);
            continue;
        }
        e_px = e_ex;
        ent_emit_row();
    }
    e_oam = (uint8_t)(e_off >> 2);
}void entities_update(void) BANKED {
    if (player_disable) {
        /* Test hook: no entities while the jet is frozen. */
        if (entity_count != 0u) {
            entities_clear();
        }
        while (ent_oam_used > ENT_OAM_FIRST) {
            ent_oam_used--;
            shadow_OAM[ent_oam_used].y = 240u;
        }
        return;
    }

    e_oam = ENT_OAM_FIRST;
    ent_anim = (uint8_t)(ent_anim + game_ticks);

    if (!river_freeze) {
        ent_wake_acc = (uint8_t)(ent_wake_acc + game_ticks);
        while (ent_wake_acc >= 16u) {
            ent_wake_acc = (uint8_t)(ent_wake_acc - 16u);
            ent_wake();
        }
    }

    if (bridge_wait) {
        /* Put the bar on the road once the road's top is on the ring: bar
         * rows 2..16 of the shape sit on the 16 road rows. */
        e_y = river_road_top();
        if (e_y != 0xFFu && (e_y < 232u || e_y >= 250u)) {
            for (e_i = 0; e_i < MAX_ENTITIES && ent_type[e_i] != ENTITY_NONE; e_i++) {
            }
            if (e_i < MAX_ENTITIES) {
                ent_type[e_i] = ENT_AUX_BRIDGE;
                ent_x[e_i] = bridge_x;
                ent_y[e_i] = (uint8_t)(e_y - 2u - river_scrolled);
                ent_flags[e_i] = 0;
                ent_aux[e_i] = 0;
                entity_count++;
            }
            ent_tank_place();
            bridge_wait = 0;
        }
    }

    for (e_i = 0; e_i < MAX_ENTITIES; e_i++) {
        e_type = ent_type[e_i];
        if (e_type == ENTITY_NONE) {
            continue;
        }
        if (e_type == ENT_AUX_TANK && (ent_flags[e_i] & ENT_F_WAIT)) {
            continue;                       /* no position until the bar is placed */
        }
        if (e_type != ENT_AUX_SHELL) {      /* the shell stays where it burst on screen */
            ent_y[e_i] = (uint8_t)(ent_y[e_i] + river_scrolled);
        }
        if (ent_y[e_i] >= 232u && ent_y[e_i] < 250u) {
            ent_type[e_i] = ENTITY_NONE;
            entity_count--;
            continue;
        }
        e_type = ent_type[e_i];
        if (e_type >= ENT_AUX_TANK) {
            if (!river_freeze) {
                for (e_n = 0; e_n < game_ticks; e_n++) {
                    ent_cur = (uint8_t)(ent_frame + e_n);
                    if (e_type == ENT_AUX_TANK) {
                        ent_tank_tick();
                    } else {
                        ent_shell_tick();
                    }
                    if (ent_type[e_i] == ENTITY_NONE) {
                        break;
                    }
                }
                if (ent_type[e_i] == ENTITY_NONE) {
                    continue;
                }
            }
        } else if (e_type <= ENT_AUX_EXPLODE_LAST) {
            ent_aux[e_i] = (uint8_t)(ent_aux[e_i] + game_ticks);
            if (ent_aux[e_i] >= ENT_EXPLODE_TICKS) {
                ent_aux[e_i] = 0;
                if (++ent_type[e_i] > ENT_AUX_EXPLODE_LAST) {
                    ent_type[e_i] = ENTITY_NONE;
                    entity_count--;
                    continue;
                }
            }
        } else {
            /* Patrol speed is per frame and independent of the scroll
             * speed; the world stands still while the river is frozen. */
            if (!river_freeze) {
                for (e_n = 0; e_n < game_ticks; e_n++) {
                    ent_cur = (uint8_t)(ent_frame + e_n);
                    ent_move();
                }
            }
            /* Rotor animation: the Atari swaps the two shapes of a helicopter
             * (aux 8<->9, 10<->11, `$BB24` remap) every second frame. */
            if (e_type >= 8u && e_type <= 11u) {
                ent_type[e_i] = (uint8_t)((e_type & 0xFEu) | ((ent_anim >> 1) & 1u));
            }
        }
        ent_draw();
    }

    ent_frame = (uint8_t)(ent_frame + game_ticks);

    /* Hide sprites freed since the last frame. */
    while (ent_oam_used > e_oam) {
        ent_oam_used--;
        shadow_OAM[ent_oam_used].y = 240u;
    }
    ent_oam_used = e_oam;
}
