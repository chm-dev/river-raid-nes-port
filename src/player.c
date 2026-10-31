
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
#define PLAYER_STATE_PAUSE      6u      /* START pressed: frozen, RESUME / EXIT menu */
#define PLAYER_START_INPUT      (J_A | J_LEFT | J_RIGHT | J_UP | J_DOWN)
#define ATTRACT_REPEAT_FRAMES   30u     /* Atari $A6DA: Up/Down repeat while held */

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

/* Pause menu, drawn with sprites (the playfield nametable is the scroll ring
 * and its pattern table is the river cache, so it cannot hold text).  Letters
 * R E S U M X I T are charset glyphs uploaded to sprite tiles PAUSE_TILE_BASE+0..7
 * (clear of the debris tiles, which end at 238).  OAM slots 56..63 are free; 2..4
 * are the tank/shell slots, idle while the entities are paused.  "RESUME" and
 * "EXIT" sit in a band that is cleared of other sprites, so no scanline ever
 * has more than 7 of them. */
#define PAUSE_TILE_BASE         240u
#define PAUSE_GLYPHS            8u
/* The HUD's raised-digit "top" tiles (config.h) sit right after the pause glyphs. */
typedef char hud_top_tiles_clear[(TILE_TOP_YELLOW >= PAUSE_TILE_BASE + PAUSE_GLYPHS
                                  && TILE_TOP_RIGHT + TILE_TOP_COUNT <= 256u) ? 1 : -1];
#define PAUSE_PARTS             10u     /* letters; the cursor is slot PAUSE_CURSOR_SLOT */
#define PAUSE_CURSOR_SLOT       2u
#define PAUSE_ROW1_Y            112u
#define PAUSE_ROW2_Y            128u
#define PAUSE_BAND_TOP          104u    /* sprites overlapping these scanlines are hidden */
#define PAUSE_BAND_BOT          144u
#define PAUSE_CURSOR_X          88u

/* Charset codes; the game only patches the letters its own text uses, so X is
 * the OS-font glyph at 120, not the alphabetical 24 (that is a comma). */
static const uint8_t pause_glyph[PAUSE_GLYPHS] = { 18, 5, 19, 21, 13, 120, 9, 20 };
/* RESUME: R E S U M E   EXIT: E X I T */
static const uint8_t pause_tile[PAUSE_PARTS] = { 0, 1, 2, 3, 4, 1, 1, 5, 6, 7 };
static const uint8_t pause_slot[PAUSE_PARTS] = { 56, 57, 58, 59, 60, 61, 62, 63, 3, 4 };
static const uint8_t pause_x[PAUSE_PARTS] = { 104, 112, 120, 128, 136, 144, 112, 120, 128, 136 };

static const palette_color_t player_palette[4] = {
    0x0Fu, 0x28u, 0x30u, 0x16u
};

/* Two 8x8 sprite tiles in GBDK's interleaved 2bpp format; the 14-row
 * Atari shapes become two stacked tiles. */
#define player_tile_buf     (river_tile_pat)   /* boot-time scratch, see river.h */

uint8_t player_state;
static uint8_t player_timer;
static uint8_t player_fuel_tick;
static uint8_t player_prev_pad;
static uint8_t pl_step;

/* Game selection and the two-player state.  Like the Atari ($09 / $77, and the
 * $6B-$75 mirror swapped by $B247) the idle player's state is kept aside and
 * exchanged after every death.  pl_other: score[3], lives, bridge, then
 * river_swap_checkpoint()'s level + 4 seeds. */
