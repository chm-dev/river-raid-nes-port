/*
 * entities.h - enemy / fuel / explosion slots.
 *
 * Eight slots; an entity's type is the Atari object shape index (aux):
 * 7 jet, 8..11 helicopter, 12 big helicopter, 13 ship, 15 fuel depot.
 * 2..6 are the explosion frames (the same fragments the Atari uses for the
 * jet's death).  Positions are NES screen coordinates: x == Atari HPOS and
 * y is the top scanline of the shape; sprites above the HUD split are
 * hidden.
 */
#ifndef ENTITIES_H
#define ENTITIES_H

#include <nes/nes.h>
#include <stdint.h>

#define MAX_ENTITIES            8u
#define ENTITY_NONE             0u

#define ENT_AUX_EXPLODE_FIRST   2u
#define ENT_AUX_EXPLODE_LAST    6u
#define ENT_AUX_JET             7u
#define ENT_AUX_SHIP            13u
#define ENT_AUX_FUEL            15u

typedef struct {
    uint8_t type;       /* aux shape index; ENTITY_NONE = free */
    uint8_t x;
    uint8_t y;
    uint8_t flags;      /* bit 0: moving left */
    uint8_t aux;        /* move / animation timer */
} entity_t;

extern entity_t entities[MAX_ENTITIES];
extern uint8_t entity_count;

/* Results of entities_check_player(). */
#define ENT_HIT_NONE            0u
#define ENT_HIT_FUEL            1u
#define ENT_HIT_DEADLY          2u

void entities_init(void) BANKED;

/* Load the object / explosion / shot tiles and sprite palettes 2 and 3.
 * Call with the display off. */
void entities_init_graphics(void) BANKED;

/* Scroll with the river, patrol, spawn, animate and draw. */
void entities_update(void) BANKED;

/* Sprite tile of an aux shape (first tile) and of the player shot. */
uint8_t entities_tile(uint8_t aux) BANKED;
uint8_t entities_shot_tile(void) BANKED;

/* Shot box (2x6 at sx,sy) against live entities; destroys and scores the
 * first hit.  Returns non-zero on a hit. */
uint8_t entities_hit_shot(uint8_t sx, uint8_t sy) BANKED;

/* Jet box (8x14 at px,py) against live entities. */
uint8_t entities_check_player(uint8_t px, uint8_t py) BANKED;

/* Add a BCD score value in tens (atari_score_tens[]) to the HUD score. */
void entities_add_score(uint8_t bcd_tens) BANKED;

/* Remove everything (new life / new game). */
void entities_clear(void) BANKED;

#endif /* ENTITIES_H */
