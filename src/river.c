/*
 * river.c - Phase 4 placeholder.
 *
 * TODO(Phase 4): port the exact Atari LFSR ($B183), bank patterns
 * ($B800/$B810), widths ($BBB2), feature tables ($BBCC/$BBD4/$BBEB) and
 * template stream ($BBF6/$BC00), then stream one 32-byte row per 8 pixels
 * of scroll into the nametable ring.
 */
#include <stdint.h>

#include "config.h"
#include "river.h"

uint8_t river_left_x[RIVER_ROWS];
uint8_t river_right_x[RIVER_ROWS];
uint8_t river_flags[RIVER_ROWS];

uint8_t river_scroll_y;
uint16_t river_distance;

void river_init(void) {
    uint8_t i;

    for (i = 0; i < RIVER_ROWS; i++) {
        river_left_x[i] = 3;
        river_right_x[i] = 28;
        river_flags[i] = 0;
    }
    river_scroll_y = 0;
    river_distance = 0;
}

void river_update(void) {
    /* Intentionally empty in Phase 2.  See the TODO above. */
}