static uint8_t pl_two;                  /* 1 = two players alternating */
static uint8_t pl_cur;                  /* active player (joypad index) */
static uint8_t pl_other[10];
static uint8_t pl_hi[4][3];             /* hi-score per start level */
static uint8_t pl_hi_idx;               /* start level hud_hi_bcd belongs to */
static uint8_t pl_sel;                  /* start level picked in the menu (river_variant once built) */
static uint8_t pl_frozen;               /* menu changed: the old river stands still until pl_wait_end */
static uint16_t pl_wait_end;            /* sys_time at which the picked level's river starts */
static uint8_t pl_nocol;                /* debug start (SELECT): the jet cannot crash */
static uint8_t pl_rep;                  /* attract: frames until Up/Down repeats */
static uint8_t pause_sel;               /* 0 = RESUME, 1 = EXIT */
static uint8_t pause_from;              /* state to return to: PLAY, or READY (jet not yet moving) */
static joypads_t pl_pads;
static uint8_t pl_new;
static uint8_t pl_i;
static uint8_t pl_t;

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
    player_state = PLAYER_STATE_ATTRACT;
    player.alive = 0;
    player_timer = 0;
    player_fuel_tick = 0;
    player_prev_pad = 0xFFu;            /* a button held at boot is not a press */
    river_speed = 1u;
    river_vel = RIVER_VEL_CRUISE;
    joypad_init(2u, &pl_pads);
    pl_two = 0;
    pl_cur = 0;
    hud_other_bcd = pl_other;
    hud_two = 0;
    hud_cur = 0;
    pl_hi_idx = river_variant;
    pl_sel = river_variant;
    pl_frozen = 0;
    hud_game = 1u;
    hud_lives = 0;
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

    /* Pause-menu letters: charset glyphs as plane-0 sprite tiles. */
    for (pl_step = 0; pl_step < PAUSE_GLYPHS; pl_step++) {
        const uint8_t *src = atari_charset + (uint16_t)pause_glyph[pl_step] * 8u;

        for (pl_i = 0; pl_i < 8u; pl_i++) {
            player_tile_buf[pl_i * 2u] = src[pl_i];
            player_tile_buf[pl_i * 2u + 1u] = 0x00u;
        }
        set_sprite_data((uint8_t)(PAUSE_TILE_BASE + pl_step), 1, player_tile_buf);
    }
}

/*
 * Non-zero when the jet's box overlaps land or an island on the tile rows
 * under its nose and tail.  Rows whose bounds are 0/0 are the empty black
 * world at game start and never collide.
 */
/* Does the lit part of the current jet shape (rows pl_first..pl_last, ORed into
 * pl_bits) overlap land on the tile row `pl_slot`?  The Atari tests the sprite's
 * own pixels (P2PF), not its 8-px cell: the straight jet is columns 1..7 and
 * both banked jets columns 2..6, so a gap that fits the banked jet is passable. */
static uint8_t pl_bits;
static uint8_t pl_cmin;
static uint8_t pl_cmax;
static uint8_t pl_shape;

static uint8_t player_slot_hit(uint8_t px) {
    if (pl_bits == 0u) {
        return 0;
    }
    pl_cmin = 0;
    while (!(pl_bits & (0x80u >> pl_cmin))) {
        pl_cmin++;
    }
    pl_cmax = 7u;
    while (!(pl_bits & (0x80u >> pl_cmax))) {
        pl_cmax--;
    }
    /* pl_cmax is the column of the right-most lit pixel; the box is exclusive. */
    pl_l = (uint8_t)(px + pl_cmin);
    pl_r = (uint8_t)(px + pl_cmax + 1u);
    if (river_right_x[pl_slot] == 0u) {
        return 0;
    }
    if (pl_l < river_left_x[pl_slot] || pl_r > river_right_x[pl_slot]) {
        return 1;
    }
    return (river_flags[pl_slot] & RIVER_FLAG_ISLAND) &&
           river_island_left_x[pl_slot] < river_island_right_x[pl_slot] &&
           pl_l < river_island_right_x[pl_slot] &&
           pl_r > river_island_left_x[pl_slot];
}

/*
 * Non-zero when the jet's lit pixels overlap land or an island.  Every tile row
 * the 14 sprite rows touch is tested, each with the union of the shape rows that
 * fall in it.  Rows whose bounds are 0/0 are the empty black world at game start
 * and never collide.
 */
