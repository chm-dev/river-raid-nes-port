/*
 * collision.h - deterministic hit tests (Phase 5 scaffolding).
 *
 * Phase 2 only provides the shared AABB helper; terrain collision will
 * use river_left_x/right_x/island bounds once the generator exists.
 */
#ifndef COLLISION_H
#define COLLISION_H

#include <stdint.h>

typedef struct {
    uint8_t x;
    uint8_t y;
    uint8_t w;
    uint8_t h;
} hitbox_t;

void collision_init(void);

/* Phase 5: player/shot vs terrain and vs entity slots. */
void collision_update(void);

uint8_t collision_aabb(const hitbox_t *a, const hitbox_t *b);

#endif /* COLLISION_H */
