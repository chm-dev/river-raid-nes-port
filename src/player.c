/*
 * player.c - Phase 5 placeholder plus the Phase 3 sprite-CHR load.
 *
 * TODO(Phase 5): joypad steering, banking animation, throttle speed,
 * 14-row flight shapes atari_player_shape[shape*14], shot motion, fuel
 * depletion/refuelling and respawn at the last cleared bridge.
 */
#include <nes/nes.h>
#include <stdint.h>

#include "config.h"
#include "atari_data.h"
#include "player.h"

player_state_t player;

static const palette_color_t player_palette[4] = {
    0x0Fu, 0x28u, 0x30u, 0x16u
};

/* Two 8x8 sprite tiles in GBDK's interleaved 2bpp format; the 14-row
 * Atari shapes become two stacked tiles. */
static uint8_t player_tile_buf[32];

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

static void player_load_shape(uint8_t first_tile, uint8_t shape) {
    const uint8_t *src = atari_player_shape + (uint16_t)shape * ATARI_PLAYER_SHAPE_LEN;
    uint8_t i;

    /* 1bpp source -> 2bpp with the set pixels in plane 0 (sprite colour 1).
     * The library build does not ship set_sprite_1bpp_data(), so expand it
     * here rather than relying on the missing symbol. */
    for (i = 0; i < 8; i++) {
        player_tile_buf[i * 2] = src[i];
        player_tile_buf[i * 2 + 1] = 0x00u;
        player_tile_buf[16 + i * 2] = (i < 6) ? src[8 + i] : 0x00u;
        player_tile_buf[16 + i * 2 + 1] = 0x00u;
    }
    set_sprite_data(first_tile, 2, player_tile_buf);
}

void player_init_graphics(void) {
    set_sprite_palette(0, 1, player_palette);

    /* Tile pairs: 0/1 bank-left, 2/3 bank-right, 4/5 straight. */
    player_load_shape(SPRITE_TILE_BASE + 0u, 7);
    player_load_shape(SPRITE_TILE_BASE + 2u, 8);
    player_load_shape(SPRITE_TILE_BASE + 4u, 9);

    /* Phase 3 keeps OAM hidden; Phase 5 owns the metasprite instances. */
}

void player_update(void) {
    /* Intentionally empty until Phase 5. */
}
