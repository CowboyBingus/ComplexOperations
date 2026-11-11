/* The tactical map's objective markers. The map object (HUD + 0x1A0E28, builder 0x18A5580) holds 16 marker slots and
 * 10 trackers in itself, and a complex mission keeps them: its up to 30 objectives share the 16 that HellForge's level
 * 11 guards. Moving the arrays would mean rewriting the updater's code per mission, and that updater (0x18B42F0) runs
 * in the frame's "during_world_update_work" job (0xAB41F0) beside the game thread, so the mod writes nothing here. */
#ifndef CO_MARKERS_H
#define CO_MARKERS_H

#include "co.h"

/* NULL when the builder and the updater hold the game's 16 and 10, and the entity pass and the route marker go through
 * HellForge's guards of a 17th slot (its stubs' form, their two ways out). Reads only. */
const char* co_markers_check(uintptr_t game);

#endif
