/*
 * entities.c - Phase 5 placeholder.
 *
 * TODO(Phase 5): deterministic spawn cadence from the Atari tables,
 * bridge checker, helicopter/ship patrol ranges, jet crossings, fuel
 * depots and explosion state.  Sprites are hidden above scanline 36.
 */
#include <stdint.h>

#include "entities.h"

entity_t entities[MAX_ENTITIES];
uint8_t entity_count;

void entities_init(void) {
    uint8_t i;

    for (i = 0; i < MAX_ENTITIES; i++) {
        entities[i].type = ENTITY_NONE;
        entities[i].x = 0;
        entities[i].y = 0;
        entities[i].flags = 0;
        entities[i].aux = 0;
    }
    entity_count = 0;
}

void entities_update(void) {
    /* Intentionally empty in Phase 2. */
}
