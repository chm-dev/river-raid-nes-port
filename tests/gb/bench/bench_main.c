/*
 * bench_main.c - feasibility benchmark: the unmodified river generator and tile
 * classifier (C paths, no 6502 asm) timed on the SM83.  VRAM writes are not
 * included (river_put is the host no-op).  Results are in M-cycles: DMG has
 * 17556 per frame, CGB double speed 35112.  Read bench_* through the .noi.
 */
#include <gb/gb.h>
#include <stdint.h>

#include "config.h"
#include "river.h"
#include "entities.h"

#define BENCH_ROWS 4000u

/* The symbols river.c needs from the modules that are not linked. */
uint8_t game_ticks = 1u;
uint8_t playfield_scroll_y;
uint8_t entity_count;
void hud_set_playfield_scroll(uint8_t y) { playfield_scroll_y = y; }
void entities_row(void) BANKED { }

/* The timer runs at CPU/16 clocks... TAC=6: one tick every 16 M-cycles. */
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

/* One entry per start level (1/5/20/50). */
uint32_t bench_total[4];        /* M-cycles for BENCH_ROWS raw rows */
uint16_t bench_avg[4];          /* M-cycles per raw row, tile jobs included */
uint16_t bench_max[4];          /* the worst river_update() */
uint16_t bench_over_vb[4];      /* calls over 8000 M */
uint8_t bench_done;

static uint16_t b_t0, b_dt, b_i;
static uint8_t b_v;
static uint32_t b_sum;

void main(void) {
    DISPLAY_OFF;
    add_TIM(tim_isr);
    TMA_REG = 0u;
    TIMA_REG = 0u;
    TAC_REG = 0x06u;
    set_interrupts(TIM_IFLAG);

    for (b_v = 0; b_v < 4u; b_v++) {
        river_init(b_v);
        river_freeze = 1u;
        /* Warm up: leave the start road and the black rows behind. */
        for (b_i = 0; b_i < 400u; b_i++) {
            river_owed = 1u;
            river_gen_budget = 3u;
            river_update();
        }
        b_sum = 0u;
        bench_max[b_v] = 0u;
        for (b_i = 0; b_i < BENCH_ROWS; b_i++) {
            river_owed = 1u;
            river_gen_budget = 3u;
            b_t0 = ticks();
            river_update();
            b_dt = (uint16_t)(ticks() - b_t0);
            b_sum += b_dt;
            if (b_dt > bench_max[b_v]) {
                bench_max[b_v] = b_dt;
            }
            if (b_dt >= 500u) {
                bench_over_vb[b_v]++;
            }
        }
        bench_total[b_v] = b_sum << 4;
        bench_avg[b_v] = (uint16_t)(bench_total[b_v] / BENCH_ROWS);
        bench_max[b_v] <<= 4;
    }
    bench_done = 1u;
    for (;;) { }
}
