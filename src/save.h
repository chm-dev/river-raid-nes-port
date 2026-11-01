#ifndef SAVE_H
#define SAVE_H

#include <stdint.h>
#include <gb/gb.h>

/* Hi-score persistence in cartridge SRAM (GBC build only, src/gbc/save_gbc.c).
 * `data` is 12 bytes: four 3-byte BCD scores, one per start level. */
void save_load(uint8_t *dst) BANKED;
void save_store(const uint8_t *src) BANKED;

#endif
