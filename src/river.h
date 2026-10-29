/*
 * river.h - procedural river geometry and the Phase 3 scrolling demo.
 *
 * The playfield is a full 30-row (240-pixel) ring in nametable 2 ($2400).
 * The visible viewport is 26 rows tall, leaving four hidden rows for the
 * next tile rows.  river_update() advances the fine scroll one pixel per
 * frame and writes one new 32-tile row every eight pixels.
 */
#ifndef RIVER_H
#define RIVER_H

#include <stdint.h>

#define RIVER_ROWS              30u
#define RIVER_COLS              32u

#define RIVER_FLAG_BRIDGE       0x01u
#define RIVER_FLAG_ISLAND       0x02u

/* Per-row water bounds, in background tile columns (0..31).  Phase 4 will
 * fill these while streaming; Phase 3 keeps them at the default channel. */
extern uint8_t river_left_x[RIVER_ROWS];
extern uint8_t river_right_x[RIVER_ROWS];
extern uint8_t river_flags[RIVER_ROWS];

/* Number of tile rows generated so far (also the demo scroll distance). */
extern uint16_t river_distance;

/* Test/demo hook: when non-zero, river_update() leaves the fine scroll
 * untouched so a test can drive playfield_scroll_y directly. */
extern uint8_t river_freeze;

void river_init(void);

/* Advance the playfield by one pixel and stream the next hidden row when
 * the fine scroll crosses a tile boundary. */
void river_update(void);

/* Phase 4 hook: replace the demo row source with the Atari LFSR/template
 * generator.  The caller supplies one 20-tile row from the visible window. */
void river_stream_demo_row(uint8_t slot, uint8_t demo_row);

#endif /* RIVER_H */
