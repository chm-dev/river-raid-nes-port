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

#include "config.h"
#include "hud.h"
#include "river.h"
#include "player.h"
#include "entities.h"
#include "collision.h"

#ifndef LOGIC_FRAMES
#define LOGIC_FRAMES 2u             /* player/entities every n-th frame: 2 = 30 Hz */
#endif

/* Declared in hud.h for the NES build, where hud.c owns the split. */
volatile uint8_t playfield_scroll_y;

/* The logic sprites; this file is compiled without the shadow_OAM rename.  The
 * NES code addresses 48 slots (the shot is slot 47, entities use up to 43), but
 * the GBC has 40 hardware sprites: oam_camera() packs the visible ones. */
#define LOGIC_OAM_SLOTS     48u
volatile struct OAM_item_t gbc_logic_oam[LOGIC_OAM_SLOTS];

void hud_gbc_init(void);

/* Read by tests through the .noi. */
volatile uint16_t frame_counter;
uint8_t game_ticks = 1u;
static uint8_t main_last;
static uint8_t logic_acc;       /* frames since the last logic pass */

/* Camera. SCX runs 0..GBC_CAM_MAX_X: 24 map columns (192 px) in a 160 px
 * screen leave 32 px to pan. */
#define GBC_CAM_MAX_X       32u
#define GBC_CAM_STEP        2u      /* px per frame the camera may move */
static uint8_t camera_x;

/* Index meaning is set by the river tiles: 0 outline (black), 1 land,
 * 3 water; 2 is spare (road line).  One palette for now; per-row colour is
 * milestone 3. */
static const palette_color_t bkg_palette[4] = {
    RGB(0, 0, 0), RGB(5, 17, 5), RGB(28, 26, 6), RGB(6, 12, 26)
};

/* Sprite palettes: 0 jet / fuel depot / shot, 1 bridge, 2 helicopters, 3 ships
 * (the colours gen_entity_gfx.py picks on the NES, as RGB555).  Colour 0 is
 * transparent. */
static const palette_color_t spr_palette[4][4] = {
    { RGB(0, 0, 0), RGB(31, 28, 6), RGB(31, 31, 31), RGB(30, 6, 4) },
    { RGB(0, 0, 0), RGB(3, 14, 4), RGB(18, 11, 4), RGB(20, 20, 6) },
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

/* Centre the view on the jet, clamped to the ring, a few pixels per frame. */
static void camera_follow(void) {
    int16_t want = (int16_t)player.x - (int16_t)SOURCE_X_OFFSET - 76;

    if (want < 0) {
        want = 0;
    } else if (want > (int16_t)GBC_CAM_MAX_X) {
        want = GBC_CAM_MAX_X;
    }
    if (want > (int16_t)camera_x) {
        camera_x = (uint8_t)((want - camera_x > GBC_CAM_STEP) ? camera_x + GBC_CAM_STEP : want);
    } else if (want < (int16_t)camera_x) {
        camera_x = (uint8_t)((camera_x - want > GBC_CAM_STEP) ? camera_x - GBC_CAM_STEP : want);
    }
}

static void apply_camera(void) {
    SCX_REG = camera_x;
    SCY_REG = (uint8_t)(playfield_scroll_y + (GBC_VIEW_TOP_SCANLINE - PLAYFIELD_SCANLINE));
}

/* Translate the logic sprites (NES coordinates) to hardware OAM: subtract the
 * camera, add the +16/+8 hardware offsets and hide whatever is outside the
 * view.  Sprite tiles live in VRAM bank 1 (OAM attribute bit 3). */
static void oam_camera(void) {
    uint8_t i, n = 0;
    int16_t vy, vx;

    for (i = 0; i < LOGIC_OAM_SLOTS && n < 40u; i++) {
        vy = (int16_t)gbc_logic_oam[i].y - (int16_t)GBC_VIEW_TOP_SCANLINE;
        vx = (int16_t)gbc_logic_oam[i].x - (int16_t)SOURCE_X_OFFSET - (int16_t)camera_x;
        if (gbc_logic_oam[i].y >= 232u || vy <= -8 || vy >= (int16_t)GBC_VIEW_LINES ||
            vx <= -8 || vx >= 160) {
            continue;
        }
        shadow_OAM[n].y = (uint8_t)(vy + 16);
        shadow_OAM[n].x = (uint8_t)(vx + 8);
        shadow_OAM[n].tile = gbc_logic_oam[i].tile;
        shadow_OAM[n].prop = (uint8_t)(gbc_logic_oam[i].prop | S_BANK);
        n++;
    }
    while (n < 40u) {
        shadow_OAM[n++].y = 0u;
    }
}

void main(void) {
    uint8_t i;

    if (_cpu != CGB_TYPE) {
        gbc_required();
    }
    cpu_fast();

    DISPLAY_OFF;
    set_bkg_palette(0, 1, bkg_palette);
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

    river_init(0);
    player_init();
    entities_init();
    collision_init();
    camera_follow();
    apply_camera();
    oam_camera();
    SHOW_BKG;
    SHOW_SPRITES;
    DISPLAY_ON;

    main_last = (uint8_t)sys_time;
    for (;;) {
        /* Wait for a new frame, but only if none passed during the last
         * pass: an overrun pass starts the next one at once (catch-up). */
        vsync();
        apply_camera();

        game_ticks = (uint8_t)((uint8_t)sys_time - main_last);
        main_last = (uint8_t)sys_time;
        if (game_ticks > 4u) {
            game_ticks = 4u;
        }
        frame_counter++;
        if ((uint8_t)(logic_acc + game_ticks) < LOGIC_FRAMES) {
            hud_update();               /* VRAM writes, still in VBlank */
        }

        /* Same schedule as the NES main: the river every frame, the player
         * group and the entity group on alternate frames. */
        river_gen_budget = ((uint8_t)(logic_acc + game_ticks) >= LOGIC_FRAMES) ? 3u : 1u;
        river_update();

        logic_acc = (uint8_t)(logic_acc + game_ticks);
        if (logic_acc >= LOGIC_FRAMES) {
            game_ticks = logic_acc;
            logic_acc = 0;
            entities_follow();
            player_update();
            collision_update();
        } else {
            entities_update();
        }
        camera_follow();
        oam_camera();
    }
}
