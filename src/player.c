
/*
 * player.c - jet steering, banking, throttle, terrain crash and fuel.
 *
 * The jet is two stacked 8x8 sprites on OAM slots 0/1.  Left/right steer,
 * up/down set the throttle, and any terrain overlap costs a life; the jet
 * then respawns in mid-river after a short delay (the original restarts at
 * the last cleared bridge, which waits for the entity phase).  The shot,
 * fuel pick-ups and scoring arrive with the entity manager.
 */
#include <nes/nes.h>
#include <stdint.h>

#include "config.h"
#include "atari_data.h"
#include "hud.h"
#include "river.h"
#include "player.h"

player_state_t player;

/* Test hook: when non-zero the jet is hidden and player_update() does nothing. */
uint8_t player_disable;

#define PLAYER_WIDTH                8u
#define PLAYER_HEIGHT                14u
#define PLAYER_STEER_SPEED      1u
#define PLAYER_X_MIN            (SOURCE_X_OFFSET + 8u)
#define PLAYER_X_MAX            (SOURCE_X_OFFSET + 184u - PLAYER_WIDTH)
#define PLAYER_DEAD_FRAMES      90u
#define PLAYER_FUEL_FRAMES      8u      /* frames per fuel unit */

#define PLAYER_STATE_PLAY       0u
#define PLAYER_STATE_DEAD       1u
#define PLAYER_STATE_OVER       2u

#define PLAYER_SPRITE_TOP       0u
#define PLAYER_SPRITE_BOTTOM    1u

/* Shapes 7/8/9 live in sprite tiles 0..5 (see player_init_graphics). */
#define PLAYER_TILE_LEFT        (SPRITE_TILE_BASE + 0u)
#define PLAYER_TILE_RIGHT       (SPRITE_TILE_BASE + 2u)
#define PLAYER_TILE_STRAIGHT    (SPRITE_TILE_BASE + 4u)

static const palette_color_t player_palette[4] = {
    0x0Fu, 0x28u, 0x30u, 0x16u
};

/* Two 8x8 sprite tiles in GBDK's interleaved 2bpp format; the 14-row
 * Atari shapes become two stacked tiles. */
static uint8_t player_tile_buf[32];

static uint8_t player_state;
static uint8_t player_timer;
static uint8_t player_fuel_tick;
static uint8_t player_prev_pad;

/* File-scope scratch (SDCC aliases block locals across calls). */
static uint8_t pl_pad;
static uint8_t pl_row;
static uint8_t pl_l;
static uint8_t pl_r;
static uint8_t pl_slot;
static uint8_t pl_probe;

void player_init(void) {
    player.x = PLAYER_START_X;
    player.y = PLAYER_START_Y;
    player.speed = 1;
    player.bank = PLAYER_BANK_NONE;
    player.shot_x = 0;
    player.shot_y = 0;
    player.shot_active = 0;
    player.alive = 1;
    player.fuel_hi = 0xFF;
    player.fuel_lo = 0xFF;
    player_state = PLAYER_STATE_PLAY;
    player_timer = 0;
    player_fuel_tick = 0;
    player_prev_pad = 0;
    river_speed = 1u;
}

static void player_load_shape(uint8_t first_tile, uint8_t shape) {
    const uint8_t *src = atari_player_shape + (uint16_t)shape * ATARI_PLAYER_SHAPE_LEN;
    uint8_t i;

    /* 1bpp source -> 2bpp with the set pixels in plane 0 (sprite colour 1).
     * The library build does not ship set_sprite_1bpp_data(), so expand it
     * here rather than relying on the missing symbol. */
    for (i = 0; i < 8; i++) {
        player_tile_buf[i * 2] = src[i];
        player_tile_buf[i * 2 + 1] = 0x00u;
        player_tile_buf[16 + i * 2] = (i < 6) ? src[8 + i] : 0x00u;
        player_tile_buf[16 + i * 2 + 1] = 0x00u;
    }
    set_sprite_data(first_tile, 2, player_tile_buf);
}

void player_init_graphics(void) {
    set_sprite_palette(0, 1, player_palette);

    /* Tile pairs: 0/1 bank-left, 2/3 bank-right, 4/5 straight. */
    player_load_shape(SPRITE_TILE_BASE + 0u, 7);
    player_load_shape(SPRITE_TILE_BASE + 2u, 8);
    player_load_shape(SPRITE_TILE_BASE + 4u, 9);

    hide_sprite(PLAYER_SPRITE_TOP);
    hide_sprite(PLAYER_SPRITE_BOTTOM);
}

/*
 * Non-zero when the jet's box overlaps land or an island on the tile rows
 * under its nose and tail.  Rows whose bounds are 0/0 are the empty black
 * world at game start and never collide.
 */
