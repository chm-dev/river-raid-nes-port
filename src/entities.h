/*
 * entities.h - enemy / fuel / bridge slots (Phase 5 scaffolding).
 *
 * One slot per visible horizontal band is enough for River Raid's spawn
 * model; renderers must hide any sprite whose Y enters the top HUD.
 */
#ifndef ENTITIES_H
#define ENTITIES_H

#include <stdint.h>

#define MAX_ENTITIES            8u
#define ENTITY_NONE             0u

typedef struct {
    uint8_t type;       /* atari_type_remap[] value; ENTITY_NONE = free */
    uint8_t x;
    uint8_t y;
    uint8_t flags;
    uint8_t aux;
} entity_t;

extern entity_t entities[MAX_ENTITIES];
extern uint8_t entity_count;

void entities_init(void);

/* Phase 5: spawn from atari_spawn_types[]/atari_spawn_x[] and patrol. */
void entities_update(void);

#endif /* ENTITIES_H */
