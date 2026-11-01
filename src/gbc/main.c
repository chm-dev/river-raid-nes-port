/*
 * gbc/main.c - River Raid Game Boy Color port, milestone 1.
 *
 * Boot: refuse to run on anything but a GBC (an original Game Boy ignores the
 * CGB-only header flag), switch to double speed, load the BG palette, build
 * the river ring in BG map 0 and run the frame loop.
 *
 * Camera: the ring is 256 px, so the river's scroll value is also SCY (plus a
 * constant that anchors the 128 px view to the bottom of the logic world).
 * There is no split-screen interrupt: SCX/SCY are written right after vsync(),
 * still inside VBlank.  Game logic keeps NES coordinates (X = Atari HPOS, Y =
 * NES scanlines); only this file maps them to the screen.
 *
 * Sprites: the logic files are compiled with shadow_OAM renamed to
 * gbc_logic_oam (see build_gbc.ps1), so they keep writing NES-coordinate
 * sprites.  oam_camera() translates them into the real shadow_OAM each frame.
 */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <gbdk/console.h>
#include <gbdk/font.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "hud.h"
#include "river.h"
#include "player.h"
#include "entities.h"
#include "collision.h"
#include "sound.h"

#ifndef LOGIC_FRAMES
#define LOGIC_FRAMES 2u             /* player/entities every n-th frame: 2 = 30 Hz */
#endif

/* Declared in hud.h for the NES build, where hud.c owns the split. */
volatile uint8_t playfield_scroll_y;

/* The logic sprites; this file is compiled without the shadow_OAM rename.  The
 * NES code addresses 48 slots (the shot is slot 47, entities use up to 43), but
 * the GBC has 40 hardware sprites: oam_camera() packs the visible ones. */
#define LOGIC_OAM_SLOTS     64u     /* NES slots 0..63: the pause menu letters use 56..63 */
volatile struct OAM_item_t gbc_logic_oam[LOGIC_OAM_SLOTS];

void hud_gbc_init(void);

/* Read by tests through the .noi. */
volatile uint16_t frame_counter;
uint8_t game_ticks = 1u;
static uint8_t ent_skipped;         /* 1 = last pass ran the player group only */
static uint8_t main_last;
#ifdef PROF_EXPORT_STATICS
/* Per-pass log for tests/gb/pass_log.mjs: frames elapsed (game_ticks), pass kind
 * (0 entities, 1 player, 2 player+entities), LY at pass start / after the river /
 * after the logic, and sys_time deltas for the last two. */
#define PASS_LOG_N 64u
uint8_t pl_idx;
uint8_t pl_rec[PASS_LOG_N][10];
#ifdef PROF_OAMCHECK
uint8_t pl_oam_a[160];
uint16_t pl_oam_n, pl_oam_err;
#endif
#endif
static uint8_t logic_acc;      /* frames since the last logic pass */

/* Camera. SCX runs 0..GBC_CAM_MAX_X: 24 map columns (192 px) in a 160 px
 * screen leave 32 px to pan. */
#define GBC_CAM_MAX_X       32u
#define GBC_CAM_STEP        1u     /* px per frame the camera may move */
#define GBC_CAM_DEAD        12u     /* px the jet may drift from centre before the camera pans */
#define GBC_CAM_CENTER     (GBC_CAM_MAX_X / 2u)
#define GBC_CAM_JET_X       (SOURCE_X_OFFSET + 76u) /* jet X that puts the camera at 0 */
static uint8_t camera_x = GBC_CAM_CENTER;
static uint8_t cam_spawn_set;       /* camera_x holds this river's spawn view */
#define GBC_PLAYER_STATE_ATTRACT 5u /* = PLAYER_STATE_ATTRACT in player.h */
#define GBC_PLAYER_STATE_WAIT 3u    /* = PLAYER_STATE_WAIT in player.c */
#define GBC_PLAYER_STATE_READY 4u   /* = PLAYER_STATE_READY in player.c */
#define GBC_PLAYER_STATE_PAUSE 6u   /* = PLAYER_STATE_PAUSE in player.c */
#define GBC_PLAYER_WIDTH      8u    /* = PLAYER_WIDTH in player.c */

