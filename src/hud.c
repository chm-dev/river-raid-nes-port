/*
 * hud.c - stationary HUD built from the original Atari HUD artwork.
 *
 * The source HUD is a gray band with black and yellow text:
 *
 *   row 0:   P1          score          HI       hi-score
 *   row 1:   GAME        [ fuel box ]   BRIDGE   bridge#
 *   row 2:      game#    [ E 1/2 F ]    lives
 *
 * Fixed artwork comes straight from charset.bin:
 *   $43-$46 = "BRIDGE" (two letters per 8x8 glyph)
 *   $5A     = "P1",  $5B = "HI"
 *   $47-$4A = gauge top row  (47 48 49 48 4A)
 *   $4B-$4E = gauge bottom row (4B 4C 4D 4C 4E)
 *   $5C     = lives plane
 * Small labels are doubled horizontally, exactly like the Atari's mode-6
 * text.  "GAME" uses the ordinary uppercase glyphs the same way.
 *
 * The fuel level is a yellow vertical bar that slides from F (full) to E
 * (empty), behind the gauge's fixed E / 1-2 / F artwork.  The gauge frame
 * and its labels are background art; E, 1/2 and F are duplicated as black
 * sprites with a lower OAM index than the bar, so when the bar passes under
 * a label the sprite keeps the label visible on top.
 *
 * Tile memory layout (see config.h):
 *   0..127    Atari 8x8 character set (1bpp source expanded to NES 2bpp)
 *   128..209  Phase 3 river demo tiles
 *   210..255  fixed HUD artwork and yellow digits
 *
 * The HUD is drawn once into nametable 0 and then only dirty dynamic fields
 * are rewritten.  Everything here uses GBDK's normal tile API; while the
 * display is off those calls write VRAM directly, and once it is on they go
 * through the NMI transfer buffer.
 */
#include <nes/nes.h>
#include <stdint.h>
#include <string.h>

#include "config.h"
#include "atari_data.h"
#include "hud.h"

volatile uint8_t playfield_scroll_y;

uint8_t hud_score_bcd[3];
uint8_t hud_hi_bcd[3];
uint8_t hud_fuel;
uint8_t hud_lives;
uint8_t hud_game;
uint8_t hud_bridge;

/* The backdrop is black (Atari COLBK, needed for the river's black pixels), so
 * the gray band is pixel value 1 of the HUD tiles: black ink is value 0 (the
 * backdrop), yellow ink is value 2, white spare is value 3. */
static const palette_color_t hud_palette[4] = {
    BG_COLOR_BLACK, BG_COLOR_GRAY, BG_COLOR_YELLOW, BG_COLOR_WHITE
};

static const palette_color_t playfield_palette[4] = {
    BG_COLOR_BLACK, BG_COLOR_GREEN, BG_COLOR_BROWN, BG_COLOR_BLUE
};

/* The land alternates between $D2 and $D6 each level (it changes at the road). */
static const palette_color_t playfield_alt_palette[4] = {
    BG_COLOR_BLACK, BG_COLOR_BRIGHT_GREEN, BG_COLOR_BROWN, BG_COLOR_BLUE
};

/* Road rows: the land pixels become grey ($06/$08) and the yellow centre line
 * ($1C); black is the road's dark edge and the bank outline. */
static const palette_color_t road_palette[4] = {
    BG_COLOR_BLACK, BG_COLOR_GRAY, 0x38u, BG_COLOR_BLUE
};

/* Gauge labels and fraction are plane-0 sprites on sprite palette 2, whose
 * entry 1 is black (set up in entities_init_graphics). */

/* Fuel marker: 4-pixel yellow column in two stacked sprites (8 + 2 px),
 * starting just below the gauge's top markers and ending just above the
 * bottom rule.  The colour starts in tile column 0 so the sprite X equals
 * the marker X.  Sprite palette 0 entry 1 is the player yellow. */
