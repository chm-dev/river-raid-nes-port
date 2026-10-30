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
 *   split lands on scanline 30/31.
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

#ifndef LOGIC_FRAMES
#define LOGIC_FRAMES 2u             /* HUD/player/entities every n-th frame: 2 = 30 Hz, 1 = 60 Hz */
#endif

/* Read by tests through roms/river_raid.noi. */
volatile uint16_t frame_counter;
volatile uint16_t vbl_events;
volatile uint16_t split_events;
uint8_t game_ticks = 1u;
static uint8_t main_last;
static uint8_t logic_acc;       /* frames since the last logic pass */

void hud_set_playfield_scroll(uint8_t y) {
    playfield_scroll_y = y;
}

static void vbl_handler(void) NO_OVERLAY_LOCALS;
static void lcd_handler(void) NO_OVERLAY_LOCALS;

static void vbl_handler(void) {
    /* The NMI applies this entry during VBlank.  Ask for the faked LCD
     * entry at HUD_SPLIT_LYC; the handler installs the playfield scroll.  The
     * HUD pass scrolls nametable 0 up by HUD_SCROLL_Y to drop its blank
     * padding row 0, so the three content rows start at scanline 0. */
    move_bkg(0, HUD_SCROLL_Y);
    /* HUD pass: nametable 0 and the HUD/sprite pattern table ($1000). */
    shadow_PPUCTRL = (uint8_t)((shadow_PPUCTRL & 0xECu) | BG_TABLE_BIT);
    LYC_REG = HUD_SPLIT_LYC;
    vbl_events++;
}

static void lcd_handler(void) {
    /* Playfield pass: nametable 1 and the fine scroll.  GBDK's deferred ISR
     * adds the current scanline to bkg_scroll_y when it writes the entry
     * (see write_shadow_registers_to_buffer), so subtract the split line
     * here.  Without this the playfield is 30 px ahead of the ring: the
     * newest row lands at the bottom instead of entering at the top. */
    uint8_t sy = playfield_scroll_y;

    sy = (sy >= HUD_SPLIT_SCANLINE)
        ? (uint8_t)(sy - HUD_SPLIT_SCANLINE)
        : (uint8_t)(sy + (240u - HUD_SPLIT_SCANLINE));
    move_bkg(0, sy);
    /* Playfield pass: nametable 1 and the playfield's own pattern table
     * ($0000).  Main-thread set_bkg_data() therefore targets the river
     * cache whenever this handler was the last to run. */
    shadow_PPUCTRL = (uint8_t)((shadow_PPUCTRL & 0xECu) | PLAYFIELD_NT_BITS);
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
    entities_init_graphics();
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

    main_last = (uint8_t)sys_time;
    for (;;) {
        /* Wait for a new frame, but only if none passed during the last
         * pass: an overrun pass starts the next one at once (catch-up). */
        vsync();
        game_ticks = (uint8_t)((uint8_t)sys_time - main_last);
        main_last = (uint8_t)sys_time;
        if (game_ticks > 4u) {
            game_ticks = 4u;
        }
        frame_counter++;

        /* The river (scroll, row generation, tile uploads) runs every frame;
         * everything else runs once per LOGIC_FRAMES frames, in two groups on
         * alternate frames so their costs never land together:
         *   - player (jet, shot, terrain and entity hit tests), plus a cheap
         *     entities_follow() that moves the entity sprites with the river,
         *     since sprites redrawn only every other frame would lag it and
         *     snap back (jitter);
         *   - HUD and entities (full update and redraw; entities_update()
         *     measures its own elapsed time and consumes river_scrolled).
         * The APU runs every frame: the Atari's sound engine is driven from the
         * 60 Hz VBI and the shot's 15-step pitch sweep needs one step per
         * frame to sound like the original. */
        river_update();
        sound_update();

        logic_acc = (uint8_t)(logic_acc + game_ticks);
        if (logic_acc >= LOGIC_FRAMES) {
            game_ticks = logic_acc;         /* at most 1 + 4 */
            logic_acc = 0;
            entities_follow();
            player_update();
            collision_update();
        } else {
            hud_update();
            entities_update();
        }
    }
}