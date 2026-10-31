/*
 * stubs_m1.c - stand-ins for modules that are not in the milestone 1 link.
 * Drop each stub from the build as its real module is linked.
 */
#include <gb/gb.h>

#include "entities.h"

/* river.c calls this after each raw row.  With no entities the generator's
 * object inputs stay at "none" (river_obj_count = 0x0B), so nothing to do. */
void entities_row(void) BANKED {
}