static uint8_t player_hits_terrain(void) {
    uint8_t px = (uint8_t)(player.x - SOURCE_X_OFFSET);
    const uint8_t *shape;

    pl_shape = (player.bank == 1) ? 7u : (player.bank == 2) ? 8u : 9u;
    shape = atari_player_shape + (uint16_t)pl_shape * ATARI_PLAYER_SHAPE_LEN;
    pl_bits = 0;
    pl_slot = river_slot_at_scanline(player.y);
    for (pl_probe = 0; pl_probe < PLAYER_HEIGHT; pl_probe++) {
        if (pl_probe != 0u &&
            !(((uint8_t)playfield_scroll_y + player.y + pl_probe - PLAYFIELD_SCANLINE) & 7u)) {
            /* The previous tile row ends here. */
            if (player_slot_hit(px)) {
                return 1;
            }
            pl_bits = 0;
            pl_slot = river_slot_at_scanline((uint8_t)(player.y + pl_probe));
        }
        pl_bits |= shape[pl_probe];
    }
    return player_slot_hit(px);
}

/*
 * One 60 Hz step of the Atari's steering ($A455-$A493).  Holding a direction
 * ramps a speed (pl_steer_acc, +8 per frame, $2B) that is added to an 8-bit
 * fraction (pl_steer_frac, $58); each carry/borrow moves the jet 1 px.  From rest
 * that is 1 px on the first frame, then one per ~8 frames, reaching 1 px/frame
 * after 31 frames - so a short tap nudges it by a pixel or two.  Right gets its
 * first-frame step from the "previous stick had no right" test ($B9 bit 7).
 * Releasing the stick clears the ramp.  Both directions: right wins ($A473).
 */
static uint8_t pl_steer_acc;
static uint8_t pl_steer_frac;
static uint8_t pl_prev_right;
static uint8_t pl_step_px;
static uint16_t pl_steer_sum;

static void player_steer_tick(void) {
    if (!(pl_pad & (J_LEFT | J_RIGHT))) {
        pl_steer_acc = 0;
        pl_steer_frac = 0;
        return;
    }
    if (pl_steer_acc >= 248u) {
        pl_step_px = 1u;
    } else {
        pl_steer_acc = (uint8_t)(pl_steer_acc + 8u);
        if (pl_pad & J_RIGHT) {
            pl_steer_sum = (uint16_t)(pl_prev_right ? pl_steer_frac : 0xFFu) +
                           pl_steer_acc;
            pl_steer_frac = (uint8_t)pl_steer_sum;
            pl_step_px = (uint8_t)(pl_steer_sum >> 8);
        } else {
            pl_step_px = (pl_steer_frac < pl_steer_acc);
            pl_steer_frac = (uint8_t)(pl_steer_frac - pl_steer_acc);
        }
    }
    if (!pl_step_px) {
        return;
    }
    if (pl_pad & J_RIGHT) {
        if (player.x < PLAYER_X_MAX) {
            player.x++;
        }
    } else if (player.x > PLAYER_X_MIN) {
        player.x--;
    }
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
    player_state = (hud_lives == 0u && !(pl_two && pl_other[3] != 0u))
        ? PLAYER_STATE_OVER : PLAYER_STATE_DEAD;
}

/* $B247: exchange the active player's score/lives/bridge/checkpoint with the
 * idle player's. */
static void player_swap_players(void) {
    for (pl_i = 0; pl_i < 3u; pl_i++) {
        pl_t = hud_score_bcd[pl_i];
        hud_score_bcd[pl_i] = pl_other[pl_i];
        pl_other[pl_i] = pl_t;
    }
    pl_t = hud_lives;
    hud_lives = pl_other[3];
    pl_other[3] = pl_t;
    pl_t = hud_bridge;
    hud_bridge = pl_other[4];
    pl_other[4] = pl_t;
    river_swap_checkpoint(&pl_other[5]);
    pl_cur ^= 1u;
    hud_cur = pl_cur;
}

/* Game number (the Atari's $09 + 1) and the scoreboard layout that goes with it. */
static void player_show_game(void) {
    hud_game = (uint8_t)(1u + (pl_sel << 1) + pl_two);
    hud_two = pl_two;
    hud_cur = pl_cur;
}

