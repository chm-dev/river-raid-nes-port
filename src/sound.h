/*
 * sound.h - 2A03 APU engine noise and effects.
 *
 * All timings come from the Atari's deferred VBI ($B2A5); see sound.c for the
 * captured register values and the POKEY -> 2A03 pitch conversion.
 */
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

void sound_init(void);

/* Advance the effects and the engine hiss; call once per frame. */
void sound_update(void);

/* Engine noise: 0 slow, 1 cruise, 2 fast, SOUND_ENGINE_OFF = silent. */
#define SOUND_ENGINE_OFF        3u
void sound_set_engine(uint8_t speed);

/* Fire button: the 15-frame 405 -> 196 Hz sweep ($7D = $0F). */
void sound_play_shot(void);

/* Fuel tank picked up: the 9-frame buzz ($7F = 9). */
void sound_play_pickup(void);

/* An object was destroyed by the shot: 23 frames of noise ($7C = $17). */
void sound_play_explosion(void);

/* The jet was destroyed: 55 frames of noise ($7C = $37). */
void sound_play_crash(void);

/* The bridge tank fired its shell, or the shell restarted: the Atari's $B4C0
 * chirp and the fireball's noise tail ($50 = 1..$38). */
void sound_play_shell(void);
void sound_play_missile(void);

/* Score crossed 10,000: the four-note arpeggio ($7E = $40). */
void sound_play_bonus(void);

/* Start / option blip: two frames at 492 Hz ($C6 = 2). */
void sound_play_blip(void);

#endif /* SOUND_H */
