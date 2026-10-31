/*
 * stubs_m1.c - stand-ins for modules that are not in the milestone 1 link.
 * Drop each stub from the build as its real module is linked.
 */
#include <gb/gb.h>

#include "hud.h"
#include "sound.h"

/* HUD state, read and written by player.c and entities.c.  The HUD itself is
 * rewritten for GBC in milestone 2/3. */
uint8_t hud_score_bcd[3];
uint8_t hud_hi_bcd[3];
uint8_t hud_fuel;
uint8_t hud_lives;
uint8_t hud_game;
uint8_t hud_bridge;
uint8_t hud_two;
uint8_t hud_cur;
uint8_t hud_blink;
static uint8_t stub_other_bcd[3];
uint8_t *hud_other_bcd = stub_other_bcd;

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
