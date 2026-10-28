/*
 * river.h - procedural river geometry (Phase 4 scaffolding).
 *
 * The Atari generator writes a 160-row ring; the NES viewport only needs
 * the 30 rows that can be visible in the default single-screen nametable.
 * For now the arrays hold a straight default channel and are not streamed.
 */
#ifndef RIVER_H
#define RIVER_H

#include <stdint.h>

#define RIVER_ROWS              30u
#define RIVER_COLS              32u

#define RIVER_FLAG_BRIDGE       0x01u
#define RIVER_FLAG_ISLAND       0x02u

/* Per-row water bounds, in background tile columns (0..31). */
extern uint8_t river_left_x[RIVER_ROWS];
extern uint8_t river_right_x[RIVER_ROWS];
extern uint8_t river_flags[RIVER_ROWS];

/* Absolute scroll of the playfield, in pixels (0..239). */
extern uint8_t river_scroll_y;
extern uint16_t river_distance;

void river_init(void);

/* Phase 4: generate/stream the next row and advance the collision ring. */
void river_update(void);

#endif /* RIVER_H */
