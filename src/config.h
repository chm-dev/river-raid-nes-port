/*
 * config.h - shared NES port geometry and tile constants.
 *
 * The NES screen is 32 x 30 background tiles (256 x 240 pixels).  The top
 * three tile rows (24 scanlines) are the stationary HUD; the remaining 27
 * rows are the vertically scrolling river playfield.  GBDK's add_LCD()
 * handler switches the scroll registers at scanline 24.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* ------------------------------------------------------------------------- */
/* Screen geometry (tiles / scanlines)                                       */
/* ------------------------------------------------------------------------- */

#define SCREEN_TILES_W          32u
#define SCREEN_TILES_H          30u

#define HUD_TILES_H             3u
#define HUD_SCANLINES           (HUD_TILES_H * 8u)          /* 24 */

#define PLAYFIELD_TOP_ROW       HUD_TILES_H                 /* 3 */
#define PLAYFIELD_ROWS          (SCREEN_TILES_H - HUD_TILES_H) /* 27 */
#define PLAYFIELD_SCANLINES     (PLAYFIELD_ROWS * 8u)       /* 216 */

/* Visible width of the Atari mode-E playfield in bytes/tiles. */
#define ATARI_ROW_BYTES         48u
#define ATARI_VISIBLE_X         4u
#define ATARI_VISIBLE_BYTES     40u

/* ------------------------------------------------------------------------- */
/* Background tile allocation                                                */
/* ------------------------------------------------------------------------- */
/*
 * Tiles 0..127 are reserved for the extracted Atari 8x8 character set
 * (glyph index == tile index).  Tiles 128..255 hold the river/land tiles.
 * The boot ROM only needs a handful of solid/transition tiles.
 */

#define TILE_FIRST_GLYPH        0u
#define TILE_LAST_GLYPH         127u

#define TILE_LAND               128u    /* solid bank green (colour entry 2) */
#define TILE_WATER              129u    /* solid river blue (colour entry 3) */
#define TILE_BANK_LEFT          130u    /* land on left, water on right */
#define TILE_BANK_RIGHT         131u    /* water on left, land on right */
#define TILE_BRIDGE             132u    /* blue with a white bridge band */

#define TILE_FIRST_SCENE        128u
#define TILE_SCENE_COUNT        5u

/* ------------------------------------------------------------------------- */
/* Shared background palette (one 4-colour BG palette for the boot screen)   */
/* ------------------------------------------------------------------------- */

#define BG_COLOR_BLACK          0x0Fu
#define BG_COLOR_WHITE          0x30u
#define BG_COLOR_GREEN          0x1Au
#define BG_COLOR_BLUE           0x12u

/* ------------------------------------------------------------------------- */
/* Extracted Atari character codes (see docs/ATARI_RE_SPEC.md section 8)     */
/* ------------------------------------------------------------------------- */

#define GLYPH_SPACE             0u
#define GLYPH_A                 1u
#define GLYPH_Z                 26u
#define GLYPH_DOT               63u
#define GLYPH_BANG              65u
#define GLYPH_QUESTION          95u
#define GLYPH_0                 80u
#define GLYPH_9                 89u

/* ------------------------------------------------------------------------- */
/* Boot-screen layout (nametable rows/columns)                               */
/* ------------------------------------------------------------------------- */

#define BOOT_TITLE_ROW          0u
#define BOOT_TITLE_X            11u
#define BOOT_SUBTITLE_ROW       1u
#define BOOT_SUBTITLE_X         8u
#define BOOT_PHASE_ROW          2u
#define BOOT_PHASE_X            10u

#define BOOT_STATUS_ROW0        4u
#define BOOT_STATUS_X0          11u
#define BOOT_STATUS_ROW1        5u
#define BOOT_STATUS_X1          11u
#define BOOT_STATUS_ROW2        6u
#define BOOT_STATUS_X2          10u

#define BOOT_RIVER_TOP_ROW      8u
#define BOOT_RIVER_ROWS         17u
#define BOOT_RIVER_LEFT_MIN     3u
#define BOOT_RIVER_RIGHT_MAX    28u
#define BOOT_RIVER_WAVE_ROWS    8u
#define BOOT_BRIDGE_ROW         16u

#define BOOT_FRAME_LABEL_ROW    26u
#define BOOT_FRAME_LABEL_X      11u
#define BOOT_FRAME_DIGITS_ROW   26u
#define BOOT_FRAME_DIGITS_X     17u

#define BOOT_FOOTER_ROW         28u
#define BOOT_FOOTER_X           8u

#endif /* CONFIG_H */
