/* Host harness for the object spawner in src/entities.c.  Reads one generator
 * state per line (the Atari's zero page at $AB80, from a live trace) and prints
 * what entities_row() does with it.  tools/object_host_check.py compares that
 * with tools/object_model.py.  It #includes entities.c so the statics
 * (sp_hold) can be driven. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "nes/nes.h"
volatile uint16_t sys_time;     /* GBDK's frame counter, read by entities_update() */
#include "../../src/entities.c"

volatile uint8_t playfield_scroll_y;
uint8_t game_ticks = 1;
volatile OAM_item_t shadow_OAM[64];
uint8_t hud_score_bcd[3], hud_hi_bcd[3], hud_bridge, player_disable;
void hud_set_playfield_scroll(uint8_t y) { playfield_scroll_y = y; }
void set_bkg_data(uint8_t f, uint8_t n, const uint8_t *d) { (void)f; (void)n; (void)d; }
void set_vram_byte(uint8_t *a, uint8_t v) { (void)a; (void)v; }
void set_sprite_palette(uint8_t f, uint8_t n, const palette_color_t *p) { (void)f; (void)n; (void)p; }
void set_sprite_data(uint8_t f, uint8_t n, const uint8_t *d) { (void)f; (void)n; (void)d; }
void set_bkg_palette_entry(uint8_t p, uint8_t e, palette_color_t c) { (void)p; (void)e; (void)c; }

int main(void) {
    unsigned v[19];

    while (scanf("%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u",
                 &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9],
                 &v[10], &v[11], &v[12], &v[13], &v[14], &v[15], &v[16], &v[17], &v[18]) == 19) {
        rg_rowcount = v[0]; rg_level = v[1]; rg_countdown = v[2]; rg_section_timer = v[3];
        rg_island_width = v[4]; rg_margin = v[5]; rg_island_flags = v[6];
        rg_margin_flag = v[7]; rg_flags23 = v[8]; rg_phase = v[9]; rg_left = v[10];
        rg_b5 = v[11]; rg_lfsr_a = v[12]; rg_lfsr_b = v[13]; rg_lfsr_c = v[14];
        sp_hold = v[15];
        river_obj_count = v[16] ? 0 : 0x0B;     /* $4E < 11 */
        river_owed = 0; river_scrolled = 0;
        river_obj_aux[0] = 0xEE; river_obj_x[0] = 0;
        entities_clear();
        sp_hold = v[15];
        river_obj_count = v[16] ? 0 : 0x0B;
        (void)v[17]; (void)v[18];
        entities_row();
        printf("%u %u %u %u %u\n", river_obj_count != 0x0B, sp_hold, river_obj_aux[0], river_obj_x[0],
               entity_count ? (ent_flags[0] & ENT_F_LEFT) != 0 : 0);
    }
    return 0;
}