/* Pause menu: the sprites other than the menu's own are cleared from its band
 * (they are redrawn by the entity/player passes once the game resumes). */
static void player_pause_draw(void) {
    for (pl_i = 0; pl_i < PAUSE_PARTS; pl_i++) {
        pl_slot = pause_slot[pl_i];
        set_sprite_tile(pl_slot, (uint8_t)(PAUSE_TILE_BASE + pause_tile[pl_i]));
        set_sprite_prop(pl_slot, S_PAL(0));
        move_sprite(pl_slot, pause_x[pl_i],
                    (uint8_t)((pl_i < 6u ? PAUSE_ROW1_Y : PAUSE_ROW2_Y) - 1u));
    }
    set_sprite_tile(PAUSE_CURSOR_SLOT, HUD_LIFE_TILE);     /* the plane icon */
    set_sprite_prop(PAUSE_CURSOR_SLOT, S_PAL(0));
    move_sprite(PAUSE_CURSOR_SLOT, PAUSE_CURSOR_X,
                (uint8_t)((pause_sel ? PAUSE_ROW2_Y : PAUSE_ROW1_Y) - 1u));
}

static void player_pause_hide(void) {
    for (pl_i = 0; pl_i < PAUSE_PARTS; pl_i++) {
        hide_sprite(pause_slot[pl_i]);
    }
    hide_sprite(PAUSE_CURSOR_SLOT);
}

static void player_pause_enter(void) {
    river_freeze = 1u;
    ent_paused = 1u;
    sound_set_engine(SOUND_ENGINE_OFF);
    sound_play_blip();
    for (pl_i = 0; pl_i < PLAYER_SHOT_SLOT + 1u; pl_i++) {
        pl_t = shadow_OAM[pl_i].y;
        if (pl_t < 232u && (uint8_t)(pl_t + 8u) > PAUSE_BAND_TOP && pl_t < PAUSE_BAND_BOT) {
            shadow_OAM[pl_i].y = 240u;
        }
    }
    pause_sel = 0;
    pause_from = player_state;
    player_pause_draw();
    player_state = PLAYER_STATE_PAUSE;
}

/* Back to the attract screen on start level `variant`: a fresh river with no
 * jet, a blank scoreboard and the hi-score of that level.  The jet is gone and
 * the menu (Up/Down = start level, SELECT = 1P/2P, START) is live. */
static void player_attract(uint8_t variant) {
    pl_hi[pl_hi_idx][0] = hud_hi_bcd[0];
    pl_hi[pl_hi_idx][1] = hud_hi_bcd[1];
    pl_hi[pl_hi_idx][2] = hud_hi_bcd[2];
    player_kill_shot();
    entities_clear();
    hide_sprite(PLAYER_SPRITE_TOP);
    hide_sprite(PLAYER_SPRITE_BOTTOM);
    player_pause_hide();
    ent_paused = 0;
    pl_nocol = 0;
    sound_set_engine(SOUND_ENGINE_OFF);
    DISPLAY_OFF;
    hud_bridge = river_init(variant);
    DISPLAY_ON;
    pl_sel = river_variant;
    pl_frozen = 0;
    ent_hold = 0;                       /* the demo world moves again */
    pl_hi_idx = river_variant;
    hud_hi_bcd[0] = pl_hi[pl_hi_idx][0];
    hud_hi_bcd[1] = pl_hi[pl_hi_idx][1];
    hud_hi_bcd[2] = pl_hi[pl_hi_idx][2];
    hud_score_bcd[0] = 0;
    hud_score_bcd[1] = 0;
    hud_score_bcd[2] = 0;
    hud_lives = 0;
    hud_fuel = 255u;
    pl_cur = 0;
    pl_other[0] = 0;                    /* no P2 score left over from the last game */
    pl_other[1] = 0;
    pl_other[2] = 0;
    player.alive = 0;
    river_speed = 1u;
    river_vel = RIVER_VEL_CRUISE;
    player_show_game();
    player_state = PLAYER_STATE_ATTRACT;
}

