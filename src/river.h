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

/* Pattern scratch of the pending tile row.  Before river_init() the HUD,
 * player and entity code borrow it to expand their CHR tiles (boot-time only;
 * the river owns it from river_init() on). */
extern uint8_t river_tile_pat[RIVER_TILE_COLS * 16];

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

/* Generator state read by the object spawner (Atari zero-page names in
 * river.c) and the number of scrolled rows the generator still owes. */
extern uint8_t rg_left, rg_phase, rg_rowcount, rg_island_flags, rg_countdown;
extern uint8_t rg_island_width, rg_margin, rg_margin_flag, rg_level;
extern uint8_t rg_section_timer, rg_flags23, rg_lfsr_a, rg_lfsr_b, rg_lfsr_c, rg_b5;
extern uint8_t river_owed;

/* Number of tile rows generated so far (also the demo scroll distance). */
extern uint16_t river_distance;

/* Test/demo hook: when non-zero, river_update() leaves the fine scroll
 * untouched so a test can drive playfield_scroll_y directly. */
extern uint8_t river_freeze;

/* Throttle set by the player: 0 = slow (0.5 px/frame), 1 = cruise (1 px/frame),
 * 2 = fast (2 rows per frame). */
extern uint8_t river_speed;

/* Actual scroll velocity in 1/32 row per frame.  It eases toward the
 * throttle's target at RIVER_ACCEL per frame; reset it to cruise on restart. */
extern uint8_t river_gen_budget;
#define RIVER_VEL_SLOW      16u
#define RIVER_VEL_CRUISE    32u
#define RIVER_VEL_FAST      64u
#define RIVER_ACCEL         1u
extern uint8_t river_vel;

/* Pixels (raw rows) the playfield advanced in the last river_update(): 0..2. */
extern uint8_t river_scrolled;

/* Rows (pixels) the last river_update() alone scrolled: 0..6. */
extern uint8_t river_rows_last;

/* Frames elapsed since the previous main-loop pass (1..4), set in main().  All
 * game logic scales with this so a slow pass never slows the game down. */
extern uint8_t game_ticks;

/* Collision-ring slot (== nametable row) under a playfield scanline
 * (36..239). */
uint8_t river_slot_at_scanline(uint8_t scanline);

/* Start the river for a new game at start level variant (0..3 = bridge
 * 1/5/20/50, a row of atari_level_descriptors); returns the start bridge. */
uint8_t river_init(uint8_t variant);
extern uint8_t river_variant;

/* 2P: swap rg_level + the 4 checkpoint seeds with blk[0..4]. */
void river_swap_checkpoint(uint8_t *blk);

/* The jet was lost: restart the river at the last cleared bridge (the
 * generator checkpoint, see river.c).  bridges is the HUD bridge counter. */
void river_restart(uint8_t bridges);

/* Schedule a 16-scanline road (grey with a yellow centre line) over the land
 * on the next whole pair of tile rows. */
void river_road_begin(void);

/* Scanline of the scheduled road's top edge once its first tile row has been
 * written, else 0xFF.  Values 232..249 mean it is still above the hidden rows
 * (wrapped to the bottom of the window): try again later. */
uint8_t river_road_top(void);

/* Advance the playfield by one pixel and stream the next hidden row when
 * the fine scroll crosses a tile boundary. */
void river_update(void);

/* Load the solid land/water tiles and reset the exact-pattern cache. */
void river_init_tiles(void);

#endif /* RIVER_H */
