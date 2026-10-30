/*
 * config.h - shared NES port geometry and tile constants.
 *
 * The NES screen is 32 x 30 background tiles (256 x 240 pixels).  The top
 * four tile rows (32 scanlines) plus 4 padding lines are the stationary HUD;
 * the remaining 204 pixels are the vertically scrolling river playfield.
 * GBDK's add_LCD() deferred handler switches the scroll registers at
 * scanline 36.
 *
 * The HUD lives in nametable 0 ($2000).  The playfield lives in the second
 * physical nametable, $2400.  The iNES header uses horizontal arrangement
 * (vertical mirroring), so $2000 and $2400 are the two distinct nametables.
 * Keeping the playfield in its own nametable is what allows fine 1-pixel
 * scrolling: the 30-row ring wraps back to its own row 0 instead of into the
 * HUD rows.  The switch uses PPUCTRL's horizontal nametable bit, which the
 * PPU copies into its address register on every scanline (the vertical bit
 * would only change at the pre-render).
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* ------------------------------------------------------------------------- */
/* Screen geometry (tiles / scanlines)                                       */
/* ------------------------------------------------------------------------- */

#define SCREEN_TILES_W          32u
#define SCREEN_TILES_H          30u

#define HUD_TILES_H             4u
#define HUD_SCANLINES           (HUD_TILES_H * 8u)          /* 32 */

/* The HUD is three tile rows (nametable rows 1..3, 24 scanlines) plus one gray
 * scanline of padding.  Nametable row 0 is blank padding that is scrolled out
 * of view (the HUD pass runs with a vertical scroll of HUD_SCROLL_Y), which
 * keeps every HUD tile position unchanged.
 *
 * Requested LYC row for the playfield split.  GBDK's faked LCD delay applies
 * the entry one scanline later, so LYC=24 puts the playfield at scanline
 * 25/26; the gray padding line absorbs that one-line jitter. */
#define HUD_SCROLL_Y            5u      /* 8 - 3 px of top padding */
#ifndef HUD_SPLIT_LYC
#define HUD_SPLIT_SCANLINE      30u     /* 3 top + 24 content + 3 bottom */
#define HUD_SPLIT_LYC           29u     /* -> visible split 30/31 */
#endif

#define PLAYFIELD_TOP_ROW       HUD_TILES_H                 /* 4 */
#define PLAYFIELD_SCANLINE      HUD_SPLIT_SCANLINE          /* 30 */
#define PLAYFIELD_ROWS          26u                             /* 27 visible rows */
#define PLAYFIELD_SCANLINES     210u                            /* 240 - 30 */

/* Visible width of the Atari mode-E playfield in bytes/tiles. */
#define ATARI_ROW_BYTES         48u
#define ATARI_VISIBLE_X         4u
#define ATARI_VISIBLE_BYTES     40u

/* ------------------------------------------------------------------------- */
/* Background tile allocation                                                */
/* ------------------------------------------------------------------------- */
/*
 * Background tile allocation (256 BG slots, all of CHR-RAM):
 *
 *   0         blank / space glyph
 *   1..10     black digits 0..9 (gray band = value 1, ink = backdrop black)
 *   48..57    yellow digits 0..9 (ink = value 2); score and bridge use these
 *   11..26    gauge frame built from charset glyphs $47-$4E
 *             (5 cells: top 47/48/49/48/4A, bottom 4B/4C/4D/4C/4E)
 *   27..28    wide "P1" label (two tiles)
 *   29..30    wide "HI" label (two tiles)
 *   31..38    wide "BRIDGE" label (four glyphs, two tiles each)
 *   39..46    wide "GAME" letters (16x8 px, two tiles per letter)
 *   58..59    wide "P2" label ($4F); the Atari swaps it for "HI" in two-player games
 *
 * These HUD tiles (0..46) live in pattern table $1000, shared with the
 * sprites (which start at SPRITE_TILE_BASE).  The playfield uses its own
 * table $0000: 0 solid land, 1 solid water, 2 solid black, 3..255 exact
 * river cache (253 slots).  main.c flips PPUCTRL bit 4 between the HUD and playfield
 * passes of the split.
 *
 * The river cache is filled at runtime: each visible 8x8 block is uploaded
 * from the generated scanline data, so bank edges and trees are exact.
 *
 * The small labels are the supplied Atari 4-pixel-font glyphs (charset codes
 * $43-$46 "BRIDGE", $5A "P1", $5B "HI"), doubled horizontally exactly like
 * the source's mode-6 text.  Gauge E / 1-2 / F and the plane lives icon are
 * sprite overlays taken from the same charset ($47/$49/$4A/$4B/$4D/$4E/$5C).
 */