/* Index meaning is set by the river tiles: 0 outline (black), 1 land,
 * 3 water; 2 is spare (road line).  One palette for now; per-row colour is
 * milestone 3. */
static const palette_color_t bkg_palette[4][4] = {
    { GBC_RGB_BLACK, GBC_RGB_LAND, GBC_RGB_BROWN, GBC_RGB_WATER },     /* 0 land */
    { GBC_RGB_BLACK, GBC_RGB_BLACK, GBC_RGB_BLACK, GBC_RGB_BLACK },     /* 1 HUD (hud_gbc_init) */
    { GBC_RGB_BLACK, GBC_RGB_ALT, GBC_RGB_BROWN, GBC_RGB_WATER },      /* 2 bright land */
    { GBC_RGB_BLACK, GBC_RGB_ROAD, GBC_RGB_YELLOW, GBC_RGB_WATER }      /* 3 road */
};

/* Sprite palettes: 0 jet / fuel depot / shot, 1 bridge, 2 helicopters, 3 ships
 * (the colours gen_entity_gfx.py picks on the NES, as RGB555).  Colour 0 is
 * transparent. */
static const palette_color_t spr_palette[4][4] = {
    { RGB(0, 0, 0), RGB(31, 28, 6), RGB(31, 31, 31), RGB(30, 6, 4) },
    { RGB(0, 0, 0), RGB(3, 14, 4), GBC_RGB_BROWN, RGB(20, 20, 6) },
    { RGB(0, 0, 0), RGB(0, 0, 0), RGB(31, 28, 6), RGB(4, 6, 18) },
    { RGB(0, 0, 0), RGB(0, 0, 0), RGB(22, 4, 4), RGB(6, 28, 28) }
};

void hud_set_playfield_scroll(uint8_t y) {
    playfield_scroll_y = y;
}

/* An original Game Boy ignores the header's CGB-only flag, so say why nothing
 * will run instead of showing garbage. */
static void gbc_required(void) {
    font_init();
    font_set(font_load(font_ibm));
    gotoxy(4, 8);
    puts("GBC REQUIRED");
    SHOW_BKG;
    DISPLAY_ON;
    for (;;) {
        wait_vbl_done();
    }
}

/* Camera X for a jet at logic X `jet_x`, clamped to the ring. */
static uint8_t camera_for(int16_t jet_x) {
    jet_x -= (int16_t)GBC_CAM_JET_X;
    if (jet_x < 0) {
        return 0u;
    }
    return (jet_x > (int16_t)GBC_CAM_MAX_X) ? (uint8_t)GBC_CAM_MAX_X : (uint8_t)jet_x;
}

/* Centre the view on the jet, clamped to the ring, a few pixels per frame.
 *
 * Attract and the (re)spawn scroll-in (WAIT) have no jet: the view holds the
 * X the jet will give it when it appears.  player_center() puts the jet on the
 * centre of RIVER_SPAWN_SLOT, so that is computed with the same formula and
 * set at once, not eased.  river_start() zeroes every slot's bounds and a
 * generated row is never 0, so a zero there means a fresh river whose spawn
 * slot is not generated yet: keep the old X meanwhile (the slot is written
 * ~12 scrolled rows after the start, while the whole view is still black).
 * The value is latched because attract keeps scrolling and the ring later
 * overwrites the slot; it is re-read after every river_init/river_restart,
 * including the attract-menu level change, which stays in ATTRACT. */