static uint8_t player_hits_terrain(void) {
    uint8_t px = (uint8_t)(player.x - SOURCE_X_OFFSET);

    for (pl_probe = 0; pl_probe < 2u; pl_probe++) {
        pl_row = (pl_probe == 0u) ? player.y
                                  : (uint8_t)(player.y + PLAYER_HEIGHT - 1u);
        pl_slot = river_slot_at_scanline(pl_row);
        pl_l = river_left_x[pl_slot];
        pl_r = river_right_x[pl_slot];
        if (pl_r == 0u) {
            continue;
        }
        if (px < pl_l || (uint8_t)(px + PLAYER_WIDTH) > pl_r) {
            return 1;
        }
        if ((river_flags[pl_slot] & RIVER_FLAG_ISLAND) &&
            river_island_left_x[pl_slot] < river_island_right_x[pl_slot] &&
            px < river_island_right_x[pl_slot] &&
            (uint8_t)(px + PLAYER_WIDTH) > river_island_left_x[pl_slot]) {
            return 1;
        }
    }
    return 0;
}

static void player_show(uint8_t tile) {
    move_sprite(PLAYER_SPRITE_TOP, player.x, player.y);
    move_sprite(PLAYER_SPRITE_BOTTOM, player.x, (uint8_t)(player.y + 8u));
    set_sprite_tile(PLAYER_SPRITE_TOP, tile);
    set_sprite_tile(PLAYER_SPRITE_BOTTOM, (uint8_t)(tile + 1u));
}

static void player_crash(void) {
    hide_sprite(PLAYER_SPRITE_TOP);
    hide_sprite(PLAYER_SPRITE_BOTTOM);
    river_freeze = 1u;
    player.alive = 0;
    if (hud_lives != 0u) {
        hud_lives--;
    }
    player_timer = PLAYER_DEAD_FRAMES;
    player_state = (hud_lives == 0u) ? PLAYER_STATE_OVER : PLAYER_STATE_DEAD;
}

/* Re-enter the river at its centre under the jet. */
static void player_respawn(void) {
    pl_slot = river_slot_at_scanline(player.y);
    pl_l = river_left_x[pl_slot];
    pl_r = river_right_x[pl_slot];
    if (pl_r == 0u) {
        player.x = PLAYER_START_X;
    } else {
        player.x = (uint8_t)(SOURCE_X_OFFSET + ((pl_l + pl_r) >> 1) -
                             (PLAYER_WIDTH >> 1));
    }
    player.alive = 1;
    player.speed = 1;
    hud_fuel = 255u;
    player_fuel_tick = 0;
    river_speed = 1u;
    river_freeze = 0;
    player_state = PLAYER_STATE_PLAY;
}

void player_update(void) {
    if (player_disable) {
        hide_sprite(PLAYER_SPRITE_TOP);
        hide_sprite(PLAYER_SPRITE_BOTTOM);
        return;
    }

    pl_pad = joypad();

    if (player_state != PLAYER_STATE_PLAY) {
        if (player_timer != 0u) {
            player_timer--;
        } else if (player_state == PLAYER_STATE_DEAD) {
            player_respawn();
        } else if ((pl_pad & J_START) && !(player_prev_pad & J_START)) {
            hud_lives = 3u;
            hud_score_bcd[0] = 0;
            hud_score_bcd[1] = 0;
            hud_score_bcd[2] = 0;
            player_respawn();
        }
        player_prev_pad = pl_pad;
        return;
    }

    /* Steering and banking. */
    player.bank = PLAYER_BANK_NONE;
    if (pl_pad & J_LEFT) {
        if (player.x > PLAYER_X_MIN) {
            player.x -= PLAYER_STEER_SPEED;
        }
        player.bank = 1;
    } else if (pl_pad & J_RIGHT) {
        if (player.x < PLAYER_X_MAX) {
            player.x += PLAYER_STEER_SPEED;
        }
        player.bank = 2;
    }

    /* Throttle: down = slow, otherwise cruise. */
    river_speed = (pl_pad & J_DOWN) ? 0u : 1u;

    /* Fuel. */
    if (++player_fuel_tick >= PLAYER_FUEL_FRAMES) {
        player_fuel_tick = 0;
        if (hud_fuel != 0u) {
            hud_fuel--;
        }
    }

    if (player.bank == 1) {
        player_show(PLAYER_TILE_LEFT);
    } else if (player.bank == 2) {
        player_show(PLAYER_TILE_RIGHT);
    } else {
        player_show(PLAYER_TILE_STRAIGHT);
    }

    if (hud_fuel == 0u || player_hits_terrain()) {
        player_crash();
    }
    player_prev_pad = pl_pad;
}