#define TILE_BLANK              0u
#define TILE_DIGIT_BASE         1u      /* 10 black digits: 1..10 */
#define TILE_DIGIT_YELLOW_BASE  48u     /* 10 yellow digits: 48..57 */
#define TILE_GAUGE_BASE         11u     /* 16 tiles: 8 glyphs x 2 halves */
#define TILE_GAUGE_TL           (TILE_GAUGE_BASE + 0u)
#define TILE_GAUGE_T            (TILE_GAUGE_BASE + 2u)
#define TILE_GAUGE_TC           (TILE_GAUGE_BASE + 4u)
#define TILE_GAUGE_TR           (TILE_GAUGE_BASE + 6u)
#define TILE_GAUGE_BL           (TILE_GAUGE_BASE + 8u)
#define TILE_GAUGE_B            (TILE_GAUGE_BASE + 10u)
#define TILE_GAUGE_BC           (TILE_GAUGE_BASE + 12u)
#define TILE_GAUGE_BR           (TILE_GAUGE_BASE + 14u)
#define TILE_P1_WIDE            27u     /* 2 tiles */
#define TILE_HI_WIDE            29u     /* 2 tiles */
#define TILE_BRIDGE_WIDE        31u     /* 8 tiles: BR ID G E */
#define TILE_GAME_WIDE          39u     /* 8 tiles: G A M E halves */
#define TILE_P2_WIDE            58u     /* 2 tiles, right after the yellow digits */

/* The playfield owns its own 256-tile background pattern table ($0000,
 * PPUCTRL bit 4 = 0 during the playfield pass).  The HUD tiles above and all
 * sprites share the other table ($1000): the split handlers flip PPUCTRL
 * bit 4 per region, and sprites sit at SPRITE_TILE_BASE+ to stay clear of the
 * HUD IDs.  Playfield table layout: 0 land, 1 water, 2..255 river cache. */
#define TILE_LAND               0u
#define TILE_WATER              1u
#define TILE_BLACK              2u      /* solid black: the empty world at game start */
#define TILE_RIVER_BASE         3u
#define TILE_RIVER_COUNT        250u    /* exact per-row river cache */
/* Solid road tiles for the land margins beside the river (columns 0-3 and
 * 28-31) in the two tile rows of a road.  A road is 16 scanlines, the grey
 * body with a yellow centre line; the Atari recolours the land per scanline,
 * the NES uses its own BG palette 3.  ROAD_A is the first (lower) tile row,
 * ROAD_B the second.  See river_road_* in river.c. */
#define TILE_ROAD_A             253u
#define TILE_ROAD_B             254u    /* 255 is kept free: river_margin_id() uses 0xFF for "none" */

#define BG_TABLE_BIT            0x10u   /* PPUCTRL bit 4: BG pattern table */
#define SPRITE_TILE_BASE        64u     /* sprites share the HUD table */

/* Sprite tiles for the gauge overlay.
 *   8 = E, 9 = F, 10..13 = 16x14 "1/2", 14 + 16 = yellow level bar,
 *   15 = yellow lives plane ($5C).
 * Gauge labels use OAM slots 48..53; the bar uses slots 54..55 so it sits
 * behind them (lower OAM index wins).  Lives planes use slots 44..46. */
#define HUD_GAUGE_E_TILE        (SPRITE_TILE_BASE + 8u)
#define HUD_GAUGE_F_TILE        (SPRITE_TILE_BASE + 9u)
#define HUD_FRAC_TILE           (SPRITE_TILE_BASE + 10u)
#define HUD_FUEL_BAR_TILE       (SPRITE_TILE_BASE + 14u)
#define HUD_LIFE_TILE           (SPRITE_TILE_BASE + 15u)
#define HUD_FUEL_BAR_TILE2      (SPRITE_TILE_BASE + 16u)
#define HUD_LIFE_SLOT           44u
#define HUD_GAUGE_E_SLOT        48u
#define HUD_FRAC_SLOT           49u
#define HUD_GAUGE_F_SLOT        53u
#define HUD_FUEL_BAR_SLOT       54u
#define HUD_FUEL_BAR_SLOT2      55u
#define HUD_GAUGE_E_X           80u
#define HUD_GAUGE_F_X           136u
#define HUD_FRAC_X              104u
/* Screen Y of the HUD sprites (the HUD pass scrolls the nametable up by
 * HUD_SCROLL_Y, so these are 8 less than the tile-row-based positions). */
#define HUD_FRAC_Y              12u
#define HUD_GAUGE_LABEL_Y       18u
#define HUD_FUEL_BAR_Y          16u     /* just below the top markers */
#define HUD_FUEL_BAR_Y2         24u     /* ends just above the bottom rule */
#define HUD_FUEL_BAR_X_MIN      80u     /* empty, on the E marker */
#define HUD_FUEL_BAR_X_MAX      142u    /* full, on the F marker */
#define HUD_LIFE_X              160u
#define HUD_LIFE_Y              19u

