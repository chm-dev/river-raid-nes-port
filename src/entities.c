/*
 * entities.c - enemies, fuel depots and explosions.
 *
 * Simplified against the Atari spawner: objects appear just above the
 * visible river at a random X inside the water, scroll down with the river
 * and patrol between the banks (the Atari instead pre-places them in the
 * generator and lets the bank clamp make room).  Types, shapes and scores
 * come from the extracted Atari tables.
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
#define ENT_SPAWN_PIXELS        56u     /* scrolled rows between spawns */
#define ENT_MIN_WATER           40u
#define ENT_EXPLODE_TICKS       4u

static uint8_t ent_rng = 0x5Au;
static uint8_t ent_spawn_acc;
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
static volatile OAM_item_t *e_o;
static uint8_t e_oam;
static uint8_t e_px;

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
    ent_spawn_acc = 0;
}

void entities_init(void) BANKED {
    entities_clear();
}

void entities_init_graphics(void) BANKED {
    palette_color_t pal[4];

    pal[0] = BG_COLOR_BLACK;
    pal[1] = ent_gfx_palette[0][0];
    pal[2] = ent_gfx_palette[0][1];
    pal[3] = ent_gfx_palette[0][2];
    set_sprite_palette(2, 1, pal);
    pal[1] = ent_gfx_palette[1][0];
    pal[2] = ent_gfx_palette[1][1];
    pal[3] = ent_gfx_palette[1][2];
    set_sprite_palette(3, 1, pal);
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

/* Water bounds under entity e_i; returns 0 when the row is unknown. */
static uint8_t ent_bounds(void) {
    e_slot = river_slot_at_scanline((uint8_t)(ent_y[e_i] + (e_rows >> 1)));
    e_r = river_right_x[e_slot];
    if (e_r == 0u) {
        return 0;
    }
    e_l = (uint8_t)(river_left_x[e_slot] + SOURCE_X_OFFSET);
    e_r = (uint8_t)(e_r + SOURCE_X_OFFSET);
    return 1;
}

static void ent_spawn(void) {
    e_slot = river_slot_at_scanline(PLAYFIELD_SCANLINE);
    if (river_flags[e_slot] & (RIVER_FLAG_ISLAND | RIVER_FLAG_BRIDGE)) {
        return;
    }
    e_r = river_right_x[e_slot];
    e_l = river_left_x[e_slot];
    if (e_r == 0u || (uint8_t)(e_r - e_l) < ENT_MIN_WATER) {
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

    e_t = ent_random();
    e_type = atari_spawn_types[e_t & 7u];
    if (((e_t >> 3) & 3u) == 0u) {
        e_type = ENT_AUX_FUEL;
    } else if (e_type == 12u && hud_bridge < 3u) {
        e_type = 8u;
    } else if (e_type == 7u && hud_bridge < 5u) {
        e_type = ENT_AUX_SHIP;
    }
    e_rows = ent_gfx_rows[e_type];
    e_w = ent_width(e_type);

    e_x = (uint8_t)(((uint16_t)ent_random() *
                     (uint8_t)(e_r - e_l - 2u * 10u - e_w)) >> 8);
    ent_type[e_i] = e_type;
    ent_x[e_i] = (uint8_t)(e_l + SOURCE_X_OFFSET + 10u + e_x);
    ent_y[e_i] = (uint8_t)(PLAYFIELD_SCANLINE - e_rows);
    ent_flags[e_i] = (ent_random() & 1u);
    ent_aux[e_i] = 0;
    entity_count++;
}

static void ent_explode(void) {
    /* Centre the debris on the old shape. */
    ent_y[e_i] = (uint8_t)(ent_y[e_i] + (e_rows >> 1) - 3u);
    ent_type[e_i] = ENT_AUX_EXPLODE_FIRST;
    ent_aux[e_i] = 0;
}

uint8_t entities_hit_shot(uint8_t sx, uint8_t sy) BANKED {
    for (e_i = 0; e_i < MAX_ENTITIES; e_i++) {
        e_type = ent_type[e_i];
        if (e_type < ENT_AUX_JET) {
            continue;
        }
        e_rows = ent_gfx_rows[e_type];
        e_w = ent_width(e_type);
        if ((uint16_t)sx < (uint16_t)ent_x[e_i] + e_w &&
            (uint16_t)sx + 2u > ent_x[e_i] &&
            (uint16_t)sy < (uint16_t)ent_y[e_i] + e_rows &&
            (uint16_t)sy + 6u > ent_y[e_i]) {
            entities_add_score(atari_score_tens[e_type]);
            ent_explode();
            return 1;
        }
    }
    return 0;
}

uint8_t entities_check_player(uint8_t px, uint8_t py) BANKED {
    for (e_i = 0; e_i < MAX_ENTITIES; e_i++) {
        e_type = ent_type[e_i];
        if (e_type < ENT_AUX_JET) {
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

static void ent_move(void) {
    e_type = ent_type[e_i];
    if (e_type == ENT_AUX_FUEL) {
        return;
    }
    e_rows = ent_gfx_rows[e_type];
    e_w = ent_width(e_type);
    /* Ships crawl, helicopters walk, the jet flies two pixels a frame. */
    ent_aux[e_i]++;
    if (e_type == ENT_AUX_SHIP && (ent_aux[e_i] & 1u)) {
        return;
    }
    e_px = (e_type == ENT_AUX_JET) ? 2u : 1u;
    if (ent_flags[e_i] & 1u) {
        ent_x[e_i] = (uint8_t)(ent_x[e_i] - e_px);
    } else {
        ent_x[e_i] = (uint8_t)(ent_x[e_i] + e_px);
    }
    if (e_type == ENT_AUX_JET) {
        /* Crossing jets wrap at the Atari patrol limits $2C..$D4. */
        if (ent_x[e_i] >= 0xD4u) {
            ent_x[e_i] = (uint8_t)(0x2Cu + (ent_x[e_i] - 0xD4u));
        } else if (ent_x[e_i] < 0x2Cu) {
            ent_x[e_i] = 0xD4u;
        }
        return;
    }
    /* Banks move at most a pixel per row, so checking every 4th tick is enough. */
    if (!(ent_aux[e_i] & 3u) && ent_bounds()) {
        e_x = ent_x[e_i];
        if (e_x <= (uint8_t)(e_l + 2u)) {
            ent_x[e_i] = (uint8_t)(e_l + 3u);
            ent_flags[e_i] &= 0xFEu;
        } else if ((uint8_t)(e_x + e_w + 2u) >= e_r) {
            ent_x[e_i] = (uint8_t)(e_r - e_w - 3u);
            ent_flags[e_i] |= 1u;
        }
    }
}

static void ent_draw(void) {
    e_type = ent_type[e_i];
    e_tile = (uint8_t)(ENT_TILE_BASE + ent_gfx_first[e_type]);
    e_cols = ent_gfx_cols[e_type];
    /* A left-moving shape is the mirror image: flip every tile and reverse
     * the column order.  Fuel depots and debris never flip. */
    e_flip = ((ent_flags[e_i] & 1u) && e_type >= ENT_AUX_JET &&
              e_type != ENT_AUX_FUEL) ? S_FLIPX : 0u;
    e_prop = (uint8_t)(ent_gfx_pal[e_type] | e_flip);
    for (e_t = 0; e_t < ent_gfx_rows[e_type]; e_t += 8u) {
        e_y = (uint8_t)(ent_y[e_i] + e_t);
        for (e_c = 0; e_c < e_cols; e_c++, e_tile++) {
            if (e_y < HUD_SPLIT_SCANLINE || e_y >= 232u || e_oam > ENT_OAM_LAST) {
                continue;
            }
            e_o = &shadow_OAM[e_oam];
            e_o->y = e_y;
            e_o->tile = e_tile;
            e_o->prop = e_prop;
            e_o->x = (uint8_t)(ent_x[e_i] +
                               (e_flip ? (uint8_t)((e_cols - 1u - e_c) << 3)
                                       : (uint8_t)(e_c << 3)));
            e_oam++;
        }
    }
}
void entities_update(void) BANKED {
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

    e_px = river_scrolled;
    e_oam = ENT_OAM_FIRST;
    ent_anim++;

    ent_spawn_acc = (uint8_t)(ent_spawn_acc + e_px);
    if (ent_spawn_acc >= ENT_SPAWN_PIXELS) {
        ent_spawn_acc = 0;
        ent_spawn();
    }

    for (e_i = 0; e_i < MAX_ENTITIES; e_i++) {
        e_type = ent_type[e_i];
        if (e_type == ENTITY_NONE) {
            continue;
        }
        ent_y[e_i] = (uint8_t)(ent_y[e_i] + river_scrolled);
        if (ent_y[e_i] >= 232u && ent_y[e_i] < 250u) {
            ent_type[e_i] = ENTITY_NONE;
            entity_count--;
            continue;
        }
        e_type = ent_type[e_i];
        if (e_type <= ENT_AUX_EXPLODE_LAST) {
            if (++ent_aux[e_i] >= ENT_EXPLODE_TICKS) {
                ent_aux[e_i] = 0;
                if (++ent_type[e_i] > ENT_AUX_EXPLODE_LAST) {
                    ent_type[e_i] = ENTITY_NONE;
                    entity_count--;
                    continue;
                }
            }
        } else {
            if (river_scrolled != 0u) {
                ent_move();
            }
            /* Rotor animation: the Atari swaps the two shapes of a helicopter
             * (aux 8<->9, 10<->11, `$BB24` remap) every second frame. */
            if (e_type >= 8u && e_type <= 11u && !(ent_anim & 1u)) {
                ent_type[e_i] ^= 1u;
            }
        }
        ent_draw();
    }

    /* Hide sprites freed since the last frame. */
    while (ent_oam_used > e_oam) {
        ent_oam_used--;
        shadow_OAM[ent_oam_used].y = 240u;
    }
    ent_oam_used = e_oam;
}
