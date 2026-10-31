/*
 * gbc/hud_gbc.c - minimal HUD for the GBC port (milestone 2).
 *
 * The HUD is the Window layer: 20x2 tiles at the bottom of the screen (WY =
 * 128), below the 128 px playfield.  Its tiles live in VRAM bank 1, ids 0..23:
 * the sprite tiles start at SPRITE_TILE_BASE (64), so the low ids are free
 * there, and the river cache owns ids 3..254 of bank 0.  The window map's
 * attribute bytes select bank 1 and BG palette 1 once, at boot.
 *
 *   row 0:  score (6 digits)  E [fuel gauge, 8 tiles] F
 *   row 1:  lives (plane icons)                         B nn  (bridge)
 *
 * The art is the Atari charset (digits, E F B, the $5C plane), turned into
 * 2bpp: ink is colour 2 (yellow) on colour 0 (black).  hud_update() builds the
 * wanted 40 cells and writes only the ones that changed.  Call it right after
 * vsync(): the writes then land in VBlank.
 */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdint.h>

#include "config.h"
#include "atari_data.h"
#include "hud.h"

/* HUD state, read and written by player.c and entities.c. */
uint8_t hud_score_bcd[3];
uint8_t hud_hi_bcd[3];
uint8_t hud_fuel;
uint8_t hud_lives;
uint8_t hud_game;
uint8_t hud_bridge;
uint8_t hud_two;
uint8_t hud_cur;
uint8_t hud_blink;
static uint8_t hud_other_store[3];
uint8_t *hud_other_bcd = hud_other_store;

#define HT_BLANK        0u
#define HT_DIGIT        1u      /* 10 tiles: 1..10 */
#define HT_BAR          11u     /* 9 tiles: bar with 0..8 px filled */
#define HT_E            20u
#define HT_F            21u
#define HT_PLANE        22u
#define HT_B            23u
#define HT_COUNT        24u

#define HUD_COLS        20u
#define HUD_CELLS       (HUD_COLS * 2u)
#define HUD_WY          GBC_VIEW_LINES
#define HUD_ATTR        0x09u   /* VRAM bank 1, BG palette 1 */

#define HUD_SCORE_COL   0u
#define HUD_E_COL       8u
#define HUD_BAR_COL     9u
#define HUD_BAR_CELLS   8u
#define HUD_F_COL       17u
#define HUD_BRIDGE_COL  17u     /* row 1: B, tens, ones */
#define HUD_MAX_PLANES  6u

static const palette_color_t hud_palette[4] = {
    RGB(0, 0, 0), RGB(10, 10, 10), RGB(31, 28, 6), RGB(31, 31, 31)
};

static uint8_t hud_want[HUD_CELLS];
static uint8_t hud_shown[HUD_CELLS];
static uint8_t hud_tile[16];

/* Scratch for hud_update (file scope: SDCC aliases block locals across calls). */
static uint8_t hu_i;
static uint8_t hu_n;
static uint8_t hu_started;

/* One glyph row (1bpp) into the 2bpp buffer: ink = colour 2, i.e. plane 1. */
static void hud_glyph_tile(uint8_t id, uint8_t glyph) {
    const uint8_t *src = atari_charset + (uint16_t)glyph * 8u;

    for (hu_i = 0; hu_i < 8u; hu_i++) {
        hud_tile[hu_i * 2u] = 0u;
        hud_tile[hu_i * 2u + 1u] = src[hu_i];
    }
    set_bkg_data(id, 1, hud_tile);
}

/* Gauge cell with `px` pixels filled from the left (yellow), the rest grey. */
static void hud_bar_tile(uint8_t id, uint8_t px) {
    uint8_t row, filled, grey, p0, p1;

    filled = (uint8_t)(0xFFu << (8u - px));
    if (px == 0u) {
        filled = 0u;
    }
    grey = (uint8_t)~filled;
    for (row = 0; row < 8u; row++) {
        if (row == 0u || row == 7u) {
            p0 = 0u;
            p1 = 0u;
        } else {
            p0 = grey;                  /* colour 1 where grey, colour 2 where filled */
            p1 = filled;
        }
        hud_tile[row * 2u] = p0;
        hud_tile[row * 2u + 1u] = p1;
    }
    set_bkg_data(id, 1, hud_tile);
}

