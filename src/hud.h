/*
 * hud.h - boot screen, text helpers and the fixed top-HUD split.
 *
 * The HUD occupies nametable rows 0..2 (scanlines 0..23).  The playfield
 * starts at scanline 24 and uses the same 32x30 nametable for now.  The
 * real scrolling ring buffer is introduced in Phase 4; `playfield_scroll_y`
 * is already the interface the LCD handler uses.
 */
#ifndef HUD_H
#define HUD_H

#include <stdint.h>

/* True vertical scroll of the playfield, in pixels (0..239). */
extern volatile uint8_t playfield_scroll_y;

/* Install the shared 4-colour background palette in the PPU shadow.
 * Call this before DISPLAY_OFF so the blank screen uses the black backdrop. */
void hud_init_palette(void);

/* Load the Atari charset + boot-screen tiles and draw the boot screen.
 * Must be called while the display is off (direct VRAM mode). */
void hud_init_graphics(void);

/* Increment the 4-digit BCD frame counter and queue its tiles. */
void hud_update_frame(void);

/* Phase 3+ interface: set the playfield's absolute vertical scroll. */
void hud_set_playfield_scroll(uint8_t y);

#endif /* HUD_H */