static const uint8_t fuel_bar_tile[16] = {
    0xF0, 0x00, 0xF0, 0x00, 0xF0, 0x00, 0xF0, 0x00,
    0xF0, 0x00, 0xF0, 0x00, 0xF0, 0x00, 0xF0, 0x00
};
static const uint8_t fuel_bar_tile2[16] = {
    0xF0, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* Bar X position by fuel sixteenth.  The half-tank entry (8) lines up with
 * the centre of the fixed 1/2 artwork. */
static const uint8_t fuel_bar_x[16] = {
    80, 84, 88, 92, 95, 99, 103, 107,
    111, 115, 120, 124, 129, 133, 137, 142
};

/* Scratch tile buffer for doubled glyphs (2 tiles) and gauge sprites. */
static uint8_t wide_tile_buf[2 * 16];
static uint8_t gauge_sprite_buf[4 * 16];

/* Scratch row shared by text and dynamic field drawing.  BSS, not the stack. */
static uint8_t hud_row_buf[SCREEN_TILES_W];

/* Cached values; hud_update() writes a field only when it differs. */
static uint8_t shown_score[6];
static uint8_t shown_hi[6];
static uint8_t shown_bridge[2];
static uint8_t shown_game;
static uint8_t shown_lives;
static uint8_t shown_fuel_step;

/* ------------------------------------------------------------------------- */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------- */

static void hud_vram_byte(uint16_t addr, uint8_t value) {
    set_vram_byte((uint8_t *)addr, value);
}

/* Duplicate a byte's pixels horizontally: 8 source bits -> 16 output bits. */
static uint8_t spread_left(uint8_t v) {
    uint8_t out = 0;

    if (v & 0x80u) out |= 0xC0u;
    if (v & 0x40u) out |= 0x30u;
    if (v & 0x20u) out |= 0x0Cu;
    if (v & 0x10u) out |= 0x03u;
    return out;
}

static uint8_t spread_right(uint8_t v) {
    uint8_t out = 0;

    if (v & 0x08u) out |= 0xC0u;
    if (v & 0x04u) out |= 0x30u;
    if (v & 0x02u) out |= 0x0Cu;
    if (v & 0x01u) out |= 0x03u;
    return out;
}

/* Load one charset glyph as two 16x8 background tiles on the gray band
 * (value 1 = plane 0).  Black ink clears plane 0 (value 0, the backdrop);
 * yellow ink also sets plane 1 (value 2). */
static void hud_wide_glyph(uint8_t dest, uint8_t code, uint8_t yellow) {
    const uint8_t *src = atari_charset + (uint16_t)(code & 0x7Fu) * 8u;
    uint8_t row;

    for (row = 0; row < 8u; row++) {
        uint8_t l = spread_left(src[row]);
        uint8_t r = spread_right(src[row]);

        wide_tile_buf[row * 2u] = (uint8_t)~l;
        wide_tile_buf[row * 2u + 1u] = yellow ? l : 0u;
        wide_tile_buf[16u + row * 2u] = (uint8_t)~r;
        wide_tile_buf[16u + row * 2u + 1u] = yellow ? r : 0u;
    }
    set_bkg_data(dest, 2, wide_tile_buf);
}

/* Load the ten charset digits (and a solid gray blank) as 8x8 tiles: black
 * digits at TILE_DIGIT_BASE, yellow digits at TILE_DIGIT_YELLOW_BASE. */
static void hud_load_digits(void) {
    uint8_t d;
    uint8_t row;

    for (row = 0; row < 8u; row++) {
        wide_tile_buf[row * 2u] = 0xFFu;
        wide_tile_buf[row * 2u + 1u] = 0u;
    }
    set_bkg_data(TILE_BLANK, 1, wide_tile_buf);

    for (d = 0; d < 10u; d++) {
        const uint8_t *src = atari_charset + (uint16_t)(GLYPH_0 + d) * 8u;

        for (row = 0; row < 8u; row++) {
            wide_tile_buf[row * 2u] = (uint8_t)~src[row];
            wide_tile_buf[row * 2u + 1u] = 0u;
            wide_tile_buf[16u + row * 2u] = (uint8_t)~src[row];
            wide_tile_buf[16u + row * 2u + 1u] = src[row];
        }
        set_bkg_data((uint8_t)(TILE_DIGIT_BASE + d), 1, wide_tile_buf);
        set_bkg_data((uint8_t)(TILE_DIGIT_YELLOW_BASE + d), 1, wide_tile_buf + 16);
    }
}

/* Convert 3 BCD bytes (6 digits) to tile IDs, suppressing leading zeros.
 * `base` is TILE_DIGIT_BASE (black) or TILE_DIGIT_YELLOW_BASE. */
static void hud_bcd_tiles(const uint8_t bcd[3], uint8_t out[6], uint8_t base) {
    uint8_t digits[6];
    uint8_t started = 0;
    uint8_t i;

    digits[0] = (uint8_t)(bcd[0] >> 4);
    digits[1] = (uint8_t)(bcd[0] & 0x0Fu);
    digits[2] = (uint8_t)(bcd[1] >> 4);
    digits[3] = (uint8_t)(bcd[1] & 0x0Fu);
    digits[4] = (uint8_t)(bcd[2] >> 4);
    digits[5] = (uint8_t)(bcd[2] & 0x0Fu);

    for (i = 0; i < 6u; i++) {
        if (digits[i] != 0u) {
            started = 1u;
        }
        out[i] = started ? (uint8_t)(base + digits[i]) : GLYPH_SPACE;
    }
    if (!started) {
        out[5] = base; /* a zero score still shows one digit */
    }
}

/* ------------------------------------------------------------------------- */
/* Gauge sprite art (from the original charset)                              */
/* ------------------------------------------------------------------------- */

/* Gauge labels use the 4-pixel-wide letters.  The top bar is the last row
 * of the upper box glyph ($47 for E, $4A for F); the body is the first six
 * rows of the lower glyph ($4B / $4E).  E sits in columns 4..7, F in 0..3. */
static void hud_build_gauge_letter(uint8_t dest, uint8_t top_code,
                                   uint8_t body_code, uint8_t shift) {
    const uint8_t *top = atari_charset + (uint16_t)(top_code & 0x7Fu) * 8u;
    const uint8_t *body = atari_charset + (uint16_t)(body_code & 0x7Fu) * 8u;
    uint8_t row;

    for (row = 0; row < 8u; row++) {
        uint8_t src;

        if (row == 0u) {
            src = top[7];                  /* top bar */
        } else if (row < 7u) {
            src = body[row - 1u];          /* letter body */
        } else {
            src = 0u;
        }
        wide_tile_buf[row * 2u] =
            spread_left((uint8_t)(((src >> shift) & 0x0Fu) << 4));
        wide_tile_buf[row * 2u + 1u] = 0u;
    }
    set_sprite_data(dest, 1, wide_tile_buf);
}

/* "1/2" is baked into the gauge-box centre glyphs: $49 carries the upper
 * seven rows, $4D the lower seven.  Build it as a 16x14 (2x2 sprite) map. */
static void hud_build_gauge_fraction(void) {
    uint8_t row;
    uint8_t i;

    for (i = 0; i < 4u * 16u; i++) {
        gauge_sprite_buf[i] = 0u;
    }

    for (row = 0; row < 14u; row++) {
        uint8_t src = (row < 7u)
            ? atari_charset[(0x49u * 8u) + 1u + row]
            : atari_charset[(0x4Du * 8u) + (row - 7u)];
        uint8_t left = spread_left(src);
        uint8_t right = spread_right(src);
        uint8_t tile = (row < 8u) ? 0u : 2u;
        uint8_t local = (uint8_t)(row & 7u);
        uint16_t base = (uint16_t)tile * 16u + (uint16_t)local * 2u;

        gauge_sprite_buf[base] = left;
        gauge_sprite_buf[base + 1u] = 0u;
        gauge_sprite_buf[base + 16u] = right;
        gauge_sprite_buf[base + 17u] = 0u;
    }
    set_sprite_data(HUD_FRAC_TILE, 4, gauge_sprite_buf);
}

/* Lives plane is the source's $5C glyph, one sprite tile. */
static void hud_build_life_tile(void) {
    const uint8_t *src = atari_charset + (0x5Cu * 8u);
    uint8_t row;

    for (row = 0; row < 8u; row++) {
        gauge_sprite_buf[row * 2u] = src[row];
        gauge_sprite_buf[row * 2u + 1u] = 0u;
    }
    set_sprite_data(HUD_LIFE_TILE, 1, gauge_sprite_buf);
}

static void hud_init_gauge_sprites(void) {
    uint8_t i;

    hud_build_gauge_letter(HUD_GAUGE_E_TILE, 0x47u, 0x4Bu, 0u);
    hud_build_gauge_letter(HUD_GAUGE_F_TILE, 0x4Au, 0x4Eu, 4u);
    hud_build_gauge_fraction();
    hud_build_life_tile();
    set_sprite_data(HUD_FUEL_BAR_TILE, 1, fuel_bar_tile);
    set_sprite_data(HUD_FUEL_BAR_TILE2, 1, fuel_bar_tile2);

    set_sprite_tile(HUD_GAUGE_E_SLOT, HUD_GAUGE_E_TILE);
    set_sprite_prop(HUD_GAUGE_E_SLOT, S_PAL(2));
    move_sprite(HUD_GAUGE_E_SLOT, HUD_GAUGE_E_X, HUD_GAUGE_LABEL_Y - 1u);

    for (i = 0; i < 4u; i++) {
        uint8_t slot = (uint8_t)(HUD_FRAC_SLOT + i);

        set_sprite_tile(slot, (uint8_t)(HUD_FRAC_TILE + i));
        set_sprite_prop(slot, S_PAL(2));
        move_sprite(slot,
                    (uint8_t)(HUD_FRAC_X + (i & 1u) * 8u),
                    (uint8_t)(HUD_FRAC_Y + (i >> 1) * 8u - 1u));
    }

    set_sprite_tile(HUD_GAUGE_F_SLOT, HUD_GAUGE_F_TILE);
    set_sprite_prop(HUD_GAUGE_F_SLOT, S_PAL(2));
    move_sprite(HUD_GAUGE_F_SLOT, HUD_GAUGE_F_X, HUD_GAUGE_LABEL_Y - 1u);

    set_sprite_tile(HUD_FUEL_BAR_SLOT, HUD_FUEL_BAR_TILE);
    set_sprite_prop(HUD_FUEL_BAR_SLOT, S_PAL(0));
    move_sprite(HUD_FUEL_BAR_SLOT, HUD_FUEL_BAR_X_MAX, HUD_FUEL_BAR_Y - 1u);

    set_sprite_tile(HUD_FUEL_BAR_SLOT2, HUD_FUEL_BAR_TILE2);
    set_sprite_prop(HUD_FUEL_BAR_SLOT2, S_PAL(0));
    move_sprite(HUD_FUEL_BAR_SLOT2, HUD_FUEL_BAR_X_MAX, HUD_FUEL_BAR_Y2 - 1u);

    for (i = 0; i < HUD_LIVES_ICON_MAX; i++) {
        uint8_t slot = (uint8_t)(HUD_LIFE_SLOT + i);

        set_sprite_tile(slot, HUD_LIFE_TILE);
        set_sprite_prop(slot, S_PAL(0));
        hide_sprite(slot);
    }
}

/* ------------------------------------------------------------------------- */
/* Dynamic fields                                                            */
/* ------------------------------------------------------------------------- */

static void hud_write_score(void) {
    uint8_t tiles[6];
    uint8_t i;
    uint8_t changed = 0;

    hud_bcd_tiles(hud_score_bcd, tiles, TILE_DIGIT_YELLOW_BASE);
    for (i = 0; i < 6u; i++) {
        if (tiles[i] != shown_score[i]) {
            changed = 1u;
        }
    }
    if (changed) {
        set_bkg_tiles(HUD_SCORE_X, HUD_SCORE_ROW, 6, 1, tiles);
        memcpy(shown_score, tiles, 6);
    }
}

static void hud_write_hi(void) {
    uint8_t tiles[6];
    uint8_t i;
    uint8_t changed = 0;

    hud_bcd_tiles(hud_hi_bcd, tiles, TILE_DIGIT_BASE);
    for (i = 0; i < 6u; i++) {
        if (tiles[i] != shown_hi[i]) {
            changed = 1u;
        }
    }
    if (changed) {
        set_bkg_tiles(HUD_HI_X, HUD_HI_ROW, 6, 1, tiles);
        memcpy(shown_hi, tiles, 6);
    }
}

static void hud_write_bridge(void) {
    uint8_t value = hud_bridge;
    uint8_t tiles[2];

    if (value > HUD_BRIDGE_DIGIT_MAX) {
        value = HUD_BRIDGE_DIGIT_MAX;
    }
    if (value >= 10u) {
        tiles[0] = (uint8_t)(TILE_DIGIT_YELLOW_BASE + value / 10u);
        tiles[1] = (uint8_t)(TILE_DIGIT_YELLOW_BASE + value % 10u);
    } else {
        tiles[0] = TILE_BLANK;
        tiles[1] = (uint8_t)(TILE_DIGIT_YELLOW_BASE + value);
    }
    if (tiles[0] != shown_bridge[0] || tiles[1] != shown_bridge[1]) {
        set_bkg_tiles(HUD_BRIDGE_DIGIT_X, HUD_BRIDGE_ROW, 2, 1, tiles);
        shown_bridge[0] = tiles[0];
        shown_bridge[1] = tiles[1];
    }
}

static void hud_write_game(void) {
    uint8_t digit = hud_game;

    if (digit > 9u) {
        digit = 9u;
    }
    if (digit != shown_game) {
        set_bkg_tile_xy(HUD_GAME_DIGIT_X, HUD_GAME_DIGIT_ROW,
                        (uint8_t)(TILE_DIGIT_BASE + digit));
        shown_game = digit;
    }
}

static void hud_write_lives(void) {
    uint8_t i;
    uint8_t icons = hud_lives;

    if (icons > HUD_LIVES_ICON_MAX) {
        icons = HUD_LIVES_ICON_MAX;
    }
    if (icons == shown_lives) {
        return;
    }
    for (i = 0; i < HUD_LIVES_ICON_MAX; i++) {
        uint8_t slot = (uint8_t)(HUD_LIFE_SLOT + i);

        if (i < icons) {
            move_sprite(slot, (uint8_t)(HUD_LIFE_X + i * 8u),
                        HUD_LIFE_Y - 1u);
        } else {
            hide_sprite(slot);
        }
    }
    shown_lives = icons;
}

/* Slide the yellow level marker across the gauge; the labels never change. */
static void hud_write_fuel(void) {
    uint8_t step = (uint8_t)(hud_fuel >> 4);

    if (step == shown_fuel_step) {
        return;
    }
    move_sprite(HUD_FUEL_BAR_SLOT, fuel_bar_x[step], HUD_FUEL_BAR_Y - 1u);
    move_sprite(HUD_FUEL_BAR_SLOT2, fuel_bar_x[step], HUD_FUEL_BAR_Y2 - 1u);
    shown_fuel_step = step;
}

/* ------------------------------------------------------------------------- */
/* Static layout                                                             */
/* ------------------------------------------------------------------------- */

static void hud_draw_static(void) {
    uint8_t tiles[HUD_GAUGE_W];
    uint8_t i;

    /* Labels are the game's own 4-pixel HUD glyphs, doubled like mode 6. */
    hud_wide_glyph(TILE_P1_WIDE, 0x5Au, 1u);          /* P1, yellow */
    hud_wide_glyph(TILE_HI_WIDE, 0x5Bu, 0u);          /* HI, black */
    hud_wide_glyph(TILE_BRIDGE_WIDE + 0u, 0x43u, 1u); /* BR */
    hud_wide_glyph(TILE_BRIDGE_WIDE + 2u, 0x44u, 1u); /* ID */
    hud_wide_glyph(TILE_BRIDGE_WIDE + 4u, 0x45u, 1u); /* G */
    hud_wide_glyph(TILE_BRIDGE_WIDE + 6u, 0x46u, 1u); /* E */

    /* GAME uses the ordinary uppercase glyphs, doubled. */
    hud_wide_glyph(TILE_GAME_WIDE + 0u, 7u, 0u);      /* G */
    hud_wide_glyph(TILE_GAME_WIDE + 2u, 1u, 0u);      /* A */
    hud_wide_glyph(TILE_GAME_WIDE + 4u, 13u, 0u);     /* M */
    hud_wide_glyph(TILE_GAME_WIDE + 6u, 5u, 0u);      /* E */

    /* Gauge frame comes from the charset's 5-cell assembly:
     *   top    47 48 49 48 4A
     *   bottom 4B 4C 4D 4C 4E */
    hud_wide_glyph(TILE_GAUGE_TL, 0x47u, 0u);
    hud_wide_glyph(TILE_GAUGE_T, 0x48u, 0u);
    hud_wide_glyph(TILE_GAUGE_TC, 0x49u, 0u);
    hud_wide_glyph(TILE_GAUGE_TR, 0x4Au, 0u);
    hud_wide_glyph(TILE_GAUGE_BL, 0x4Bu, 0u);
    hud_wide_glyph(TILE_GAUGE_B, 0x4Cu, 0u);
    hud_wide_glyph(TILE_GAUGE_BC, 0x4Du, 0u);
    hud_wide_glyph(TILE_GAUGE_BR, 0x4Eu, 0u);

    tiles[0] = TILE_P1_WIDE;
    tiles[1] = TILE_P1_WIDE + 1u;
    set_bkg_tiles(HUD_P1_X, HUD_P1_ROW, 2, 1, tiles);

    tiles[0] = TILE_HI_WIDE;
    tiles[1] = TILE_HI_WIDE + 1u;
    set_bkg_tiles(HUD_HI_LABEL_X, HUD_HI_ROW, 2, 1, tiles);

    for (i = 0; i < HUD_GAME_TILES; i++) {
        tiles[i] = (uint8_t)(TILE_GAME_WIDE + i);
    }
    set_bkg_tiles(HUD_GAME_X, HUD_GAME_ROW, HUD_GAME_TILES, 1, tiles);

    for (i = 0; i < HUD_BRIDGE_TILES; i++) {
        tiles[i] = (uint8_t)(TILE_BRIDGE_WIDE + i);
    }
    set_bkg_tiles(HUD_BRIDGE_X, HUD_BRIDGE_ROW, HUD_BRIDGE_TILES, 1, tiles);

    /* Top row: TL, T, TC, T, TR. */
    tiles[0] = TILE_GAUGE_TL;
    tiles[1] = TILE_GAUGE_TL + 1u;
    tiles[2] = TILE_GAUGE_T;
    tiles[3] = TILE_GAUGE_T + 1u;
    tiles[4] = TILE_GAUGE_TC;
    tiles[5] = TILE_GAUGE_TC + 1u;
    tiles[6] = TILE_GAUGE_T;
    tiles[7] = TILE_GAUGE_T + 1u;
    tiles[8] = TILE_GAUGE_TR;
    tiles[9] = TILE_GAUGE_TR + 1u;
    set_bkg_tiles(HUD_GAUGE_X, HUD_GAUGE_ROW, HUD_GAUGE_W, 1, tiles);

    /* Bottom row: BL, B, BC, B, BR. */
    tiles[0] = TILE_GAUGE_BL;
    tiles[1] = TILE_GAUGE_BL + 1u;
    tiles[2] = TILE_GAUGE_B;
    tiles[3] = TILE_GAUGE_B + 1u;
    tiles[4] = TILE_GAUGE_BC;
    tiles[5] = TILE_GAUGE_BC + 1u;
    tiles[6] = TILE_GAUGE_B;
    tiles[7] = TILE_GAUGE_B + 1u;
    tiles[8] = TILE_GAUGE_BR;
    tiles[9] = TILE_GAUGE_BR + 1u;
    set_bkg_tiles(HUD_GAUGE_X, HUD_GAUGE_BOTTOM_ROW, HUD_GAUGE_W, 1, tiles);
}

static void hud_refresh_all(void) {
    hud_write_score();
    hud_write_hi();
    hud_write_bridge();
    hud_write_game();
    hud_write_lives();
    hud_write_fuel();
}

/* ------------------------------------------------------------------------- */
/* Public interface                                                          */
/* ------------------------------------------------------------------------- */

void hud_init_palette(void) BANKED {
    set_bkg_palette(HUD_PALETTE, 1, hud_palette);
    set_bkg_palette(PLAYFIELD_PALETTE, 1, playfield_palette);
    set_bkg_palette(PLAYFIELD_ALT_PALETTE, 1, playfield_alt_palette);
    set_bkg_palette(ROAD_PALETTE, 1, road_palette);
}

void hud_init_graphics(void) BANKED {
    uint8_t i;

    /* Blank (solid gray) at tile 0, black digits 1..10, yellow digits 48..57. */
    /* HUD tiles live in the $1000 pattern table (shared with sprites); the
     * playfield uses $0000.  Select it for the BG loads below only. */
    shadow_PPUCTRL |= BG_TABLE_BIT;
    hud_load_digits();

    /* River tiles are generated by river_init_tiles() during river_init(). */

    /* Source HUD artwork, generated from the Atari charset. */
    hud_draw_static();
    shadow_PPUCTRL &= (uint8_t)~BG_TABLE_BIT;
    hud_init_gauge_sprites();

    /* Palette 0 everywhere; yellow vs black text is chosen by tile art. */
    for (i = 0; i < 64; i++) {
        hud_vram_byte((uint16_t)(PPU_NT0_ATTR + i), 0x00u);
    }

    hud_score_bcd[0] = 0;
    hud_score_bcd[1] = 0;
    hud_score_bcd[2] = 0;
    hud_hi_bcd[0] = 0;
    hud_hi_bcd[1] = 0;
    hud_hi_bcd[2] = 0;
    hud_fuel = 255;
    hud_lives = 3;
    hud_game = 1;
    hud_bridge = 1;
    shown_fuel_step = 0xFFu; /* force the first marker write */

    hud_refresh_all();
}

/* One field per call: each HUD field is refreshed every sixth pass, which keeps
 * the per-pass cost low (the score/hi-score tile conversion alone is ~1k). */
static uint8_t hud_phase;

void hud_update(void) BANKED {
    switch (hud_phase) {
    case 0: hud_write_score(); break;
    case 1: hud_write_hi(); break;
    case 2: hud_write_bridge(); break;
    case 3: hud_write_game(); break;
    case 4: hud_write_lives(); break;
    default: hud_write_fuel(); hud_phase = 0xFFu; break;
    }
    hud_phase++;
}

