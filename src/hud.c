/*
 * hud.c - Phase 2 boot screen and HUD scaffolding.
 *
 * Tile memory layout:
 *   0..127   Atari 8x8 character set (1bpp source expanded to NES 2bpp)
 *   128..132 scene tiles (interleaved Game Boy 2bpp source -> NES planar)
 *
 * Everything here is drawn through GBDK's normal tile API.  While the
 * display is off those calls write VRAM directly; once the display is on
 * they use the transfer buffer and are consumed by the NMI handler.
 */
#include <nes/nes.h>
#include <stdint.h>

#include "config.h"
#include "atari_data.h"
#include "hud.h"

volatile uint8_t playfield_scroll_y;

/* One 4-colour background palette.  Entry 0 is also the universal backdrop,
 * so the boot screen is white-on-black.  Entries 1..3 are white/green/blue. */
static const palette_color_t boot_palette[4] = {
    BG_COLOR_BLACK, BG_COLOR_WHITE, BG_COLOR_GREEN, BG_COLOR_BLUE
};

/* Scene tile source is Game Boy-style interleaved 2bpp: eight (plane0,
 * plane1) row pairs.  set_bkg_data() converts these to NES planar CHR. */
static const uint8_t scene_tiles[TILE_SCENE_COUNT * 16] = {
    /* 128 TILE_LAND: colour 2 (green) */
    0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
    0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
    /* 129 TILE_WATER: colour 3 (blue) */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* 130 TILE_BANK_LEFT: green on the left, blue on the right */
    0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0xFF,
    0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0xFF,
    /* 131 TILE_BANK_RIGHT: blue on the left, green on the right */
    0xF0, 0xFF, 0xF0, 0xFF, 0xF0, 0xFF, 0xF0, 0xFF,
    0xF0, 0xFF, 0xF0, 0xFF, 0xF0, 0xFF, 0xF0, 0xFF,
    /* 132 TILE_BRIDGE: blue with a two-pixel white band */
    0xFF, 0xE7, 0xFF, 0xE7, 0xFF, 0xE7, 0xFF, 0xE7,
    0xFF, 0xE7, 0xFF, 0xE7, 0xFF, 0xE7, 0xFF, 0xE7,
};

/* Scratch row shared by text and scene drawing.  BSS, not the stack. */
static uint8_t hud_row_buf[SCREEN_TILES_W];

/* BCD frame counter displayed at row 26.  Kept in BCD so the main loop
 * does not need 16-bit division every frame. */
static uint8_t frame_digits[4];
static uint8_t frame_glyphs[4];

/* Wavy river shape used by the boot screen.  The phase 4 generator will
 * replace this with the authentic Atari LFSR/table stream. */
static const uint8_t river_wave[BOOT_RIVER_WAVE_ROWS] = {
    0, 1, 2, 3, 4, 3, 2, 1
};

static uint8_t glyph_for_char(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (uint8_t)(c - 'A' + GLYPH_A);
    }
    if (c >= 'a' && c <= 'z') {
        return (uint8_t)(c - 'a' + 33u);
    }
    if (c >= '0' && c <= '9') {
        return (uint8_t)(c - '0' + GLYPH_0);
    }
    if (c == '.') {
        return GLYPH_DOT;
    }
    if (c == '!') {
        return GLYPH_BANG;
    }
    if (c == '?') {
        return GLYPH_QUESTION;
    }
    /* Anything else (including spaces) is the blank glyph. */
    return GLYPH_SPACE;
}

static void hud_draw_text(uint8_t x, uint8_t y, const char *text) {
    uint8_t n = 0;

    while (text[n] != '\0' && n < SCREEN_TILES_W) {
        hud_row_buf[n] = glyph_for_char(text[n]);
        n++;
    }
    if (n != 0) {
        set_bkg_tiles(x, y, n, 1, hud_row_buf);
    }
}

