/*
 * collision.c - Phase 5 placeholder plus the shared AABB helper.
 */
#include <stdint.h>

#include "collision.h"

/*
 * Returns non-zero when two axis-aligned boxes overlap.  Edges that only
 * touch do not count as a hit (same convention as the Atari PMG tests).
 */
uint8_t collision_aabb(const hitbox_t *a, const hitbox_t *b) {
    uint16_t a_right = (uint16_t)a->x + a->w;
    uint16_t a_bottom = (uint16_t)a->y + a->h;
    uint16_t b_right = (uint16_t)b->x + b->w;
    uint16_t b_bottom = (uint16_t)b->y + b->h;

    return (uint8_t)(a->x < b_right && b->x < a_right &&
                     a->y < b_bottom && b->y < a_bottom);
}

void collision_init(void) {
    /* No persistent collision state yet. */
}

void collision_update(void) {
    /* Intentionally empty in Phase 2. */
}
