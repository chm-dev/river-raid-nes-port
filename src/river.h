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

/* The whole 48-byte Atari row is shown: 24 tiles of river area (nametable
 * columns 4..27) between 4 land columns on each side.  Generator banks can
 * reach bytes 3..46 at high levels, outside the 40-byte window of the
 * original TV picture. */
#define RIVER_TILE_COLS         24u
#define RIVER_LEFT_COL          4u

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

/* Object-avoidance inputs for the generator's bank clamp.  One slot (the
 * newest spawn): river_obj_count = 0 while it is within the generator's
 * reach, 0x0B (none) otherwise.  x is the Atari HPOS (= NES screen X). */
extern uint8_t river_obj_count;
extern uint8_t river_obj_aux[1];
extern uint8_t river_obj_x[1];
extern uint8_t river_obj_flags[1];
extern uint8_t river_obj_v37;

/* Number of tile rows generated so far (also the demo scroll distance). */
extern uint16_t river_distance;

/* Test/demo hook: when non-zero, river_update() leaves the fine scroll
 * untouched so a test can drive playfield_scroll_y directly. */
extern uint8_t river_freeze;

/* Throttle set by the player: 0 = slow (0.5 px/frame), 1 = cruise (0.75),
 * 2 = fast (1 px/frame, the generator's CPU ceiling). */
extern uint8_t river_speed;

/* 1 when the playfield advanced a pixel (raw row) in the last river_update(). */
extern uint8_t river_scrolled;

/* Collision-ring slot (== nametable row) under a playfield scanline
 * (36..239). */
uint8_t river_slot_at_scanline(uint8_t scanline);

void river_init(void);

/* Advance the playfield by one pixel and stream the next hidden row when
 * the fine scroll crosses a tile boundary. */
void river_update(void);

/* Load the solid land/water tiles and reset the exact-pattern cache. */
void river_init_tiles(void);

#endif /* RIVER_H */
