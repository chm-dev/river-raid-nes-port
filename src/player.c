
/*
 * player.c - jet steering, banking, throttle, terrain crash and fuel.
 *
 * The jet is two stacked 8x8 sprites on OAM slots 0/1.  Left/right steer,
 * up/down set the throttle, and any terrain overlap costs a life; after a
 * short delay the river restarts at the last cleared bridge (river_restart)
 * and the jet waits on it for input.
 */
#include <nes/nes.h>
#include <stdint.h>

#include "config.h"
#include "atari_data.h"
#include "hud.h"
#include "river.h"
#include "entities.h"
#include "entity_gfx.h"
#include "player.h"
#include "sound.h"

player_state_t player;

/* Test hook: when non-zero the jet is hidden and player_update() does nothing. */
uint8_t player_disable;

/* Test hook: when non-zero the jet never crashes and never runs out of fuel
 * (used to measure sustained scroll speed). */
uint8_t player_invuln;

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
#define PLAYER_STATE_WAIT       3u      /* game start: river still scrolling in */
#define PLAYER_STATE_READY      4u      /* jet shown, river stopped until input */
#define PLAYER_START_INPUT      (J_A | J_LEFT | J_RIGHT | J_UP | J_DOWN)

#define PLAYER_SHOT_SLOT        47u
#define PLAYER_SHOT_SPEED       6u
#define PLAYER_SHOT_WIDTH       2u
#define PLAYER_REFUEL_RATE      2u      /* fuel units per frame on a depot ($A40A: $5B += 2) */
#define PLAYER_DEATH_STEPS      5u      /* debris shapes 6..2, 8 frames each */
/* Shapes 2..6, 2 tiles each, right after the entity tiles (entities.c puts
 * them at SPRITE_TILE_BASE + 24). */
#define PLAYER_DEBRIS_TILE      (SPRITE_TILE_BASE + 24u + ENT_GFX_TILES)

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
#define player_tile_buf     (river_tile_pat)   /* boot-time scratch, see river.h */

static uint8_t player_state;
static uint8_t player_timer;
static uint8_t player_fuel_tick;
static uint8_t player_prev_pad;
static uint8_t pl_step;

/* File-scope scratch (SDCC aliases block locals across calls). */
static uint8_t pl_ticks;
static uint8_t pl_pad;
static uint8_t pl_row;
static uint8_t pl_l;
static uint8_t pl_r;
static uint8_t pl_slot;
static uint8_t pl_probe;

