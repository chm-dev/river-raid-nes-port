/*
 * hud.h - stationary top HUD and its shared state.
 *
 * The HUD occupies nametable rows 0..3 (scanlines 0..31, row 0 padding)
 * and the split leaves four more gray scanlines below; the playfield
 * scrolls from scanline 36.  It mirrors the Atari original's three content
 * rows: P1/score/HI/hi-score, then GAME/fuel gauge/BRIDGE/bridge number,
 * then the game number, gauge body and remaining-lives plane icons.  The
 * playfield is drawn from the second physical nametable ($2400).  Only the
 * dynamic fields are rewritten after boot, and only when changed.
 */
#ifndef HUD_H
#define HUD_H

#include <nes/nes.h>
#include <stdint.h>

/* True vertical scroll of the playfield, in pixels (0..239). */
extern volatile uint8_t playfield_scroll_y;

/* HUD state.  Tests and later phases write these directly; hud_update()
 * notices the change and queues only the affected tiles. */
extern uint8_t hud_score_bcd[3];    /* big-endian BCD, 6 digits */
extern uint8_t hud_hi_bcd[3];
extern uint8_t hud_fuel;            /* 0..255, shown as a reduced fraction */
extern uint8_t hud_lives;           /* plane icons under BRIDGE (0..3) */
extern uint8_t hud_game;            /* game variation / skill */
extern uint8_t hud_bridge;          /* current bridge number (0..99) */

/* Two-player scoreboard, as on the Atari ($AF80/$AF4B): the left field is
 * always P1's score; the right field is "HI" + hi-score in a one-player game
 * and "P2" + P2's score in a two-player game.  hud_score_bcd is the active
 * player's score, hud_other_bcd (set by the player code) the idle player's. */
extern uint8_t hud_two;             /* 1 = two players alternating */
extern uint8_t hud_cur;             /* active player, 0 = P1 */
extern uint8_t *hud_other_bcd;      /* idle player's score, 3 BCD bytes */

/* Install the HUD and playfield background palettes in the PPU shadow.
 * Call this before DISPLAY_OFF so the blank screen uses the black backdrop. */
void hud_init_palette(void) BANKED;

/* Load the charset, river tile set and HUD tiles; draw the static HUD text
 * and the initial dynamic values.  Must be called while the display is off. */
void hud_init_graphics(void) BANKED;

/* Queue only the HUD tiles whose state changed since the last call. */
void hud_update(void) BANKED;

/* Phase 3+ interface: set the playfield's absolute vertical scroll.  Defined
 * in main.c (fixed bank): the rest of the HUD lives in switchable bank 1 and
 * river_update() sets the scroll every frame. */
void hud_set_playfield_scroll(uint8_t y);

#endif /* HUD_H */
