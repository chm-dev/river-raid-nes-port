/*
 * sound.h - 2A03 APU engine noise and effects.
 *
 * All timings come from the Atari's deferred VBI ($B2A5); see sound.c for the
 * captured register values and the POKEY -> 2A03 pitch conversion.
 */
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

/* The GBC engine lives in its own ROM bank (gbc/sound_gbc.c); the NES one is
 * plain code. */
#ifdef __SDCC_sm83
#include <gb/gb.h>
#define SND_BANKED BANKED
#else
#define SND_BANKED
#endif

#ifdef __SDCC_sm83
/* GBC: 1 while the low-fuel warning note sounds; the HUD gauge turns red with it. */
extern uint8_t sound_warn_on;
#endif

void sound_init(void) SND_BANKED;

/* Advance the effects and the engine hiss; call once per frame. */
void sound_update(void) SND_BANKED;

/* Engine noise: 0 slow, 1 cruise, 2 fast, SOUND_ENGINE_OFF = silent. */
#define SOUND_ENGINE_OFF        3u
void sound_set_engine(uint8_t speed) SND_BANKED;

/* Fire button: the 15-frame 405 -> 196 Hz sweep ($7D = $0F). */
void sound_play_shot(void) SND_BANKED;

/* Fuel tank picked up: the 9-frame buzz ($7F = 9). */
void sound_play_pickup(void) SND_BANKED;

/* An object was destroyed by the shot: 23 frames of noise ($7C = $17). */
void sound_play_explosion(void) SND_BANKED;

/* The jet was destroyed: 55 frames of noise ($7C = $37). */
void sound_play_crash(void) SND_BANKED;

/* The bridge tank fired its shell, or the shell restarted: the Atari's $B4C0
 * chirp and the fireball's noise tail ($50 = 1..$38). */
void sound_play_shell(void) SND_BANKED;
void sound_play_missile(void) SND_BANKED;

/* Score crossed 10,000: the four-note arpeggio ($7E = $40). */
void sound_play_bonus(void) SND_BANKED;

/* Start / option blip: two frames at 492 Hz ($C6 = 2). */
void sound_play_blip(void) SND_BANKED;

#endif /* SOUND_H */
