/*
 * sound.c - Phase 6 placeholder.
 *
 * TODO(Phase 6): pulse 1 shot/fuel warning, pulse 2 cues, triangle engine
 * hum pitched by speed, noise explosions.  Keep all register writes in a
 * frame-tick update so the music/SFX cadence is stable.
 */
#include <stdint.h>

#include "sound.h"

static uint8_t engine_speed;

void sound_init(void) {
    engine_speed = 0;
}

void sound_update(void) {
    /* Intentionally empty in Phase 2. */
}

void sound_set_engine(uint8_t speed) {
    engine_speed = speed;
}

void sound_play_shot(void) {
    /* TODO(Phase 6) */
}

void sound_play_explosion(void) {
    /* TODO(Phase 6) */
}

void sound_play_refuel(void) {
    /* TODO(Phase 6) */
}