static void camera_follow(void) {
    uint8_t want;
    int16_t ideal;

    if (player_state == GBC_PLAYER_STATE_ATTRACT || player_state == GBC_PLAYER_STATE_WAIT) {
        if (river_right_x[RIVER_SPAWN_SLOT] == 0u) {
            cam_spawn_set = 0;
        } else if (!cam_spawn_set) {
            cam_spawn_set = 1u;
            camera_x = camera_for((int16_t)(SOURCE_X_OFFSET - (GBC_PLAYER_WIDTH >> 1)) +
                                  (int16_t)(((uint16_t)river_left_x[RIVER_SPAWN_SLOT] +
                                             river_right_x[RIVER_SPAWN_SLOT]) >> 1));
        }
        return;
    }
    cam_spawn_set = 0;

    want = camera_for((int16_t)player.x);
    if (player_state == GBC_PLAYER_STATE_READY) {
        camera_x = want;                /* the jet just appeared: no easing (already equal) */
        return;
    }
    /* Dead zone: hold the camera while the jet stays within GBC_CAM_DEAD px of
     * the view centre; past that, aim for the spot that puts the jet on the
     * zone's edge.  Measured from the unclamped centred camera so the pan still
     * reaches 0 / GBC_CAM_MAX_X. */
    ideal = (int16_t)player.x - (int16_t)GBC_CAM_JET_X;
    if (ideal - (int16_t)camera_x > (int16_t)GBC_CAM_DEAD) {
        ideal -= (int16_t)GBC_CAM_DEAD;
    } else if ((int16_t)camera_x - ideal > (int16_t)GBC_CAM_DEAD) {
        ideal += (int16_t)GBC_CAM_DEAD;
    } else {
        ideal = (int16_t)camera_x;
    }
    want = (ideal < 0) ? 0u : (ideal > (int16_t)GBC_CAM_MAX_X) ? (uint8_t)GBC_CAM_MAX_X : (uint8_t)ideal;
    if (want > camera_x) {
        camera_x = ((uint8_t)(want - camera_x) > GBC_CAM_STEP) ? (uint8_t)(camera_x + GBC_CAM_STEP) : want;
    } else if (want < camera_x) {
        camera_x = ((uint8_t)(camera_x - want) > GBC_CAM_STEP) ? (uint8_t)(camera_x - GBC_CAM_STEP) : want;
    }
}

static void apply_camera(void) {
    SCX_REG = camera_x;
    SCY_REG = (uint8_t)(playfield_scroll_y + (GBC_VIEW_TOP_SCANLINE - PLAYFIELD_SCANLINE));
}

/* Translate the logic sprites (NES coordinates) to hardware OAM: subtract the
 * camera, add the +16/+8 hardware offsets and hide whatever is outside the
 * view.  Sprite tiles live in VRAM bank 1 (OAM attribute bit 3). */
/* oam_camera's 8-bit range tests wrap correctly only with SOURCE_X_OFFSET + camera >= 8. */
typedef char oam_camera_x_assert[(SOURCE_X_OFFSET >= 8u) ? 1 : -1];

static void oam_camera_slow(void) {
    uint8_t i, n = 0, y, x, xbase;

    /* All 8-bit: y < 240 spans fewer than 256 values and x spans exactly 256,
     * so "-8 < v < limit" becomes one unsigned compare of v + 8 - 1 (the sum
     * wraps for everything off-view). */
    xbase = (uint8_t)(SOURCE_X_OFFSET + camera_x - 8u);
    for (i = 0; i < LOGIC_OAM_SLOTS && n < 40u; i++) {
        y = gbc_logic_oam[i].y;
        if (y >= ENT_CULL_Y) {
            continue;
        }
        /* Pause menu slots (player.c) hold hardware coordinates: no camera. */
        if (player_state == GBC_PLAYER_STATE_PAUSE && (i >= 56u || (i >= 2u && i <= 4u))) {
            shadow_OAM[n].y = y;
            shadow_OAM[n].x = gbc_logic_oam[i].x;
            shadow_OAM[n].tile = gbc_logic_oam[i].tile;
            shadow_OAM[n].prop = (uint8_t)(gbc_logic_oam[i].prop | S_BANK);
            n++;
            continue;
        }
        y = (uint8_t)(y - (GBC_VIEW_TOP_SCANLINE - 8u));        /* vy + 8 */
        x = (uint8_t)(gbc_logic_oam[i].x - xbase);              /* vx + 8 */
        if ((uint8_t)(y - 1u) >= (uint8_t)(GBC_VIEW_LINES + 7u) ||
            (uint8_t)(x - 1u) >= 167u) {
            continue;
        }
        shadow_OAM[n].y = (uint8_t)(y + 8u);
        shadow_OAM[n].x = x;
        shadow_OAM[n].tile = gbc_logic_oam[i].tile;
        shadow_OAM[n].prop = (uint8_t)(gbc_logic_oam[i].prop | S_BANK);
        n++;
    }
    while (n < 40u) {
        shadow_OAM[n++].y = 0u;
    }
}

