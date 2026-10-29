#ifndef STUB_NES_H
#define STUB_NES_H
#include <stdint.h>
#define NO_OVERLAY_LOCALS
void set_bkg_data(uint8_t first, uint8_t n, const uint8_t *d);
void set_vram_byte(uint8_t *addr, uint8_t v);
#endif
