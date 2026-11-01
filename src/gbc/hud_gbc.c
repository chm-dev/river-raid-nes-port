/*
 * gbc/hud_gbc.c - minimal HUD for the GBC port (milestone 2).
 *
 * The HUD is the Window layer: 20x2 tiles at the bottom of the screen (WY =
 * 128), below the 128 px playfield.  Its tiles live in VRAM bank 1, ids 0..23:
 * the sprite tiles start at SPRITE_TILE_BASE (64), so the low ids are free
 * there, and the river cache owns ids 3..254 of bank 0.  The window map's
 * attribute bytes select bank 1 and BG palette 1 once, at boot.
 *
 *   row 0:  lives (plane icons)   score, centred   B nn  (bridge)
 *   row 1:                 E [fuel gauge, 8 tiles] F, centred
 *
 * The art is the Atari charset (digits, E F B, the $5C plane), turned into
 * 2bpp: ink is colour 2 on colour 0 (black).  The ink colour comes from the
 * cell's palette: 1 yellow (planes, score), 4 B/F, 5 E, 6 gauge.  Palettes 4..6
 * change while the low-fuel note sounds (sound_warn_on): the gauge goes red and
 * the text goes yellow.  hud_update() builds the wanted 40 cells and writes only
 * the ones that changed.  Call it right after vsync(): the writes then land in
 * VBlank.
 */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdint.h>

#include "config.h"
#include "atari_data.h"
#include "hud.h"
#include "player.h"
#include "sound.h"

/* HUD state, read and written by player.c and entities.c. */
uint8_t hud_score_bcd[3];
uint8_t hud_hi_bcd[3];
uint8_t hud_fuel;
uint8_t hud_lives;
uint8_t hud_game;
uint8_t hud_bridge;
uint8_t hud_blink;

#define HT_BLANK        0u
#define HT_DIGIT        1u      /* 10 tiles: 1..10 */
#define HT_BAR          11u     /* 9 tiles: bar with 0..8 px filled */
#define HT_E            20u
#define HT_F            21u
#define HT_PLANE        22u
#define HT_B            23u
#define HT_HI_H         24u     /* idle: white HI and high score (ink in both planes = colour 3) */
#define HT_HI_I         25u
#define HT_WDIGIT       26u     /* 10 tiles: 26..35 */
#define HT_COUNT        36u

#define HUD_COLS        20u
#define HUD_CELLS       (HUD_COLS * 2u)
#define HUD_WY          GBC_VIEW_LINES
#define HUD_ATTR        0x08u   /* VRAM bank 1; OR in the BG palette number */
#define HUD_PAL_MAIN    1u      /* yellow ink: planes, score */
#define HUD_PAL_TEXT    4u      /* B and F: white, yellow on low fuel */
#define HUD_PAL_E       5u      /* E: red, yellow on low fuel */
#define HUD_PAL_BAR     6u      /* gauge: white, red on low fuel */

#define HUD_SCORE_DIGITS 6u
#define HUD_SCORE_FIRST 6u      /* score cells 6..16, blanked before each redraw */
#define HUD_SCORE_LAST  16u
#define HUD_BRIDGE_COL  17u     /* row 0: B, tens, ones */
#define HUD_GAUGE_COL   5u      /* row 1: E, 8 bar cells, F = 10 cells, centred */
#define HUD_E_COL       HUD_GAUGE_COL
#define HUD_BAR_COL     (HUD_GAUGE_COL + 1u)
#define HUD_BAR_CELLS   8u
#define HUD_F_COL       (HUD_BAR_COL + HUD_BAR_CELLS)
#define HUD_MAX_PLANES  6u

