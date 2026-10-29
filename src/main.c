/*
 * main.c - River Raid NES port, Phase 3.
 *
 * Boot sequence:
 *   1. Put the HUD and playfield palettes in the shadow registers (backdrop
 *      black) and blank the display.
 *   2. Load CHR-RAM, draw the HUD, load the player sprite shapes and fill
 *      the playfield ring in nametable 1.
 *   3. Install the VBL/LCD handlers and enable both interrupts.
 *   4. Turn the display on and run the frame loop.
 *
 * Screen split:
 *   The HUD lives in nametable 0 ($2000); the 30-row playfield ring lives in
 *   nametable 1 ($2400).  GBDK's deferred LCD handler runs from the NMI path
 *   and switches the playfield scroll/nametable every frame, so the split
 *   cannot be delayed by main-loop work (river row streaming).  The visible
 *   split lands on scanline 36/37.
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

/* Read by tests through roms/river_raid.noi. */
volatile uint16_t frame_counter;
volatile uint16_t vbl_events;
volatile uint16_t split_events;

static void vbl_handler(void) NO_OVERLAY_LOCALS;
static void lcd_handler(void) NO_OVERLAY_LOCALS;

static void vbl_handler(void) {
    /* The NMI applies this entry during VBlank.  Ask for the faked LCD
     * entry at HUD_SPLIT_LYC; the handler installs the playfield scroll. */
    move_bkg(0, 0);
    shadow_PPUCTRL = (uint8_t)((shadow_PPUCTRL & 0xFCu) | 0x00u);
    LYC_REG = HUD_SPLIT_LYC;
    vbl_events++;
}

static void lcd_handler(void) {
    /* Playfield pass: nametable 1 and the absolute fine scroll.  The
     * deferred entry is built from HUD_SPLIT_LYC, so add the difference to
     * keep the requested scroll (GBDK's PPUADDR sets v). */
    move_bkg(0, (uint8_t)(playfield_scroll_y +
                          (HUD_SPLIT_SCANLINE - HUD_SPLIT_LYC)));
    shadow_PPUCTRL = (uint8_t)((shadow_PPUCTRL & 0xFCu) | PLAYFIELD_NT_BITS);
    LYC_REG = 0;
    split_events++;
}

void main(void) {
    /* Set the backdrop before blanking so DISPLAY_OFF leaves black, not
     * the crt0 default white. */
    hud_init_palette();
    DISPLAY_OFF;

    hud_init_graphics();
    player_init_graphics();
    river_init();

    player_init();
    entities_init();
    collision_init();
    sound_init();

    add_VBL(vbl_handler);
    add_LCD(lcd_handler);
    set_interrupts(VBL_IFLAG | LCD_IFLAG);

    DISPLAY_ON;
    SHOW_SPRITES;

    for (;;) {
        vsync();
        frame_counter++;
        hud_update();
        river_update();

        /* Phase 4-6 update hooks; each is a no-op stub for now. */
        player_update();
        entities_update();
        collision_update();
        sound_update();
    }
}
