/* A complex mission's director patrols, off while the living world's squads take their place. The director update
 * (0x94EB90) reads the row's three caps into registers, scales them (0x959650, from the copy at 0x94FE38) and compares
 * the scaled cap once: at the gate below, against the patrol units alive. Nothing else reads it, so taking the gate's
 * jump is what a cap of 0 does, and the difficulty rows stay the game's for every level, the war table and the ship. */
#ifndef CO_PATROLS_H
#define CO_PATROLS_H

#include "co.h"

#define CO_PATROL_CMP 0x9513A1u  /* cmp [rax+0x614], r12d: alive patrol units against the scaled cap */
#define CO_PATROL_GATE 0x9513A8u /* jae 0x95258A: past the loop of patrol requests and placement queries */
#define CO_PATROL_NEXT 0x9513AEu /* test r13d, r13d: the loop's way in; it sets the flags again */
#define CO_PATROL_SKIP 0x95258Au /* test r13d, r13d: behind the loop */
#define CO_PATROL_STUB 24u
extern const uint8_t kCoPatrolCmp[7], kCoPatrolGate[6], kCoPatrolNext[3], kCoPatrolSkip[3];

/* NULL when the four sites hold the game's bytes. Reads only. */
const char* co_patrols_check(uintptr_t game);
/* The gate becomes a jump to the stub, whose flag starts at 0. NULL, or why; the gate is as it was after a failure. */
const char* co_patrols_install(uintptr_t game);
/* 1 while the mission in play is complex and the living world runs it; nothing before the install. */
void co_patrols_live(int off);
/* The stub for `code`: the game's jae, then the same jump while [flag] is not 0, else on into the loop. */
size_t co_patrols_stub(uint8_t* out, uintptr_t code, uintptr_t game, uintptr_t flag);

#endif
