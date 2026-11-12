/* The living world's density guards (KB living-world-density): the game's unchecked writes and native-only gates that
 * more online enemies reach first. Each is a switched site (co_switch.h): the guard on a complex mission, the game's
 * code on any other level and on the ship. With them the living world runs its density budget; without, one budget. */
#ifndef CO_DENSITY_H
#define CO_DENSITY_H

#include "co.h"

#define CO_DENSITY_RAY_LIMIT 3584u /* of the ray queue's 4096 */

/* Once, with the living world's installs. NULL, or why the guards stay off (the game's code is then as it was). */
const char* co_density_install(uintptr_t game);
/* The mission in play is complex (1) or not: every switched site's path, and, on the first complex level, the
 * budget's pair table (HellForge's own guard there is taken as it is). */
void co_density_live(int complex);
/* 1 while the guards serve complex missions: installed, and the pair table guarded or not reached yet. */
int co_density_on(void);
/* The game thread's tick: the living world's units the native gates leave out, counted on a complex mission only. */
void co_density_tick(float dt);

#endif