/* RESUME / EXIT: Up, Down and SELECT move the cursor, START or A confirms. */
static void player_pause_update(void) {
    pl_new = (uint8_t)(pl_pad & ~player_prev_pad);
    player_prev_pad = pl_pad;
    if (pl_new & (J_UP | J_DOWN | J_SELECT)) {
        pause_sel ^= 1u;
        player_pause_draw();
        sound_play_blip();
    } else if (pl_new & (J_START | J_A)) {
        sound_play_blip();
        if (pause_sel) {
            player_attract(river_variant);
        } else {
            player_pause_hide();
            /* Paused before the first input: the world still waits for the jet. */
            river_freeze = (pause_from == PLAYER_STATE_READY);
            ent_paused = 0;
            player_state = pause_from;
        }
    }
}

/* Atari $A69D: the game number steps 0..7 and wraps; bit 0 is "two players" and
 * bits 1-2 the start level.  Up = next, Down = previous.
 *
 * Like the Atari, a change only edits the scoreboard and freezes the old river
 * (and its enemies: $76 = $FF, $29 = 0) and (re)loads the idle timer $2A = $25,
 * counted down every 8 frames ($A2F5).  The picked level's river starts from
 * black only when that timer runs out ($A688), or at START. */
#define ATTRACT_SELECT_FRAMES   (0x25u * 8u)

static void player_step_game(uint8_t pad) {
    pl_t = (uint8_t)(((pl_sel << 1) | pl_two) + ((pad & J_UP) ? 1u : 7u)) & 7u;
    pl_two = (uint8_t)(pl_t & 1u);
    if ((uint8_t)(pl_t >> 1) != pl_sel) {
        /* New start level: its hi-score and first bridge number. */
        pl_hi[pl_hi_idx][0] = hud_hi_bcd[0];
        pl_hi[pl_hi_idx][1] = hud_hi_bcd[1];
        pl_hi[pl_hi_idx][2] = hud_hi_bcd[2];
        pl_sel = (uint8_t)(pl_t >> 1);
        pl_hi_idx = pl_sel;
        hud_hi_bcd[0] = pl_hi[pl_sel][0];
        hud_hi_bcd[1] = pl_hi[pl_sel][1];
        hud_hi_bcd[2] = pl_hi[pl_sel][2];
        hud_bridge = atari_level_descriptors[pl_sel * 6u + 4u];
    }
    river_freeze = 1u;
    ent_hold = 1u;
    pl_frozen = 1u;
    pl_wait_end = (uint16_t)(sys_time + ATTRACT_SELECT_FRAMES);
    player_show_game();
    sound_play_blip();
}

static void player_respawn(uint8_t new_game);

/* New game from the attract screen; the other player (if any) starts on the
 * same level. */
static void player_start_game(void) {
    hud_lives = 3u;
    pl_cur = 0;
    pl_frozen = 0;
    pl_other[0] = 0;
    pl_other[1] = 0;
    pl_other[2] = 0;
    pl_other[3] = 3u;
    pl_other[4] = atari_level_descriptors[pl_sel * 6u + 4u];
    pl_other[5] = pl_other[4];                  /* level */
    pl_other[6] = atari_level_descriptors[pl_sel * 6u + 0u];
    pl_other[7] = atari_level_descriptors[pl_sel * 6u + 1u];
    pl_other[8] = atari_level_descriptors[pl_sel * 6u + 2u];
    pl_other[9] = atari_level_descriptors[pl_sel * 6u + 3u];
    player_show_game();
    sound_play_blip();
    player_respawn(1);
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
        hud_bridge = river_init(pl_sel);
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
    ent_hold = 1u;                      /* the world waits for the jet */
    ent_scrolled = 0;
    pl_steer_acc = 0;
    pl_steer_frac = 0;
    player_state = PLAYER_STATE_WAIT;
}

