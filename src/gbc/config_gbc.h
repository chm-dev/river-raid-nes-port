/*
 * config_gbc.h - GBC replacements for the NES playfield constants in config.h.
 *
 * The river ring lives in BG map 0 ($9800, 32x32 tiles) and wraps at 256 px,
 * the same as SCY, so the ring scroll value is also the hardware scroll.
 * PPU_NT1_ATTR is deliberately not defined: the NES attribute table has no
 * counterpart here (colour is the BG attribute map in VRAM bank 1), so any
 * code that still writes it fails to compile instead of corrupting map row 30.
 */
#ifndef CONFIG_GBC_H
#define CONFIG_GBC_H

#define PPU_NT1_BASE            0x9800u

/* The 24 river tile columns sit in map columns 0..23, but logic X stays the
 * Atari HPOS, where river pixel 0 is at X = 32 (as on the NES).  The camera
 * subtracts it when it draws sprites (see gbc/main.c). */
#define RIVER_LEFT_COL_PX       32u
#define SOURCE_X_OFFSET         (RIVER_LEFT_COL_PX)

#define RIVER_RING_ROWS         32u
#define RIVER_SCROLL_HEIGHT     256u

/* The 160x144 screen shows a 128 px playfield (the 16 px HUD is the Window at
 * the bottom).  The view is bottom-anchored: screen line 0 shows this logic
 * scanline.  It also sets the jet's height on screen, so keep it tunable. */
#define GBC_VIEW_LINES          128u
#define GBC_VIEW_TOP_SCANLINE   (240u - GBC_VIEW_LINES)

#endif /* CONFIG_GBC_H */