static void hud_draw_scene(void) {
    uint8_t r;
    uint8_t x;

    for (r = 0; r < BOOT_RIVER_ROWS; r++) {
        uint8_t y = (uint8_t)(BOOT_RIVER_TOP_ROW + r);
        uint8_t wave = river_wave[r & (BOOT_RIVER_WAVE_ROWS - 1u)];
        uint8_t left = (uint8_t)(BOOT_RIVER_LEFT_MIN + wave);
        uint8_t right = (uint8_t)(BOOT_RIVER_RIGHT_MAX - wave);

        for (x = 0; x < SCREEN_TILES_W; x++) {
            uint8_t tile;

            if (x < left) {
                tile = TILE_LAND;
            } else if (x == left) {
                tile = TILE_BANK_LEFT;
            } else if (x < right) {
                tile = (y == BOOT_BRIDGE_ROW) ? TILE_BRIDGE : TILE_WATER;
            } else if (x == right) {
                tile = TILE_BANK_RIGHT;
            } else {
                tile = TILE_LAND;
            }
            hud_row_buf[x] = tile;
        }
        set_bkg_tiles(0, y, (uint8_t)SCREEN_TILES_W, 1, hud_row_buf);
    }
}

void hud_init_palette(void) {
    set_bkg_palette(0, 1, boot_palette);
}

void hud_init_graphics(void) {
    /* 128 glyphs, 1bpp source; set_bkg_1bpp_data() expands to NES 2bpp.
     * GBDK's storage initialises fg=3,bg=0 which leaves plane 1 equal
     * to plane 0 (every bit becomes colour entry 3).  The library build
     * does not ship set_1bpp_colors_ex(), so poke the colour pair
     * directly: low byte = fg, high byte = bg.  fg=1/bg=0 puts glyph
     * bits in plane 0 only (colour entry 1, white). */
    _current_1bpp_colors = 0x0001;
    set_bkg_1bpp_data(TILE_FIRST_GLYPH, 128, atari_charset);
    set_bkg_data(TILE_FIRST_SCENE, TILE_SCENE_COUNT, scene_tiles);

    hud_draw_text(BOOT_TITLE_X, BOOT_TITLE_ROW, "RIVER RAID");
    hud_draw_text(BOOT_SUBTITLE_X, BOOT_SUBTITLE_ROW, "ATARI 800 TO NES");
    hud_draw_text(BOOT_PHASE_X, BOOT_PHASE_ROW, "PHASE 2 BOOT");

    hud_draw_text(BOOT_STATUS_X0, BOOT_STATUS_ROW0, "CHR RAM OK");
    hud_draw_text(BOOT_STATUS_X1, BOOT_STATUS_ROW1, "PALETTE OK");
    hud_draw_text(BOOT_STATUS_X2, BOOT_STATUS_ROW2, "NAMETABLE OK");

    hud_draw_scene();

    hud_draw_text(BOOT_FRAME_LABEL_X, BOOT_FRAME_LABEL_ROW, "FRAME");

    frame_digits[0] = 0;
    frame_digits[1] = 0;
    frame_digits[2] = 0;
    frame_digits[3] = 0;
    frame_glyphs[0] = GLYPH_0;
    frame_glyphs[1] = GLYPH_0;
    frame_glyphs[2] = GLYPH_0;
    frame_glyphs[3] = GLYPH_0;
    set_bkg_tiles(BOOT_FRAME_DIGITS_X, BOOT_FRAME_DIGITS_ROW, 4, 1,
                  frame_glyphs);

    hud_draw_text(BOOT_FOOTER_X, BOOT_FOOTER_ROW, "MESEN SMOKE TEST");
}

void hud_update_frame(void) {
    uint8_t i = 3;

    /* BCD increment with carry from ones to thousands. */
    for (;;) {
        frame_digits[i]++;
        if (frame_digits[i] < 10u) {
            break;
        }
        frame_digits[i] = 0;
        if (i == 0) {
            break;
        }
        i--;
    }

    frame_glyphs[0] = (uint8_t)(GLYPH_0 + frame_digits[0]);
    frame_glyphs[1] = (uint8_t)(GLYPH_0 + frame_digits[1]);
    frame_glyphs[2] = (uint8_t)(GLYPH_0 + frame_digits[2]);
    frame_glyphs[3] = (uint8_t)(GLYPH_0 + frame_digits[3]);
    set_bkg_tiles(BOOT_FRAME_DIGITS_X, BOOT_FRAME_DIGITS_ROW, 4, 1,
                  frame_glyphs);
}

void hud_set_playfield_scroll(uint8_t y) {
    playfield_scroll_y = y;
}
