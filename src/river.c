/*
 * river.c - Phase 3 scrolling playfield demo.
 *
 * The playfield ring is a full 30-row nametable at $2400.  At fine scroll 0
 * the viewport shows rows 3..29; each 8-pixel advance rotates the ring by
 * one row.  The row that just left the top is hidden and can be rewritten
 * with the next river row before it re-enters at the bottom 24 frames later.
 *
 * The Phase 3 row source is the captured Atari river fixture, packed into
 * 8x8 NES tiles by tools/gen_river_demo.py.  Phase 4 replaces only
 * river_stream_demo_row() with the procedural generator.
 */
#include <nes/nes.h>
#include <stdint.h>

#include "config.h"
#include "hud.h"
#include "river.h"
#include "river_demo_data.h"

#if TILE_RIVER_COUNT != RIVER_DEMO_TILE_COUNT
#error "config.h TILE_RIVER_COUNT does not match river_demo_data.h"
#endif

uint8_t river_left_x[RIVER_ROWS];
uint8_t river_right_x[RIVER_ROWS];
uint8_t river_flags[RIVER_ROWS];

uint16_t river_distance;
uint8_t river_freeze;

/* One 32-byte nametable row: six land tiles, the 20-tile river window,
 * then six more land tiles.  Static so the main loop never uses the stack
 * for this buffer. */
static uint8_t river_row_buf[RIVER_COLS];

/* Tile-row boundary counter.  Starts at 0; the first streamed row is
 * written when the fine scroll reaches 8. */
static uint16_t river_step;

static void river_vram_byte(uint16_t addr, uint8_t value) {
    set_vram_byte((uint8_t *)addr, value);
}

static void river_write_slot(uint8_t slot, uint8_t demo_row) {
    uint8_t i;
    uint8_t land = (uint8_t)(TILE_RIVER_BASE + RIVER_DEMO_TILE_LAND);
    uint16_t addr = (uint16_t)(PPU_NT1_BASE + ((uint16_t)slot << 5));

    for (i = 0; i < 6; i++) {
        river_row_buf[i] = land;
    }
    for (i = 0; i < RIVER_DEMO_TILES_W; i++) {
        river_row_buf[6u + i] =
            (uint8_t)(TILE_RIVER_BASE +
                      river_demo_row_tiles[(uint16_t)demo_row * RIVER_DEMO_TILES_W + i]);
    }
    for (i = 0; i < 6; i++) {
        river_row_buf[26u + i] = land;
    }

    for (i = 0; i < RIVER_COLS; i++) {
        river_vram_byte((uint16_t)(addr + i), river_row_buf[i]);
    }
}

void river_stream_demo_row(uint8_t slot, uint8_t demo_row) {
    river_write_slot(slot, demo_row);
}

static void river_stream_next(void) {
    uint8_t slot = (uint8_t)((2u + river_step) % RIVER_RING_ROWS);
    uint8_t demo_row =
        (uint8_t)((8u + river_step + 2u) % RIVER_DEMO_TILE_ROWS);

    river_stream_demo_row(slot, demo_row);
    river_distance = river_step;
}

void river_init(void) {
    uint8_t i;
    uint8_t slot;

    for (i = 0; i < RIVER_ROWS; i++) {
        river_left_x[i] = 3;
        river_right_x[i] = 28;
        river_flags[i] = 0;
    }

    /* Playfield attributes: palette 2 (all four 16x16 blocks in each byte). */
    for (i = 0; i < 64; i++) {
        river_vram_byte((uint16_t)(PPU_NT1_ATTR + i), 0xAAu);
    }

    /* Ring prefill.  Slot 3 is the top of the initial viewport, so the
     * fixture's tile row 0 goes there; slots 0..2 hold the rows that will
     * enter at the bottom first.  The 19-row fixture repeats to fill 30. */
    for (slot = 0; slot < RIVER_ROWS; slot++) {
        uint8_t ring_row = (uint8_t)((slot + 27u) % RIVER_RING_ROWS);
        uint8_t demo_row = (uint8_t)(ring_row % RIVER_DEMO_TILE_ROWS);
        river_write_slot(slot, demo_row);
    }

    river_step = 0;
    river_distance = 0;
    river_freeze = 0;
    hud_set_playfield_scroll(0);
}

void river_update(void) {
    uint8_t next;

    if (river_freeze) {
        return;
    }

    next = (uint8_t)(playfield_scroll_y + 1u);

    if (next >= RIVER_SCROLL_HEIGHT) {
        next = 0;
        river_step++;
        river_stream_next();
    } else if ((next & 7u) == 0u) {
        river_step++;
        river_stream_next();
    }

    hud_set_playfield_scroll(next);
}
