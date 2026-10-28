/*
 * sound.h - 2A03 APU engine/SFX interface (Phase 6 scaffolding).
 *
 * Register writes will be driven from the 60 Hz frame tick.  The TIM
 * handler is deliberately not installed yet; Phase 2 uses VBL/LCD only.
 */
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

void sound_init(void);

/* Phase 6: advance envelopes and the engine-hum oscillator. */
void sound_update(void);

void sound_set_engine(uint8_t speed);
void sound_play_shot(void);
void sound_play_explosion(void);
void sound_play_refuel(void);

#endif /* SOUND_H */
