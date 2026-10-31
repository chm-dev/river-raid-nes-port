/*
 * player.h - player jet and shot state (Phase 5 scaffolding).
 *
 * Coordinates are playfield pixels: x = 0..255, y = 0..203 (scanline 36
 * is the top of the visible playfield).  Fuel is kept as two BCD-ish bytes;
 * the exact Atari gauge rate is still an open item in archive/nes/PROGRESS.md.
 */
#ifndef PLAYER_H
#define PLAYER_H

#include <gb/gb.h>
#include <stdint.h>

#define PLAYER_START_X          120u
#ifdef __SDCC_sm83
/* GBC: the 128 px view is short ahead of the jet, so sit 4 px lower.  The
 * bottom sprite ends at 230; sprites at y >= 232 are culled. */
#define PLAYER_START_Y          222u
#else
#define PLAYER_START_Y          218u    /* ~8 scanlines above the bottom of the playfield, like the Atari */
#endif
#define PLAYER_BANK_NONE        0u

typedef struct {
    uint8_t x;
    uint8_t y;
    uint8_t speed;
    uint8_t bank;
    uint8_t shot_x;
    uint8_t shot_y;
    uint8_t shot_active;
    uint8_t alive;
    uint8_t fuel_hi;
    uint8_t fuel_lo;
} player_state_t;

extern player_state_t player;

/* Game flow state (PLAYER_STATE_* in player.c); only the attract state is public:
 * enemy shots stay silent while the demo scrolls, as on the Atari ($29 bit 7). */
#define PLAYER_STATE_ATTRACT    5u      /* no jet; the river scrolls, the menu is live */
extern uint8_t player_state;

/* Test hook: freeze the jet (hidden, no fuel burn, no collisions). */
extern uint8_t player_disable;
extern uint8_t player_invuln;

void player_init(void) BANKED;

/* Load the three 14-row flight shapes into the sprite pattern table.
 * Called while the display is off; Phase 5 adds the metasprite instances. */
void player_init_graphics(void) BANKED;

/* Phase 5: read joypad, apply throttle/banking, move the shot and fuel. */
void player_update(void) BANKED;

#endif /* PLAYER_H */