void player_update(void) BANKED {
    /* The Atari blinks the active player's label + score from the (re)start
     * until the first input ($76 in 1..$7F). */
    hud_blink = (player_state == PLAYER_STATE_WAIT || player_state == PLAYER_STATE_READY);

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

    if (player_state == PLAYER_STATE_ATTRACT) {
        if (pl_frozen && (int16_t)((uint16_t)sys_time - pl_wait_end) >= 0) {
            player_attract(pl_sel);     /* idle timer out: the picked level begins ($A688) */
        }
        pl_pad = joypad();
        pl_new = (uint8_t)(pl_pad & ~player_prev_pad);
        player_prev_pad = pl_pad;
        if (pl_new & (J_START | J_SELECT)) {
            /* SELECT is the debugging start: the same game, but the jet
             * cannot crash into anything. */
            pl_nocol = (pl_new & J_SELECT) ? 1u : 0u;
            player_start_game();
        } else if (pl_new & (J_UP | J_DOWN)) {
            pl_rep = ATTRACT_REPEAT_FRAMES;
            player_step_game(pl_new);
        } else if (pl_pad & (J_UP | J_DOWN)) {
            /* Held: repeat every $1E frames like the Atari ($A6DA). */
            if (pl_rep > game_ticks) {
                pl_rep = (uint8_t)(pl_rep - game_ticks);
            } else {
                pl_rep = ATTRACT_REPEAT_FRAMES;
                player_step_game(pl_pad);
            }
        }
        return;
    }

    if (player_state == PLAYER_STATE_WAIT) {
        if (river_right_x[river_slot_at_scanline(239u)] == 0u) {
            return;
        }
        river_freeze = 1u;
        player_center();
        entities_clear_near(player.y);  /* nothing spawns on top of the jet */
        player_state = PLAYER_STATE_READY;
    }

    pl_ticks = game_ticks;
    if (pl_two) {
        joypad_ex(&pl_pads);
        pl_pad = pl_pads.joypads[pl_cur];
    } else {
        pl_pad = joypad();
    }

    if (player_state == PLAYER_STATE_PAUSE) {
        player_pause_update();
        return;
    }

    if (player_state == PLAYER_STATE_READY) {
        /* Stopped on the river: the game starts when the player fires or
         * moves. */
        player_show(PLAYER_TILE_STRAIGHT);
        if ((pl_pad & ~player_prev_pad) & J_START) {
            player_pause_enter();
            player_prev_pad = pl_pad;
            return;
        }
        if (!(pl_pad & PLAYER_START_INPUT)) {
            player_prev_pad = pl_pad;
            return;
        }
        river_freeze = 0;
        ent_hold = 0;
        player_state = PLAYER_STATE_PLAY;
    }

    if (player_state != PLAYER_STATE_PLAY) {
        if (player_timer != 0u) {
            player_death_anim();
            player_timer = (player_timer > pl_ticks) ? (uint8_t)(player_timer - pl_ticks) : 0u;
        } else if (player_state == PLAYER_STATE_DEAD) {
            if (pl_two && pl_other[3] != 0u) {
                player_swap_players();      /* the other player's turn */
            }
            player_respawn(0);
        } else {
            player_attract(river_variant);  /* game over */
        }
        player_prev_pad = pl_pad;
        return;
    }

    if ((pl_pad & ~player_prev_pad) & J_START) {
        player_pause_enter();
        player_prev_pad = pl_pad;
        return;
    }

    /* Steering and banking. */
    player.bank = PLAYER_BANK_NONE;
    if (pl_pad & J_RIGHT) {
        player.bank = 2;
    } else if (pl_pad & J_LEFT) {
        player.bank = 1;
    }
    pl_prev_right = (uint8_t)(player_prev_pad & J_RIGHT);
    for (pl_i = 0; pl_i < pl_ticks; pl_i++) {
        player_steer_tick();
        pl_prev_right = (uint8_t)(pl_pad & J_RIGHT);
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
    } else if (hud_fuel == 0u ||
               (!pl_nocol && (pl_hit == ENT_HIT_DEADLY || player_hits_terrain()))) {
        player_crash();
    }
    player_prev_pad = pl_pad;
}
