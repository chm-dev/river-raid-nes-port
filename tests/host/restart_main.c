/* Host harness for river_restart() (the $B1C0 checkpoint choice).  Includes
 * river.c to reach its private generator state.  Reads lines
 *   level f23 c4c e2
 * (the inputs of tools/atari_checkpoint_trace.py) from stdin, loads the same
 * sentinels the trace poked, runs river_restart() and prints
 *   level seeds[4] prev[4] f23
 * for tests/host/compare_restart.py.  See tests/host_restart.ps1. */
#include <stdio.h>
#include <string.h>
#include "nes/nes.h"
#include "river.c"

volatile uint8_t playfield_scroll_y;
uint8_t game_ticks = 1;
void entities_row(void){}
void hud_set_playfield_scroll(uint8_t y){ playfield_scroll_y=y; }
void set_bkg_data(uint8_t f,uint8_t n,const uint8_t*d){(void)f;(void)n;(void)d;}
void set_vram_byte(uint8_t*a,uint8_t v){(void)a;(void)v;}

int main(void) {
    unsigned level, f23, c4c, e2;

    while (scanf("%u %u %u %u", &level, &f23, &c4c, &e2) == 4) {
        const uint8_t life_bridges = 3;      /* bridge counter when the life started */

        river_init(0);
        rg_level = (uint8_t)level;
        rg_flags23 = (uint8_t)f23;
        /* $4C bit 2 is the level parity at life start; rg_life keeps it in bit 7. */
        rg_life = (uint8_t)(((c4c & 0x04u) ? 0x80u : 0u) | life_bridges);
        rg_seeds[0] = 0x11; rg_seeds[1] = 0x12; rg_seeds[2] = 0x13; rg_seeds[3] = 0x14;
        rg_prev[0] = 0x21; rg_prev[1] = 0x22; rg_prev[2] = 0x23; rg_prev[3] = 0x24;
        rg_lfsr_a = 0x31; rg_lfsr_b = 0x32; rg_lfsr_c = 0x33; rg_left = 0x34;

        /* $2E != 0 <=> a bridge was destroyed since the life started. */
        river_restart((uint8_t)(life_bridges + (e2 ? 1 : 0)));

        printf("%u %u %u %u %u %u %u %u %u %u\n", rg_level,
               rg_seeds[0], rg_seeds[1], rg_seeds[2], rg_seeds[3],
               rg_prev[0], rg_prev[1], rg_prev[2], rg_prev[3], rg_flags23);
    }
    return 0;
}
