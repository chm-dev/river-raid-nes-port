/*
 * main.c - River Raid NES port, Phase 2 boot ROM.
 *
 * Boot sequence:
 *   1. Put the shared background palette in the shadow registers (backdrop
 *      black) and blank the display.
 *   2. Load CHR-RAM and draw the boot screen in direct VRAM mode.
 *   3. Install the VBL/LCD handlers and enable both interrupts.
 *   4. Turn the display on and run the frame loop.
 *
 * The LCD handler implements the fixed HUD / scrolling-playfield split at
 * scanline 24.  GBDK adds the LCD scanline to bkg_scroll_y when it builds
 * each buffered register write, so the playfield handler subtracts
 * HUD_SCANLINES to produce the requested absolute scroll.
 */
#include <nes/nes.h>
#include <stdint.h>

#include "config.h"
#include "hud.h"
#include "river.h"
#include "player.h"
#include "entities.h"
#include "collision.h"
#include "sound.h"

/* Read by tests/nes_smoke.mjs through roms/river_raid.noi. */
volatile uint16_t frame_counter;
volatile uint16_t vbl_events;
volatile uint16_t lcd_events;

static void vbl_handler(void) NO_OVERLAY_LOCALS;
static void lcd_handler(void) NO_OVERLAY_LOCALS;

static void vbl_handler(void) {
    /* Scanline 0 must already use the HUD scroll.  The deferred buffer
     * applies this entry at the start of the frame, so pass 0 directly
     * (adding a scanline here would tear the top row). */
    move_bkg(0, 0);
    LYC_REG = 0;
    vbl_events++;
}

static void lcd_handler(void) {
    if (LYC_REG == 0) {
        /* HUD pass: keep the first 24 scanlines at scroll 0. */
        move_bkg(0, 0);
        LYC_REG = HUD_SCANLINES;
    } else {
        /* Playfield pass.  The split entry is installed at
         * HUD_SCANLINES, so the playfield's absolute scroll is the
         * requested value from the top of the playfield. */
        move_bkg(0, playfield_scroll_y);
        LYC_REG = 0;
    }
    lcd_events++;
}

void main(void) {
    /* Set the backdrop before blanking so DISPLAY_OFF leaves black, not
     * the crt0 default white. */
    hud_init_palette();
    DISPLAY_OFF;

    hud_init_graphics();

    river_init();
    player_init();
    entities_init();
    collision_init();
    sound_init();

    add_VBL(vbl_handler);
    add_LCD(lcd_handler);
    set_interrupts(VBL_IFLAG | LCD_IFLAG);

    DISPLAY_ON;

    for (;;) {
        vsync();
        frame_counter++;
        hud_update_frame();

        /* Phase 4-6 update hooks; each is a no-op stub for now. */
        river_update();
        player_update();
        entities_update();
        collision_update();
        sound_update();
    }
}