static const palette_color_t hud_palette[4] = {
    RGB(0, 0, 0), RGB(10, 10, 10), RGB(31, 28, 6), RGB(31, 31, 31)
};
/* [0] normal, [1] low-fuel warning sounding. */
static const palette_color_t hud_pal_text[2][4] = {
    { RGB(0, 0, 0), RGB(10, 10, 10), RGB(31, 31, 31), RGB(31, 31, 31) },
    { RGB(0, 0, 0), RGB(10, 10, 10), RGB(31, 28, 6), RGB(31, 28, 6) }
};
static const palette_color_t hud_pal_e[2][4] = {
    { RGB(0, 0, 0), RGB(10, 10, 10), RGB(22, 4, 4), RGB(22, 4, 4) },
    { RGB(0, 0, 0), RGB(10, 10, 10), RGB(31, 28, 6), RGB(31, 28, 6) }
};
static const palette_color_t hud_pal_bar[2][4] = {
    { RGB(0, 0, 0), RGB(10, 10, 10), RGB(31, 31, 31), RGB(31, 31, 31) },
    { RGB(0, 0, 0), RGB(10, 10, 10), RGB(22, 4, 4), RGB(22, 4, 4) }
};
static uint8_t hud_warn_shown;

static uint8_t hud_want[HUD_CELLS];
static uint8_t hud_shown[HUD_CELLS];
static uint8_t hud_tile[16];

/* Scratch for hud_update (file scope: SDCC aliases block locals across calls). */
static uint8_t hu_i;
static uint8_t hu_n;
static uint8_t hu_started;
static const uint8_t *hu_src;     /* BCD number hud_put_score draws */
static uint8_t hu_base;            /* first digit tile: yellow or white */

/* One glyph row (1bpp) into the 2bpp buffer: ink = colour 2, i.e. plane 1. */
static void hud_glyph_tile(uint8_t id, uint8_t glyph) {
    const uint8_t *src = atari_charset + (uint16_t)glyph * 8u;

    for (hu_i = 0; hu_i < 8u; hu_i++) {
        hud_tile[hu_i * 2u] = 0u;
        hud_tile[hu_i * 2u + 1u] = src[hu_i];
    }
    set_bkg_data(id, 1, hud_tile);
}

