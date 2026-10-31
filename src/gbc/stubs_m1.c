/*
 * stubs_m1.c - stand-ins for modules that are not in the milestone 1 link.
 * Drop each stub from the build as its real module is linked.
 */
#include <gb/gb.h>

#include "sound.h"

/* The HUD state and hud_update() now live in gbc/hud_gbc.c. */

/* Sound is rewritten for the GB sound chip in milestone 3; the NES APU engine
 * in sound.c writes NES registers and must not be linked. */
void sound_init(void) {}
void sound_update(void) {}
void sound_set_engine(uint8_t speed) { (void)speed; }
void sound_play_shot(void) {}
void sound_play_pickup(void) {}
void sound_play_explosion(void) {}
void sound_play_crash(void) {}
void sound_play_shell(void) {}
void sound_play_missile(void) {}
void sound_play_bonus(void) {}
void sound_play_blip(void) {}