/* Boot-time setup; call with the display off. */
void hud_gbc_init(void) {
    uint8_t i;

    set_bkg_palette(1, 1, hud_palette);

    VBK_REG = 1;
    for (i = 0; i < 16u; i++) {
        hud_tile[i] = 0u;
    }
    set_bkg_data(HT_BLANK, 1, hud_tile);
    for (i = 0; i < 10u; i++) {
        hud_glyph_tile((uint8_t)(HT_DIGIT + i), (uint8_t)(GLYPH_0 + i));
    }
    for (i = 0; i <= 8u; i++) {
        hud_bar_tile((uint8_t)(HT_BAR + i), i);
    }
    hud_glyph_tile(HT_E, GLYPH_A + 4u);
    hud_glyph_tile(HT_F, GLYPH_A + 5u);
    hud_glyph_tile(HT_PLANE, 0x5Cu);
    hud_glyph_tile(HT_B, GLYPH_A + 1u);

    /* Attribute bytes of the window map ($9C00): bank 1 tiles, palette 1. */
    for (i = 0; i < HUD_COLS; i++) {
        set_win_tile_xy(i, 0, HUD_ATTR);
        set_win_tile_xy(i, 1, HUD_ATTR);
    }
    VBK_REG = 0;

    for (i = 0; i < HUD_CELLS; i++) {
        hud_want[i] = HT_BLANK;
        hud_shown[i] = 0xFFu;           /* force the first update to write all */
    }
    LCDC_REG = (uint8_t)((LCDC_REG | LCDCF_WIN9C00) & (uint8_t)~LCDCF_BG9C00);
    move_win(7, HUD_WY);
    SHOW_WIN;
    hud_update();
}

/* Six BCD digits with leading zeros blanked (a zero score shows one 0). */
static void hud_put_score(void) {
    hu_started = 0;
    for (hu_n = 0; hu_n < 6u; hu_n++) {
        hu_i = hud_score_bcd[hu_n >> 1];
        hu_i = (hu_n & 1u) ? (uint8_t)(hu_i & 0x0Fu) : (uint8_t)(hu_i >> 4);
        if (hu_i != 0u || hu_n == 5u) {
            hu_started = 1;
        }
        hud_want[HUD_SCORE_COL + hu_n] = hu_started ? (uint8_t)(HT_DIGIT + hu_i) : HT_BLANK;
    }
}

void hud_update(void) BANKED {
    uint8_t cell;

    hud_put_score();

    hud_want[HUD_E_COL] = HT_E;
    hud_want[HUD_F_COL] = HT_F;
    hu_n = (uint8_t)(hud_fuel >> 2);    /* 0..63 px over 8 cells */
    for (hu_i = 0; hu_i < HUD_BAR_CELLS; hu_i++) {
        if (hu_n >= 8u) {
            cell = 8u;
            hu_n = (uint8_t)(hu_n - 8u);
        } else {
            cell = hu_n;
            hu_n = 0;
        }
        hud_want[HUD_BAR_COL + hu_i] = (uint8_t)(HT_BAR + cell);
    }

    for (hu_i = 0; hu_i < HUD_MAX_PLANES; hu_i++) {
        hud_want[HUD_COLS + hu_i] = (hu_i < hud_lives) ? HT_PLANE : HT_BLANK;
    }

    hud_want[HUD_COLS + HUD_BRIDGE_COL] = HT_B;
    hud_want[HUD_COLS + HUD_BRIDGE_COL + 1u] =
        (uint8_t)(HT_DIGIT + (hud_bridge / 10u) % 10u);
    hud_want[HUD_COLS + HUD_BRIDGE_COL + 2u] = (uint8_t)(HT_DIGIT + hud_bridge % 10u);

    for (hu_i = 0; hu_i < HUD_CELLS; hu_i++) {
        if (hud_want[hu_i] != hud_shown[hu_i]) {
            hud_shown[hu_i] = hud_want[hu_i];
            set_win_tile_xy((uint8_t)(hu_i % HUD_COLS), (uint8_t)(hu_i / HUD_COLS), hud_want[hu_i]);
        }
    }
}
