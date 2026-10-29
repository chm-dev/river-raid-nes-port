#ifndef STUB_NES_H
#define STUB_NES_H
#include <stdint.h>
#define NO_OVERLAY_LOCALS
#define BANKED
void set_bkg_data(uint8_t first, uint8_t n, const uint8_t *d);
void set_vram_byte(uint8_t *addr, uint8_t v);
typedef uint8_t palette_color_t;
typedef struct { uint8_t y, tile; union { uint8_t attr, prop; }; uint8_t x; } OAM_item_t;
void set_bkg_palette_entry(uint8_t palette, uint8_t entry, palette_color_t rgb_data);
extern volatile OAM_item_t shadow_OAM[64];
void set_sprite_palette(uint8_t first, uint8_t n, const palette_color_t *p);
void set_sprite_data(uint8_t first, uint8_t n, const uint8_t *d);
#define S_FLIPX 0x40
#endif