/* The common case (no pause menu): same output as oam_camera_slow, walking the
 * arrays by pointer.  The per-slot work is what costs here (64 slots, most of
 * them empty), so index arithmetic and the pause test stay out of the loop. */
static void oam_camera(void) {
    const OAM_item_t *s;
    OAM_item_t *d;
    uint8_t i, n, y, x, xbase;

    if (player_state == GBC_PLAYER_STATE_PAUSE) {
        oam_camera_slow();
        return;
    }
    xbase = (uint8_t)(SOURCE_X_OFFSET + camera_x - 8u);
    s = gbc_logic_oam;
    d = shadow_OAM;
    n = 40u;
    for (i = LOGIC_OAM_SLOTS; i != 0u; i--, s++) {
        y = s->y;
        if (y >= ENT_CULL_Y) {
            continue;
        }
        y = (uint8_t)(y - (GBC_VIEW_TOP_SCANLINE - 8u));        /* vy + 8 */
        x = (uint8_t)(s->x - xbase);                            /* vx + 8 */
        if ((uint8_t)(y - 1u) >= (uint8_t)(GBC_VIEW_LINES + 7u) ||
            (uint8_t)(x - 1u) >= 167u) {
            continue;
        }
        d->y = (uint8_t)(y + 8u);
        d->x = x;
        d->tile = s->tile;
        d->prop = (uint8_t)(s->prop | S_BANK);
        d++;
        if (--n == 0u) {
            return;
        }
    }
    while (n != 0u) {
        d->y = 0u;
        d++;
        n--;
    }
}
/* The HUD Window starts at screen line GBC_VIEW_LINES and sprites draw over it,
 * so entities would stay visible on the HUD until culled.  LYC fires on the last
 * playfield line; once its HBlank is reached (the line's sprites are drawn) the
 * sprite layer is switched off, and the main loop switches it on again in VBlank.
 * Short on purpose, no banked calls (rule 2). */
static void hud_hide_sprites_isr(void) {
    while ((STAT_REG & STATF_BUSY) != 0u) {
    }
    LCDC_REG &= (uint8_t)~LCDCF_OBJON;
}

