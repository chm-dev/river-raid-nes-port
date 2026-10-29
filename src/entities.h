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

#define MAX_ENTITIES            11u     /* the Atari has 11 object slots */
#define ENTITY_NONE             0u

#define ENT_AUX_EXPLODE_FIRST   2u
#define ENT_AUX_EXPLODE_LAST    6u
#define ENT_AUX_JET             7u
#define ENT_AUX_SHIP            13u
#define ENT_AUX_BRIDGE          14u
#define ENT_AUX_FUEL            15u
/* The bridge tank (Atari P1, $38 state / $39 X / $3A Y) and the shell it
 * fires (P0, $50 progress).  They are not shootable objects: entities.c keeps
 * the Atari state in ent_aux (tank: $38, shell: $50). */
#define ENT_AUX_TANK            16u
#define ENT_AUX_SHELL           17u

/* Slot arrays.  type is the aux shape index (ENTITY_NONE = free); flags bit 0
 * = moving left (Atari status bit 3), bit 1 = awake (bit 6: has been picked
 * by the wake-up lottery), bit 2 = turn latch (bit 4); aux is an animation
 * timer. */
#define ENT_F_LEFT              0x01u
#define ENT_F_AWAKE             0x02u
#define ENT_F_LATCH             0x04u
/* Tank only: waiting for the bridge bar to get a position; the bridge was
 * destroyed ($2E != 0: stands still); its shell is flying ($50 >= 0). */
#define ENT_F_WAIT              0x08u
#define ENT_F_STOP              0x10u
#define ENT_F_SHOOT             0x20u
extern uint8_t ent_type[MAX_ENTITIES];
extern uint8_t ent_x[MAX_ENTITIES];
extern uint8_t ent_y[MAX_ENTITIES];
extern uint8_t ent_flags[MAX_ENTITIES];
extern uint8_t ent_aux[MAX_ENTITIES];
extern uint8_t entity_count;

/* Results of entities_check_player(). */
#define ENT_HIT_NONE            0u
#define ENT_HIT_FUEL            1u
#define ENT_HIT_DEADLY          2u

void entities_init(void) BANKED;

/* Load the object / explosion / shot tiles and sprite palettes 2 and 3.
 * Call with the display off. */
void entities_init_graphics(void) BANKED;

/* Scroll with the river, patrol, animate and draw. */
void entities_update(void) BANKED;

/* The Atari spawn block ($AB80-$AE96), called by the river generator after
 * each raw row it renders (see river.c). */
void entities_row(void) BANKED;

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