void player_init(void) BANKED {
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
    /* Like the Atari, the jet only appears once the river has scrolled in
     * to the bottom of the screen; then everything waits for input. */
    player_state = PLAYER_STATE_WAIT;
    player_timer = 0;
    player_fuel_tick = 0;
    player_prev_pad = 0;
    river_speed = 1u;
    river_vel = RIVER_VEL_CRUISE;
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

void player_init_graphics(void) BANKED {
    set_sprite_palette(0, 1, player_palette);

    /* Tile pairs: 0/1 bank-left, 2/3 bank-right, 4/5 straight. */
    player_load_shape(SPRITE_TILE_BASE + 0u, 7);
    player_load_shape(SPRITE_TILE_BASE + 2u, 8);
    player_load_shape(SPRITE_TILE_BASE + 4u, 9);
    for (pl_step = 2u; pl_step <= 6u; pl_step++) {
        player_load_shape((uint8_t)(PLAYER_DEBRIS_TILE + ((pl_step - 2u) << 1)), pl_step);
    }

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
    shadow_OAM[PLAYER_SPRITE_TOP].y = player.y;
    shadow_OAM[PLAYER_SPRITE_TOP].tile = tile;
    shadow_OAM[PLAYER_SPRITE_TOP].prop = S_PAL(0);
    shadow_OAM[PLAYER_SPRITE_TOP].x = player.x;
    shadow_OAM[PLAYER_SPRITE_BOTTOM].y = (uint8_t)(player.y + 8u);
    shadow_OAM[PLAYER_SPRITE_BOTTOM].tile = (uint8_t)(tile + 1u);
    shadow_OAM[PLAYER_SPRITE_BOTTOM].prop = S_PAL(0);
    shadow_OAM[PLAYER_SPRITE_BOTTOM].x = player.x;
}
static uint8_t pl_hit;
static uint8_t pl_tile;

/* The jet's death: the original steps $5E through shapes 6,5,4,3,2 (debris
 * drawn in the jet's own yellow, 14 rows) every 8 frames, then goes blank. */
static void player_death_anim(void) {
    pl_step = (uint8_t)((PLAYER_DEAD_FRAMES - player_timer) >> 3);
    if (pl_step >= PLAYER_DEATH_STEPS) {
        shadow_OAM[PLAYER_SPRITE_TOP].y = 240u;
        shadow_OAM[PLAYER_SPRITE_BOTTOM].y = 240u;
        return;
    }
    /* Shape 6 - step lives in the debris tile pair of that shape. */
    pl_tile = (uint8_t)(PLAYER_DEBRIS_TILE + ((4u - pl_step) << 1));
    player_show(pl_tile);
}
static void player_kill_shot(void) {
    player.shot_active = 0;
    hide_sprite(PLAYER_SHOT_SLOT);
}

/* Fire, fly and test the single player shot. */
static void player_update_shot(void) {
    if (!player.shot_active) {
        if (!(pl_pad & J_A)) {
            return;
        }
        player.shot_x = (uint8_t)(player.x + 3u);
        player.shot_y = (uint8_t)(player.y - 6u);
        player.shot_active = 1;
        sound_play_shot();
    } else {
        pl_row = (uint8_t)(PLAYER_SHOT_SPEED * pl_ticks);
        if (player.shot_y < (uint8_t)(HUD_SPLIT_SCANLINE + pl_row + 6u)) {
            player_kill_shot();
            return;
        }
        player.shot_y = (uint8_t)(player.shot_y - pl_row);
    }

    pl_slot = river_slot_at_scanline(player.shot_y);
    pl_l = river_left_x[pl_slot];
    pl_r = river_right_x[pl_slot];
    pl_row = (uint8_t)(player.shot_x - SOURCE_X_OFFSET);
    if (pl_r != 0u &&
        (pl_row < pl_l || (uint8_t)(pl_row + PLAYER_SHOT_WIDTH) > pl_r ||
         ((river_flags[pl_slot] & RIVER_FLAG_ISLAND) &&
          pl_row < river_island_right_x[pl_slot] &&
          (uint8_t)(pl_row + PLAYER_SHOT_WIDTH) > river_island_left_x[pl_slot]))) {
        player_kill_shot();
        return;
    }
    if (entities_hit_shot(player.shot_x, player.shot_y)) {
        sound_play_explosion();
        player_kill_shot();
        return;
    }
    /* The bar sits at columns 3..4 of its tile; shot_x is the box's left edge. */
    move_sprite(PLAYER_SHOT_SLOT, (uint8_t)(player.shot_x - 3u), player.shot_y);
    set_sprite_tile(PLAYER_SHOT_SLOT, entities_shot_tile());
    set_sprite_prop(PLAYER_SHOT_SLOT, S_PAL(0));
}

static void player_crash(void) {
    player_kill_shot();
    hide_sprite(PLAYER_SPRITE_TOP);
    hide_sprite(PLAYER_SPRITE_BOTTOM);
    river_freeze = 1u;
    player.alive = 0;
    sound_play_crash();
    sound_set_engine(SOUND_ENGINE_OFF);
    if (hud_lives != 0u) {
        hud_lives--;
    }
    player_timer = PLAYER_DEAD_FRAMES;
    player_state = (hud_lives == 0u) ? PLAYER_STATE_OVER : PLAYER_STATE_DEAD;
}

/* Put the jet at the centre of the river under it. */
static void player_center(void) {
    pl_slot = river_slot_at_scanline(player.y);
    pl_l = river_left_x[pl_slot];
    pl_r = river_right_x[pl_slot];
    if (pl_r == 0u) {
        player.x = PLAYER_START_X;
    } else {
        player.x = (uint8_t)(SOURCE_X_OFFSET + ((pl_l + pl_r) >> 1) -
                             (PLAYER_WIDTH >> 1));
    }
}

/* Restart the river (at the last cleared bridge, or from level 1 for a new
 * game) and go through the game-start sequence: the river scrolls in, then
 * the jet waits on it for input. */
static void player_respawn(uint8_t new_game) {
    /* Rebuilding the ring takes many frames: blank the screen first so the
     * old river, its entities and the jet's shot vanish together. */
    entities_clear();
    player_kill_shot();
    DISPLAY_OFF;
    if (new_game) {
        hud_bridge = 1u;
        river_init();
    } else {
        river_restart(hud_bridge);
    }
    DISPLAY_ON;
    player.alive = 1;
    player.speed = 1;
    hud_fuel = 255u;
    player_fuel_tick = 0;
    river_speed = 1u;
    river_vel = RIVER_VEL_CRUISE;
    player_state = PLAYER_STATE_WAIT;
}

void player_update(void) BANKED {
    if (player_disable) {
        sound_set_engine(SOUND_ENGINE_OFF);
        river_speed = 1u;       /* tests expect 1 px/frame */
        river_vel = RIVER_VEL_CRUISE;
        if (player.alive) {
            player.alive = 0;
            hide_sprite(PLAYER_SPRITE_TOP);
            hide_sprite(PLAYER_SPRITE_BOTTOM);
            player_kill_shot();
        }
        return;
    }

    if (player_state == PLAYER_STATE_WAIT) {
        if (river_right_x[river_slot_at_scanline(239u)] == 0u) {
            return;
        }
        river_freeze = 1u;
        player_center();
        player_state = PLAYER_STATE_READY;
    }

    pl_ticks = game_ticks;
    pl_pad = joypad();

    if (player_state == PLAYER_STATE_READY) {
        /* Stopped on the river: the game starts when the player fires or
         * moves. */
        player_show(PLAYER_TILE_STRAIGHT);
        if (!(pl_pad & PLAYER_START_INPUT)) {
            player_prev_pad = pl_pad;
            return;
        }
        river_freeze = 0;
        player_state = PLAYER_STATE_PLAY;
    }

    if (player_state != PLAYER_STATE_PLAY) {
        if (player_timer != 0u) {
            player_death_anim();
            player_timer = (player_timer > pl_ticks) ? (uint8_t)(player_timer - pl_ticks) : 0u;
        } else if (player_state == PLAYER_STATE_DEAD) {
            player_respawn(0);
        } else if ((pl_pad & J_START) && !(player_prev_pad & J_START)) {
            hud_lives = 3u;
            hud_score_bcd[0] = 0;
            hud_score_bcd[1] = 0;
            hud_score_bcd[2] = 0;
            sound_play_blip();
            player_respawn(1);
        }
        player_prev_pad = pl_pad;
        return;
    }

    /* Steering and banking. */
    player.bank = PLAYER_BANK_NONE;
    if (pl_pad & J_LEFT) {
        if (player.x > PLAYER_X_MIN) {
            player.x = (player.x - PLAYER_X_MIN > pl_ticks)
                ? (uint8_t)(player.x - pl_ticks) : PLAYER_X_MIN;
        }
        player.bank = 1;
    } else if (pl_pad & J_RIGHT) {
        if (player.x < PLAYER_X_MAX) {
            player.x = (PLAYER_X_MAX - player.x > pl_ticks)
                ? (uint8_t)(player.x + pl_ticks) : PLAYER_X_MAX;
        }
        player.bank = 2;
    }

    /* Throttle: down = slow, up = fast, otherwise cruise. */
    river_speed = (pl_pad & J_DOWN) ? 0u : (pl_pad & J_UP) ? 2u : 1u;
    sound_set_engine(river_speed);

    /* Fuel. */
    player_fuel_tick = (uint8_t)(player_fuel_tick + pl_ticks);
    if (player_fuel_tick >= PLAYER_FUEL_FRAMES) {
        player_fuel_tick = (uint8_t)(player_fuel_tick - PLAYER_FUEL_FRAMES);
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

    player_update_shot();

    pl_hit = entities_check_player(player.x, player.y);
    if (pl_hit == ENT_HIT_FUEL) {
        /* The original re-arms the 8-frame tank note every frame of the
         * overlap ($A40A sets $7A = 4 each time the previous sweep ended), so
         * it repeats for as long as the jet stays on the tank. */
        sound_play_pickup();
        /* The pass covers pl_ticks frames; the Atari adds per frame. */
        hud_fuel = (hud_fuel > (uint8_t)(255u - PLAYER_REFUEL_RATE * pl_ticks))
            ? 255u : (uint8_t)(hud_fuel + PLAYER_REFUEL_RATE * pl_ticks);
    }

    if (player_invuln) {
        hud_fuel = 255u;
    } else if (hud_fuel == 0u || pl_hit == ENT_HIT_DEADLY ||
               player_hits_terrain()) {
        player_crash();
    }
    player_prev_pad = pl_pad;
}