/* ------------------------------------------------------------------------- */
/* Background palettes                                                       */
/* ------------------------------------------------------------------------- */
/*
 * Palette 0 (HUD): backdrop black, gray band, yellow highlights, white spare.
 * (Black text is pixel value 0; the backdrop is black like Atari COLBK so the
 * river's black hill pixels need no remapping.)
 * Palette 2 (playfield): black, bank green, black outline, water blue.
 * The HUD is a single palette so black and yellow text can share 16x16
 * attribute cells; the source uses exactly these two text colours.
 */

#define BG_COLOR_BLACK          0x0Fu
#define BG_COLOR_WHITE          0x30u
/* Playfield colours match the Atari registers (palette_altirra_ntsc.bin):
 * land $D2 (003900) -> NES $09, water $84 (123EB2) -> NES $01, outline $20
 * (0C0000) -> NES $07 (dark brown) for the hills only.  Bank outlines are
 * redrawn as pixel value 0 (black backdrop, see river_edge_black) so edges
 * stay black while the hills keep brown + black shades.  Colours were chosen
 * by Lab distance against both the Mesen default and NES-classic palettes. */
#define BG_COLOR_GREEN          0x09u
#define BG_COLOR_BROWN          0x07u
#define BG_COLOR_BLUE           0x11u   /* brighter than the Atari $84 on request */
#define BG_COLOR_YELLOW         0x28u
#define BG_COLOR_GRAY           0x00u
#define BG_COLOR_BRIGHT_GREEN   0x19u   /* land on odd levels ($D6, 008300) */

#define HUD_PALETTE             0u
#define PLAYFIELD_PALETTE       2u      /* odd levels: dark green land ($D2) */
#define PLAYFIELD_ALT_PALETTE   1u      /* even levels: bright green land ($D6) */
#define ROAD_PALETTE            3u      /* black, grey road, yellow line, water */

/* ------------------------------------------------------------------------- */
/* Playfield nametable                                                       */
/* ------------------------------------------------------------------------- */

#define PPU_NT0_BASE            0x2000u
#define PPU_NT0_ATTR            0x23C0u
#define PPU_NT1_BASE            0x2400u
#define PPU_NT1_ATTR            0x27C0u

#define PLAYFIELD_NT_BITS       0x01u   /* PPUCTRL bit 0 selects $2400 */

/* Screen X of a source-pixel X (the river row starts at nametable column 4). */
#define SOURCE_X_OFFSET         (RIVER_LEFT_COL_PX)
#define RIVER_LEFT_COL_PX       32u

#define RIVER_RING_ROWS         30u
#define RIVER_VIEW_TOP_ROW      3u      /* ring row at scroll 0 */
#define RIVER_SCROLL_HEIGHT     240u

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
/* HUD layout (nametable rows 0..3, columns 0..31)                           */
/* ------------------------------------------------------------------------- */
/*
 * Row 0 is blank padding, like the empty lines above the Atari HUD band;
 * the split leaves another 4 gray scanlines below row 3.
 *
 *   row 1:   P1          score          HI       hi-score
 *   row 2:   GAME        [ fuel box ]   BRIDGE   bridge#
 *   row 3:      game#    [ E 1/2 F | ]  lives
 *
 * The gauge box is two tile rows tall, so row 2 holds its top part and
 * row 3 its bottom part plus E/F, the fuel fraction and the level bar.
 */

#define HUD_P1_X                1u
#define HUD_P1_ROW              1u

#define HUD_SCORE_ROW           1u
#define HUD_SCORE_X             11u
#define HUD_SCORE_DIGITS        6u

#define HUD_HI_LABEL_X          20u
#define HUD_HI_ROW              1u
#define HUD_HI_X                25u

#define HUD_GAME_ROW            2u
#define HUD_GAME_X              1u
#define HUD_GAME_TILES          8u      /* four 16x8 letters */
#define HUD_GAME_DIGIT_ROW      3u
#define HUD_GAME_DIGIT_X        5u

#define HUD_GAUGE_ROW           2u
#define HUD_GAUGE_X             9u
#define HUD_GAUGE_W             10u     /* 5 cells x 2 tiles, two rows tall */
#define HUD_GAUGE_BOTTOM_ROW    3u

#define HUD_BRIDGE_ROW          2u
#define HUD_BRIDGE_X            20u
#define HUD_BRIDGE_TILES        8u
#define HUD_BRIDGE_DIGIT_X      29u     /* two tiles, 29..30 */
#define HUD_BRIDGE_DIGIT_MAX    99u

#define HUD_LIVES_ICON_MAX      3u

#endif /* CONFIG_H */
