/*
 * player.c - Phase 5 placeholder.
 *
 * TODO(Phase 5): joypad steering, banking animation, throttle speed,
 * 14-row flight shapes atari_player_shape[shape*14], shot motion, fuel
 * depletion/refuelling and respawn at the last cleared bridge.
 */
#include <stdint.h>

#include "player.h"

player_state_t player;

void player_init(void) {
    player.x = PLAYER_START_X;
    player.y = PLAYER_START_Y;
    player.speed = 1;
    player.bank = PLAYER_BANK_NONE;
    player.shot_x = 0;
    player.shot_y = 0;
    player.shot_active = 0;
    player.alive = 1;
    player.fuel_hi = 0xFF;
    player.fuel_lo = 0xFF;
}

void player_update(void) {
    /* Intentionally empty in Phase 2. */
}