void main(void) {
    uint8_t i;

    if (_cpu != CGB_TYPE) {
        gbc_required();
    }
    cpu_fast();

    DISPLAY_OFF;
    set_bkg_palette(0, GBC_RIVER_PALS, &bkg_palette[0][0]);
    for (i = 0; i < LOGIC_OAM_SLOTS; i++) {
        gbc_logic_oam[i].y = 240u;
    }

    /* Sprite tiles go to VRAM bank 1: the ids the sprite code uses overlap the
     * river cache in bank 0. */
    VBK_REG = 1;
    player_init_graphics();
    entities_init_graphics();
    VBK_REG = 0;
    /* The two modules above feed NES colour numbers to the palette call (a
     * no-op in entities.c, garbage in player.c); set the real ones last. */
    set_sprite_palette(0, 4, &spr_palette[0][0]);
    hud_gbc_init();
    sound_init();

    river_init(0);
    player_init();
    entities_init();
    collision_init();
    camera_follow();
    apply_camera();
    oam_camera();
    SHOW_BKG;
    SHOW_SPRITES;
    LYC_REG = (uint8_t)(GBC_VIEW_LINES - 1u);
    STAT_REG |= STATF_LYC;
    add_LCD(hud_hide_sprites_isr);
    set_interrupts(VBL_IFLAG | LCD_IFLAG);
    DISPLAY_ON;

    main_last = (uint8_t)sys_time;
    for (;;) {
        /* Wait for a new frame, but only if none passed during the last
         * pass: an overrun pass starts the next one at once (catch-up). */
        vsync();
        SHOW_SPRITES;                   /* hud_hide_sprites_isr turned them off */
        apply_camera();

        game_ticks = (uint8_t)((uint8_t)sys_time - main_last);
        main_last = (uint8_t)sys_time;
        if (game_ticks > 4u) {
            game_ticks = 4u;
        }
        frame_counter++;
        sound_update();                 /* real frame count, before game_ticks is merged */
        if ((uint8_t)(logic_acc + game_ticks) < LOGIC_FRAMES) {
            hud_update();               /* VRAM writes, still in VBlank */
        }

        /* Same schedule as the NES main: the river every frame, the player
         * group and the entity group on alternate frames. */
        river_gen_budget = ((uint8_t)(logic_acc + game_ticks) >= LOGIC_FRAMES) ? 3u : 1u;
#ifdef PROF_EXPORT_STATICS
        pl_rec[pl_idx][0] = game_ticks;
        pl_rec[pl_idx][1] = LY_REG;
        pl_rec[pl_idx][7] = (uint8_t)sys_time;
        pl_rec[pl_idx][8] = river_owed;
#endif
        river_update();
#ifdef PROF_EXPORT_STATICS
        pl_rec[pl_idx][2] = LY_REG;
        pl_rec[pl_idx][3] = (uint8_t)((uint8_t)sys_time - pl_rec[pl_idx][7]);
        pl_rec[pl_idx][9] = river_owed;
#endif

        logic_acc = (uint8_t)(logic_acc + game_ticks);
        if (logic_acc >= LOGIC_FRAMES) {
            game_ticks = logic_acc;
            logic_acc = 0;
            /* An overrun pass (game_ticks >= LOGIC_FRAMES) takes this branch
             * every time, which would starve the entity group: it moves,
             * spawns and removes them.  Run it after the player in that case. */
            if (ent_skipped) {
                player_update();
                collision_update();
                entities_update();
                ent_skipped = 0;
#ifdef PROF_EXPORT_STATICS
                pl_rec[pl_idx][5] = 2u;
#endif
            } else {
                entities_follow();
                player_update();
                collision_update();
                ent_skipped = 1;
#ifdef PROF_EXPORT_STATICS
                pl_rec[pl_idx][5] = 1u;
#endif
            }
        } else {
            entities_update();
            ent_skipped = 0;
#ifdef PROF_EXPORT_STATICS
            pl_rec[pl_idx][5] = 0u;
#endif
        }
#ifdef PROF_EXPORT_STATICS
        pl_rec[pl_idx][4] = LY_REG;     /* after the logic, before camera/OAM */
#endif
        camera_follow();
        oam_camera();
#ifdef PROF_EXPORT_STATICS
        pl_rec[pl_idx][6] = LY_REG;
#ifdef PROF_OAMCHECK
        /* oam_camera must match the reference walk byte for byte (LESSONS 18). */
        memcpy(pl_oam_a, shadow_OAM, sizeof pl_oam_a);
        oam_camera_slow();
        pl_oam_n++;
        if (memcmp(pl_oam_a, shadow_OAM, sizeof pl_oam_a) != 0) {
            pl_oam_err++;
        }
#endif
        pl_idx = (uint8_t)((pl_idx + 1u) & (PASS_LOG_N - 1u));
#endif
    }
}
