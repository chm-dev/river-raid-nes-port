/*
 * bench2_main.c - feasibility benchmark: the real river and entity code (C paths,
 * no 6502 asm) in the main.c pass order at fast speed (1.5 rows/frame), no VRAM
 * writes.  Results in M-cycles: DMG 17556 per frame, CGB double speed 35112.
 */
#include <gb/gb.h>
#include <stdint.h>

#include "config.h"
#include "river.h"
#include "entities.h"
#include "player.h"
#include "hud.h"

#define BENCH_PASSES 800u

/* The symbols the linked modules need from the ones that are not linked. */
uint8_t game_ticks = 1u;
volatile uint8_t playfield_scroll_y;
void hud_set_playfield_scroll(uint8_t y) { playfield_scroll_y = y; }
uint8_t hud_score_bcd[3], hud_hi_bcd[3], hud_lives, hud_bridge;
player_state_t player;
uint8_t player_state, player_disable, player_invuln;
void sound_play_shell(void) { }
void sound_play_missile(void) { }
void sound_play_bonus(void) { }

/* The timer: one tick every 16 M-cycles. */
static volatile uint8_t tim_hi;
static void tim_isr(void) { tim_hi++; }

static uint16_t ticks(void) {
    uint8_t h, l;
    do {
        h = tim_hi;
        l = TIMA_REG;
    } while (h != tim_hi);
    return ((uint16_t)h << 8) | l;
}

/* Per start level (1/5/20/50): pass A = river + follow + player hit test,
 * pass B = river + entities_update (the HUD is not linked). */
uint16_t bench_a_avg[4], bench_a_max[4], bench_a_over[4];
uint16_t bench_b_avg[4], bench_b_max[4], bench_b_over[4];
uint8_t bench_ents_max[4];
uint8_t bench_done;

static uint16_t b_t0, b_dt, b_i;
static uint8_t b_v;
static uint32_t b_sa, b_sb;

void main(void) {
    DISPLAY_OFF;
    add_TIM(tim_isr);
    TMA_REG = 0u;
    TIMA_REG = 0u;
    TAC_REG = 0x06u;
    set_interrupts(TIM_IFLAG);

    for (b_v = 0; b_v < 4u; b_v++) {
        river_init(b_v);
        entities_init();
        ent_hold = 0u;
        river_speed = 2u;
        player_state = 0u;
        player.x = 120u;
        player.y = 218u;
        b_sa = 0u;
        b_sb = 0u;
        for (b_i = 0; b_i < BENCH_PASSES; b_i++) {
            sys_time++;
            game_ticks = 1u;
            b_t0 = ticks();
            river_gen_budget = 3u;
            river_update();
            entities_follow();
            entities_check_player(120u, 218u);
            b_dt = (uint16_t)(ticks() - b_t0);
            b_sa += b_dt;
            if (b_dt > bench_a_max[b_v]) bench_a_max[b_v] = b_dt;
            if (b_dt >= (17556u >> 4)) bench_a_over[b_v]++;

            sys_time++;
            game_ticks = 1u;
            b_t0 = ticks();
            river_gen_budget = 1u;
            river_update();
            entities_update();
            b_dt = (uint16_t)(ticks() - b_t0);
            b_sb += b_dt;
            if (b_dt > bench_b_max[b_v]) bench_b_max[b_v] = b_dt;
            if (b_dt >= (17556u >> 4)) bench_b_over[b_v]++;
            if (entity_count > bench_ents_max[b_v]) bench_ents_max[b_v] = entity_count;
        }
        bench_a_avg[b_v] = (uint16_t)((b_sa << 4) / BENCH_PASSES);
        bench_b_avg[b_v] = (uint16_t)((b_sb << 4) / BENCH_PASSES);
        bench_a_max[b_v] <<= 4;
        bench_b_max[b_v] <<= 4;
    }
    bench_done = 1u;
    for (;;) { }
}
