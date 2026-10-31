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
 * still inside VBlank.  Game logic keeps NES coordinates; only this file maps
 * them to the screen.
 */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <gbdk/console.h>
#include <gbdk/font.h>
#include <stdint.h>
#include <stdio.h>

#include "config.h"
#include "river.h"

/* Declared in hud.h for the NES build, where hud.c owns the split. */
volatile uint8_t playfield_scroll_y;

/* Read by tests through the .noi. */
volatile uint16_t frame_counter;
uint8_t game_ticks = 1u;
static uint8_t main_last;

/* Camera. SCX runs 0..GBC_CAM_MAX_X: 24 map columns (192 px) in a 160 px
 * screen leave 32 px to pan. */
#define GBC_CAM_MAX_X       32u
static uint8_t camera_x;

/* Index meaning is set by the river tiles: 0 outline (black), 1 land,
 * 3 water; 2 is spare (road line).  One palette for now; per-row colour is
 * milestone 3. */
static const palette_color_t bkg_palette[4] = {
    RGB(0, 0, 0), RGB(5, 17, 5), RGB(28, 26, 6), RGB(6, 12, 26)
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

/* Temporary stand-in for the jet until player.c is ported: UP/DOWN pick the
 * throttle, LEFT/RIGHT pan the camera. */
static void debug_input(void) {
    uint8_t pad = joypad();

    river_speed = (pad & J_UP) ? 2u : (pad & J_DOWN) ? 0u : 1u;
    if ((pad & J_LEFT) && camera_x > 0u) {
        camera_x--;
    }
    if ((pad & J_RIGHT) && camera_x < GBC_CAM_MAX_X) {
        camera_x++;
    }
}

static void apply_camera(void) {
    SCX_REG = camera_x;
    SCY_REG = (uint8_t)(playfield_scroll_y + (GBC_VIEW_TOP_SCANLINE - PLAYFIELD_SCANLINE));
}

void main(void) {
    if (_cpu != CGB_TYPE) {
        gbc_required();
    }
    cpu_fast();

    DISPLAY_OFF;
    set_bkg_palette(0, 1, bkg_palette);
    river_init(0);
    apply_camera();
    SHOW_BKG;
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

        debug_input();
        river_update();
    }
}
