/*
 * river.h - procedural river geometry and the Phase 3 scrolling demo.
 *
 * The playfield is a full 30-row (240-pixel) ring in nametable 2 ($2400).
 * The visible viewport is 26 rows tall; new rows are written one row above
 * the visible top and then scroll down.  river_update() moves the fine
 * scroll one pixel down per frame and writes one new 32-tile row every
 * eight pixels, with raw row 7 at the top of each 8x8 tile (the Atari
 * stream is bottom-up relative to the display).
 */
#ifndef RIVER_H
#define RIVER_H

#include <stdint.h>

#define RIVER_ROWS              30u
#define RIVER_COLS              32u

#define RIVER_FLAG_BRIDGE       0x01u
#define RIVER_FLAG_ISLAND       0x02u
#define RIVER_FLAG_FEATURE      0x04u
#define RIVER_FLAG_FLASH        0x08u

/* Per-tile-row water bounds in source pixel X (0..191).  The Atari/PMG
 * coordinate system is retained so Phase 5 can compare the jet directly. */
extern uint8_t river_left_x[RIVER_ROWS];
extern uint8_t river_right_x[RIVER_ROWS];
extern uint8_t river_island_left_x[RIVER_ROWS];
extern uint8_t river_island_right_x[RIVER_ROWS];
extern uint8_t river_flags[RIVER_ROWS];

/* Object-avoidance inputs used by the generator's bank clamp.  Phase 5
 * fills these from the entity slots; Phase 4 leaves them empty. */
extern uint8_t river_obj_count;
extern uint8_t river_obj_aux[11];
extern uint8_t river_obj_x[11];
extern uint8_t river_obj_flags[11];
extern uint8_t river_obj_v37;

/* Number of tile rows generated so far (also the demo scroll distance). */
extern uint16_t river_distance;

/* Test/demo hook: when non-zero, river_update() leaves the fine scroll
 * untouched so a test can drive playfield_scroll_y directly. */
extern uint8_t river_freeze;

void river_init(void);

/* Advance the playfield by one pixel and stream the next hidden row when
 * the fine scroll crosses a tile boundary. */
void river_update(void);

/* Load the solid land/water tiles and reset the exact-pattern cache. */
void river_init_tiles(void);

#endif /* RIVER_H */