/* Same glyph in colour 3 (white in palette 1), for the idle high score. */
static void hud_white_tile(uint8_t id, uint8_t glyph) {
    const uint8_t *src = atari_charset + (uint16_t)glyph * 8u;

    for (hu_i = 0; hu_i < 8u; hu_i++) {
        hud_tile[hu_i * 2u] = src[hu_i];
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

    set_bkg_palette(HUD_PAL_MAIN, 1, hud_palette);
    hud_warn_shown = 0xFFu;             /* force the first palette write */

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
    hud_white_tile(HT_HI_H, GLYPH_A + 7u);
    hud_white_tile(HT_HI_I, GLYPH_A + 8u);
    for (i = 0; i < 10u; i++) {
        hud_white_tile((uint8_t)(HT_WDIGIT + i), (uint8_t)(GLYPH_0 + i));
    }

    /* Attribute bytes of the window map ($9C00): bank 1 tiles, palette 1. */
    for (i = 0; i < HUD_COLS; i++) {
        set_win_tile_xy(i, 0, (uint8_t)(HUD_ATTR | ((i >= HUD_BRIDGE_COL) ? HUD_PAL_TEXT : HUD_PAL_MAIN)));
        set_win_tile_xy(i, 1, (uint8_t)(HUD_ATTR | HUD_PAL_MAIN));
    }
    set_win_tile_xy(HUD_E_COL, 1, (uint8_t)(HUD_ATTR | HUD_PAL_E));
    for (i = 0; i < HUD_BAR_CELLS; i++) {
        set_win_tile_xy((uint8_t)(HUD_BAR_COL + i), 1, (uint8_t)(HUD_ATTR | HUD_PAL_BAR));
    }
    set_win_tile_xy(HUD_F_COL, 1, (uint8_t)(HUD_ATTR | HUD_PAL_TEXT));
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

/* Number on row 0 (the score, or the high score when idle), centred in the 20 columns.  Six BCD digits, leading zeros
 * dropped (a zero score keeps one 0).  An odd digit count cannot sit exactly
 * on the middle of a tile grid; it is placed half a tile to the right. */
static void hud_put_score(void) {
    for (hu_n = HUD_SCORE_FIRST; hu_n <= HUD_SCORE_LAST; hu_n++) {
        hud_want[hu_n] = HT_BLANK;
    }
    hu_started = 0;                     /* digits to draw */
    for (hu_n = 0; hu_n < HUD_SCORE_DIGITS - 1u; hu_n++) {
        hu_i = hu_src[hu_n >> 1];
        hu_i = (hu_n & 1u) ? (uint8_t)(hu_i & 0x0Fu) : (uint8_t)(hu_i >> 4);
        if (hu_i != 0u) {
            break;
        }
    }
    hu_started = (uint8_t)(HUD_SCORE_DIGITS - hu_n);    /* first digit index hu_n */
    hu_i = (uint8_t)((HUD_COLS - hu_started + 1u) >> 1);    /* first column */
    for (; hu_n < HUD_SCORE_DIGITS; hu_n++) {
        hud_want[hu_i++] = (uint8_t)(hu_base +
            ((hu_n & 1u) ? (hu_src[hu_n >> 1] & 0x0Fu) : (hu_src[hu_n >> 1] >> 4)));
    }
}
void hud_update(void) BANKED {
    uint8_t cell;

    if (player_state == PLAYER_STATE_ATTRACT) {
        hu_src = hud_hi_bcd;            /* idle: HI label and the high score, white */
        hu_base = HT_WDIGIT;
    } else {
        hu_src = hud_score_bcd;
        hu_base = HT_DIGIT;
    }
    hud_put_score();

    hud_want[HUD_COLS + HUD_E_COL] = HT_E;
    hud_want[HUD_COLS + HUD_F_COL] = HT_F;
    hu_n = (uint8_t)(hud_fuel >> 2);    /* 0..63 px over 8 cells */
    for (hu_i = 0; hu_i < HUD_BAR_CELLS; hu_i++) {
        if (hu_n >= 8u) {
            cell = 8u;
            hu_n = (uint8_t)(hu_n - 8u);
        } else {
            cell = hu_n;
            hu_n = 0;
        }
        hud_want[HUD_COLS + HUD_BAR_COL + hu_i] = (uint8_t)(HT_BAR + cell);
    }

    for (hu_i = 0; hu_i < HUD_MAX_PLANES; hu_i++) {
        hud_want[hu_i] = (hu_i < hud_lives && player_state != PLAYER_STATE_ATTRACT) ? HT_PLANE : HT_BLANK;
    }
    if (player_state == PLAYER_STATE_ATTRACT) {
        hud_want[0] = HT_HI_H;
        hud_want[1] = HT_HI_I;
    }

    hud_want[HUD_BRIDGE_COL] = HT_B;
    hud_want[HUD_BRIDGE_COL + 1u] = (uint8_t)(HT_DIGIT + (hud_bridge / 10u) % 10u);
    hud_want[HUD_BRIDGE_COL + 2u] = (uint8_t)(HT_DIGIT + hud_bridge % 10u);

    /* Colours follow the low-fuel note: red gauge while it sounds. */
    cell = (sound_warn_on != 0u) ? 1u : 0u;
    if (cell != hud_warn_shown) {
        hud_warn_shown = cell;
        set_bkg_palette(HUD_PAL_TEXT, 1, hud_pal_text[cell]);
        set_bkg_palette(HUD_PAL_E, 1, hud_pal_e[cell]);
        set_bkg_palette(HUD_PAL_BAR, 1, hud_pal_bar[cell]);
    }

    for (hu_i = 0; hu_i < HUD_CELLS; hu_i++) {
        if (hud_want[hu_i] != hud_shown[hu_i]) {
            hud_shown[hu_i] = hud_want[hu_i];
            set_win_tile_xy((uint8_t)(hu_i % HUD_COLS), (uint8_t)(hu_i / HUD_COLS), hud_want[hu_i]);
        }
    }
}
