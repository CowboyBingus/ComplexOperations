/* A reinforcement hellpod's start on a complex level. The game starts a Reinforce pod at its player's destroyer (game
 * 0x929E40, the only caller 0x930A68 writing the pod's networked start) and the pod's first phase homes on the drop
 * point sideways at up to 1000 m/s (pod update 0x92DEC5..0x92E1FB). The destroyer stays above the middle of the map
 * while an XL drop point can be 8 km from it: the player's camera crosses the map, and the world streams for the
 * path. The guard keeps the start's direction and height and brings it to at most CO_PODSTART_REACH from the drop
 * point, the game's own distance on a map of 2 km. The initial drop starts above its point and is left as it is. */
#ifndef CO_PODSTART_H
#define CO_PODSTART_H

#include "co.h"

#define CO_PODSTART 0x929E40u  /* game: (manager, out start, entity record, params) -> out; params+0x38 the drop point */
#define CO_PODSTART_TARGET 0x38u
#define CO_PODSTART_REACH 1024.0f

/* Brings `start` to at most `reach` metres from `target` across the ground (x, y), along the same line; its height
 * stays. 1 when it moved it. */
int co_podstart_cap(float* start, const float* target, float reach);
/* Hooks the start once. `scale` answers the live map's scale (1 off complex levels): the start is changed only above
 * 1. NULL, or why the game's start stays. */
const char* co_podstart_install(uintptr_t game, float (*scale)(void));

#ifdef CO_TEST
void co_podstart_test_reset(void);
#endif

#endif
